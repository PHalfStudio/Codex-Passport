#include "codex_ble_hid.h"
#include "codex_ble_link.h"
#include "codex_protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_npl.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"
#include <string.h>

static const char *TAG = "codex_ble";
#ifdef CONFIG_CODEX_DIAL_LEGACY_ALIAS
#define CODEX_DIAL_ALIAS true
#else
#define CODEX_DIAL_ALIAS false
#endif
typedef enum { WORK_RX, WORK_KEY, WORK_BATTERY } work_kind_t;
typedef struct {
    work_kind_t kind;
    uint32_t generation;
    uint64_t time_ms;
    union {
        struct { uint8_t data[64]; uint8_t length; } rx;
        codex_action_t key;
    } value;
} work_t;
static QueueHandle_t queue;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static codex_ble_link_t link;
static codex_state_t published = {.battery = -1, .charging = -1}, worker_state;
static codex_protocol_t protocol;
static uint16_t input_handle, battery_handle;
static uint8_t address_type;
static bool started, initialized, abort_pending;
static uint32_t rx_overflow, rx_rejected, tx_drop, send_generation;
static uint32_t worker_generation, worker_seen_overflow, abort_generation;
static uint16_t abort_connection;
static codex_key_id_t remote_pressed = CODEX_NO_KEY;
static struct ble_npl_callout adv_retry;
static struct ble_npl_event abort_event;
static struct ble_npl_event repair_event;
static bool repair_requested, repair_cache_ready;
static int repair_peer_count;
static ble_addr_t repair_peers[1];
static struct ble_store_value_sec repair_peer_security[1];
static bool repair_security_present[1];

// ESP-IDF NimBLE examples expose this initializer without a public declaration.
void ble_store_config_init(void);
static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static codex_ble_link_t link_copy(void)
{
    portENTER_CRITICAL(&lock); codex_ble_link_t copy = link; portEXIT_CRITICAL(&lock);
    return copy;
}
void codex_ble_snapshot(codex_state_t *out)
{
    portENTER_CRITICAL(&lock);
    *out = published;
    out->encrypted = link.encrypted; out->subscribed = link.subscribed;
    out->mtu = link.mtu; out->generation = link.generation;
    out->ready = codex_ble_link_ready(&link);
    out->stats.rx_dropped += rx_overflow;
    out->stats.rx_errors += rx_rejected;
    out->stats.tx_dropped += tx_drop;
    portEXIT_CRITICAL(&lock);
}
static void publish(void)
{
    portENTER_CRITICAL(&lock);
    bool changed = memcmp(published.agents, worker_state.agents, sizeof(published.agents)) ||
        memcmp(&published.ambient, &worker_state.ambient, sizeof(published.ambient)) ||
        memcmp(&published.keys, &worker_state.keys, sizeof(published.keys)) ||
        memcmp(&published.stats, &worker_state.stats, sizeof(published.stats)) ||
        memcmp(published.last_method, worker_state.last_method, sizeof(published.last_method));
    // Link and battery belong to callbacks / the sampler, lighting to the worker.
    memcpy(published.agents, worker_state.agents, sizeof(published.agents));
    published.ambient = worker_state.ambient; published.keys = worker_state.keys;
    published.stats = worker_state.stats;
    memcpy(published.last_method, worker_state.last_method, sizeof(published.last_method));
    if (changed) ++published.revision;
    portEXIT_CRITICAL(&lock);
}
static void host_abort(struct ble_npl_event *event)
{
    (void)event;
    portENTER_CRITICAL(&lock);
    codex_ble_link_t l = link;
    uint16_t target = abort_connection; uint32_t generation = abort_generation;
    portEXIT_CRITICAL(&lock);
    if (l.connection != BLE_HS_CONN_HANDLE_NONE && l.connection == target && l.generation == generation) {
        int rc = ble_gap_terminate(l.connection, BLE_ERR_REM_USER_CONN_TERM);
        if (rc && rc != BLE_HS_ENOTCONN) {
            ESP_LOGE(TAG, "Termination failed rc=%d; scheduling stack reset", rc);
            ble_hs_sched_reset(rc);
        }
    }
    portENTER_CRITICAL(&lock);
    if (abort_generation == generation) abort_pending = false;
    portEXIT_CRITICAL(&lock);
}
void codex_ble_abort(void)
{
    if (!initialized) return;
    portENTER_CRITICAL(&lock);
    link.host_active = false;
    link.closing = true; // Host Exit Suspend cannot reopen a link being terminated.
    ++link.generation;
    abort_connection = link.connection; abort_generation = link.generation;
    published.connection = CODEX_ERROR;
    abort_pending = true;
    portEXIT_CRITICAL(&lock);
    if (!ble_npl_event_is_queued(&abort_event))
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &abort_event);
}
static bool notify_report(void *unused, const uint8_t body[CODEX_REPORT_BODY])
{
    (void)unused;
    codex_ble_link_t l = link_copy();
    // Input transport readiness is independent of receiving the first host RPC.
    if (l.generation != send_generation || !codex_ble_link_ready(&l)) return false;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(body, CODEX_REPORT_BODY);
    if (!om) { codex_ble_abort(); return false; }
    int rc = ble_gatts_notify_custom(l.connection, input_handle, om);
    // NimBLE consumes om even on failure. Never free or retry it twice.
    if (rc) {
        ESP_LOGW(TAG, "Input notification failed rc=%d", rc);
        codex_ble_abort();
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(4)); // Worker only: preserve fragment ordering
    return true;
}
bool codex_ble_action(const codex_action_t *action)
{
    const codex_key_t *key = codex_key_by_id(action->id);
    if (!key || key->kind == CODEX_KEY_LOCAL) return false;
    codex_ble_link_t l = link_copy();
    if (!queue || !codex_ble_link_ready(&l)) return false;
    work_t work = {.kind = WORK_KEY, .generation = l.generation, .time_ms = action->time_ms};
    work.value.key = *action;
    if (xQueueSend(queue, &work, 0) == pdTRUE) return true;
    portENTER_CRITICAL(&lock); ++tx_drop; portEXIT_CRITICAL(&lock);
    if (!action->down) codex_ble_abort();
    return false;
}
void codex_ble_battery(int percentage, int charging)
{
    portENTER_CRITICAL(&lock);
    published.battery = percentage >= 0 && percentage <= 100 ? percentage : -1;
    published.charging = charging >= 0 && charging <= 1 ? charging : -1;
    ++published.revision;
    portEXIT_CRITICAL(&lock);
    if (queue && percentage >= 0) {
        work_t w = {.kind = WORK_BATTERY};
        (void)xQueueSend(queue, &w, 0);
    }
}
static int append(struct os_mbuf *om, const void *bytes, size_t len)
{
    return os_mbuf_append(om, bytes, len) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
enum { ATTR_INFO, ATTR_MAP, ATTR_INPUT, ATTR_OUTPUT, ATTR_CONTROL, ATTR_MODE,
       ATTR_REF_INPUT, ATTR_REF_OUTPUT, ATTR_MANUFACTURER, ATTR_MODEL, ATTR_VERSION,
       ATTR_PNP, ATTR_BATTERY };
static int access_attr(uint16_t connection, uint16_t attribute,
                       struct ble_gatt_access_ctxt *ctx, void *arg)
{
    (void)attribute;
    unsigned kind = (unsigned)(uintptr_t)arg;
    bool write = ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR;
    if (write) {
        codex_ble_link_t l = link_copy();
        if (l.connection != connection || !l.encrypted) return BLE_ATT_ERR_INSUFFICIENT_ENC;
        if (l.closing) return BLE_ATT_ERR_UNLIKELY;
        size_t size = OS_MBUF_PKTLEN(ctx->om);
        if (kind == ATTR_OUTPUT) {
            if (size != 63 && size != 64) {
                portENTER_CRITICAL(&lock); ++rx_rejected; portEXIT_CRITICAL(&lock);
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            work_t w = {.kind = WORK_RX, .generation = l.generation, .time_ms = now_ms()};
            w.value.rx.length = (uint8_t)size;
            if (os_mbuf_copydata(ctx->om, 0, size, w.value.rx.data)) return BLE_ATT_ERR_UNLIKELY;
            if (xQueueSend(queue, &w, 0) != pdTRUE) {
                portENTER_CRITICAL(&lock); ++rx_overflow; portEXIT_CRITICAL(&lock);
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return 0;
        }
        uint8_t value;
        if (size != 1 || os_mbuf_copydata(ctx->om, 0, 1, &value)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (kind == ATTR_MODE) return value == 1 ? 0 : BLE_ATT_ERR_REQ_NOT_SUPPORTED;
        if (kind == ATTR_CONTROL) {
            if (value > 1) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            portENTER_CRITICAL(&lock);
            link.suspended = value == 0;
            if (value == 0) ++link.generation;
            portEXIT_CRITICAL(&lock);
            return 0;
        }
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    const uint8_t info[] = {0x11,0x01,0x00,0x02};
    const uint8_t input_ref[] = {6,1}, output_ref[] = {6,2}, mode = 1;
    static const uint8_t empty[CODEX_REPORT_BODY] = {2,0};
    switch (kind) {
    case ATTR_INFO: return append(ctx->om, info, sizeof(info));
    case ATTR_MAP: return append(ctx->om, codex_report_map, codex_report_map_size);
    case ATTR_INPUT: case ATTR_OUTPUT: return append(ctx->om, empty, sizeof(empty));
    case ATTR_REF_INPUT: return append(ctx->om, input_ref, sizeof(input_ref));
    case ATTR_REF_OUTPUT: return append(ctx->om, output_ref, sizeof(output_ref));
    case ATTR_MODE: return append(ctx->om, &mode, 1);
    case ATTR_MANUFACTURER: return append(ctx->om, "Work Louder", 11);
    case ATTR_MODEL: return append(ctx->om, "Codex Passport", 14);
    case ATTR_VERSION: return append(ctx->om, CODEX_VERSION, strlen(CODEX_VERSION));
    case ATTR_PNP: return append(ctx->om, codex_pnp_id, sizeof(codex_pnp_id));
    case ATTR_BATTERY: {
        portENTER_CRITICAL(&lock); int battery = published.battery; portEXIT_CRITICAL(&lock);
        if (battery < 0) return BLE_ATT_ERR_UNLIKELY;
        uint8_t percentage = (uint8_t)battery;
        return append(ctx->om, &percentage, 1);
    }
    default: return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
}
#define ARG(k) ((void *)(uintptr_t)(k))
#define READ_CHR(uuid16, k) { .uuid = BLE_UUID16_DECLARE(uuid16), .access_cb = access_attr, .arg = ARG(k), .flags = BLE_GATT_CHR_F_READ }
static struct ble_gatt_dsc_def input_descriptors[] = {
    {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
     .access_cb = access_attr, .arg = ARG(ATTR_REF_INPUT)}, {0}
};
static struct ble_gatt_dsc_def output_descriptors[] = {
    {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ,
     .access_cb = access_attr, .arg = ARG(ATTR_REF_OUTPUT)}, {0}
};
static const struct ble_gatt_svc_def battery_services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180F),
     .characteristics = (const struct ble_gatt_chr_def[]) {
        {.uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = access_attr, .arg = ARG(ATTR_BATTERY),
         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
         .min_key_size = 16, .val_handle = &battery_handle}, {0}}}, {0}
};
static const struct ble_gatt_svc_def *hid_includes[] = {&battery_services[0], NULL};
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180A),
     .characteristics = (const struct ble_gatt_chr_def[]) {
        READ_CHR(0x2A29, ATTR_MANUFACTURER), READ_CHR(0x2A24, ATTR_MODEL),
        READ_CHR(0x2A26, ATTR_VERSION), READ_CHR(0x2A50, ATTR_PNP), {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x1812), .includes = hid_includes,
     .characteristics = (const struct ble_gatt_chr_def[]) {
        READ_CHR(0x2A4A, ATTR_INFO), READ_CHR(0x2A4B, ATTR_MAP),
        {.uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = access_attr, .arg = ARG(ATTR_CONTROL),
         .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC, .min_key_size = 16},
        {.uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = access_attr, .arg = ARG(ATTR_MODE),
         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
         .min_key_size = 16},
        {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = access_attr, .arg = ARG(ATTR_INPUT),
         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
         .min_key_size = 16, .descriptors = input_descriptors, .val_handle = &input_handle},
        {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = access_attr, .arg = ARG(ATTR_OUTPUT),
         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                  BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
         .min_key_size = 16, .descriptors = output_descriptors}, {0}}}, {0}
};
static int gap_event(struct ble_gap_event *ev, void *arg);
static void schedule_repair(void)
{
    if (!ble_npl_event_is_queued(&repair_event))
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &repair_event);
}
static void repair_finish(int rc)
{
    portENTER_CRITICAL(&lock);
    repair_requested = false; abort_pending = false;
    published.pairing_reset = rc ? CODEX_PAIR_FAILED : CODEX_PAIR_DONE;
    published.last_error = rc;
    if (rc) published.connection = CODEX_ERROR;
    ++published.revision;
    portEXIT_CRITICAL(&lock);
    if (rc) ESP_LOGE(TAG, "Pairing reset failed rc=%d; retry from local sidebar", rc);
    else {
        repair_cache_ready = false;
        memset(repair_peer_security, 0, sizeof(repair_peer_security));
        ESP_LOGI(TAG, "Pairing store cleared and reloaded; forget device on Mac before pairing");
    }
}
static void advertise(struct ble_npl_event *event);
static int repair_persistent_clean(void)
{
    // Inspect entry names only, never log/read bond-key blobs or erase UI NVS.
    nvs_iterator_t iterator = NULL;
    esp_err_t rc = nvs_entry_find("nvs", "nimble_bond", NVS_TYPE_ANY, &iterator);
    while (rc == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(iterator, &info);
        if (strncmp(info.key, "local_irk_", 10)) {
            nvs_release_iterator(iterator);
            return BLE_HS_ESTORE_FAIL;
        }
        rc = nvs_entry_next(&iterator);
    }
    nvs_release_iterator(iterator);
    return rc == ESP_ERR_NVS_NOT_FOUND ? 0 : rc;
}
static void repair_host(struct ble_npl_event *event)
{
    (void)event;
    portENTER_CRITICAL(&lock); bool pending = repair_requested; portEXIT_CRITICAL(&lock);
    if (!pending) return;
    (void)ble_gap_adv_stop();
    ble_npl_callout_stop(&adv_retry);
    codex_ble_link_t l = link_copy();
    if (l.connection != BLE_HS_CONN_HANDLE_NONE) {
        int rc = ble_gap_terminate(l.connection, BLE_ERR_REM_USER_CONN_TERM);
        if (rc && rc != BLE_HS_ENOTCONN) repair_finish(rc);
        else if (rc == BLE_HS_ENOTCONN) {
            portENTER_CRITICAL(&lock); codex_ble_link_reset(&link, BLE_HS_CONN_HANDLE_NONE); portEXIT_CRITICAL(&lock);
            schedule_repair();
        }
        return; // Continue only after the real disconnection callback.
    }
    if (!repair_cache_ready) {
        int rc = ble_store_util_bonded_peers(repair_peers, &repair_peer_count, 1);
        if (rc) { repair_finish(rc); return; }
        for (int j = 0; j < repair_peer_count; ++j) {
            struct ble_store_key_sec key = {.peer_addr = repair_peers[j]};
            repair_security_present[j] = ble_store_read_peer_sec(&key, &repair_peer_security[j]) == 0;
        }
        repair_cache_ready = true;
    }
    for (int j = 0; j < repair_peer_count; ++j) {
        int rc = ble_gap_unpair(&repair_peers[j]);
        if (rc && rc != BLE_HS_ENOENT) { repair_finish(rc); return; }
        if (repair_security_present[j] && repair_peer_security[j].irk_present) {
            uint8_t rpa[6];
            rc = ble_gap_rd_local_resolv_addr(repair_peers[j].type & 1U, &repair_peers[j], rpa);
            // IDF 5.5.3 may return success from unpair after a controller error.
            // Preserve this peer for an explicit retry rather than reporting success.
            if (!rc) {
                int restore_rc = ble_store_write_peer_sec(&repair_peer_security[j]);
                repair_finish(restore_rc ? restore_rc : BLE_HS_EBUSY); return;
            }
            if (rc != BLE_HS_HCI_ERR(BLE_ERR_UNK_CONN_ID)) { repair_finish(rc); return; }
        }
    }
    const int types[] = {BLE_STORE_OBJ_TYPE_OUR_SEC, BLE_STORE_OBJ_TYPE_PEER_SEC,
                         BLE_STORE_OBJ_TYPE_CCCD, BLE_STORE_OBJ_TYPE_CSFC, BLE_STORE_OBJ_TYPE_PEER_ADDR};
    for (unsigned j = 0; j < sizeof(types)/sizeof(types[0]); ++j) {
        union ble_store_key key = {0};
        if (types[j] == BLE_STORE_OBJ_TYPE_CCCD) key.cccd.peer_addr = *BLE_ADDR_ANY;
        else if (types[j] == BLE_STORE_OBJ_TYPE_CSFC) key.csfc.peer_addr = *BLE_ADDR_ANY;
        else if (types[j] == BLE_STORE_OBJ_TYPE_PEER_ADDR) key.rpa_rec.peer_rpa_addr = *BLE_ADDR_ANY;
        else key.sec.peer_addr = *BLE_ADDR_ANY;
        int rc = ble_store_util_delete_all(types[j], &key);
        if (rc) { repair_finish(rc); return; }
    }
    // Reload the standard persistent store, then verify it is empty. Keep local IRK.
    ble_store_config_init();
    for (unsigned j = 0; j < sizeof(types)/sizeof(types[0]); ++j) {
        int count = 0, rc = ble_store_util_count(types[j], &count);
        if (rc || count) { repair_finish(rc ? rc : BLE_HS_ESTORE_FAIL); return; }
    }
    int persistent_rc = repair_persistent_clean();
    if (persistent_rc) { repair_finish(persistent_rc); return; }
    repair_finish(0);
    advertise(NULL);
}
esp_err_t codex_ble_repair(void)
{
    portENTER_CRITICAL(&lock);
    if (!initialized) {
        published.pairing_reset = CODEX_PAIR_FAILED;
        portEXIT_CRITICAL(&lock); return ESP_ERR_INVALID_STATE;
    }
    if (repair_requested) { portEXIT_CRITICAL(&lock); return ESP_ERR_INVALID_STATE; }
    repair_requested = true; link.host_active = false; link.closing = true; ++link.generation;
    published.pairing_reset = CODEX_PAIR_PENDING; ++published.revision;
    portEXIT_CRITICAL(&lock);
    schedule_repair();
    return ESP_OK;
}
static void advertise(struct ble_npl_event *event)
{
    (void)event;
    codex_ble_link_t l = link_copy();
    portENTER_CRITICAL(&lock); bool repairing = repair_requested; portEXIT_CRITICAL(&lock);
    if (!started || repairing || l.connection != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) return;
    const ble_uuid16_t hid = BLE_UUID16_INIT(0x1812);
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)"Codex Micro"; fields.name_len = 11; fields.name_is_complete = 1;
    fields.appearance = 0x03C0; fields.appearance_is_present = 1;
    fields.uuids16 = &hid; fields.num_uuids16 = 1; fields.uuids16_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    struct ble_gap_adv_params params = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    if (!rc) rc = ble_gap_adv_start(address_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    portENTER_CRITICAL(&lock);
    published.connection = rc ? CODEX_ERROR : CODEX_ADVERTISING;
    published.last_error = rc; ++published.revision;
    portEXIT_CRITICAL(&lock);
    if (rc) {
        ESP_LOGW(TAG, "Advertising failed rc=%d; retry in 1 s", rc);
        ble_npl_callout_reset(&adv_retry, ble_npl_time_ms_to_ticks32(1000));
    } else ESP_LOGI(TAG, "Advertising Codex Micro (personal interoperability firmware)");
}
static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status) { advertise(NULL); break; }
        portENTER_CRITICAL(&lock);
        codex_ble_link_reset(&link, ev->connect.conn_handle);
        published.connection = CODEX_CONNECTED; ++published.revision;
        portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "BLE connected; host integration not yet verified");
        {
            int rc = ble_gap_security_initiate(ev->connect.conn_handle);
            if (rc && rc != BLE_HS_EALREADY) {
                ESP_LOGW(TAG, "Security initiation rc=%d", rc); codex_ble_abort();
            }
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        portENTER_CRITICAL(&lock);
        codex_ble_link_reset(&link, BLE_HS_CONN_HANDLE_NONE);
        published.connection = CODEX_DISCONNECTED; ++published.revision;
        portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "Disconnected reason=%d; clearing pending input", ev->disconnect.reason);
        portENTER_CRITICAL(&lock); bool repairing = repair_requested; portEXIT_CRITICAL(&lock);
        if (repairing) schedule_repair(); else advertise(NULL);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc desc;
        int rc = ble_gap_conn_find(ev->enc_change.conn_handle, &desc);
        bool secure = !ev->enc_change.status && !rc && desc.sec_state.encrypted && desc.sec_state.key_size == 16;
        portENTER_CRITICAL(&lock);
        if (link.connection == ev->enc_change.conn_handle) {
            link.encrypted = secure;
            if (!secure) { ++link.generation; link.host_active = false; }
            ++published.revision;
        }
        portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "Encryption %s status=%d authenticated=%d key_bytes=%u", secure ? "ready" : "failed",
                 ev->enc_change.status, !rc && desc.sec_state.authenticated, rc ? 0 : desc.sec_state.key_size);
        if (!secure) codex_ble_abort();
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == input_handle) {
            bool revoked = false;
            portENTER_CRITICAL(&lock);
            if (link.connection == ev->subscribe.conn_handle) {
                revoked = link.subscribed && !ev->subscribe.cur_notify;
                link.subscribed = ev->subscribe.cur_notify != 0;
                if (!link.subscribed) { ++link.generation; link.host_active = false; published.connection = CODEX_CONNECTED; }
                ++published.revision;
            }
            portEXIT_CRITICAL(&lock);
            ESP_LOGI(TAG, "Input notification subscription=%d", ev->subscribe.cur_notify);
            // With no notification path a release cannot reach the host. End this
            // link instead of leaving a previously delivered press held indefinitely.
            if (revoked) codex_ble_abort();
        }
        break;
    case BLE_GAP_EVENT_MTU:
        portENTER_CRITICAL(&lock);
        if (link.connection == ev->mtu.conn_handle) { link.mtu = ev->mtu.value; ++published.revision; }
        portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "ATT MTU=%u (minimum 66 for vendor reports)", ev->mtu.value);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // The current peer explicitly requests pairing again after forgetting us.
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &desc) ||
            ble_store_util_delete_peer(&desc.peer_id_addr)) return BLE_GAP_REPEAT_PAIRING_IGNORE;
        ESP_LOGI(TAG, "Replacing current peer bond on explicit re-pair request");
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(NULL); break;
    default: break;
    }
    return 0;
}
static void reset(int reason)
{
    portENTER_CRITICAL(&lock);
    codex_ble_link_reset(&link, BLE_HS_CONN_HANDLE_NONE);
    published.connection = CODEX_ERROR; published.last_error = reason; ++published.revision;
    portEXIT_CRITICAL(&lock);
    ESP_LOGW(TAG, "NimBLE reset reason=%d", reason);
}
static void sync_host(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &address_type);
    if (rc) reset(rc);
    else {
        portENTER_CRITICAL(&lock); bool repairing = repair_requested; portEXIT_CRITICAL(&lock);
        if (repairing) schedule_repair(); else advertise(NULL);
    }
}
static void host_task(void *unused)
{
    (void)unused;
    nimble_port_run();
    // Lifetime application: no screen teardown or unsupported stop/restart path.
    reset(BLE_HS_EDISABLED);
    vTaskDelete(NULL);
}
static void worker_reconcile(const codex_ble_link_t *l)
{
    portENTER_CRITICAL(&lock);
    uint32_t overflow = rx_overflow;
    worker_state.battery = published.battery;
    worker_state.charging = published.charging;
    portEXIT_CRITICAL(&lock);
    if (worker_generation != l->generation || overflow != worker_seen_overflow) {
        codex_protocol_reset(&protocol);
        if (worker_generation != l->generation) {
            remote_pressed = CODEX_NO_KEY;
            for (unsigned i = 0; i < CODEX_AGENTS; ++i) worker_state.agents[i].known = false;
            worker_state.ambient.known = worker_state.keys.known = false;
            worker_state.last_method[0] = 0;
        }
        worker_generation = l->generation; worker_seen_overflow = overflow;
        publish();
    }
}
static void worker_process(const work_t *work)
{
    work_t w = *work;
    codex_ble_link_t l = link_copy();
    // Recheck AFTER the queue wait: a new connection can arrive while blocked.
    worker_reconcile(&l);
    send_generation = l.generation;
    if (w.kind == WORK_BATTERY) {
        if (l.encrypted && battery_handle) ble_gatts_chr_updated(battery_handle);
        return;
    }
    if (w.generation != l.generation) {
        if (w.kind == WORK_RX) ++worker_state.stats.rx_dropped;
        else ++worker_state.stats.tx_dropped;
        publish(); return;
    }
    if (w.kind == WORK_RX) {
        uint32_t before = worker_state.stats.messages_rx;
        codex_protocol_receive(&protocol, w.value.rx.data, w.value.rx.length, w.time_ms);
        if (worker_state.stats.messages_rx != before) {
            portENTER_CRITICAL(&lock);
            if (link.generation == w.generation && link.encrypted && !link.closing && !abort_pending && !repair_requested) {
                link.host_active = true; published.connection = CODEX_HOST_ACTIVE;
            }
            portEXIT_CRITICAL(&lock);
            ESP_LOGI(TAG, "RPC method=%s", worker_state.last_method);
        }
    } else if (w.kind == WORK_KEY) {
        const codex_key_t *k = codex_key_by_id(w.value.key.id);
        if (!k || !codex_ble_link_matches(&l, w.generation) ||
            (w.value.key.down && now_ms() - w.time_ms > 250)) {
            ++worker_state.stats.tx_dropped; publish(); return;
        }
        if (k->kind != CODEX_KEY_DETENT &&
            ((w.value.key.down && remote_pressed != CODEX_NO_KEY) ||
             (!w.value.key.down && remote_pressed != w.value.key.id))) {
            ++worker_state.stats.tx_dropped; publish(); return;
        }
        char json[144];
        if (codex_action_json(&w.value.key, CODEX_DIAL_ALIAS, json, sizeof(json)) &&
            codex_protocol_send(&protocol, json, strlen(json))) {
            if (k->kind != CODEX_KEY_DETENT) remote_pressed = w.value.key.down ? k->id : CODEX_NO_KEY;
            ESP_LOGI(TAG, "Input id=%d action=%d lag_ms=%llu", k->id,
                     k->kind == CODEX_KEY_DETENT ? 2 : w.value.key.down,
                     (unsigned long long)(now_ms() - w.time_ms));
        }
    }
    publish();
}
static void worker_task(void *unused)
{
    (void)unused;
    for (;;) {
        codex_ble_link_t l = link_copy(); worker_reconcile(&l);
        codex_protocol_tick(&protocol, now_ms());
        work_t w;
        if (xQueueReceive(queue, &w, pdMS_TO_TICKS(10)) == pdTRUE) worker_process(&w);
        publish();
    }
}
esp_err_t codex_ble_start(void)
{
    if (initialized) return ESP_ERR_INVALID_STATE;
    codex_state_init(&published); codex_state_init(&worker_state);
    codex_ble_link_reset(&link, BLE_HS_CONN_HANDLE_NONE);
    queue = xQueueCreate(32, sizeof(work_t));
    if (!queue) return ESP_ERR_NO_MEM;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) { vQueueDelete(queue); queue = NULL; return err; }
    ble_svc_gap_init(); ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set("Codex Micro");
    if (!rc) rc = ble_svc_gap_device_appearance_set(0x03C0);
    if (!rc) rc = ble_att_set_preferred_mtu(247);
    if (!rc) rc = ble_gatts_count_cfg(battery_services);
    if (!rc) rc = ble_gatts_count_cfg(services);
    if (!rc) rc = ble_gatts_add_svcs(battery_services);
    if (!rc) rc = ble_gatts_add_svcs(services);
    if (rc) { nimble_port_deinit(); vQueueDelete(queue); queue = NULL; return ESP_FAIL; }
    ble_hs_cfg.reset_cb = reset; ble_hs_cfg.sync_cb = sync_host;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1; ble_hs_cfg.sm_mitm = 0;
    // NimBLE's sm_sc_only enforces authenticated level 4 for every protected
    // ATT attribute, which Just Works cannot satisfy (ATT error 0x05 even after
    // encryption). SC pairing remains required by CONFIG_BT_NIMBLE_SM_LEGACY=n;
    // keep level 2, encrypted CCCDs/reports and the existing 16-byte key checks.
    ble_hs_cfg.sm_sc = 1; ble_hs_cfg.sm_sc_only = 0; ble_hs_cfg.sm_sec_lvl = 2;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    ble_npl_event_init(&abort_event, host_abort, NULL);
    ble_npl_event_init(&repair_event, repair_host, NULL);
    rc = ble_npl_callout_init(&adv_retry, nimble_port_get_dflt_eventq(), advertise, NULL);
    if (rc) {
        ble_npl_event_deinit(&abort_event); ble_npl_event_deinit(&repair_event);
        nimble_port_deinit(); vQueueDelete(queue); queue = NULL; return ESP_ERR_NO_MEM;
    }
    codex_protocol_init(&protocol, &worker_state, notify_report, NULL);
    TaskHandle_t worker = NULL;
    if (xTaskCreate(worker_task, "codex_rpc", 6144, NULL, 6, &worker) != pdPASS) {
        ble_npl_callout_stop(&adv_retry); ble_npl_callout_deinit(&adv_retry);
        ble_npl_event_deinit(&abort_event); ble_npl_event_deinit(&repair_event);
        nimble_port_deinit(); vQueueDelete(queue); queue = NULL; return ESP_ERR_NO_MEM;
    }
    started = initialized = true;
    if (xTaskCreate(host_task, "nimble_host", NIMBLE_HS_STACK_SIZE, NULL,
                    configMAX_PRIORITIES - 4, NULL) != pdPASS) {
        started = initialized = false; vTaskDelete(worker);
        ble_npl_callout_stop(&adv_retry); ble_npl_callout_deinit(&adv_retry);
        ble_npl_event_deinit(&abort_event); ble_npl_event_deinit(&repair_event);
        nimble_port_deinit();
        vQueueDelete(queue); queue = NULL; return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "HOGP VID=303A PID=8360 usage=FF00 report=6 body=63 bonding=SC Just Works");
    return ESP_OK;
}

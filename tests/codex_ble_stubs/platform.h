#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_ERR_NVS_NOT_FOUND 0x1102
typedef void *nvs_iterator_t;
typedef struct { char key[16]; } nvs_entry_info_t;
#define NVS_TYPE_ANY 0
int nvs_entry_find(const char *,const char *,int,nvs_iterator_t *);
void nvs_entry_info(nvs_iterator_t,nvs_entry_info_t *);
int nvs_entry_next(nvs_iterator_t *);
void nvs_release_iterator(nvs_iterator_t);
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
void test_log(const char *format,...);
#define ESP_LOGI(tag,...) ((void)(tag),test_log(__VA_ARGS__))
#define ESP_LOGW(tag,...) ((void)(tag),test_log(__VA_ARGS__))
#define ESP_LOGE(tag,...) ((void)(tag),test_log(__VA_ARGS__))
int64_t esp_timer_get_time(void);
typedef struct test_queue *QueueHandle_t;
typedef void *TaskHandle_t;
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define pdPASS 1
#define configMAX_PRIORITIES 25
#define NIMBLE_HS_STACK_SIZE 4096
QueueHandle_t xQueueCreate(unsigned depth,unsigned size);
int xQueueSend(QueueHandle_t q,const void *item,unsigned timeout);
int xQueueReceive(QueueHandle_t q,void *item,unsigned timeout);
void vQueueDelete(QueueHandle_t q);
void vTaskDelay(unsigned ticks);
void vTaskDelete(void *task);
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void **handle);

typedef struct { uint16_t value; } ble_uuid_t;
typedef ble_uuid_t ble_uuid16_t;
#define BLE_UUID16_DECLARE(x) (&(const ble_uuid_t){(x)})
#define BLE_UUID16_INIT(x) {(x)}
#define BLE_HS_CONN_HANDLE_NONE UINT16_MAX
#define BLE_HS_FOREVER -1
#define BLE_HS_ENOTCONN 2
#define BLE_HS_EALREADY 3
#define BLE_HS_EDISABLED 4
#define BLE_HS_ENOENT 5
#define BLE_HS_EBUSY 6
#define BLE_HS_ESTORE_FAIL 7
#define BLE_HS_EREJECT 8
#define BLE_HS_ATT_ERR(x) (0x100+(x))
#define BLE_HS_HCI_ERR(x) (0x200+(x))
#define BLE_ERR_UNK_CONN_ID 2
#define BLE_ERR_REM_USER_CONN_TERM 0x13
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_ATT_ERR_INSUFFICIENT_ENC 15
#define BLE_ATT_ERR_INSUFFICIENT_AUTHEN 5
#define BLE_ATT_ERR_INSUFFICIENT_AUTHOR 8
#define BLE_ATT_ERR_INSUFFICIENT_KEY_SZ 12
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_ATT_ERR_REQ_NOT_SUPPORTED 6
#define BLE_ATT_ERR_VALUE_NOT_ALLOWED 19
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 3
#define BLE_ATT_ERR_READ_NOT_PERMITTED 2
#define BLE_ATT_F_READ 1
#define BLE_ATT_F_WRITE 2
#define BLE_ATT_F_READ_ENC 4
#define BLE_ATT_F_READ_AUTHEN 8
#define BLE_ATT_F_READ_AUTHOR 16
#define BLE_ATT_F_WRITE_ENC 32
#define BLE_ATT_F_WRITE_AUTHEN 64
#define BLE_ATT_F_WRITE_AUTHOR 128
#define BLE_GATT_ACCESS_OP_WRITE_CHR 1
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_GATT_CHR_F_READ 2
#define BLE_GATT_CHR_F_WRITE 8
#define BLE_GATT_CHR_F_WRITE_NO_RSP 4
#define BLE_GATT_CHR_F_NOTIFY 16
#define BLE_GATT_CHR_F_READ_ENC 512
#define BLE_GATT_CHR_F_READ_AUTHEN 1024
#define BLE_GATT_CHR_F_READ_AUTHOR 2048
#define BLE_GATT_CHR_F_WRITE_ENC 4096
#define BLE_GATT_CHR_F_WRITE_AUTHEN 8192
#define BLE_GATT_CHR_F_WRITE_AUTHOR 16384
#define BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC 0x8000
#define BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN 0x10000
#define BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHOR 0x20000
#define BLE_GAP_AUTHORIZE_REJECT 2
typedef uint32_t ble_gatt_chr_flags;
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_HS_ADV_F_DISC_GEN 2
#define BLE_HS_ADV_F_BREDR_UNSUP 4
#define BLE_GAP_CONN_MODE_UND 2
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_GAP_REPEAT_PAIRING_IGNORE 0
#define BLE_GAP_REPEAT_PAIRING_RETRY 1
#define BLE_SM_IO_CAP_NO_IO 3
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
enum { BLE_GAP_EVENT_CONNECT, BLE_GAP_EVENT_DISCONNECT, BLE_GAP_EVENT_ENC_CHANGE,
       BLE_GAP_EVENT_SUBSCRIBE, BLE_GAP_EVENT_MTU, BLE_GAP_EVENT_REPEAT_PAIRING, BLE_GAP_EVENT_ADV_COMPLETE };
typedef struct { uint8_t type,val[6]; } ble_addr_t;
static const ble_addr_t test_any_addr={.type=255};
#define BLE_ADDR_ANY (&test_any_addr)
enum {BLE_STORE_OBJ_TYPE_OUR_SEC, BLE_STORE_OBJ_TYPE_PEER_SEC, BLE_STORE_OBJ_TYPE_CCCD, BLE_STORE_OBJ_TYPE_CSFC, BLE_STORE_OBJ_TYPE_PEER_ADDR};
struct ble_store_key_sec { ble_addr_t peer_addr; };
struct ble_store_value_sec { ble_addr_t peer_addr; bool irk_present,ltk_present; };
union ble_store_key {
    struct ble_store_key_sec sec;
    struct { ble_addr_t peer_addr; } cccd,csfc;
    struct { ble_addr_t peer_rpa_addr; } rpa_rec;
};
struct ble_gap_event {
    int type;
    union {
        struct { int status; uint16_t conn_handle; } connect,enc_change;
        struct { int reason; } disconnect;
        struct { uint16_t conn_handle,attr_handle; int cur_notify; } subscribe;
        struct { uint16_t conn_handle,value; } mtu;
        struct { uint16_t conn_handle; } repeat_pairing;
    };
};
struct ble_gap_sec_state { bool encrypted,bonded,authenticated,authorize; uint8_t key_size; };
struct ble_gap_conn_desc { struct ble_gap_sec_state sec_state; ble_addr_t peer_id_addr; };
struct os_mbuf { uint8_t data[256]; size_t length; };
#define OS_MBUF_PKTLEN(om) ((om)->length)
struct ble_gatt_access_ctxt { int op; struct os_mbuf *om; };
typedef int (*ble_gatt_access_fn)(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);
struct ble_gatt_dsc_def { const ble_uuid_t *uuid; unsigned att_flags; uint8_t min_key_size; ble_gatt_access_fn access_cb; void *arg; };
struct ble_gatt_chr_def {
    const ble_uuid_t *uuid; ble_gatt_access_fn access_cb; void *arg;
    unsigned flags; uint8_t min_key_size; uint16_t *val_handle; struct ble_gatt_dsc_def *descriptors;
};
struct ble_gatt_svc_def {
    unsigned type; const ble_uuid_t *uuid; const struct ble_gatt_svc_def **includes;
    const struct ble_gatt_chr_def *characteristics;
};
struct ble_hs_adv_fields {
    uint8_t flags; const uint8_t *name; uint8_t name_len,name_is_complete;
    uint16_t appearance; uint8_t appearance_is_present;
    const ble_uuid16_t *uuids16; uint8_t num_uuids16,uuids16_is_complete;
};
struct ble_gap_adv_params { uint8_t conn_mode,disc_mode; };
struct ble_npl_event { void (*fn)(struct ble_npl_event *); bool queued; };
struct ble_npl_callout { void (*fn)(struct ble_npl_event *); unsigned delay; };
struct test_hs_cfg {
    void (*reset_cb)(int); void (*sync_cb)(void); void (*store_status_cb)(void);
    int sm_io_cap,sm_bonding,sm_mitm,sm_sc,sm_sc_only,sm_sec_lvl,sm_our_key_dist,sm_their_key_dist;
};
extern struct test_hs_cfg ble_hs_cfg;
int os_mbuf_append(struct os_mbuf *,const void *,size_t);
int os_mbuf_copydata(struct os_mbuf *,unsigned,size_t,void *);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *,size_t);
int ble_gatts_notify_custom(uint16_t,uint16_t,struct os_mbuf *);
void ble_gatts_chr_updated(uint16_t);
int ble_gap_terminate(uint16_t,int);
void ble_hs_sched_reset(int);
int ble_gap_adv_active(void);
int ble_gap_adv_stop(void);
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_start(uint8_t,const void *,int,const struct ble_gap_adv_params *,int (*)(struct ble_gap_event *,void *),void *);
int ble_gap_security_initiate(uint16_t);
int ble_gap_conn_find(uint16_t,struct ble_gap_conn_desc *);
int ble_store_util_delete_peer(const ble_addr_t *);
int ble_store_util_bonded_peers(ble_addr_t *,int *,int);
int ble_store_read_peer_sec(const struct ble_store_key_sec *,struct ble_store_value_sec *);
int ble_store_write_peer_sec(const struct ble_store_value_sec *);
int ble_gap_unpair(const ble_addr_t *);
int ble_gap_rd_local_resolv_addr(uint8_t,const ble_addr_t *,uint8_t *);
int ble_store_util_delete_all(int,const union ble_store_key *);
int ble_store_util_count(int,int *);
void ble_store_util_status_rr(void);
int ble_hs_util_ensure_addr(int);
int ble_hs_id_infer_auto(int,uint8_t *);
void nimble_port_run(void);
int nimble_port_init(void);
int nimble_port_deinit(void);
void *nimble_port_get_dflt_eventq(void);
void ble_npl_event_init(struct ble_npl_event *,void (*)(struct ble_npl_event *),void *);
void ble_npl_event_deinit(struct ble_npl_event *);
bool ble_npl_event_is_queued(struct ble_npl_event *);
void ble_npl_eventq_put(void *,struct ble_npl_event *);
int ble_npl_callout_init(struct ble_npl_callout *,void *,void (*)(struct ble_npl_event *),void *);
int ble_npl_callout_reset(struct ble_npl_callout *,unsigned);
void ble_npl_callout_stop(struct ble_npl_callout *);
void ble_npl_callout_deinit(struct ble_npl_callout *);
unsigned ble_npl_time_ms_to_ticks32(unsigned);
void ble_svc_gap_init(void);
void ble_svc_gatt_init(void);
int ble_svc_gap_device_name_set(const char *);
int ble_svc_gap_device_appearance_set(unsigned);
int ble_att_set_preferred_mtu(unsigned);
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *);

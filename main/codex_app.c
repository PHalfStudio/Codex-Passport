#include "codex_ble_hid.h"
#include "codex_input.h"
#include "codex_ui.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "codex_app";
typedef struct { codex_button_t button; codex_input_event_t event; uint64_t ms; } input_message_t;
static QueueHandle_t input_queue;
static portMUX_TYPE input_lock = portMUX_INITIALIZER_UNLOCKED;
static bool overflow;
static uint64_t milliseconds(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static void button_callback(bsp_btn_t b, bsp_btn_ev_t e, void *unused)
{
    (void)unused;
    // Do not enqueue CLICK/DOUBLE, do not poll ADC or touch BLE/LVGL here.
    codex_input_event_t event;
    switch (e) {
    case BSP_BTN_PRESS: event = CODEX_PRESS; break;
    case BSP_BTN_RELEASE: event = CODEX_RELEASE; break;
    case BSP_BTN_LONG: event = CODEX_LONG; break;
    default: return;
    }
    input_message_t msg = {(codex_button_t)b, event, milliseconds()};
    if (xQueueSend(input_queue, &msg, 0) != pdTRUE) {
        portENTER_CRITICAL(&input_lock); overflow = true; portEXIT_CRITICAL(&input_lock);
    }
}
static bool emit(void *unused, const codex_action_t *action)
{
    (void)unused;
    return codex_ble_action(action);
}
static void battery_task(void *unused)
{
    (void)unused;
    esp_err_t rc = bsp_battery_init();
    if (rc != ESP_OK) ESP_LOGW(TAG, "Battery unavailable: %s", esp_err_to_name(rc));
    for (;;) {
        int soc = rc == ESP_OK ? bsp_battery_soc() : -1;
        codex_ble_battery(soc, -1); // No charging GPIO or charging API in this BSP.
        vTaskDelay(pdMS_TO_TICKS(15000));
        if (rc != ESP_OK) rc = bsp_battery_init();
    }
}
typedef struct { uint8_t schema, page, focus[CODEX_PAGES], brightness; } preferences_t;
static void restore(codex_input_t *input, codex_local_t *local)
{
    nvs_handle_t nvs;
    if (nvs_open("codex_ui", NVS_READONLY, &nvs) != ESP_OK) return;
    preferences_t prefs; size_t size = sizeof(prefs);
    if (nvs_get_blob(nvs, "navigation", &prefs, &size) == ESP_OK && size == sizeof(prefs) &&
        prefs.schema == 2 && prefs.page < CODEX_PAGES) {
        bool valid = true;
        for (unsigned j = 0; j < CODEX_PAGES; ++j)
            if (prefs.focus[j] >= codex_page_count((codex_page_t)j)) valid = false;
        if (valid) {
            input->page = prefs.page; memcpy(input->focus, prefs.focus, sizeof(prefs.focus));
            if (prefs.brightness >= 10 && prefs.brightness <= 100) local->brightness = prefs.brightness;
        }
    }
    nvs_close(nvs);
}
static bool save(const codex_input_t *input, const codex_local_t *local)
{
    preferences_t p = {.schema = 2, .page = input->page, .brightness = local->brightness};
    memcpy(p.focus, input->focus, sizeof(p.focus));
    nvs_handle_t nvs;
    esp_err_t e = nvs_open("codex_ui", NVS_READWRITE, &nvs);
    if (e != ESP_OK) return false;
    e = nvs_set_blob(nvs, "navigation", &p, sizeof(p));
    if (e == ESP_OK) e = nvs_commit(nvs);
    nvs_close(nvs);
    if (e != ESP_OK) ESP_LOGW(TAG, "UI preference save failed: %s", esp_err_to_name(e));
    return e == ESP_OK;
}
void app_main(void)
{
    ESP_LOGI(TAG, "Codex Passport %s - ESP32-C3 / no PSRAM / BLE vendor HID", CODEX_VERSION);
    esp_err_t nvs_result = nvs_flash_init();
    // Never silently erase user NVS for a version/full-pages error.
    if (nvs_result != ESP_OK) ESP_LOGE(TAG, "NVS failed: %s; no automatic erase", esp_err_to_name(nvs_result));
    codex_input_t input; codex_input_init(&input, emit, NULL);
    codex_local_t local; codex_local_init(&local);
#ifdef CONFIG_CODEX_PROTECT_FORK_SEND
    input.protect_fork_send = true;
#endif
    if (nvs_result == ESP_OK) restore(&input, &local);
    bool ui = bsp_display_init() == ESP_OK && bsp_lvgl_init() != NULL;
    if (ui && bsp_lvgl_lock(500)) { ui = codex_ui_create(); bsp_lvgl_unlock(); }
    else ui = false;
    if (ui) bsp_display_backlight(local.brightness);
    else ESP_LOGE(TAG, "Display initialization failed");
    input_queue = xQueueCreate(24, sizeof(input_message_t));
    if (!input_queue) { ESP_LOGE(TAG, "Input queue allocation failed"); return; }
    esp_err_t buttons = bsp_button_init(button_callback, NULL);
    if (buttons != ESP_OK) ESP_LOGE(TAG, "Buttons unavailable: %s", esp_err_to_name(buttons));
    esp_err_t ble = nvs_result == ESP_OK ? codex_ble_start() : nvs_result;
    if (ble != ESP_OK) ESP_LOGE(TAG, "BLE startup failed: %s", esp_err_to_name(ble));
    if (xTaskCreate(battery_task, "codex_battery", 3072, NULL, 2, NULL) != pdPASS)
        ESP_LOGW(TAG, "Battery worker unavailable; SOC remains unknown");
    uint64_t last_ui = 0, last_input = milliseconds(), last_health = 0;
    codex_page_t saved_page = input.page;
    uint8_t saved_brightness = local.brightness;
    uint8_t saved_focus[CODEX_PAGES]; memcpy(saved_focus, input.focus, sizeof(saved_focus));
    bool recovering = false;
    uint64_t released_since = 0;
    for (;;) {
        uint64_t now = milliseconds();
        codex_state_t state; codex_ble_snapshot(&state);
        if (ble != ESP_OK) { state.connection = CODEX_ERROR; state.last_error = ble; }
        codex_input_link(&input, state.ready, state.generation, now);
        portENTER_CRITICAL(&input_lock); bool lost = overflow; overflow = false; portEXIT_CRITICAL(&input_lock);
        if (lost) {
            codex_input_cancel(&input, now); codex_ble_abort();
            xQueueReset(input_queue); recovering = true; released_since = 0;
            ESP_LOGW(TAG, "Input queue overflow; controls cancelled until stable release");
            memset(local.held, 0, sizeof(local.held)); local.owner = -1;
            local.confirm = false; local.blocked = true;
        }
        input_message_t event;
        if (xQueueReceive(input_queue, &event, pdMS_TO_TICKS(5)) == pdTRUE && !recovering) {
            // Avoid executing an old press after a long blocked UI operation.
            if (event.event == CODEX_PRESS && now > event.ms + 200) codex_input_cancel(&input, now);
            else codex_local_dispatch(&local, &input, event.button, event.event, event.ms);
            last_input = now;
        }
        if (recovering) {
            int mv = bsp_button_read_mv(); // Reuse BSP's ADC1; never allocate another unit.
            if (mv >= BSP_BTN_MV_RELEASED_MIN) {
                if (!released_since) released_since = now;
                if (now - released_since >= 50) {
                    memset(input.held, 0, sizeof(input.held)); input.blocked = false; recovering = false;
                    local.blocked = false;
                    if (local.locked) local.wake_armed = true;
                }
            } else released_since = 0;
        }
        codex_input_tick(&input, milliseconds());
        codex_local_tick(&local, milliseconds());
        uint8_t requests = codex_local_requests(&local);
        if (requests & CODEX_LOCAL_REPAIR) (void)codex_ble_repair();
        if (requests & (CODEX_LOCAL_OFF | CODEX_LOCAL_ON)) {
            bool visible = (requests & CODEX_LOCAL_ON) != 0;
            if (ui && bsp_lvgl_lock(100)) {
                esp_err_t e = bsp_display_set_visible(visible);
                bsp_lvgl_unlock();
                if (e != ESP_OK) ESP_LOGW(TAG, "Panel visibility failed: %s", esp_err_to_name(e));
            }
            bsp_display_backlight(visible ? local.brightness : 0);
        } else if (requests & CODEX_LOCAL_DIM) bsp_display_backlight(local.brightness);
        if (input.transport_fault) { input.transport_fault = false; codex_ble_abort(); }
        if (ui && !local.locked && now - last_ui >= 50 && bsp_lvgl_lock(10)) {
            if (buttons != ESP_OK) {
                input.feedback = "Buttons unavailable";
                input.feedback_until = UINT64_MAX;
            }
            codex_ui_render(&input, &state, now); codex_ui_local(&local, &state, now);
            bsp_lvgl_unlock(); last_ui = now;
        }
        if (nvs_result == ESP_OK && now - last_input >= 10000 && input.owner < 0 && local.owner < 0 &&
            (input.page != saved_page || local.brightness != saved_brightness || memcmp(input.focus, saved_focus, sizeof(saved_focus)))) {
            if (save(&input, &local)) {
                saved_page = input.page; saved_brightness = local.brightness;
                memcpy(saved_focus, input.focus, sizeof(saved_focus));
            }
            last_input = now; // Bounded retry rate on NVS errors
        }
        if (now - last_health >= 30000) {
            ESP_LOGI(TAG, "Health heap=%u min=%u largest=%u app_stack=%u RX=%lu err=%lu drop=%lu TXfail=%lu",
                     (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned long)state.stats.messages_rx,
                     (unsigned long)state.stats.rx_errors, (unsigned long)state.stats.rx_dropped,
                     (unsigned long)state.stats.tx_failures);
            last_health = now;
        }
    }
}

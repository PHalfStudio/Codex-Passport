idf_component_register(
    SRCS "codex_app.c" "codex_input.c" "codex_virtual_keys.c" "codex_local.c"
         "codex_ui.c" "codex_ble_hid.c" "codex_ble_link.c"
         "codex_protocol.c" "codex_state.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_timer json nvs_flash
)

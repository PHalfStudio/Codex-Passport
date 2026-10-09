#pragma once
#include "codex_state.h"
#include "codex_virtual_keys.h"
#include "esp_err.h"

esp_err_t codex_ble_start(void);
bool codex_ble_action(const codex_action_t *action);
void codex_ble_snapshot(codex_state_t *state);
void codex_ble_battery(int percentage, int charging);
void codex_ble_abort(void);       // Fail closed when a confirmed release cannot be queued
esp_err_t codex_ble_repair(void); // Explicit local confirmation only; preserves UI NVS

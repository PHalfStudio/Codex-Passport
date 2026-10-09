#pragma once
#include "codex_input.h"
#include "codex_state.h"
#include "codex_local.h"

// Caller holds bsp_lvgl_lock(), including creation and rendering.
bool codex_ui_create(void);
void codex_ui_render(const codex_input_t *input, const codex_state_t *state, uint64_t now);
void codex_ui_local(const codex_local_t *local, const codex_state_t *state, uint64_t now);

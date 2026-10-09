#pragma once
#include "codex_virtual_keys.h"

#define CODEX_LONG_MS 500
#define CODEX_CONFIRM_MS 3000
typedef enum { CODEX_UP, CODEX_DOWN, CODEX_OK, CODEX_BUTTONS } codex_button_t;
typedef enum { CODEX_PRESS, CODEX_RELEASE, CODEX_LONG, CODEX_CLICK, CODEX_DOUBLE, CODEX_INVALID } codex_input_event_t;
typedef enum {
    CODEX_IDLE, CODEX_NAV_PRESSED, CODEX_NAV_LONG_HANDLED,
    CODEX_VIRTUAL_PRESSED, CODEX_CONFIRM_PENDING, CODEX_INPUT_DISCONNECTED
} codex_input_phase_t;
typedef bool (*codex_emit_fn)(void *user, const codex_action_t *action);
typedef struct {
    codex_page_t page;
    uint8_t focus[CODEX_PAGES];
    codex_input_phase_t phase;
    codex_key_id_t active_pressed_id, confirm_id;
    uint64_t pressed_ms, confirm_deadline, feedback_until;
    bool held[CODEX_BUTTONS], blocked, long_handled, ready;
    bool protect_dangerous, protect_fork_send, transport_fault;
    bool local_requested;
    int owner;
    uint32_t revision, generation;
    const char *feedback;
    codex_emit_fn emit;
    void *user;
} codex_input_t;
void codex_input_init(codex_input_t *input, codex_emit_fn emit, void *user);
void codex_input_link(codex_input_t *input, bool ready, uint32_t generation, uint64_t now);
void codex_input_event(codex_input_t *input, codex_button_t button, codex_input_event_t event, uint64_t now);
void codex_input_tick(codex_input_t *input, uint64_t now);
void codex_input_cancel(codex_input_t *input, uint64_t now);

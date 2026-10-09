#pragma once
#include "codex_input.h"

typedef enum { CODEX_LOCAL_MENU, CODEX_LOCAL_BRIGHTNESS, CODEX_LOCAL_DIAGNOSTICS } codex_local_mode_t;
enum { CODEX_LOCAL_OFF = 1, CODEX_LOCAL_ON = 2, CODEX_LOCAL_DIM = 4, CODEX_LOCAL_REPAIR = 8 };
enum { CODEX_LOCAL_LOCK, CODEX_LOCAL_BRIGHT, CODEX_LOCAL_PAIR, CODEX_LOCAL_DEBUG, CODEX_LOCAL_BACK, CODEX_LOCAL_ITEMS };
typedef struct {
    bool open, locked, wake_armed, wake_consuming, confirm, blocked;
    bool held[CODEX_BUTTONS];
    int owner;
    uint8_t focus, brightness, requests;
    codex_local_mode_t mode;
    uint64_t confirm_deadline;
    uint32_t revision;
} codex_local_t;
void codex_local_init(codex_local_t *local);
void codex_local_open(codex_local_t *local);
void codex_local_event(codex_local_t *local, codex_button_t button, codex_input_event_t event, uint64_t now);
void codex_local_tick(codex_local_t *local, uint64_t now);
uint8_t codex_local_requests(codex_local_t *local);
void codex_local_dispatch(codex_local_t *local, codex_input_t *input,
                          codex_button_t button, codex_input_event_t event, uint64_t now);

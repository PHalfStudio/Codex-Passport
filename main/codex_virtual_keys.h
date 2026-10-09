#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { CODEX_AGENTS_PAGE, CODEX_COMMANDS_PAGE, CODEX_NAV_PAGE, CODEX_PAGES } codex_page_t;
typedef enum {
    CODEX_AG00, CODEX_AG01, CODEX_AG02, CODEX_AG03, CODEX_AG04, CODEX_AG05,
    CODEX_FAST, CODEX_APPROVE, CODEX_DECLINE, CODEX_FORK, CODEX_MIC, CODEX_SEND,
    CODEX_JOY_UP, CODEX_JOY_RIGHT, CODEX_JOY_DOWN, CODEX_JOY_LEFT,
    CODEX_DIAL_CW, CODEX_DIAL_CCW, CODEX_DIAL_CLICK, CODEX_LOCAL_SETTINGS, CODEX_KEY_COUNT,
    CODEX_NO_KEY = -1
} codex_key_id_t;
typedef enum { CODEX_KEY_HID, CODEX_KEY_RAD, CODEX_KEY_DETENT, CODEX_KEY_LOCAL } codex_key_kind_t;
typedef struct {
    codex_key_id_t id;
    const char *label, *wire_key;
    codex_key_kind_t kind;
    int agent;
    float angle;
    bool dangerous;
} codex_key_t;
typedef struct { codex_key_id_t id; bool down; uint64_t time_ms; } codex_action_t;
size_t codex_page_count(codex_page_t page);
const char *codex_page_name(codex_page_t page);
const codex_key_t *codex_key_at(codex_page_t page, unsigned focus);
const codex_key_t *codex_key_by_id(codex_key_id_t id);
bool codex_action_json(const codex_action_t *action, bool dial_alias, char *out, size_t capacity);

#pragma once
#include "codex_state.h"
#include <stddef.h>

#define CODEX_REPORT_ID 6
#define CODEX_REPORT_BODY 63
#define CODEX_FRAGMENT_SIZE 61
#define CODEX_JSON_MAX 4096
#define CODEX_RX_TIMEOUT_MS 2500
typedef bool (*codex_report_send_fn)(void *user, const uint8_t body[CODEX_REPORT_BODY]);
typedef struct {
    char rx[CODEX_JSON_MAX + 1], response[CODEX_JSON_MAX + 512];
    size_t used;
    unsigned depth, tokens;
    bool string, escape, discard;
    uint64_t last_rx_ms;
    codex_state_t *state;
    codex_report_send_fn send;
    void *user;
} codex_protocol_t;
extern const uint8_t codex_report_map[];
extern const size_t codex_report_map_size;
extern const uint8_t codex_pnp_id[7];
void codex_protocol_init(codex_protocol_t *p, codex_state_t *state, codex_report_send_fn send, void *user);
void codex_protocol_reset(codex_protocol_t *p);
void codex_protocol_tick(codex_protocol_t *p, uint64_t now);
bool codex_protocol_receive(codex_protocol_t *p, const uint8_t *report, size_t size, uint64_t now);
bool codex_protocol_send(codex_protocol_t *p, const char *json, size_t length);
size_t codex_report_pack(const char *text, size_t length, uint8_t body[CODEX_REPORT_BODY]);

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CODEX_VERSION "1.0.0"
#define CODEX_AGENTS 6
typedef enum {
    CODEX_DISCONNECTED, CODEX_ADVERTISING, CODEX_CONNECTED,
    CODEX_HOST_ACTIVE, CODEX_ERROR
} codex_connection_t;
typedef enum { CODEX_LIGHT_OFF, CODEX_LIGHT_SOLID, CODEX_LIGHT_BREATH } codex_effect_t;
typedef enum { CODEX_PAIR_NONE, CODEX_PAIR_PENDING, CODEX_PAIR_DONE, CODEX_PAIR_FAILED } codex_pairing_reset_t;
typedef struct {
    uint32_t color;
    float brightness, speed, magic;
    codex_effect_t effect;
    int effect_code;
    bool known, sync_keys, sync_ambient;
} codex_light_t;
typedef struct {
    uint32_t reports_rx, messages_rx, rx_errors, rx_dropped, rx_timeouts;
    uint32_t tx_reports, tx_failures, tx_dropped;
} codex_stats_t;
typedef struct {
    codex_connection_t connection;
    bool encrypted, subscribed, ready;
    uint16_t mtu;
    uint32_t generation, revision;
    int battery;                 // -1 = unavailable, never a guessed SOC
    int charging;                // -1 = board has no public charging sensor
    int last_error;
    codex_pairing_reset_t pairing_reset;
    codex_light_t agents[CODEX_AGENTS], ambient, keys;
    codex_stats_t stats;
    char last_method[32];         // method names only; no host content or identity
} codex_state_t;
void codex_state_init(codex_state_t *state);
uint32_t codex_light_color(const codex_light_t *light, uint64_t now_ms);
const char *codex_connection_name(codex_connection_t connection);

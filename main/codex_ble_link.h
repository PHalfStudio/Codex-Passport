#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t connection, mtu;
    uint32_t generation;
    bool encrypted, subscribed, host_active, suspended, closing;
} codex_ble_link_t;
void codex_ble_link_reset(codex_ble_link_t *link, uint16_t connection);
bool codex_ble_link_ready(const codex_ble_link_t *link);
bool codex_ble_link_matches(const codex_ble_link_t *link, uint32_t generation);

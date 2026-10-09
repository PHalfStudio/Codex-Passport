#include "codex_ble_link.h"
void codex_ble_link_reset(codex_ble_link_t *l, uint16_t connection)
{
    uint32_t next = l->generation + 1;
    *l = (codex_ble_link_t){.connection = connection, .mtu = 23, .generation = next};
}
bool codex_ble_link_ready(const codex_ble_link_t *l)
{
    // 63 bytes + 3-byte ATT notification header must fit without truncation.
    // The desktop can accept input before its lighting/status RPC succeeds.
    // host_active is evidence for the UI, not a prerequisite for sending input.
    return l->connection != UINT16_MAX && l->encrypted && l->subscribed &&
        l->mtu >= 66 && !l->suspended && !l->closing;
}
bool codex_ble_link_matches(const codex_ble_link_t *l, uint32_t generation)
{
    return codex_ble_link_ready(l) && generation == l->generation;
}

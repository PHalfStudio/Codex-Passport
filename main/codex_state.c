#include "codex_state.h"
#include <string.h>

void codex_state_init(codex_state_t *s)
{
    memset(s, 0, sizeof(*s));
    s->battery = s->charging = -1;
    s->mtu = 23;
    for (unsigned i = 0; i < CODEX_AGENTS; ++i) {
        s->agents[i].brightness = 1;
        s->agents[i].effect = CODEX_LIGHT_SOLID;
    }
}
const char *codex_connection_name(codex_connection_t c)
{
    switch (c) {
    case CODEX_ADVERTISING: return "Advertising";
    case CODEX_CONNECTED: return "BLE Connected";
    case CODEX_HOST_ACTIVE: return "Host Active";
    case CODEX_ERROR: return "Error";
    default: return "Disconnected";
    }
}
uint32_t codex_light_color(const codex_light_t *l, uint64_t ms)
{
    if (!l->known) return 0x626975; // Neutral, not a fabricated host status
    if (l->effect == CODEX_LIGHT_OFF) return 0x222630;
    float b = l->brightness;
    if (l->effect == CODEX_LIGHT_BREATH) {
        unsigned period = 4000U - (unsigned)(l->speed * 2500.0f);
        unsigned phase = (unsigned)(ms % period);
        unsigned half = period / 2;
        float triangle = (float)(phase < half ? phase : period - phase) / half;
        b *= 0.25f + 0.75f * triangle;
    }
    uint32_t r = (uint32_t)(((l->color >> 16) & 255) * b);
    uint32_t g = (uint32_t)(((l->color >> 8) & 255) * b);
    uint32_t blue = (uint32_t)((l->color & 255) * b);
    return (r << 16) | (g << 8) | blue;
}

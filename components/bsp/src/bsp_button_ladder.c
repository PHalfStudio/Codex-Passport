#include "bsp_button_ladder.h"

void bsp_button_ladder_init(bsp_button_ladder_t *g)
{
    *g = (bsp_button_ladder_t){.active = -1, .candidate = -2};
}
int bsp_button_ladder_sample(bsp_button_ladder_t *g, int raw, uint64_t ms)
{
    if (raw < -1 || raw > 2) {
        g->active = -1; g->armed = false; g->candidate = -2;
        return -1;
    }
    if (g->active >= 0) {
        if (raw == g->active) return g->active;
        g->active = -1;
        g->armed = false;
        g->candidate = raw;
        g->candidate_since = ms;
        return -1;
    }
    if (raw != g->candidate) {
        g->candidate = raw;
        g->candidate_since = ms;
        return -1;
    }
    // 20 ms + button component's 10 ms debounce = approximately 30 ms.
    if (ms - g->candidate_since < 20) return -1;
    if (raw == -1) g->armed = true;
    else if (g->armed) g->active = raw;
    return g->active;
}

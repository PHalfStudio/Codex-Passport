#pragma once
#include <stdbool.h>
#include <stdint.h>

// Shared ADC identity filter. -1 means released; -2 means invalid/unavailable.
// A different key cannot become active until a stable fully released sample.
typedef struct {
    int active, candidate;
    uint64_t candidate_since;
    bool armed;
} bsp_button_ladder_t;
void bsp_button_ladder_init(bsp_button_ladder_t *gate);
int bsp_button_ladder_sample(bsp_button_ladder_t *gate, int raw_key, uint64_t ms);

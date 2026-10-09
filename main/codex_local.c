#include "codex_local.h"
#include <string.h>

void codex_local_init(codex_local_t *l)
{
    memset(l, 0, sizeof(*l)); l->owner = -1; l->brightness = 65;
}
void codex_local_open(codex_local_t *l)
{
    memset(l->held, 0, sizeof(l->held)); l->owner = -1; l->blocked = false;
    l->confirm = false; l->open = true; l->mode = CODEX_LOCAL_MENU; ++l->revision;
}
void codex_local_tick(codex_local_t *l, uint64_t now)
{
    if (l->confirm && now >= l->confirm_deadline) { l->confirm = false; ++l->revision; }
}
uint8_t codex_local_requests(codex_local_t *l)
{
    uint8_t requests = l->requests; l->requests = 0; return requests;
}
void codex_local_event(codex_local_t *l, codex_button_t b, codex_input_event_t ev, uint64_t now)
{
    if ((unsigned)b >= CODEX_BUTTONS || ev == CODEX_CLICK || ev == CODEX_DOUBLE || ev == CODEX_LONG) return;
    codex_local_tick(l, now);
    if (ev == CODEX_RELEASE) {
        if (!l->held[b]) return;
        l->held[b] = false;
        if (l->open && !l->blocked && l->owner == (int)b && b != CODEX_OK) {
            if (l->mode == CODEX_LOCAL_BRIGHTNESS) {
                int level = l->brightness + (b == CODEX_UP ? 5 : -5);
                l->brightness = level < 10 ? 10 : level > 100 ? 100 : (uint8_t)level;
                l->requests |= CODEX_LOCAL_DIM;
            } else if (l->mode == CODEX_LOCAL_MENU) {
                l->focus = (uint8_t)((l->focus + (b == CODEX_UP ? CODEX_LOCAL_ITEMS - 1 : 1)) % CODEX_LOCAL_ITEMS);
            }
            ++l->revision;
        }
        if (!l->held[0] && !l->held[1] && !l->held[2]) {
            l->owner = -1; l->blocked = false; l->wake_consuming = false;
            if (l->locked) l->wake_armed = true;
        }
        return;
    }
    if (ev != CODEX_PRESS || l->held[b]) return;
    l->held[b] = true;
    if (l->owner >= 0 || l->blocked) { l->blocked = true; l->confirm = false; return; }
    l->owner = b;
    if (l->locked) {
        if (l->wake_armed) {
            l->locked = false; l->wake_consuming = true; l->requests |= CODEX_LOCAL_ON; ++l->revision;
        }
        return;
    }
    if (l->wake_consuming || !l->open) return;
    if (b != CODEX_OK) { l->confirm = false; ++l->revision; return; }
    if (l->mode != CODEX_LOCAL_MENU) { l->mode = CODEX_LOCAL_MENU; ++l->revision; return; }
    switch (l->focus) {
    case CODEX_LOCAL_LOCK:
        l->locked = true; l->wake_armed = false; l->open = false; l->requests |= CODEX_LOCAL_OFF; break;
    case CODEX_LOCAL_BRIGHT: l->mode = CODEX_LOCAL_BRIGHTNESS; break;
    case CODEX_LOCAL_PAIR:
        if (l->confirm) { l->confirm = false; l->requests |= CODEX_LOCAL_REPAIR; }
        else { l->confirm = true; l->confirm_deadline = now + CODEX_CONFIRM_MS; }
        break;
    case CODEX_LOCAL_DEBUG: l->mode = CODEX_LOCAL_DIAGNOSTICS; break;
    default: l->open = false; l->wake_consuming = true; break;
    }
    ++l->revision;
}
void codex_local_dispatch(codex_local_t *l, codex_input_t *i,
                          codex_button_t b, codex_input_event_t event, uint64_t now)
{
    if (l->open || l->locked || l->wake_consuming) codex_local_event(l, b, event, now);
    else codex_input_event(i, b, event, now);
    // Open only after the entry key is released, so that edge cannot select a row.
    if (i->local_requested && i->owner < 0 && !i->held[0] && !i->held[1] && !i->held[2]) {
        i->local_requested = false; codex_local_open(l);
    }
}

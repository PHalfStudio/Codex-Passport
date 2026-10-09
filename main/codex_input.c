#include "codex_input.h"
#include <string.h>

static void feedback(codex_input_t *i, const char *text, uint64_t now)
{
    i->feedback = text;
    i->feedback_until = now + 1200;
    ++i->revision;
}
static void phase(codex_input_t *i)
{
    if (i->active_pressed_id != CODEX_NO_KEY) i->phase = CODEX_VIRTUAL_PRESSED;
    else if (i->owner >= 0 && i->owner != CODEX_OK)
        i->phase = i->long_handled ? CODEX_NAV_LONG_HANDLED : CODEX_NAV_PRESSED;
    else if (i->confirm_id != CODEX_NO_KEY) i->phase = CODEX_CONFIRM_PENDING;
    else i->phase = i->ready ? CODEX_IDLE : CODEX_INPUT_DISCONNECTED;
}
void codex_input_init(codex_input_t *i, codex_emit_fn emit, void *user)
{
    memset(i, 0, sizeof(*i));
    i->owner = -1;
    i->active_pressed_id = i->confirm_id = CODEX_NO_KEY;
    i->protect_dangerous = true;
    i->emit = emit;
    i->user = user;
    phase(i);
}
static void release(codex_input_t *i, uint64_t now)
{
    if (i->active_pressed_id == CODEX_NO_KEY) return;
    codex_action_t a = {i->active_pressed_id, false, now};
    if (i->ready && !i->emit(i->user, &a)) i->transport_fault = true;
    i->active_pressed_id = CODEX_NO_KEY;
    ++i->revision;
}
void codex_input_cancel(codex_input_t *i, uint64_t now)
{
    release(i, now);
    i->confirm_id = CODEX_NO_KEY;
    i->local_requested = false;
    i->blocked = true;
    i->owner = -1;
    phase(i);
    feedback(i, "Input cancelled", now);
}
void codex_input_link(codex_input_t *i, bool ready, uint32_t generation, uint64_t now)
{
    if (ready == i->ready && generation == i->generation) return;
    // Never send an old release to a new host, or replay a held key on reconnect.
    i->active_pressed_id = i->confirm_id = CODEX_NO_KEY;
    i->owner = -1;
    i->blocked = i->held[0] || i->held[1] || i->held[2];
    i->ready = ready;
    i->generation = generation;
    phase(i);
    feedback(i, ready ? "Controls ready" : "Waiting for HID link", now);
}
void codex_input_tick(codex_input_t *i, uint64_t now)
{
    if (i->confirm_id != CODEX_NO_KEY && now >= i->confirm_deadline) {
        i->confirm_id = CODEX_NO_KEY;
        feedback(i, "Confirmation expired", now);
    }
    if (i->owner >= 0 && i->owner != CODEX_OK && !i->long_handled &&
        now - i->pressed_ms >= CODEX_LONG_MS) {
        i->long_handled = true;
        i->confirm_id = CODEX_NO_KEY;
        i->page = (codex_page_t)((i->page + (i->owner == CODEX_UP ? CODEX_PAGES - 1 : 1)) % CODEX_PAGES);
        feedback(i, "Page changed", now);
    }
    if (i->feedback && now >= i->feedback_until) {
        i->feedback = NULL;
        ++i->revision;
    }
    phase(i);
}
void codex_input_event(codex_input_t *i, codex_button_t b, codex_input_event_t ev, uint64_t now)
{
    if (ev == CODEX_CLICK || ev == CODEX_DOUBLE) return;
    if (ev == CODEX_INVALID || (unsigned)b >= CODEX_BUTTONS) { codex_input_cancel(i, now); return; }
    codex_input_tick(i, now);
    if (ev == CODEX_LONG) return; // elapsed timestamp is authoritative, no BSP repeat
    if (ev == CODEX_RELEASE) {
        if (!i->held[b]) return;
        i->held[b] = false;
        if (!i->blocked && i->owner == (int)b) {
            if (b == CODEX_OK) release(i, now);
            else if (!i->long_handled) {
                unsigned count = (unsigned)codex_page_count(i->page);
                i->focus[i->page] = (uint8_t)((i->focus[i->page] + (b == CODEX_UP ? count - 1 : 1)) % count);
                ++i->revision;
            }
            i->owner = -1;
        }
        if (!i->held[0] && !i->held[1] && !i->held[2]) i->blocked = false;
        phase(i);
        return;
    }
    if (ev != CODEX_PRESS || i->held[b]) return;
    i->held[b] = true;
    if (i->blocked) return;
    if (i->owner >= 0) { codex_input_cancel(i, now); return; }
    i->owner = b;
    i->pressed_ms = now;
    i->long_handled = false;
    if (b != CODEX_OK) {
        i->confirm_id = CODEX_NO_KEY;
        ++i->revision;
        phase(i);
        return;
    }
    const codex_key_t *key = codex_key_at(i->page, i->focus[i->page]);
    if (!key) { feedback(i, "Invalid focus", now); phase(i); return; }
    if (key->kind == CODEX_KEY_LOCAL) {
        i->local_requested = true; feedback(i, "Local settings", now); phase(i); return;
    }
    if (!i->ready) { feedback(i, "HID not ready - not sent", now); phase(i); return; }
    bool guarded = (i->protect_dangerous && key->dangerous) ||
        (i->protect_fork_send && (key->id == CODEX_FORK || key->id == CODEX_SEND));
    if (guarded && i->confirm_id != key->id) {
        i->confirm_id = key->id;
        i->confirm_deadline = now + CODEX_CONFIRM_MS;
        feedback(i, "Press OK again to confirm", now);
        phase(i);
        return;
    }
    i->confirm_id = CODEX_NO_KEY;
    codex_action_t a = {key->id, true, now};
    if (i->emit(i->user, &a)) {
        if (key->kind != CODEX_KEY_DETENT) i->active_pressed_id = key->id;
        feedback(i, key->kind == CODEX_KEY_DETENT ? "One dial step" : "OK held", now);
    } else feedback(i, "Not sent", now);
    phase(i);
}

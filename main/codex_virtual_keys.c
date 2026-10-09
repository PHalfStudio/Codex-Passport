#include "codex_virtual_keys.h"
#include <stdio.h>

static const codex_key_t keys[CODEX_KEY_COUNT] = {
    {CODEX_AG00, "Agent 1", "AG00", CODEX_KEY_HID, 0, 0, false},
    {CODEX_AG01, "Agent 2", "AG01", CODEX_KEY_HID, 1, 0, false},
    {CODEX_AG02, "Agent 3", "AG02", CODEX_KEY_HID, 2, 0, false},
    {CODEX_AG03, "Agent 4", "AG03", CODEX_KEY_HID, 3, 0, false},
    {CODEX_AG04, "Agent 5", "AG04", CODEX_KEY_HID, 4, 0, false},
    {CODEX_AG05, "Agent 6", "AG05", CODEX_KEY_HID, 5, 0, false},
    {CODEX_FAST, "Fast", "ACT06", CODEX_KEY_HID, -1, 0, false},
    {CODEX_APPROVE, "Approve", "ACT07", CODEX_KEY_HID, -1, 0, true},
    {CODEX_DECLINE, "Decline", "ACT08", CODEX_KEY_HID, -1, 0, true},
    {CODEX_FORK, "Fork", "ACT09", CODEX_KEY_HID, -1, 0, false},
    {CODEX_MIC, "Mic / PTT", "ACT10", CODEX_KEY_HID, -1, 0, false},
    {CODEX_SEND, "Send", "ACT12", CODEX_KEY_HID, -1, 0, false},
    {CODEX_JOY_UP, "Stick Up", NULL, CODEX_KEY_RAD, -1, .75f, false},
    {CODEX_JOY_RIGHT, "Stick Right", NULL, CODEX_KEY_RAD, -1, 0, false},
    {CODEX_JOY_DOWN, "Stick Down", NULL, CODEX_KEY_RAD, -1, .25f, false},
    {CODEX_JOY_LEFT, "Stick Left", NULL, CODEX_KEY_RAD, -1, .5f, false},
    {CODEX_DIAL_CW, "Dial CW", "ENC_CW", CODEX_KEY_DETENT, -1, 0, false},
    {CODEX_DIAL_CCW, "Dial CCW", "ENC_CC", CODEX_KEY_DETENT, -1, 0, false},
    {CODEX_DIAL_CLICK, "Dial Click", "ENC_CLK", CODEX_KEY_HID, -1, 0, false},
    {CODEX_LOCAL_SETTINGS, "Settings", NULL, CODEX_KEY_LOCAL, -1, 0, false},
};
size_t codex_page_count(codex_page_t page)
{
    return page == CODEX_NAV_PAGE ? 8 : ((unsigned)page < CODEX_PAGES ? 6 : 0);
}
const char *codex_page_name(codex_page_t page)
{
    static const char *names[] = {"AGENTS", "COMMANDS", "NAV / DIAL"};
    return (unsigned)page < CODEX_PAGES ? names[page] : "?";
}
const codex_key_t *codex_key_by_id(codex_key_id_t id)
{
    return id >= 0 && id < CODEX_KEY_COUNT ? &keys[id] : NULL;
}
const codex_key_t *codex_key_at(codex_page_t page, unsigned focus)
{
    if (focus >= codex_page_count(page)) return NULL;
    return &keys[(page == CODEX_AGENTS_PAGE ? 0 : page == CODEX_COMMANDS_PAGE ? 6 : 12) + focus];
}
bool codex_action_json(const codex_action_t *a, bool alias, char *out, size_t cap)
{
    const codex_key_t *k = codex_key_by_id(a->id);
    if (!k || k->kind == CODEX_KEY_LOCAL || (k->kind == CODEX_KEY_DETENT && !a->down)) return false;
    int n;
    if (k->kind == CODEX_KEY_RAD) {
        // Fixed decimal strings avoid locale and floating printf differences.
        static const char *angles[] = {"0.75", "0.00", "0.25", "0.50"};
        n = snprintf(out, cap, "{\"method\":\"v.oai.rad\",\"params\":{\"a\":%s,\"d\":%d}}",
                     a->down ? angles[k->id - CODEX_JOY_UP] : "0", a->down ? 1 : 0);
    } else {
        const char *wire = alias && k->id == CODEX_DIAL_CLICK ? "ENC" : k->wire_key;
        int act = k->kind == CODEX_KEY_DETENT ? 2 : (a->down ? 1 : 0);
        if (k->agent >= 0)
            n = snprintf(out, cap, "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\",\"act\":%d,\"ag\":%d}}", wire, act, k->agent);
        else
            n = snprintf(out, cap, "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\",\"act\":%d}}", wire, act);
    }
    return n > 0 && (size_t)n < cap;
}

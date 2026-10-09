#include "codex_ui.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *screen, *connection, *battery, *page_label, *page_number;
static lv_obj_t *cards[8], *labels[8], *dots[8], *focus_label, *feedback_label, *debug_label, *accent;
static lv_obj_t *sidebar, *local_rows[CODEX_LOCAL_ITEMS], *local_info, *local_hint;
static uint32_t local_revision = UINT32_MAX, diagnostic_revision = UINT32_MAX;
static int pairing_revision = -1;
static int old_page = -1, old_focus = -1, old_pressed = -2, old_confirm = -2;
static uint32_t colors[8];
static uint32_t old_ambient = UINT32_MAX;
static const uint32_t BG = 0x0B111B, CARD = 0x172130, FOCUS = 0xC8F06E;
static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, const char *text)
{
    lv_obj_t *obj = lv_label_create(parent);
    if (!obj) return NULL;
    lv_obj_set_pos(obj, x, y); lv_obj_set_width(obj, w);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text);
    return obj;
}
static void text(lv_obj_t *obj, const char *value)
{
    if (strcmp(lv_label_get_text(obj), value)) lv_label_set_text(obj, value);
}
bool codex_ui_create(void)
{
    screen = lv_obj_create(NULL);
    if (!screen) return false;
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xEDF3FA), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(screen, 18, 12, 165, "CODEX PASSPORT");
    battery = label(screen, 181, 12, 44, "--%");
    connection = label(screen, 18, 34, 158, "Disconnected");
    page_label = label(screen, 14, 57, 185, "AGENTS");
    page_number = label(screen, 194, 61, 36, "1/3");
    if (!title || !battery || !connection || !page_label || !page_number) return false;
    lv_obj_set_style_text_font(page_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(connection, lv_color_hex(0x9CAABE), 0);
    accent = lv_obj_create(screen);
    if (!accent) return false;
    lv_obj_remove_style_all(accent); lv_obj_set_pos(accent, 14, 81); lv_obj_set_size(accent, 212, 2);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x3A485A), 0);
    for (unsigned j = 0; j < 8; ++j) {
        cards[j] = lv_obj_create(screen);
        if (!cards[j]) return false;
        lv_obj_remove_style_all(cards[j]);
        lv_obj_remove_flag(cards[j], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(cards[j], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(cards[j], lv_color_hex(CARD), 0);
        lv_obj_set_style_border_width(cards[j], 2, 0);
        lv_obj_set_style_border_color(cards[j], lv_color_hex(0x2B3748), 0);
        lv_obj_set_style_radius(cards[j], 8, 0);
        labels[j] = label(cards[j], 8, 18, 84, "");
        dots[j] = lv_obj_create(cards[j]);
        if (!labels[j] || !dots[j]) return false;
        lv_obj_remove_style_all(dots[j]); lv_obj_set_size(dots[j], 10, 5);
        lv_obj_set_pos(dots[j], 8, 7); lv_obj_set_style_radius(dots[j], 2, 0);
        lv_obj_set_style_bg_opa(dots[j], LV_OPA_COVER, 0);
        colors[j] = UINT32_MAX;
    }
    focus_label = label(screen, 14, 251, 214, "Focus: Agent 1");
    feedback_label = label(screen, 14, 273, 214, "Hold UP/DOWN: page");
    debug_label = label(screen, 18, 295, 205, "UP / DOWN select   OK act");
    if (!focus_label || !feedback_label || !debug_label) return false;
    lv_obj_set_style_text_color(feedback_label, lv_color_hex(0xA7B6C9), 0);
    lv_obj_set_style_text_color(debug_label, lv_color_hex(0x8B9AAF), 0);
    lv_screen_load(screen);
    return true;
}
void codex_ui_render(const codex_input_t *i, const codex_state_t *s, uint64_t now)
{
    if (!screen) return;
    char buf[96];
    text(connection, codex_connection_name(s->connection));
    snprintf(buf, sizeof(buf), s->battery < 0 ? "--%%" : "%d%%", s->battery);
    text(battery, buf);
    const codex_key_t *focused = codex_key_at(i->page, i->focus[i->page]);
    bool page_changed = old_page != (int)i->page;
    if (page_changed) {
        text(page_label, codex_page_name(i->page));
        snprintf(buf, sizeof(buf), "%d/3", i->page + 1); text(page_number, buf);
        for (unsigned j = 0; j < 8; ++j) {
            const codex_key_t *key = codex_key_at(i->page, j);
            if (!key) { lv_obj_add_flag(cards[j], LV_OBJ_FLAG_HIDDEN); continue; }
            lv_obj_remove_flag(cards[j], LV_OBJ_FLAG_HIDDEN);
            bool nav = i->page == CODEX_NAV_PAGE;
            if (nav) lv_obj_add_flag(dots[j], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_remove_flag(dots[j], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(cards[j], 12 + (j % 2) * 114, 91 + (j / 2) * (nav ? 38 : 51));
            lv_obj_set_size(cards[j], 102, nav ? 33 : 44);
            lv_obj_set_pos(labels[j], 7, nav ? 9 : 18);
            text(labels[j], key->label);
            colors[j] = UINT32_MAX;
        }
        old_page = i->page;
    }
    if (page_changed || old_focus != i->focus[i->page] || old_pressed != (int)i->active_pressed_id || old_confirm != (int)i->confirm_id) {
        for (unsigned j = 0; j < codex_page_count(i->page); ++j) {
            const codex_key_t *key = codex_key_at(i->page, j);
            bool focus = j == i->focus[i->page], held = key->id == i->active_pressed_id;
            bool confirm = key->id == i->confirm_id;
            uint32_t border = confirm ? 0xFFAF45 : held ? 0xFFFFFF : focus ? FOCUS : 0x2B3748;
            lv_obj_set_style_border_color(cards[j], lv_color_hex(border), 0);
            lv_obj_set_style_border_width(cards[j], focus || held || confirm ? 3 : 1, 0);
            lv_obj_set_style_bg_color(cards[j], lv_color_hex(held ? 0x315064 : confirm ? 0x493318 : CARD), 0);
        }
        old_focus = i->focus[i->page]; old_pressed = i->active_pressed_id; old_confirm = i->confirm_id;
    }
    for (unsigned j = 0; j < codex_page_count(i->page); ++j) {
        uint32_t color = i->page == CODEX_AGENTS_PAGE ? codex_light_color(&s->agents[j], now) :
            i->page == CODEX_COMMANDS_PAGE && (j == 1 || j == 2) ? 0xFFAF45 : 0x586B83;
        if (colors[j] != color) {
            lv_obj_set_style_bg_color(dots[j], lv_color_hex(color), 0); colors[j] = color;
        }
    }
    uint32_t ambient = s->ambient.known ? codex_light_color(&s->ambient, now) : 0x3A485A;
    if (old_ambient != ambient) {
        lv_obj_set_style_bg_color(accent, lv_color_hex(ambient), 0);
        old_ambient = ambient;
    }
    if (i->confirm_id != CODEX_NO_KEY) {
        snprintf(buf, sizeof(buf), "CONFIRM %s (%llus)", focused->label,
                 (unsigned long long)((i->confirm_deadline > now ? i->confirm_deadline - now : 0) + 999) / 1000);
        text(focus_label, buf); text(feedback_label, "OK again / UP DOWN cancel");
    } else {
        snprintf(buf, sizeof(buf), "%s%s", i->active_pressed_id == CODEX_NO_KEY ? "Focus: " : "Held: ", focused->label);
        text(focus_label, buf);
        const char *hint = focused->id == CODEX_LOCAL_SETTINGS ? "OK: local sidebar" : focused->id == CODEX_MIC ? "Hold OK: computer mic" :
            focused->id == CODEX_DIAL_CLICK ? "Hold OK: dial long press" : "Hold UP/DOWN: page";
        if (i->feedback) hint = i->feedback;
        else if (s->last_error) { snprintf(buf, sizeof(buf), "BLE error %d", s->last_error); hint = buf; }
        else if (s->encrypted && s->mtu < 66) hint = "Host MTU too small";
        else if (s->connection == CODEX_HOST_ACTIVE && !s->subscribed) hint = "Waiting for notifications";
        text(feedback_label, hint);
    }
    // Diagnostics alternate with the legend, without host payloads or private data.
    if ((now / 5000) % 2) {
        snprintf(buf, sizeof(buf), "RX %lu  ERR %lu  TX! %lu",
                 (unsigned long)s->stats.messages_rx, (unsigned long)s->stats.rx_errors,
                 (unsigned long)s->stats.tx_failures);
        text(debug_label, buf);
    } else text(debug_label, "UP DOWN select  OK act");
}
void codex_ui_local(const codex_local_t *l, const codex_state_t *s, uint64_t now)
{
    (void)now;
    if (!l->open) { if (sidebar) lv_obj_add_flag(sidebar, LV_OBJ_FLAG_HIDDEN); return; }
    if (sidebar && !lv_obj_has_flag(sidebar, LV_OBJ_FLAG_HIDDEN) && local_revision == l->revision &&
        pairing_revision == (int)s->pairing_reset &&
        (l->mode != CODEX_LOCAL_DIAGNOSTICS || diagnostic_revision == s->revision)) return;
    if (!sidebar) {
        sidebar = lv_obj_create(screen);
        if (!sidebar) return;
        lv_obj_remove_style_all(sidebar); lv_obj_remove_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(sidebar, 43, 26); lv_obj_set_size(sidebar, 185, 270);
        lv_obj_set_style_bg_color(sidebar, lv_color_hex(0x101E2F), 0);
        lv_obj_set_style_bg_opa(sidebar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sidebar, 2, 0);
        lv_obj_set_style_border_color(sidebar, lv_color_hex(0x91A7C2), 0);
        lv_obj_set_style_radius(sidebar, 9, 0);
        lv_obj_t *title = label(sidebar, 12, 13, 155, "DEVICE");
        if (!title) return;
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        static const char *names[] = {"Screen Off", "Brightness", "Re-pair", "Diagnostics", "Back"};
        for (unsigned j = 0; j < CODEX_LOCAL_ITEMS; ++j) {
            local_rows[j] = label(sidebar, 12, 53 + j * 31, 155, names[j]);
            if (!local_rows[j]) return;
            lv_obj_set_height(local_rows[j], 24);
        }
        local_info = label(sidebar, 12, 52, 155, "");
        local_hint = label(sidebar, 12, 222, 155, "UP DOWN: select\nOK: open");
        if (!local_info || !local_hint) return;
        lv_obj_set_style_text_color(local_hint, lv_color_hex(0xADBCD0), 0);
    }
    lv_obj_remove_flag(sidebar, LV_OBJ_FLAG_HIDDEN);
    bool menu = l->mode == CODEX_LOCAL_MENU;
    for (unsigned j = 0; j < CODEX_LOCAL_ITEMS; ++j) {
        if (menu) lv_obj_remove_flag(local_rows[j], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(local_rows[j], LV_OBJ_FLAG_HIDDEN);
        if (menu) {
            bool selected = j == l->focus;
            uint32_t bg = selected ? (l->confirm ? 0xFFAF45 : FOCUS) : 0x101E2F;
            if (lv_color_to_u32(lv_obj_get_style_bg_color(local_rows[j], 0)) != lv_color_to_u32(lv_color_hex(bg)))
                lv_obj_set_style_bg_color(local_rows[j], lv_color_hex(bg), 0);
            lv_obj_set_style_bg_opa(local_rows[j], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(local_rows[j], lv_color_hex(selected ? 0x0B111B : 0xEDF3FA), 0);
        }
    }
    char buf[260];
    if (menu) lv_obj_add_flag(local_info, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_remove_flag(local_info, LV_OBJ_FLAG_HIDDEN);
        if (l->mode == CODEX_LOCAL_BRIGHTNESS)
            snprintf(buf, sizeof(buf), "Brightness\n\n%u%%\n\nUP +5 / DOWN -5", l->brightness);
        else {
            char bat[16];
            if (s->battery < 0) strcpy(bat, "unknown");
            else snprintf(bat, sizeof(bat), "%d%%", s->battery);
            snprintf(buf, sizeof(buf), "ENC %s / NTF %s\nMTU %u\nBattery %s\nCharging %s\nRX %lu ERR %lu\nDROP %lu\nTX FAIL %lu\n%s",
                s->encrypted ? "yes" : "no", s->subscribed ? "yes" : "no", s->mtu,
                bat, s->charging < 0 ? "unknown" : s->charging ? "yes" : "no", (unsigned long)s->stats.messages_rx,
                (unsigned long)s->stats.rx_errors, (unsigned long)s->stats.rx_dropped,
                (unsigned long)s->stats.tx_failures, s->last_method[0] ? s->last_method : "No host RPC");
        }
        text(local_info, buf);
    }
    const char *hint = menu ? "UP DOWN: select\nOK: open" : "OK: Back";
    if (l->confirm) hint = "OK again: forget\nUP DOWN: cancel";
    else if (menu && l->focus == CODEX_LOCAL_PAIR) {
        if (s->pairing_reset == CODEX_PAIR_PENDING) hint = "Clearing pairing...";
        else if (s->pairing_reset == CODEX_PAIR_DONE) hint = "Pairing cleared\nForget on Mac";
        else if (s->pairing_reset == CODEX_PAIR_FAILED) hint = "Re-pair failed\nOK: retry";
    }
    text(local_hint, hint);
    local_revision = l->revision; diagnostic_revision = s->revision; pairing_revision = s->pairing_reset;
}

#include "codex_protocol.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

// Interface facts only: one vendor collection, no keyboard emulation.
const uint8_t codex_report_map[] = {
    0x06,0x00,0xFF, 0x09,0x01, 0xA1,0x01, 0x85,0x06,
    0x15,0x00, 0x26,0xFF,0x00, 0x75,0x08, 0x95,0x3F,
    0x09,0x02, 0x81,0x02, 0x95,0x3F, 0x09,0x03, 0x91,0x02, 0xC0
};
const size_t codex_report_map_size = sizeof(codex_report_map);
// PnP fields are explicitly little endian, NOT Arduino's pre-swapped values.
const uint8_t codex_pnp_id[7] = {0x02,0x3A,0x30,0x60,0x83,0x01,0x01};
void codex_protocol_reset(codex_protocol_t *p)
{
    p->used = p->depth = p->tokens = 0;
    p->string = p->escape = p->discard = false;
    p->last_rx_ms = 0;
}
void codex_protocol_init(codex_protocol_t *p, codex_state_t *s, codex_report_send_fn send, void *user)
{
    memset(p, 0, sizeof(*p));
    p->state = s; p->send = send; p->user = user;
}
void codex_protocol_tick(codex_protocol_t *p, uint64_t now)
{
    if ((p->used || p->discard) && now - p->last_rx_ms >= CODEX_RX_TIMEOUT_MS) {
        ++p->state->stats.rx_timeouts;
        ++p->state->stats.rx_dropped;
        codex_protocol_reset(p);
    }
}
size_t codex_report_pack(const char *text, size_t len, uint8_t body[CODEX_REPORT_BODY])
{
    size_t n = len < CODEX_FRAGMENT_SIZE ? len : CODEX_FRAGMENT_SIZE;
    memset(body, 0, CODEX_REPORT_BODY);
    body[0] = 2; body[1] = (uint8_t)n;
    if (n) memcpy(body + 2, text, n);
    return n;
}
bool codex_protocol_send(codex_protocol_t *p, const char *json, size_t len)
{
    if (!json || !len || len > sizeof(p->response) - 1) return false;
    size_t offset = 0;
    // Include the LF in the final report whenever it fits, otherwise send its own.
    while (offset <= len) {
        uint8_t body[CODEX_REPORT_BODY];
        size_t n = codex_report_pack(json + offset, len - offset, body);
        offset += n;
        if (offset == len && n < CODEX_FRAGMENT_SIZE) {
            body[2 + n] = '\n'; body[1] = (uint8_t)(n + 1); ++offset;
        }
        if (!p->send(p->user, body)) { ++p->state->stats.tx_failures; return false; }
        ++p->state->stats.tx_reports;
    }
    return true;
}
static const cJSON *field(const cJSON *o, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(o, name);
}
static bool number(const cJSON *v, double low, double high)
{
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= low && v->valuedouble <= high;
}
static bool integer(const cJSON *v, double low, double high)
{
    return number(v, low, high) && floor(v->valuedouble) == v->valuedouble;
}
static bool light_update(codex_light_t *l, const cJSON *obj)
{
    if (!cJSON_IsObject(obj)) return false;
    codex_light_t n = *l;
    const cJSON *v = field(obj, "c");
    if (v) { if (!integer(v, 0, 0xFFFFFF)) return false; n.color = (uint32_t)v->valuedouble; }
    v = field(obj, "b");
    if (v) { if (!number(v, 0, 1)) return false; n.brightness = (float)v->valuedouble; }
    v = field(obj, "s");
    if (v) { if (!number(v, 0, 1)) return false; n.speed = (float)v->valuedouble; }
    v = field(obj, "m");
    if (v) { if (!number(v, 0, 1)) return false; n.magic = (float)v->valuedouble; }
    v = field(obj, "e");
    if (v) {
        if (cJSON_IsString(v)) {
            n.effect = strcmp(v->valuestring, "off") == 0 ? CODEX_LIGHT_OFF :
                       strcmp(v->valuestring, "breath") == 0 ? CODEX_LIGHT_BREATH : CODEX_LIGHT_SOLID;
            n.effect_code = n.effect == CODEX_LIGHT_BREATH ? 4 : n.effect == CODEX_LIGHT_OFF ? 0 : 1;
        } else if (integer(v, 0, 255)) {
            n.effect_code = v->valueint;
            n.effect = v->valueint == 0 ? CODEX_LIGHT_OFF :
                (v->valueint == 4 || v->valueint == 6) ? CODEX_LIGHT_BREATH : CODEX_LIGHT_SOLID;
        } else return false;
    }
    const char *names[] = {"sk", "sa"};
    bool *dest[] = {&n.sync_keys, &n.sync_ambient};
    for (unsigned i = 0; i < 2; ++i) {
        v = field(obj, names[i]);
        if (v) {
            if (!cJSON_IsBool(v) && !integer(v, 0, 1)) return false;
            *dest[i] = cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valueint == 1);
        }
    }
    // Missing fields retain the previous slot, including the neutral unknown state.
    if (field(obj, "c")) n.known = true;
    *l = n;
    return true;
}
static bool valid_utf8(const unsigned char *s, size_t length)
{
    for (size_t i = 0; i < length;) {
        unsigned c = s[i++], extra, value;
        if (c < 128) continue;
        if (c >= 0xC2 && c <= 0xDF) { extra = 1; value = c & 31; }
        else if (c >= 0xE0 && c <= 0xEF) { extra = 2; value = c & 15; }
        else if (c >= 0xF0 && c <= 0xF4) { extra = 3; value = c & 7; }
        else return false;
        if (i + extra > length) return false;
        unsigned count = extra;
        while (extra--) {
            unsigned next = s[i++];
            if ((next & 0xC0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        if ((count == 1 && value < 128) || (count == 2 && value < 2048) ||
            (count == 3 && value < 65536) || value > 0x10FFFF ||
            (value >= 0xD800 && value <= 0xDFFF)) return false;
    }
    return true;
}
static void rpc(codex_protocol_t *p)
{
    if (!valid_utf8((const unsigned char *)p->rx, p->used)) {
        ++p->state->stats.rx_errors;
        return;
    }
    const char *end = NULL;
    cJSON *req = cJSON_ParseWithLengthOpts(p->rx, p->used + 1, &end, true);
    const cJSON *method = field(req, "method");
    if (!method) method = field(req, "m");
    const cJSON *params = field(req, "params");
    if (!params) params = field(req, "p");
    const cJSON *id = field(req, "id");
    if (!cJSON_IsObject(req) || !cJSON_IsString(method) ||
        strlen(method->valuestring) >= sizeof(p->state->last_method) ||
        (id && !cJSON_IsString(id) && !cJSON_IsNumber(id) && !cJSON_IsNull(id))) {
        ++p->state->stats.rx_errors; cJSON_Delete(req); return;
    }
    ++p->state->stats.messages_rx;
    size_t method_length = strlen(method->valuestring);
    for (size_t j = 0; j < method_length; ++j) {
        unsigned char c = (unsigned char)method->valuestring[j];
        p->state->last_method[j] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ? (char)c : '?';
    }
    p->state->last_method[method_length] = 0;
    p->state->connection = CODEX_HOST_ACTIVE;
    ++p->state->revision;
    const char *m = method->valuestring;
    cJSON *result = cJSON_CreateObject();
    int error = 0;
    if (!result) { ++p->state->stats.rx_errors; cJSON_Delete(req); return; }
    if (!strcmp(m, "sys.version")) {
        cJSON_AddStringToObject(result, "version", CODEX_VERSION);
    } else if (!strcmp(m, "device.status")) {
        cJSON_AddStringToObject(result, "version", CODEX_VERSION);
        cJSON_AddNumberToObject(result, "profile_index", 0);
        cJSON_AddNumberToObject(result, "layer_index", 1);
        if (p->state->battery >= 0) cJSON_AddNumberToObject(result, "battery", p->state->battery);
        else cJSON_AddNullToObject(result, "battery");
        if (p->state->charging >= 0) cJSON_AddBoolToObject(result, "is_charging", p->state->charging != 0);
        else cJSON_AddNullToObject(result, "is_charging");
        cJSON_AddBoolToObject(result, "battery_available", p->state->battery >= 0);
        cJSON_AddBoolToObject(result, "charging_available", p->state->charging >= 0);
    } else if (!strcmp(m, "v.oai.thstatus")) {
        if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) > 32) error = -32602;
        else {
            const cJSON *entry;
            cJSON_ArrayForEach(entry, params) {
                const cJSON *slot = field(entry, "id");
                if (!integer(slot, 0, CODEX_AGENTS - 1) ||
                    !light_update(&p->state->agents[slot ? slot->valueint : 0], entry)) {
                    ++p->state->stats.rx_errors; error = -32602;
                }
            }
        }
        cJSON_AddBoolToObject(result, "ok", !error);
    } else if (!strcmp(m, "v.oai.rgbcfg")) {
        if (!cJSON_IsObject(params)) error = -32602;
        else {
            const cJSON *a = field(params, "ambient"), *k = field(params, "keys");
            if ((a && !light_update(&p->state->ambient, a)) ||
                (k && !light_update(&p->state->keys, k))) error = -32602;
        }
        cJSON_AddBoolToObject(result, "ok", !error);
    } else if (!strcmp(m, "host.focused_app") || !strcmp(m, "lights.preview")) {
        cJSON_AddBoolToObject(result, "ok", true);
    } else error = -32601;
    if (id) {
        cJSON *response = cJSON_CreateObject();
        cJSON *copy = cJSON_Duplicate(id, true);
        if (response && copy) {
            cJSON_AddItemToObject(response, "id", copy);
            if (field(req, "jsonrpc")) cJSON_AddStringToObject(response, "jsonrpc", "2.0");
            if (error) {
                cJSON *err = cJSON_AddObjectToObject(response, "error");
                cJSON_AddNumberToObject(err, "code", error);
                cJSON_AddStringToObject(err, "message", error == -32601 ? "Method not found" : "Invalid params");
            } else {
                cJSON_AddItemToObject(response, "result", result); result = NULL;
            }
            if (cJSON_PrintPreallocated(response, p->response, sizeof(p->response), false))
                codex_protocol_send(p, p->response, strlen(p->response));
            else ++p->state->stats.tx_failures;
        } else { cJSON_Delete(copy); ++p->state->stats.tx_failures; }
        cJSON_Delete(response);
    }
    cJSON_Delete(result);
    cJSON_Delete(req);
}
static void bad(codex_protocol_t *p)
{
    ++p->state->stats.rx_errors;
    ++p->state->stats.rx_dropped;
    codex_protocol_reset(p);
}
bool codex_protocol_receive(codex_protocol_t *p, const uint8_t *data, size_t size, uint64_t now)
{
    codex_protocol_tick(p, now);
    if (!data || (size != CODEX_REPORT_BODY && size != CODEX_REPORT_BODY + 1)) { bad(p); return false; }
    if (size == CODEX_REPORT_BODY + 1) {
        if (*data != CODEX_REPORT_ID) { bad(p); return false; }
        ++data;
    }
    if (data[0] != 2 || data[1] > CODEX_FRAGMENT_SIZE) { bad(p); return false; }
    ++p->state->stats.reports_rx;
    size_t len = data[1];
    const char *chunk = (const char *)data + 2;
    // No sequence field exists. A recognizable new RPC at a report boundary
    // is a recovery point after a missing fragment, not a nested-lighting object.
    if ((p->used || p->discard) && len >= 5 &&
        ((len >= 9 && !memcmp(chunk, "{\"method\"", 9)) ||
         (len >= 10 && !memcmp(chunk, "{\"jsonrpc\"", 10)) || !memcmp(chunk, "{\"m\":", 5))) bad(p);
    p->last_rx_ms = now;
    for (size_t j = 0; j < len; ++j) {
        unsigned char c = (unsigned char)chunk[j];
        if (p->discard) { if (c == '\n' || c == '\r') codex_protocol_reset(p); continue; }
        if (!p->used) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
            if (c != '{') { ++p->state->stats.rx_errors; continue; }
        }
        if (!c || p->used == CODEX_JSON_MAX || (c < 32 && c != '\n' && c != '\r' && c != '\t')) {
            bad(p); p->discard = true; continue;
        }
        p->rx[p->used++] = (char)c;
        if (p->string) {
            if (p->escape) p->escape = false;
            else if (c == '\\') p->escape = true;
            else if (c == '"') p->string = false;
            else if (c == '\n' || c == '\r') { bad(p); continue; }
        } else {
            if (c == '"') { p->string = true; ++p->tokens; }
            if (c == '{' || c == '[') { ++p->depth; ++p->tokens; }
            if (c == '}' || c == ']') {
                if (!p->depth) { bad(p); continue; }
                --p->depth;
            }
            if (c == ',') ++p->tokens;
            if (p->depth > 16 || p->tokens > 256) { bad(p); p->discard = true; continue; }
            if (!p->depth) {
                p->rx[p->used] = '\0';
                rpc(p);
                codex_protocol_reset(p);
                p->last_rx_ms = now;
            } else if (c == '\n' || c == '\r') {
                // A delimiter on incomplete JSON means a dropped/malformed message.
                bad(p);
            }
        }
    }
    return true;
}

// Platform fixtures only; permission decisions use actual ESP-IDF SDK functions.
struct ble_att_svr_entry { uint8_t ha_flags,ha_min_key_size; uint16_t ha_handle_id; };
struct ble_hs_conn { struct ble_gap_sec_state bhc_sec_state; };
struct ble_hs_conn_addrs { ble_addr_t peer_id_addr; };
static void ble_hs_lock(void) {}
static void ble_hs_unlock(void) {}
static void ble_att_svr_get_sec_state(uint16_t h,struct ble_gap_sec_state *s)
{ assert(h==link.connection); *s=peer.sec_state; }
static struct ble_hs_conn *ble_hs_conn_find(uint16_t h)
{ static struct ble_hs_conn c; assert(h==link.connection); c.bhc_sec_state=peer.sec_state; return &c; }
static void ble_hs_conn_addrs(struct ble_hs_conn *c,struct ble_hs_conn_addrs *a)
{ (void)c; a->peer_id_addr=peer.peer_id_addr; }
static int ble_gap_authorize_event(uint16_t h,uint16_t a,int read)
{ (void)h; (void)a; (void)read; return BLE_GAP_AUTHORIZE_REJECT; }
#include "codex_nimble_permissions.inc"

static uint8_t att_result(const struct ble_gatt_chr_def *c,bool cccd,bool read)
{
    struct ble_att_svr_entry entry={
        .ha_flags=cccd ? ble_gatts_chr_clt_cfg_flags_from_chr_flags(c->flags) :
                        ble_gatts_att_flags_from_chr_flags(c->flags),
        .ha_min_key_size=cccd ? 0 : c->min_key_size
    };
    uint8_t error=0;
    (void)ble_att_svr_check_perms(link.connection,read,&entry,&error);
    return error;
}
static void security_regression(void)
{
    const struct ble_gatt_chr_def *c=services[1].characteristics;
    const struct ble_gatt_chr_def *b=battery_services[0].characteristics;
    peer.sec_state=(struct ble_gap_sec_state){.encrypted=true,.bonded=true,.key_size=16};
    // Reproduce the exact macOS error using the previous SC-only configuration.
    int configured=ble_hs_cfg.sm_sc_only;
    ble_hs_cfg.sm_sc_only=1;
    assert(att_result(&c[4],true,false)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
    assert(att_result(&c[5],false,false)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
    ++cases;
    ble_hs_cfg.sm_sc_only=configured;
    // Production configuration must permit SC Just Works, not require MITM.
    assert(att_result(&c[4],true,false)==0);
    assert(att_result(&c[4],false,true)==0);
    assert(att_result(&c[5],false,false)==0);
    assert(att_result(&c[5],false,true)==0);
    assert(att_result(&c[2],false,false)==0);
    assert(att_result(&c[3],false,false)==0);
    assert(att_result(b,true,false)==0 && att_result(b,false,true)==0);
    ++cases;
    peer.sec_state.encrypted=false;
    assert(att_result(&c[4],true,false)==BLE_ATT_ERR_INSUFFICIENT_ENC);
    assert(att_result(&c[5],false,false)==BLE_ATT_ERR_INSUFFICIENT_ENC);
    assert(att_result(&c[4],false,true)==BLE_ATT_ERR_INSUFFICIENT_ENC);
    ++cases;
    peer.sec_state.encrypted=true;peer.sec_state.key_size=7;
    assert(att_result(&c[5],false,false)==BLE_ATT_ERR_INSUFFICIENT_KEY_SZ);
    assert(att_result(&c[4],false,true)==BLE_ATT_ERR_INSUFFICIENT_KEY_SZ);
    peer.sec_state.key_size=16;
    ++cases;
}

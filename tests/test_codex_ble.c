#include "../main/codex_ble_hid.c"
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

struct test_hs_cfg ble_hs_cfg;
struct test_queue { unsigned depth,size,head,count; uint8_t data[32][128]; };
static uint64_t clock_ms=10;
static unsigned adv_count,notify_count,terminate_count,delete_count,reset_count,cases;
static bool advertising,fail_queue,fail_notify,fail_mbuf;
static int fail_nimble,fail_gatts,fail_task,task_calls,fail_adv;
static int bonded_count=1,store_delete_calls,unpair_calls,fail_unpair,fail_store_count;
static bool persistent_dirty;
static struct ble_gap_conn_desc peer={.sec_state={.encrypted=true,.bonded=true,.key_size=16}};
static uint8_t notifications[200][63];
void test_log(const char *fmt,...) { (void)fmt; }
int nvs_entry_find(const char *part,const char *name,int type,nvs_iterator_t *it) {
    assert(!strcmp(part,"nvs") && !strcmp(name,"nimble_bond") && type==NVS_TYPE_ANY);
    *it=persistent_dirty?(void *)1:NULL;return persistent_dirty?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
void nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t *info) { assert(it);strcpy(info->key,"peer_sec_1"); }
int nvs_entry_next(nvs_iterator_t *it) { *it=NULL;return ESP_ERR_NVS_NOT_FOUND; }
void nvs_release_iterator(nvs_iterator_t it) { (void)it; }
int64_t esp_timer_get_time(void) { return (int64_t)clock_ms*1000; }
QueueHandle_t xQueueCreate(unsigned depth,unsigned size) {
    if(fail_queue) return NULL;
    assert(depth<=32 && size<=128); struct test_queue *q=calloc(1,sizeof(*q)); assert(q); q->depth=depth;q->size=size;return q;
}
int xQueueSend(QueueHandle_t q,const void *item,unsigned timeout) {
    assert(timeout==0 && q); if(q->count==q->depth) return 0;
    memcpy(q->data[(q->head+q->count)%q->depth],item,q->size); ++q->count; return 1;
}
int xQueueReceive(QueueHandle_t q,void *item,unsigned timeout) {
    (void)timeout; if(!q->count) return 0;
    memcpy(item,q->data[q->head],q->size); q->head=(q->head+1)%q->depth;--q->count;return 1;
}
void vQueueDelete(QueueHandle_t q) { free(q); }
void vTaskDelay(unsigned ticks) { clock_ms+=ticks; }
void vTaskDelete(void *task) { (void)task; }
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void **handle) {
    (void)task;(void)name;(void)stack;(void)arg;(void)priority;
    if(++task_calls==fail_task) return 0;
    if(handle) *handle=(void *)1;
    return 1;
}
int os_mbuf_append(struct os_mbuf *om,const void *data,size_t len) {
    assert(om->length+len<=sizeof(om->data));memcpy(om->data+om->length,data,len);om->length+=len;return 0;
}
int os_mbuf_copydata(struct os_mbuf *om,unsigned offset,size_t len,void *out) {
    if(offset+len>om->length) return -1;
    memcpy(out,om->data+offset,len);return 0;
}
struct os_mbuf *ble_hs_mbuf_from_flat(const void *data,size_t len) {
    if(fail_mbuf) return NULL;
    struct os_mbuf *om=calloc(1,sizeof(*om));assert(om);os_mbuf_append(om,data,len);return om;
}
int ble_gatts_notify_custom(uint16_t conn,uint16_t handle,struct os_mbuf *om) {
    assert(conn==link.connection && handle==input_handle && om->length==63 && notify_count<200);
    int rc=fail_notify ? 5 : 0;
    if(!rc) memcpy(notifications[notify_count++],om->data,63);
    free(om);return rc;
}
void ble_gatts_chr_updated(uint16_t h) { assert(h==battery_handle); }
int ble_gap_terminate(uint16_t conn,int reason) { assert(conn==link.connection && reason==0x13);++terminate_count;return 0; }
void ble_hs_sched_reset(int r) { assert(r);++reset_count; }
int ble_gap_adv_active(void) { return advertising; }
int ble_gap_adv_stop(void) { advertising=false;return 0; }
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *f) {
    assert(f->name_len==11 && !memcmp(f->name,"Codex Micro",11) && f->appearance==0x03C0);
    assert(f->uuids16->value==0x1812 && f->num_uuids16==1); return 0;
}
int ble_gap_adv_start(uint8_t addr,const void *peer_addr,int duration,const struct ble_gap_adv_params *p,int (*cb)(struct ble_gap_event *,void *),void *u) {
    (void)addr;(void)peer_addr;(void)duration;(void)cb;(void)u;assert(p->conn_mode==BLE_GAP_CONN_MODE_UND);
    ++adv_count;if(fail_adv) return 5;advertising=true;return 0;
}
int ble_gap_security_initiate(uint16_t conn) { assert(conn==link.connection);return 0; }
int ble_gap_conn_find(uint16_t conn,struct ble_gap_conn_desc *d) { assert(conn==link.connection);*d=peer;return 0; }
int ble_store_util_delete_peer(const ble_addr_t *a) { (void)a;++delete_count;return 0; }
int ble_store_util_bonded_peers(ble_addr_t *a,int *count,int max) { assert(max==1);*count=bonded_count;a[0]=(ble_addr_t){.type=0};return 0; }
int ble_store_read_peer_sec(const struct ble_store_key_sec *k,struct ble_store_value_sec *v) { v->peer_addr=k->peer_addr;v->irk_present=true;v->ltk_present=true;return 0; }
int ble_store_write_peer_sec(const struct ble_store_value_sec *s) { (void)s;return 0; }
int ble_gap_unpair(const ble_addr_t *a) { (void)a;++unpair_calls;if(fail_unpair) return 9;bonded_count=0;return 0; }
int ble_gap_rd_local_resolv_addr(uint8_t t,const ble_addr_t *a,uint8_t *out) { (void)t;(void)a;(void)out;return BLE_HS_HCI_ERR(BLE_ERR_UNK_CONN_ID); }
int ble_store_util_delete_all(int type,const union ble_store_key *key) { (void)type;assert(key->sec.peer_addr.type==255);++store_delete_calls;return 0; }
int ble_store_util_count(int type,int *out) { (void)type;*out=fail_store_count?1:0;return 0; }
void ble_store_util_status_rr(void) {}
void ble_store_config_init(void) {}
int ble_hs_util_ensure_addr(int v) { assert(!v);return 0; }
int ble_hs_id_infer_auto(int v,uint8_t *out) { assert(!v);*out=0;return 0; }
void nimble_port_run(void) {}
int nimble_port_init(void) { return fail_nimble; }
int nimble_port_deinit(void) { return 0; }
void *nimble_port_get_dflt_eventq(void) { return (void *)1; }
void ble_npl_event_init(struct ble_npl_event *e,void (*cb)(struct ble_npl_event *),void *u) { (void)u;e->fn=cb;e->queued=false; }
void ble_npl_event_deinit(struct ble_npl_event *e) { e->queued=false;e->fn=NULL; }
bool ble_npl_event_is_queued(struct ble_npl_event *e) { return e->queued; }
void ble_npl_eventq_put(void *q,struct ble_npl_event *e) { (void)q;e->queued=true; }
int ble_npl_callout_init(struct ble_npl_callout *c,void *q,void (*cb)(struct ble_npl_event *),void *u) { (void)q;(void)u;c->fn=cb;return 0; }
int ble_npl_callout_reset(struct ble_npl_callout *c,unsigned ticks) { c->delay=ticks;return 0; }
void ble_npl_callout_stop(struct ble_npl_callout *c) { c->delay=0; }
void ble_npl_callout_deinit(struct ble_npl_callout *c) { c->fn=NULL; }
unsigned ble_npl_time_ms_to_ticks32(unsigned v) { return v; }
void ble_svc_gap_init(void) {}
void ble_svc_gatt_init(void) {}
int ble_svc_gap_device_name_set(const char *s) { assert(!strcmp(s,"Codex Micro"));return 0; }
int ble_svc_gap_device_appearance_set(unsigned v) { assert(v==0x03C0);return 0; }
int ble_att_set_preferred_mtu(unsigned v) { assert(v==247);return 0; }
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *s) { assert(s);return fail_gatts; }
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *s) {
    for(;s->type;++s) for(const struct ble_gatt_chr_def *c=s->characteristics;c->uuid;++c)
        if(c->val_handle) *c->val_handle=c->uuid->value==0x2A19?20:40;
    return 0;
}
static void drain(void) { work_t w;while(xQueueReceive(queue,&w,0)) worker_process(&w); }
static void connect_peer(void) {
    advertising=false;struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.conn_handle=1}};
    gap_event(&e,NULL);e.type=BLE_GAP_EVENT_ENC_CHANGE;e.enc_change.conn_handle=1;gap_event(&e,NULL);
    e.type=BLE_GAP_EVENT_MTU;e.mtu.conn_handle=1;e.mtu.value=247;gap_event(&e,NULL);
    e.type=BLE_GAP_EVENT_SUBSCRIBE;e.subscribe.conn_handle=1;e.subscribe.attr_handle=input_handle;e.subscribe.cur_notify=1;gap_event(&e,NULL);
}
static int receive_rpc(const char *text,bool prefix) {
    struct os_mbuf om={0};size_t n=codex_report_pack(text,strlen(text),om.data+(prefix?1:0));
    assert(n==strlen(text));if(prefix) om.data[0]=6;om.length=prefix?64:63;
    struct ble_gatt_access_ctxt c={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&om};
    return access_attr(1,0,&c,ARG(ATTR_OUTPUT));
}
static void disconnect_peer(void) { struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT,.disconnect={.reason=19}};gap_event(&e,NULL); }
static void pump_abort(void) { assert(abort_event.queued);abort_event.queued=false;abort_event.fn(&abort_event); }
static void pump_repair(void) { assert(repair_event.queued);repair_event.queued=false;repair_event.fn(&repair_event); }
#ifdef CODEX_TEST_REAL_ATT
#include "att_security.h"
#endif
int main(void) {
    fail_queue=true;assert(codex_ble_start()==ESP_ERR_NO_MEM && !queue);fail_queue=false;++cases;
    fail_nimble=ESP_FAIL;assert(codex_ble_start()==ESP_FAIL && !queue);fail_nimble=0;++cases;
    fail_gatts=5;assert(codex_ble_start()==ESP_FAIL && !queue);fail_gatts=0;++cases;
    fail_task=1;task_calls=0;assert(codex_ble_start()==ESP_ERR_NO_MEM && !initialized);fail_task=0;++cases;
    assert(codex_ble_start()==ESP_OK && initialized);assert(codex_ble_start()==ESP_ERR_INVALID_STATE);
    assert(ble_hs_cfg.sm_bonding && !ble_hs_cfg.sm_mitm && ble_hs_cfg.sm_sc && !ble_hs_cfg.sm_sc_only);
    assert(ble_hs_cfg.sm_sec_lvl==2 && ble_hs_cfg.sm_io_cap==BLE_SM_IO_CAP_NO_IO);++cases;
    fail_adv=1;sync_host();assert(published.connection==CODEX_ERROR && adv_retry.delay==1000);
    fail_adv=0;adv_retry.fn(NULL);assert(advertising && published.connection==CODEX_ADVERTISING);++cases;
    const struct ble_gatt_chr_def *chars=services[1].characteristics;
    assert(services[1].uuid->value==0x1812 && battery_services[0].uuid->value==0x180F && services[1].includes);
    assert(chars[4].flags & BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC);
    assert(chars[5].flags & BLE_GATT_CHR_F_WRITE_ENC);assert(chars[4].descriptors[0].uuid->value==0x2908);++cases;
    connect_peer();assert(published.connection==CODEX_CONNECTED && !link.host_active);
#ifdef CODEX_TEST_REAL_ATT
    security_regression();
#endif
    // No subscription is still a hard stop; do not accumulate future commands.
    link.subscribed=false;
    codex_action_t a={CODEX_APPROVE,true,clock_ms};assert(!codex_ble_action(&a) && !queue->count);++cases;
    link.subscribed=true;
    // Encrypted, subscribed input must work before any host status/lighting RPC.
    a=(codex_action_t){CODEX_AG00,true,clock_ms};assert(codex_ble_action(&a));drain();
    assert(remote_pressed==CODEX_AG00 && notify_count && !link.host_active);
    a.down=false;a.time_ms=clock_ms;assert(codex_ble_action(&a));drain();
    assert(remote_pressed==CODEX_NO_KEY && published.connection==CODEX_CONNECTED);++cases;
    assert(receive_rpc("{\"method\":\"device.status\",\"id\":1}",true)==0);assert(!link.host_active);
    drain();assert(link.host_active && codex_ble_link_ready(&link) && notify_count);++cases;
    struct os_mbuf om={0};struct ble_gatt_access_ctxt c={.op=0,.om=&om};
    assert(access_attr(1,0,&c,ARG(ATTR_PNP))==0 && om.length==7 && !memcmp(om.data,codex_pnp_id,7));++cases;
    om.length=0;assert(access_attr(1,0,&c,ARG(ATTR_REF_INPUT))==0 && om.data[0]==6 && om.data[1]==1);++cases;
    om.length=0;assert(access_attr(1,0,&c,ARG(ATTR_BATTERY))==BLE_ATT_ERR_UNLIKELY);
    codex_ble_battery(43,-1);drain();om.length=0;assert(access_attr(1,0,&c,ARG(ATTR_BATTERY))==0 && om.data[0]==43);++cases;
    c.op=1;om.length=62;assert(access_attr(1,0,&c,ARG(ATTR_OUTPUT))==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN && rx_rejected);++cases;
    unsigned before=notify_count;a=(codex_action_t){CODEX_MIC,true,clock_ms};assert(codex_ble_action(&a));drain();
    assert(remote_pressed==CODEX_MIC && notify_count>before);before=notify_count;
    assert(codex_ble_action(&a));drain();assert(notify_count==before);++cases;
    a.down=false;a.time_ms=clock_ms;assert(codex_ble_action(&a));drain();assert(remote_pressed==CODEX_NO_KEY && notify_count>before);++cases;
    a=(codex_action_t){CODEX_SEND,true,clock_ms};assert(codex_ble_action(&a));clock_ms+=300;before=notify_count;drain();assert(before==notify_count);++cases;
    assert(receive_rpc("{\"method\":\"old\",\"params\":",false)==0);drain();assert(protocol.used);
    a=(codex_action_t){CODEX_APPROVE,true,clock_ms};assert(codex_ble_action(&a));unsigned gen=link.generation;
    disconnect_peer();assert(advertising && link.generation!=gen && !link.subscribed);
    connect_peer();before=notify_count;drain();assert(notify_count==before && !protocol.used && remote_pressed==CODEX_NO_KEY);++cases;
    assert(receive_rpc("{\"method\":\"device.status\",\"id\":2}",false)==0);drain();assert(codex_ble_link_ready(&link));++cases;
    struct ble_gap_event e={.type=BLE_GAP_EVENT_SUBSCRIBE,.subscribe={.conn_handle=1,.attr_handle=input_handle,.cur_notify=0}};
    gap_event(&e,NULL);assert(!codex_ble_action(&a));pump_abort();disconnect_peer();connect_peer();
    assert(codex_ble_link_ready(&link) && !link.host_active);
    assert(receive_rpc("{\"m\":\"device.status\",\"id\":3}",false)==0);drain();assert(codex_ble_link_ready(&link));++cases;
    e.type=BLE_GAP_EVENT_MTU;e.mtu.conn_handle=1;e.mtu.value=65;gap_event(&e,NULL);assert(!codex_ble_action(&a));
    e.mtu.value=66;gap_event(&e,NULL);assert(codex_ble_link_ready(&link));++cases;
    e.type=BLE_GAP_EVENT_REPEAT_PAIRING;e.repeat_pairing.conn_handle=1;assert(gap_event(&e,NULL)==BLE_GAP_REPEAT_PAIRING_RETRY && delete_count==1);++cases;
    fail_notify=true;a=(codex_action_t){CODEX_AG00,true,clock_ms};assert(codex_ble_action(&a));drain();
    assert(!link.host_active && protocol.state->stats.tx_failures && abort_event.queued);
    assert(!codex_ble_link_ready(&link) && !codex_ble_action(&a));
    before=notify_count;assert(!notify_report(NULL,notifications[0]) && notify_count==before);
    c.op=BLE_GATT_ACCESS_OP_WRITE_CHR;om.length=1;om.data[0]=1;
    assert(access_attr(1,0,&c,ARG(ATTR_CONTROL))==BLE_ATT_ERR_UNLIKELY);
    assert(link.closing && !codex_ble_link_ready(&link));++cases;
    pump_abort();assert(terminate_count);fail_notify=false;++cases;
    disconnect_peer();connect_peer();assert(receive_rpc("{\"m\":\"device.status\",\"id\":4}",false)==0);drain();
    codex_ble_abort();disconnect_peer();connect_peer();before=terminate_count;pump_abort();assert(terminate_count==before);++cases;
    assert(receive_rpc("{\"m\":\"device.status\",\"id\":5}",false)==0);drain();
    a=(codex_action_t){CODEX_AG00,true,clock_ms};for(unsigned j=0;j<32;++j) assert(codex_ble_action(&a));
    a.down=false;assert(!codex_ble_action(&a) && abort_event.queued && tx_drop);pump_abort();drain();++cases;
    disconnect_peer();connect_peer();for(unsigned j=0;j<32;++j) assert(receive_rpc("{\"m\":\"device.status\"}",false)==0);
    assert(receive_rpc("{\"m\":\"device.status\"}",false)==BLE_ATT_ERR_INSUFFICIENT_RES && rx_overflow);drain();++cases;
    e.type=BLE_GAP_EVENT_ENC_CHANGE;e.enc_change.conn_handle=1;peer.sec_state.key_size=7;gap_event(&e,NULL);
    assert(!link.encrypted && abort_event.queued);pump_abort();++cases;
    peer.sec_state.key_size=16;disconnect_peer();connect_peer();
    assert(codex_ble_repair()==ESP_OK && published.pairing_reset==CODEX_PAIR_PENDING && !codex_ble_link_ready(&link));
    before=terminate_count;pump_repair();assert(terminate_count==before+1 && !store_delete_calls);
    disconnect_peer();assert(!advertising);pump_repair();assert(advertising && store_delete_calls==5 && published.pairing_reset==CODEX_PAIR_DONE);++cases;
    connect_peer();bonded_count=1;fail_unpair=1;assert(codex_ble_repair()==ESP_OK);pump_repair();disconnect_peer();pump_repair();
    assert(published.pairing_reset==CODEX_PAIR_FAILED && !advertising && repair_cache_ready);++cases;
    fail_unpair=0;assert(codex_ble_repair()==ESP_OK);pump_repair();assert(published.pairing_reset==CODEX_PAIR_DONE && advertising);++cases;
    disconnect_peer();fail_store_count=1;assert(codex_ble_repair()==ESP_OK);pump_repair();assert(published.pairing_reset==CODEX_PAIR_FAILED && !advertising);++cases;
    fail_store_count=0;assert(codex_ble_repair()==ESP_OK);pump_repair();assert(advertising && published.pairing_reset==CODEX_PAIR_DONE);++cases;
    persistent_dirty=true;assert(codex_ble_repair()==ESP_OK);pump_repair();assert(published.pairing_reset==CODEX_PAIR_FAILED && !advertising);++cases;
    persistent_dirty=false;assert(codex_ble_repair()==ESP_OK);pump_repair();assert(published.pairing_reset==CODEX_PAIR_DONE && advertising);++cases;
    vQueueDelete(queue);queue=NULL;
    printf("Codex production BLE/GATT callbacks and worker: %u cases PASS (stub radio, not hardware pairing)\n",cases);
}

#include "codex_protocol.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static codex_protocol_t protocol;
static codex_state_t state;
static uint8_t reports[160][63];
static char output[9000];
static size_t report_count, output_used;
static bool fail_send;
static unsigned cases;
static uint64_t clock_ms;
static bool send(void *unused, const uint8_t *body)
{
    (void)unused;
    if (fail_send) return false;
    assert(report_count < 160 && body[0] == 2 && body[1] <= 61);
    memcpy(reports[report_count++],body,63);
    assert(output_used+body[1]<sizeof(output));
    memcpy(output+output_used,body+2,body[1]); output_used+=body[1]; output[output_used]=0;
    for(unsigned j=2+body[1];j<63;++j) assert(body[j]==0);
    return true;
}
static void init(void)
{
    codex_state_init(&state); codex_protocol_init(&protocol,&state,send,NULL);
    output_used=report_count=0; output[0]=0; fail_send=false; clock_ms=10;
}
static void receive_bytes(const char *msg,size_t len,bool prefix)
{
    size_t offset=0;
    while(offset<len) {
        uint8_t report[64];
        size_t n=codex_report_pack(msg+offset,len-offset,report+(prefix?1:0));
        if(prefix) report[0]=6;
        assert(codex_protocol_receive(&protocol,report,prefix?64:63,clock_ms++));
        offset+=n;
    }
}
static void receive(const char *msg,bool prefix) { receive_bytes(msg,strlen(msg),prefix); }
static cJSON *response(void)
{ cJSON *r=cJSON_Parse(output); assert(r); return r; }
static cJSON *get(cJSON *o,const char *k) { return cJSON_GetObjectItemCaseSensitive(o,k); }
#define TEST(...) do { init(); __VA_ARGS__; ++cases; } while(0)
static void framing_tests(void)
{
    TEST(uint8_t b[63]; memset(b,0xAA,sizeof(b)); assert(codex_report_pack("abc",3,b)==3);
         assert(b[0]==2 && b[1]==3 && !memcmp(b+2,"abc",3)); for(unsigned j=5;j<63;++j) assert(!b[j]));
    TEST(char text[184]; memset(text,'x',sizeof(text)); assert(codex_protocol_send(&protocol,text,183));
         assert(report_count==4 && reports[0][1]==61 && reports[3][1]==1 && reports[3][2]=='\n'));
    TEST(assert(codex_protocol_send(&protocol,"{}",2)); assert(report_count==1 && !strcmp(output,"{}\n")));
    TEST(fail_send=true; assert(!codex_protocol_send(&protocol,"{}",2)); assert(state.stats.tx_failures==1));
    TEST(receive("{\"method\":\"sys.version\",\"id\":7}\n",false);
         assert(strstr(output,CODEX_VERSION) && state.connection==CODEX_HOST_ACTIVE && !protocol.used));
    TEST(receive("{\"m\":\"sys.version\",\"id\":7}\r\n",true); assert(state.stats.messages_rx==1 && report_count));
    TEST(receive("{\"method\":\"sys.version\",\"id\":7}",false); assert(report_count && !protocol.used));
    TEST(receive("{\"m\":\"sys.version\",\"id\":1}{\"m\":\"device.status\",\"id\":2}\r\n",true);
         assert(state.stats.messages_rx==2 && strstr(output,"\"id\":1") && strstr(output,"\"id\":2")));
    TEST(uint8_t b[64]={0}; codex_report_pack("{}",2,b); b[0]=3;
         assert(!codex_protocol_receive(&protocol,b,63,10)); b[0]=2; b[1]=62;
         assert(!codex_protocol_receive(&protocol,b,63,11)); assert(!codex_protocol_receive(&protocol,b,62,12));
         assert(!codex_protocol_receive(&protocol,b,64,13)); assert(!codex_protocol_receive(&protocol,NULL,63,14));
         assert(state.stats.rx_errors==5));
    TEST(char msg[3600]; strcpy(msg,"{\"method\":\"host.focused_app\",\"params\":{\"text\":\"");
         size_t n=strlen(msg); memset(msg+n,'a',3400); strcpy(msg+n+3400,"\"},\"id\":9}\n");
         receive(msg,true); assert(state.stats.messages_rx==1 && strstr(output,"\"ok\":true")));
    TEST(char msg[4600]; strcpy(msg,"{\"method\":\"host.focused_app\",\"params\":\"");
         size_t n=strlen(msg); memset(msg+n,'x',4300); strcpy(msg+n+4300,"\"}\n");
         receive(msg,false); assert(!state.stats.messages_rx && state.stats.rx_dropped);
         receive("{\"m\":\"sys.version\",\"id\":10}",false); assert(state.stats.messages_rx==1));
    TEST(receive("{\"method\":\"old\",\"params\":{",false);
         receive("{\"method\":\"sys.version\",\"id\":3}",false);
         assert(state.stats.messages_rx==1 && state.stats.rx_dropped==1));
    TEST(receive("{\"m\":\"sys.version\",",false); codex_protocol_tick(&protocol,clock_ms+2500);
         assert(protocol.used==0 && state.stats.rx_timeouts==1);
         receive("{\"m\":\"sys.version\",\"id\":5}",false); assert(state.stats.messages_rx==1));
    TEST(receive("{\"m\":\"sys.version\",",false); codex_protocol_reset(&protocol);
         assert(!protocol.used && !protocol.depth && !protocol.string);
         receive("{\"m\":\"device.status\",\"id\":9}",false); assert(state.stats.messages_rx==1));
    TEST(receive("{\"method\":\"broken\",]\n{\"m\":\"sys.version\",\"id\":1}\n",false);
         assert(state.stats.rx_errors && state.stats.messages_rx==1));
    TEST(const char msg[]="{\"method\":\"host.focused_app\",\"params\":\"\xC0\xAF\",\"id\":1}";
         receive(msg,false); assert(state.stats.rx_errors && !state.stats.messages_rx));
    TEST(receive("{\"m\":\"host.focused_app\",\"p\":\"hello \xF0\x9F\x8E\xA4\",\"id\":1}",false);
         assert(state.stats.messages_rx==1));
    TEST(char msg[512]; strcpy(msg,"{\"m\":\"host.focused_app\",\"p\":");
         size_t n=strlen(msg); for(unsigned j=0;j<20;++j) msg[n++]='[';
         msg[n++]='0'; for(unsigned j=0;j<20;++j) msg[n++]=']'; msg[n++]='}'; msg[n++]='\n'; msg[n]=0;
         receive(msg,false); assert(!state.stats.messages_rx && state.stats.rx_dropped));
    TEST(char msg[1200]; strcpy(msg,"{\"m\":\"host.focused_app\",\"p\":[");
         for(unsigned j=0;j<280;++j) strcat(msg,j?",0":"0"); strcat(msg,"]}\n");
         receive(msg,false); assert(!state.stats.messages_rx && state.stats.rx_dropped));
}
static void rpc_tests(void)
{
    TEST(state.battery=47; state.charging=1;
         receive("{\"jsonrpc\":\"2.0\",\"method\":\"device.status\",\"id\":\"request-42\"}",true);
         cJSON *r=response(),*v=get(r,"result");
         assert(!strcmp(get(r,"id")->valuestring,"request-42") && !strcmp(get(r,"jsonrpc")->valuestring,"2.0"));
         assert(get(v,"profile_index")->valueint==0 && get(v,"layer_index")->valueint==1);
         assert(get(v,"battery")->valueint==47 && cJSON_IsTrue(get(v,"is_charging")));
         assert(cJSON_IsString(get(v,"version"))); cJSON_Delete(r));
    TEST(receive("{\"method\":\"device.status\",\"id\":null}",false);
         cJSON *r=response(),*v=get(r,"result"); assert(cJSON_IsNull(get(r,"id")));
         assert(cJSON_IsNull(get(v,"battery")) && cJSON_IsNull(get(v,"is_charging")));
         assert(cJSON_IsFalse(get(v,"battery_available")) && cJSON_IsFalse(get(v,"charging_available"))); cJSON_Delete(r));
    TEST(receive("{\"m\":\"unknown.method\",\"id\":123}",false);
         cJSON *r=response(); assert(get(r,"id")->valueint==123 && get(get(r,"error"),"code")->valueint==-32601); cJSON_Delete(r));
    TEST(receive("{\"method\":\"unknown.method\"}",false); assert(!report_count));
    TEST(receive("{\"method\":\"sys.version\",\"id\":true}",false); assert(!report_count && state.connection==CODEX_DISCONNECTED));
    TEST(receive("{\"m\":\"host.focused_app\",\"p\":{\"private\":\"not retained\"},\"id\":1}",false);
         assert(!strstr(output,"private") && !strcmp(state.last_method,"host.focused_app")));
    TEST(receive("{\"m\":\"lights.preview\",\"id\":1}",false); assert(strstr(output,"\"ok\":true")));
    TEST(receive("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":4,\"c\":3166206,\"b\":0.7,\"e\":4,\"s\":0.4,\"sk\":0,\"sa\":0}],\"id\":2}",true);
         assert(state.agents[4].known && state.agents[4].color==3166206 && state.agents[4].effect==CODEX_LIGHT_BREATH);
         for(unsigned j=0;j<6;++j) if(j!=4) assert(!state.agents[j].known);
         assert(strstr(output,"\"ok\":true")));
    TEST(receive("{\"m\":\"v.oai.thstatus\",\"p\":[{\"id\":1,\"c\":16711680,\"e\":\"breath\"}]}",false);
         assert(state.agents[1].known && state.agents[1].effect==CODEX_LIGHT_BREATH);
         receive("{\"m\":\"v.oai.thstatus\",\"p\":[{\"id\":1,\"b\":0.2}]}",false);
         assert(state.agents[1].color==16711680 && state.agents[1].brightness>.19f && state.agents[1].effect==CODEX_LIGHT_BREATH));
    TEST(receive("{\"m\":\"v.oai.thstatus\",\"p\":[{\"id\":0,\"c\":1,\"e\":\"off\"},{\"id\":3,\"c\":2,\"e\":88}]}",false);
         assert(state.agents[0].effect==CODEX_LIGHT_OFF && state.agents[3].effect==CODEX_LIGHT_SOLID));
    const char *bad[]={"{\"id\":6,\"c\":1}","{\"id\":-1,\"c\":1}","{\"id\":0.5,\"c\":1}","{\"c\":1}",
        "{\"id\":0,\"c\":16777216}","{\"id\":0,\"c\":-1}","{\"id\":0,\"b\":1e999}",
        "{\"id\":0,\"b\":-1}","{\"id\":0,\"s\":2}","{\"id\":0,\"e\":null}"};
    for(unsigned j=0;j<sizeof(bad)/sizeof(bad[0]);++j) {
        TEST(char msg[240]; snprintf(msg,sizeof(msg),"{\"m\":\"v.oai.thstatus\",\"p\":[%s],\"id\":1}",bad[j]);
             receive(msg,false); assert(state.stats.rx_errors && !state.agents[0].known && strstr(output,"-32602")));
    }
    TEST(receive("{\"m\":\"v.oai.thstatus\",\"p\":[{\"id\":0}],\"id\":1}",false);
         assert(!state.agents[0].known && strstr(output,"true")));
    TEST(receive("{\"m\":\"v.oai.rgbcfg\",\"p\":{\"ambient\":{\"c\":3050327,\"b\":1,\"e\":2},\"keys\":{\"c\":255,\"b\":0.3,\"e\":1}},\"id\":3}",true);
         assert(state.ambient.known && state.ambient.color==3050327 && state.keys.color==255 && !strstr(output,"error")));
    TEST(receive("{\"m\":\"v.oai.thstatus\",\"p\":{},\"id\":1}",false); assert(strstr(output,"-32602")));
    TEST(assert(codex_light_color(&state.agents[0],0)==0x626975);
         state.agents[0]=(codex_light_t){.known=true,.color=0xFFFFFF,.brightness=1,.effect=CODEX_LIGHT_BREATH,.speed=.4f};
         assert(codex_light_color(&state.agents[0],0)!=codex_light_color(&state.agents[0],1000)));
    TEST(assert(codex_report_map[0]==6 && codex_report_map[1]==0 && codex_report_map[2]==0xFF);
         assert(codex_report_map[8]==6 && codex_pnp_id[1]==0x3A && codex_pnp_id[2]==0x30);
         assert(codex_pnp_id[3]==0x60 && codex_pnp_id[4]==0x83));
}
static void fuzz(void)
{
    init(); uint32_t seed=7;
    for(unsigned j=0;j<20000;++j) {
        uint8_t r[64];
        for(unsigned i=0;i<64;++i) { seed=seed*1664525u+1013904223u; r[i]=(uint8_t)(seed>>24); }
        if(j%3==0) { r[0]=2; r[1]=(uint8_t)(j%62); }
        codex_protocol_receive(&protocol,r,j%2?63:64,j+10);
        assert(protocol.used<=4096 && protocol.depth<=16);
    }
    codex_protocol_reset(&protocol);
    receive("{\"m\":\"device.status\",\"id\":1}",false); assert(strstr(output,"profile_index"));
    ++cases;
}
int main(void)
{
    framing_tests(); rpc_tests(); fuzz();
    printf("Codex report/RPC/state: %u cases and 20000 malformed reports PASS\n",cases);
}

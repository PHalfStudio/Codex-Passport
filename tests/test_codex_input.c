#include "codex_input.h"
#include "codex_ble_link.h"
#include "bsp_button_ladder.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static codex_action_t actions[128];
static unsigned count, cases;
static bool fail_emit;
static codex_input_t input;
static bool emit(void *u, const codex_action_t *a)
{
    (void)u;
    if (fail_emit) return false;
    assert(count < 128); actions[count++] = *a; return true;
}
static void init(void)
{
    count = 0; fail_emit = false;
    codex_input_init(&input, emit, NULL);
    codex_input_link(&input, true, 1, 0);
}
static void event(codex_button_t b, codex_input_event_t e, uint64_t ms)
{ codex_input_event(&input, b, e, ms); }
static void click(codex_button_t b, uint64_t ms)
{ event(b, CODEX_PRESS, ms); event(b, CODEX_RELEASE, ms + 50); }
static void focus(codex_page_t page, unsigned index)
{ input.page = page; input.focus[page] = index; }
#define TEST(...) do { init(); __VA_ARGS__; ++cases; } while (0)
static void nav_tests(void)
{
    TEST(event(CODEX_UP,CODEX_PRESS,10); assert(input.focus[0]==0);
         event(CODEX_UP,CODEX_RELEASE,60); assert(input.focus[0]==5 && !count));
    TEST(click(CODEX_DOWN,10); assert(input.focus[0]==1 && !count));
    TEST(event(CODEX_DOWN,CODEX_PRESS,10); codex_input_tick(&input,509); assert(input.page==0);
         codex_input_tick(&input,510); assert(input.page==1 && input.phase==CODEX_NAV_LONG_HANDLED);
         event(CODEX_DOWN,CODEX_LONG,520); codex_input_tick(&input,2000);
         event(CODEX_DOWN,CODEX_RELEASE,2200); assert(input.page==1 && input.focus[1]==0));
    TEST(event(CODEX_UP,CODEX_PRESS,10); event(CODEX_UP,CODEX_LONG,510);
         event(CODEX_UP,CODEX_RELEASE,800); assert(input.page==2 && input.focus[2]==0));
    TEST(focus(CODEX_NAV_PAGE,0); event(CODEX_DOWN,CODEX_PRESS,10);
         event(CODEX_DOWN,CODEX_RELEASE,510); assert(input.page==0));
    TEST(event(CODEX_UP,CODEX_PRESS,10); event(CODEX_UP,CODEX_RELEASE,509);
         assert(input.page==0 && input.focus[0]==5));
    TEST(focus(CODEX_NAV_PAGE,7); click(CODEX_DOWN,10); assert(input.focus[2]==0);
         click(CODEX_UP,100); assert(input.focus[2]==7));
    TEST(click(CODEX_DOWN,10); click(CODEX_DOWN,100);
         event(CODEX_DOWN,CODEX_PRESS,200); event(CODEX_DOWN,CODEX_RELEASE,800);
         click(CODEX_DOWN,900); event(CODEX_UP,CODEX_PRESS,1100);
         event(CODEX_UP,CODEX_RELEASE,1700); assert(input.page==0 && input.focus[0]==2 && input.focus[1]==1));
    TEST(click(CODEX_DOWN,10); event(CODEX_DOWN,CODEX_CLICK,200); event(CODEX_DOWN,CODEX_DOUBLE,210);
         event(CODEX_DOWN,CODEX_RELEASE,220); assert(input.focus[0]==1));
    TEST(event(CODEX_DOWN,CODEX_PRESS,10); event(CODEX_DOWN,CODEX_PRESS,11);
         event(CODEX_DOWN,CODEX_RELEASE,100); event(CODEX_DOWN,CODEX_RELEASE,101); assert(input.focus[0]==1));
}
static void key_tests(void)
{
    for (unsigned j=0;j<6;++j) {
        TEST(focus(CODEX_AGENTS_PAGE,j); click(CODEX_OK,10);
             assert(count==2 && actions[0].id==(int)j && actions[0].down && !actions[1].down);
             char json[144], key[8]; snprintf(key,sizeof(key),"AG0%u",j);
             assert(codex_action_json(&actions[0],false,json,sizeof(json)) && strstr(json,key));
             assert(codex_key_by_id(actions[0].id)->agent==(int)j));
    }
    TEST(click(CODEX_OK,10); click(CODEX_OK,170); assert(count==4);
         for(unsigned j=0;j<4;++j) assert(actions[j].id==CODEX_AG00 && actions[j].down==(j%2==0));
         assert(actions[2].time_ms-actions[0].time_ms<350));
    const char *wire[]={"ACT06","ACT07","ACT08","ACT09","ACT10","ACT12"};
    for(unsigned j=0;j<6;++j) {
        TEST(input.protect_dangerous=false; focus(CODEX_COMMANDS_PAGE,j); click(CODEX_OK,10);
             char json[144]; assert(count==2 && codex_action_json(&actions[0],false,json,sizeof(json)));
             assert(strstr(json,wire[j]) && !strstr(json,"ACT11")));
    }
    TEST(event(CODEX_OK,CODEX_RELEASE,10); assert(!count);
         event(CODEX_OK,CODEX_PRESS,20); event(CODEX_OK,CODEX_PRESS,21);
         event(CODEX_OK,CODEX_CLICK,30); event(CODEX_OK,CODEX_DOUBLE,31);
         event(CODEX_OK,CODEX_RELEASE,80); assert(count==2));
    TEST(event(CODEX_OK,CODEX_PRESS,10); input.focus[0]=5;
         event(CODEX_OK,CODEX_RELEASE,100); assert(count==2 && actions[1].id==CODEX_AG00));
    for(unsigned j=1;j<=2;++j) {
        TEST(focus(CODEX_COMMANDS_PAGE,j); event(CODEX_OK,CODEX_PRESS,10);
             assert(!count && input.phase==CODEX_CONFIRM_PENDING);
             event(CODEX_OK,CODEX_RELEASE,70); assert(!count);
             event(CODEX_OK,CODEX_DOUBLE,90); assert(!count);
             event(CODEX_OK,CODEX_PRESS,200); assert(count==1 && actions[0].down);
             event(CODEX_OK,CODEX_RELEASE,250); assert(count==2 && !actions[1].down));
    }
    TEST(focus(CODEX_COMMANDS_PAGE,1); click(CODEX_OK,10); codex_input_tick(&input,3010);
         click(CODEX_OK,3020); assert(!count && input.confirm_id==CODEX_APPROVE));
    TEST(focus(CODEX_COMMANDS_PAGE,1); click(CODEX_OK,10); event(CODEX_UP,CODEX_PRESS,100);
         assert(input.confirm_id==CODEX_NO_KEY); event(CODEX_UP,CODEX_RELEASE,150);
         assert(input.focus[1]==0 && !count));
    TEST(focus(CODEX_COMMANDS_PAGE,2); click(CODEX_OK,10); event(CODEX_DOWN,CODEX_PRESS,100);
         event(CODEX_DOWN,CODEX_RELEASE,600); assert(input.page==2 && input.confirm_id==CODEX_NO_KEY && !count));
    TEST(focus(CODEX_COMMANDS_PAGE,1); event(CODEX_OK,CODEX_PRESS,10);
         codex_input_tick(&input,2000); event(CODEX_OK,CODEX_LONG,2100);
         event(CODEX_OK,CODEX_RELEASE,2500); assert(!count));
    TEST(input.protect_fork_send=true; focus(CODEX_COMMANDS_PAGE,3); click(CODEX_OK,10);
         assert(!count); click(CODEX_OK,200); assert(count==2));
    TEST(focus(CODEX_COMMANDS_PAGE,5); event(CODEX_OK,CODEX_PRESS,10);
         assert(count==1 && actions[0].id==CODEX_SEND));
}
static void held_tests(void)
{
    TEST(focus(CODEX_COMMANDS_PAGE,4); event(CODEX_OK,CODEX_PRESS,10);
         for(unsigned ms=20;ms<2000;ms+=10) codex_input_tick(&input,ms);
         event(CODEX_OK,CODEX_LONG,510); assert(count==1 && input.active_pressed_id==CODEX_MIC);
         event(CODEX_OK,CODEX_RELEASE,2010); assert(count==2 && actions[1].time_ms-actions[0].time_ms==2000));
    TEST(focus(CODEX_COMMANDS_PAGE,4); click(CODEX_OK,10); click(CODEX_OK,170);
         event(CODEX_OK,CODEX_DOUBLE,230); assert(count==4 && actions[2].time_ms-actions[0].time_ms==160));
    for(unsigned j=4;j<=5;++j) {
        TEST(focus(CODEX_NAV_PAGE,j); event(CODEX_OK,CODEX_PRESS,10);
             event(CODEX_OK,CODEX_LONG,510); codex_input_tick(&input,2010);
             event(CODEX_OK,CODEX_RELEASE,2020); assert(count==1);
             char json[144]; assert(codex_action_json(&actions[0],false,json,sizeof(json)));
             assert(strstr(json,"\"act\":2") && strstr(json,j==4?"ENC_CW":"ENC_CC")));
    }
    TEST(focus(CODEX_NAV_PAGE,6); event(CODEX_OK,CODEX_PRESS,10); codex_input_tick(&input,1510);
         assert(input.page==2 && input.active_pressed_id==CODEX_DIAL_CLICK && count==1);
         event(CODEX_OK,CODEX_RELEASE,2010); assert(count==2);
         char json[144]; assert(codex_action_json(&actions[1],false,json,sizeof(json)) && strstr(json,"ENC_CLK"));
         assert(codex_action_json(&actions[1],true,json,sizeof(json)) && strstr(json,"\"ENC\"")));
    const char *angles[]={"0.75","0.00","0.25","0.50"};
    for(unsigned j=0;j<4;++j) {
        TEST(focus(CODEX_NAV_PAGE,j); event(CODEX_OK,CODEX_PRESS,10); codex_input_tick(&input,2100);
             assert(count==1); event(CODEX_OK,CODEX_RELEASE,2200); click(CODEX_OK,2300); assert(count==4);
             char json[144]; assert(codex_action_json(&actions[0],false,json,sizeof(json)) && strstr(json,angles[j]) && strstr(json,"\"d\":1"));
             assert(codex_action_json(&actions[1],false,json,sizeof(json)) && strstr(json,"\"a\":0,\"d\":0")));
    }
}
static void safety_tests(void)
{
    TEST(codex_input_link(&input,false,2,10); focus(CODEX_COMMANDS_PAGE,1); click(CODEX_OK,20);
         assert(!count && input.confirm_id==CODEX_NO_KEY));
    TEST(event(CODEX_OK,CODEX_PRESS,10); codex_input_link(&input,false,2,20);
         codex_input_link(&input,true,3,30); event(CODEX_OK,CODEX_RELEASE,40);
         assert(count==1 && input.active_pressed_id==CODEX_NO_KEY); click(CODEX_OK,100); assert(count==3));
    TEST(codex_input_link(&input,false,2,10); event(CODEX_OK,CODEX_PRESS,20);
         codex_input_link(&input,true,3,30); event(CODEX_OK,CODEX_PRESS,40); assert(!count);
         event(CODEX_OK,CODEX_RELEASE,50); click(CODEX_OK,100); assert(count==2));
    TEST(focus(CODEX_COMMANDS_PAGE,1); click(CODEX_OK,10); codex_input_link(&input,false,2,100);
         assert(input.confirm_id==CODEX_NO_KEY && !count));
    TEST(focus(CODEX_COMMANDS_PAGE,4); event(CODEX_OK,CODEX_PRESS,10);
         event(CODEX_UP,CODEX_PRESS,100); assert(count==2 && !actions[1].down);
         event(CODEX_UP,CODEX_RELEASE,150); event(CODEX_OK,CODEX_RELEASE,160);
         assert(input.page==1 && input.focus[1]==4); click(CODEX_OK,200); assert(count==4));
    TEST(event(CODEX_UP,CODEX_PRESS,10); event(CODEX_OK,CODEX_PRESS,30);
         event(CODEX_OK,CODEX_RELEASE,70); event(CODEX_UP,CODEX_RELEASE,100); assert(!count && input.focus[0]==0));
    TEST(event(CODEX_BUTTONS,CODEX_INVALID,10); event(CODEX_OK,CODEX_PRESS,20);
         event(CODEX_OK,CODEX_RELEASE,50); assert(!count));
    TEST(fail_emit=true; click(CODEX_OK,10); assert(!count && input.active_pressed_id==CODEX_NO_KEY));
    TEST(event(CODEX_OK,CODEX_PRESS,10); fail_emit=true; event(CODEX_OK,CODEX_RELEASE,100);
         assert(count==1 && input.transport_fault && input.active_pressed_id==CODEX_NO_KEY));
    TEST(codex_action_t a={CODEX_NO_KEY,true,0}; char b[8]; assert(!codex_action_json(&a,false,b,sizeof(b)));
         a.id=CODEX_AG00; assert(!codex_action_json(&a,false,b,sizeof(b))));
    codex_ble_link_t link={0};
    codex_ble_link_reset(&link,UINT16_MAX); assert(!codex_ble_link_ready(&link));
    codex_ble_link_reset(&link,7); link.encrypted=true; link.subscribed=true;
    assert(!codex_ble_link_ready(&link)); link.mtu=66; assert(codex_ble_link_ready(&link));
    assert(!link.host_active); // Input-only use; no false "Host Active" claim.
    link.suspended=true; assert(!codex_ble_link_ready(&link)); link.suspended=false;
    link.subscribed=false; assert(!codex_ble_link_ready(&link)); link.subscribed=true;
    link.encrypted=false; assert(!codex_ble_link_ready(&link)); link.encrypted=true;
    ++cases;
    unsigned gen=link.generation; assert(codex_ble_link_matches(&link,gen));
    codex_ble_link_reset(&link,UINT16_MAX); assert(!codex_ble_link_matches(&link,gen));
    codex_ble_link_reset(&link,7); assert(!link.encrypted && !link.subscribed && !link.host_active);
    assert(!codex_ble_link_matches(&link,gen)); ++cases;
}
static void ladder_tests(void)
{
    bsp_button_ladder_t g;
    bsp_button_ladder_init(&g);
    assert(bsp_button_ladder_sample(&g,2,0)==-1); // Boot with OK held is not a press.
    assert(bsp_button_ladder_sample(&g,2,50)==-1);
    assert(bsp_button_ladder_sample(&g,-1,60)==-1);
    assert(bsp_button_ladder_sample(&g,-1,80)==-1);
    assert(bsp_button_ladder_sample(&g,2,90)==-1);
    assert(bsp_button_ladder_sample(&g,2,109)==-1);
    assert(bsp_button_ladder_sample(&g,2,110)==2);
    assert(bsp_button_ladder_sample(&g,0,120)==-1); // Chord/identity change releases, never transfers.
    assert(bsp_button_ladder_sample(&g,0,500)==-1);
    assert(bsp_button_ladder_sample(&g,1,600)==-1);
    assert(bsp_button_ladder_sample(&g,-2,610)==-1); // Failed ADC cannot create UP.
    assert(bsp_button_ladder_sample(&g,0,700)==-1);
    assert(bsp_button_ladder_sample(&g,0,730)==-1);
    bsp_button_ladder_sample(&g,-1,740); bsp_button_ladder_sample(&g,-1,770);
    bsp_button_ladder_sample(&g,1,800); assert(bsp_button_ladder_sample(&g,1,830)==1);
    ++cases;
}
int main(void)
{
    nav_tests(); key_tests(); held_tests(); safety_tests(); ladder_tests();
    printf("Codex input/mapping/link/ADC: %u cases PASS\n",cases);
}

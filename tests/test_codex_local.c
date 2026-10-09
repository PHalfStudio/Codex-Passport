#include "codex_local.h"
#include <assert.h>
#include <stdio.h>

static codex_local_t local;
static codex_input_t input;
static unsigned remote_count,cases;
static bool emit(void *u,const codex_action_t *a) { (void)u;(void)a;++remote_count;return true; }
static void init(void) {
    codex_local_init(&local); codex_input_init(&input,emit,NULL);
    codex_input_link(&input,true,1,0);input.page=CODEX_NAV_PAGE;input.focus[2]=7;remote_count=0;
}
static void ev(codex_button_t b,codex_input_event_t e,uint64_t now) { codex_local_dispatch(&local,&input,b,e,now); }
static void click(codex_button_t b,uint64_t now) { ev(b,CODEX_PRESS,now);ev(b,CODEX_RELEASE,now+50); }
#define TEST(...) do {init();__VA_ARGS__;assert(!remote_count);++cases;} while(0)
int main(void) {
    TEST(ev(CODEX_OK,CODEX_PRESS,10);assert(!local.open && input.active_pressed_id==CODEX_NO_KEY);
         ev(CODEX_OK,CODEX_RELEASE,60);assert(local.open && local.owner==-1));
    TEST(codex_input_link(&input,false,2,0);click(CODEX_OK,10);assert(local.open));
    TEST(click(CODEX_OK,10);click(CODEX_UP,100);assert(local.focus==CODEX_LOCAL_BACK);
         click(CODEX_DOWN,200);assert(local.focus==CODEX_LOCAL_LOCK));
    TEST(click(CODEX_OK,10);click(CODEX_DOWN,100);click(CODEX_OK,200);assert(local.mode==CODEX_LOCAL_BRIGHTNESS);
         click(CODEX_UP,300);assert(local.brightness==70 && (codex_local_requests(&local)&CODEX_LOCAL_DIM));
         click(CODEX_DOWN,400);assert(local.brightness==65);
         click(CODEX_OK,500);assert(local.mode==CODEX_LOCAL_MENU));
    TEST(codex_local_open(&local);local.mode=CODEX_LOCAL_BRIGHTNESS;
         for(unsigned j=0;j<30;++j) click(CODEX_UP,10+j*100);assert(local.brightness==100);
         for(unsigned j=0;j<30;++j) click(CODEX_DOWN,4000+j*100);assert(local.brightness==10));
    TEST(codex_local_open(&local);local.focus=CODEX_LOCAL_PAIR;click(CODEX_OK,10);
         assert(local.confirm && !codex_local_requests(&local));click(CODEX_OK,200);
         assert(!local.confirm && codex_local_requests(&local)==CODEX_LOCAL_REPAIR));
    TEST(codex_local_open(&local);local.focus=CODEX_LOCAL_PAIR;click(CODEX_OK,10);codex_local_tick(&local,3010);
         click(CODEX_OK,3100);assert(local.confirm && !codex_local_requests(&local)));
    TEST(codex_local_open(&local);local.focus=CODEX_LOCAL_PAIR;click(CODEX_OK,10);
         click(CODEX_UP,100);assert(!local.confirm && local.focus==CODEX_LOCAL_BRIGHT));
    TEST(codex_local_open(&local);local.focus=CODEX_LOCAL_DEBUG;click(CODEX_OK,10);
         assert(local.mode==CODEX_LOCAL_DIAGNOSTICS);click(CODEX_OK,200);assert(local.mode==CODEX_LOCAL_MENU));
    TEST(click(CODEX_OK,10);click(CODEX_OK,100);assert(local.locked && !local.open && local.wake_armed);
         assert(codex_local_requests(&local)==CODEX_LOCAL_OFF);
         ev(CODEX_OK,CODEX_PRESS,200);assert(!local.locked && local.wake_consuming);
         assert(codex_local_requests(&local)==CODEX_LOCAL_ON);
         ev(CODEX_OK,CODEX_LONG,700);assert(!local.open);
         ev(CODEX_OK,CODEX_RELEASE,800);assert(!local.wake_consuming && local.owner==-1 && !local.open));
    TEST(click(CODEX_OK,10);ev(CODEX_OK,CODEX_PRESS,100);
         assert(local.locked && !local.wake_armed);ev(CODEX_OK,CODEX_PRESS,110);assert(local.locked);
         ev(CODEX_OK,CODEX_RELEASE,160);ev(CODEX_DOWN,CODEX_PRESS,200);ev(CODEX_DOWN,CODEX_RELEASE,800);
         assert(!local.locked && input.page==CODEX_NAV_PAGE && input.focus[2]==7));
    TEST(click(CODEX_OK,10);local.focus=CODEX_LOCAL_BACK;click(CODEX_OK,100);
         assert(!local.open && local.owner==-1 && !local.wake_consuming);
         click(CODEX_OK,200);assert(local.open));
    TEST(click(CODEX_OK,10);ev(CODEX_OK,CODEX_CLICK,100);ev(CODEX_OK,CODEX_DOUBLE,120);
         assert(local.open && !local.locked && !codex_local_requests(&local)));
    TEST(click(CODEX_OK,10);ev(CODEX_UP,CODEX_PRESS,100);ev(CODEX_OK,CODEX_PRESS,120);
         ev(CODEX_OK,CODEX_RELEASE,160);ev(CODEX_UP,CODEX_RELEASE,200);
         assert(local.focus==CODEX_LOCAL_LOCK && !local.locked));
    printf("Codex local sidebar/lock/wake/brightness/confirmation: %u cases PASS\n",cases);
}

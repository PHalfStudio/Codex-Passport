// Render the production UI with the actual pinned LVGL, not widget mocks.
#include "../main/codex_ui.c"
#include "bsp_display_rounding.h"
#include <assert.h>
#include <stdlib.h>

static uint16_t framebuffer[240*320], buffer[240*40];
static unsigned flushes;
static codex_input_t input;
static codex_state_t state;
static codex_local_t local;
static bool emit(void *u,const codex_action_t *a) { (void)u; (void)a; return true; }
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data)
{
    ++flushes;
    lv_draw_buf_t *draw = lv_display_get_buf_active(display);
    for(int y=area->y1;y<=area->y2;++y) {
        int x1,x2; bool valid=bsp_display_rounded_row_span(y,240,320,30,&x1,&x2);
        const uint16_t *row=(const uint16_t *)(data+(y-area->y1)*draw->header.stride);
        for(int x=area->x1;x<=area->x2;++x) {
            assert(x>=0 && x<240 && y>=0 && y<320);
            framebuffer[y*240+x]=valid && x>=x1 && x<=x2 ? row[x-area->x1] : 0;
        }
    }
    lv_display_flush_ready(display);
}
static void advance(unsigned ms)
{ for(unsigned j=0;j<ms;j+=20) { lv_tick_inc(20); lv_timer_handler(); } }
static void check_layout(void)
{
    lv_obj_update_layout(screen);
    for(unsigned j=0;j<codex_page_count(input.page);++j) {
        int x=lv_obj_get_x(cards[j]),y=lv_obj_get_y(cards[j]);
        assert(x>=0 && y>=80 && x+lv_obj_get_width(cards[j])<=240 && y+lv_obj_get_height(cards[j])<=245);
        assert(!lv_obj_has_flag(cards[j],LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
        lv_point_t size;
        lv_text_get_size(&size,lv_label_get_text(labels[j]),&lv_font_montserrat_14,0,0,1000,LV_TEXT_FLAG_NONE);
        assert(size.x<=lv_obj_get_width(labels[j]));
    }
    assert(lv_obj_get_y(debug_label)+lv_obj_get_height(debug_label)<320);
}
static void snapshot(const char *dir,const char *name,uint64_t now)
{
    codex_ui_render(&input,&state,now); codex_ui_local(&local,&state,now); check_layout(); advance(200);
    char path[1024]; snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n240 320\n255\n");
    for(unsigned j=0;j<240*320;++j) {
        unsigned p=framebuffer[j]; unsigned char rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
        assert(fwrite(rgb,1,3,f)==3);
    }
    fclose(f);
}
int main(int argc,char **argv)
{
    assert(argc==2); setbuf(stdout,NULL); lv_init();
    lv_display_t *d=lv_display_create(240,320); assert(d);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,flush);
    codex_input_init(&input,emit,NULL); codex_state_init(&state); codex_local_init(&local);
    assert(codex_ui_create()); state.connection=CODEX_ADVERTISING;
    snapshot(argv[1],"advertising",1000);
    state.connection=CODEX_HOST_ACTIVE; state.battery=57; state.ready=true; state.mtu=247; state.subscribed=state.encrypted=true;
    codex_input_link(&input,true,1,1100);
    const uint32_t colors[]={0xFFFFFF,0x304FFE,0x00FF4C,0xFF6D00,0xFF0033,0};
    for(unsigned j=0;j<6;++j) state.agents[j]=(codex_light_t){.known=true,.color=colors[j],.brightness=1,.effect=j==5?CODEX_LIGHT_OFF:CODEX_LIGHT_SOLID};
    snapshot(argv[1],"agents",2400);
    input.page=CODEX_COMMANDS_PAGE; input.focus[1]=4;
    snapshot(argv[1],"commands",2500);
    input.focus[1]=1; codex_input_event(&input,CODEX_OK,CODEX_PRESS,2600);
    codex_input_event(&input,CODEX_OK,CODEX_RELEASE,2650);
    snapshot(argv[1],"confirmation",2700);
    assert(strstr(lv_label_get_text(focus_label),"CONFIRM Approve"));
    codex_input_event(&input,CODEX_OK,CODEX_PRESS,2800);
    snapshot(argv[1],"approve-held",2800);
    codex_input_event(&input,CODEX_OK,CODEX_RELEASE,2900);
    input.page=CODEX_NAV_PAGE; input.focus[2]=6;
    codex_input_event(&input,CODEX_OK,CODEX_PRESS,3100); codex_input_tick(&input,4100);
    snapshot(argv[1],"dial-held",4100); assert(input.active_pressed_id==CODEX_DIAL_CLICK);
    codex_input_event(&input,CODEX_OK,CODEX_RELEASE,4200);
    input.feedback=NULL;
    input.focus[2]=7;codex_local_open(&local);
    snapshot(argv[1],"sidebar",4300);
    local.focus=CODEX_LOCAL_BRIGHT;
    codex_local_event(&local,CODEX_OK,CODEX_PRESS,4400);codex_local_event(&local,CODEX_OK,CODEX_RELEASE,4450);
    snapshot(argv[1],"brightness",4500);
    codex_local_event(&local,CODEX_OK,CODEX_PRESS,4600);codex_local_event(&local,CODEX_OK,CODEX_RELEASE,4650);
    local.focus=CODEX_LOCAL_PAIR;
    codex_local_event(&local,CODEX_OK,CODEX_PRESS,4700);codex_local_event(&local,CODEX_OK,CODEX_RELEASE,4750);
    snapshot(argv[1],"repair-confirmation",4800);
    local.confirm=false;local.focus=CODEX_LOCAL_DEBUG;
    codex_local_event(&local,CODEX_OK,CODEX_PRESS,4900);codex_local_event(&local,CODEX_OK,CODEX_RELEASE,4950);
    snapshot(argv[1],"diagnostics",5000);
    local.open=false;
    // Exercise all cards and page reuse; no objects are allocated per page.
    for(unsigned j=0;j<500;++j) {
        input.page=(codex_page_t)(j%3); input.focus[input.page]=(uint8_t)((j/3)%codex_page_count(input.page));
        if(j%20==0) { codex_local_open(&local);local.mode=(codex_local_mode_t)((j/20)%3); }
        if(j%20==10) local.open=false;
        codex_ui_render(&input,&state,5000+j*50);codex_ui_local(&local,&state,5000+j*50);check_layout();advance(60);
    }
    codex_ui_render(&input,&state,30000);codex_ui_local(&local,&state,30000);advance(300);unsigned before=flushes;
    for(unsigned j=0;j<20;++j) { codex_ui_render(&input,&state,30000);codex_ui_local(&local,&state,30000);advance(60); }
    assert(flushes==before); // Dirty-state updates, not a full-frame render loop
    lv_mem_monitor_t mem; lv_mem_monitor(&mem);
    // TLSF metadata is inside LV_MEM_SIZE and differs on 32-/64-bit hosts.
    assert(LV_MEM_SIZE==32*1024 && mem.total_size<=LV_MEM_SIZE && mem.free_size>4096 && mem.max_used<mem.total_size);
    printf("Codex UI: 10 frames, 500 page/focus/sidebar changes PASS; configured_pool=%u usable=%zu free=%zu max_used=%zu\n",LV_MEM_SIZE,mem.total_size,mem.free_size,mem.max_used);
    lv_deinit();
}

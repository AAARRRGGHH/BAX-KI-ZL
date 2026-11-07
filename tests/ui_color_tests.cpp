#include "ui.hpp"
#include "file_settings.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
using namespace sm;
namespace {
uint32_t now=1;uint64_t transferred=0;std::vector<uint16_t> screen(466*466);
uint32_t ticks(){return now;}
void flush(lv_display_t* d,const lv_area_t* area,uint8_t* bytes){
 auto* p=reinterpret_cast<uint16_t*>(bytes);const int width=area->x2-area->x1+1;
 for(int y=area->y1;y<=area->y2;++y){std::copy_n(p,width,screen.begin()+y*466+area->x1);p+=width;transferred+=width;}
 lv_display_flush_ready(d);
}
struct Pointer{int x=233,y=233;bool down=false;};
void read(lv_indev_t* d,lv_indev_data_t* out){const auto& p=*static_cast<Pointer*>(lv_indev_get_user_data(d));out->point={p.x,p.y};out->state=p.down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
struct Output:MidiTransport{bool send(MidiEvent)override{return true;}};
struct Store:FileSettings{int writes=0;using FileSettings::FileSettings;bool save_all(Selection k,Selection a,ChordSelection c,Setup s)override{++writes;return FileSettings::save_all(k,a,c,s);}};
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"Color wheel FAIL %d: %s\n",__LINE__,#v);std::exit(1);}}while(0)
}
int main(){
 const std::string path="ui-color.cfg";for(const auto& suffix:{"",".motion"})std::filesystem::remove(path+suffix);
 lv_init();lv_tick_set_cb(ticks);auto* display=lv_display_create(466,466);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 std::vector<uint16_t> buffer(466*20);lv_display_set_buffers(display,buffer.data(),nullptr,uint32_t(buffer.size()*2),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
 Pointer p;auto* input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_user_data(input,&p);lv_indev_set_read_cb(input,read);
 Store storage(path);Application app(storage);Output output;MidiRouter router(output);Ui ui(app,router);ui.build();
 const auto pump=[&](int ms){for(int i=0;i<ms;i+=5){now+=5;ui.performance_tick(uint64_t(now)*1000);lv_indev_read(input);lv_timer_handler();}lv_refr_now(display);};
 const auto swipe=[&](){p={233,263,true};pump(30);for(int i=1;i<=6;++i){p.x=233-83*i/6;pump(20);}p.down=false;pump(50);};
 ui.open_setup();pump(40);swipe();CHECK(ui.setup_page()==5);swipe();CHECK(ui.setup_page()==1);
 const auto original=app.setup.accent;uint64_t pixels=0,micros=0,max_pixels=0;
 p={428,233,true};pump(40);
 for(int frame=1;frame<=36;++frame){
  const float a=frame*6.28318530718f/36;p.x=int(std::lround(233+195*std::cos(a)));p.y=int(std::lround(233+195*std::sin(a)));
  const auto before=transferred;const auto started=std::chrono::steady_clock::now();pump(40);
  micros+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
  const auto delta=transferred-before;pixels+=delta;max_pixels=std::max(max_pixels,delta);
  CHECK(app.setup.accent==original && storage.writes==0 && ui.setup_page()==1);
  const auto partial=screen;lv_obj_invalidate(lv_screen_active());lv_refr_now(display);CHECK(screen==partial);
 }
 CHECK(max_pixels<466*466/2); // Dragging must never repaint the whole display.
 const auto idle=transferred;pump(120);CHECK(transferred==idle);
 p.down=false;pump(40);
 ui.power_button(ButtonEvent::Press);ui.power_button(ButtonEvent::Release);pump(40);
 CHECK(!ui.is_setup() && app.setup.accent!=original && storage.load_setup().accent==app.setup.accent && storage.writes==1);
 std::printf("Color wheel: pixels/update=%llu max=%llu host_us/update=%llu; partial/full redraw identical; one write on confirmation\n",
  static_cast<unsigned long long>(pixels/36),static_cast<unsigned long long>(max_pixels),static_cast<unsigned long long>(micros/36));
 for(const auto& suffix:{"",".motion"})std::filesystem::remove(path+suffix);
}

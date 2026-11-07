#include "ui.hpp"
#include "file_settings.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
using namespace sm;
namespace {
uint32_t now=1;
uint32_t ticks(){return now;}
void flush(lv_display_t* d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);}
struct Pointer{int x=233,y=233;bool down=false;};
void read(lv_indev_t* d,lv_indev_data_t* data){auto& p=*static_cast<Pointer*>(lv_indev_get_user_data(d));data->point={p.x,p.y};data->state=p.down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
bool has_cancel(lv_obj_t* object){if(lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN))return false;if(lv_obj_check_type(object,&lv_label_class) && std::string(lv_label_get_text(object))=="CANCEL")return true;for(uint32_t i=0;i<lv_obj_get_child_count(object);++i)if(has_cancel(lv_obj_get_child(object,i)))return true;return false;}
struct Output:MidiTransport{bool send(MidiEvent)override{return true;}};
struct Store:FileSettings{
 bool fail=false;
 using FileSettings::FileSettings;
 bool save_all(Selection k,Selection a,ChordSelection c,Setup s)override{return !fail && FileSettings::save_all(k,a,c,s);}
};
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"PWR confirmation failed at %d: %s\n",__LINE__,#v);std::exit(1);}}while(0)
}

lv_obj_t* find_text(lv_obj_t* object,const char* text){
 if(lv_obj_check_type(object,&lv_label_class) && std::string(lv_label_get_text(object))==text)return object;
 for(uint32_t i=0;i<lv_obj_get_child_count(object);++i)if(auto* found=find_text(lv_obj_get_child(object,i),text))return found;
 return nullptr;
}
int main(){
 const std::string path="ui-motion.cfg";for(const auto& suffix:{"",".motion"})std::filesystem::remove(path+suffix);
 lv_init();lv_tick_set_cb(ticks);auto* display=lv_display_create(466,466);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 std::vector<uint16_t> buffer(466*20);lv_display_set_buffers(display,buffer.data(),nullptr,uint32_t(buffer.size()*2),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
 Pointer pointer;auto* input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_user_data(input,&pointer);lv_indev_set_read_cb(input,read);
 Store storage(path);Application app(storage);Output output;MidiRouter router(output);Ui ui(app,router);ui.build();
 const auto pump=[&](int ms){for(int i=0;i<ms;i+=5){now+=5;ui.motion_sample(0,0,1,now);ui.motion_tick(now);ui.performance_tick(uint64_t(now)*1000);lv_indev_read(input);lv_timer_handler();}};
 const auto click=[&](int x,int y){pointer={x,y,true};pump(30);pointer.down=false;pump(50);};
 const auto swipe=[&](int x,int y,int end_x,int end_y){pointer={x,y,true};pump(30);for(int i=1;i<=6;++i){pointer.x=x+(end_x-x)*i/6;pointer.y=y+(end_y-y)*i/6;pump(20);}pointer.down=false;pump(50);};
 const auto power=[&](){ui.power_button(ButtonEvent::Press);ui.power_button(ButtonEvent::Release);pump(20);};
 ui.open_setup();pump(50);CHECK(ui.setup_page()==0);swipe(233,263,150,263);CHECK(ui.setup_page()==5);
 ui.scroll(233,186,-2);ui.scroll(130,315,-74);ui.scroll(310,315,-1);pump(250);
 // A horizontal drag starting in a selector belongs to that selector.
 swipe(130,315,220,315);CHECK(ui.setup_page()==5);
 click(233,403);
 auto* calibrated=find_text(lv_screen_active(),"CALIBRATED");CHECK(calibrated);
 auto* button=lv_obj_get_parent(calibrated);auto fill=lv_obj_get_style_bg_color(button,LV_PART_MAIN);CHECK(fill.red || fill.green || fill.blue);
 pump(2800);CHECK(std::string(lv_label_get_text(calibrated))=="CALIBRATED");
 // A second calibration restarts the three-second confirmation window.
 click(233,403);pump(2800);CHECK(std::string(lv_label_get_text(calibrated))=="CALIBRATED");
 pump(250);CHECK(std::string(lv_label_get_text(calibrated))=="CALIBRATE");
 fill=lv_obj_get_style_bg_color(button,LV_PART_MAIN);CHECK(!fill.red && !fill.green && !fill.blue);
 power();CHECK(!ui.is_setup());
 CHECK(app.motion.channel==3 && app.motion.cc[0]==74 && app.motion.cc[1]==1);
 CHECK(storage.load_motion()==app.motion);ui.open_setup();CHECK(ui.setup_page()==5);
 // Wrap backwards over the first option, then forwards over the final option.
 ui.scroll(233,180,3);ui.scroll(142,300,75);ui.scroll(324,300,2);pump(250);power();
 CHECK(app.motion.channel==16 && app.motion.cc[0]==119 && app.motion.cc[1]==119);
 ui.open_setup();ui.scroll(233,180,-1);ui.scroll(142,300,-1);ui.scroll(324,300,-1);pump(250);power();
 CHECK(app.motion.channel==1 && app.motion.cc[0]==-1 && app.motion.cc[1]==-1);
 CHECK(storage.load_motion()==app.motion);ui.open_setup();
 ui.scroll(142,300,-1);pump(250);power();CHECK(app.motion.cc[0]==1 && storage.load_motion().cc[0]==1);
 ui.open_setup();
 swipe(233,263,150,263);CHECK(ui.setup_page()==1);swipe(233,263,320,263);CHECK(ui.setup_page()==5);swipe(233,263,320,263);CHECK(ui.setup_page()==0);
 ui.cancel_setup();app.motion.cc={{-1,-1}};pump(50);
 for(const auto& suffix:{"",".motion"})std::filesystem::remove(path+suffix);
 std::puts("Motion UI / gestures / PWR / persistence: PASS");
}

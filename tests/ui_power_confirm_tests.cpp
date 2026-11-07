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
int main(){
 const std::string path="ui-power-confirm.cfg";
 auto clean=[&](){for(const auto& suffix:{"",".slots",".bounce"})std::filesystem::remove(path+suffix);for(int i=1;i<=music_slot_count;++i)std::filesystem::remove(path+".slot"+std::to_string(i));};clean();
 lv_init();lv_tick_set_cb(ticks);auto* display=lv_display_create(466,466);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 std::vector<uint16_t> buffer(466*20);lv_display_set_buffers(display,buffer.data(),nullptr,uint32_t(buffer.size()*2),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
 Pointer pointer;auto* input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_user_data(input,&pointer);lv_indev_set_read_cb(input,read);
 Store storage(path);Application app(storage);Output output;MidiRouter router(output);Ui ui(app,router);ui.build();
 const auto pump=[&](int ms){for(int i=0;i<ms;i+=5){now+=5;ui.performance_tick(uint64_t(now)*1000);lv_indev_read(input);lv_timer_handler();}};
 const auto power=[&](){ui.power_button(ButtonEvent::Press);ui.power_button(ButtonEvent::Release);pump(20);};
 const auto click=[&](int x,int y){pointer={x,y,true};pump(30);pointer.down=false;pump(50);};
 const auto swipe=[&](int x,int y,int end_x,int end_y){pointer={x,y,true};pump(30);for(int i=1;i<=6;++i){pointer.x=x+(end_x-x)*i/6;pointer.y=y+(end_y-y)*i/6;pump(20);}pointer.down=false;pump(50);};
 // Chord scale/strum: same commit path as SELECT, without cycling play mode.
 CHECK(ui.is_chords());ui.open();pump(20);ui.scroll(80,233,-2);ui.scroll(359,233,-3);pump(30);power();
 CHECK(ui.is_chords() && !ui.is_selecting() && app.chords.root==2 && app.chords.strum_ms==30 && storage.load_chords()==app.chords);
 // Chord editor: commit the variant and octave offset, ending its audition.
 swipe(233,233,233,69);CHECK(ui.is_editing_chord());ui.scroll(170,233,-3);ui.scroll(349,233,-1);pump(30);power();
 CHECK(ui.is_chords() && !ui.is_editing_chord() && !ui.is_selecting());CHECK(app.chords.variants[0]==ChordVariant::Seventh && app.chords.octave_shifts[0]==1 && storage.load_chords()==app.chords);
 // Holding PWR opens setup; releasing that same hold must not confirm it.
 ui.open();pump(20);ui.power_button(ButtonEvent::Press);ui.power_button(ButtonEvent::Hold);ui.power_button(ButtonEvent::Release);CHECK(ui.is_setup() && !ui.is_selecting());power();CHECK(ui.is_chords() && !ui.is_setup());
 power();CHECK(ui.is_arpeggiator());power();CHECK(ui.is_bounce());CHECK(ui.toggle_bounce_menu());ui.scroll(140,233,-1);pump(30);power();CHECK(ui.is_bounce() && !ui.is_bounce_menu());power();CHECK(!ui.is_chords() && !ui.is_arpeggiator() && !ui.is_bounce());
 // Keyboard scale, including cancellation and a failed storage commit.
 ui.open();pump(20);ui.scroll(105,233,-1);pump(30);const auto before=app.current;const auto chosen=app.draft;CHECK(chosen!=before);
 ui.power_button(ButtonEvent::Press);ui.power_button(ButtonEvent::Cancel);ui.power_button(ButtonEvent::Release);CHECK(ui.is_selecting() && app.current==before);
 storage.fail=true;power();CHECK(ui.is_selecting() && app.current==before);storage.fail=false;power();CHECK(!ui.is_selecting() && app.current==chosen && storage.load()==chosen);
 // A touch confirmation during a held physical press cannot cause a second action.
 ui.open();pump(20);ui.power_button(ButtonEvent::Press);CHECK(ui.confirm());ui.power_button(ButtonEvent::Release);CHECK(!ui.is_chords() && !ui.is_arpeggiator() && !ui.is_bounce() && !ui.is_selecting());
 ui.power_button(ButtonEvent::Release);CHECK(!ui.is_chords());
 // Conversely, opening a menu after the press began does not auto-confirm it.
 ui.power_button(ButtonEvent::Press);ui.open();pump(20);ui.power_button(ButtonEvent::Release);CHECK(ui.is_selecting());power();CHECK(!ui.is_selecting() && !ui.is_chords());
 // SAVE/LOAD/DELETE use explicit touch buttons. PWR never acts in a dialog.
 ui.open_setup();for(int page=0;page<5;++page)swipe(233,page==1?263:page<=2?390:410,150,page==1?263:page<=2?390:410);CHECK(ui.setup_page()==4);
 MusicPreset saved;click(155,370);ui.scroll(86,233,-1);power();CHECK(ui.is_setup() && has_cancel(lv_screen_active()) && !storage.load_slot(0,saved));
 ui.power_button(ButtonEvent::Press);ui.power_button(ButtonEvent::Hold);ui.power_button(ButtonEvent::Release);CHECK(has_cancel(lv_screen_active()) && !storage.load_slot(0,saved));click(310,370);
 click(155,370);ui.scroll(86,233,-1);click(155,370);CHECK(storage.load_slot(0,saved));
 auto modified=app.chords;modified.root=(modified.root+3)%12;CHECK(app.save_chords(modified));ui.refresh();
 click(310,370);power();CHECK(ui.is_setup() && has_cancel(lv_screen_active()) && app.chords==modified);click(310,370);click(310,370);click(155,370);CHECK(app.chords==saved.chords && storage.load_bounce()==saved.kinetic);
 click(397,218);power();CHECK(ui.is_setup() && has_cancel(lv_screen_active()) && storage.load_slot(0,saved));click(310,370);click(397,218);click(155,370);CHECK(!storage.load_slot(0,saved));power();CHECK(!ui.is_setup());
 lv_display_delete(display);lv_deinit();clean();
 std::puts("PWR: scale/chord confirmation, failures, hold/cancel, stale releases, inert slot dialogs PASS");
}

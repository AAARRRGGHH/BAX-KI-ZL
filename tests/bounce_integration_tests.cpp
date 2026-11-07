#include "ui.hpp"
#include "file_settings.hpp"
#include "bounce_storage.hpp"
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace sm;
namespace {
uint32_t now=1;
uint32_t ticks(){return now;}
void flush(lv_display_t* d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);}
struct Pointer{int x=233,y=233;bool down=false;};
void read(lv_indev_t* d,lv_indev_data_t* data){auto& p=*static_cast<Pointer*>(lv_indev_get_user_data(d));data->point={p.x,p.y};data->state=p.down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
struct Output:MidiTransport{
 std::vector<MidiEvent> events;std::array<std::array<int,128>,16> held{};
 bool send(MidiEvent e)override{events.push_back(e);if(e.kind==MidiKind::NoteOn)held[e.channel-1][e.note]=1;else if(e.kind==MidiKind::NoteOff)held[e.channel-1][e.note]=0;else held[e.channel-1]={};return true;}
 int ons()const{int count=0;for(auto e:events)count+=e.kind==MidiKind::NoteOn;return count;}
 bool clean()const{for(const auto& c:held)for(int n:c)if(n)return false;return true;}
};
void check(bool ok,int line){if(!ok){std::fprintf(stderr,"Bounce integration check failed at %d\n",line);std::exit(1);}}
#define REQUIRE(...) check((__VA_ARGS__),__LINE__)
}
int main(){
 const std::string path="bounce-integration.cfg";std::filesystem::remove(path);std::filesystem::remove(path+".bounce");
 lv_init();lv_tick_set_cb(ticks);auto* display=lv_display_create(466,466);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 std::vector<uint16_t> first(466*20),second(first.size());lv_display_set_buffers(display,first.data(),second.data(),uint32_t(first.size()*2),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
 Pointer points[2];lv_indev_t* inputs[2];for(int i=0;i<2;++i){inputs[i]=lv_indev_create();lv_indev_set_type(inputs[i],LV_INDEV_TYPE_POINTER);lv_indev_set_user_data(inputs[i],&points[i]);lv_indev_set_read_cb(inputs[i],read);}
 FileSettings storage(path);Application app(storage);Output output;MidiRouter router(output);Ui ui(app,router);ui.build();
 auto pump=[&](int ms){for(int i=0;i<ms;i+=5){now+=5;ui.performance_tick(uint64_t(now)*1000);for(auto* input:inputs)lv_indev_read(input);lv_timer_handler();}};
 auto power=[&](){ui.power_button(ButtonEvent::Press);ui.power_button(ButtonEvent::Release);pump(20);};
 REQUIRE(ui.is_chords());power();REQUIRE(ui.is_arpeggiator());power();REQUIRE(ui.is_bounce());
 points[0]={233,233,true};pump(30);points[0].y=100;pump(30);points[0].y=80;pump(20);points[0].down=false;pump(750);REQUIRE(output.ons()>0);
 // Actual two LVGL inputs, not a direct call to the gesture helper.
 points[0]={180,180,true};pump(25);points[1]={290,290,true};pump(25);points[0].down=false;pump(25);points[1].down=false;pump(30);REQUIRE(ui.is_bounce_menu());
 ui.scroll(140,233,-1);pump(40);REQUIRE(app.current.root==1 && app.chords.root==1 && app.arp_scale().root==1);
 REQUIRE(storage.load_bounce().scale==app.current);power();REQUIRE(ui.is_bounce() && !ui.is_bounce_menu());
 // Channel changes and runtime articulation use the same route as other modes.
 ui.reset_midi_session();REQUIRE(output.clean());REQUIRE(router.channel(6));
 points[0]={233,233,true};pump(30);points[0].y=90;pump(25);points[0].down=false;pump(750);
 bool channel_six=false;for(const auto& e:output.events)if(e.kind==MidiKind::NoteOn && e.channel==6)channel_six=true;REQUIRE(channel_six);
 power();REQUIRE(!ui.is_chords() && !ui.is_arpeggiator() && !ui.is_bounce());REQUIRE(output.clean());
 const auto stopped=output.ons();pump(1000);REQUIRE(output.ons()==stopped);power();REQUIRE(ui.is_chords());
 // CRC/version validation and non-destructive round trip of separate settings.
 auto saved=storage.load_bounce();saved.bpm=149;saved.quantized=false;saved.low=1;saved.high=6;saved.gate=55;REQUIRE(storage.save_bounce(saved));REQUIRE(storage.load_bounce()==saved);
 auto bytes=bounce::pack(saved);bounce::Settings decoded;REQUIRE(bounce::unpack(bytes,decoded) && decoded==saved);bytes[7]^=1;REQUIRE(!bounce::unpack(bytes,decoded));
 // Existing version-1 settings retain every current field; rotation is retired.
 bytes=bounce::pack(saved);bytes[0]=1;bytes[8]=uint8_t(saved.timing);bytes[10]=15;const auto legacy_crc=bounce::checksum(bytes);for(int i=0;i<4;++i)bytes[12+i]=uint8_t(legacy_crc>>(8*i));auto legacy_expected=saved;legacy_expected.gate=50;REQUIRE(bounce::unpack(bytes,decoded) && decoded==legacy_expected);REQUIRE(bounce::pack(decoded)[0]==7 && bounce::pack(decoded)[10]==50);
 auto setup=app.setup;setup.global_scale=false;REQUIRE(app.save_setup(setup));power();power();REQUIRE(ui.is_bounce());
 REQUIRE(ui.toggle_bounce_menu());ui.scroll(140,233,-1);pump(30);REQUIRE(app.current.root==1 && storage.load_bounce().scale.root==2);power();power();REQUIRE(!ui.is_bounce());
 // The existing face-down action also resets Bounce without changing its scale.
 power();power();power();REQUIRE(ui.is_bounce());pump(20);
 const auto kept_settings=storage.load_bounce();const auto kept_scale=kept_settings.scale;
 REQUIRE(ui.can_reset_play_mode() && ui.reset_play_mode());auto reset=storage.load_bounce();REQUIRE(reset==kept_settings && output.clean());const auto reset_ons=output.ons();pump(800);REQUIRE(output.ons()==reset_ons);
 REQUIRE(ui.toggle_bounce_menu() && !ui.can_reset_play_mode());power();REQUIRE(!ui.is_bounce_menu());
 // Clock reaches Bounce through the same UI dispatcher as the arpeggiator.
 auto clock_setup=app.setup;clock_setup.receive_clock=true;REQUIRE(app.save_setup(clock_setup));ui.refresh();ui.midi_realtime(0xfa,uint64_t(now)*1000);ui.midi_realtime(0xf8,uint64_t(now)*1000);ui.midi_disconnected();REQUIRE(output.clean());
 REQUIRE(ui.reset_play_mode() && app.setup.receive_clock && storage.load_bounce().scale==kept_scale);
 REQUIRE(ui.prepare_shutdown());pump(30);REQUIRE(output.clean());
 lv_display_delete(display);lv_deinit();std::filesystem::remove(path);std::filesystem::remove(path+".bounce");
 std::puts("Bounce integrated: four modes, native two-touch menu, global/independent scale, channel, persistence, partial display, stop cleanup PASS");
}

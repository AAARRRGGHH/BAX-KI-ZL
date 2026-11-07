#include "motion_cc.hpp"
#include "file_settings.hpp"
#include "usb_midi_protocol.hpp"
#include "ble_midi_protocol.hpp"
#include "synth.hpp"
#include <cstdlib>
#include <cstdio>
#include <filesystem>
using namespace sm;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"Motion FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 CHECK(MotionCc::value(-90)==0 && MotionCc::value(-45)==0);
 CHECK(MotionCc::value(-2)==64 && MotionCc::value(0)==64 && MotionCc::value(2)==64);
 CHECK(MotionCc::value(45)==127 && MotionCc::value(90)==127);
 for(int a=-90;a<90;++a)CHECK(MotionCc::value(float(a))<=MotionCc::value(float(a+1)));
 MotionSettings cfg;CHECK(valid_motion(cfg) && cfg.cc[0]==-1 && cfg.cc[1]==-1);
 cfg.channel=16;cfg.cc={{1,119}};cfg.neutral={{.3f,.4f,std::sqrt(.75f)}};
 MotionSettings decoded;CHECK(unpack_motion(pack_motion(cfg),decoded));CHECK(decoded.channel==16 && decoded.cc==cfg.cc);
 auto bad=pack_motion(cfg);bad[3]=121;CHECK(!unpack_motion(bad,decoded));bad=pack_motion(cfg);bad[0]=99;CHECK(!unpack_motion(bad,decoded));
 const std::string path="motion-test.cfg";FileSettings store(path);CHECK(store.save_motion(cfg));
 auto saved=store.load_motion();CHECK(saved.cc==cfg.cc && saved.channel==16);
 CHECK(store.save_setup({}));CHECK(store.load_motion()==saved);std::filesystem::remove(path);std::filesystem::remove(path+".motion");
 MotionCc m;cfg.neutral={{0,0,1}};m.configure(cfg);std::vector<MidiEvent> events;
 auto send=[&](MidiEvent e){events.push_back(e);return true;};
 CHECK(!m.sample(NAN,0,1,1));CHECK(!m.sample(4,0,1,2));CHECK(m.sample(0,0,1,10));
 m.update(40,true,send);CHECK(events.size()==2 && events[0].velocity==64 && events[1].velocity==64 && events[0].channel==16);
 for(uint32_t t=50;t<=500;t+=10){m.sample(0,0,1,t);m.update(t,true,send);}CHECK(events.size()==2);
 for(uint32_t t=510;t<=1510;t+=10){m.sample(.7071068f,0,.7071068f,t);m.update(t,true,send);}
 CHECK(events.back().velocity<=1); // positive X is inverted
 MotionSettings calibrated;CHECK(m.calibrate(1510,calibrated));m.configure(calibrated);events.clear();m.update(1560,true,send);
 CHECK(events.size()==2 && events[0].velocity==64 && events[1].velocity==64);
 events.clear();m.update(2000,true,send);CHECK(events.empty());CHECK(!m.calibrate(2000,calibrated));
 m.sample(0,0,1,2010);m.update(2040,false,send);CHECK(events.empty());
 // Calibration remains well-defined when held vertically or edge-on.
 for(auto neutral: {std::array<float,3>{{0,1,0}},std::array<float,3>{{1,0,0}}}){
  MotionCc vertical;cfg.neutral=neutral;vertical.configure(cfg);vertical.sample(neutral[0],neutral[1],neutral[2],10);events.clear();vertical.update(40,true,send);
  CHECK(events.size()==2 && events[0].velocity==64 && events[1].velocity==64);
 }

 CHECK(motion_target(0)==motion_off && motion_target(1)==1 && motion_target(119)==119);
 for(int option=0;option<120;++option)CHECK(motion_option(motion_target(option))==option);
 cfg.neutral={{0,0,1}};cfg.cc={{74,119}};
 auto old=pack_motion(cfg);old[0]=2;old[2]=255;
 CHECK(unpack_motion(old,decoded) && decoded.channel==cfg.channel && decoded.cc[0]==motion_off && decoded.cc[1]==119 && decoded.neutral==cfg.neutral);
 old[0]=1;old[2]=1;CHECK(unpack_motion(old,decoded) && decoded.cc[0]==motion_off);
 // X runs in the opposite direction; Y still increases for positive tilt.
 MotionCc direction;direction.configure(cfg);direction.sample(0,.7071068f,.7071068f,10);events.clear();direction.update(40,true,send);
 CHECK(events.size()==2 && events[0].velocity==64 && events[1].velocity==127);
 direction.sample(.7071068f,0,.7071068f,1000);
 for(uint32_t t=1010;t<=2200;t+=10){direction.sample(.7071068f,0,.7071068f,t);direction.update(t,true,send);}
 int last_x=-1;for(const auto& event:events)if(event.note==74)last_x=event.velocity;CHECK(last_x>=0 && last_x<=1);
 UsbMidiPacket u;BleMidiPacket b;
 for(int channel=1;channel<=16;++channel)for(int cc=0;cc<120;++cc)for(int value:{0,64,127}){
  MidiEvent e{MidiKind::ControlChange,uint8_t(channel),uint8_t(cc),uint8_t(value)};
  CHECK(encode_usb_midi(e,u) && u[0]==11 && u[1]==(0xb0|channel-1) && u[2]==cc && u[3]==value);
  CHECK(encode_ble_midi(e,1234,b) && b[2]==u[1] && b[3]==cc && b[4]==value);
 }
 Synth synth;synth.event({MidiKind::ControlChange,1,74,127});CHECK(synth.active_voices()==0);

 std::puts("Motion mapping / inverted X / migration / persistence / USB / BLE: PASS");
}

#include "music.hpp"
#include "file_settings.hpp"
#include "axp2101_policy.hpp"
#include "cst9217_report.h"
#include "ble_midi_protocol.hpp"
#include "theme.hpp"
#include "setup_storage.hpp"
#include "power_trace.hpp"
#include "pmic_power_key.hpp"
#include "synth.hpp"
#include "battery.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
using namespace sm;
int checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct MemorySettings:Settings { Selection value,arp; Setup setup; ChordSelection chords; int writes=0,setup_writes=0; bool fail=false; Selection load()override{return value;} bool save(Selection s)override{++writes;if(fail)return false;value=s;return true;} Setup load_setup()override{return setup;} bool save_setup(Setup s)override{++setup_writes;if(fail)return false;setup=s;return true;} Selection load_arp()override{return arp;} ChordSelection load_chords()override{return chords;} bool save_all(Selection k,Selection a,ChordSelection c,Setup s)override { if(k!=value) ++writes; if(s!=setup) ++setup_writes; if(fail)return false; value=k;arp=a;chords=c;setup=s;return true;} };
struct Transport:MidiTransport { std::vector<MidiEvent> events; bool fail=false; bool send(MidiEvent e)override{if(fail)return false;events.push_back(e);return true;} };
struct Registers:PmicRegisters {
 std::array<uint8_t,256> values{};
 std::vector<std::pair<uint8_t,uint8_t>> writes;
 int fail_once=-1,ignore_once=-1;
 bool read(uint8_t reg,uint8_t& value) override { value=values[reg]; return true; }
 bool write(uint8_t reg,uint8_t value) override {
   writes.push_back({reg,value});
   if(fail_once==reg) { fail_once=-1; return false; }
   if(ignore_once==reg) { ignore_once=-1; return true; }
   values[reg]=value; return true;
 }
};
struct KeyRegisters:Registers {
 int fail_read_once=-1;
 bool read(uint8_t reg,uint8_t& value) override {
   if(fail_read_once==reg) { fail_read_once=-1; return false; }
   return Registers::read(reg,value);
 }
 bool write(uint8_t reg,uint8_t value) override {
   const auto old=values[reg]; const bool ignored=ignore_once==reg;
   if(!Registers::write(reg,value)) return false;
   if(reg==0x49 && !ignored) values[reg]=old&uint8_t(~value); // Actual W1C semantics
   return true;
 }
};
std::string names(Selection s) { std::string r;for(auto n:notes(s)){if(!r.empty())r+=", ";r+=n.name;}return r; }
int main() {
 CHECK(Setup{}.travel && Setup{}.synth.waveform==Waveform::Square && Setup{}.synth.attack_ms==50 && Setup{}.synth.decay_ms==400);
 { // Battery telemetry must never write PMIC policy or turn bad data into 0%.
   struct BatteryRegisters:PmicRegisters {
     std::array<uint8_t,256> values{}; int failure=-1,writes=0;
     bool read(uint8_t address,uint8_t& value)override { if(address==failure) return false; value=values[address]; return true; }
     bool write(uint8_t,uint8_t)override { ++writes; return false; }
   } pmic;
   pmic.values[0x00]=0x28; pmic.values[0x18]=8;
   for(int value=0;value<=255;++value) {
     pmic.values[0xa4]=uint8_t(value); const auto battery=read_battery(pmic);
     CHECK(battery.valid && battery.connected && battery.gauge_enabled);
     CHECK(battery.percent==(value<=100?value:-1)); CHECK(pmic.writes==0);
   }
   pmic.values[0xa4]=72;
   for(int failure:{0x00,0x01,0x18,0xa4}) { pmic.failure=failure; auto battery=read_battery(pmic); CHECK(!battery.valid && battery.percent==-1); }
   pmic.failure=-1;
   for(int value=0;value<=255;++value) {
     pmic.values[0x01]=uint8_t(value); CHECK(read_battery(pmic).charging==((value>>5)==1));
   }
   pmic.values[0x01]=0x20;
   auto charging=read_battery(pmic); CHECK(charging.charging && battery_display(charging).external_power && battery_display(charging).bars==-1);
   // USB at boot, standby/full, unplug/replug and a missing battery all use VBUS.
   for(int status:{0,0x20,4}) {
     pmic.values[0x01]=uint8_t(status);
     pmic.values[0x00]=0x28;
     CHECK(battery_display(read_battery(pmic)).external_power);
     CHECK(battery_display(read_battery(pmic)).bars==-1);
     pmic.values[0x00]=8;
     CHECK(!battery_display(read_battery(pmic)).external_power);
     CHECK(battery_display(read_battery(pmic)).bars==4);
     pmic.values[0x00]=0x28;
     CHECK(battery_display(read_battery(pmic)).external_power);
   }
   CHECK(battery_display({true,false,false,-1,false,true}).external_power);
   CHECK(!battery_display({true,true,true,72,false,true},false).external_power);
   pmic.values[0x01]=0x20;
   for(int percent=0;percent<=100;++percent) {
     auto display=battery_display({true,true,true,percent}); CHECK(display.bars==(percent+19)/20 && !display.external_power);
   }
   CHECK(battery_display({true,true,true,72}).bars==4); CHECK(battery_display({true,true,true,100}).bars==5);
   CHECK(battery_display({true,true,true,0}).bars==0); CHECK(battery_display({true,true,true,101}).bars==-1);
   CHECK(battery_display({true,false,false,-1}).missing); CHECK(battery_display({true,true,false,-1}).bars==-1);
   CHECK(battery_display({true,true,true,100},false).bars==-1);
   pmic.failure=-1; pmic.values[0x18]=0;
   auto disabled=read_battery(pmic); CHECK(disabled.valid && disabled.connected && !disabled.gauge_enabled && disabled.percent==-1 && disabled.charging);
   pmic.values[0x00]=0x20; pmic.failure=0x18;
   auto absent=read_battery(pmic); CHECK(absent.valid && !absent.connected && absent.percent==-1); CHECK(pmic.writes==0);
 }
 { // Held tone sustains; Decay is the release duration requested by the user.
   Synth synth; synth.configure({Waveform::Triangle,100,100});
   std::vector<int16_t> attack(4800),held(4800),tail(6000);
   synth.event({MidiKind::NoteOn,1,69,100}); synth.render(attack.data(),attack.size());
   long long early=0,late=0;
   for(int i=0;i<480;++i) { early+=std::abs(int(attack[i])); late+=std::abs(int(attack[4320+i])); }
   CHECK(early*5<late); CHECK(synth.active_voices()==1);
   for(int i=0;i<20;++i) synth.render(held.data(),held.size());
   int crossings=0,peak=0;
   for(size_t i=1;i<held.size();++i) { if(held[i-1]<=0 && held[i]>0) ++crossings; peak=std::max(peak,std::abs(int(held[i]))); }
   CHECK(crossings>=43 && crossings<=45); CHECK(peak>4000 && peak<6000);
   synth.event({MidiKind::NoteOff,1,69,0}); synth.render(tail.data(),tail.size());
   CHECK(synth.active_voices()==0);
   for(size_t i=5000;i<tail.size();++i) CHECK(tail[i]==0);
 }
 for(auto wave:{Waveform::Saw,Waveform::Square,Waveform::Triangle,Waveform::Pulse,Waveform::Sine}) {
   Synth synth; synth.configure({wave,0,10}); int16_t pcm[1024]{};
   synth.event({MidiKind::NoteOn,1,60,100}); synth.event({MidiKind::NoteOn,1,67,100});
   CHECK(synth.active_voices()==2); synth.render(pcm,1024);
   bool sounding=false; for(auto sample:pcm) { CHECK(std::abs(int(sample))<16000); sounding|=sample!=0; } CHECK(sounding);
   synth.event({MidiKind::NoteOn,1,72,100}); CHECK(synth.active_voices()==3);
   synth.event({MidiKind::AllNotesOff,1,0,0}); synth.render(pcm,1024);
   for(auto sample:pcm) CHECK(sample==0);
   for(int pitch=0;pitch<128;++pitch) { synth.event({MidiKind::NoteOn,1,uint8_t(pitch),100}); synth.render(pcm,64); synth.stop(); }
 }
 for(auto wave:{Waveform::Saw,Waveform::Square,Waveform::Triangle,Waveform::Pulse,Waveform::Sine}) {
   // All oscillators retain headroom with two full-velocity voices; no boost.
   Synth synth; synth.configure({wave,0,10});
   synth.event({MidiKind::NoteOn,1,60,127}); synth.event({MidiKind::NoteOn,1,67,127});
   int16_t samples[4800]{}; synth.render(samples,4800);
   for(auto sample:samples) CHECK(std::abs(int(sample))<=12500);
 }
 { // Warm sine contains only the intended low-level second/third partials.
   Synth synth; synth.configure({Waveform::Sine,0,100}); synth.event({MidiKind::NoteOn,1,69,100});
   int16_t warm[4800]{}; synth.render(warm,4800);
   std::vector<int16_t> samples(48000); synth.render(samples.data(),samples.size());
   double amplitude[3]{};
   for(int harmonic=1;harmonic<=3;++harmonic) {
     double real=0,imaginary=0;
     for(size_t i=0;i<samples.size();++i) {
       const double angle=6.283185307179586*440*harmonic*i/48000;
       real+=samples[i]*std::cos(angle); imaginary+=samples[i]*std::sin(angle);
     }
     amplitude[harmonic-1]=std::hypot(real,imaginary);
   }
   CHECK(amplitude[1]/amplitude[0]>.14 && amplitude[1]/amplitude[0]<.18);
   CHECK(amplitude[2]/amplitude[0]>.06 && amplitude[2]/amplitude[0]<.10);
 }
 { // Pulse must not introduce a DC component into the speaker signal.
   Synth synth; synth.configure({Waveform::Pulse,0,100}); synth.event({MidiKind::NoteOn,1,69,100});
   std::vector<int16_t> samples(48000); synth.render(samples.data(),samples.size());
   long long sum=0; for(auto sample:samples) sum+=sample;
   CHECK(std::abs(double(sum)/samples.size())<10);
 }
 for(auto wave:{Waveform::Saw,Waveform::Square,Waveform::Triangle,Waveform::Pulse,Waveform::Sine}) {
   // A chord attack adds a fresh envelope while its old release keeps decaying.
   Synth chord; chord.configure({wave,100,4000}); chord.event({MidiKind::NoteOn,1,60,100,true});
   int16_t warm_chord[4800]{}; chord.render(warm_chord,4800);
   chord.event({MidiKind::NoteOff,1,60,0}); Synth tail=chord;
   Synth attack; attack.configure({wave,100,4000});
   chord.event({MidiKind::NoteOn,1,60,100,true}); attack.event({MidiKind::NoteOn,1,60,100,true});
   CHECK(chord.active_voices()==2);
   int16_t combined[480]{},old_tail[480]{},new_attack[480]{};
   chord.render(combined,480); tail.render(old_tail,480); attack.render(new_attack,480);
   for(int i=0;i<480;++i) CHECK(std::abs(int(combined[i])-int(old_tail[i])-int(new_attack[i]))<=2);
   // Rapid Off/On is sample-continuous, not a fresh Attack.
   Synth actual; actual.configure({wave,100,400}); actual.event({MidiKind::NoteOn,1,69,100});
   int16_t warm[960]{}; actual.render(warm,960); Synth reference=actual;
   for(int repeat=0;repeat<12;++repeat) {
     actual.event({MidiKind::NoteOff,1,69,0}); actual.event({MidiKind::NoteOn,1,69,100});
     int16_t a[240]{},b[240]{}; actual.render(a,240); reference.render(b,240);
     for(int i=0;i<240;++i) CHECK(a[i]==b[i]);
   }
 }
 { // Fast note changes must not keep restarting a long Attack from zero.
   Synth synth; synth.configure({Waveform::Triangle,2000,400}); synth.event({MidiKind::NoteOn,1,60,100});
   std::vector<int16_t> warm(96000); synth.render(warm.data(),warm.size()); int old=60;
   for(int repeat=0;repeat<50;++repeat) {
     const int next=60+(repeat+1)%12;
     synth.event({MidiKind::NoteOff,1,uint8_t(old),0}); synth.event({MidiKind::NoteOn,1,uint8_t(next),100});
     int16_t samples[240]{}; synth.render(samples,240); int peak=0;
     for(auto sample:samples) peak=std::max(peak,std::abs(int(sample)));
     CHECK(peak>2000); CHECK(synth.active_voices()==1); old=next;
   }
 }
 { // Voice stealing crossfades the replaced voice instead of cutting it.
   Synth synth; synth.configure({Waveform::Triangle,0,400});
   for(int note:{60,67,64,71,74,76,77,79,81,83}) synth.event({MidiKind::NoteOn,1,uint8_t(note),100});
   int16_t before[1000]{},after[300]{}; synth.render(before,1000);
   synth.event({MidiKind::NoteOn,1,72,100}); synth.render(after,300);
   CHECK(std::abs(int(after[0])-int(before[999]))<2000); CHECK(synth.active_voices()==10);
   synth.event({MidiKind::AllNotesOff,1,0,0}); synth.render(after,300);
   for(auto sample:after) CHECK(sample==0);
 }
 { // Output routing and failures never duplicate local notes onto MIDI.
   struct Midi:MidiTransport { int count=0; bool enabled=true; bool send(MidiEvent)override{++count;return enabled;} void set_enabled(bool v)override{enabled=v;} } midi;
   struct Audio:LocalAudio { Synth synth; bool fail=false,enabled=false; bool configure(bool v,SynthSettings s)override{enabled=v;synth.configure(s);return !fail;} bool send(MidiEvent e)override{synth.event(e);return enabled;} } audio;
   PerformanceOutput output(midi,audio); MidiRouter router(output);
   Setup midi_only;midi_only.travel=false;CHECK(output.configure(midi_only)); router.press(0,60); router.release(0); CHECK(midi.count==2);
   Setup travel; travel.travel=true; CHECK(output.configure(travel)); CHECK(!midi.enabled);
   router.press(0,60); router.press(1,67); CHECK(audio.synth.active_voices()==2); CHECK(midi.count==2);
   router.panic(); CHECK(audio.synth.active_voices()==0); CHECK(midi.count==2);
   audio.fail=true; CHECK(!output.configure(travel)); output.send({MidiKind::NoteOn,1,60,100}); CHECK(midi.count==2);
   audio.fail=false; CHECK(output.configure(midi_only)); router.press(0,62); CHECK(midi.count==3);
 }
 for(int wave=0;wave<waveform_count;++wave) for(bool enabled:{false,true}) for(int channel=1;channel<=16;++channel)
   for(uint16_t attack:{0,100,2000}) for(uint16_t decay:{10,400,4000}) for(bool receive:{false,true}) {
     Setup setup{channel,0x2ee7e7,enabled,{Waveform(wave),attack,decay},receive};
     CHECK(unpack_setup(pack_setup(setup))==setup);
   }
 CHECK((unpack_setup((uint64_t(1)<<32)|(uint64_t(9)<<24)|0x123456)==Setup{9,0x123456}));
 // Existing v2 waveform ID 2 (formerly sine) becomes Triangle in place.
 const uint64_t legacy_sine=(uint64_t(2)<<56)|(uint64_t(400)<<42)|(uint64_t(20)<<31)|
   (uint64_t(2)<<29)|(uint64_t(1)<<28)|(uint64_t(8)<<24)|0x123456;
 CHECK((unpack_setup(legacy_sine)==Setup{9,0x123456,true,{Waveform::Triangle,20,400}}));
 { Setup saved{16,0x123456,true,{Waveform::Sine,20,400},true};
   const uint64_t legacy_v3=(pack_setup(saved)&~((uint64_t(255)<<56)|(uint64_t(1)<<55)))|(uint64_t(3)<<56);
   saved.receive_clock=false; CHECK(unpack_setup(legacy_v3)==saved);
 }
 // PMIC key IRQ configuration and ACKs must preserve every unrelated flag.
 for(int mask=0;mask<256;++mask) {
   KeyRegisters registers; registers.values[0x41]=uint8_t(mask); registers.values[0x49]=0xff;
   registers.values[0x10]=0x30; registers.values[0x22]=4;
   CHECK(enable_pmic_power_key(registers)); CHECK(registers.values[0x41]==uint8_t(mask|3));
   CHECK(registers.values[0x49]==0xfc); CHECK(registers.values[0x10]==0x30 && registers.values[0x22]==4);
   for(auto write:registers.writes) CHECK((write.first==0x41 && write.second==uint8_t(mask|3)) || (write.first==0x49 && write.second==3));
   registers.writes.clear(); registers.values[0x49]=uint8_t(mask); uint8_t status=0;
   CHECK(poll_pmic_power_key(registers,status)); CHECK(status==mask); CHECK(registers.values[0x49]==(mask&~3));
   CHECK(registers.writes.size()==((mask&3)?1u:0u));
   for(auto write:registers.writes) CHECK(write.first==0x49 && write.second==(mask&3));
 }
 for(int fail:{0x41,0x49}) {
   KeyRegisters registers; registers.values[0x41]=0xfc; registers.fail_once=fail;
   CHECK(!enable_pmic_power_key(registers)); CHECK(registers.values[0x41]==0xfc);
 }
 KeyRegisters ignored; ignored.values[0x41]=0xfc; ignored.ignore_once=0x41;
 CHECK(!enable_pmic_power_key(ignored)); CHECK(ignored.values[0x41]==0xfc);
 KeyRegisters read_error; read_error.fail_read_once=0x41;
 CHECK(!enable_pmic_power_key(read_error)); CHECK(read_error.writes.empty());
 uint8_t failed_status=0; read_error.fail_read_once=0x49;
 CHECK(!poll_pmic_power_key(read_error,failed_status)); CHECK(read_error.writes.empty());
 read_error.values[0x49]=0xc3; read_error.fail_once=0x49;
 CHECK(!poll_pmic_power_key(read_error,failed_status)); CHECK(read_error.values[0x49]==0xc3);
 // A real PMIC press/hold/release retains the existing 20/40/600 ms timing,
 // including timestamp wrap. USB/battery status bits are never button input.
 for(uint32_t base:{0u,0xffffff00u}) {
   PmicPowerKey key; std::vector<ButtonEvent> events;
   auto receive=[](int b,ButtonEvent event,void* p) { CHECK(b==1); static_cast<std::vector<ButtonEvent>*>(p)->push_back(event); };
   for(uint32_t t=0;t<=1000;t+=10) {
     const uint8_t status=t==0?3:t==100?0x82:t==800?0x41:0xf0;
     key.update(true,status,base+t,receive,&events);
     if(t<120) CHECK(events.empty());
     if(t==120) CHECK(events==std::vector<ButtonEvent>{ButtonEvent::Press});
     if(t==720) CHECK((events==std::vector<ButtonEvent>{ButtonEvent::Press,ButtonEvent::Hold}));
   }
   CHECK((events==std::vector<ButtonEvent>{ButtonEvent::Press,ButtonEvent::Hold,ButtonEvent::Release}));
 }
 for(int scenario=0;scenario<3;++scenario) {
   PmicPowerKey key; std::vector<ButtonEvent> events;
   auto receive=[](int,ButtonEvent event,void* p) { static_cast<std::vector<ButtonEvent>*>(p)->push_back(event); };
   for(uint32_t t=0;t<=1400;t+=10) {
     uint8_t status=t==100 || t==500?2:t==1200?1:0;
     if(t==300 && scenario==0) status=3; // coalesced, unknowable edge order
     if(t==310 && scenario==1) status=2; // stale edge after read/ACK failure
     if(scenario==2 && t>200 && t<400) continue; // scanner/polling gap
     key.update(!(t==300 && scenario==1),status,t,receive,&events);
   }
   CHECK((events==std::vector<ButtonEvent>{ButtonEvent::Press,ButtonEvent::Cancel,ButtonEvent::Press,ButtonEvent::Hold,ButtonEvent::Release}));
 }
 { // No fake click from a held power-on key releasing, or VBUS changes alone.
   PmicPowerKey key; int events=0;
   auto receive=[](int,ButtonEvent,void* p) { ++*static_cast<int*>(p); };
   for(uint32_t t=0;t<=2000;t+=10) key.update(true,t==0?3:t==800?1:t%20?0x80:0x40,t,receive,&events);
   CHECK(events==0);
 }
 { // Brief contact bounce must not restart a hold or create a second click.
   PmicPowerKey key; std::vector<ButtonEvent> events;
   auto receive=[](int,ButtonEvent event,void* p) { static_cast<std::vector<ButtonEvent>*>(p)->push_back(event); };
   for(uint32_t t=0;t<=1000;t+=10) key.update(true,t==100 || t==190?2:t==180 || t==800?1:0,t,receive,&events);
   CHECK((events==std::vector<ButtonEvent>{ButtonEvent::Press,ButtonEvent::Hold,ButtonEvent::Release}));
 }
 // Bounded offline trace: FIFO order, wrap/overwrite, stale/future reads,
 // 32-bit timestamp wrap and large repeated use without allocating memory.
 PowerTrace<3> trace; PowerTraceEntry entry;
 CHECK(trace.begin()==0 && trace.end()==0); CHECK(!trace.get(0,entry));
 for(uint32_t i=0;i<10000;++i) {
   trace.push({0xffffff00u+i,PowerTraceKind::Raw,uint8_t(i%2),uint16_t(i%3),i,i+1});
   CHECK(trace.end()==uint64_t(i)+1); CHECK(trace.begin()==(i>=3?i-2:0));
   CHECK(!trace.get(trace.end(),entry));
   if(trace.begin()) CHECK(!trace.get(trace.begin()-1,entry));
   for(auto j=trace.begin();j<trace.end();++j) {
     CHECK(trace.get(j,entry)); CHECK(entry.value==j); CHECK(entry.extra==j+1);
     CHECK(entry.time==uint32_t(0xffffff00u+uint32_t(j)));
     CHECK(entry.kind==PowerTraceKind::Raw); CHECK(entry.detail==j%2); CHECK(entry.flags==j%3);
   }
 }
 CHECK(accent_for_hue(180)==0x2ee7e7); CHECK(accent_for_hue(0)==accent_for_hue(360));
 for(int h=0;h<360;++h) CHECK(std::abs(hue_for_accent(accent_for_hue(h))-h)<.4f);
 CHECK(!valid_setup({0,0x2ee7e7})); CHECK(!valid_setup({17,0x2ee7e7})); CHECK(!valid_setup({1,0x1000000}));
 for(int channel=1;channel<=16;++channel) for(int hue=0;hue<360;++hue) {
   Setup setup{channel,accent_for_hue(hue)}; CHECK(unpack_setup(pack_setup(setup))==setup);
 }
 CHECK(unpack_setup(0)==Setup{}); CHECK(unpack_setup(uint64_t(2)<<32)==Setup{});
 CHECK(unpack_setup((uint64_t(1)<<32)|(uint64_t(17)<<24))==Setup{});
 BleMidiPacket packet{};
 CHECK(encode_ble_midi({MidiKind::NoteOn,1,60,100},0,packet));
 CHECK((packet==BleMidiPacket{0x80,0x80,0x90,60,100}));
 CHECK(encode_ble_midi({MidiKind::NoteOff,16,127,0},8191,packet));
 CHECK((packet==BleMidiPacket{0xbf,0xff,0x8f,127,0}));
 CHECK(encode_ble_midi({MidiKind::AllNotesOff,2,0,0},8192,packet));
 CHECK((packet==BleMidiPacket{0x80,0x80,0xb1,123,0}));
 CHECK(encode_ble_midi({MidiKind::NoteOn,1,60,100},128,packet));
 CHECK(packet[0]==0x81 && packet[1]==0x80);
 CHECK(!encode_ble_midi({MidiKind::NoteOn,0,60,100},0,packet));
 CHECK(!encode_ble_midi({MidiKind::NoteOn,17,60,100},0,packet));
 CHECK(!encode_ble_midi({MidiKind::NoteOn,1,128,100},0,packet));
 CHECK(!encode_ble_midi({MidiKind::NoteOn,1,60,128},0,packet));
 BleMidiOutbox box; BleMidiOutbox::Pending pending,old;
 CHECK(!box.enqueue({MidiKind::NoteOn,1,60,100},0));
 CHECK(box.enqueue({MidiKind::NoteOff,1,60,0},0));
 box.session(true); auto generation=box.generation();
 CHECK(box.reset_pending() && !box.ready());
 box.acknowledge_reset(generation);
 for(int i=0;i<32;++i) {
   CHECK(box.peek(42,pending));
   CHECK(pending.packet[2]==0xb0+i/2 && pending.packet[3]==(i%2?120:123));
   box.delivered(pending);
 }
 CHECK(box.ready() && !box.peek(43,pending));
 CHECK(box.enqueue({MidiKind::NoteOn,1,60,100},100));
 CHECK(box.enqueue({MidiKind::NoteOff,1,60,0},200));
 CHECK(box.peek(300,pending)); old=pending;
 CHECK(pending.packet[1]==uint8_t(0x80|100) && pending.packet[2]==0x90);
 CHECK(box.peek(400,pending) && pending.packet==old.packet); // failed notify is retried unchanged
 box.delivered(pending); box.delivered(pending); // duplicate completion must not drop next note
 CHECK(box.peek(400,pending) && pending.packet[2]==0x80);
 box.session(false); CHECK(!box.peek(500,pending));
 box.session(true); box.acknowledge_reset(generation); CHECK(box.reset_pending());
 box.delivered(old); CHECK(box.peek(500,pending) && pending.packet[3]==123);
 box.acknowledge_reset(box.generation());
 for(int i=0;i<32;++i) { CHECK(box.peek(500,pending)); box.delivered(pending); }
 CHECK(box.ready());
 for(int i=0;i<128;++i) CHECK(box.enqueue({MidiKind::NoteOn,1,60,100},600));
 CHECK(!box.enqueue({MidiKind::NoteOn,1,61,100},601));
 CHECK(!box.ready() && box.reset_pending());
 CHECK(box.peek(601,pending) && pending.packet[2]==0xb0 && pending.packet[3]==123);
 // BLE batches keep chords together without changing the single-message USB API.
 box.session(true); box.acknowledge_reset(box.generation());
 for(int i=0;i<32;++i) { CHECK(box.peek(0,pending)); box.delivered(pending); }
 for(int i=0;i<6;++i) CHECK(box.enqueue({MidiKind::NoteOn,1,uint8_t(60+i),100},127));
 CHECK(box.enqueue({MidiKind::NoteOff,1,60,0},128));
 BleMidiOutbox::Batch batch;
 CHECK(box.peek_batch(130,batch) && batch.length==17 && batch.token.count==4);
 auto first=batch.token;
 for(int i=0;i<4;++i) CHECK(batch.packet[1+i*4]==0xff && batch.packet[3+i*4]==60+i);
 box.delivered(first); box.delivered(first); // Duplicate acknowledgement is harmless.
 CHECK(box.peek_batch(130,batch) && batch.length==9 && batch.token.count==2);
 CHECK(batch.packet[3]==64 && batch.packet[7]==65); // Stop at timestampHigh rollover.
 box.delivered(batch.token);
 CHECK(box.peek_batch(130,batch) && batch.length==5 && batch.packet[0]==0x81 && batch.packet[2]==0x80);
 box.session(false); box.delivered(batch.token); CHECK(!box.peek_batch(131,batch));
 std::array<uint8_t,15> report{}; sm_cst_point points[2]{};
 report[0]=0x06; report[1]=10; report[2]=20; report[3]=0x34;
 report[5]=2; report[6]=0xab;
 report[7]=0x16; report[8]=24; report[9]=5; report[10]=0xf2;
 CHECK(sm_cst9217_decode(report.data(),report.size(),points)==2);
 CHECK(points[0].id==0 && points[0].x==163 && points[0].y==324);
 CHECK(points[1].id==1 && points[1].x==399 && points[1].y==82);
 report[0]=0; CHECK(sm_cst9217_decode(report.data(),15,points)==1); CHECK(points[0].id==1 && points[0].x==399);
 report[0]=0x06;
 for(int i=0;i<5;++i) std::swap(report[i],report[7+i]);
 CHECK(sm_cst9217_decode(report.data(),15,points)==2); CHECK(points[0].id==1 && points[1].id==0);
 report[5]=1; CHECK(sm_cst9217_decode(report.data(),15,points)==1); CHECK(points[0].id==1);
 report[5]=0; CHECK(sm_cst9217_decode(report.data(),15,points)==0);
 report[5]=3; CHECK(sm_cst9217_decode(report.data(),15,points)==-1);
 report[5]=2; report[7]=0x16; CHECK(sm_cst9217_decode(report.data(),15,points)==-1);
 report[6]=0; CHECK(sm_cst9217_decode(report.data(),15,points)==-1);
 CHECK(sm_cst9217_decode(report.data(),10,points)==-1);
 // Power policy must preserve every unrelated bit, notably thermal shutdown.
 for(int r22=0;r22<8;++r22) for(int r10=0;r10<256;r10+=4) {
   Registers pmic; pmic.values[0x22]=uint8_t(r22); pmic.values[0x10]=uint8_t(r10);
   CHECK(disable_pwr_shutdown(pmic));
   CHECK(pmic.values[0x22]==(r22&~2)); CHECK(pmic.values[0x10]==(r10&~4));
   for(auto write:pmic.writes) CHECK(write.first==0x10 || write.first==0x22);
   pmic.writes.clear(); CHECK(disable_pwr_shutdown(pmic)); CHECK(pmic.writes.empty());
 }
 for(bool ignore:{false,true}) {
   Registers pmic; pmic.values[0x22]=7; pmic.values[0x10]=0x34;
   if(ignore) pmic.ignore_once=0x10; else pmic.fail_once=0x10;
   CHECK(!disable_pwr_shutdown(pmic)); CHECK(pmic.values[0x22]==7); CHECK(pmic.values[0x10]==0x34);
 }
 Registers shutdown; shutdown.values[0x10]=0xb0;
 CHECK(request_pmic_shutdown(shutdown)); CHECK(shutdown.values[0x10]==0xb1);
 shutdown.writes.clear(); CHECK(!request_pmic_shutdown(shutdown)); CHECK(!disable_pwr_shutdown(shutdown)); CHECK(shutdown.writes.empty());
 CHECK(names({2,1})=="D, E, F, G, A, Bb, C"); CHECK(names({0,0})=="C, D, E, F, G, A, B");
 CHECK(names({0,9})=="C, D, E, G, A"); CHECK(names({0,10})=="C, Eb, F, G, Bb");
 CHECK(names({0,blues_mode})=="C, Eb, F, Gb, G, Bb");
 CHECK(names({2,blues_mode})=="D, F, G, Ab, A, C");
 CHECK(valid(Selection{0,blues_mode})); CHECK(!valid(Selection{0,12})); CHECK(modes().size()==12);
 CHECK(names({6,0})=="F#, G#, A#, B, C#, D#, E#");
 for(int root=0;root<12;++root)for(int mode=0;mode<int(modes().size());++mode) {
   auto n=notes({root,mode}); CHECK(n.size()==(mode==blues_mode?6:mode>=9?5:7));
   CHECK(n[0].pitch_class==root);
   for(size_t i=0;i<n.size();++i) { CHECK(n[i].pitch_class==(root+modes()[mode].intervals[i])%12); CHECK(n[i].midi==60+root+modes()[mode].intervals[i]); if(i) CHECK(n[i].midi>n[i-1].midi); }
 }
 for(const auto& m:modes()) CHECK(m.type==ModeType::Scale); CHECK(modes()[0].type==ModeType::Scale);
 MemorySettings memory; Application app(memory); CHECK((app.current==Selection{0,0}));
 app.begin(); app.draft={0,0}; app.back(); CHECK((app.current==Selection{0,0})); CHECK(memory.writes==0);
 app.begin(); app.draft={0,10}; CHECK(app.select()); CHECK((app.current==Selection{0,10})); CHECK(memory.writes==1);
 app.begin(); CHECK(app.select()); CHECK(memory.writes==1);
 app.begin(); app.draft={5,0}; memory.fail=true; CHECK(!app.select()); CHECK(app.selecting); CHECK((app.current==Selection{0,10}));
 CHECK(app.setup==Setup{}); CHECK(app.save_setup(Setup{})); CHECK(memory.setup_writes==0);
 CHECK(!app.save_setup({0,0x2ee7e7})); CHECK(memory.setup_writes==0);
 CHECK(!app.save_setup({16,0xe72e2e})); CHECK(app.setup==Setup{});
 memory.fail=false; CHECK(app.save_setup({16,0xe72e2e})); CHECK(memory.setup_writes==2);
 Application restarted(memory); CHECK((restarted.setup==Setup{16,0xe72e2e})); CHECK((restarted.current==Selection{0,10}));
 CHECK(restarted.save_setup(restarted.setup)); CHECK(memory.setup_writes==2);
 FileSettings file("test-settings.cfg"); std::filesystem::remove("test-settings.cfg"); CHECK((file.load()==Selection{})); {std::ofstream f("test-settings.cfg");f<<"1 0 12";} CHECK((file.load()==Selection{})); CHECK(file.save({0,9})); CHECK((file.load()==Selection{0,9})); CHECK(file.save({9,2})); CHECK((file.load()==Selection{9,2}));
 {std::ofstream f("test-settings.cfg");f<<"1 99 999";} CHECK((file.load()==Selection{}));
 {std::ofstream f("test-settings.cfg");f<<"2 0 0";} CHECK((file.load()==Selection{})); std::filesystem::remove("test-settings.cfg");
 {std::ofstream f("test-settings.cfg");f<<"1 2 1";} // migrate old selection without losing it
 CHECK((file.load()==Selection{2,1})); CHECK(file.load_setup()==Setup{});
 CHECK(file.save_setup({9,0xe72e2e})); CHECK((file.load()==Selection{2,1}));
 FileSettings after_restart("test-settings.cfg"); CHECK((after_restart.load_setup()==Setup{9,0xe72e2e}));
 CHECK(after_restart.save({7,4})); CHECK((file.load_setup()==Setup{9,0xe72e2e}));
 CHECK(!file.save_setup({17,0})); CHECK((file.load_setup()==Setup{9,0xe72e2e}));
 {std::ofstream f("test-settings.cfg");f<<"2 2 1 0 123";} CHECK(file.load_setup()==Setup{}); CHECK((file.load()==Selection{2,1}));
 {std::ofstream f("test-settings.cfg");f<<"2 2 1 7 16777216";} CHECK(file.load_setup()==Setup{});
 {std::ofstream f("test-settings.cfg");f<<"2 2 1 7 123 garbage";} CHECK(file.load_setup()==Setup{});
 {std::ofstream f("test-settings.cfg");f<<"2 2 1 7 123";}
 CHECK((file.load_setup()==Setup{7,123}));
 const Setup saved_travel{7,123,true,{Waveform::Square,2000,4000}};
 CHECK(file.save_setup(saved_travel));
 FileSettings travel_restart("test-settings.cfg"); CHECK(travel_restart.load_setup()==saved_travel);
 CHECK(travel_restart.save({5,3})); CHECK(file.load_setup()==saved_travel);
  {std::ifstream f("test-settings.cfg"); int version=0; f>>version; CHECK(version==19);}
 { Setup receive=saved_travel; receive.receive_clock=true; CHECK(file.save_setup(receive));
   FileSettings loaded("test-settings.cfg"); CHECK(loaded.load_setup()==receive); CHECK(loaded.save({0,0})); CHECK(file.load_setup()==receive);
 }
 {std::ofstream f("test-settings.cfg");f<<"4 2 1 9 1193046 1 4 20 400";}
 CHECK((file.load_setup()==Setup{9,0x123456,true,{Waveform::Sine,20,400},false}));
 {std::ofstream f("test-settings.cfg");f<<"5 2 1 9 1193046 1 4 20 400 2";}
 CHECK(file.load_setup()==Setup{});
 CHECK(!valid_synth({Waveform(5),10,400})); CHECK(!valid_synth({Waveform::Triangle,2001,400}));
 CHECK(!valid_synth({Waveform::Triangle,10,0})); CHECK(!valid_synth({Waveform::Triangle,10,4001}));
 {std::ofstream f("test-settings.cfg");f<<"3 2 1 9 1193046 1 2 20 400";}
 CHECK((file.load_setup()==Setup{9,0x123456,true,{Waveform::Triangle,20,400}}));
 std::filesystem::remove("test-settings.cfg");
 Transport t; MidiRouter router(t); router.press(0,60);router.press(1,60);router.release(0);CHECK(t.events.size()==1);router.release(1);CHECK(t.events.size()==2);CHECK(t.events.back().kind==MidiKind::NoteOff);
 router.press(0,62);router.press(0,64);CHECK(t.events[t.events.size()-2].kind==MidiKind::NoteOff);router.panic();CHECK(t.events.back().kind==MidiKind::AllNotesOff); CHECK(!router.channel(17));
 router.press(0,65); t.fail=true; router.release(0); router.press(0,67); CHECK(t.events.back().note==65); CHECK(!router.channel(2));
 t.fail=false; router.panic(); CHECK(t.events[t.events.size()-2].kind==MidiKind::NoteOff); CHECK(t.events[t.events.size()-2].note==65); CHECK(router.channel(2));
 NoteEngine engine(router,{2,1}); engine.press(0,5);CHECK(t.events.back().note==70);CHECK(t.events.back().channel==2);engine.release(0);CHECK(t.events.back().kind==MidiKind::NoteOff);
 engine.selection({0,10});engine.press(0,1);CHECK(t.events.back().note==63);engine.release(0);
 CHECK(engine.octave()==4); CHECK(!engine.set_octave(-1)); CHECK(!engine.set_octave(9)); CHECK(engine.octave()==4);
 engine.selection({0,0}); engine.press(0,0); CHECK(t.events.back().note==60);
 const auto held_events=t.events.size();
 CHECK(engine.set_octave(5)); CHECK(engine.set_octave(3)); CHECK(engine.set_octave(8)); CHECK(t.events.size()==held_events);
 engine.press(1,0); CHECK(t.events.back().note==108);
 engine.release(0); CHECK(t.events.back().kind==MidiKind::NoteOff); CHECK(t.events.back().note==60);
 engine.release(1); CHECK(t.events.back().kind==MidiKind::NoteOff); CHECK(t.events.back().note==108);
 CHECK(engine.set_octave(5));
 engine.press(0,0); CHECK(t.events.back().note==72); engine.release(0);
 for(int octave=0;octave<=8;++octave) for(int root=0;root<12;++root) for(int mode=0;mode<int(modes().size());++mode) {
   engine.selection({root,mode}); CHECK(engine.set_octave(octave));
   for(size_t degree=0;degree<modes()[mode].intervals.size();++degree) {
     int expected=12*(octave+1)+root+modes()[mode].intervals[degree];
     CHECK(engine.pitch(int(degree))==expected); CHECK(engine.playable(int(degree))==(expected<=127));
     const auto before=t.events.size();
     engine.press(0,int(degree)); auto event=t.events.back();
     if(expected>127) { CHECK(t.events.size()==before); engine.release(0); CHECK(t.events.size()==before); continue; }
     CHECK(event.kind==MidiKind::NoteOn); CHECK(event.note==expected);
     CHECK(event.note%12==(root+modes()[mode].intervals[degree])%12);
     engine.release(0); CHECK(t.events.back().kind==MidiKind::NoteOff);
   }
 }
 engine.selection({11,1}); CHECK(engine.set_octave(8));
 engine.press(0,1); CHECK(t.events.back().note==121); engine.release(0); // C#9
 engine.press(0,5); CHECK(t.events.back().note==127); // G9
 engine.press(0,6); CHECK(t.events.back().kind==MidiKind::NoteOff); CHECK(t.events.back().note==127); // A9 is outside MIDI.
 std::vector<std::pair<int,ButtonEvent>> ev; Buttons b; auto cb=[](int i,ButtonEvent e,void* p){static_cast<decltype(ev)*>(p)->push_back({i,e});};
 b.update(0,true,0,cb,&ev); b.update(0,true,21,cb,&ev);b.update(1,true,21,cb,&ev);b.update(1,true,42,cb,&ev);b.update(0,true,622,cb,&ev);b.update(0,false,630,cb,&ev);b.update(0,false,651,cb,&ev);
 CHECK(ev.size()==4);CHECK(ev[0].second==ButtonEvent::Press);CHECK(ev[1].first==1);CHECK(ev[2].second==ButtonEvent::Hold);CHECK(ev[3].second==ButtonEvent::Release);
 // Hardware scan uses short release debounce: a brief valid gap must rearm
 // the next press, while an isolated two-millisecond bounce must not.
 for(int button=0;button<2;++button) {
   Buttons rapid(10,4); ev.clear();
   for(uint32_t now=0;now<250;now+=2) rapid.update(button,now%50<40,now,cb,&ev);
   CHECK(ev.size()==10);
   for(size_t i=0;i<ev.size();++i) { CHECK(ev[i].first==button); CHECK(ev[i].second==(i%2?ButtonEvent::Release:ButtonEvent::Press)); }
   Buttons bounce(10,4); ev.clear();
   for(uint32_t now=0;now<=30;now+=2) bounce.update(button,true,now,cb,&ev);
   bounce.update(button,false,32,cb,&ev); bounce.update(button,true,34,cb,&ev);
   for(uint32_t now=36;now<=60;now+=2) bounce.update(button,true,now,cb,&ev);
   CHECK(ev.size()==1); CHECK(ev[0].second==ButtonEvent::Press);
 }
 // A power-on press and unstable release never reach the UI. The first
 // subsequent press still produces exactly Press/Hold/Release, even at wrap.
 for(uint32_t base:{10000u,0xffffff00u}) {
   Buttons startup(20,40); startup.require_release(1,100); ev.clear();
   for(uint32_t t=0;t<=2000;t+=2) startup.update(1,true,base+t,cb,&ev);
   for(uint32_t t=2002;t<2100;t+=2) startup.update(1,false,base+t,cb,&ev);
   startup.update(1,true,base+2100,cb,&ev); // bounce restarts the quiet period
   for(uint32_t t=2102;t<=2200;t+=2) startup.update(1,false,base+t,cb,&ev);
   CHECK(ev.empty());
   for(uint32_t t=2202;t<=2220;t+=2) startup.update(1,true,base+t,cb,&ev);
   CHECK(ev.empty()); // fewer than 100 ms released: still unarmed
   for(uint32_t t=2222;t<=2322;t+=2) startup.update(1,false,base+t,cb,&ev);
   CHECK(ev.empty());
   for(uint32_t t=2324;t<=3000;t+=2) startup.update(1,true,base+t,cb,&ev);
   CHECK(ev.size()==2); CHECK(ev[0].second==ButtonEvent::Press); CHECK(ev[1].second==ButtonEvent::Hold);
   startup.update(1,false,base+3002,cb,&ev);
   startup.update(1,true,base+3020,cb,&ev); // short release bounce during hold
   startup.update(1,true,base+3060,cb,&ev); CHECK(ev.size()==2);
   for(uint32_t t=3062;t<=3102;t+=2) startup.update(1,false,base+t,cb,&ev);
   CHECK(ev.size()==3); CHECK(ev.back().second==ButtonEvent::Release);
   for(uint32_t t=3104;t<=3124;t+=2) startup.update(1,true,base+t,cb,&ev);
   for(uint32_t t=3126;t<=3166;t+=2) startup.update(1,false,base+t,cb,&ev);
   CHECK(ev.size()==5); CHECK(ev[3].second==ButtonEvent::Press); CHECK(ev[4].second==ButtonEvent::Release);
 }
 // USB/reset startup with PWR already released also waits the full quiet period.
 Buttons idle_start(20,40); idle_start.require_release(1); ev.clear();
 idle_start.update(1,false,100000,cb,&ev); idle_start.update(1,false,100098,cb,&ev);
 idle_start.update(1,true,100099,cb,&ev); idle_start.update(1,true,101000,cb,&ev);
 CHECK(ev.empty());
 idle_start.update(1,false,101002,cb,&ev); idle_start.update(1,false,101102,cb,&ev);
 idle_start.update(1,true,101104,cb,&ev); idle_start.update(1,true,101124,cb,&ev);
 CHECK(ev.size()==1); CHECK(ev[0].second==ButtonEvent::Press);
 std::printf("PASS: %d checks\n",checks); return 0;
}

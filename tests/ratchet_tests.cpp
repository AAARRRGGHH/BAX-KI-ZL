#include "arpeggiator.hpp"
#include "arp_storage.hpp"
#include "file_settings.hpp"
#include "music_slot_storage.hpp"
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include <filesystem>
using namespace sm;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"Ratchet FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct Output:MidiTransport {
 uint64_t now=0; std::vector<MidiEvent> events; std::vector<uint64_t> times;
 bool send(MidiEvent e)override { events.push_back(e); times.push_back(now); return true; }
 int ons()const { int count=0; for(auto e:events) if(e.kind==MidiKind::NoteOn) ++count; return count; }
 void balanced()const { bool held=false; for(auto e:events) {
   if(e.kind==MidiKind::NoteOn) { CHECK(!held); held=true; }
   if(e.kind==MidiKind::NoteOff) { CHECK(held); held=false; }
 } CHECK(!held); }
};
int main() {
 CHECK(ArpSettings{}.ratchet_every==0 && ArpSettings{}.ratchet_count==2);
 for(int initial:{5,30,50,75,90,100}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.gate=initial; arp.settings(s); s.ratchet_every=4; arp.settings(s);
   auto active=arp.state().settings;
   CHECK(active.gate==std::min(initial,50) && active.gate_before_ratchet==initial);
   active.ratchet_count=4; active.ratchet_every=8; arp.settings(active);
   CHECK(arp.state().settings.gate_before_ratchet==initial);
   // The snapshot survives a current gate edit and a complete preset reload.
   active=arp.state().settings; active.gate=25; arp.settings(active);
   const auto saved=arp.preset(9); CHECK(unpack_arpeggiator(pack_arpeggiator(saved))==saved);
   MusicPreset slot; slot.arpeggiator=saved; MusicPreset restored_slot;
   CHECK(unpack_music_slot(pack_music_slot(slot),restored_slot) && restored_slot==slot);
   Output reloaded_out; MidiRouter reloaded_router(reloaded_out); Arpeggiator reloaded(reloaded_router);
   CHECK(reloaded.restore(restored_slot.arpeggiator)); active=reloaded.state().settings;
   CHECK(active.gate==25 && active.gate_before_ratchet==initial);
   active.ratchet_every=0; reloaded.settings(active);
   CHECK(reloaded.state().settings.gate==initial && reloaded.state().settings.gate_before_ratchet==0);
 }
 { // Old active Ratchet presets migrate to a separated gate without losing the original.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpPreset old;
   old.settings.gate=100; old.settings.ratchet_every=4;
   CHECK(arp.restore(old)); CHECK(arp.state().settings.gate==50 && arp.state().settings.gate_before_ratchet==100);
   auto s=arp.state().settings; s.ratchet_every=0; arp.settings(s); CHECK(arp.state().settings.gate==100);
 }
 // The new prefix is disjoint from all previous formats, and fits extreme values.
 for(int original=5;original<=100;original+=5) for(int gate=5;gate<=100;gate+=5)
 for(int pattern=0;pattern<arp_pattern_count;++pattern) for(int every:{1,16}) {
   ArpPreset p; p.settings.gate_before_ratchet=original; p.settings.gate=gate; p.settings.ratchet_every=every;
   p.settings.pattern=arp_patterns[pattern]; p.settings.low=p.settings.high=8; p.settings.ratchet_count=4;
   p.settings.division=32; p.settings.timing=ArpTiming::Triplet; p.settings.probability=100;
   p.settings.swing=50; p.settings.block_size=7; p.bpm=300; p.selected=127; p.page=9; p.settings.clock=ArpClock::Midi;
   CHECK(unpack_arpeggiator(pack_arpeggiator(p))==p);
 }
 for(int count=2;count<=4;++count) for(int gate:{5,50,100}) for(int swing:{0,50})
 for(auto timing:{ArpTiming::Straight,ArpTiming::Dotted,ArpTiming::Triplet}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=s.high=4; s.ratchet_every=1; s.ratchet_count=count; s.gate=gate; s.swing=swing; s.timing=timing;
   arp.settings(s); arp.toggle(0); arp.toggle(2); arp.enable(true);
   const uint64_t base=timing==ArpTiming::Dotted?375000:timing==ArpTiming::Triplet?166667:250000;
   for(uint64_t t=1000000;t<1000000+4*base;t+=1000) { out.now=t; arp.tick(t); }
   CHECK(out.ons()==4*count);
   for(auto e:out.events) if(e.kind==MidiKind::NoteOn) CHECK(e.note==60 || e.note==64);
   arp.enable(false); out.balanced(); CHECK(!router.source_active(14));
 }
 { // EVERY counts slots, and repeats one pitch without advancing the melody.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=s.high=4; s.ratchet_every=4; s.ratchet_count=3; arp.settings(s);
   arp.toggle(0); arp.toggle(2); arp.enable(true);
   for(int step=0;step<8;++step) {
     const auto before=out.ons();
     for(uint64_t t=1000000+step*250000;t<1250000+step*250000;t+=1000) { out.now=t; arp.tick(t); }
     CHECK(out.ons()-before==((step+1)%4==0?3:1));
     for(size_t i=0;i<out.events.size();++i) if(out.events[i].kind==MidiKind::NoteOn && out.times[i]>=uint64_t(1000000+step*250000)) CHECK(out.events[i].note==(step%2?64:60));
   }
   arp.enable(false); out.balanced();
 }
 for(int count=2;count<=4;++count) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.clock=ArpClock::Midi; s.ratchet_every=1; s.ratchet_count=count; arp.settings(s); arp.toggle(0); arp.enable(true);
   arp.realtime(0xfa,900000); uint64_t next_clock=1000000;
   for(uint64_t t=1000000;t<1240000;t+=1000) {
     while(next_clock<=t) { arp.realtime(0xf8,next_clock); next_clock+=20833; }
     out.now=t; arp.tick(t);
   }
   CHECK(out.ons()==count); arp.realtime(0xfc,1240000); const auto stopped=out.ons();
   arp.tick(1245000); CHECK(out.ons()==stopped); out.balanced();
 }
 { // Removing the note in a gate gap cancels pending repeats as well.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.ratchet_every=1; s.ratchet_count=4; s.gate=5; arp.settings(s); arp.toggle(0); arp.enable(true);
   arp.tick(1000000); arp.tick(1004000); CHECK(arp.state().playing==-1);
   arp.toggle(0); arp.tick(1062500); arp.tick(1125000); CHECK(out.ons()==1); out.balanced();
 }
 { // Pauses stay silent, including BLOCKSIZE rests and 0% legacy probability.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.ratchet_every=1; s.ratchet_count=4; s.block_size=2; arp.settings(s); arp.toggle(0); arp.enable(true);
   for(uint64_t t=1000000;t<1750000;t+=1000) arp.tick(t);
   CHECK(out.ons()==8); arp.enable(false); out.balanced();
   s.probability=0; arp.settings(s); arp.enable(true); const auto before=out.ons();
   for(uint64_t t=2000000;t<2750000;t+=1000) arp.tick(t);
   CHECK(out.ons()==before);
 }
 { // A late worker skips missed attacks instead of sending a burst.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.ratchet_every=1; s.ratchet_count=4; s.gate=50; arp.settings(s); arp.toggle(0); arp.enable(true);
   arp.tick(1000000); arp.tick(1190000); CHECK(out.ons()==2);
   arp.tick(2000000); CHECK(out.ons()==3); arp.suspend(true); arp.tick(2062500); CHECK(out.ons()==3); out.balanced();
 }
 for(int low=0;low<=8;++low) for(int high=low;high<=8;++high)
 for(int every=0;every<=16;++every) for(int count=2;count<=4;++count) for(int page=0;page<10;++page) {
   ArpPreset p; p.settings.low=low; p.settings.high=high; p.settings.ratchet_every=every; p.settings.ratchet_count=count;
   p.settings.gate=(low*17+count)%101; p.settings.probability=(high*19+page)%101;
   p.settings.pattern=arp_patterns[page%arp_pattern_count]; p.settings.block_size=(page%6)+2; p.settings.swing=(page*7)%51;
   p.bpm=40+(every*13+page)%261; p.selected=(every*17+page)%128; p.page=page;
   CHECK(unpack_arpeggiator(pack_arpeggiator(p))==p);
   const auto slot=pack_music_slot({{}, {}, {},p}); MusicPreset decoded; CHECK(unpack_music_slot(slot,decoded) && decoded.arpeggiator==p);
 }
 const char* path="ratchet-settings.cfg"; std::filesystem::remove(path); FileSettings file(path);
 ArpPreset stored; stored.page=9; stored.settings.ratchet_every=16; stored.settings.ratchet_count=4; stored.settings.gate=50; stored.settings.gate_before_ratchet=100;
 CHECK(file.save_arpeggiator(stored)); FileSettings reopened(path); CHECK(reopened.load_arpeggiator()==stored);
 std::filesystem::remove(path);
 std::puts("Ratchet subdivision, clock, rests, cleanup, no bursts and storage passed");
}

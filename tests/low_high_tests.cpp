#include "arpeggiator.hpp"
#include "arp_storage.hpp"
#include "music_slot_storage.hpp"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
using namespace sm;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"LowHigh FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct Output:MidiTransport {
 std::vector<MidiEvent> events;
 bool send(MidiEvent e)override { events.push_back(e); return true; }
 std::vector<int> ons()const { std::vector<int> result; for(auto e:events) if(e.kind==MidiKind::NoteOn) result.push_back(e.note); return result; }
};
int main() {
 for(int root=0;root<12;++root) for(int mode=0;mode<int(modes().size());++mode)
 for(int low=0;low<=8;++low) for(int high=low;high<=8;++high) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=low; s.high=high; s.pattern=ArpPattern::LowHigh; arp.settings(s); arp.selection({root,mode});
   arp.toggle(1); arp.toggle(3); arp.enable(true);
   std::vector<int> pool;
   for(int octave=low;octave<=high;++octave) for(int degree:{1,3}) {
     const int pitch=12*(octave+1)+root+modes()[mode].intervals[degree]; if(pitch<=127) pool.push_back(pitch);
   }
   if(pool.empty()) { arp.tick(1000000); CHECK(out.ons().empty()); continue; }
   const int lowest=pool.front(),highest=lowest+12*(high-low);
   const bool playable=std::find(pool.begin(),pool.end(),highest)!=pool.end();
   for(int step=0;step<9;++step) {
     arp.tick(1000000+step*250000ull); const int pitch=arp.state().playing;
     if(step%3==0) CHECK(pitch==lowest);
     else if(step%3==1) CHECK(pitch==(playable?highest:-1));
     else {
       const auto candidates=std::count_if(pool.begin(),pool.end(),[&](int n){return n!=lowest && n!=highest;});
       if(!candidates) CHECK(pitch==-1);
       else CHECK(pitch!=lowest && pitch!=highest && std::find(pool.begin(),pool.end(),pitch)!=pool.end());
     }
   }
   arp.enable(false); CHECK(!router.source_active(14));
 }
 { // With one pitch class across three octaves, the middle octave is allowed.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=3; s.high=5; s.pattern=ArpPattern::LowHigh; arp.settings(s); arp.toggle(0); arp.enable(true);
   for(int step=0;step<30;++step) arp.tick(1000000+step*250000ull);
   const auto notes=out.ons(); CHECK(notes.size()==30);
   for(size_t i=0;i<notes.size();++i) CHECK(notes[i]==(i%3==0?48:i%3==1?72:60));
 }
 for(int high:{3,4}) { // No eligible random pitch: keep the third slot as a rest.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=3; s.high=high; s.pattern=ArpPattern::LowHigh; arp.settings(s); arp.toggle(0); arp.enable(true);
   for(int step=0;step<9;++step) { arp.tick(1000000+step*250000ull); if(step%3==2) CHECK(arp.state().playing==-1); }
   CHECK(out.ons().size()==6);
 }
 { // Live edits keep the three-step phase and select the new anchors.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=3; s.high=5; s.pattern=ArpPattern::LowHigh; arp.settings(s);
   arp.toggle(2); arp.toggle(4); arp.toggle(6); arp.enable(true); arp.tick(1000000); CHECK(arp.state().playing==52);
   arp.toggle(2); arp.tick(1250000); CHECK(arp.state().playing==79);
   arp.tick(1500000); CHECK(arp.state().playing!=55 && arp.state().playing!=79);
   s.high=6; arp.settings(s); arp.tick(1750000); CHECK(arp.state().playing==55);
   arp.tick(2000000); CHECK(arp.state().playing==91);
 }
 for(int ratchet:{0,1}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=3; s.high=5; s.pattern=ArpPattern::LowHigh; s.clock=ArpClock::Midi;
   s.ratchet_every=ratchet; s.ratchet_count=3; arp.settings(s); arp.toggle(0); arp.enable(true); arp.realtime(0xfa,900000);
   uint64_t clock=1000000;
   for(uint64_t now=1000000;now<1740000;now+=1000) {
     while(clock<=now) { arp.realtime(0xf8,clock); clock+=20833; }
     arp.tick(now);
   }
   const auto notes=out.ons(); const int repeats=ratchet?3:1;
   CHECK(notes.size()==size_t(3*repeats));
   for(int i=0;i<3*repeats;++i) CHECK(notes[i]==(i/repeats==0?48:i/repeats==1?72:60));
   arp.realtime(0xfc,1740000); CHECK(!router.source_active(14));
 }
 for(int every:{0,1,16}) for(int page=0;page<10;++page) {
   ArpPreset p; p.settings.pattern=ArpPattern::LowHigh; p.settings.ratchet_every=every; p.page=page;
   const auto packed=pack_arpeggiator(p); CHECK(unpack_arpeggiator(packed)==p);
   MusicPreset slot; slot.arpeggiator=p; MusicPreset loaded;
   CHECK(unpack_music_slot(pack_music_slot(slot),loaded) && loaded==slot);
 }
 std::puts("LOW & HIGH anchors, excluded random endpoints, live edits, clock, ratchet and storage passed");
}

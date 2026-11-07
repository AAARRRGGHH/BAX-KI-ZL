#include "arpeggiator.hpp"
#include "arp_storage.hpp"
#include "music_slot_storage.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
using namespace sm;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"Motif FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct Output:MidiTransport {
 std::vector<MidiEvent> events;
 bool send(MidiEvent e)override { events.push_back(e); return true; }
 std::vector<int> ons()const { std::vector<int> result; for(auto e:events) if(e.kind==MidiKind::NoteOn) result.push_back(e.note); return result; }
};
constexpr ArpPattern patterns[]={ArpPattern::Gravity,ArpPattern::Pivot};
int main() {
 const std::vector<int> pool{60,62,64,65,67,69,71};
 const auto rank=[&](int pitch) { const auto found=std::find(pool.begin(),pool.end(),pitch); CHECK(found!=pool.end()); return int(found-pool.begin()); };
 for(auto pattern:patterns) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=s.high=4; s.pattern=pattern; arp.settings(s);
   for(int degree=0;degree<7;++degree) arp.toggle(degree); arp.enable(true);
   for(int step=0;step<300;++step) arp.tick(1000000+step*250000ull);
   const auto notes=out.ons(); CHECK(notes.size()==300);
   if(pattern==ArpPattern::Gravity) for(int i=0;i<300;i+=3) {
     CHECK(rank(notes[i])>=2 && rank(notes[i+1])==rank(notes[i])/2 && notes[i+2]==60);
   }
   if(pattern==ArpPattern::Pivot) for(int i=0;i<298;i+=2) {
     CHECK(notes[i]!=notes[i+1]);
     if((i/2)%2==0) CHECK(notes[i]==notes[i+2] && notes[i+1]!=notes[i+3]);
     else CHECK(notes[i]!=notes[i+2] && notes[i+1]==notes[i+3]);
   }
   arp.enable(false); CHECK(!router.source_active(14));
 }
 for(auto pattern:patterns) for(int count=0;count<=3;++count) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=s.high=4; s.pattern=pattern; arp.settings(s);
   for(int i=0;i<count;++i) arp.toggle(i); arp.enable(true);
   for(int i=0;i<90;++i) { arp.tick(1000000+i*250000ull); if(count) CHECK(arp.state().playing>=0); }
   CHECK(out.ons().size()==size_t(count?90:0)); arp.enable(false); CHECK(!router.source_active(14));
 }
 for(auto pattern:patterns) for(int snapshot:{0,5,50,100}) for(int every:{0,1,16}) for(int page=0;page<10;++page) {
   if(snapshot && !every) continue;
   ArpPreset p; p.settings.pattern=pattern; p.settings.ratchet_every=every; p.settings.ratchet_count=4;
   p.settings.low=p.settings.high=8; p.settings.gate_before_ratchet=snapshot; p.settings.gate=snapshot?100:37;
   p.settings.probability=100; p.settings.swing=50; p.settings.block_size=7; p.bpm=300; p.page=page; p.selected=127;
   const auto packed=pack_arpeggiator(p); CHECK(unpack_arpeggiator(packed)==p);
   MusicPreset slot; slot.arpeggiator=p; MusicPreset loaded;
   CHECK(unpack_music_slot(pack_music_slot(slot),loaded) && loaded==slot);
 }
 for(auto removed:{ArpPattern::Mirror,ArpPattern::OctaveEcho}) for(int snapshot:{0,100}) {
   ArpPreset old; old.settings.pattern=removed;
   if(snapshot) { old.settings.ratchet_every=4; old.settings.gate=50; old.settings.gate_before_ratchet=snapshot; }
   CHECK(unpack_arpeggiator(pack_arpeggiator(old))==old);
   MusicPreset slot; slot.arpeggiator=old; MusicPreset decoded;
   CHECK(unpack_music_slot(pack_music_slot(slot),decoded) && decoded==slot);
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   CHECK(arp.restore(decoded.arpeggiator)); CHECK(arp.state().settings.pattern==ArpPattern::Up);
 }
 CHECK(arp_pattern_count==10);
 CHECK(arp_patterns[8]==ArpPattern::Gravity && arp_patterns[9]==ArpPattern::Pivot);
 std::puts("Gravity/Pivot rules, no rests, sparse pools, stable IDs and removed-pattern migration passed");
}

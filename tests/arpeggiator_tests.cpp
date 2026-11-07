#include "arpeggiator.hpp"
#include "ble_midi_receive.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
using namespace sm;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"Arp FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct Output:MidiTransport {
 std::vector<MidiEvent> events;
 bool send(MidiEvent e) override { events.push_back(e); return true; }
 std::vector<int> ons() const { std::vector<int> r; for(auto e:events) if(e.kind==MidiKind::NoteOn) r.push_back(e.note); return r; }
};
int main() {
 CHECK(ArpSettings{}.gate==100);
 for(int gate:{0,1,25,50,75,100}) for(int swing:{0,50})
 for(auto timing:{ArpTiming::Straight,ArpTiming::Dotted,ArpTiming::Triplet}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=s.high=4; s.gate=gate; s.swing=swing; s.timing=timing; arp.settings(s);
   arp.toggle(0); arp.toggle(2); arp.enable(true);
   uint64_t base=timing==ArpTiming::Dotted?375000:timing==ArpTiming::Triplet?166667:250000;
   const uint64_t length=(base*(100+swing)+50)/100,short_length=2*base-length;
   arp.tick(1000000);
   if(!gate) {
     CHECK(out.events.empty()); arp.tick(1000000+length); CHECK(out.events.empty());
     s.gate=100; arp.settings(s); arp.tick(1000000+2*base); CHECK(out.ons()==std::vector<int>{60});
   } else {
     const auto deadline=std::max<uint64_t>(1000,length*gate/100);
     arp.tick(1000000+deadline-1); CHECK(out.events.size()==1);
     arp.tick(1000000+deadline); CHECK(out.events[1].kind==MidiKind::NoteOff);
     arp.tick(1000000+length); CHECK(out.ons()==(std::vector<int>{60,64}));
     const auto short_gate=std::max<uint64_t>(1000,short_length*gate/100);
     arp.tick(1000000+length+short_gate-1); CHECK(arp.state().playing==64);
     arp.tick(1000000+length+short_gate); CHECK(out.events[3].kind==MidiKind::NoteOff);
   }
   arp.enable(false); CHECK(!router.source_active(14));
 }
 { // Live gate edits leave the current note and sequence untouched.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.low=s.high=4; arp.settings(s); arp.toggle(0); arp.toggle(2); arp.enable(true); arp.tick(1000000);
   s.gate=50; arp.settings(s); arp.tick(1249999); CHECK(out.events.size()==1);
   arp.tick(1250000); CHECK(out.ons()==(std::vector<int>{60,64}));
   arp.tick(1374999); CHECK(arp.state().playing==64);
   arp.tick(1375000); CHECK(arp.state().playing==-1);
 }
 { // External MIDI clock uses the same percentage; Stop releases a 100% note.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s;
   s.clock=ArpClock::Midi; s.gate=50; arp.settings(s); arp.toggle(0); arp.enable(true);
   arp.realtime(0xfa,900000); arp.realtime(0xf8,1000000); arp.tick(1000000);
   for(int i=1;i<=6;++i) arp.realtime(0xf8,1000000+i*20833ull);
   arp.tick(1124999); CHECK(out.events.size()==1);
   arp.tick(1125000); CHECK(out.events.size()==2 && out.events.back().kind==MidiKind::NoteOff);
   s.gate=100; arp.settings(s);
   for(int i=7;i<=12;++i) { arp.realtime(0xf8,1000000+i*20833ull); arp.tick(1000000+i*20833ull); }
   CHECK(out.ons().size()==2); arp.realtime(0xfc,1300000); CHECK(!router.source_active(14));
 }
 { // The shared clock tempo is measured even while arpeggiator playback is off.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.clock=ArpClock::Midi; arp.settings(s);
   CHECK(arp.clock_quarter_us()==500000);
   for(int i=0;i<80;++i) arp.realtime(0xf8,1000000+i*41667ull); // 60 BPM.
   CHECK(arp.clock_quarter_us()>999000 && arp.clock_quarter_us()<1001000);
   const auto last=arp.clock_quarter_us(); arp.disconnected(); CHECK(arp.clock_quarter_us()==last);
 }
 for(int block=2;block<=7;++block) for(auto clock:{ArpClock::Tap,ArpClock::Midi}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75;
   s.low=s.high=4; s.block_size=block; s.clock=clock; arp.settings(s);
   arp.toggle(0); arp.toggle(1); arp.toggle(2); arp.enable(true);
   if(clock==ArpClock::Midi) arp.realtime(0xfa,1000000);
   int played=0;
   for(int step=0;step<24;++step) {
     const auto time=1000000+step*250000ull;
     if(clock==ArpClock::Midi) {
       const int first=step?1:0;
       for(int pulse=first;pulse<=12;++pulse) {
         if(!step && pulse) break;
         const auto tick=step?time-250000+pulse*20833ull:time;
         arp.realtime(0xf8,tick); arp.tick(tick);
       }
     } else arp.tick(time);
     const bool rest=step%(block+1)==block;
     if(!rest) ++played;
     CHECK(int(out.ons().size())==played);
     if(!rest) { const int pitches[]={60,62,64}; CHECK(out.ons().back()==pitches[step%3]); }
   }
   s.block_size=1; arp.settings(s); CHECK(arp.state().settings.block_size==block);
   s.block_size=8; arp.settings(s); CHECK(arp.state().settings.block_size==block);
   arp.reset_defaults(); CHECK(arp.state().settings.block_size==0);
 }
 for(auto clock:{ArpClock::Tap,ArpClock::Midi}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   arp.selection({1,blues_mode});
   ArpSettings dirty; dirty.low=2; dirty.high=7; dirty.division=32; dirty.probability=100;
   dirty.swing=35; dirty.pattern=ArpPattern::Anchor; dirty.timing=ArpTiming::Dotted; dirty.clock=clock;
   arp.settings(dirty); arp.tempo(207); arp.tap(900000); arp.toggle(0); arp.enable(true);
   if(clock==ArpClock::Midi) { arp.realtime(0xfa,1000000); arp.realtime(0xf8,1000000); }
   arp.tick(1000000); CHECK(router.source_active(14));
   dirty.probability=23; arp.settings(dirty);
   const auto before=arp.state(); arp.reset_defaults(); const auto state=arp.state();
   CHECK(state.enabled && !state.suspended && state.running==before.running);
   CHECK(state.selected==0 && state.playing==-1 && !router.source_active(14));
   CHECK(state.settings.low==3 && state.settings.high==5 && state.settings.division==8);
   CHECK(state.settings.pattern==ArpPattern::Up && state.settings.timing==ArpTiming::Straight);
   CHECK(state.settings.probability==100 && state.settings.swing==0 && state.bpm==120);
   CHECK(state.settings.clock==clock && state.clock_present==before.clock_present);
   arp.tap(1020000); CHECK(arp.state().bpm==120); // No stale tap-history estimate.
   const auto count=out.ons().size(); arp.tick(1030000); CHECK(out.ons().size()==count);
   arp.toggle(0);
   if(clock==ArpClock::Midi) arp.realtime(0xf8,1050000);
   arp.tick(1050000); CHECK(out.ons().back()==49); // Same key/scale, default octave 3.
 }
 { // Blues has six playable degrees, including its passing flattened fifth.
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   arp.selection({0,blues_mode}); ArpSettings settings; settings.gate=75; settings.low=settings.high=4; arp.settings(settings);
   for(int degree=0;degree<6;++degree) arp.toggle(degree);
   arp.enable(true);
   for(int step=0;step<7;++step) arp.tick(1000000+uint64_t(step)*250000);
   CHECK((out.ons()==std::vector<int>{60,63,65,66,67,70,60}));
 }
 for(int from=0;from<arp_pattern_count;++from) for(int to=0;to<arp_pattern_count;++to)
 for(auto clock:{ArpClock::Tap,ArpClock::Midi}) for(int swing:{0,50}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75;
   s.low=s.high=4; s.pattern=arp_patterns[from]; s.clock=clock; s.swing=swing; arp.settings(s);
   for(int degree:{0,2,4}) arp.toggle(degree); arp.enable(true);
   if(clock==ArpClock::Midi) { arp.realtime(0xfa,900000); arp.realtime(0xf8,1000000); }
   arp.tick(1000000); CHECK(out.ons().size()==1); const int playing=arp.state().playing;
   s.pattern=arp_patterns[to]; arp.settings(s); CHECK(arp.state().settings.pattern==arp_patterns[to]);
   CHECK(out.events.size()==1 && arp.state().playing==playing);
   const uint64_t length=swing?375000:250000,gate=length*3/4;
   // Feed clock without ticking the sequencer, then check the exact gate deadline.
   if(clock==ArpClock::Midi) for(int i=1;i*20833ull<=gate;++i) arp.realtime(0xf8,1000000+i*20833ull);
   arp.tick(1000000+gate-1); CHECK(out.events.size()==1);
   arp.tick(1000000+gate); CHECK(out.events.size()==2 && out.events.back().kind==MidiKind::NoteOff);
   if(clock==ArpClock::Tap) {
     arp.tick(1000000+length-1); CHECK(out.ons().size()==1); arp.tick(1000000+length);
   } else {
     const int clocks=swing?18:12;
     for(int i=int(gate/20833)+1;i<=clocks;++i) { auto t=1000000+i*20833ull; arp.realtime(0xf8,t); arp.tick(t); }
     // Exact clock boundaries are used; their wall-time estimate may be a few us early.
   }
   const auto notes=out.ons(); CHECK(notes.size()==2);
   CHECK(notes.back()==60 || notes.back()==64 || notes.back()==67);
   if(from!=to) {
     if(to==int(ArpPattern::Down)) CHECK(notes.back()==67);
     if(to==int(ArpPattern::Up) || to==int(ArpPattern::UpDown) || to==int(ArpPattern::Converge) || to==int(ArpPattern::Anchor)) CHECK(notes.back()==60);
   }
   // A pattern change must not restart the long/short Swing pair.
   if(clock==ArpClock::Tap) { arp.tick(1499999); CHECK(out.ons().size()==2); arp.tick(1500000); CHECK(out.ons().size()==3); }
   arp.enable(false); CHECK(!router.source_active(14));
 }
 { // Only the final choice before the next step is adopted, even with octave edits.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; arp.settings(s);
   arp.toggle(0); arp.toggle(2); arp.enable(true); arp.tick(1000000);
   s.pattern=ArpPattern::Down; arp.settings(s); s.pattern=ArpPattern::Anchor; arp.settings(s);
   s.high=5; arp.settings(s); CHECK(out.events.size()==1);
   arp.tick(1250000); CHECK(out.ons().back()==60);
   arp.tick(1500000); CHECK(out.ons().back()!=60);
 }
 { // Scrolling back to the running pattern before the boundary cancels the change.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; arp.settings(s);
   arp.toggle(0); arp.toggle(2); arp.toggle(4); arp.enable(true); arp.tick(1000000);
   s.pattern=ArpPattern::Down; arp.settings(s); s.pattern=ArpPattern::Up; arp.settings(s);
   arp.tick(1250000); CHECK(out.ons().back()==64);
 }
 { // Extending the octave range preserves the gate and includes the new octave.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; arp.settings(s);
   arp.toggle(0); arp.enable(true); arp.tick(1000000);
   s.high=5; arp.settings(s); CHECK(out.events.size()==1 && arp.state().playing==60);
   arp.tick(1187499); CHECK(out.events.size()==1);
   arp.tick(1187500); CHECK(out.events.back().kind==MidiKind::NoteOff);
   arp.tick(1249999); CHECK(out.ons().size()==1); arp.tick(1250000); CHECK(out.ons().back()==72);
   s.high=4; arp.settings(s); CHECK(out.events.back().kind==MidiKind::NoteOff && out.events.back().note==72);
   arp.tick(1499999); CHECK(out.ons().size()==2); arp.tick(1500000); CHECK(out.ons().back()==60);
 }
 { // Changing the lower bound preserves ANCHOR's anchor/free alternation.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; s.pattern=ArpPattern::Anchor; arp.settings(s);
   arp.toggle(0); arp.toggle(4); arp.enable(true); arp.tick(1000000);
   s.low=3; arp.settings(s); CHECK(out.events.size()==1);
   arp.tick(1250000); CHECK(out.ons().back()!=48);
   arp.tick(1500000); CHECK(out.ons().back()==48);
 }
 for(int pattern=0;pattern<arp_pattern_count;++pattern) for(auto clock:{ArpClock::Tap,ArpClock::Midi}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75;
   s.low=s.high=4; s.swing=50; s.pattern=arp_patterns[pattern]; s.clock=clock; arp.settings(s);
   arp.toggle(0); arp.toggle(2); arp.enable(true);
   if(clock==ArpClock::Midi) arp.realtime(0xfa,900000);
   // Sample at 20 ms; external clock gives the same 120 BPM as internal mode.
   for(int i=0;i<=48;++i) {
     const uint64_t time=1000000+i*20833ull;
     if(i==3) { s.high=6; arp.settings(s); }
     if(i==20) { s.low=5; s.high=5; arp.settings(s); }
     if(i==30) { s.low=8; s.high=8; arp.selection({11,0}); arp.settings(s); }
     if(clock==ArpClock::Midi) arp.realtime(0xf8,time);
     arp.tick(time);
     if(i<18) CHECK(out.ons().size()==1);
     if(i>=19 && i<24) CHECK(out.ons().size()==2);
     if(i>=25 && i<42) CHECK(out.ons().size()==3);
     if(i>=43 && i<48) CHECK(out.ons().size()==4);
     if(clock==ArpClock::Midi) CHECK(arp.state().running);
   }
   arp.enable(false); CHECK(!router.source_active(14));
   int held=-1;
   for(auto e:out.events) {
     if(e.kind==MidiKind::NoteOn) { CHECK(held==-1 && e.note<=127); held=e.note; }
     if(e.kind==MidiKind::NoteOff) { CHECK(held==e.note); held=-1; }
   }
   CHECK(held==-1);
 }
 { // Live insertion preserves the gate and deadline; removal releases only that pitch.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; arp.settings(s);
   arp.toggle(0); arp.toggle(4); arp.enable(true); arp.tick(1000000);
   arp.toggle(2); CHECK(out.events.size()==1 && arp.state().playing==60);
   arp.tick(1187499); CHECK(out.events.size()==1);
   arp.tick(1187500); CHECK(out.events.back().kind==MidiKind::NoteOff);
   arp.tick(1249999); CHECK(out.ons().size()==1);
   arp.tick(1250000); CHECK(out.ons().back()==64);
   arp.toggle(2); CHECK(out.events.back().kind==MidiKind::NoteOff && out.events.back().note==64);
   arp.tick(1499999); CHECK(out.ons().size()==2);
   arp.tick(1500000); CHECK((out.ons()==std::vector<int>{60,64,67}));
 }
 for(auto pattern:{ArpPattern::Down,ArpPattern::UpDown}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; s.pattern=pattern; arp.settings(s);
   arp.toggle(0); arp.toggle(4); arp.enable(true);
   arp.tick(1000000);
   if(pattern==ArpPattern::UpDown) arp.tick(1250000); // Now descending from G.
   arp.toggle(2);
   const uint64_t next=pattern==ArpPattern::Down?1250000:1500000;
   arp.tick(next-1); const auto count=out.ons().size();
   arp.tick(next); CHECK(out.ons().size()==count+1 && out.ons().back()==64);
   arp.toggle(0); arp.tick(next+250000); CHECK(out.ons().back()==67);
 }
 { // ANCHOR keeps its alternating anchor/free phase across live edits.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; s.pattern=ArpPattern::Anchor; arp.settings(s);
   arp.toggle(0); arp.toggle(4); arp.enable(true); arp.tick(1000000); arp.toggle(2);
   arp.tick(1250000); CHECK(out.ons().back()!=60);
   arp.tick(1500000); CHECK(out.ons().back()==60);
 }
 { // SHUFFLE preserves unplayed survivors and inserts new pitches once.
   Output original,edited; MidiRouter a(original),b(edited); Arpeggiator reference(a),arp(b);
   ArpSettings s; s.gate=75; s.low=s.high=4; s.pattern=ArpPattern::Shuffle; reference.settings(s); arp.settings(s);
   for(int i=0;i<6;++i) { reference.toggle(i); arp.toggle(i); }
   reference.enable(true); arp.enable(true);
   for(int i=0;i<6;++i) reference.tick(1000000+i*250000ull);
   arp.tick(1000000); arp.tick(1250000);
   const auto order=original.ons(); const int removed=order[3];
   const int pitches[]={60,62,64,65,67,69};
   const int degree=int(std::find(std::begin(pitches),std::end(pitches),removed)-std::begin(pitches));
   arp.toggle(degree); arp.toggle(6);
   for(int i=2;i<6;++i) arp.tick(1000000+i*250000ull);
   const auto notes=edited.ons(); CHECK(notes.size()==6);
   std::vector<int> expected,actual;
   for(int i=2;i<6;++i) if(order[i]!=removed) expected.push_back(order[i]);
   int added=0;
   for(int i=2;i<6;++i) { if(notes[i]==71) ++added; else actual.push_back(notes[i]); }
   CHECK(added==1 && actual==expected);
 }
 { // An empty selection keeps the swung grid; rejoining waits for its next step.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; s.swing=50; arp.settings(s);
   arp.toggle(0); arp.enable(true); arp.tick(1000000); arp.clear();
   CHECK(out.events.back().kind==MidiKind::NoteOff);
   arp.tick(1375000); arp.toggle(2); arp.tick(1499999); CHECK(out.ons().size()==1);
   arp.tick(1500000); CHECK(out.ons().back()==64 && out.ons().size()==2);
   arp.tick(1874999); CHECK(out.ons().size()==2); arp.tick(1875000); CHECK(out.ons().size()==3);
 }
 { // Live edits and empty steps preserve external MIDI transport and phase.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; s.clock=ArpClock::Midi; arp.settings(s);
   arp.toggle(0); arp.toggle(4); arp.enable(true); arp.realtime(0xfa,1000000);
   for(int i=0;i<=24;++i) {
     if(i==3) arp.toggle(2);
     if(i==13) arp.clear();
     if(i==20) arp.toggle(4);
     const auto time=1100000+i*20833ull; arp.realtime(0xf8,time); arp.tick(time);
     CHECK(arp.state().running);
     if(i<12) CHECK(out.ons().size()==1);
     else if(i<24) CHECK(out.ons().size()==2);
   }
   CHECK((out.ons()==std::vector<int>{60,64,67}));
 }
 for(int pattern=0;pattern<arp_pattern_count;++pattern) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.pattern=arp_patterns[pattern]; arp.settings(s);
   arp.toggle(0); arp.enable(true);
   for(int i=0;i<300;++i) {
     const auto time=1000000+i*250000ull;
     arp.tick(time); const auto before=out.ons().size();
     arp.toggle(i%7); if(i%13==0) arp.clear();
     arp.tick(time+1000); CHECK(out.ons().size()==before);
     for(auto e:out.events) if(e.kind==MidiKind::NoteOn) CHECK(e.note<=127);
   }
   arp.enable(false); CHECK(!router.source_active(14));
   int held=-1;
   for(auto e:out.events) {
     if(e.kind==MidiKind::NoteOn) { CHECK(held==-1); held=e.note; }
     if(e.kind==MidiKind::NoteOff) { CHECK(held==e.note); held=-1; }
   }
   CHECK(held==-1);
 }
 for(int division:{1,2,4,8,16,32}) for(auto timing:{ArpTiming::Straight,ArpTiming::Dotted,ArpTiming::Triplet}) for(int swing:{0,25,50}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings settings; settings.gate=75;
   settings.division=division; settings.timing=timing; settings.swing=swing; arp.settings(settings); arp.toggle(0); arp.enable(true);
   int units=768/division;
   if(timing==ArpTiming::Dotted) units=units*3/2; if(timing==ArpTiming::Triplet) units=units*2/3;
   const uint64_t base=uint64_t(std::llround(500000.0*units/192)),long_step=(base*(100+swing)+50)/100,short_step=2*base-long_step;
   arp.tick(1000000); CHECK(out.ons().size()==1);
   arp.tick(1000000+long_step*3/4); CHECK(out.events.back().kind==MidiKind::NoteOff);
   arp.tick(1000000+long_step-1); CHECK(out.ons().size()==1);
   arp.tick(1000000+long_step); CHECK(out.ons().size()==2);
   arp.tick(1000000+long_step+short_step*3/4); CHECK(out.events.back().kind==MidiKind::NoteOff);
   arp.tick(1000000+2*base-1); CHECK(out.ons().size()==2);
   arp.tick(1000000+2*base); CHECK(out.ons().size()==3);
 }
 { // Swing may fall between incoming clocks (32nds at 25% = 3.75 clocks).
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings settings; settings.gate=75;
   settings.clock=ArpClock::Midi; settings.division=32; settings.swing=25; arp.settings(settings);
   for(int i=0;i<100;++i) arp.realtime(0xf8,100000+i*20000ull);
   arp.toggle(0); arp.enable(true); arp.realtime(0xfa,4990000);
   for(int i=0;i<=3;++i) { arp.realtime(0xf8,5000000+i*20000ull); arp.tick(5000000+i*20000ull); }
   arp.tick(5074900); CHECK(out.ons().size()==1); arp.tick(5075100); CHECK(out.ons().size()==2);
   for(int i=4;i<=6;++i) { arp.realtime(0xf8,5000000+i*20000ull); arp.tick(5000000+i*20000ull); }
   CHECK(out.ons().size()==3);
 }
 { // Swing edits wait for the next pair and survive a key change.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings settings; settings.gate=75;
   CHECK(arp.state().settings.swing==0); arp.toggle(0); arp.enable(true); arp.tick(1000000);
   settings.swing=-1; arp.settings(settings); CHECK(arp.state().settings.swing==0);
   settings.swing=51; arp.settings(settings); CHECK(arp.state().settings.swing==0);
   settings.swing=50; arp.settings(settings); arp.selection({2,1});
   arp.tick(1249999); CHECK(out.ons().size()==1); arp.tick(1250000); CHECK(out.ons().size()==2);
   arp.tick(1500000); CHECK(out.ons().size()==3); arp.tick(1874999); CHECK(out.ons().size()==3);
   arp.tick(1875000); CHECK(out.ons().size()==4); arp.tick(2000000); CHECK(out.ons().size()==5);
 }
 { // External 24-PPQN reference stays straight; only the note grid swings.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings settings; settings.gate=75; settings.clock=ArpClock::Midi; settings.swing=50;
   arp.settings(settings); arp.toggle(0); arp.enable(true); arp.realtime(0xfa,1000000);
   for(int i=0;i<18;++i) { auto time=1100000+i*20000ull; arp.realtime(0xf8,time); arp.tick(time); }
   CHECK(out.ons().size()==1);
   arp.realtime(0xf8,1460000); arp.tick(1460000); CHECK(out.ons().size()==2);
   for(int i=19;i<24;++i) { auto time=1100000+i*20000ull; arp.realtime(0xf8,time); arp.tick(time); }
   CHECK(out.ons().size()==2); arp.realtime(0xf8,1580000); arp.tick(1580000); CHECK(out.ons().size()==3);
   arp.realtime(0xfc,1590000); CHECK(out.events.back().kind==MidiKind::NoteOff);
 }
 {
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   CHECK(arp.state().bpm==120); arp.tempo(40); CHECK(arp.state().bpm==40); arp.tempo(300); CHECK(arp.state().bpm==300);
   arp.tempo(39); CHECK(arp.state().bpm==300); arp.tempo(301); CHECK(arp.state().bpm==300);
   arp.tap(1000000); arp.tap(1500000); CHECK(arp.state().bpm==120);
   arp.tempo(80); arp.tap(2000000); arp.tap(3500000); CHECK(arp.state().bpm==40);
   arp.tempo(120); arp.tap(4000000); arp.tap(4200000); CHECK(arp.state().bpm==300);
 }
 { // Tapping is a tempo edit, never a pattern restart or an early Note Off.
   for(int pattern=0;pattern<arp_pattern_count;++pattern) for(int swing:{0,50}) {
     Output tapped_out,wheel_out; MidiRouter tapped_router(tapped_out),wheel_router(wheel_out);
     Arpeggiator tapped(tapped_router),wheel(wheel_router);
     ArpSettings settings; settings.gate=75; settings.low=settings.high=4; settings.pattern=arp_patterns[pattern]; settings.swing=swing;
     for(auto* arp:{&tapped,&wheel}) {
       arp->settings(settings); arp->toggle(0); arp->toggle(2); arp->toggle(4); arp->enable(true);
     }
     for(uint64_t time=1000000;time<=2000000;time+=1000) {
       if(time==1050000 || time==1400000) {
         const auto event_count=tapped_out.events.size(); const int held=tapped.state().playing;
         CHECK(held>=0);
         tapped.tap(time);
         CHECK(tapped_out.events.size()==event_count && tapped.state().playing==held);
         if(time==1400000) { CHECK(tapped.state().bpm==171); wheel.tempo(171); }
       }
       tapped.tick(time); wheel.tick(time);
       CHECK(tapped.state().playing==wheel.state().playing);
       CHECK(tapped_out.events.size()==wheel_out.events.size());
       CHECK(tapped_out.ons()==wheel_out.ons());
     }
     tapped.enable(false); wheel.enable(false);
     CHECK(!tapped_router.source_active(14) && !wheel_router.source_active(14));
   }
 }
 { // A stray TAP must not restart transport or cut a MIDI-clocked note either.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings settings; settings.gate=75; settings.clock=ArpClock::Midi;
   arp.settings(settings); arp.toggle(0); arp.enable(true); arp.realtime(0xfa,1000000);
   arp.realtime(0xf8,1000000); arp.tick(1000000); const auto count=out.events.size();
   arp.tap(1050000); CHECK(out.events.size()==count && arp.state().playing==48 && arp.state().running);
   arp.tick(1050000); CHECK(out.events.size()==count);
 }
 { // Retry a rejected Note Off even after the sequencer is stopped.
   struct Failing:MidiTransport { bool fail=false; int offs=0; bool send(MidiEvent e)override { if(e.kind==MidiKind::NoteOff) { if(fail) return false; ++offs; } return true; } } out;
   MidiRouter router(out); Arpeggiator arp(router); arp.toggle(0); arp.enable(true); arp.tick(1000000);
   out.fail=true; arp.enable(false); CHECK(router.source_active(14));
   out.fail=false; arp.tick(1000001); CHECK(!router.source_active(14)); CHECK(out.offs==1);
 }
 for(auto pattern:{ArpPattern::Up,ArpPattern::Down,ArpPattern::UpDown,ArpPattern::Random}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   ArpSettings s; s.gate=75; s.low=4; s.high=6; s.pattern=pattern; arp.settings(s); arp.enable(true); arp.toggle(0); arp.toggle(2);
   const int up[]={60,64,72,76,84,88,60,64,72,76,84,88};
   const int down[]={88,84,76,72,64,60,88,84,76,72,64,60};
   const int both[]={60,64,72,76,84,88,84,76,72,64,60,64};
   for(int i=0;i<12;++i) arp.tick(1000000+250000ull*i);
   auto notes=out.ons(); CHECK(notes.size()==12);
   for(int i=0;i<12;++i) {
     if(pattern==ArpPattern::Up) CHECK(notes[i]==up[i]);
     if(pattern==ArpPattern::Down) CHECK(notes[i]==down[i]);
     if(pattern==ArpPattern::UpDown) CHECK(notes[i]==both[i]);
     CHECK(notes[i]==60 || notes[i]==64 || notes[i]==72 || notes[i]==76 || notes[i]==84 || notes[i]==88);
   }
   arp.enable(false); CHECK(out.events.back().kind==MidiKind::NoteOff);
 }
 for(auto pattern:{ArpPattern::Shuffle,ArpPattern::Converge}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=4; s.high=6; s.pattern=pattern;
   arp.settings(s); arp.toggle(0); arp.toggle(2); arp.enable(true);
   const std::vector<int> pool{60,64,72,76,84,88};
   const int converge[]={60,88,64,84,72,76};
   for(int i=0;i<120;++i) arp.tick(1000000+i*250000ull);
   const auto notes=out.ons(); CHECK(notes.size()==120);
   for(size_t i=0;i<notes.size();++i) {
     auto current=std::find(pool.begin(),pool.end(),notes[i]); CHECK(current!=pool.end());
     if(pattern==ArpPattern::Converge) CHECK(notes[i]==converge[i%6]);
     if(pattern==ArpPattern::Shuffle) {
       for(size_t j=i/6*6;j<i;++j) CHECK(notes[i]!=notes[j]);
       if(i && i%6==0) CHECK(notes[i]!=notes[i-1]);
     }
   }
 }
 {
   const auto pattern=ArpPattern::Anchor;
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings settings; settings.gate=75; settings.low=4; settings.high=6; settings.pattern=pattern; arp.settings(settings);
   arp.toggle(0); arp.toggle(2); arp.enable(true);
   for(int i=0;i<120;++i) arp.tick(1000000+i*250000ull);
   const auto notes=out.ons(); const std::vector<int> pool{60,64,72,76,84,88}; CHECK(notes.size()==120);
   for(size_t i=0;i<notes.size();++i) {
     CHECK(std::find(pool.begin(),pool.end(),notes[i])!=pool.end());
     if(i%2==0) CHECK(notes[i]==60); else CHECK(notes[i]!=60);
   }
 }
 for(int pattern=0;pattern<arp_pattern_count;++pattern) {
   Output original,changed; MidiRouter a(original),b(changed); Arpeggiator reference(a),arp(b);
   ArpSettings settings; settings.gate=75; settings.low=4; settings.high=6; settings.pattern=arp_patterns[pattern]; reference.settings(settings); arp.settings(settings);
   reference.toggle(0); reference.toggle(2); arp.toggle(0); arp.toggle(2); reference.enable(true); arp.enable(true);
   const std::vector<int> old_pool{60,64,72,76,84,88},new_pool{62,65,74,77,86,89};
   for(int i=0;i<30;++i) {
     const uint64_t time=1000000+i*250000ull;
     if(i==5) { arp.selection({2,1}); arp.tick(time-1); CHECK(changed.ons().size()==5); }
     reference.tick(time); arp.tick(time);
   }
   const auto before=original.ons(),after=changed.ons(); CHECK(before.size()==after.size());
   for(size_t i=0;i<after.size();++i) {
     const int position=int(std::find(old_pool.begin(),old_pool.end(),before[i])-old_pool.begin()); CHECK(position<6);
     CHECK(after[i]==(i<5?old_pool[position]:new_pool[position]));
   }
 }
 for(int pattern=0;pattern<arp_pattern_count;++pattern) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings settings; settings.gate=75;
   settings.pattern=arp_patterns[pattern]; settings.low=settings.high=4; arp.settings(settings);
   for(int degree:{0,3,5,6}) arp.toggle(degree); arp.enable(true);
   for(int i=0;i<80;++i) {
     const bool pentatonic=(i/10)%2!=0;
     if(i%10==0) arp.selection({0,pentatonic?9:0});
     arp.tick(1000000+i*250000ull); CHECK(arp.state().selected==105);
     const auto notes=out.ons(); CHECK(notes.size()==size_t(i+1)); const int note=notes.back();
     if(pentatonic) CHECK(note==60 || note==67); else CHECK(note==60 || note==65 || note==69 || note==71);
   }
 }
 for(int pattern=0;pattern<arp_pattern_count;++pattern) for(int count=0;count<=3;++count) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; s.pattern=arp_patterns[pattern]; arp.settings(s);
   for(int i=0;i<count;++i) arp.toggle(i); arp.enable(true);
   for(int i=0;i<100;++i) arp.tick(1000000+250000ull*i);
   auto notes=out.ons(); CHECK(notes.size()==(count?(pattern==int(ArpPattern::LowHigh) && count==1?67:100):0));
   for(int pitch:notes) CHECK(pitch==60 || (count>=2 && pitch==62) || (count>=3 && pitch==64));
 }
 { // Degrees 1/4/6/7 survive seven/five-note changes without resetting the beat.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; arp.settings(s);
   for(int degree:{0,3,5,6}) arp.toggle(degree); arp.enable(true);
   const uint16_t mask=(1<<0)|(1<<3)|(1<<5)|(1<<6);
   arp.tick(1000000); arp.tick(1250000); CHECK((out.ons()==std::vector<int>{60,65}));
   arp.selection({2,1}); CHECK(arp.state().selected==mask); CHECK(out.events.back().kind==MidiKind::NoteOff);
   arp.tick(1400000); CHECK(out.ons().size()==2);
   arp.tick(1500000); CHECK(out.ons().back()==70);
   arp.selection({2,9}); CHECK(arp.state().selected==mask);
   arp.tick(1749999); CHECK(out.ons().size()==3); arp.tick(1750000); CHECK(out.ons().back()==69);
   arp.selection({2,1}); CHECK(arp.state().selected==mask);
   for(int i=0;i<4;++i) arp.tick(2000000+250000ull*i);
   auto notes=out.ons(); CHECK(notes[4]==70 && notes[5]==72 && notes[6]==62 && notes[7]==67);
 }
 { // Up/down retains its descending direction.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; s.pattern=ArpPattern::UpDown; arp.settings(s);
   for(int degree:{0,3,5,6}) arp.toggle(degree); arp.enable(true);
   for(int i=0;i<4;++i) arp.tick(1000000+i*250000ull);
   arp.selection({2,1}); arp.tick(2000000); arp.tick(2250000); CHECK(out.ons()[4]==70 && out.ons()[5]==67);
 }
 { // Clock phase and Start state survive selection edits.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ArpSettings s; s.gate=75; s.low=s.high=4; s.clock=ArpClock::Midi; arp.settings(s);
   arp.toggle(0); arp.toggle(3); arp.enable(true); arp.realtime(0xfa,1000000);
   for(int i=0;i<4;++i) { auto t=1100000+i*20833ull; arp.realtime(0xf8,t); arp.tick(t); }
   arp.selection({2,1}); CHECK(arp.state().running);
   for(int i=4;i<12;++i) { auto t=1100000+i*20833ull; arp.realtime(0xf8,t); arp.tick(t); }
   CHECK(out.ons().size()==1); arp.realtime(0xf8,1100000+12*20833ull); arp.tick(1100000+12*20833ull);
   CHECK((out.ons()==std::vector<int>{60,67}));
 }
 { // Hidden degrees return when the scale includes them again.
   Output out; MidiRouter router(out); Arpeggiator arp(router); arp.toggle(5); arp.toggle(6); arp.enable(true); arp.tick(1000000);
   arp.selection({0,9}); auto count=out.ons().size(); arp.tick(1500000); CHECK(out.ons().size()==count);
   CHECK(arp.state().selected==((1<<5)|(1<<6))); arp.selection({0,0}); arp.tick(1750000); CHECK(out.ons().size()==count+1);
 }
 for(int division:{1,2,4,8,16,32}) for(auto timing:{ArpTiming::Straight,ArpTiming::Dotted,ArpTiming::Triplet}) {
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   ArpSettings s; s.gate=75; s.division=division; s.timing=timing; arp.settings(s); arp.toggle(0); arp.enable(true);
   int units=768/division;
   if(timing==ArpTiming::Dotted) units=units*3/2;
   if(timing==ArpTiming::Triplet) units=units*2/3;
   auto length=uint64_t(std::llround(500000.0*units/192));
   arp.tick(1000000); CHECK(out.ons().size()==1);
   arp.tick(1000000+length*3/4); CHECK(out.events.back().kind==MidiKind::NoteOff);
   arp.tick(1000000+length-1); CHECK(out.ons().size()==1);
   arp.tick(1000000+length); CHECK(out.ons().size()==2);
   arp.tick(100000000); CHECK(out.ons().size()==3);
 }
 { // Cross into octave 9, omit only notes beyond MIDI 127.
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   arp.selection({11,1}); ArpSettings s; s.gate=75; s.low=8; s.high=8; arp.settings(s);
   for(int i=0;i<7;++i) arp.toggle(i); arp.enable(true);
   for(int i=0;i<9;++i) arp.tick(1000000+i*250000ull);
   CHECK((out.ons()==std::vector<int>{119,121,122,124,126,127,119,121,122}));
   arp.clear(); CHECK(out.events.back().kind==MidiKind::NoteOff);
   auto size=out.ons().size(); arp.tick(9000000); CHECK(out.ons().size()==size);
 }
 { // A skipped step advances the pattern without emitting a Note On.
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   ArpSettings s; s.gate=75; s.low=s.high=4; s.probability=0; arp.settings(s); arp.toggle(0); arp.toggle(2); arp.enable(true);
   arp.tick(1000000); CHECK(out.ons().empty());
   s.probability=100; arp.settings(s); arp.tick(1250000); CHECK(out.ons().back()==64);
   s.probability=50; arp.settings(s); out.events.clear();
   for(int i=0;i<1000;++i) arp.tick(1500000+250000ull*i);
   CHECK(out.ons().size()>400 && out.ons().size()<600);
   s.probability=0; arp.settings(s); out.events.clear();
   for(int i=0;i<100;++i) arp.tick(300000000+250000ull*i);
   CHECK(out.ons().empty());
 }
 {
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   arp.tap(1000000); arp.tap(1600000); arp.tap(2200000); CHECK(arp.state().bpm==100);
   arp.toggle(0); arp.enable(true); arp.tick(2300000); CHECK(out.ons().size()==1);
   arp.suspend(true); CHECK(out.events.back().kind==MidiKind::NoteOff);
   arp.tick(9000000); CHECK(out.ons().size()==1);
   arp.suspend(false); arp.tick(9000001); CHECK(out.ons().size()==2);
   arp.selection({2,1}); CHECK(arp.state().selected==1); CHECK(out.events.back().kind==MidiKind::NoteOff);
 }
 { // Receive Clock OFF ignores remote transport without interrupting Tap mode.
   Output out; MidiRouter router(out); Arpeggiator arp(router); arp.toggle(0); arp.toggle(2); arp.enable(true);
   arp.tick(1000000); arp.realtime(0xfa,1100000); arp.realtime(0xfc,1150000); arp.realtime(0xf8,1200000);
   arp.tick(1249999); CHECK(out.ons().size()==1); arp.tick(1250000); CHECK(out.ons().back()==52);
 }
 { // Measured BPM is independent of playback, suspension and internal tempo.
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   CHECK(arp.state().clock_bpm==0);
   arp.tempo(87); ArpSettings settings; settings.gate=75; settings.clock=ArpClock::Midi; arp.settings(settings);
   arp.suspend(true);
   uint64_t time=1000000;
   arp.realtime(0xf8,time); CHECK(arp.state().clock_bpm==0);
   for(int bpm:{120,90,140}) {
     for(int i=0;i<24;++i) { time+=uint64_t(std::llround(60000000.0/(bpm*24))); arp.realtime(0xf8,time); }
     CHECK(arp.state().clock_bpm==bpm && arp.state().clock_present);
     CHECK(arp.state().bpm==87 && out.ons().empty());
   }
   arp.realtime(0xfc,time); CHECK(arp.state().clock_bpm==140);
   arp.tick(time+500001); CHECK(!arp.state().clock_present && arp.state().clock_bpm==0);
   arp.realtime(0xfa,time+1000000); CHECK(arp.state().clock_bpm==0);
   time+=1000000; arp.realtime(0xf8,time);
   for(int i=0;i<24;++i) { time+=20833; arp.realtime(0xf8,time); }
   CHECK(arp.state().clock_bpm==120);
   arp.disconnected(); CHECK(arp.state().clock_bpm==0 && !arp.state().clock_present);
   settings.clock=ArpClock::Tap; arp.settings(settings);
   for(int i=0;i<25;++i) arp.realtime(0xf8,time+1000000+i*20833);
   CHECK(arp.state().clock_bpm==0 && arp.state().bpm==87);
 }
 { // MIDI transport and interpolation for dotted 32nds (4.5 clocks).
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   ArpSettings s; s.gate=75; s.clock=ArpClock::Midi; s.low=s.high=4; arp.settings(s); arp.toggle(0); arp.toggle(2); arp.enable(true);
   arp.realtime(0xf8,1000000); arp.tick(1000000); CHECK(out.ons().empty()); arp.realtime(0xfa,1100000);
   for(int i=0;i<=24;++i) { uint64_t t=1200000+i*20833ull; arp.realtime(0xf8,t); arp.tick(t); }
   CHECK((out.ons()==std::vector<int>{60,64,60}));
   arp.realtime(0xfc,1800000); CHECK(out.events.back().kind==MidiKind::NoteOff);
   auto count=out.ons().size(); arp.tick(1900000); CHECK(out.ons().size()==count);
   arp.realtime(0xfb,2000000); arp.realtime(0xf8,2000000); arp.tick(2000000); CHECK(out.ons().size()==count+1);
   arp.tick(2600000); CHECK(!arp.state().running && !arp.state().clock_present);
   s.division=32; s.timing=ArpTiming::Dotted; arp.settings(s); arp.realtime(0xfa,3000000); out.events.clear();
   for(int i=0;i<=20;++i) { uint64_t t=3100000+i*20833ull; arp.realtime(0xf8,t); arp.tick(t); arp.tick(t+10420); }
   CHECK(out.ons().size()==5); arp.disconnected(); CHECK(!arp.state().running);
 }
 {
   BleMidiRealtimeDecoder decoder; std::vector<MidiRealtime> events; auto collect=[&](MidiRealtime e) { events.push_back(e); };
   const uint8_t a[]={0x80,0xf8,0xfa}; CHECK(decoder.parse(a,sizeof(a),1000000,collect)); CHECK(events.size()==1 && events[0].status==0xfa);
   decoder.reset(); events.clear();
   const uint8_t b[]={0x80,0x80,0xfa,0x80,0xf8,0x95,0xf8}; CHECK(decoder.parse(b,sizeof(b),2000000,collect));
   CHECK(events.size()==3 && events[2].time_us-events[1].time_us==21000);
   CHECK(events.back().time_us==2000000 && events.front().time_us==1979000);
   const uint8_t bad[]={0x80,0x80,0xfa,0x81,0x90,60}; auto count=events.size(); CHECK(!decoder.parse(bad,sizeof(bad),2100000,collect)); CHECK(events.size()==count);
   decoder.reset(); events.clear();
   const uint8_t wrap1[]={0xbf,0xfe,0xf8},wrap2[]={0x80,0x88,0xf8};
   CHECK(decoder.parse(wrap1,sizeof(wrap1),3000000,collect)); CHECK(decoder.parse(wrap2,sizeof(wrap2),3010000,collect)); CHECK(events[1].time_us-events[0].time_us==10000);
   decoder.reset(); events.clear();
   const uint8_t within[]={0xbf,0xfe,0xf8,0x88,0xf8};
   CHECK(decoder.parse(within,sizeof(within),3500000,collect)); CHECK(events.size()==2 && events[1].time_us-events[0].time_us==10000);
   decoder.reset(); events.clear();
   const uint8_t channel[]={0x80,0x80,0x90,60,100,0x81,64,100,0x82,0xf8}; CHECK(decoder.parse(channel,sizeof(channel),4000000,collect)); CHECK(events.size()==1 && events[0].status==0xf8);
   decoder.reset(); events.clear();
   // Arrival jitter must not become pulse interval jitter. Also exercise 13-bit
   // wraps, packet batching, and sender/local clock drift over a long stream.
   uint64_t previous=0;
   for(unsigned i=0;i<3000;++i) {
     const uint64_t remote=uint64_t(i)*20;
     const uint8_t clock[]={uint8_t(0x80|((remote>>7)&63)),uint8_t(0x80|(remote&127)),0xf8};
     const uint64_t arrival=5000000+remote*1000+uint64_t(i)*2+(i%4)*3000;
     CHECK(decoder.parse(clock,sizeof(clock),arrival,collect));
     CHECK(events.back().time_us<=arrival);
     if(i) { const auto gap=events.back().time_us-previous; CHECK(gap>=19000 && gap<=21000); }
     previous=events.back().time_us;
   }
   CHECK(events.size()==3000);
   // Silence longer than half a wrap starts a fresh correlation.
   const uint8_t resumed[]={0x80,0x80,0xfa};
   CHECK(decoder.parse(resumed,sizeof(resumed),70000000,collect)); CHECK(events.back().time_us==70000000);
 }
 // End-to-end timing: delayed BLE writes bundle pairs of 24-PPQN pulses.
 // Decode real wire packets, release them through the same future-timestamp
 // consumer as the device, then measure actual Note On times, not just stamps.
 auto ble_rhythm=[](bool buffered,int division,ArpTiming timing,int swing,bool late_packets=false){
   struct TimedOutput:MidiTransport{uint64_t now=0;std::vector<uint64_t> ons;bool send(MidiEvent e)override{if(e.kind==MidiKind::NoteOn)ons.push_back(now);return true;}} output;
   MidiRouter router(output);Arpeggiator arp(router);ArpSettings settings;settings.clock=ArpClock::Midi;settings.division=division;settings.timing=timing;settings.swing=swing;settings.low=settings.high=4;arp.settings(settings);arp.toggle(0);arp.toggle(2);arp.enable(true);
   BleMidiRealtimeDecoder decoder;std::vector<MidiRealtime> queued;size_t head=0;unsigned batch=0;
   const auto collect=[&](MidiRealtime e){queued.push_back(buffered?ble_midi_clock_playout(e):e);};
   constexpr uint64_t origin=1000000;const uint8_t start[]={uint8_t(0x80|((7000>>7)&63)),uint8_t(0x80|(7000&127)),0xfa};
   for(uint64_t now=origin;now<origin+6300000;now+=1000){
     output.now=now;
     if(now==origin+5000)CHECK(decoder.parse(start,sizeof(start),now,collect));
     while(batch<150){
       const unsigned first=batch*2,last=first+1;const uint64_t arrival=origin+uint64_t(last)*500000/24+5000+(batch%5)*2000+(late_packets && batch%11==3?90000:0);
       if(arrival>now)break;
       const unsigned a=unsigned(7000+std::llround(double(first)*500./24))&8191,b=unsigned(7000+std::llround(double(last)*500./24))&8191;
       if((a>>7)==(b>>7)){const uint8_t bytes[]={uint8_t(0x80|(a>>7)),uint8_t(0x80|(a&127)),0xf8,uint8_t(0x80|(b&127)),0xf8};CHECK(decoder.parse(bytes,sizeof(bytes),now,collect));}
       else for(unsigned stamp:{a,b}){const uint8_t bytes[]={uint8_t(0x80|(stamp>>7)),uint8_t(0x80|(stamp&127)),0xf8};CHECK(decoder.parse(bytes,sizeof(bytes),now,collect));}
       ++batch;
     }
     while(head<queued.size() && queued[head].time_us<=now){const auto event=queued[head++];arp.realtime(event.status,event.time_us);}
     arp.tick(now);
   }
   return output.ons;
 };
 for(int division:{8,16,32})for(auto timing:{ArpTiming::Straight,ArpTiming::Dotted,ArpTiming::Triplet})for(int swing:{0,25}){
   const auto ons=ble_rhythm(true,division,timing,swing);CHECK(ons.size()>12);
   double base=2000000./division;if(timing==ArpTiming::Dotted)base*=1.5;else if(timing==ArpTiming::Triplet)base*=2./3;
   for(size_t i=8;i+1<ons.size();++i){const double expected=base*(i%2?100-swing:100+swing)/100;CHECK(std::abs(double(ons[i+1]-ons[i])-expected)<=2500);}
 }
 for(int division:{8,16,32})for(auto timing:{ArpTiming::Straight,ArpTiming::Dotted,ArpTiming::Triplet})for(int swing:{0,25}){
   const auto ons=ble_rhythm(true,division,timing,swing,true);CHECK(ons.size()>12);
   double base=2000000./division;if(timing==ArpTiming::Dotted)base*=1.5;else if(timing==ArpTiming::Triplet)base*=2./3;
   for(size_t i=8;i+1<ons.size();++i){const double expected=base*(i%2?100-swing:100+swing)/100;CHECK(std::abs(double(ons[i+1]-ons[i])-expected)<=2500);}
 }
 {const auto ons=ble_rhythm(false,16,ArpTiming::Straight,0);double worst=0;for(size_t i=8;i+1<ons.size();++i)worst=std::max(worst,std::abs(double(ons[i+1]-ons[i])-125000));CHECK(worst<=2500);}
 std::puts("PASS: arpeggiator patterns, timing, gate, probability, transport, buffered BLE clock rhythm");
}

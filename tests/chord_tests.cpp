#include "chords.hpp"
#include "arpeggiator.hpp"
#include "synth.hpp"
#include "file_settings.hpp"
#include "chord_storage.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
using namespace sm;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"Chord FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct Output:MidiTransport {
 std::vector<MidiEvent> events; bool fail=false; Synth synth;
 bool send(MidiEvent event) override { if(fail) return false; events.push_back(event); synth.event(event); return true; }
};
int main() {
 for(int offset=0;offset<500000;offset+=7000) {
   // Full 24-PPQN path at 120 BPM / 1/8: sweep touch phase across a quarter,
   // recording actual output timestamps rather than supplying synthetic beats.
   struct Timed:MidiTransport {
     uint64_t now=0; std::vector<uint64_t> attacks;
     bool send(MidiEvent event) override { if(event.kind==MidiKind::NoteOn) attacks.push_back(now); return true; }
   } out;
   MidiRouter router(out); Arpeggiator arp(router); ChordEngine engine(router);
   ArpSettings settings; settings.clock=ArpClock::Midi; arp.settings(settings);
   ChordSelection selection; selection.strum_division=8; engine.selection(selection); engine.clock_sync(true);
   const uint64_t origin=1000000; arp.realtime(0xfa,origin); unsigned pulse=0; bool pressed=false;
   for(out.now=origin;out.now<=origin+offset+1100000;out.now+=1000) {
     while(origin+uint64_t(pulse)*500000/24<=out.now) arp.realtime(0xf8,origin+uint64_t(pulse++)*500000/24);
     arp.tick(out.now); engine.tick(out.now,arp.clock_quarter_us());
     if(!pressed && out.now>=origin+offset) { engine.press(0,0); pressed=true; }
   }
   CHECK(out.attacks.size()==4);
   CHECK(out.attacks[0]==origin+offset); // No waiting for any global grid edge.
   for(int i=1;i<4;++i)
     CHECK(out.attacks[i]-out.attacks[i-1]>=248000 && out.attacks[i]-out.attacks[i-1]<=252000);
   engine.stop();
 }
 for(bool release_everyone:{false,true}) {
   // Off accepted / On rejected: restore the remaining owner, or cancel when
   // both fingers lift. Never resurrect an unowned note or send unbalanced Ons.
   struct Selective:MidiTransport {
     bool reject_on=false; std::vector<MidiEvent> accepted;
     bool send(MidiEvent event) override {
       if(reject_on && event.kind==MidiKind::NoteOn) return false;
       accepted.push_back(event); return true;
     }
   } out;
   MidiRouter router(out); router.press(2,60,true); out.reject_on=true;
   router.press(7,60,true); CHECK(out.accepted.size()==2);
   CHECK(!router.source_active(2) && !router.source_active(7));
   router.release(7); if(release_everyone) router.release(2);
   router.retry(); CHECK(out.accepted.size()==2);
   out.reject_on=false; router.retry();
   if(release_everyone) CHECK(out.accepted.size()==2 && !router.source_active(2));
   else {
     CHECK(out.accepted.size()==3 && router.source_active(2));
     router.release(2); CHECK(out.accepted.size()==4);
   }
 }
 { // Shared pitches retrigger with balanced Off/On and retain both owners.
   Output out; MidiRouter router(out); ChordEngine engine(router);
   std::set<int> first,second,combined;
   for(int i=0;i<4;++i) { first.insert(engine.palette()[0].pitches[i]); second.insert(engine.palette()[5].pitches[i]); }
   combined=first; combined.insert(second.begin(),second.end());
   engine.press(0,0); const auto first_size=out.events.size(); engine.press(1,5);
   const auto shared=first.size()+second.size()-combined.size();
   CHECK(first_size==4 && out.events.size()==combined.size()+2*shared);
   int ons=0,offs=0;
   for(auto event:out.events) { if(event.kind==MidiKind::NoteOn) { ++ons; CHECK(event.fresh_attack); } else ++offs; }
   CHECK(ons==8 && offs==int(shared));
   for(int i=2;i<6;++i) CHECK(router.source_active(i));
   for(int i=7;i<11;++i) CHECK(router.source_active(i));
   const auto before_release=out.events.size(); engine.release(0);
   CHECK(out.events.size()==before_release+combined.size()-second.size());
   for(size_t i=before_release;i<out.events.size();++i) CHECK(!second.count(out.events[i].note));
   for(int i=7;i<11;++i) CHECK(router.source_active(i));
   const auto still_held=out.events.size(); engine.tick(2000000); CHECK(out.events.size()==still_held);
   engine.release(1); CHECK(out.events.size()==16);
   for(int i=0;i<16;++i) CHECK(!router.source_active(i));
   // The same pad also articulates again; either finger can retain it.
   out.events.clear(); engine.press(0,0); engine.press(1,0); CHECK(out.events.size()==12);
   engine.release(1); CHECK(out.events.size()==12); engine.release(0); CHECK(out.events.size()==16);
 }
 { // Retrigger at the shared voice's strum slot, not at the new pad's touch.
   Output out; MidiRouter router(out); ChordEngine engine(router); ChordSelection selection;
   selection.strum_division=8; engine.selection(selection); engine.clock_sync(true);
   const auto advance=[&](double beat) { engine.tick(1000000+uint64_t(beat*500000),500000); };
   advance(.1); engine.press(0,0); advance(2); // All C voices are held.
   advance(2.1); engine.press(1,5); // Am shares E3 and C4, but not its first A2.
   const auto touched=out.events.size(); CHECK(out.events.back().note==45);
   advance(2.599); CHECK(out.events.size()==touched);
   advance(2.6); CHECK(out.events.size()==touched+2);
   CHECK(out.events[touched].kind==MidiKind::NoteOff && out.events[touched].note==52);
   CHECK(out.events[touched+1].kind==MidiKind::NoteOn && out.events[touched+1].note==52);
   engine.stop(); for(int i=0;i<16;++i) CHECK(!router.source_active(i));
 }
 { // Independent tempo-based strums, starting immediately for either finger.
   Output out; MidiRouter router(out); ChordEngine engine(router); ChordSelection selection;
   selection.variants.fill(ChordVariant::Ninth); selection.octave_shifts[1]=1;
   selection.strum_division=8; engine.selection(selection); engine.clock_sync(true);
   const auto advance=[&](double beat) { engine.tick(1000000+uint64_t(beat*500000),500000); };
   advance(.1); engine.press(0,0); CHECK(router.source_active(2));
   advance(.6); CHECK(router.source_active(3));
   engine.press(1,1); CHECK(router.source_active(7));
   advance(1.1); CHECK(router.source_active(4) && router.source_active(8));
   advance(1.6); CHECK(router.source_active(5) && router.source_active(9));
   // Releasing one finger cancels only its fifth pending voice.
   engine.release(0); advance(2.1); CHECK(!router.source_active(6) && router.source_active(10));
   advance(2.6); CHECK(router.source_active(11));
   for(int i=7;i<12;++i) CHECK(router.source_active(i));
   engine.release(1); for(int i=0;i<16;++i) CHECK(!router.source_active(i));
 }
 { // Each contact retries failed sends independently, including stop cleanup.
   Output out; MidiRouter router(out); ChordEngine engine(router);
   engine.press(0,0); engine.press(1,1); out.fail=true; engine.stop();
   CHECK(router.source_active(2) && router.source_active(7));
   out.fail=false; engine.retry(); for(int i=0;i<16;++i) CHECK(!router.source_active(i));
   out.fail=true; engine.press(0,0); engine.press(1,1); out.fail=false; engine.retry();
   CHECK(router.source_active(2) && router.source_active(7)); engine.stop();
 }
 for(auto choice:chord_strum_choices) if(choice.division) {
   // Arbitrary touch phase: immediate attack, all following attacks relative
   // to it, for straight/dotted/triplet and all subdivisions.
   for(uint64_t quarter:{500000ull,1000000ull}) {
     Output out; MidiRouter router(out); ChordEngine engine(router);
     ChordSelection selection; selection.strum_division=choice.division; selection.strum_timing=choice.timing;
     selection.variants[0]=ChordVariant::Ninth; engine.selection(selection); engine.clock_sync(true);
     const double factor=choice.timing==1?1.5:choice.timing==2?2./3:1.;
     const double step=4.*factor/choice.division;
     const uint64_t origin=1000000;
     const uint64_t start=origin+123456;
     engine.tick(start,quarter); engine.press(0,0); CHECK(out.events.size()==1);
     for(int voice=1;voice<5;++voice) {
       const auto due=start+uint64_t(std::ceil(voice*step*quarter-1e-9));
       engine.tick(due-1,quarter); CHECK(out.events.size()==size_t(voice));
       engine.tick(due,quarter); CHECK(out.events.size()==size_t(voice+1));
     }
     engine.release(0); for(int source=2;source<7;++source) CHECK(!router.source_active(source));
   }
 }
 { // Receive real 24-PPQN ticks while the arpeggiator itself is disabled.
   Output out; MidiRouter router(out); Arpeggiator arp(router); ChordEngine engine(router);
   ArpSettings settings; settings.clock=ArpClock::Midi; arp.settings(settings);
   ChordSelection selection; selection.strum_division=8; engine.selection(selection); engine.clock_sync(true);
   arp.realtime(0xfa,1000000);
   for(int i=0;i<=6;++i) arp.realtime(0xf8,1000000+uint64_t(i)*500000/24);
   const auto advance=[&](uint64_t now) { arp.tick(now); engine.tick(now,arp.clock_quarter_us()); };
   advance(1125000); engine.press(0,0); CHECK(out.events.size()==1);
   // Changing play mode or resetting the arp does not rephase the chord.
   arp.enable(true); arp.suspend(true); arp.reset_defaults();
   for(int i=7;i<=11;++i) arp.realtime(0xf8,1000000+uint64_t(i)*500000/24);
   advance(1249999); CHECK(out.events.size()==1);
   arp.realtime(0xf8,1250000); advance(1250000); CHECK(out.events.size()==1);
   // A new transport Start does not reset a hand-played strum either.
   arp.realtime(0xfa,1260000); advance(1260000); CHECK(out.events.size()==1);
   arp.realtime(0xf8,1260000); advance(1374999); CHECK(out.events.size()==1);
   advance(1376000); CHECK(out.events.size()==2); // Allow the clock filter's sub-ms rounding.
   engine.release(0); CHECK(out.events.size()==4);
 }
 { // Release cancels every remaining attack, regardless of clock phase.
   Output out; MidiRouter router(out); ChordEngine engine(router);
   ChordSelection selection; selection.strum_division=8; engine.selection(selection); engine.clock_sync(true);
   engine.tick(1100000,500000); engine.press(0,0); engine.release(0);
   engine.tick(1250000,500000); CHECK(out.events.size()==2);
   engine.press(0,0); CHECK(out.events.size()==3); engine.release(0);
   engine.tick(2000000,500000); CHECK(out.events.size()==4);
 }
 { // Audition the draft (including octave) once; retrigger replaces old notes.
   Output out; MidiRouter router(out); ChordEngine engine(router);
   ChordSelection draft; draft.variants[0]=ChordVariant::Ninth; draft.octave_shifts[0]=1;
   engine.tick(1000000); engine.preview(draft,0);
   const auto expected=chord_palette(draft)[0]; CHECK(out.events.size()==5);
   for(int i=0;i<5;++i) CHECK(out.events[i].note==expected.pitches[i]);
   engine.tick(1599999); CHECK(out.events.size()==5);
   engine.tick(1600000); CHECK(out.events.size()==10);
   for(int i=2;i<7;++i) CHECK(!router.source_active(i));
   engine.tick(2000000); engine.preview(draft,0); engine.tick(2100000);
   draft.octave_shifts[0]=-1; engine.preview(draft,0);
   CHECK(out.events.size()==25); engine.tick(2600000); CHECK(out.events.size()==25);
   engine.tick(2700000); CHECK(out.events.size()==30);
 }
 { // Slow strums complete before preview Note Off; closing cancels pending notes.
   Output out; MidiRouter router(out); ChordEngine engine(router); ChordSelection draft;
   draft.strum_ms=250; engine.tick(1000000); engine.preview(draft,0);
   engine.tick(2000000); CHECK(out.events.size()==4);
   engine.tick(2599999); CHECK(out.events.size()==4);
   engine.tick(2600000); CHECK(out.events.size()==8);
   engine.preview(draft,0); engine.stop(); const auto count=out.events.size();
   engine.tick(9000000); CHECK(out.events.size()==count);
 }
 { // Failed Note Off is retried after preview expiry.
   Output out; MidiRouter router(out); ChordEngine engine(router);
   engine.tick(1000000); engine.preview({},0); out.fail=true; engine.tick(1600000);
   CHECK(router.source_active(2)); out.fail=false; engine.tick(1601000);
   for(int i=2;i<7;++i) CHECK(!router.source_active(i));
 }
 for(uint64_t quarter:{500000ull,1000000ull}) for(auto choice:chord_strum_choices) {
   const int division=choice.division;
   Output out; MidiRouter router(out); ChordEngine engine(router); ChordSelection selection;
   selection.strum_ms=250; selection.strum_division=division; selection.strum_timing=choice.timing; engine.selection(selection); engine.clock_sync(true);
   const uint64_t start=1123456; engine.tick(start,quarter); engine.press(0,0);
   CHECK(out.events.size()==(division?1:4)); // First voice is immediate, without grid quantization.
   if(division) for(int voice=1;voice<4;++voice) {
     const int numerator=choice.timing==1?3:choice.timing==2?2:1;
     const int denominator=division*(choice.timing==1?2:choice.timing==2?3:1);
     const auto due=start+(uint64_t(voice)*4*quarter*numerator+denominator-1)/denominator;
     engine.tick(due-1,quarter); CHECK(out.events.size()==size_t(voice));
     engine.tick(due,quarter); CHECK(out.events.size()==size_t(voice+1));
   }
   engine.release(0); for(int source=2;source<7;++source) CHECK(!router.source_active(source));
 }
 { // Tempo changes preserve elapsed musical time rather than restart the strum.
   Output out; MidiRouter router(out); ChordEngine engine(router); ChordSelection selection;
   selection.strum_division=8; engine.selection(selection); engine.clock_sync(true);
   engine.tick(1000000,500000); engine.press(0,0); CHECK(out.events.size()==1);
   engine.tick(1100000,1000000); // .2 quarters elapsed; switch from 120 to 60 BPM.
   engine.tick(1399999,1000000); CHECK(out.events.size()==1);
   engine.tick(1400000,1000000); CHECK(out.events.size()==2);
   engine.release(0); const auto size=out.events.size();
   engine.tick(5000000,1000000); CHECK(out.events.size()==size);
 }
 { // Strum timing is ordered, subtly varied, and cancelled on release.
   struct Timed:MidiTransport {
     uint64_t time=0; std::vector<std::pair<uint64_t,MidiEvent>> events;
     bool send(MidiEvent e) override { events.push_back({time,e}); return true; }
   } out;
   MidiRouter router(out); ChordEngine engine(router); ChordSelection selection; selection.strum_ms=50; selection.strum_division=8;
   engine.selection(selection);
   std::vector<uint64_t> first_gaps; bool varied=false;
   for(int take=0;take<8;++take) {
     out.events.clear(); out.time=1000000+take*1000000ull; engine.tick(out.time); engine.press(0,0);
     CHECK(out.events.size()==1 && out.events[0].second.note==36);
     for(int ms=1;ms<=200;++ms) { out.time+=1000; engine.tick(out.time); }
     CHECK(out.events.size()==4);
     std::vector<uint64_t> gaps;
     for(int i=1;i<4;++i) {
       const auto gap=out.events[i].first-out.events[i-1].first;
       CHECK(gap>=45000 && gap<=55000); gaps.push_back(gap);
       CHECK(out.events[i].second.note>out.events[i-1].second.note);
     }
     if(!take) first_gaps=gaps; else varied|=gaps!=first_gaps;
     engine.release(0);
   }
   CHECK(varied);
   out.events.clear(); engine.press(0,0); CHECK(out.events.size()==1);
   engine.release(0); out.time+=1000000; engine.tick(out.time);
   CHECK(out.events.size()==2 && out.events.back().second.kind==MidiKind::NoteOff);
   // Replacing a partially played chord cancels every remaining old note.
   out.events.clear(); engine.press(0,0); out.time+=10000; engine.tick(out.time); engine.press(0,1);
   const auto after_switch=out.events.size(); out.time+=300000; engine.tick(out.time);
   for(size_t i=after_switch;i<out.events.size();++i) if(out.events[i].second.kind==MidiKind::NoteOn)
     CHECK(out.events[i].second.note==45 || out.events[i].second.note==53 || out.events[i].second.note==62);
   engine.stop(); for(int i=2;i<7;++i) CHECK(!router.source_active(i));
   selection.strum_ms=0; engine.selection(selection); out.events.clear(); engine.press(0,0);
   CHECK(out.events.size()==4); engine.stop();
   selection.strum_ms=250; engine.selection(selection); out.events.clear();
   out.time+=1000000; engine.tick(out.time); engine.press(0,0);
   for(int ms=0;ms<900;++ms) { out.time+=1000; engine.tick(out.time); }
   CHECK(out.events.size()==4);
   for(int i=1;i<4;++i) CHECK(out.events[i].first-out.events[i-1].first>=225000 && out.events[i].first-out.events[i-1].first<=275000);
   engine.stop();
 }
 const char* major[]={"C","Dm","Em","F","G","Am","Bdim"};
 const char* dminor[]={"Dm","Edim","F","Gm","Am","Bb","C"};
 const char* harmonic[]={"Cm","Ddim","Ebaug","Fm","G","Ab","Bdim"};
 for(int i=0;i<7;++i) {
   CHECK(chord_palette({0,0})[i].name==major[i]);
   CHECK(chord_palette({2,1})[i].name==dminor[i]);
   CHECK(chord_palette({0,2})[i].name==harmonic[i]);
 }
 CHECK((chord_palette({0,0})[0].pitches==std::array<int,5>{{36,43,52,60,-1}}));
 CHECK((chord_palette({0,0})[6].pitches==std::array<int,5>{{47,53,62,71,-1}}));
 CHECK(chord_for({0,0},0,ChordVariant::Parallel).name=="Cm");
 CHECK(chord_for({0,0},1,ChordVariant::Parallel).name=="D");
 CHECK(chord_for({0,0},0,ChordVariant::Dominant7).name=="C7");
 CHECK(chord_for({0,0},0,ChordVariant::Seventh).name=="Cmaj7");
 CHECK(chord_for({0,0},1,ChordVariant::Seventh).name=="Dm7");
 CHECK(chord_for({0,0},4,ChordVariant::Seventh).name=="G7");
 CHECK(chord_for({0,0},6,ChordVariant::Seventh).name=="Bm7b5");
 CHECK(chord_for({0,0},0,ChordVariant::Ninth).name=="Cmaj9");
 CHECK(chord_for({0,0},1,ChordVariant::Ninth).name=="Dm9");
 CHECK(chord_for({0,0},0,ChordVariant::Sus4).name=="Csus4");
 CHECK(chord_for({0,0},0,ChordVariant::Sus2).name=="Csus2");
 CHECK(chord_for({0,0},0,ChordVariant::Diminished).name=="Cdim");
 CHECK(chord_for({0,0},0,ChordVariant::Augmented).name=="Caug");
 CHECK((chord_for({0,0},0,ChordVariant::Ninth).pitches==std::array<int,5>{{36,43,52,59,62}}));
 const char* blues_chords[]={"Cm","Ddim","Eb","Fm","Gm","Ab","Bb"};
 for(int pad=0;pad<7;++pad) CHECK(chord_palette({0,blues_mode})[pad].name==blues_chords[pad]);
 CHECK(chord_mode_option(blues_mode)==11 && chord_modes[11]==blues_mode);
 for(int option=0;option<chord_scale_count;++option) {
   const int mode=chord_modes[option],harmony=mode==9?0:(mode==10 || mode==blues_mode)?1:mode;
   CHECK(modes()[harmony].intervals.size()==7);
   const auto reference=chord_palette({0,mode});
   const auto& intervals=modes()[harmony].intervals;
   auto pitch=[&](int degree) { return intervals[degree%7]+12*(degree/7); };
   for(int root=0;root<12;++root) {
     auto palette=chord_palette({root,mode}); CHECK(palette.size()==7);
     CHECK((unpack_chords(pack_chords({root,mode}))==ChordSelection{root,mode}));
     for(int pad=0;pad<7;++pad) {
       const auto& chord=palette[pad];
       CHECK(!chord.name.empty()); CHECK(chord.pitches[0]==36+root+pitch(pad));
       CHECK(chord.pitches[1]-chord.pitches[0]==pitch(pad+4)-pitch(pad));
       CHECK(chord.pitches[2]-chord.pitches[0]==12+pitch(pad+2)-pitch(pad));
       CHECK(chord.pitches[3]-chord.pitches[0]==24);
       for(int voice=0;voice<4;++voice) {
         const int note=chord.pitches[voice];
         CHECK(note>=0 && note<=127); CHECK(note-reference[pad].pitches[voice]==root);
         CHECK(std::find(intervals.begin(),intervals.end(),(note-root+120)%12)!=intervals.end());
         if(voice) CHECK(note>chord.pitches[voice-1]);
       }
       CHECK(chord.voices==4 && chord.pitches[4]==-1);
       for(int variant=0;variant<chord_variant_count;++variant) {
         const auto alternative=chord_for({root,mode},pad,ChordVariant(variant));
         CHECK(alternative.voices==(variant==int(ChordVariant::Ninth)?5:4));
         CHECK(alternative.pitches[0]==chord.pitches[0]);
         for(int voice=0;voice<alternative.voices;++voice) {
           CHECK(alternative.pitches[voice]>=0 && alternative.pitches[voice]<=127);
           if(voice) CHECK(alternative.pitches[voice]>alternative.pitches[voice-1]);
         }
         if(variant==int(ChordVariant::Seventh)) for(int voice=0;voice<4;++voice)
           CHECK(std::find(intervals.begin(),intervals.end(),(alternative.pitches[voice]-root+120)%12)!=intervals.end());
         for(int shift=-1;shift<=1;++shift) {
           ChordSelection shifted{root,mode}; shifted.octave_shifts[pad]=int8_t(shift);
           const auto transposed=chord_for(shifted,pad,ChordVariant(variant));
           CHECK(transposed.name==alternative.name && transposed.voices==alternative.voices);
           for(int voice=0;voice<alternative.voices;++voice) {
             CHECK(transposed.pitches[voice]==alternative.pitches[voice]+12*shift);
             CHECK(transposed.pitches[voice]>=0 && transposed.pitches[voice]<=127);
           }
           if(pad!=0) CHECK(chord_palette(shifted)[0].pitches==palette[0].pitches);
         }
       }
     }
   }
 }
 { // Every pad emits exactly the same notes after repeated full circles.
   Output out; MidiRouter router(out); ChordEngine engine(router);
   const auto palette=engine.palette();
   for(int circle=0;circle<4;++circle) for(int pad=0;pad<7;++pad) {
     const auto before=out.events.size(); engine.press(0,pad);
     CHECK(out.events.size()==before+4);
     for(int voice=0;voice<4;++voice) CHECK(out.events[before+voice].note==palette[pad].pitches[voice]);
     engine.release(0);
   }
 }
 for(int wave=0;wave<waveform_count;++wave) {
   Output out; out.synth.configure({Waveform(wave),0,10}); MidiRouter router(out); ChordEngine engine(router);
   int16_t audio[2048]{};
   for(int mode:chord_modes) {
     engine.selection({0,mode});
     for(int pad=0;pad<chord_pad_count;++pad) {
       engine.press(0,pad); CHECK(out.synth.active_voices()==4); out.synth.render(audio,2048);
       int peak=0; for(auto sample:audio) peak=std::max(peak,std::abs(int(sample)));
       CHECK(peak>1000 && peak<31000);
       engine.release(0); out.synth.render(audio,2048); CHECK(out.synth.active_voices()==0);
       for(int i=1000;i<2048;++i) CHECK(audio[i]==0);
     }
   }
   // Two disjoint pads sound together; lifting one does not retrigger the other.
   engine.press(0,0); engine.press(1,1); CHECK(out.synth.active_voices()==8);
   engine.release(1); out.synth.render(audio,2048); CHECK(out.synth.active_voices()==4); engine.release(0); engine.stop();
   for(int i=0;i<16;++i) CHECK(!router.source_active(i));
   out.synth.render(audio,2048); // Finish the preceding chord's independent release.
   out.fail=true; engine.press(0,2); out.fail=false; engine.retry(); CHECK(out.synth.active_voices()==4);
   out.fail=true; engine.release(0); CHECK(router.source_active(2)); out.fail=false; engine.retry(); CHECK(!router.source_active(2));
   engine.press(0,6); engine.selection({2,1});
   for(int i=0;i<16;++i) CHECK(!router.source_active(i));
   ChordSelection ninths;
   ninths.variants.fill(ChordVariant::Ninth); engine.selection(ninths);
   for(int pad=0;pad<7;++pad) {
     engine.press(0,pad); CHECK(out.synth.active_voices()==5); CHECK(router.source_active(6));
     out.synth.render(audio,2048); int peak=0;
     for(auto value:audio) peak=std::max(peak,std::abs(int(value)));
     CHECK(peak>1000 && peak<31000);
     engine.release(0); out.synth.render(audio,2048); CHECK(out.synth.active_voices()==0);
     for(int source=2;source<7;++source) CHECK(!router.source_active(source));
   }
   // A failed release must also retain/retry the fifth note of a ninth chord.
   engine.press(0,0); out.fail=true; engine.release(0); CHECK(router.source_active(6));
   out.fail=false; engine.retry(); CHECK(!router.source_active(6));
   router.panic(); CHECK(out.synth.active_voices()==0);
   // Ten distinct held pitches fit without voice stealing for every waveform.
   ninths.octave_shifts[1]=1; engine.selection(ninths);
   engine.press(0,0); engine.press(1,1); CHECK(out.synth.active_voices()==10);
   out.synth.render(audio,2048); int peak=0;
   for(auto value:audio) peak=std::max(peak,std::abs(int(value)));
   CHECK(peak>1000 && peak<30000);
   for(int source=2;source<12;++source) CHECK(router.source_active(source));
   engine.release(0); out.synth.render(audio,2048); CHECK(out.synth.active_voices()==5);
   engine.release(1); out.synth.render(audio,2048); CHECK(out.synth.active_voices()==0);
 }
 { // Independent scale choices, file version 9 and migration of former feelings.
   const std::string path="test-chord-settings.cfg"; FileSettings settings(path);
   std::filesystem::remove(path); CHECK(settings.load_chords()==ChordSelection{});
   CHECK(settings.save({2,1})); Setup setup; setup.channel=9; setup.travel=true; CHECK(settings.save_setup(setup));
   CHECK(settings.save_chords({10,6})); CHECK((settings.load_chords()==ChordSelection{10,6}));
   CHECK((settings.load()==Selection{2,1})); CHECK(settings.load_setup()==setup);
   CHECK(settings.save({5,4})); CHECK((settings.load_chords()==ChordSelection{10,6}));
   ChordSelection edited{10,6};
   for(int pad=0;pad<7;++pad) edited.variants[pad]=ChordVariant(pad);
   CHECK(settings.save_chords(edited)); CHECK(settings.load_chords()==edited);
   CHECK(unpack_chords(pack_chords(edited))==edited);
   for(int option=0;option<chord_variant_count;++option) {
     edited.variants.fill(ChordVariant(option));
     CHECK(unpack_chords(pack_chords(edited))==edited);
     CHECK(settings.save_chords(edited)); CHECK(settings.load_chords()==edited);
   }
   CHECK(settings.save_setup(setup)); CHECK(settings.load_chords()==edited);
   edited.variants[0]=ChordVariant(chord_variant_count); CHECK(!settings.save_chords(edited));
   CHECK(unpack_chords(pack_chords({10,6})|15)==ChordSelection{});
   CHECK(unpack_chords(pack_chords({10,6})|(uint64_t(31)<<55))==ChordSelection{});
   CHECK(unpack_chords(pack_chords({10,6})|(uint64_t(2)<<36))==ChordSelection{});
   ChordSelection shifted{10,6}; shifted.variants.fill(ChordVariant::Ninth);
   for(int pad=0;pad<7;++pad) shifted.octave_shifts[pad]=int8_t(pad%3-1);
   CHECK(unpack_chords(pack_chords(shifted))==shifted);
   CHECK(settings.save_chords(shifted)); CHECK(settings.load_chords()==shifted);
   CHECK(settings.save_setup(setup)); CHECK(settings.load_chords()==shifted);
   CHECK(settings.save({2,4})); CHECK(settings.load_chords()==shifted);
   shifted.octave_shifts[0]=2; CHECK(!settings.save_chords(shifted));
   shifted.octave_shifts[0]=-2; CHECK(!settings.save_chords(shifted));
   ChordSelection old_extended{10,6}; old_extended.variants.fill(ChordVariant::Ninth);
   const uint64_t old_v5=(uint64_t(5)<<56)|(pack_chords(old_extended)&((uint64_t(1)<<36)-1));
   CHECK(unpack_chords(old_v5)==old_extended);
   for(int strum=0;strum<=250;strum+=10) {
     old_extended.strum_ms=strum;
     CHECK(unpack_chords(pack_chords(old_extended))==old_extended);
     CHECK(settings.save_chords(old_extended)); CHECK(settings.load_chords()==old_extended);
   }
   old_extended.strum_ms=0;
   const uint64_t old_v6=(uint64_t(6)<<56)|(pack_chords(old_extended)&((uint64_t(1)<<50)-1));
   CHECK(unpack_chords(old_v6)==old_extended);
   const uint64_t old_v7=(uint64_t(7)<<56)|(pack_chords(old_extended)&((uint64_t(1)<<50)-1))|(uint64_t(5)<<50);
   auto migrated=old_extended; migrated.strum_ms=30;
   CHECK(unpack_chords(old_v7)==migrated); // Previous 25 ms rounds to the new grid.
   const uint64_t old_v8=(uint64_t(8)<<56)|(pack_chords(old_extended)&((uint64_t(1)<<55)-1));
   CHECK(unpack_chords(old_v8)==old_extended);
   for(auto choice:chord_strum_choices) for(int ms:{0,70,250}) {
     auto value=old_extended; value.strum_ms=ms; value.strum_division=choice.division; value.strum_timing=choice.timing;
     CHECK(unpack_chords(pack_chords(value))==value);
     CHECK(settings.save_chords(value) && settings.load_chords()==value);
   }
   old_extended.strum_division=3; CHECK(!settings.save_chords(old_extended)); old_extended.strum_division=0;
   auto old_sync=old_extended; old_sync.strum_division=8;
   const uint64_t old_v9=(uint64_t(9)<<59)|(uint64_t(7)<<55)|(pack_chords(old_extended)&((uint64_t(1)<<55)-1));
   CHECK(unpack_chords(old_v9)==old_sync); // Previous option 7 remains straight 1/8.
   // UI reordering must never reinterpret previously persisted revision-10 IDs.
   for(int stored=0;stored<17;++stored) {
     const uint64_t saved=(uint64_t(10)<<60)|(uint64_t(stored)<<55)|(pack_chords(old_extended)&((uint64_t(1)<<55)-1));
     auto expected=old_extended;
     expected.strum_division=stored<5?chord_strum_divisions[stored]:32>>((stored-5)/3);
     expected.strum_timing=stored<5?0:(stored-5)%3;
     CHECK(unpack_chords(saved)==expected);
   }
   auto invalid_rhythm=old_sync; invalid_rhythm.strum_division=64; invalid_rhythm.strum_timing=1;
   CHECK(!settings.save_chords(invalid_rhythm));
   invalid_rhythm.strum_division=32; invalid_rhythm.strum_timing=3; CHECK(!settings.save_chords(invalid_rhythm));
   old_extended.strum_ms=251; CHECK(!settings.save_chords(old_extended));
   old_extended.strum_ms=3; CHECK(!settings.save_chords(old_extended));
   { std::ofstream f(path); f<<"11 2 1 9 3074023 0 2 10 400 1 10 6 0 1 2 3 4 5 6 0 0 0 0 0 0 0\n"; }
   CHECK(settings.load_chords().strum_ms==0 && settings.load_chords().variants[1]==ChordVariant::Parallel);
   { std::ofstream f(path); f<<"12 2 1 9 3074023 0 2 10 400 1 10 6 0 1 2 3 4 5 6 0 0 0 0 0 0 0 25\n"; }
   CHECK(settings.load_chords().strum_ms==30 && settings.load_chords().variants[1]==ChordVariant::Parallel);
   { std::ofstream f(path); f<<"13 2 1 9 3074023 0 2 10 400 1 10 6 0 1 2 3 4 5 6 0 0 0 0 0 0 0 250\n"; }
   CHECK(settings.load_chords().strum_ms==250 && settings.load_chords().strum_division==0);
   { std::ofstream f(path); f<<"14 2 1 9 3074023 0 2 10 400 1 10 6 0 1 2 3 4 5 6 0 0 0 0 0 0 0 250 8\n"; }
   CHECK(settings.load_chords().strum_ms==250 && settings.load_chords().strum_division==8 && settings.load_chords().strum_timing==0);
   CHECK(!settings.save_chords({12,0})); CHECK(!settings.save_chords({0,chord_scale_count}));
   for(int version:{6,7,8}) for(int old=0;old<(version==6?6:version==7?10:4);++old) {
     { std::ofstream f(path); f<<version<<" 2 1 9 3074023 0 2 10 400 1 10 "<<old<<'\n'; }
     const auto mapped=version==6?legacy_chords(10,old):version==7?legacy_ten_chords(10,old):legacy_four_chords(10,old);
     CHECK(settings.load_chords()==mapped); CHECK(settings.save_setup(setup)); CHECK(settings.load_chords()==mapped);
     CHECK((settings.load()==Selection{2,1}));
   }
   { std::ofstream f(path); f<<"5 2 1 9 3074023 0 2 10 400 1\n"; }
   CHECK(settings.load_chords()==ChordSelection{}); CHECK((settings.load()==Selection{2,1})); CHECK(settings.load_setup().receive_clock);
   { std::ofstream f(path); f<<"9 2 1 9 3074023 0 2 10 400 1 10 6\n"; }
   CHECK((settings.load_chords()==ChordSelection{10,6}));
   CHECK(settings.save_setup(setup)); CHECK((settings.load_chords()==ChordSelection{10,6}));
   { std::ofstream f(path); f<<"10 2 1 9 3074023 0 2 10 400 1 10 6 0 1 2 3 4 5 8\n"; }
   ChordSelection old_file{10,6}; old_file.variants={{ChordVariant::Triad,ChordVariant::Parallel,ChordVariant::Dominant7,ChordVariant::Seventh,ChordVariant::Ninth,ChordVariant::Sus4,ChordVariant::Augmented}};
   CHECK(settings.load_chords()==old_file);
   CHECK(settings.save_setup(setup)); CHECK(settings.load_chords()==old_file);
   std::filesystem::remove(path);
   for(int old=0;old<10;++old) {
     CHECK(unpack_chords(0x20000u|(10<<8)|old)==legacy_ten_chords(10,old));
     Selection scale; ChordSelection chords;
     CHECK(unpack_harmony((uint64_t(1)<<56)|(uint64_t(old)<<16)|(1<<8)|2,scale,chords));
     CHECK((scale==Selection{2,1})); CHECK(chords==legacy_ten_chords(2,old));
   }
   CHECK((unpack_chords(0x30a00)==ChordSelection{10,0}));
   CHECK((unpack_chords(0x30a03)==ChordSelection{10,1}));
   CHECK(unpack_chords(0x300ff)==ChordSelection{});
   CHECK(unpack_chords(0x40009)==ChordSelection{});
   CHECK(unpack_chords(0x50000)==ChordSelection{});
 }
 { // Failed/unchanged confirmation leaves other modes intact.
   struct Store:Settings {
     Selection scale{2,1}; ChordSelection chords{10,6}; bool fail=false; int writes=0;
     Selection load() override { return scale; } bool save(Selection s) override { scale=s; return true; }
     ChordSelection load_chords() override { return chords; }
     bool save_chords(ChordSelection c) override { ++writes; if(fail) return false; chords=c; return true; }
     Setup load_setup() override { Setup s; s.global_scale=false; return s; }
     bool save_all(Selection k,Selection,ChordSelection c,Setup) override { ++writes; if(fail)return false; scale=k;chords=c;return true; }
   } store;
   Application app(store); CHECK((app.chords==ChordSelection{10,6}));
   store.fail=true; CHECK(!app.save_chords({5,4})); CHECK((app.current==Selection{2,1}));
   CHECK((app.chords==ChordSelection{10,6}));
   store.fail=false; CHECK(app.save_chords({5,4})); CHECK((app.current==Selection{2,1}));
   const int writes=store.writes; CHECK(app.save_chords(app.chords)); CHECK(store.writes==writes);
 }
 std::puts("PASS: fixed voicings without octave drift, 120 harmony selections including Blues and nine variants, five-voice audio, release/retry, migration and independent persistence");
}

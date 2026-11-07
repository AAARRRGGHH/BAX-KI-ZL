#include "chords.hpp"
namespace sm {
namespace {
std::string seventh_name(int third,int fifth,int seventh) {
 if(third==3 && fifth==6) return seventh==9?"dim7":seventh==10?"m7b5":"dim(maj7)";
 if(third==3) return seventh==11?"m(maj7)":"m7";
 if(fifth==8) return seventh==11?"aug(maj7)":"aug7";
 return seventh==11?"maj7":"7";
}
}
Chord chord_for(ChordSelection selection,int degree,ChordVariant variant) {
 if(!valid(selection) || degree<0 || degree>=chord_pad_count || int(variant)>=chord_variant_count) return {};
 // The six-note minor blues melody uses seven natural-minor harmony degrees;
 // its flattened fifth is a melodic blue note, not an extra chord root.
 const int harmony_mode=selection.mode==9?0:(selection.mode==10 || selection.mode==blues_mode)?1:selection.mode;
 const auto& intervals=modes()[harmony_mode].intervals;
 const auto scale_notes=notes({selection.root,harmony_mode},chord_base_octave);
 const auto pitch=[&](int index) { return intervals[index%7]+12*(index/7); };
 int third=pitch(degree+2)-pitch(degree),fifth=pitch(degree+4)-pitch(degree);
 const int seventh=pitch(degree+6)-pitch(degree);
 const int bass=12*(chord_base_octave+1)+selection.root+pitch(degree)+12*selection.octave_shifts[degree];
 Chord chord; std::string suffix;
 if(variant==ChordVariant::Parallel) { third=third==4?3:4; fifth=7; }
 if(variant==ChordVariant::Dominant7) { third=4; fifth=7; }
 if(variant==ChordVariant::Sus4) { third=5; fifth=7; }
 if(variant==ChordVariant::Sus2) { third=2; fifth=7; }
 if(variant==ChordVariant::Diminished) { third=3; fifth=6; }
 if(variant==ChordVariant::Augmented) { third=4; fifth=8; }
 if(variant==ChordVariant::Dominant7 || variant==ChordVariant::Seventh || variant==ChordVariant::Ninth) {
   const int extension=variant==ChordVariant::Dominant7?10:seventh;
   suffix=seventh_name(third,fifth,extension);
   chord.pitches={{bass,bass+fifth,bass+12+third,bass+12+extension,-1}};
   if(variant==ChordVariant::Ninth) {
     if(suffix=="maj7") suffix="maj9";
     else if(suffix=="m7") suffix="m9";
     else if(suffix=="7") suffix="9";
     else if(suffix=="m7b5") suffix="m9b5";
     else if(suffix=="m(maj7)") suffix="m(maj9)";
     else if(suffix=="aug(maj7)") suffix="aug(maj9)";
     else suffix+="(add9)";
     // A conventional major ninth (14 semitones), intentionally chromatic
     // where necessary; the seventh itself remains scale-derived.
     chord.pitches[4]=bass+26; chord.voices=5;
   }
 } else {
   suffix=third==2?"sus2":third==5?"sus4":third==3?(fifth==6?"dim":"m"):(fifth==8?"aug":"");
   chord.pitches={{bass,bass+fifth,bass+12+third,bass+24,-1}};
 }
 chord.name=scale_notes[degree].name+suffix;
 return chord;
}
std::array<Chord,chord_pad_count> chord_palette(ChordSelection selection) {
 if(!valid(selection)) selection={};
 std::array<Chord,chord_pad_count> result;
 for(int degree=0;degree<chord_pad_count;++degree) result[degree]=chord_for(selection,degree,selection.variants[degree]);
 return result;
}
void ChordEngine::sound(int contact,int pad) {
 auto& part=parts_[contact]; part.pad=pad; part.pending=part.releasing=true; retry();
}
void ChordEngine::retry() {
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 router_.retry();
 for(int contact=0;contact<2;++contact) retry_part(contact);
 if(preview_ && !preview_off_ && !parts_[0].pending) preview_off_=now_+600000;
}
void ChordEngine::retry_part(int contact) {
 auto& part=parts_[contact]; const int base=2+contact*chord_voice_count;
 if(!part.pending) return;
 if(part.releasing) {
   for(int source=base;source<base+chord_voice_count;++source) router_.release(source);
   for(int source=base;source<base+chord_voice_count;++source) if(router_.source_active(source)) return;
   part.releasing=false;
   if(part.pad<0) { part.pending=false; return; }
   part.pitches=palette_[part.pad].pitches; part.voices=palette_[part.pad].voices;
   part.sync=clock_sync_; part.beats=0;
   part.division=strum_division_; part.timing=strum_timing_;
   part.due[0]=now_;
   for(int voice=1;voice<part.voices;++voice) {
     // Small independent timing variation; 0 remains exactly simultaneous.
     random_^=random_<<13; random_^=random_>>17; random_^=random_<<5;
     const int percent=90+int(random_%21);
     part.due[voice]=part.due[voice-1]+(part.sync?0:uint64_t(strum_ms_)*1000*percent/100);
   }
 }
 for(int voice=0;voice<part.voices;++voice) {
   const double factor=part.timing==1?1.5:part.timing==2?2./3:1.;
   if(part.sync && part.division) {
     if(part.beats+1e-12<double(voice)*4/part.division*factor) break;
   } else if(now_<part.due[voice]) break;
   if(!router_.source_active(voice+base)) router_.press(voice+base,part.pitches[voice],true);
   if(!router_.source_active(voice+base)) return;
 }
 for(int source=base;source<base+part.voices;++source) if(!router_.source_active(source)) return;
 part.pending=false;
}
void ChordEngine::clock_sync(bool enabled) { std::lock_guard<std::recursive_mutex> lock(mutex_); clock_sync_=enabled; }
void ChordEngine::tick(uint64_t now_us,uint64_t quarter_us) {
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 for(auto& part:parts_) if(part.pending && !part.releasing && part.sync && now_us>=now_)
   part.beats+=double(now_us-now_)/quarter_us_;
 now_=now_us;
 if(quarter_us>=10000 && quarter_us<=6000000) quarter_us_=quarter_us;
 retry();
 if(preview_ && preview_off_ && now_>=preview_off_) stop();
}
void ChordEngine::selection(ChordSelection value) { std::lock_guard<std::recursive_mutex> lock(mutex_); if(!valid(value)) return; stop(); palette_=chord_palette(value); strum_ms_=value.strum_ms; strum_division_=value.strum_division; strum_timing_=value.strum_timing; }
void ChordEngine::preview(ChordSelection value,int pad) {
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(!valid(value) || pad<0 || pad>=chord_pad_count) return;
 // Editor audition needs one pad, not a fresh seven-chord palette on every
 // selection. Closing the editor restores the saved palette as before.
 stop(); palette_[pad]=chord_for(value,pad,value.variants[pad]);
 strum_ms_=value.strum_ms; strum_division_=value.strum_division; strum_timing_=value.strum_timing;
 press(0,pad);
 // Let every strummed voice arrive, then audition the complete chord for 600 ms.
 preview_=true; preview_off_=parts_[0].pending?0:now_+600000;
}
void ChordEngine::press(int contact,int pad) {
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(contact<0 || contact>=2 || pad<0 || pad>=chord_pad_count) return;
 preview_=false; preview_off_=0;
 sound(contact,pad);
}
void ChordEngine::release(int contact) {
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(contact<0 || contact>=2) return;
 sound(contact,-1);
}
void ChordEngine::stop() {
 std::lock_guard<std::recursive_mutex> lock(mutex_); preview_=false; preview_off_=0;
 for(auto& part:parts_) { part.pad=-1; part.pending=part.releasing=true; }
 retry();
}
}

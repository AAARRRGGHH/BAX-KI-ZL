#include "synth.hpp"
#include <algorithm>
#include <cmath>
namespace sm {
namespace {
float blep(float phase,float step) {
 if(phase<step) { const float t=phase/step; return t+t-t*t-1; }
 if(phase>1-step) { const float t=(phase-1)/step; return t*t+t+t+1; }
 return 0;
}
}
Synth::Synth() {
 for(size_t i=0;i<steps_.size();++i) steps_[i]=440.f*std::pow(2.f,(float(i)-69)/12)/rate;
 for(size_t i=0;i<harmonic_table_.size();++i) harmonic_table_[i]=std::sin(float(i)*6.28318530718f/1024);
}
void Synth::configure(SynthSettings settings) { if(valid_synth(settings)) { stop(); settings_=settings; } }
void Synth::stop() { for(auto& voice:voices_) voice={}; for(auto& tail:tails_) tail={}; tail_frames_.fill(0); }
int Synth::active_voices() const { int count=0; for(const auto& v:voices_) if(v.note>=0) ++count; return count; }
void Synth::event(MidiEvent e) {
 if(e.kind==MidiKind::ControlChange)return;
 if(e.kind==MidiKind::AllNotesOff) { stop(); return; }
 if(e.note>127) return;
 if(e.kind==MidiKind::NoteOff || !e.velocity) {
   for(auto& v:voices_) if(v.note==e.note && !v.releasing) {
     v.releasing=true; v.release_frames=0; v.release_step=v.level/(settings_.decay_ms*rate/1000);
     if(v.level<=0) v={};
   }
   return;
 }
 Voice* chosen=nullptr;
 for(auto& v:voices_) if(v.note==e.note && !v.releasing) {
   if(!e.fresh_attack) return;
   chosen=&v; break;
 }
 // A rapid slide often generates Off/On in the same input cycle. Reuse its
 // envelope and phase instead of restarting Attack or piling up decay tails.
 if(!e.fresh_attack) for(auto& v:voices_) if(v.note>=0 && v.releasing && v.release_frames<=1200 &&
   (!chosen || v.age>chosen->age)) chosen=&v;
 const bool legato=chosen!=nullptr && !e.fresh_attack;
 if(!chosen) for(auto& v:voices_) if(v.note<0) { chosen=&v; break; }
 if(!chosen) for(auto& v:voices_) if(v.releasing && (!chosen || v.level<chosen->level)) chosen=&v;
 if(!chosen) chosen=&*std::min_element(voices_.begin(),voices_.end(),[](const Voice& a,const Voice& b){ return a.age<b.age; });
 if(!legato) {
   const size_t index=size_t(chosen-voices_.data());
   if(chosen->note>=0) { tails_[index]=*chosen; tail_frames_[index]=240; }
   *chosen={};
 }
 chosen->note=e.note; chosen->step=steps_[e.note]; chosen->releasing=false; chosen->release_frames=0;
 chosen->gain=.19f*e.velocity/127;
 chosen->age=++serial_;
}
float Synth::harmonic(float phase) const {
 phase-=int(phase); const float position=phase*1024; const int index=int(position);
 return harmonic_table_[index]+(harmonic_table_[index+1]-harmonic_table_[index])*(position-index);
}
float Synth::sample(Voice& v,float attack_step) {
     if(v.note<0) return 0;
     if(v.releasing) { ++v.release_frames; v.level=std::max(0.f,v.level-v.release_step); if(v.level<=0) { v={}; return 0; } }
     else v.level=std::min(1.f,v.level+attack_step);
     float wave;
     if(settings_.waveform==Waveform::Triangle) {
       // Band-limited odd harmonics with the common, unboosted voice gain.
       constexpr float weights[]={1.f,-1.f/9,1.f/25,-1.f/49,1.f/81,-1.f/121,1.f/169,-1.f/225};
       wave=0;
       for(int partial=0;partial<8;++partial) {
         const int harmonic=partial*2+1;
         if(v.step*harmonic>=.48f) break;
         wave+=this->harmonic(v.phase*harmonic)*weights[partial];
       }
       wave*=.81056946914f; // 8 / pi^2
     } else if(settings_.waveform==Waveform::Sine) {
       wave=harmonic(v.phase); float normalization=1;
       if(v.step*2<.48f) { wave+=.16f*harmonic(v.phase*2); normalization+=.16f; }
       if(v.step*3<.48f) { wave+=.08f*harmonic(v.phase*3); normalization+=.08f; }
       wave/=normalization; // Speaker-friendly soft sine, without gain boost.
     } else if(settings_.waveform==Waveform::Saw) wave=2*v.phase-1-blep(v.phase,v.step);
     else {
       const float duty=settings_.waveform==Waveform::Pulse?.25f:.5f;
       float other=v.phase+1-duty; if(other>=1) other-=1;
       wave=(v.phase<duty?1.f:-1.f)+blep(v.phase,v.step)-blep(other,v.step);
       if(settings_.waveform==Waveform::Pulse) wave=(wave+.5f)/1.5f; // Remove DC and retain peak headroom.
     }
     const float output=wave*v.level*v.gain;
     v.phase+=v.step; if(v.phase>=1) v.phase-=1;
     return output;
}
void Synth::render(int16_t* output,size_t frames) {
 // Even Attack=0 gets a two-millisecond anti-click onset.
 const float attack_step=1000.f/(rate*std::max<uint16_t>(2,settings_.attack_ms));
 for(size_t i=0;i<frames;++i) {
   float sum=0,headroom=0;
   for(size_t voice=0;voice<voices_.size();++voice) {
     sum+=sample(voices_[voice],attack_step);
     headroom+=voices_[voice].level*voices_[voice].gain;
     if(tail_frames_[voice]) {
       sum+=sample(tails_[voice],attack_step)*(float(tail_frames_[voice])/240);
       headroom+=tails_[voice].level*tails_[voice].gain*(float(tail_frames_[voice])/240);
       if(!--tail_frames_[voice]) tails_[voice]={};
     }
   }
   // Keep existing small chords unchanged. With two dense chords, follow the
   // smooth envelopes to retain mix headroom rather than hard-clip ten voices.
   if(headroom>.9f) sum*=.9f/headroom;
   output[i]=int16_t(std::clamp(sum,-.95f,.95f)*32767);
 }
}
}

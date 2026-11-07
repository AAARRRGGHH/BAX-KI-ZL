#pragma once
#include "music.hpp"
#include <cstddef>
namespace sm {
inline constexpr std::array<uint16_t,10> synth_attacks{0,5,10,20,50,100,200,500,1000,2000};
inline constexpr std::array<uint16_t,10> synth_decays{10,25,50,100,200,400,800,1200,2000,4000};
inline constexpr size_t synth_voice_count=10;
class Synth {
 struct Voice { int note=-1; float phase=0,step=0,level=0,gain=0,release_step=0; bool releasing=false; uint32_t age=0,release_frames=0; };
 std::array<Voice,synth_voice_count> voices_{};
 std::array<Voice,synth_voice_count> tails_{};
 std::array<uint16_t,synth_voice_count> tail_frames_{};
 std::array<float,128> steps_{};
 std::array<float,1025> harmonic_table_{};
 SynthSettings settings_{}; uint32_t serial_=0;
 static constexpr float rate=48000.f;
 float sample(Voice& voice,float attack_step);
 float harmonic(float phase) const;
public:
 Synth();
 void configure(SynthSettings settings);
 void event(MidiEvent event);
 void stop();
 void render(int16_t* output,size_t frames);
 int active_voices() const;
};
struct LocalAudio {
 virtual ~LocalAudio()=default;
 virtual bool configure(bool enabled,SynthSettings settings)=0;
 virtual bool send(MidiEvent event)=0;
};
class PerformanceOutput:public MidiTransport {
 MidiTransport& midi_; LocalAudio& audio_; Setup setup_{}; bool ready_=true;
public:
 PerformanceOutput(MidiTransport& midi,LocalAudio& audio):midi_(midi),audio_(audio){}
 bool configure(Setup value) {
   if(!valid_setup(value)) return false;
   // Route is changed before backend initialization: failed audio never leaks MIDI.
   setup_=value; midi_.set_route(value.midi_route); midi_.set_enabled(!value.travel);
   ready_=audio_.configure(value.travel,value.synth); return ready_;
 }
 bool travel() const { return setup_.travel; }
 bool ready() const { return ready_; }
 bool send(MidiEvent event) override { return setup_.travel?audio_.send(event):midi_.send(event); }
};
}

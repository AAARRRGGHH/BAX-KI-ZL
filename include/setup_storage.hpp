#pragma once
#include "music.hpp"
namespace sm {
// One atomic NVS integer; also reads legacy v1 (version32/channel8/RGB24).
inline uint64_t pack_setup(Setup s) {
 // AUTO keeps v5/v6 byte-identical. USB uses v7/v8, BLE v9/v10;
 // odd tags are global ON. Legacy v1-v4 default to global ON / AUTO.
 const unsigned tag=5+2*unsigned(s.midi_route)+(s.global_scale?0:1);
 return (uint64_t(tag)<<56)|(uint64_t(s.receive_clock)<<55)|(uint64_t(int(s.synth.waveform)>>2)<<54)|(uint64_t(s.synth.decay_ms)<<42)|(uint64_t(s.synth.attack_ms)<<31)|
   (uint64_t(int(s.synth.waveform)&3)<<29)|(uint64_t(s.travel)<<28)|(uint64_t(s.channel-1)<<24)|s.accent;
}
inline Setup unpack_setup(uint64_t value) {
 if((value>>32)==1) { Setup old{int((value>>24)&255),uint32_t(value&0xffffff)}; return valid_setup(old)?old:Setup{}; }
 const auto version=value>>56;
 if((version<2 || version>10) || (version<4 && (value&(uint64_t(1)<<55))) || (version==2 && (value&(uint64_t(1)<<54)))) return {};
 Setup s{int((value>>24)&15)+1,uint32_t(value&0xffffff),bool((value>>28)&1),
   {Waveform(((value>>29)&3)|((version>=3?((value>>54)&1):0)<<2)),uint16_t((value>>31)&2047),uint16_t((value>>42)&4095)},version>=4 && bool((value>>55)&1),!(version>=5 && version%2==0)};
 if(version>=7) s.midi_route=MidiRoute((version-5)/2);
 return valid_setup(s)?s:Setup{};
}
}

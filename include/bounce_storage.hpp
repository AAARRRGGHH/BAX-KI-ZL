#pragma once
#include "music.hpp"
namespace sm::bounce {
using Bytes=std::array<uint8_t,16>;
inline uint32_t checksum(const Bytes& b){uint32_t h=2166136261u;for(int i=0;i<12;++i)h=(h^b[i])*16777619u;return h;}
inline Bytes pack(Settings s){Bytes b{{7,uint8_t(s.scale.root),uint8_t(s.scale.mode),uint8_t(s.low),uint8_t(s.high),uint8_t(s.bpm),uint8_t(s.bpm>>8),uint8_t(s.division),uint8_t(s.timing),uint8_t((s.quantized?1:0)),uint8_t(s.gate),uint8_t(s.energy_seconds)}};const auto h=checksum(b);for(int i=0;i<4;++i)b[12+i]=uint8_t(h>>(8*i));return b;}
inline bool unpack(const Bytes& b,Settings& s){
 uint32_t h=0;for(int i=0;i<4;++i)h|=uint32_t(b[12+i])<<(8*i);if(b[0]<1 || b[0]>7 || b[9]>(b[0]>=4 && b[0]<=6?3:1) || h!=checksum(b) || (b[0]==5 && (b[8]>>2)>=12) || (b[0]==1?b[10]>15:b[0]==2?b[10]!=0:false))return false;
 // Version 1's rotation byte is deliberately ignored; other settings survive.
 Settings v;v.scale={b[1],b[2]};v.low=b[3];v.high=b[4];v.bpm=b[5]+256*b[6];v.division=b[7];v.timing=ArpTiming(b[0]==5?b[8]&3:b[8]);v.quantized=(b[9]&1)!=0;v.energy_seconds=b[11];v.gate=b[0]>=3?b[10]:50;if(!valid_settings(v))return false;s=v;return true;
}
}

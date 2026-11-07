#pragma once
#include "arp_settings.hpp"
namespace sm {
inline uint64_t pack_arpeggiator(ArpPreset s) {
 if(!valid_arp_preset(s)) return 0;
 int division=0; while((1<<division)!=s.settings.division && division<5) ++division;
 const auto& v=s.settings;
 if(int(v.pattern)>=8) {
   const bool snapshot=v.gate_before_ratchet!=0;
   uint64_t payload=uint64_t(v.high*(v.high+1)/2+v.low);
   const auto add=[&](uint64_t value,uint64_t radix) { payload=payload*radix+value; };
   add(division,6); add(int(v.pattern)-8,4); add(int(v.timing),3); add(v.probability,101);
   add(v.swing,51); add(v.block_size? v.block_size-1:0,7); add(s.bpm-40,261);
   add(s.selected,128); add(s.page,10); add(int(v.clock),2); add(snapshot?v.gate/5:v.gate,snapshot?21:101);
   add(v.ratchet_every,17); add(v.ratchet_count-2,3);
   if(snapshot) { add(v.gate_before_ratchet/5,21); return (uint64_t(4)<<61)|payload; }
   return (uint64_t(5)<<60)|payload;
 }
 if(v.gate_before_ratchet) {
   // Two-bit 11 prefix leaves 62 payload bits for the persistent gate snapshot.
   uint64_t payload=uint64_t(v.high*(v.high+1)/2+v.low);
   const auto add=[&](uint64_t value,uint64_t radix) { payload=payload*radix+value; };
   add(division,6); add(int(v.pattern),8); add(int(v.timing),3); add(v.probability,101);
   add(v.swing,51); add(v.block_size? v.block_size-1:0,7); add(s.bpm-40,261);
   add(s.selected,128); add(s.page,10); add(int(v.clock),2); add(v.gate/5,21);
   add(v.ratchet_every,17); add(v.ratchet_count-2,3); add(v.gate_before_ratchet/5,21);
   return (uint64_t(3)<<62)|payload;
 }
 if(v.ratchet_every || v.ratchet_count!=2 || s.page==9) {
   // Mixed-radix encoding fits all supported values losslessly in 60 bits.
   uint64_t payload=uint64_t(v.high*(v.high+1)/2+v.low);
   const auto add=[&](uint64_t value,uint64_t radix) { payload=payload*radix+value; };
   add(division,6); add(int(v.pattern),v.pattern==ArpPattern::LowHigh?8:7); add(int(v.timing),3); add(v.probability,101);
   add(v.swing,51); add(v.block_size? v.block_size-1:0,7); add(s.bpm-40,261);
   add(s.selected,128); add(s.page,10); add(int(v.clock),2); add(v.gate,101);
   add(v.ratchet_every,17); add(v.ratchet_count-2,3);
   return (uint64_t(v.pattern==ArpPattern::LowHigh?4:3)<<60)|payload;
 }
 return (uint64_t(2)<<60)|uint64_t(v.low)|(uint64_t(v.high)<<4)|(uint64_t(division)<<8)|
   (uint64_t(v.pattern)<<11)|(uint64_t(v.timing)<<14)|(uint64_t(v.probability)<<16)|
   (uint64_t(v.swing)<<23)|(uint64_t(v.block_size)<<29)|(uint64_t(s.bpm)<<32)|
   (uint64_t(s.selected)<<41)|(uint64_t(s.page)<<48)|(uint64_t(v.clock)<<52)|(uint64_t(v.gate)<<53);
}
inline ArpPreset unpack_arpeggiator(uint64_t v) {
 if((v>>61)==4 || (v>>60)==5) {
   const bool snapshot=(v>>61)==4;
   uint64_t payload=v&((uint64_t(1)<<(snapshot?61:60))-1);
   const auto take=[&](uint64_t radix) { const int result=int(payload%radix); payload/=radix; return result; };
   ArpPreset s; auto& a=s.settings;
   if(snapshot) a.gate_before_ratchet=take(21)*5;
   a.ratchet_count=take(3)+2; a.ratchet_every=take(17); a.gate=take(snapshot?21:101)*(snapshot?5:1);
   a.clock=ArpClock(take(2)); s.page=take(10); s.selected=uint16_t(take(128)); s.bpm=take(261)+40;
   const int block=take(7); a.block_size=block?block+1:0; a.swing=take(51);
   a.probability=take(101); a.timing=ArpTiming(take(3)); a.pattern=ArpPattern(8+take(4)); a.division=1<<take(6);
   if(payload>=45 || (snapshot && !a.gate_before_ratchet)) return {};
   a.high=0; while(uint64_t((a.high+1)*(a.high+2)/2)<=payload) ++a.high;
   a.low=int(payload)-a.high*(a.high+1)/2;
   return valid_arp_preset(s)?s:ArpPreset{};
 }
 if((v>>62)==3) {
   uint64_t payload=v&((uint64_t(1)<<62)-1);
   const auto take=[&](uint64_t radix) { const int result=int(payload%radix); payload/=radix; return result; };
   ArpPreset s; auto& a=s.settings;
   a.gate_before_ratchet=take(21)*5; a.ratchet_count=take(3)+2; a.ratchet_every=take(17);
   a.gate=take(21)*5; a.clock=ArpClock(take(2)); s.page=take(10);
   s.selected=uint16_t(take(128)); s.bpm=take(261)+40;
   const int block=take(7); a.block_size=block?block+1:0; a.swing=take(51);
   a.probability=take(101); a.timing=ArpTiming(take(3)); a.pattern=ArpPattern(take(8)); a.division=1<<take(6);
   if(payload>=45 || !a.gate_before_ratchet) return {};
   a.high=0; while(uint64_t((a.high+1)*(a.high+2)/2)<=payload) ++a.high;
   a.low=int(payload)-a.high*(a.high+1)/2;
   return valid_arp_preset(s)?s:ArpPreset{};
 }
 if((v>>60)==3 || (v>>60)==4) {
   uint64_t payload=v&((uint64_t(1)<<60)-1);
   const auto take=[&](uint64_t radix) { const int result=int(payload%radix); payload/=radix; return result; };
   ArpPreset s; auto& a=s.settings;
   a.ratchet_count=take(3)+2; a.ratchet_every=take(17); a.gate=take(101); a.clock=ArpClock(take(2));
   s.page=take(10); s.selected=uint16_t(take(128)); s.bpm=take(261)+40;
   const int block=take(7); a.block_size=block?block+1:0; a.swing=take(51);
   a.probability=take(101); a.timing=ArpTiming(take(3)); a.pattern=ArpPattern(take((v>>60)==4?8:7)); a.division=1<<take(6);
   if(payload>=45) return {};
   a.high=0; while(uint64_t((a.high+1)*(a.high+2)/2)<=payload) ++a.high;
   a.low=int(payload)-a.high*(a.high+1)/2;
   return valid_arp_preset(s)?s:ArpPreset{};
 }
 const bool legacy=(v>>56)==1;
 if((!legacy && (v>>60)!=2) || (legacy && ((v>>52)&15)!=0) || ((v>>8)&7)>5) return {};
 ArpPreset s;
 auto& a=s.settings;
 a.low=int(v&15); a.high=int((v>>4)&15); a.division=1<<((v>>8)&7);
 a.probability=int((v>>16)&127); a.swing=int((v>>23)&63); a.block_size=int((v>>29)&7);
 a.pattern=ArpPattern((v>>11)&7); a.timing=ArpTiming((v>>14)&3);
 a.clock=ArpClock((v>>(legacy?51:52))&1);
 a.gate=legacy?100:int((v>>53)&127);
 s.bpm=int((v>>32)&511); s.selected=uint16_t((v>>41)&127); s.page=int((v>>48)&(legacy?7:15));
 return valid_arp_preset(s)?s:ArpPreset{};
}
}

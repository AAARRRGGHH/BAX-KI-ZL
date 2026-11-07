#pragma once
#include "chord_storage.hpp"
#include "setup_storage.hpp"
namespace sm {
struct PerformanceSettings { Selection keyboard{},arp{}; ChordSelection chords{}; Setup setup{}; };
inline std::array<uint8_t,24> pack_performance(PerformanceSettings s) {
 std::array<uint8_t,24> bytes{{'P','B','L',1,uint8_t(s.keyboard.root),uint8_t(s.keyboard.mode),uint8_t(s.arp.root),uint8_t(s.arp.mode)}};
 const uint64_t chords=pack_chords(s.chords),setup=pack_setup(s.setup);
 for(int i=0;i<8;++i) { bytes[8+i]=uint8_t(chords>>(8*i)); bytes[16+i]=uint8_t(setup>>(8*i)); }
 return bytes;
}
inline bool unpack_performance(const std::array<uint8_t,24>& bytes,PerformanceSettings& result) {
 if(bytes[0]!='P' || bytes[1]!='B' || bytes[2]!='L' || bytes[3]!=1) return false;
 uint64_t chords=0,setup=0;
 for(int i=0;i<8;++i) { chords|=uint64_t(bytes[8+i])<<(8*i); setup|=uint64_t(bytes[16+i])<<(8*i); }
 PerformanceSettings value{{bytes[4],bytes[5]},{bytes[6],bytes[7]},unpack_chords(chords),unpack_setup(setup)};
 if(!valid(value.keyboard) || !valid(value.arp) || pack_chords(value.chords)!=chords || pack_setup(value.setup)!=setup) return false;
 result=value; return true;
}
}

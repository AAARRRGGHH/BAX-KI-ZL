#pragma once
#include <array>
#include <cstdint>
namespace sm::ripples {
// Phase positions are 1/96 of a motif cycle, not a 16-step sequence.
struct Cell { int8_t degree=0; uint8_t position=0,accent=90,gate=65; bool rest=false; };
struct Motif { uint32_t id=0,parent=0; uint8_t length=5; std::array<Cell,7> cells{}; };
inline uint32_t hash(uint32_t x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
inline Motif initial_motif(uint32_t seed) {
 constexpr int contours[4][5]={{0,1,2,1,0},{0,2,3,1,0},{0,-1,1,2,0},{0,1,3,2,0}};
 constexpr int rhythms[4][5]={{0,12,36,48,72},{0,18,36,60,78},{0,16,32,48,72},{0,24,36,60,72}};
 const auto shape=hash(seed)%4,rhythm=hash(seed^0x7f4a7c15u)%4;
 Motif m; m.id=1;
 for(int i=0;i<5;++i) m.cells[i]={int8_t(contours[shape][i]),uint8_t(rhythms[rhythm][i]),uint8_t(i==0?100:i==4?78:82+hash(seed+unsigned(i))%15),uint8_t(i==4?55:65),shape==2 && i==3};
 return m;
}
inline unsigned differences(const Motif& a,const Motif& b) {
 unsigned changes=unsigned(a.length!=b.length);
 for(unsigned i=0;i<a.length && i<b.length;++i) {
   const auto x=a.cells[i],y=b.cells[i]; changes+=unsigned(x.degree!=y.degree || x.position!=y.position || x.rest!=y.rest || x.accent!=y.accent);
 }
 return changes;
}
inline uint32_t signature(const Motif& m) {
 uint32_t result=2166136261u;
 for(unsigned i=0;i<m.length;++i) { const auto c=m.cells[i]; for(auto x:{uint8_t(c.degree+16),c.position,c.accent,uint8_t(c.rest)}) result=(result^x)*16777619u; }
 return result;
}
}

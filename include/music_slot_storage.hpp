#pragma once
#include "chord_storage.hpp"
#include "arp_storage.hpp"
#include "bounce_storage.hpp"
#include <algorithm>
namespace sm {
using LegacyMusicSlotBytes=std::array<uint8_t,32>;
using NamedLegacyMusicSlotBytes=std::array<uint8_t,40>;
using MusicSlotBytes=std::array<uint8_t,56>;
template<size_t N> inline uint32_t slot_checksum(const std::array<uint8_t,N>& b) {
 uint32_t hash=2166136261u; for(size_t i=0;i<N-4;++i) hash=(hash^b[i])*16777619u; return hash;
}
inline MusicSlotBytes pack_music_slot(const MusicPreset& p) {
 if(!valid_music_preset(p)) return {};
 MusicSlotBytes b{{'P','B','S',3,uint8_t(p.keyboard.root),uint8_t(p.keyboard.mode),uint8_t(p.arp.root),uint8_t(p.arp.mode)}};
 const auto chords=pack_chords(p.chords),arp=pack_arpeggiator(p.arpeggiator);
 for(int i=0;i<8;++i) { b[8+i]=uint8_t(chords>>(8*i)); b[16+i]=uint8_t(arp>>(8*i)); b[26+i]=uint8_t(p.name[i]); }
 b[24]=uint8_t(p.octave); b[25]=p.global_scale;
 const auto kinetic=bounce::pack(p.kinetic);std::copy(kinetic.begin(),kinetic.end(),b.begin()+34);
 const auto hash=slot_checksum(b); for(int i=0;i<4;++i) b[52+i]=uint8_t(hash>>(8*i)); return b;
}
template<size_t N> inline bool unpack_music_slot(const std::array<uint8_t,N>& b,MusicPreset& result) {
 static_assert(N==32 || N==40 || N==56);
 if(b[0]!='P' || b[1]!='B' || b[2]!='S' || b[3]!=(N==32?1:N==40?2:3) || b[25]>1 || b[N-6] || b[N-5]) return false;
 uint32_t hash=0; for(int i=0;i<4;++i) hash|=uint32_t(b[N-4+i])<<(8*i);
 if(hash!=slot_checksum(b)) return false;
 uint64_t chords=0,arp=0; for(int i=0;i<8;++i) { chords|=uint64_t(b[8+i])<<(8*i); arp|=uint64_t(b[16+i])<<(8*i); }
 MusicPreset p{{b[4],b[5]},{b[6],b[7]},unpack_chords(chords),unpack_arpeggiator(arp),b[24],b[25]!=0};
 if constexpr(N>=40) for(int i=0;i<8;++i) p.name[i]=char(b[26+i]);
 if constexpr(N==56){bounce::Bytes kinetic{};std::copy_n(b.begin()+34,16,kinetic.begin());if(!bounce::unpack(kinetic,p.kinetic))return false;}
 else if(p.global_scale)p.kinetic.scale=p.keyboard;
 if(!valid_music_preset(p) || pack_chords(p.chords)!=chords || pack_arpeggiator(p.arpeggiator)!=arp) return false;
 result=p; return true;
}
inline bool unpack_music_slot_data(const MusicSlotBytes& b,size_t size,MusicPreset& result) {
 if(size==b.size()) return unpack_music_slot(b,result);
 if(size==40){NamedLegacyMusicSlotBytes old{};std::copy_n(b.begin(),40,old.begin());return unpack_music_slot(old,result);}
 if(size!=32) return false;
 LegacyMusicSlotBytes old{}; for(int i=0;i<32;++i) old[i]=b[i]; return unpack_music_slot(old,result);
}
// One atomic bank keeps compaction safe across interrupted writes.
struct MusicSlotBank {
 int count=0; std::array<MusicPreset,music_slot_count> slots{};
 bool put(int index,const MusicPreset& p) {
   if(index<0 || index>=music_slot_count || !valid_music_preset(p)) return false;
   index=std::min(index,count); slots[index]=p; if(index==count) ++count; return true;
 }
 bool erase(int index) {
   if(index<0 || index>=count) return false;
   for(int i=index;i<count-1;++i) slots[i]=slots[i+1];
   slots[--count]={}; return true;
 }
};
using LegacyMusicBankBytes=std::array<uint8_t,332>;
using MusicBankBytes=std::array<uint8_t,460>;
inline MusicBankBytes pack_music_bank(const MusicSlotBank& bank) {
 if(bank.count<0 || bank.count>music_slot_count) return {};
 MusicBankBytes b{{'P','B','B',2,uint8_t(bank.count)}};
 for(int i=0;i<bank.count;++i) {
   if(!valid_music_preset(bank.slots[i])) return {};
   const auto slot=pack_music_slot(bank.slots[i]); std::copy(slot.begin(),slot.end(),b.begin()+8+56*i);
 }
 const auto hash=slot_checksum(b); for(int i=0;i<4;++i) b[456+i]=uint8_t(hash>>(8*i)); return b;
}
template<size_t N> inline bool unpack_music_bank(const std::array<uint8_t,N>& b,MusicSlotBank& result) {
 static_assert(N==332 || N==460);constexpr size_t slot_size=N==332?40:56,checksum_at=N-4;
 if(b[0]!='P' || b[1]!='B' || b[2]!='B' || b[3]!=(N==332?1:2) || b[4]>8 || b[5] || b[6] || b[7]) return false;
 uint32_t hash=0; for(int i=0;i<4;++i) hash|=uint32_t(b[checksum_at+i])<<(8*i);
 if(hash!=slot_checksum(b)) return false;
 MusicSlotBank bank; bank.count=b[4];
 for(int i=0;i<bank.count;++i) {
   MusicSlotBytes slot{}; std::copy_n(b.begin()+8+slot_size*i,slot_size,slot.begin());
   if(!unpack_music_slot_data(slot,slot_size,bank.slots[i])) return false;
 }
 for(size_t i=8+slot_size*bank.count;i<checksum_at;++i) if(b[i]) return false;
 result=bank; return true;
}
inline bool unpack_music_bank_data(const MusicBankBytes& bytes,size_t size,MusicSlotBank& result){
 if(size==bytes.size())return unpack_music_bank(bytes,result);
 if(size!=332)return false;
 LegacyMusicBankBytes old{};std::copy_n(bytes.begin(),old.size(),old.begin());return unpack_music_bank(old,result);
}
}

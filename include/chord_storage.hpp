#pragma once
#include "music.hpp"
namespace sm {
inline ChordSelection legacy_chords(int root,int feel) {
 constexpr int migrated[]={0,0,1,1,1,0};
 if(root<0 || root>11 || feel<0 || feel>=6) return {};
 return {root,migrated[feel]};
}
inline ChordSelection legacy_ten_chords(int root,int feel) {
 constexpr int migrated[]={0,1,1,1,1,1,1,0,1,1};
 if(root<0 || root>11 || feel<0 || feel>=10) return {};
 return {root,migrated[feel]};
}
// Old emotions do not define a scale. Retain their key and major/minor basis.
inline ChordSelection legacy_four_chords(int root,int feel) {
 if(root<0 || root>11 || feel<0 || feel>=4) return {};
 return {root,feel==0?0:1};
}
inline uint64_t pack_chords(ChordSelection s) {
 // Revision 10 uses a four-bit version tag and five bits for sync choice.
 // Keep the original stored order (straight/D/T) independent of UI ordering.
 const int option=chord_strum_option(s.strum_division,s.strum_timing);
 const int stored=option<5?option:5+((option-5)/3)*3+s.strum_timing;
 uint64_t value=(uint64_t(10)<<60)|(uint64_t(stored)<<55)|
   (uint64_t(s.root)<<32)|(uint64_t(s.mode)<<28)|(uint64_t(s.strum_ms/10)<<50);
 for(int pad=0;pad<7;++pad) value|=uint64_t(s.variants[pad])<<(pad*4);
 for(int pad=0;pad<7;++pad) value|=uint64_t(s.octave_shifts[pad]+1)<<(36+pad*2);
 return value;
}
inline ChordSelection unpack_chords(uint64_t value) {
 const bool extended=(value>>60)==10;
 if(extended || (value>>59)==9) {
   const int sync=int((value>>55)&(extended?31:15));
   if(sync>=int(extended?chord_strum_choices.size():chord_strum_divisions.size())) return {};
   ChordSelection result{int((value>>32)&15),int((value>>28)&15)};
   for(int pad=0;pad<7;++pad) {
     result.variants[pad]=ChordVariant((value>>(pad*4))&15);
     result.octave_shifts[pad]=int8_t(int((value>>(36+pad*2))&3)-1);
   }
   result.strum_ms=int((value>>50)&31)*10;
   result.strum_division=extended && sync>=5?32>>((sync-5)/3):chord_strum_divisions[sync];
   result.strum_timing=extended && sync>=5?(sync-5)%3:0;
   return valid(result)?result:ChordSelection{};
 }
 if((value>>56)>=5 && (value>>56)<=8) {
   const bool strummed=(value>>56)>=7,shifted=(value>>56)>=6;
   if(strummed?((value>>55)&1)!=0:shifted?((value>>50)&63)!=0:((value>>36)&0xfffff)!=0) return {};
   ChordSelection result{int((value>>32)&15),int((value>>28)&15)};
   for(int pad=0;pad<7;++pad) result.variants[pad]=ChordVariant((value>>(pad*4))&15);
   if(shifted) for(int pad=0;pad<7;++pad) result.octave_shifts[pad]=int8_t(int((value>>(36+pad*2))&3)-1);
   if(strummed) {
     const int index=int((value>>50)&31);
     if((value>>56)==7) {
       if(index>20) return {};
       result.strum_ms=((index*5+5)/10)*10;
     } else result.strum_ms=index*10;
   }
   return valid(result)?result:ChordSelection{};
 }
 if(value>>32) return {};
 const int root=int((value>>8)&255),feel=int(value&255);
 if((value>>16)==1) return legacy_chords(root,feel);
 if((value>>16)==2) return legacy_ten_chords(root,feel);
 if((value>>16)==3) return legacy_four_chords(root,feel);
 ChordSelection result{root,feel};
 return (value>>16)==4 && valid(result) && (feel<9 || feel==blues_mode)?result:ChordSelection{};
}
// Read-only migration of the rejected shared-harmony revision.
inline bool unpack_harmony(uint64_t value,Selection& scale,ChordSelection& chords) {
 if((value>>24)!=(uint64_t(1)<<32)) return false;
 Selection s{int(value&255),int((value>>8)&255)}; const int old_feel=int((value>>16)&255);
 if(!valid(s) || old_feel>=10) return false;
 scale=s; chords=legacy_ten_chords(s.root,old_feel); return true;
}
}

#pragma once
#include "music.hpp"
namespace sm {
inline constexpr int chord_pad_count=7;
inline constexpr int chord_base_octave=2;
inline constexpr int chord_voice_count=5;
struct Chord { std::array<int,chord_voice_count> pitches{{-1,-1,-1,-1,-1}}; std::string name; int voices=4; };
// Fixed scale-degree voicings; optional per-pad variants never depend on history.
std::array<Chord,chord_pad_count> chord_palette(ChordSelection selection);
Chord chord_for(ChordSelection selection,int degree,ChordVariant variant);
class ChordEngine {
 std::recursive_mutex mutex_;
 MidiRouter& router_;
 std::array<Chord,chord_pad_count> palette_{};
 struct Part {
   std::array<int,chord_voice_count> pitches{};
   std::array<uint64_t,chord_voice_count> due{};
   int voices=0,pad=-1,division=0,timing=0;
   bool pending=false,releasing=false,sync=false;
   double beats=0;
 };
 // Router sources 2..6 and 7..11 are disjoint; 0/1 remain keyboard, 14 arp.
 std::array<Part,2> parts_{};
 int strum_ms_=0;
 int strum_division_=0;
 int strum_timing_=0;
 bool clock_sync_=false;
 uint64_t quarter_us_=500000;
 uint64_t now_=0;
 bool preview_=false;
 uint64_t preview_off_=0;
 uint32_t random_=0x95cb71a3;
 void sound(int contact,int pad);
 void retry_part(int contact);
public:
 explicit ChordEngine(MidiRouter& router):router_(router) { selection({}); }
 void selection(ChordSelection value);
 void preview(ChordSelection value,int pad);
 void press(int contact,int pad);
 void release(int contact);
 void stop();
 void retry();
 void clock_sync(bool enabled);
 void tick(uint64_t now_us,uint64_t quarter_us=0);
 const std::array<Chord,chord_pad_count>& palette() const { return palette_; }
};
}

#pragma once
#include <cstdint>
#include <array>
namespace sm {
enum class ArpPattern { Up, Down, UpDown, Random, Shuffle, Converge, Anchor, LowHigh, Mirror, Gravity, Pivot, OctaveEcho };
// Stored IDs 8 (Mirror) and 11 (OctaveEcho) remain reserved for old presets.
inline constexpr std::array<ArpPattern,10> arp_patterns{{ArpPattern::Up,ArpPattern::Down,ArpPattern::UpDown,
 ArpPattern::Random,ArpPattern::Shuffle,ArpPattern::Converge,ArpPattern::Anchor,ArpPattern::LowHigh,
 ArpPattern::Gravity,ArpPattern::Pivot}};
inline constexpr int arp_pattern_count=int(arp_patterns.size());
inline int arp_pattern_option(ArpPattern p) {
 for(int i=0;i<arp_pattern_count;++i) if(arp_patterns[i]==p) return i;
 return 0;
}
inline ArpPattern playable_arp_pattern(ArpPattern p) { return arp_patterns[arp_pattern_option(p)]; }
enum class ArpTiming { Straight, Dotted, Triplet };
enum class ArpClock { Tap, Midi };
struct ArpSettings {
 int low=3,high=5,division=8,probability=100,swing=0;
 int block_size=0;
 int gate=100;
 int gate_before_ratchet=0; // 0: no active Ratchet gate snapshot.
 int ratchet_every=0,ratchet_count=2;
 ArpPattern pattern=ArpPattern::Up;
 ArpTiming timing=ArpTiming::Straight;
 ArpClock clock=ArpClock::Tap;
 bool operator==(const ArpSettings& b) const {
   return low==b.low && high==b.high && division==b.division && probability==b.probability && swing==b.swing &&
     block_size==b.block_size && gate==b.gate && gate_before_ratchet==b.gate_before_ratchet && ratchet_every==b.ratchet_every && ratchet_count==b.ratchet_count && pattern==b.pattern && timing==b.timing && clock==b.clock;
 }
};
struct ArpPreset {
 ArpSettings settings{};
 int bpm=120; uint16_t selected=0; int page=0;
 bool operator==(const ArpPreset& b) const { return settings==b.settings && bpm==b.bpm && selected==b.selected && page==b.page; }
};
inline bool valid_arp_settings(ArpSettings s) {
 return s.low>=0 && s.high<=8 && s.low<=s.high && s.probability>=0 && s.probability<=100 && s.swing>=0 && s.swing<=50 &&
   (s.division==1 || s.division==2 || s.division==4 || s.division==8 || s.division==16 || s.division==32) &&
   s.gate_before_ratchet>=0 && s.gate_before_ratchet<=100 && s.gate_before_ratchet%5==0 &&
   (!s.gate_before_ratchet || (s.ratchet_every>0 && s.gate%5==0)) &&
   s.ratchet_every>=0 && s.ratchet_every<=16 && s.ratchet_count>=2 && s.ratchet_count<=4 && s.gate>=0 && s.gate<=100 && (s.block_size==0 || (s.block_size>=2 && s.block_size<=7)) &&
   int(s.pattern)>=0 && int(s.pattern)<12 && int(s.timing)>=0 && int(s.timing)<=2 && int(s.clock)>=0 && int(s.clock)<=1;
}
inline bool valid_arp_preset(ArpPreset s) {
 return valid_arp_settings(s.settings) && s.bpm>=40 && s.bpm<=300 && s.selected<=127 && s.page>=0 && s.page<10;
}
}

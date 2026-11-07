#pragma once
#include "music.hpp"
#include <mutex>
namespace sm {
struct ArpState {
 ArpSettings settings{}; uint16_t selected=0;
 bool enabled=false,suspended=false,running=false,clock_present=false;
 int playing=-1,bpm=120,clock_bpm=0;
};
// UI and clock worker may call concurrently. No GUI operations or allocations
// occur in the timing path; only the dedicated router source 14 is used.
class Arpeggiator {
 MidiRouter& router_; mutable std::mutex mutex_;
 ArpState state_{}; Selection selection_{};
 ArpPattern active_pattern_=ArpPattern::Up;
 std::array<int,117> pool_{}; int count_=0,index_=0,direction_=1,cursor_pitch_=-1;
 std::array<int,117> shuffle_{};
 int shuffle_size_=0,shuffle_position_=0,shuffle_last_=-1;
 bool anchor_free_=false;
 int low_high_phase_=0,motif_phase_=0;
 std::array<int,2> motif_pitches_{{-1,-1}};
 bool pivot_replace_first_=false;
 bool short_step_=false;
 int active_swing_=0;
 uint64_t swing_pair_=UINT64_MAX;
 unsigned block_step_=0,ratchet_step_=0;
 int ratchet_pitch_=-1,ratchet_total_=0,ratchet_index_=0,ratchet_gate_=100;
 uint64_t ratchet_start_=0,ratchet_length_=0;
 void cancel_ratchet() { ratchet_pitch_=-1; ratchet_total_=0; }
 void strike(uint64_t now,int pitch,uint64_t length);
 void advance_ratchet(uint64_t now);
 uint32_t random_=0x78534af1;
 uint64_t next_=0,off_=0,last_clock_=0,last_tap_=0,tap_sum_=0;
 unsigned tap_count_=0; double clock_us_=60000000.0/(120*24);
 uint64_t bpm_window_start_=0;
 unsigned bpm_window_ticks_=0;
 void clear_clock_meter();
 uint64_t clock_units_=0,boundary_=0; bool first_clock_=true;
 void silence(); void reset(); void reset_sequence(); void reset_pattern(); void rebuild(bool preserve_sequence=false,bool live_edit=false); void play(uint64_t now,uint64_t duration);
 uint32_t next_random();
 uint64_t duration() const; uint64_t step_duration(bool short_step) const; int units() const;
public:
 explicit Arpeggiator(MidiRouter& router):router_(router){}
 void enable(bool value); void suspend(bool value);
 void selection(Selection value); void settings(ArpSettings value);
 void toggle(int degree); void clear();
 void reset_defaults();
 void tap(uint64_t now_us);
 void tempo(int bpm);
 void realtime(uint8_t status,uint64_t now_us);
 void disconnected();
 void tick(uint64_t now_us);
 ArpState state() const;
 ArpPreset preset(int page=0) const;
 bool restore(ArpPreset value);
 uint64_t clock_quarter_us() const;
};
}

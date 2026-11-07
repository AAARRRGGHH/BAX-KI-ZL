#include "arpeggiator.hpp"
#include <algorithm>
#include <cmath>
namespace sm {
void Arpeggiator::clear_clock_meter() { state_.clock_bpm=0; bpm_window_start_=0; bpm_window_ticks_=0; }
void Arpeggiator::silence() { router_.release(14); state_.playing=-1; off_=router_.source_active(14)?1:0; }
uint32_t Arpeggiator::next_random() { random_^=random_<<13; random_^=random_>>17; random_^=random_<<5; return random_; }
void Arpeggiator::reset_sequence() {
 active_pattern_=state_.settings.pattern;
 index_=0; direction_=1; cursor_pitch_=-1; shuffle_size_=shuffle_position_=0; shuffle_last_=-1;
 anchor_free_=false; low_high_phase_=motif_phase_=0; motif_pitches_.fill(-1); pivot_replace_first_=false;
}
void Arpeggiator::reset_pattern() {
 reset_sequence();
 short_step_=false; active_swing_=state_.settings.swing; swing_pair_=UINT64_MAX;
 block_step_=ratchet_step_=0;
}
void Arpeggiator::reset() { cancel_ratchet(); silence(); next_=0; reset_pattern(); boundary_=0; clock_units_=0; first_clock_=true; }
void Arpeggiator::rebuild(bool preserve_sequence,bool live_edit) {
 const int previous_count=count_;
 const auto previous_pool=pool_;
 count_=0;
 const auto& intervals=modes()[selection_.mode].intervals;
 for(int octave=state_.settings.low;octave<=state_.settings.high;++octave)
   for(size_t i=0;i<intervals.size();++i) if(state_.selected&(1u<<i)) {
     int pitch=12*(octave+1)+selection_.root+intervals[i];
     if(pitch<=127) pool_[count_++]=pitch;
   }
 // Motif memory follows surviving pitches on live edits, and degree positions
 // when a scale/root change transposes the whole pool.
 for(auto& pitch:motif_pitches_) if(pitch>=0) {
   if(!count_) pitch=-1;
   else if(live_edit) {
     const auto found=std::lower_bound(pool_.begin(),pool_.begin()+count_,pitch);
     pitch=found==pool_.begin()+count_?pool_[count_-1]:*found;
   } else if(preserve_sequence) {
     const int rank=int(std::lower_bound(previous_pool.begin(),previous_pool.begin()+previous_count,pitch)-previous_pool.begin());
     pitch=pool_[std::min(count_-1,rank)];
   }
 }
 if(live_edit) {
   // Keep the current gate when its pitch still belongs to the selection.
   if(!count_ || (state_.playing>=0 && !std::binary_search(pool_.begin(),pool_.begin()+count_,state_.playing)) ||
      (ratchet_pitch_>=0 && !std::binary_search(pool_.begin(),pool_.begin()+count_,ratchet_pitch_))) { cancel_ratchet(); silence(); }
   if(count_ && cursor_pitch_>=0) {
     const int higher=int(std::upper_bound(pool_.begin(),pool_.begin()+count_,cursor_pitch_)-pool_.begin());
     const int lower=int(std::lower_bound(pool_.begin(),pool_.begin()+count_,cursor_pitch_)-pool_.begin())-1;
     switch(active_pattern_) {
     case ArpPattern::Up: index_=higher<count_?higher:0; break;
     case ArpPattern::Down: index_=count_-1-(lower>=0?lower:count_-1); break;
     case ArpPattern::UpDown:
       if(direction_>0) {
         if(higher<count_) index_=higher; else { direction_=-1; index_=std::max(0,lower); }
       } else {
         if(lower>=0) index_=lower; else { direction_=1; index_=std::min(count_-1,higher); }
       }
       break;
     default: index_%=count_; break;
     }
   }
   // Repair the unplayed part of SHUFFLE. Already consumed, surviving notes
   // stay consumed; newly added pitches enter the remaining bag once.
   if(active_pattern_==ArpPattern::Shuffle) {
     const int last_pitch=shuffle_last_>=0 && shuffle_last_<previous_count?previous_pool[shuffle_last_]:-1;
     const auto last=std::lower_bound(pool_.begin(),pool_.begin()+count_,last_pitch);
     shuffle_last_=last!=pool_.begin()+count_ && *last==last_pitch?int(last-pool_.begin()):-1;
     if(shuffle_position_<shuffle_size_) {
       std::array<int,117> remaining{}; int size=0;
       for(int i=shuffle_position_;i<shuffle_size_;++i) {
         const int pitch=previous_pool[shuffle_[i]];
         const auto found=std::lower_bound(pool_.begin(),pool_.begin()+count_,pitch);
         if(found!=pool_.begin()+count_ && *found==pitch) remaining[size++]=int(found-pool_.begin());
       }
       for(int i=0;i<count_;++i) if(!std::binary_search(previous_pool.begin(),previous_pool.begin()+previous_count,pool_[i])) {
         const int position=int(next_random()%(size+1));
         for(int j=size;j>position;--j) remaining[j]=remaining[j-1];
         remaining[position]=i; ++size;
       }
       shuffle_=remaining; shuffle_size_=size; shuffle_position_=0;
     } else shuffle_size_=shuffle_position_=0;
   }
   return;
 }
 if(preserve_sequence && cursor_pitch_>=0 && count_) {
   const auto previous=std::lower_bound(previous_pool.begin(),previous_pool.begin()+previous_count,cursor_pitch_);
   cursor_pitch_=pool_[std::min(count_-1,int(previous-previous_pool.begin()))];
 }
 cancel_ratchet(); silence();
 if(!preserve_sequence) { next_=0; reset_pattern(); }
 else if(count_!=previous_count) {
   shuffle_size_=shuffle_position_=0; shuffle_last_=-1;
   if(count_) {
     if(active_pattern_==ArpPattern::UpDown) index_=std::clamp(index_,0,count_-1);
     else index_%=count_;
   }
 }
}
int Arpeggiator::units() const {
 int n=768/state_.settings.division; // 192 internal units per quarter.
 if(state_.settings.timing==ArpTiming::Dotted) n=n*3/2;
 if(state_.settings.timing==ArpTiming::Triplet) n=n*2/3;
 return n;
}
uint64_t Arpeggiator::duration() const {
 double quarter=state_.settings.clock==ArpClock::Tap?60000000.0/state_.bpm:clock_us_*24;
 return uint64_t(std::llround(quarter*units()/192));
}
uint64_t Arpeggiator::step_duration(bool short_step) const {
 const auto base=duration(),long_step=(base*(100+active_swing_)+50)/100;
 return short_step?2*base-long_step:long_step;
}
ArpPreset Arpeggiator::preset(int page) const {
 std::lock_guard<std::mutex> lock(mutex_);
 return {state_.settings,state_.bpm,state_.selected,page};
}
bool Arpeggiator::restore(ArpPreset value) {
 if(!valid_arp_preset(value)) return false;
 value.settings.pattern=playable_arp_pattern(value.settings.pattern);
 // Keep legacy storage readable; adopt the current 5..100% wheel range only
 // when restoring playback, after the original data has passed validation.
 const auto percent=[](int v) { return std::clamp(((v+2)/5)*5,5,100); };
 value.settings.gate=percent(value.settings.gate);
 value.settings.probability=percent(value.settings.probability);
 if(value.settings.ratchet_every) {
   if(!value.settings.gate_before_ratchet) value.settings.gate_before_ratchet=value.settings.gate;
   value.settings.gate=std::min(value.settings.gate,50);
 } else value.settings.gate_before_ratchet=0;
 std::lock_guard<std::mutex> lock(mutex_);
 silence(); state_=ArpState{};
 state_.settings=value.settings; state_.bpm=value.bpm; state_.selected=value.selected;
 last_clock_=last_tap_=tap_sum_=0; tap_count_=0; clock_us_=60000000.0/(120*24); clear_clock_meter();
 reset(); rebuild(); return true;
}
void Arpeggiator::enable(bool value) {
 std::lock_guard<std::mutex> lock(mutex_); state_.enabled=value; state_.running=false; reset();
}
void Arpeggiator::suspend(bool value) {
 std::lock_guard<std::mutex> lock(mutex_); state_.suspended=value; reset();
}
void Arpeggiator::selection(Selection value) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(valid(value) && value!=selection_) { selection_=value; rebuild(true); }
}
void Arpeggiator::settings(ArpSettings value) {
 value.gate_before_ratchet=0; // Snapshot ownership stays with the running engine.
 if(!valid_arp_settings(value)) return;
 value.pattern=playable_arp_pattern(value.pattern);
 std::lock_guard<std::mutex> lock(mutex_);
 bool changed_clock=value.clock!=state_.settings.clock;
 const auto previous=state_.settings;
 if(!previous.ratchet_every && value.ratchet_every) {
   value.gate_before_ratchet=std::clamp(((value.gate+2)/5)*5,5,100);
   if(value.gate>50) value.gate=50;
 } else if(previous.ratchet_every && !value.ratchet_every) {
   value.gate=previous.gate_before_ratchet?previous.gate_before_ratchet:previous.gate;
   value.gate_before_ratchet=0;
 } else value.gate_before_ratchet=value.ratchet_every?previous.gate_before_ratchet:0;
 if(value.ratchet_every) value.gate=std::clamp(((value.gate+2)/5)*5,5,100);
 state_.settings=value;
 if(previous.block_size!=value.block_size) block_step_=0;
 if(previous.ratchet_every!=value.ratchet_every) ratchet_step_=0;
 if(changed_clock) { state_.running=false; last_clock_=0; state_.clock_present=false; clear_clock_meter(); reset(); }
 if(previous.division==value.division && previous.timing==value.timing && !changed_clock) {
   if(previous.low!=value.low || previous.high!=value.high) rebuild(true,true);
   return;
 }
 rebuild();
}
void Arpeggiator::toggle(int degree) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(degree<0 || degree>=int(modes()[selection_.mode].intervals.size())) return;
 state_.selected^=1u<<degree; rebuild(true,true);
}
void Arpeggiator::clear() { std::lock_guard<std::mutex> lock(mutex_); state_.selected=0; rebuild(true,true); }
void Arpeggiator::reset_defaults() {
 std::lock_guard<std::mutex> lock(mutex_);
 const auto clock=state_.settings.clock;
 state_.settings=ArpSettings{}; state_.settings.clock=clock;
 state_.selected=0; state_.bpm=120;
 last_tap_=tap_sum_=0; tap_count_=0;
 count_=0; reset();
 // Keep enable/suspend and the external transport/clock telemetry intact.
}
void Arpeggiator::tempo(int bpm) {
 if(bpm<40 || bpm>300) return;
 std::lock_guard<std::mutex> lock(mutex_); state_.bpm=bpm;
 last_tap_=tap_sum_=0; tap_count_=0;
}
void Arpeggiator::tap(uint64_t now) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(last_tap_ && now>last_tap_) {
   auto interval=now-last_tap_;
   if(interval>=200000 && interval<=1500000) {
     if(tap_count_>=4) { tap_sum_=0; tap_count_=0; }
     tap_sum_+=interval; ++tap_count_;
     state_.bpm=std::clamp(int(std::llround(60000000.0*tap_count_/tap_sum_)),40,300);
   } else { tap_sum_=0; tap_count_=0; }
 }
 // Match the tempo wheel: update BPM without cutting the current gate,
 // rephasing the next step, or restarting pattern/Swing state.
 last_tap_=now;
}
void Arpeggiator::realtime(uint8_t status,uint64_t now) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(state_.settings.clock!=ArpClock::Midi) return;
 if(status==0xf8) {
   // Measure a full quarter (24 clock intervals) for a stable display. This
   // telemetry is independent of the existing timing filter and keeps running
   // while playback is disabled or the setup menu suspends the arpeggiator.
   if(!bpm_window_ticks_ || now<=bpm_window_start_ ||
      (last_clock_ && now>last_clock_+500000)) {
     bpm_window_start_=now; bpm_window_ticks_=1; state_.clock_bpm=0;
   } else if(++bpm_window_ticks_>=25) {
     state_.clock_bpm=int(std::llround(60000000.0/double(now-bpm_window_start_)));
     bpm_window_start_=now; bpm_window_ticks_=1;
   }
   if(last_clock_ && now>last_clock_) {
     const auto gap=now-last_clock_;
     if(gap>=2000 && gap<=150000) clock_us_=.8*clock_us_+.2*gap;
   }
   last_clock_=now; state_.clock_present=true;
   if(first_clock_) first_clock_=false; else clock_units_+=800;
 } else if(status==0xfa) { reset(); state_.running=true; last_clock_=0; clear_clock_meter(); }
 else if(status==0xfb) { state_.running=true; last_clock_=0; first_clock_=true; clock_units_=boundary_; clear_clock_meter(); }
 else if(status==0xfc || status==0xff) { state_.running=false; cancel_ratchet(); silence(); }
}
void Arpeggiator::disconnected() {
 std::lock_guard<std::mutex> lock(mutex_);
 last_clock_=0; state_.clock_present=false;
 clear_clock_meter();
 if(state_.settings.clock==ArpClock::Midi) { state_.running=false; reset(); }
}
void Arpeggiator::play(uint64_t now,uint64_t length) {
 // Adopt the latest UI choice only on a regular step, preserving gate and swing.
 if(active_pattern_!=state_.settings.pattern) reset_sequence();
 cancel_ratchet(); silence();
 bool ratchet=false;
 if(state_.settings.ratchet_every) {
   ratchet_step_=(ratchet_step_+1)%unsigned(state_.settings.ratchet_every); ratchet=ratchet_step_==0;
 }
 if(!count_ || router_.source_active(14)) return;
 int current=0;
 const auto rank_of=[&](int pitch) {
   return std::min(count_-1,int(std::lower_bound(pool_.begin(),pool_.begin()+count_,pitch)-pool_.begin()));
 };
 const auto other=[&](int first,int second) {
   int candidates=0; for(int i=0;i<count_;++i) if(i!=first && i!=second) ++candidates;
   if(!candidates) return first>=0?first:0;
   int rank=int(next_random()%candidates);
   for(int i=0;i<count_;++i) if(i!=first && i!=second && rank--==0) return i;
   return 0;
 };
 switch(active_pattern_) {
 case ArpPattern::Mirror: case ArpPattern::OctaveEcho: // Legacy presets fall back to UP.
 case ArpPattern::Up: current=index_++%count_; break;
 case ArpPattern::Down: current=count_-1-index_++%count_; break;
 case ArpPattern::UpDown:
   current=std::clamp(index_,0,count_-1);
   if(count_>1) { if(index_>=count_-1) direction_=-1; else if(index_<=0) direction_=1; index_+=direction_; }
   break;
 case ArpPattern::Random:
   current=int(next_random()%count_); break;
 case ArpPattern::Shuffle:
   if(shuffle_position_>=shuffle_size_) {
     shuffle_size_=count_; shuffle_position_=0;
     for(int i=0;i<count_;++i) shuffle_[i]=i;
     for(int i=count_-1;i>0;--i) std::swap(shuffle_[i],shuffle_[next_random()%(i+1)]);
     if(count_>1 && shuffle_[0]==shuffle_last_) std::swap(shuffle_[0],shuffle_[1]);
   }
   current=shuffle_[shuffle_position_++]; shuffle_last_=current; break;
 case ArpPattern::Converge: {
   int rank=index_++%count_;
   current=(rank&1)?count_-1-rank/2:rank/2;
   break;
 }
 case ArpPattern::LowHigh: {
   const int low=pool_[0];
   const int high=low+12*(state_.settings.high-state_.settings.low);
   const auto found=std::lower_bound(pool_.begin(),pool_.begin()+count_,high);
   const int high_index=found!=pool_.begin()+count_ && *found==high?int(found-pool_.begin()):-1;
   const int phase=low_high_phase_; low_high_phase_=(low_high_phase_+1)%3;
   if(phase==0) current=0;
   else if(phase==1) current=high_index;
   else {
     const int candidates=count_-1-(high_index>0?1:0);
     current=-1;
     if(candidates>0) {
       int rank=int(next_random()%candidates);
       for(int i=1;i<count_;++i) if(i!=high_index && rank--==0) { current=i; break; }
     }
   }
   break;
 }
 case ArpPattern::Gravity:
   if(motif_phase_==0) {
     current=count_>=3?2+int(next_random()%(count_-2)):count_-1; motif_pitches_[0]=pool_[current];
   } else if(motif_phase_==1) current=rank_of(motif_pitches_[0])/2;
   else current=0;
   motif_phase_=(motif_phase_+1)%3;
   break;
 case ArpPattern::Pivot: {
   int first=motif_pitches_[0]<0?int(next_random()%count_):rank_of(motif_pitches_[0]);
   int second=motif_pitches_[1]<0?other(first,-1):rank_of(motif_pitches_[1]);
   if(count_>1 && first==second) second=other(first,-1);
   motif_pitches_[0]=pool_[first]; motif_pitches_[1]=pool_[second];
   current=motif_phase_==0?first:second;
   if(motif_phase_==1) {
     if(count_>2) motif_pitches_[pivot_replace_first_?0:1]=pool_[other(first,second)];
     pivot_replace_first_=!pivot_replace_first_;
   }
   motif_phase_=1-motif_phase_;
   break;
 }
 case ArpPattern::Anchor:
   current=anchor_free_ && count_>1?1+int(next_random()%(count_-1)):0;
   anchor_free_=!anchor_free_;
   break;
 }
 // Bound sequence counters during arbitrarily long sessions.
 cursor_pitch_=current>=0?pool_[current]:pool_[0]; // Keep the slot grid even when no third note exists.
 if(index_>1000000) index_%=count_;
 if(state_.settings.block_size) {
   const unsigned size=unsigned(state_.settings.block_size);
   const bool rest=block_step_==size;
   block_step_=(block_step_+1)%(size+1);
   if(rest) return;
 }
 if(current<0) return;
 if(state_.settings.probability<100) {
   if(int(next_random()%100)>=state_.settings.probability) return;
 }
 if(!state_.settings.gate) return; // Silent slot still advances the pattern.
 ratchet_pitch_=pool_[current]; ratchet_total_=ratchet?state_.settings.ratchet_count:1;
 ratchet_index_=0; ratchet_start_=now; ratchet_length_=length; ratchet_gate_=state_.settings.gate;
 strike(now,ratchet_pitch_,length/ratchet_total_);
}
void Arpeggiator::strike(uint64_t now,int pitch,uint64_t length) {
 silence(); if(router_.source_active(14)) return;
 state_.playing=pitch; router_.press(14,pitch);
 off_=now+std::min(length,std::max<uint64_t>(1000,length*ratchet_gate_/100));
}
void Arpeggiator::advance_ratchet(uint64_t now) {
 if(ratchet_total_<2 || now<ratchet_start_) return;
 const uint64_t elapsed=now-ratchet_start_;
 if(elapsed>=ratchet_length_) { cancel_ratchet(); return; }
 const int index=int(((elapsed+1)*ratchet_total_-1)/ratchet_length_);
 if(index<=ratchet_index_) return;
 ratchet_index_=index;
 const auto start=ratchet_length_*index/ratchet_total_,end=ratchet_length_*(index+1)/ratchet_total_;
 const auto gate=std::min(end-start,std::max<uint64_t>(1000,(end-start)*ratchet_gate_/100));
 // Skip missed attacks after a stalled worker; never emit a burst of old notes.
 if(elapsed>=start+gate) return;
 strike(now,ratchet_pitch_,end-start);
 off_=ratchet_start_+start+gate;
}
void Arpeggiator::tick(uint64_t now) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(off_ && now>=off_) silence();
 if(last_clock_ && now>last_clock_+500000) {
   state_.clock_present=false;
   clear_clock_meter();
   if(state_.settings.clock==ArpClock::Midi) { state_.running=false; cancel_ratchet(); silence(); }
 }
 if(!state_.enabled || state_.suspended) return;
 if(state_.settings.clock==ArpClock::Tap) {
   if(!count_ && !next_) return; // Start the internal grid on the first selection.
   if(!next_ || now>=next_) {
     if(!short_step_) active_swing_=state_.settings.swing;
     const auto length=step_duration(short_step_);
     play(now,length);
     // Preserve phase during normal jitter, skip missed steps after a stall.
     next_=next_ && now-next_<length?next_+length:now+length;
     short_step_=!short_step_;
   }
 } else if(state_.running && state_.clock_present && !first_clock_ && now>=last_clock_) {
   // Keep the measured clock running through short packet delays. Capping
   // prediction at one pulse stalls the sequence during BLE batching, then
   // compresses subsequent steps when those pulses finally arrive. The
   // existing 500-ms loss timeout bounds holdover; Stop still acts immediately.
   // Late packets never trigger a burst of historical notes.
   // Hundredths of a base unit preserve fractional-clock Swing positions.
   double position=double(clock_units_)+double(now-last_clock_)*800/clock_us_;
   if(position>=boundary_) {
     const uint64_t pair_units=uint64_t(units())*200,pair=uint64_t(position)/pair_units;
     if(pair!=swing_pair_) { swing_pair_=pair; active_swing_=state_.settings.swing; }
     const uint64_t start=pair*pair_units,middle=start+uint64_t(units())*(100+active_swing_);
     const bool second=position>=middle;
     play(now,step_duration(second)); boundary_=second?start+pair_units:middle;
   }
 }
 if(state_.settings.clock==ArpClock::Tap || (state_.running && state_.clock_present)) advance_ratchet(now);
}
ArpState Arpeggiator::state() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
uint64_t Arpeggiator::clock_quarter_us() const { std::lock_guard<std::mutex> lock(mutex_); return uint64_t(std::llround(clock_us_*24)); }
}

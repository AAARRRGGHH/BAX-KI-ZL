#pragma once
#include "music.hpp"
#include "motif.hpp"
namespace sm::ripples {
struct Origin { int process=-1,cell=-1; uint32_t motif=1,parent=0,impulse=1; };
struct MidiTrace { uint64_t time=0; MidiEvent event{}; uint32_t ticket=0; Origin origin{}; };
struct PhraseTrace { uint64_t time=0; uint32_t grid=0,cycle=0; int process=0; Motif motif{}; bool sounding=false; double energy=0,rate=0,phase_gap=0,coupling=0; };
struct Observer {
 virtual ~Observer()=default;
 virtual void midi(const MidiTrace&) {}
 virtual void phrase(const PhraseTrace&) {}
};
struct SchedulerStats { unsigned ons=0,offs=0,joined=0,dropped=0,rejected=0,max_polyphony=0,panic_cleared=0; };
// Each accepted intent owns a slot, even when it joins an existing physical note.
// A group's last owner releases its note. Failed Offs retain their slot for retry.
class Scheduler {
 MidiTransport& output_; Observer* observer_;
 struct Owner { bool used=false; uint8_t group=0; uint64_t off=0; };
 struct Group { bool used=false; uint8_t pitch=0; uint32_t ticket=0; Origin origin{}; };
 std::array<Owner,8> owners_{}; std::array<Group,8> groups_{};
 std::array<uint64_t,32> rate_times_{}; unsigned rate_head_=0,rate_count_=0;
 uint64_t burst_time_=UINT64_MAX; unsigned burst_count_=0; uint32_t serial_=0;
 uint64_t now_=0;
 bool panic_pending_=false; SchedulerStats stats_{};
 bool emit(uint64_t now,MidiEvent event,const Group& group);
public:
 Scheduler(MidiTransport& output,Observer* observer=nullptr):output_(output),observer_(observer){}
 bool strike(uint64_t now,int note,int velocity,uint64_t length,Origin origin);
 void service(uint64_t now); void stop(uint64_t now);
 void release(uint64_t now); // Normal Note Off, preserving synth decay tails.
 bool sounding(int note) const; unsigned polyphony() const;
 uint64_t time()const{return now_;}
 bool clean() const { return polyphony()==0 && !panic_pending_; }
 SchedulerStats stats() const { return stats_; }
};
}

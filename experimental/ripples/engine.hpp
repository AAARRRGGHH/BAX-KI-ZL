#pragma once
#include "scheduler.hpp"
namespace sm::ripples {
struct Config { Selection scale{}; uint32_t seed=41; int bpm=120,low=48,high=84; bool coupled=true; };
struct EngineStats { unsigned grids=0,phrases=0,mutations=0,recalls=0,rests=0,stalls=0; };
class Engine {
 Config config_; Observer* observer_; Scheduler scheduler_;
 std::array<int,128> pool_{}; int count_=0,anchor_=0,initial_velocity_=100;
 std::array<Motif,16> bank_{}; unsigned bank_count_=1,bank_head_=1; uint32_t next_motif_=2;
 struct Process { bool active=false,sounding=true; double phase=0,rate=.25,energy=1; unsigned cycle=0,next=0,last_edit=0; int last_index=0; Motif motif{}; };
 std::array<Process,2> processes_{};
 uint64_t now_=0,logical_=0,started_=0; uint32_t grid_=0; bool initialized_=false,running_=false;
 EngineStats stats_{};
 double coupling_=0;
 void grid(uint64_t now); void phrase(int process,uint64_t now); void play(int process,int cell,uint64_t now);
 Motif transform(const Motif& source,uint32_t random); int choose_pitch(int process,int desired) const;
public:
 Engine(Config config,MidiTransport& output,Observer* observer=nullptr);
 bool stone(uint64_t now,int note,int velocity);
 bool advance(uint64_t now); void stop(uint64_t now);
 bool reset(uint64_t now);
 EngineStats stats() const { return stats_; } SchedulerStats midi_stats() const { return scheduler_.stats(); }
 bool clean() const { return scheduler_.clean(); }
};
static_assert(sizeof(Engine)<=16*1024,"Ripples prototype exceeded its fixed-state budget");
}

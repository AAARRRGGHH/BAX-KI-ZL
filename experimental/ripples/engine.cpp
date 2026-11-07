#include "engine.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace sm::ripples {
namespace { constexpr double tau=6.28318530717958647692; }
Engine::Engine(Config config,MidiTransport& output,Observer* observer):config_(config),observer_(observer),scheduler_(output,observer) {
 if(!valid(config.scale) || config.bpm<40 || config.bpm>300 || config.low<0 || config.high>127 || config.low>config.high) throw std::invalid_argument("Ripples configuration");
 for(int n=config.low;n<=config.high;++n) for(int pc:modes()[config.scale.mode].intervals) if((n-config.scale.root+132)%12==pc) { pool_[count_++]=n; break; }
 if(!count_) throw std::invalid_argument("Empty Ripples pitch range");
 bank_[0]=initial_motif(config.seed);
}
bool Engine::stone(uint64_t now,int note,int velocity) {
 // Phase 2 intentionally accepts one stone only. Multi-impulse influence is a
 // later experiment, not a hidden reset of this first musical history.
 if(initialized_ || velocity<1 || velocity>127) return false;
 int index=-1; for(int i=0;i<count_;++i) if(pool_[i]==note) index=i;
 if(index<0) return false;
 initialized_=running_=true; now_=logical_=started_=now; grid_=0; anchor_=index; initial_velocity_=velocity;
 processes_[0].active=true; processes_[0].last_index=index; processes_[0].motif=bank_[0]; processes_[0].next=1;
 processes_[1].last_index=std::min(count_-1,index+2); processes_[1].motif=bank_[0]; processes_[1].rate=.20;
 phrase(0,now); processes_[0].next=1;
 scheduler_.strike(now,note,velocity,uint64_t(60000000.0/config_.bpm*.3),{-1,0,1,0,1});
 return true;
}
Motif Engine::transform(const Motif& source,uint32_t random) {
 Motif result=source; result.parent=source.id; result.id=next_motif_++;
 const unsigned cell=1+(random%(result.length-1)); auto& c=result.cells[cell];
 switch((random>>8)%3) {
 case 0: c.degree=int8_t(std::clamp(int(c.degree)+((random&16)?1:-1),-2,4)); break;
 case 1: {
   const int low=result.cells[cell-1].position+6,high=cell+1<result.length?result.cells[cell+1].position-6:84;
   c.position=uint8_t(std::clamp(int(c.position)+((random&32)?6:-6),low,high)); break;
 }
 case 2: if(cell!=result.length-1) c.rest=!c.rest; else c.accent=uint8_t(c.accent==78?90:78); break;
 }
 // Do not let repeated transformations erase the protected starting contour.
 if(differences(result,bank_[0])>2) { const auto id=result.id; result=bank_[0]; result.parent=source.id; result.id=id; result.cells[cell].degree=int8_t(std::clamp(int(result.cells[cell].degree)+((random&16)?1:-1),-2,4)); }
 bank_[bank_head_]=result; bank_head_=bank_head_%15+1; bank_count_=std::min(16u,bank_count_+1); ++stats_.mutations; return result;
}
void Engine::phrase(int i,uint64_t now) {
 auto& p=processes_[i]; const auto key=hash(config_.seed^uint32_t(i*0x45d9f3bu)^uint32_t(p.cycle*0x9e3779b9u));
 if(p.cycle && p.cycle%7==6) { p.motif=bank_[0]; ++stats_.recalls; }
 else if(p.cycle>=3 && p.cycle-p.last_edit>=3 && key%100<40) { p.motif=transform(p.motif,hash(key)); p.last_edit=p.cycle; }
 else if(p.cycle>=3 && key%100>=40 && key%100<60) { p.motif=bank_[hash(key^0x25a19u)%bank_count_]; ++stats_.recalls; }
 // A second voice begins with a shared existing variant, not a new random song.
 if(i==1 && p.cycle==0) p.motif=processes_[0].motif;
 const double beat=grid_/96.; const double activity=.5+.5*std::sin(tau*beat/64.);
 p.energy=.3+.7*activity;
 const double probability=i==0?.65+.3*activity:.18+.65*activity;
 p.sounding=p.cycle==0 || double(hash(key^0xd1b54a35u)%10000)/10000<probability;
 if(!p.sounding) ++stats_.rests;
 p.next=0; ++stats_.phrases;
 const double gap=processes_[1].phase-std::floor(processes_[1].phase)-processes_[0].phase+std::floor(processes_[0].phase);
 if(observer_) observer_->phrase({now,grid_,p.cycle,i,p.motif,p.sounding,p.energy,p.rate,std::remainder(gap,1.),coupling_});
}
int Engine::choose_pitch(int i,int desired) const {
 const auto& p=processes_[i]; desired=std::clamp(desired,0,count_-1);
 int best=desired; double score=1e9;
 for(int candidate=std::max(0,desired-2);candidate<=std::min(count_-1,desired+2);++candidate) {
   double cost=std::abs(candidate-desired)*4.+std::abs(candidate-p.last_index)*.3;
   if(std::abs(candidate-p.last_index)>4) cost+=3;
   if(scheduler_.sounding(pool_[candidate])) cost+=8;
   for(int n=0;n<count_;++n) if(scheduler_.sounding(pool_[n])) {
     const int interval=std::abs(pool_[candidate]-pool_[n])%12;
     if(interval==1 || interval==11 || interval==6) cost+=2.5;
   }
   if(cost<score) { score=cost; best=candidate; }
 }
 return best;
}
void Engine::play(int i,int cell,uint64_t now) {
 auto& p=processes_[i]; const auto c=p.motif.cells[cell];
 if(!p.sounding || c.rest) return;
 const int target=anchor_+c.degree+(i==1?2:0),index=choose_pitch(i,target);
 const int end=cell+1<p.motif.length?p.motif.cells[cell+1].position:96;
 const double beats=(end-c.position)/96./p.rate;
 const double length=std::clamp(beats*c.gate/100.,.12,1.15);
 const double breathing=.86+.14*p.energy;
 const int velocity=std::clamp(int(std::lround(initial_velocity_*c.accent/100.*breathing*(i==1?.74:1.))),1,127);
 if(scheduler_.strike(now,pool_[index],velocity,uint64_t(std::llround(length*60000000./config_.bpm)),{i,cell,p.motif.id,p.motif.parent,1})) p.last_index=index;
}
void Engine::grid(uint64_t now) {
 ++grid_; ++stats_.grids;
 const double beat=grid_/96.;
 if(!processes_[1].active && grid_>=8*96) { processes_[1].active=true; phrase(1,now); play(1,0,now); processes_[1].next=1; }
 const double envelope=.5-.5*std::cos(tau*beat/32.);
 coupling_=config_.coupled?.075*envelope:0;
 double phase[2]={processes_[0].phase,processes_[1].phase};
 for(int i=0;i<2;++i) if(processes_[i].active) {
   auto& p=processes_[i]; const double base=i==0?.25:.20;
   double correction=0;
   if(processes_[1-i].active) correction=coupling_*std::sin(tau*std::remainder(phase[1-i]-phase[i],1.));
   p.rate=std::clamp(base+correction,base*.55,base*1.45);
   p.phase+=p.rate/96.;
   if(std::abs(p.phase-std::round(p.phase))<1e-10) p.phase=std::round(p.phase);
   if(unsigned(std::floor(p.phase))>p.cycle) { p.cycle=unsigned(std::floor(p.phase)); phrase(i,now); }
   const double local=(p.phase-std::floor(p.phase))*96;
   while(p.next<p.motif.length && local+1e-9>=p.motif.cells[p.next].position) { const int cell=int(p.next++); play(i,cell,now); }
 }
}
bool Engine::advance(uint64_t target) {
 if(target<now_) return false;
 if(!initialized_ || !running_) { scheduler_.service(target); now_=logical_=target; return true; }
 if(target-logical_>32000) {
   // Live callers must not replay a backlog after a stall. Normal offline and
   // jitter tests use <=32 ms advances and evaluate every logical millisecond.
   scheduler_.service(target); ++stats_.stalls;
   const auto skipped=uint32_t((target-logical_)*config_.bpm*96/60000000ull);
   grid_+=skipped;
   for(int i=0;i<2;++i) if(processes_[i].active) {
     auto& p=processes_[i]; p.phase+=skipped/96.*p.rate; p.cycle=unsigned(p.phase);
     p.next=0; const double local=(p.phase-std::floor(p.phase))*96;
     while(p.next<p.motif.length && p.motif.cells[p.next].position<=local) ++p.next;
   }
   now_=logical_=target; return true;
 }
 while(logical_+1000<=target) {
   logical_+=1000; scheduler_.service(logical_);
   if(running_) while(uint64_t(grid_+1)*60000000ull<=uint64_t(logical_-started_)*config_.bpm*96) grid(logical_);
 }
 now_=target; return true;
}
void Engine::stop(uint64_t now) {
 now=std::max(now,now_);
 // Stop wins over an attack scheduled exactly at the stop boundary.
 if(now>now_) advance(now-1);
 running_=false; now_=logical_=now; scheduler_.stop(now);
}
bool Engine::reset(uint64_t now) {
 if(now<now_) return false;
 stop(now); if(!scheduler_.clean()) return false;
 bank_={}; bank_[0]=initial_motif(config_.seed); bank_count_=bank_head_=1; next_motif_=2;
 processes_={}; grid_=0; stats_={}; coupling_=0; initialized_=running_=false; return true;
}
}

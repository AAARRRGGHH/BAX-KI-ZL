#include "scheduler.hpp"
#include <algorithm>
namespace sm::ripples {
bool Scheduler::emit(uint64_t now,MidiEvent event,const Group& group) {
 if(!output_.send(event)) { ++stats_.rejected; return false; }
 if(observer_) observer_->midi({now,event,group.ticket,group.origin});
 return true;
}
bool Scheduler::sounding(int note) const { for(const auto& g:groups_) if(g.used && g.pitch==note) return true; return false; }
unsigned Scheduler::polyphony() const { unsigned n=0; for(const auto& g:groups_) n+=g.used; return n; }
bool Scheduler::strike(uint64_t now,int note,int velocity,uint64_t length,Origin origin) {
 if(now<now_ || panic_pending_ || note<0 || note>127 || velocity<1 || velocity>127 || !length || length>UINT64_MAX-now) return false;
 now_=now;
 int owner=-1,group=-1,free_group=-1;
 for(int i=0;i<8;++i) { if(!owners_[i].used && owner<0) owner=i; if(groups_[i].used && groups_[i].pitch==note) group=i; if(!groups_[i].used) free_group=i; }
 if(owner<0) { ++stats_.dropped; return false; }
 if(group>=0) { owners_[owner]={true,uint8_t(group),now+length}; ++stats_.joined; return true; }
 while(rate_count_ && now>=rate_times_[rate_head_]+1000000) { rate_head_=(rate_head_+1)%32; --rate_count_; }
 if(burst_time_!=now) { burst_time_=now; burst_count_=0; }
 if(free_group<0 || rate_count_>=32 || burst_count_>=4) { ++stats_.dropped; return false; }
 Group g{true,uint8_t(note),++serial_,origin};
 if(!emit(now,{MidiKind::NoteOn,1,uint8_t(note),uint8_t(velocity)},g)) return false;
 groups_[free_group]=g; owners_[owner]={true,uint8_t(free_group),now+length};
 rate_times_[(rate_head_+rate_count_)%32]=now; ++rate_count_; ++burst_count_; ++stats_.ons;
 stats_.max_polyphony=std::max(stats_.max_polyphony,polyphony()); return true;
}
void Scheduler::service(uint64_t now) {
 if(now<now_) return;
 now_=now;
 for(int i=0;i<8;++i) if(owners_[i].used && now>=owners_[i].off) {
   const int group=owners_[i].group; bool other=false;
   for(int j=0;j<8;++j) if(j!=i && owners_[j].used && owners_[j].group==group) other=true;
   if(other) { owners_[i].used=false; continue; }
   if(emit(now,{MidiKind::NoteOff,1,groups_[group].pitch,0},groups_[group])) { ++stats_.offs; owners_[i].used=false; groups_[group].used=false; }
 }
 if(panic_pending_ && emit(now,{MidiKind::AllNotesOff,1,0,0},{})) {
   stats_.panic_cleared+=polyphony(); groups_={}; owners_={}; panic_pending_=false;
 }
}
void Scheduler::stop(uint64_t now) { now=std::max(now,now_); panic_pending_=true; for(auto& o:owners_) if(o.used) o.off=now; service(now); }
void Scheduler::release(uint64_t now){now=std::max(now,now_);for(auto& owner:owners_)if(owner.used)owner.off=now;service(now);}
}

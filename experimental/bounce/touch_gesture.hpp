#pragma once
#include "world.hpp"
#include <functional>
namespace sm::bounce {
// Defer a stationary first touch briefly: a valid two-finger tap must not
// create balls or recharge a ball underneath it. Moving immediately becomes play.
class TouchGesture {
public:
 enum class Kind {Down,Move,Up,Menu};
 struct Action {Kind kind;int contact;Vec p;uint64_t time;};
 using Sink=std::function<void(Action)>;
private:
 struct Finger {bool used=false,down=false,emitted=false;int64_t id=0;Vec start{},last{};uint64_t began=0,last_time=0;};
 std::array<Finger,2> fingers_{};Sink sink_;bool pair_=false;
 int find(int64_t id)const{for(int i=0;i<2;++i)if(fingers_[i].used && fingers_[i].id==id)return i;return -1;}
 void deliver(int i){auto& f=fingers_[i];if(!f.used)return;if(!f.emitted){sink_({Kind::Down,i,f.start,f.began});f.emitted=true;sink_({Kind::Move,i,f.last,f.last_time});}if(!f.down){sink_({Kind::Up,i,f.last,f.last_time});f={};}}
 void flush(){pair_=false;for(int i=0;i<2;++i)deliver(i);}
public:
 explicit TouchGesture(Sink sink):sink_(std::move(sink)){}
 void down(int64_t id,Vec p,uint64_t now){
   if(find(id)>=0){return;}
   int index=-1;for(int i=0;i<2;++i)if(!fingers_[i].used){index=i;break;}if(index<0)return;
   fingers_[index]={true,true,false,id,p,p,now,now};auto& other=fingers_[1-index];
   if(other.used && other.down && !other.emitted && now-other.began<=120)pair_=true;
   else if(other.used)flush();
 }
 void move(int64_t id,Vec p,uint64_t now,bool notify=true){
   const int i=find(id);if(i<0)return;auto& f=fingers_[i];f.last=p;f.last_time=now;
   if(!f.emitted && std::hypot(p.x-f.start.x,p.y-f.start.y)>10){flush();return;}
   if(f.emitted && notify)sink_({Kind::Move,i,p,now});
 }
 void up(int64_t id,Vec p,uint64_t now){
   move(id,p,now,false);const int i=find(id);if(i<0)return;auto& f=fingers_[i];f.down=false;f.last_time=now;
   if(pair_){
     if(now-std::min(fingers_[0].began,fingers_[1].began)>280){flush();return;}
     if(!fingers_[0].down && !fingers_[1].down){fingers_={};pair_=false;sink_({Kind::Menu,-1,p,now});}return;
   }
   deliver(i);
 }
 void tick(uint64_t now){
   if(pair_){if(now-std::min(fingers_[0].began,fingers_[1].began)>280)flush();return;}
   for(int i=0;i<2;++i)if(fingers_[i].used && !fingers_[i].emitted && now-fingers_[i].began>=120)deliver(i);
 }
 void cancel(uint64_t now){for(int i=0;i<2;++i)if(fingers_[i].used && fingers_[i].emitted)sink_({Kind::Up,i,fingers_[i].last,now});fingers_={};pair_=false;}
};
}

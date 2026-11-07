#include "world.hpp"
#include <algorithm>
#include <cmath>
namespace sm::bounce {
namespace { constexpr double pi=3.141592653589793; double length(Vec p){return std::hypot(p.x,p.y);} double dot(Vec a,Vec b){return a.x*b.x+a.y*b.y;} Vec limit(Vec p,double max){const double square=dot(p,p);return square>max*max?p*(max/std::sqrt(square)):p;} }
uint64_t World::period_us() const {
 return clock_.enabled() && clock_.tempo_known()?uint64_t(std::llround(clock_.quarter_us()*step_units()/192.)):period_;
}
int World::step_units()const{int units=768/settings_.division;if(settings_.timing==ArpTiming::Dotted)units=units*3/2;else if(settings_.timing==ArpTiming::Triplet)units=units*2/3;return units;}
uint64_t World::gate_us()const{const auto period=period_us();return std::min(period,std::max<uint64_t>(1000,period*settings_.gate/100));}
void World::clock_sync(bool enabled){if(enabled==clock_.enabled())return;silence();clock_.enable(enabled);external_active_=false;grid_origin_=uint64_t(std::llround(time_*1000000));}
void World::update_clock_source(){
 const bool active=clock_.ready(wall_now_);
 if(active==external_active_)return;
 external_active_=active;
 // Keep already sounding notes and their regular Gate/Note Off. Discard only
 // queued attacks from the old grid so Start/Stop cannot replay stale hits.
 pending_={};grid_origin_=uint64_t(std::llround(time_*1000000));
}
void World::realtime(uint8_t status,uint64_t now){
 if(!clock_.enabled())return;
 clock_.realtime(status,now);wall_now_=std::max(wall_now_,now);
 if(status==0xfa || status==0xfb || status==0xff)pending_={};
 update_clock_source();
}
void World::clock_tick(uint64_t now){clock_.service(now);wall_now_=now;update_clock_source();}
bool World::settings(Settings value){
 if(!valid_settings(value))return false;
 const bool retime=value.bpm!=settings_.bpm || value.division!=settings_.division || value.timing!=settings_.timing || value.quantized!=settings_.quantized;
 if(retime || value.scale!=settings_.scale || value.low!=settings_.low || value.high!=settings_.high)pending_={};
 if(retime)grid_origin_=uint64_t(std::llround(time_*1000000));
 if(value.scale!=settings_.scale)flashes_={};
 if(value.energy_seconds!=settings_.energy_seconds)cached_dt_=-1;
 chords_.configure(value.scale,value.low,value.high);
 settings_=value;
 double period=240000000./value.bpm/value.division;if(value.timing==ArpTiming::Dotted)period*=1.5;if(value.timing==ArpTiming::Triplet)period*=2./3;period_=uint64_t(std::llround(period));return true;
}
void ChordSearch::configure(Selection scale,int low,int high){
 if(scale==scale_ && low==low_ && high==high_)return;
 candidates_.clear();scale_=scale;low_=low;high_=high;
 if(!valid(scale) || low<0 || high>8 || low>high)return;
 // At most 7 * 9^3 candidates. Reserve once, outside the performance path.
 // On ESP32 this large allocation also goes to PSRAM rather than consuming
 // the small internal-RAM allocator's preferred size class.
 if(candidates_.capacity()<5103)candidates_.reserve(5103);
 const auto& melody=modes()[scale.mode].intervals;
 const int bottom=12*(low+1)+scale.root,top=std::min(127,12*(high+1)+scale.root+melody.back());
 const int basis=scale.mode==9?0:scale.mode==10 || scale.mode==11?1:scale.mode;
 const auto& intervals=modes()[basis].intervals;
 std::array<bool,12> allowed{};for(int interval:melody)allowed[(scale.root+interval)%12]=true;
 for(int degree=0;degree<7;++degree){
   const int root=(scale.root+intervals[degree])%12,third=(scale.root+intervals[(degree+2)%7])%12,fifth=(scale.root+intervals[(degree+4)%7])%12;
   const int third_interval=(third-root+12)%12,fifth_interval=(fifth-root+12)%12;
   // Automatic collision harmonies favor stable major/minor triads. In
   // pentatonic/blues modes the fallback basis must not add foreign notes.
   if((third_interval!=3 && third_interval!=4) || fifth_interval!=7 || !allowed[root] || !allowed[third] || !allowed[fifth])continue;
   for(int a=bottom;a<=top;++a)if(a%12==root)
   for(int b=bottom;b<=top;++b)if(b%12==third)
   for(int c=bottom;c<=top;++c)if(c%12==fifth){
     std::array<int,3> chord{{a,b,c}};std::sort(chord.begin(),chord.end());
     candidates_.push_back({{{uint8_t(chord[0]),uint8_t(chord[1]),uint8_t(chord[2])}},uint16_t(a+b+c)});
   }
 }
}
std::array<int,3> ChordSearch::find(const int* hits,size_t count)const{
 std::array<int,3> best{{47,50,54}};if(!hits || !count || count>48)return best;
 std::array<uint16_t,128> distances{};std::array<bool,128> present{};int sum=0;
 for(size_t i=0;i<count;++i){if(hits[i]<0 || hits[i]>127)return best;present[hits[i]]=true;sum+=hits[i];}
 int last=-128;for(int p=0;p<128;++p){if(present[p])last=p;distances[p]=uint16_t(p-last);}
 last=255;for(int p=127;p>=0;--p){if(present[p])last=p;const int d=std::min(int(distances[p]),last-p);distances[p]=uint16_t(d*d);}
 const uint64_t first_factor=180*count,second_factor=60*count*count;uint64_t best_score=UINT64_MAX;
 for(const auto& candidate:candidates_){
   uint32_t hit_distance=0;for(size_t i=0;i<count;++i){int d=128;for(int n:candidate.notes)d=std::min(d,std::abs(hits[i]-n));hit_distance+=uint32_t(d*d);}
   const uint32_t chord_distance=distances[candidate.notes[0]]+distances[candidate.notes[1]]+distances[candidate.notes[2]];
   const int64_t delta=int64_t(candidate.sum)*int64_t(count)-3*sum;
   // Exactly the previous score multiplied by 180 * count^2: same choice
   // and tie order, with no floating-point divisions in the note callback.
   const uint64_t score=first_factor*hit_distance+second_factor*chord_distance+uint64_t(delta*delta);
   if(score<best_score){best_score=score;best={{candidate.notes[0],candidate.notes[1],candidate.notes[2]}};}
 }
 return best;
}
std::array<int,3> nearest_triad(const int* hits,size_t count,Selection scale,int low,int high){ChordSearch search;search.configure(scale,low,high);return search.find(hits,count);}
std::array<int,4> ChordSearch::find_four(const int* hits,size_t count)const{
 const auto triad=find(hits,count);std::array<int,4> best{{triad[0],triad[1],triad[2],-1}};if(!hits || !count || count>48 || !valid(scale_))return best;
 int root=-1;for(int pitch:triad){bool third=false,fifth=false;for(int n:triad){const int interval=(n-pitch+120)%12;third|=interval==3 || interval==4;fifth|=interval==7;}if(third && fifth){root=pitch%12;break;}}
 if(root<0)return best;
 const int bottom=12*(low_+1)+scale_.root,top=std::min(127,12*(high_+1)+scale_.root+modes()[scale_.mode].intervals.back());
 int input_sum=0;for(size_t i=0;i<count;++i)input_sum+=hits[i];uint64_t best_score=UINT64_MAX;
 for(int pitch=bottom;pitch<=top;++pitch)if(pitch%12==root && std::find(triad.begin(),triad.end(),pitch)==triad.end()){
   std::array<int,4> chord{{triad[0],triad[1],triad[2],pitch}};uint32_t forward=0,backward=0;
   for(size_t i=0;i<count;++i){int d=128;for(int n:chord)d=std::min(d,std::abs(hits[i]-n));forward+=d*d;}
   for(int n:chord){int d=128;for(size_t i=0;i<count;++i)d=std::min(d,std::abs(hits[i]-n));backward+=d*d;}
   const int64_t delta=int64_t(triad[0]+triad[1]+triad[2]+pitch)*int64_t(count)-4*input_sum;
   const uint64_t score=320*uint64_t(count)*forward+80*uint64_t(count)*count*backward+uint64_t(delta*delta);
   if(score<best_score){best_score=score;std::sort(chord.begin(),chord.end());best=chord;}
 }
 return best;
}
double Ball::radius() const {
 const double t=std::clamp(grow_elapsed/pickup_seconds,0.,1.),ease=t*t*(3-2*t);
 return grow_from*(1-ease)+(base_radius+radius_span*std::sqrt(std::min(energy,1.)))*ease;
}
uint8_t Ball::opacity()const{return retiring?uint8_t(std::clamp(1-fade_elapsed/fade_seconds,0.,1.)*255+.5):255;}
int Ball::octave(int low,int high) const {
 // Four equally sized radius bands over the visible lifetime, not equal
 // energy bands: radius depends on sqrt(energy).
 const double smallest=base_radius+radius_span*std::sqrt(.035);
 const double shrink=std::clamp((full_radius-(base_radius+radius_span*std::sqrt(energy)))/(full_radius-smallest),0.,1.);
 return low+std::min(high-low,int(shrink*(high-low+1)));
}
int World::segment(Vec p,int count) {double a=std::atan2(p.x,-p.y)+pi/count;if(a<0)a+=2*pi;return int(a/(2*pi/count))%count;}
int World::grab(Vec p) {
 const double pointer_square=dot(p,p);if(!std::isfinite(pointer_square) || pointer_square>233*233)return -1;
 int nearest=-1;double best=pickup_radius*pickup_radius;
 for(int i=int(balls.size())-1;i>=0;--i)if(balls[i].alive && !balls[i].held){const auto delta=balls[i].p-p;const double square=dot(delta,delta);if(square<=best && (nearest<0 || square<best)){best=square;nearest=i;}}
 if(nearest>=0){
   auto& b=balls[nearest];const double current_radius=b.radius();
   b.touch_target=p;b.pickup_offset=b.p-limit(p,boundary-current_radius);b.pickup_elapsed=0;
   b.held=true;b.v={};b.energy=1;b.age=0;b.grow_from=current_radius;b.grow_elapsed=0;b.retiring=false;b.fade_elapsed=0;b.remaining_seconds=-1;b.decay_seconds=0;b.collision_grace=0;return nearest;
 }
 // Pickup reaches the visible display edge, spawning stays inside the arena.
 if(pointer_square>boundary*boundary)return -1;
 size_t alive=0;for(const auto& b:balls)alive+=b.alive;if(alive>=manual_balls)return -1;
 for(int i=0;i<int(balls.size());++i) if(!balls[i].alive) {
   balls[i]={true,true,limit(p,boundary-Ball::full_radius),{},1,0,uint8_t(next_white_?255:0)};balls[i].touch_target=balls[i].p;next_white_=!next_white_;return i;
 }
 return -1;
}
void World::follow_touch(Ball& b){const double t=std::clamp(b.pickup_elapsed/Ball::pickup_seconds,0.,1.),remaining=1-t*t*(3-2*t);b.p=limit(limit(b.touch_target,boundary-b.radius())+b.pickup_offset*remaining,boundary-b.radius());}
void World::move(int id,Vec p) {if(id>=0 && id<int(balls.size()) && balls[id].held){auto& b=balls[id];b.touch_target=p;follow_touch(b);}}
Vec World::fling_velocity(Vec finger_velocity){
 const double speed=length(finger_velocity);if(!std::isfinite(speed) || speed<=0)return {};
 // Fixed curve 11: a gentle low-speed range, then progressive acceleration.
 const double normalized=std::min(1.,speed*.9/max_speed);
 const double t=normalized,response=t<=.4?t*.5:.2+(t-.4)*(4./3.);
 const double target=max_speed*std::clamp(response,0.,1.);
 return finger_velocity*(target/speed);
}
void World::release(int id,Vec v) {
 if(id<0 || id>=int(balls.size()) || !balls[id].held)return;
 auto& b=balls[id]; b.held=false;b.v=limit(v,max_speed); // No automatic random kick on a simple tap.
 // Recharging and growth already began on pickup. A quick release continues
 // the animation in flight without jumping to the full radius.
 b.p=limit(b.p,boundary-b.radius());
}
void World::queue_hit(uint64_t now,int pitch,int velocity) {
 if(pitch<0 || pitch>127)return;
 const uint64_t period=period_us();
 const bool external=clock_.ready(wall_now_) && settings_.quantized;
 const uint64_t due=external?uint64_t(std::ceil(std::max(0.,clock_.position(wall_now_)-1e-7)/step_units()))*step_units():settings_.quantized?grid_origin_+((now-grid_origin_+period-1)/period)*period:now;
 for(auto& hit:pending_)if(hit.used && hit.external==external && hit.due==due && hit.pitch==pitch){hit.velocity=std::max(hit.velocity,velocity);++hit.contacts;return;}
 for(auto& hit:pending_)if(!hit.used){hit={true,due,pitch,velocity,1,external};return;}
}
void World::play_due(uint64_t now) {
 for(;;){
   uint64_t next=UINT64_MAX;bool external=false;
   for(const auto& hit:pending_)if(hit.used && (hit.external?clock_.ready(wall_now_) && clock_.position(wall_now_)+1e-7>=double(hit.due):hit.due<=now) && hit.due<next){next=hit.due;external=hit.external;}
   if(next==UINT64_MAX)break;
   const uint64_t event_time=std::max(external?now:next,notes_.time());
   notes_.service(event_time);
   if(settings_.quantized){notes_.release(event_time);if(!notes_.clean())break;}
   std::array<int,48> hits{};size_t count=0;std::array<bool,12> classes{};unsigned distinct_classes=0;
   for(const auto& hit:pending_)if(hit.used && hit.external==external && hit.due==next){hits[count++]=hit.pitch;if(!classes[hit.pitch%12]){classes[hit.pitch%12]=true;++distinct_classes;}}
   if(distinct_classes>1){
     const auto chord=chords_.find_four(hits.data(),count);
     for(int pitch:chord){
       if(pitch<0)continue;
       int distance=128,velocity=1;
       for(const auto& hit:pending_)if(hit.used && hit.external==external && hit.due==next){
         const int d=std::abs(pitch-hit.pitch);
         if(d<distance){distance=d;velocity=hit.velocity;}else if(d==distance)velocity=std::max(velocity,hit.velocity);
       }
       notes_.strike(event_time,pitch,velocity,gate_us(),{});
     }
   }else{
     for(const auto& hit:pending_)if(hit.used && hit.external==external && hit.due==next)notes_.strike(event_time,hit.pitch,hit.velocity,gate_us(),{});
   }
   for(auto& hit:pending_)if(hit.used && hit.external==external && hit.due==next)hit.used=false;
 }
 notes_.service(now);
}
void World::clear(){for(auto& b:balls)b={};next_white_=true;last_collisions_={};pending_={};flashes_.fill(0);notes_.stop(uint64_t(std::llround(time_*1000000)));}
double World::remaining_lifetime(const Ball& b)const{
 if(!b.alive || b.retiring || b.energy<=.035)return 0;
 if(b.remaining_seconds>=0)return b.remaining_seconds;
 return std::max(0.,std::min(settings_.energy_seconds*std::log(b.energy/.035),4*settings_.energy_seconds-b.age));
}
void World::assign_lifetime(Ball& b,double energy,double remaining){
 b.energy=energy;b.remaining_seconds=remaining;
 b.decay_seconds=energy>.035 && remaining>0?remaining/std::log(energy/.035):0;
 b.decay_factor=b.decay_seconds>0?std::exp(-cached_dt_/b.decay_seconds):1;
 b.retiring=false;b.fade_elapsed=0;b.grow_elapsed=Ball::pickup_seconds;b.collision_grace=.18;
}
void World::step(double dt) {
 if(!std::isfinite(dt) || dt<=0 || dt>1./120+.000001)return;
 if(dt!=cached_dt_){cached_dt_=dt;energy_decay_=std::exp(-dt/settings_.energy_seconds);speed_decay_=std::exp(-dt/(settings_.energy_seconds*65./30.));for(auto& b:balls)if(b.alive && b.decay_seconds>0)b.decay_factor=std::exp(-dt/b.decay_seconds);}
 time_+=dt; const auto now=uint64_t(std::llround(time_*1000000));
 // Time remaining, not brightness: the view changes color only at ON/OFF.
 for(auto& f:flashes_){f-=dt;if(f<1e-9)f=0;}
 for(auto& b:balls)if(b.alive)b.grow_elapsed=std::min(Ball::pickup_seconds,b.grow_elapsed+dt);
 std::array<double,max_balls> radii{};
 for(size_t i=0;i<balls.size();++i){auto& b=balls[i];if(!b.alive)continue;if(b.held){b.pickup_elapsed=std::min(Ball::pickup_seconds,b.pickup_elapsed+dt);follow_touch(b);radii[i]=b.radius();continue;}
   b.age+=dt;b.energy*=b.decay_seconds>0?b.decay_factor:energy_decay_;b.v=b.v*speed_decay_;
   b.collision_grace=std::max(0.,b.collision_grace-dt);
   if(b.remaining_seconds>=0)b.remaining_seconds=std::max(0.,b.remaining_seconds-dt);
   if(b.energy<.035 || (b.remaining_seconds>=0?b.remaining_seconds<=0:b.age>4*settings_.energy_seconds))b.retiring=true;
   if(b.retiring){b.fade_elapsed+=dt;if(b.fade_elapsed>=Ball::fade_seconds){b={};continue;}}
   b.p=b.p+b.v*dt;
   radii[i]=b.radius();const double r=boundary-radii[i],square=dot(b.p,b.p);
   if(square>=r*r) {const double d=std::sqrt(square);
     const auto n=b.p*(1/std::max(d,.000001)); b.p=n*(r-.001);
     const double speed=dot(b.v,n);
     if(speed>0) {
       b.v=b.v-n*(2*speed);b.v=b.v*.997;b.energy*=.997;radii[i]=b.radius();
       const int degree=segment(n,segment_count());flashes_[degree]=flash_seconds;
       const int pitch=12*(b.octave(settings_.low,settings_.high)+1)+settings_.scale.root+modes()[settings_.scale.mode].intervals[degree];
       queue_hit(now,pitch,std::clamp(int(40+70*std::pow(b.energy,.65)),40,110));
     }
   }
 }
 for(size_t i=0;i<balls.size();++i) for(size_t j=i+1;j<balls.size();++j) {
   auto& a=balls[i];auto& b=balls[j];if(!a.alive || !b.alive || a.held || b.held)continue;
   auto delta=b.p-a.p;const double square=dot(delta,delta),r=radii[i]+radii[j];
   if(square>=r*r)continue;
   const double d=std::sqrt(square);
   const Vec n=d>.00001?delta*(1/d):Vec{1,0};
   a.p=a.p-n*((r-d+.002)/2);b.p=b.p+n*((r-d+.002)/2);
   const double approach=dot(b.v-a.v,n);
   if(approach<0){
     a.v=a.v+n*approach;b.v=b.v-n*approach;
     auto& last=last_collisions_[i*max_balls+j];
     if(collision_handler_ && approach<-1 && (!last || now-last>=80000)){
       last=now;const CollisionEvent event{now,i,j,a.p+n*radii[i],-approach,a.energy,b.energy,settings_.scale,a.octave(settings_.low,settings_.high),b.octave(settings_.low,settings_.high)};
       const auto response=collision_handler_->collide(event);
       for(size_t k=0;k<std::min(response.count,response.notes.size());++k){const auto note=response.notes[k];if(note.pitch>=0 && note.pitch<=127 && note.velocity>0 && note.velocity<=127)queue_hit(now,note.pitch,note.velocity);}
     }
   }
   // With no collision handler installed these remain purely physical.
 }
 // Positional correction can push a ball just outside the arena.
 for(size_t i=0;i<balls.size();++i)if(balls[i].alive)balls[i].p=limit(balls[i].p,boundary-radii[i]);
 play_due(now);
}
}

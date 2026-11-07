#pragma once
#include "world.hpp"
#include <algorithm>
namespace sm::bounce {
// Only sampled movement belongs here, never the coordinates reported on lift.
// Fixed storage, no allocation, and the same estimator on desktop and device.
class FlickTracker {
 struct Sample{uint64_t time=0;Vec p{};};
 std::array<Sample,16> samples_{};size_t count_=0;
public:
 void down(Vec p,uint64_t now){count_=1;samples_[0]={now,p};}
 void move(Vec p,uint64_t now){
   if(!std::isfinite(p.x) || !std::isfinite(p.y))return;
   if(!count_){down(p,now);return;}
   if(now<=samples_[count_-1].time)return;
   if(count_==samples_.size()){std::move(samples_.begin()+1,samples_.end(),samples_.begin());--count_;}
   samples_[count_++]={now,p};
   while(count_>2 && now-samples_[1].time>120){std::move(samples_.begin()+1,samples_.begin()+count_,samples_.begin());--count_;}
 }
 Vec velocity(uint64_t released)const{
   size_t count=count_;
   // TouchGesture may flush a deferred Move at the release timestamp. That
   // endpoint is just as unreliable as release coordinates themselves.
   while(count && samples_[count-1].time>=released)--count;
   if(count<2)return {};
   const uint64_t idle=released-samples_[count-1].time;
   if(idle>=150)return {};
   const double span=double(samples_[count-1].time-samples_[0].time);
   if(span<=0)return {};
   double mean_t=0;Vec mean{};
   for(size_t i=0;i<count;++i){mean_t+=double(samples_[i].time-samples_[0].time);mean=mean+samples_[i].p;}
   mean_t/=count;mean=mean*(1./count);double variance=0;Vec covariance{};
   for(size_t i=0;i<count;++i){const double t=double(samples_[i].time-samples_[0].time)-mean_t;variance+=t*t;covariance=covariance+(samples_[i].p-mean)*t;}
   if(variance<=0)return {};
   const double short_sample=std::min(1.,span/32.),freshness=idle<=50?1.:(150.-idle)/100.;
   return covariance*(1000./variance*short_sample*freshness);
 }
};
}

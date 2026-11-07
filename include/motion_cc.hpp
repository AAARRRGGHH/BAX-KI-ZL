#pragma once
#include "music.hpp"
#include <algorithm>
namespace sm {
// Gravity-relative tilt: two tangents at the calibrated pose; no gyro drift.
class MotionCc {
 MotionSettings settings_{};
 std::array<float,3> gravity_{{0,0,1}};
 std::array<int,2> sent_{{-1,-1}};
 uint32_t sample_at_=0,send_at_=0,refresh_at_=0; bool ready_=false;
 static float dot(const std::array<float,3>& a,const std::array<float,3>& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
public:
 void invalidate(){sent_={{-1,-1}};}
 void configure(MotionSettings s){if(valid_motion(s) && s!=settings_){settings_=s;invalidate();}}
 bool sample(float x,float y,float z,uint32_t now){
  const float norm=std::sqrt(x*x+y*y+z*z);
  if(!std::isfinite(norm)||norm<.75f||norm>1.25f)return false;
  const std::array<float,3> v{{x/norm,y/norm,z/norm}};
  const float dt=ready_?float(std::min<uint32_t>(uint32_t(now-sample_at_),100)):1000.f;
  const float alpha=ready_?dt/(100.f+dt):1.f;
  for(int i=0;i<3;++i)gravity_[i]+=alpha*(v[i]-gravity_[i]);
  ready_=true;sample_at_=now;return true;
 }
 bool calibrate(uint32_t now,MotionSettings& out)const{
  if(!ready_||uint32_t(now-sample_at_)>200)return false;
  out=settings_;const float norm=std::sqrt(dot(gravity_,gravity_));
  for(int i=0;i<3;++i)out.neutral[i]=gravity_[i]/norm;
  return valid_motion(out);
 }
 static int value(float degrees){
  const float a=std::abs(degrees);if(a<=2)return 64;
  const float t=std::clamp((a-2)/43.f,0.f,1.f);
  return int(std::lround(64+(degrees<0?-64:63)*t));
 }
 template<class Send> void update(uint32_t now,bool enabled,Send send){
  const bool active=enabled && ready_ && uint32_t(now-sample_at_)<=200;
  if(!active){invalidate();return;}
  if(uint32_t(now-send_at_)<40)return;
  send_at_=now;
  auto n=settings_.neutral;
  // Project the device X axis into the neutral plane. A Z-axis fallback also
  // permits calibrating a nearly edge-on pose without a singularity.
  std::array<float,3> x{{1-n[0]*n[0],-n[0]*n[1],-n[0]*n[2]}};
  if(dot(x,x)<.02f)x={{-n[2]*n[0],-n[2]*n[1],1-n[2]*n[2]}};
  const float norm=std::sqrt(dot(x,x));
  for(auto& v:x)v/=norm;
  std::array<float,3> y{{n[1]*x[2]-n[2]*x[1],n[2]*x[0]-n[0]*x[2],n[0]*x[1]-n[1]*x[0]}};
  // X is intentionally inverted; Y retains its original direction.
  const float vertical=dot(gravity_,n);
  const std::array<float,2> angles{{-std::atan2(dot(gravity_,x),vertical)*57.2957795f,std::atan2(dot(gravity_,y),vertical)*57.2957795f}};
  const bool refresh=uint32_t(now-refresh_at_)>=1000;
  for(int i=0;i<2;++i)if(settings_.cc[i]!=motion_off){
   const int v=value(angles[i]);
   const MidiEvent event{MidiKind::ControlChange,uint8_t(settings_.channel),uint8_t(settings_.cc[i]),uint8_t(v)};
   if((v!=sent_[i]||refresh) && send(event))sent_[i]=v;
  }
  if(refresh)refresh_at_=now;
 }
};
}

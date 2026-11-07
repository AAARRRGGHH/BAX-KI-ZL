#include "face_down_reset.hpp"
#include <cmath>
namespace sm {
bool FaceDownReset::update(float x,float y,float z,uint32_t now,bool enabled) {
 if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
   initialized_=holding_=upright_=false; return false;
 }
 if(!initialized_ || uint32_t(now-last_)>60 || now==last_) {
   initialized_=true; holding_=upright_=false;
   x_=x; y_=y; z_=z; last_=now; return false;
 }
 const auto dt=uint32_t(now-last_); last_=now;
 const float dx=x-x_,dy=y-y_,dz=z-z_;
 const float speed=1000.f*std::sqrt(dx*dx+dy*dy+dz*dz)/dt;
 x_=x; y_=y; z_=z;
 const float total=std::sqrt(x*x+y*y+z*z);
 const bool stable=total>=.82f && total<=1.18f && speed<6.f;
 // Rearm after clearly leaving the face-down zone for 100 ms. Do not require
 // stillness here: turning upright and tapping a pad is a normal quick action.
 // Stillness is required only for the actual one-second face-down hold. This latch
 // survives disabled UI gates, so menus cannot cause repeated resets face down.
 if(total>=.6f && total<=1.4f && z>-.5f*total) {
   if(!upright_) { upright_=true; upright_at_=now; }
   else if(uint32_t(now-upright_at_)>=100) armed_=true;
 } else upright_=false;
 // Within 40 degrees of horizontal, display towards the ground, continuously.
 const bool down=stable && z<=-.7660444f*total;
 if(!enabled || !armed_ || !down) { holding_=false; return false; }
 if(!holding_) { holding_=true; hold_at_=now; }
 if(uint32_t(now-hold_at_)<hold_duration_ms) return false;
 armed_=holding_=upright_=false;
 return true;
}
}

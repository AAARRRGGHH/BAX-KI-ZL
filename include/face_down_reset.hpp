#pragma once
#include <cstdint>
namespace sm {
// Display-relative acceleration in g: +Z points out of the display.
class FaceDownReset {
 uint32_t last_=0,hold_at_=0,upright_at_=0;
 float x_=0,y_=0,z_=0;
 bool initialized_=false,holding_=false,upright_=false,armed_=false;
public:
 static constexpr uint32_t hold_duration_ms=1000;
 void reset() { *this=FaceDownReset{}; }
 bool update(float x,float y,float z,uint32_t now,bool enabled);
 bool armed() const { return armed_; }
 uint32_t held_ms(uint32_t now) const { return holding_?uint32_t(now-hold_at_):0; }
};
}

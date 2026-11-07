#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace sm {
inline constexpr int battery_icon_width=48,battery_icon_height=72;
inline constexpr float battery_icon_scale=1.2f;
// The same alpha silhouette is used in power-off and charge-only screens.
inline void make_battery_outline(uint8_t* pixels) {
 const auto distance=[](float x,float y,float cx,float cy,float width,float height,float radius) {
   const float dx=std::abs(x-cx)-(width/2-radius),dy=std::abs(y-cy)-(height/2-radius);
   return radius-std::hypot(std::max(dx,0.f),std::max(dy,0.f))-std::min(std::max(dx,dy),0.f);
 };
 for(int y=0;y<battery_icon_height;++y) for(int x=0;x<battery_icon_width;++x) {
   const float px=(x+.5f)/battery_icon_scale,py=(y+.5f)/battery_icon_scale;
   const float d=battery_icon_scale*std::max(distance(px,py,20,31,34,48,4),distance(px,py,20,7,12,8,2));
   pixels[y*battery_icon_width+x]=uint8_t(255*std::clamp(d+.5f,0.f,1.f)*std::clamp(2.5f-d,0.f,1.f));
 }
}
}

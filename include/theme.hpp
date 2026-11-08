#pragma once
#include <cstdint>
#include "brand.hpp"
#include <algorithm>
#include <cmath>
namespace sm {
struct ColorScheme {
 const char* name;
 uint32_t accent_a,accent_b,text,muted;
};
inline constexpr ColorScheme color_scheme{default_accent_name,default_accent,default_accent,0xffffff,0xffffff};
// Constant brightness/saturation; cyan at hue 180 is exactly #2EE7E7.
inline uint32_t accent_for_hue(int degrees) {
 int h=(degrees%360+360)%360,sector=h/60;
 uint32_t low=46,high=231,r=low,g=low,b=low;
 const uint32_t rising=low+(185*(h%60)+30)/60,falling=high-(185*(h%60)+30)/60;
 switch(sector) {
 case 0:r=high;g=rising;break;
 case 1:r=falling;g=high;break;
 case 2:g=high;b=rising;break;
 case 3:g=falling;b=high;break;
 case 4:r=rising;b=high;break;
 default:r=high;b=falling;break;
 }
 return (r<<16)|(g<<8)|b;
}
inline float hue_for_accent(uint32_t value) {
 const float r=float((value>>16)&255),g=float((value>>8)&255),b=float(value&255);
 const float high=std::max({r,g,b}),delta=high-std::min({r,g,b});
 if(delta==0) return 180;
 float h=high==r?(g-b)/delta:high==g?2+(b-r)/delta:4+(r-g)/delta;
 return std::fmod(h*60+360,360.f);
}
}

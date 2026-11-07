#pragma once
#include "ui_fonts.hpp"
#include <algorithm>
#include <cmath>
namespace sm {
inline int selector_font_size(float distance,int base_size=32) {
 return std::clamp(int(std::lround(base_size*(1-.42f*std::clamp(distance,0.f,1.f)))),18,38);
}
inline const lv_font_t* selector_native_font(int pixels) {
 static const lv_font_t* sizes[]={&barlow_18,&barlow_19,&barlow_20,&barlow_21,&barlow_22,
   &barlow_23,&barlow_24,&barlow_25,&barlow_26,&barlow_27,&barlow_28,&barlow_29,&barlow_30,&barlow_31,&barlow_32,
   &barlow_33,&barlow_34,&barlow_35,&barlow_36,&barlow_37,&barlow_38};
 return sizes[std::clamp(pixels,18,38)-18];
}
inline const lv_font_t* selector_font(float distance,int base_size=32) {
 return selector_native_font(selector_font_size(distance,base_size));
}
}

#pragma once
#include "charge_mode.hpp"
#include "lvgl.h"
#include "battery_icon.hpp"
#include <array>
namespace sm {
// Shared firmware/simulator view. All input is blocked; PWR is handled by host.
class ChargeScreen {
 lv_obj_t *root_=nullptr,*unknown_=nullptr;
 std::array<lv_obj_t*,5> bars_{};
 std::array<uint8_t,battery_icon_width*battery_icon_height> outline_pixels_{};
 lv_image_dsc_t outline_{};
 lv_timer_t* blink_timer_=nullptr;
 BatteryState sample_{};
 ChargeAnimationClock animation_clock_;
 uint32_t sampled_at_=0;
 bool fresh_=false;
 void paint(uint32_t now);
public:
 void build(uint32_t accent);
 void update(BatteryState sample,uint32_t now,bool fresh=true);
 void close();
};
}

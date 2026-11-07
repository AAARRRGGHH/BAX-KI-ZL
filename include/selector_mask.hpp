#pragma once
#include "lvgl.h"
namespace sm {
inline void selector_mask_never_covers(lv_obj_t* mask) {
 // LVGL 9.2's simple-gradient cover check tests bg_grad_opa, but misses
 // bg_main_opa. A bottom fade (0 -> 255) is therefore mistaken for an opaque
 // cover, skipping the background and blending stale partial-buffer pixels.
 lv_obj_add_event_cb(mask,[](lv_event_t* event) {
   lv_event_set_cover_res(event,LV_COVER_RES_NOT_COVER);
 },LV_EVENT_COVER_CHECK,nullptr);
}
}

#pragma once
#include "lvgl.h"

// esp_lvgl_adapter 0.6.4 uses the newer convenience accessor despite
// declaring support for LVGL >=8,<10. In 9.2 the INVALIDATE_AREA event
// carries the same lv_area_t pointer in its event parameter (also used
// by the official Waveshare C BSP rounder callback).
#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 2
static inline lv_area_t *lv_event_get_invalidated_area(lv_event_t *event)
{
    return (lv_area_t *)lv_event_get_param(event);
}
#endif

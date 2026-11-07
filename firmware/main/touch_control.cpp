// This glue is linked into the BSP archive to avoid a BSP -> app archive cycle.
#ifdef esp_lv_adapter_register_touch
#undef esp_lv_adapter_register_touch
#endif
#include "esp_lv_adapter_input.h"
#include "esp_log.h"
// Only the official BSP's registration call is redirected by its CMake target.
// The BSP's display/controller initialization and pin configuration stay intact.
extern "C" lv_indev_t* sm_register_two_touch(const esp_lv_adapter_touch_config_t* config) {
 auto multi=*config;
 multi.multi_touch.mode=ESP_LV_ADAPTER_TOUCH_MODE_MULTI_CONTROL;
 multi.multi_touch.pointers=2;
 ESP_LOGI("touch","Registering two independent touch pointers with controller track IDs");
 return esp_lv_adapter_register_touch(&multi);
}

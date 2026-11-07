#include "motion_reset.hpp"
#include "face_down_reset.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cmath>
// The official Waveshare header defines M_PI unconditionally.
#ifdef M_PI
#undef M_PI
#endif
#include "qmi8658.h"
namespace {
qmi8658_dev_t sensor{};
uint32_t now_ms() { return uint32_t(esp_timer_get_time()/1000); }
}
bool MotionReset::start(i2c_master_bus_handle_t bus) {
 // Same bus/address as the official 1.75C 04_Immersive_block example.
 auto error=qmi8658_init(&sensor,bus,QMI8658_ADDRESS_HIGH);
 if(error==ESP_OK) error=qmi8658_enable_sensors(&sensor,QMI8658_DISABLE_ALL);
 if(error==ESP_OK) error=qmi8658_set_accel_range(&sensor,QMI8658_ACCEL_RANGE_8G);
 if(error==ESP_OK) error=qmi8658_set_accel_odr(&sensor,QMI8658_ACCEL_ODR_125HZ);
 qmi8658_set_accel_unit_mg(&sensor,true);
 if(error==ESP_OK) error=qmi8658_enable_sensors(&sensor,QMI8658_ENABLE_ACCEL);
 uint8_t control=0,enabled=0;
 if(error==ESP_OK) error=qmi8658_read_register(&sensor,QMI8658_CTRL2,&control,1);
 if(error==ESP_OK) error=qmi8658_read_register(&sensor,QMI8658_CTRL7,&enabled,1);
 if(error!=ESP_OK || control!=0x26 || (enabled&3)!=QMI8658_ENABLE_ACCEL) {
   ESP_LOGW("pose-reset","QMI8658 unavailable/configuration not verified; face-down reset disabled (%s, CTRL2=0x%02x CTRL7=0x%02x)",esp_err_to_name(error),control,enabled);
   if(sensor.dev_handle) { qmi8658_enable_sensors(&sensor,QMI8658_DISABLE_ALL); i2c_master_bus_rm_device(sensor.dev_handle); sensor.dev_handle=nullptr; }
   return false;
 }
 if(xTaskCreate([](void* context) { static_cast<MotionReset*>(context)->run(); },"pose-read",3072,this,2,nullptr)!=pdPASS) {
   qmi8658_enable_sensors(&sensor,QMI8658_DISABLE_ALL); i2c_master_bus_rm_device(sensor.dev_handle); sensor.dev_handle=nullptr;
   ESP_LOGW("pose-reset","Cannot create reader; face-down reset disabled"); return false;
 }
 ESP_LOGI("pose-reset","QMI8658 0x6b verified: +/-8g, 125Hz, gyro off; Chords/Arpeggiator/Bounce reset after holding display face down for 1s");
 return true;
}
void MotionReset::enable(bool value,int mode) {
 if(mode_.exchange(mode)!=mode) pending_.store(0);
 heartbeat_.store(now_ms()); enabled_.store(value);
 if(!value) pending_.store(0);
}
bool MotionReset::take_reset() {
 const auto timestamp=pending_.exchange(0);
 return timestamp && pending_mode_.load()==mode_.load() && uint32_t(now_ms()-timestamp)<=100;
}
void MotionReset::run() {
 sm::FaceDownReset detector; bool failed=false;
 int previous_mode=0;
 uint32_t report_at=now_ms(),last_sample=0,samples=0,errors=0,max_gap=0,max_hold_ms=0,triggers=0;
 float max_acceleration=0,last_x=0,last_y=0,last_z=0;
 for(;;) {
   const auto heartbeat=heartbeat_.load(); const auto now=now_ms();
   const int mode=mode_.load();
   if(mode!=previous_mode) { detector.reset(); pending_.store(0); last_sample=0; previous_mode=mode; }
   const bool gate=enabled_.load() && uint32_t(now-heartbeat)<=100;
   if(!gate) pending_.store(0);
   {
     bool ready=false;
     auto error=qmi8658_is_data_ready(&sensor,&ready);
     float x=0,y=0,z=0;
     if(error==ESP_OK && ready) error=qmi8658_read_accel(&sensor,&x,&y,&z);
     if(error!=ESP_OK) {
       ++errors; last_sample=0;
       detector.update(NAN,NAN,NAN,now,false); pending_.store(0);
       if(!failed) ESP_LOGW("pose-reset","Sensor read failed; gesture discarded: %s",esp_err_to_name(error));
       failed=true;
     } else if(ready) {
       failed=false;
       const auto sampled_at=now_ms();
       if(last_sample) max_gap=std::max(max_gap,uint32_t(sampled_at-last_sample));
       last_sample=sampled_at; ++samples;
       last_x=x/1000.f; last_y=y/1000.f; last_z=z/1000.f;
       max_acceleration=std::max(max_acceleration,std::sqrt(last_x*last_x+last_y*last_y+last_z*last_z));
       // QMI +Z points out of the component/display side (board photo + datasheet).
       // Driver returns mg, not g, when accel_unit_mps2 is false.
       if(detector.update(last_x,last_y,last_z,sampled_at,gate)) {
         pending_mode_.store(mode); pending_.store(sampled_at); ++triggers; max_hold_ms=sm::FaceDownReset::hold_duration_ms;
       }
       max_hold_ms=std::max(max_hold_ms,detector.held_ms(sampled_at));
     }
   }
   // No periodic serial logging on battery or a power-only USB connection.
   // These counters distinguish a closed UI gate, missing samples and a gesture
   // that never reaches the threshold without logging at sensor frequency.
   if(uint32_t(now-report_at)>=2000) {
     if(usb_serial_jtag_is_connected()) ESP_LOGI("pose-reset",
       "gate=%d samples=%lu errors=%lu max_gap=%lums peak=%.2fg last=(%.2f,%.2f,%.2f)g armed=%d hold_ms=%lu triggers=%lu",
       int(gate),static_cast<unsigned long>(samples),static_cast<unsigned long>(errors),static_cast<unsigned long>(max_gap),
       double(max_acceleration),double(last_x),double(last_y),double(last_z),int(detector.armed()),
       static_cast<unsigned long>(max_hold_ms),static_cast<unsigned long>(triggers));
     report_at=now; samples=errors=max_gap=max_hold_ms=triggers=0; max_acceleration=0;
   }
   vTaskDelay(pdMS_TO_TICKS(10));
 }
}

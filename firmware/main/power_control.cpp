#include "power_control.hpp"
#include "esp_log.h"
bool PowerControl::initialize_power_key() {
 key_ready_=verified_ && sm::enable_pmic_power_key(*this);
 if(key_ready_) ESP_LOGI("power","PWR source: AXP2101 PWRON edges, 0x41[1:0] enabled/read back; GPIO3 diagnostic only");
 else ESP_LOGE("power","PWR edge initialization failed; no GPIO fallback; BOOT shutdown remains available");
 return key_ready_;
}
bool PowerControl::read(uint8_t reg,uint8_t& value) {
 return device_ && i2c_master_transmit_receive(device_,&reg,1,&value,1,100)==ESP_OK;
}
bool PowerControl::write(uint8_t reg,uint8_t value) {
 const uint8_t bytes[]={reg,value};
 return device_ && i2c_master_transmit(device_,bytes,2,100)==ESP_OK;
}
bool PowerControl::initialize(i2c_master_bus_handle_t bus) {
 if(!bus) return false;
 i2c_device_config_t cfg{}; cfg.dev_addr_length=I2C_ADDR_BIT_LEN_7;
 cfg.device_address=0x34; cfg.scl_speed_hz=400000;
 if(i2c_master_bus_add_device(bus,&cfg,&device_)!=ESP_OK) return false;
 uint8_t id=0;
 // Register and ID verified against the official Waveshare XPowers source.
 if(!read(0x03,id) || id!=0x4a) {
   ESP_LOGE("power","Unexpected PMIC ID 0x%02x; no policy writes",id); return false;
 }
 verified_=true;
 uint8_t before22=0,before10=0;
 if(!read(0x22,before22) || !read(0x10,before10)) {
   ESP_LOGE("power","Cannot read shutdown policy; long-press policy unconfirmed"); return false;
 }
 ESP_LOGI("power","AXP2101 ID=0x%02x, original 0x22=0x%02x, 0x10=0x%02x",id,before22,before10);
 const bool ok=sm::disable_pwr_shutdown(*this);
 if(ok) ESP_LOGI("power","PWR hold enabled: 0x22[1]=0 and 0x10[2]=0 read back; BOOT 3s requests shutdown");
 else ESP_LOGE("power","PWR hold policy verification failed");
 return ok;
}

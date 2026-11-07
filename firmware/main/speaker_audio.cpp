#include "speaker_audio.hpp"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "power_diagnostics.hpp"
#include "esp_system.h"
void SpeakerAudio::mute_amplifier() { gpio_set_level(BSP_POWER_AMP_IO,0); }
void SpeakerAudio::early_mute() {
 // Load the inactive level before enabling the output; no codec/I2C needed.
 ESP_ERROR_CHECK(gpio_set_level(BSP_POWER_AMP_IO,0));
 gpio_config_t pin{}; pin.pin_bit_mask=1ULL<<BSP_POWER_AMP_IO;
 pin.mode=GPIO_MODE_OUTPUT; pin.pull_down_en=GPIO_PULLDOWN_ENABLE;
 ESP_ERROR_CHECK(gpio_config(&pin));
 ESP_ERROR_CHECK(esp_register_shutdown_handler(mute_amplifier));
}
bool SpeakerAudio::configure(bool enabled,sm::SynthSettings settings) {
 if(!sm::valid_synth(settings)) return false;
 if(!enabled && !started_) return true;
 if(!started_) {
   mute_amplifier();
   if(!mutex_) mutex_=xSemaphoreCreateMutex();
   if(!mutex_) return false;
   if(!codec_) {
     // Same Philips/mono configuration and verified pins as the official C-BSP.
     i2s_std_config_t config{}; config.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(48000);
     config.slot_cfg=I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO);
     config.gpio_cfg.mclk=BSP_I2S_MCLK; config.gpio_cfg.bclk=BSP_I2S_SCLK;
     config.gpio_cfg.ws=BSP_I2S_LCLK; config.gpio_cfg.dout=BSP_I2S_DOUT; config.gpio_cfg.din=BSP_I2S_DSIN;
     if(bsp_audio_init(&config)!=ESP_OK) return false;
     codec_=bsp_audio_codec_speaker_init(); if(!codec_) return false;
   }
   esp_codec_dev_sample_info_t format{}; format.sample_rate=48000; format.channel=1; format.bits_per_sample=16;
   if(esp_codec_dev_open(codec_,&format)!=ESP_CODEC_DEV_OK) return false;
   if(esp_codec_dev_set_out_vol(codec_,70)!=ESP_CODEC_DEV_OK) { esp_codec_dev_close(codec_); return false; }
   // The local BSP leaves PA control to us. Let DAC bias and I2S settle while
   // the speaker is disconnected, then prime DMA with actual zero samples.
   int16_t silence[128]{};
   for(int i=0;i<24;++i) {
     if(esp_codec_dev_write(codec_,silence,sizeof(silence))!=ESP_CODEC_DEV_OK) {
       esp_codec_dev_close(codec_); return false;
     }
   }
   vTaskDelay(pdMS_TO_TICKS(20));
   if(xTaskCreatePinnedToCore(worker,"travel-synth",4096,this,9,nullptr,0)!=pdPASS) { esp_codec_dev_close(codec_); return false; }
   started_=true;
   ESP_LOGI("travel-synth","ES8311 via official C-BSP: 48000 Hz mono, volume 70, ten voices");
 }
 xSemaphoreTake(mutex_,portMAX_DELAY); synth_.configure(settings); enabled_=enabled; xSemaphoreGive(mutex_);
 gpio_set_level(BSP_POWER_AMP_IO,enabled && healthy_?1:0);
 return !enabled || healthy_;
}
bool SpeakerAudio::send(sm::MidiEvent event) {
 if(!started_ || !enabled_ || !healthy_) return event.kind!=sm::MidiKind::NoteOn;
 xSemaphoreTake(mutex_,portMAX_DELAY); synth_.event(event); xSemaphoreGive(mutex_); return true;
}
void SpeakerAudio::worker(void* context) {
 auto& self=*static_cast<SpeakerAudio*>(context);
 int16_t samples[128]{};
 int64_t last=0;
 for(;;) {
   if(!self.enabled_ || !self.healthy_) { last=0; vTaskDelay(pdMS_TO_TICKS(5)); continue; }
   const int64_t now=esp_timer_get_time();
   if(last && now-last>10000) power_diagnostics::record(sm::PowerTraceKind::AudioGap,uint32_t(now/1000),0,0,uint32_t(now-last));
   last=now;
   xSemaphoreTake(self.mutex_,portMAX_DELAY); self.synth_.render(samples,128); xSemaphoreGive(self.mutex_);
   if(esp_codec_dev_write(self.codec_,samples,sizeof(samples))!=ESP_CODEC_DEV_OK) {
     self.healthy_=false; gpio_set_level(BSP_POWER_AMP_IO,0);
     ESP_LOGE("travel-synth","Speaker write failed; MIDI remains disabled while Travel Mode is active");
   }
 }
}

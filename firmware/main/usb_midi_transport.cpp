#include "usb_midi_transport.hpp"
#include "ble_midi_protocol.hpp"
#include "usb_midi_protocol.hpp"
#include "power_control.hpp"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include <atomic>
#include <cstdio>
namespace {
constexpr uint16_t config_length=TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN+TUD_MIDI_DESC_LEN;
const uint8_t config_descriptor[]={
 TUD_CONFIG_DESCRIPTOR(1,4,0,config_length,TUSB_DESC_CONFIG_ATT_SELF_POWERED,100),
 TUD_CDC_DESCRIPTOR(0,5,0x81,8,0x02,0x82,64),
 TUD_MIDI_DESCRIPTOR(2,4,0x03,0x83,64)
};
// Espressif development VID + TinyUSB composite PID. Not a separately assigned
// product identity; a public/commercial hardware release needs a licensed ID.
const tusb_desc_device_t device_descriptor={
 sizeof(tusb_desc_device_t),TUSB_DESC_DEVICE,0x0200,
 TUSB_CLASS_MISC,MISC_SUBCLASS_COMMON,MISC_PROTOCOL_IAD,64,
 TINYUSB_ESPRESSIF_VID,0x4005,0x0101,1,2,3,1
};
char serial[13]{};
const char language[]={0x09,0x04};
const char* strings[]={language,"Project BAX:KI:ZL","BAX:KI:ZL",serial,"BAX:KI:ZL MIDI","BAX:KI:ZL Serial"};
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
sm::BleMidiOutbox outbox; // Proven bounded queue/session/CC120+123 cleanup logic.
std::array<sm::MidiRealtime,128> incoming{};
size_t head=0,count=0;
bool enabled=false;
std::atomic<bool> mounted{false},boot_requested{false},baud1200{false};
std::atomic<bool> stopping{false},worker_stopped{false};
SemaphoreHandle_t delivery_gate=nullptr;
uint32_t now_ms() { return uint32_t(esp_timer_get_time()/1000); }
void received(sm::MidiRealtime event) {
 portENTER_CRITICAL(&mux);
 if(count==incoming.size()) { head=0; count=1; incoming[0]={0xff,event.time_us}; }
 else { incoming[(head+count)%incoming.size()]=event; ++count; }
 portEXIT_CRITICAL(&mux);
}
void cdc_coding(int,cdcacm_event_t* e) {
 baud1200=e->line_coding_changed_data.p_line_coding->bit_rate==1200;
}
void cdc_lines(int,cdcacm_event_t* e) {
 if(baud1200 && !e->line_state_changed_data.dtr) boot_requested=true;
}
void worker(void* context) {
 auto& pmic=*static_cast<PowerControl*>(context);
 bool powered=false,previous=false; uint32_t checked=now_ms()-100,stalled=0;
 for(;;) {
   if(stopping) { worker_stopped=true; vTaskDelete(nullptr); }
   const auto now=now_ms();
   if(uint32_t(now-checked)>=100) {
     uint8_t status=0;
     if(pmic.read(0x00,status)) {
       const bool power=(status&0x20)!=0;
       if(power!=powered) { if(power) tud_connect(); else tud_disconnect(); powered=power; }
     }
     checked=now;
   }
   const bool live=powered && tud_mounted() && tud_midi_mounted() && !tud_suspended();
   if(live!=previous) {
     xSemaphoreTake(delivery_gate,portMAX_DELAY);
     portENTER_CRITICAL(&mux); mounted=live; head=count=0; outbox.session(live && enabled); portEXIT_CRITICAL(&mux);
     xSemaphoreGive(delivery_gate);
     previous=live; stalled=0;
     received({0xff,uint64_t(esp_timer_get_time())});
     ESP_LOGI("usb-midi","USB MIDI %s",live?"mounted":"disconnected/suspended");
   }
   sm::UsbMidiPacket packet{};
   for(int i=0;i<128 && tud_midi_packet_read(packet.data());++i) {
     const auto status=sm::usb_midi_realtime(packet);
     if(status && live) received({status,uint64_t(esp_timer_get_time())});
   }
   for(int i=0;i<16 && live;++i) {
     xSemaphoreTake(delivery_gate,portMAX_DELAY);
     sm::BleMidiOutbox::Pending pending;
     portENTER_CRITICAL(&mux); const bool have=outbox.peek(now,pending); portEXIT_CRITICAL(&mux);
     if(!have) { xSemaphoreGive(delivery_gate); break; }
     packet={uint8_t(pending.packet[2]>>4),pending.packet[2],pending.packet[3],pending.packet[4]};
     const bool accepted=tud_midi_packet_write(packet.data());
     portENTER_CRITICAL(&mux);
     if(accepted) { outbox.delivered(pending); stalled=0; }
     else if(!stalled) stalled=now;
     else if(uint32_t(now-stalled)>250) { outbox.session(enabled); stalled=0; }
     portEXIT_CRITICAL(&mux);
     xSemaphoreGive(delivery_gate);
     if(!accepted) break;
   }
   vTaskDelay(pdMS_TO_TICKS(1));
 }
}
}
esp_err_t UsbMidiTransport::start(PowerControl& pmic) {
 delivery_gate=xSemaphoreCreateMutex(); if(!delivery_gate) return ESP_ERR_NO_MEM;
 uint8_t mac[6]{}; ESP_ERROR_CHECK(esp_read_mac(mac,ESP_MAC_WIFI_STA));
 std::snprintf(serial,sizeof(serial),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
 tinyusb_config_t config=TINYUSB_DEFAULT_CONFIG();
 config.descriptor.device=&device_descriptor; config.descriptor.string=strings;
 config.descriptor.string_count=6; config.descriptor.full_speed_config=config_descriptor;
 // This board reports VBUS through the AXP2101, not a verified free GPIO.
 // The worker controls attachment from that read-only PMIC status instead.
 config.task.priority=5; config.task.size=4096;
 esp_err_t result=tinyusb_driver_install(&config); if(result!=ESP_OK) return result;
 tud_disconnect();
 tinyusb_config_cdcacm_t cdc{}; cdc.cdc_port=TINYUSB_CDC_ACM_0;
 cdc.callback_line_coding_changed=cdc_coding; cdc.callback_line_state_changed=cdc_lines;
 result=tinyusb_cdcacm_init(&cdc); if(result!=ESP_OK) return result;
 result=tinyusb_console_init(0); if(result!=ESP_OK) return result;
 setvbuf(stdout,nullptr,_IONBF,0); setvbuf(stderr,nullptr,_IONBF,0);
 if(xTaskCreatePinnedToCore(worker,"usb-midi",4096,&pmic,5,nullptr,0)!=pdPASS) return ESP_ERR_NO_MEM;
 ESP_LOGI("usb-midi","BAX:KI:ZL: USB MIDI 1.0 + CDC; serial %s",serial); return ESP_OK;
}
bool UsbMidiTransport::connected() { return mounted; }
bool UsbMidiTransport::send(sm::MidiEvent event) {
 portENTER_CRITICAL(&mux); const bool ok=enabled?outbox.enqueue(event,now_ms()):event.kind!=sm::MidiKind::NoteOn; portEXIT_CRITICAL(&mux); return ok;
}
void UsbMidiTransport::set_enabled(bool value) {
 if(delivery_gate && !value) {
   const auto start=now_ms();
   for(;;) {
     sm::BleMidiOutbox::Pending pending;
     portENTER_CRITICAL(&mux); const bool have=outbox.peek(now_ms(),pending); portEXIT_CRITICAL(&mux);
     if(!have || uint32_t(now_ms()-start)>=100) break;
     vTaskDelay(pdMS_TO_TICKS(1));
   }
 }
 if(delivery_gate) xSemaphoreTake(delivery_gate,portMAX_DELAY);
 portENTER_CRITICAL(&mux);
 // A muted but still connected old destination must finish CC120/123 cleanup.
 if(value!=enabled) { enabled=value; outbox.session(mounted); }
 portEXIT_CRITICAL(&mux);
 if(delivery_gate) xSemaphoreGive(delivery_gate);
}
bool UsbMidiTransport::reset_requested(uint32_t& generation) {
 portENTER_CRITICAL(&mux); const bool reset=outbox.reset_pending(); generation=outbox.generation(); portEXIT_CRITICAL(&mux); return reset;
}
void UsbMidiTransport::acknowledge_reset(uint32_t generation) {
 portENTER_CRITICAL(&mux); outbox.acknowledge_reset(generation); portEXIT_CRITICAL(&mux);
}
bool UsbMidiTransport::receive_realtime(sm::MidiRealtime& event) {
 portENTER_CRITICAL(&mux); const bool have=count!=0;
 if(have) { event=incoming[head]; head=(head+1)%incoming.size(); --count; }
 portEXIT_CRITICAL(&mux); return have;
}
bool usb_diagnostics_connected() { return tud_cdc_connected(); }
bool usb_bootloader_requested() { return boot_requested; }
[[noreturn]] void usb_enter_bootloader() {
 stopping=true;
 while(!worker_stopped) vTaskDelay(pdMS_TO_TICKS(1));
 // Software reset does not reset the S3 USB peripheral/PHY selector. Tear
 // down OTG and return the shared internal PHY to Serial/JTAG for the ROM.
 freopen("/dev/null","w",stdout); freopen("/dev/null","w",stderr);
 tinyusb_driver_uninstall();
 usb_phy_config_t phy{}; phy.controller=USB_PHY_CTRL_SERIAL_JTAG; phy.target=USB_PHY_TARGET_INT;
 usb_phy_handle_t handle=nullptr; usb_new_phy(&phy,&handle);
 // Same documented ROM-download request used by IDF's USB console restart
 // handler. No eFuses or persistent application settings are modified.
 REG_WRITE(RTC_CNTL_OPTION1_REG,RTC_CNTL_FORCE_DOWNLOAD_BOOT);
 esp_restart();
}

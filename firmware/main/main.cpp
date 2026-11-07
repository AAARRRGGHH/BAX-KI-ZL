#include "music.hpp"
#include "bounce_storage.hpp"
#include "chord_storage.hpp"
#include "ui.hpp"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <atomic>
#include "power_control.hpp"
#include "ble_midi_transport.hpp"
#include "usb_midi_transport.hpp"
#include "midi_transport_mux.hpp"
#include "setup_storage.hpp"
#include "power_diagnostics.hpp"
#include "speaker_audio.hpp"
#include "motion_reset.hpp"
#include "charge_screen.hpp"
#include "esp_system.h"
#include "esp_attr.h"
#include "performance_storage.hpp"
#include "arp_storage.hpp"
#include "music_slot_storage.hpp"
namespace {
// ESP-IDF RTC_NOINIT_ATTR preserves this word across esp_restart(), without
// additional flash writes. A cold/watchdog reset never accepts the marker.
RTC_NOINIT_ATTR uint32_t charge_handoff;
struct InputEvent { int button; sm::ButtonEvent event; uint32_t time; bool raw,down; };
QueueHandle_t input_events=nullptr;
QueueHandle_t battery_events=nullptr;
std::atomic<uint32_t> lost_events{0};
std::atomic<uint32_t> delayed_scan_ms{0};
PowerControl pmic;
bool boot_down=false,shutdown_sent=false;
uint32_t boot_started=0;
uint32_t power_off_at=0;
BleMidiTransport ble_transport;
UsbMidiTransport usb_transport;
sm::MidiTransportMux transport(ble_transport,usb_transport);
MotionReset motion;
void enqueue(InputEvent event) {
 power_diagnostics::record(event.raw?sm::PowerTraceKind::Raw:sm::PowerTraceKind::Button,
   event.time,uint8_t(event.button),event.raw?uint16_t(event.down):uint16_t(event.event));
 if(xQueueSend(input_events,&event,0)!=pdTRUE) ++lost_events;
}
class NvsSettings: public sm::Settings {
public:
 sm::MotionSettings load_motion()override{
  sm::MotionBytes bytes{};sm::MotionSettings s;nvs_handle_t h;if(nvs_open("smartmidi",NVS_READONLY,&h)!=ESP_OK)return s;
  size_t size=bytes.size();const auto err=nvs_get_blob(h,"motion_v1",bytes.data(),&size);nvs_close(h);
  if(err==ESP_OK && size==bytes.size())sm::unpack_motion(bytes,s);
  return s;
 }
 bool save_motion(sm::MotionSettings s)override{
  if(!sm::valid_motion(s))return false;
  const auto bytes=sm::pack_motion(s);nvs_handle_t h;if(nvs_open("smartmidi",NVS_READWRITE,&h)!=ESP_OK)return false;
  auto err=nvs_set_blob(h,"motion_v1",bytes.data(),bytes.size());if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);return err==ESP_OK;
 }
private:
 bool runtime(sm::PerformanceSettings& value,sm::ArpPreset& arp,sm::bounce::Settings* kinetic=nullptr) {
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READONLY,&h)!=ESP_OK) return false;
   std::array<uint8_t,48> bytes{}; size_t size=bytes.size();
   const auto error=nvs_get_blob(h,"runtime_v1",bytes.data(),&size); nvs_close(h);
   if(error!=ESP_OK || (size!=32 && size!=48)) return false;
   if(size==48){sm::bounce::Bytes k{};std::copy_n(bytes.begin()+32,16,k.begin());sm::bounce::Settings decoded;if(!sm::bounce::unpack(k,decoded))return false;if(kinetic)*kinetic=decoded;}else if(kinetic)return false;
   std::array<uint8_t,24> performance_bytes{}; std::copy_n(bytes.begin(),24,performance_bytes.begin());
   uint64_t packed=0; for(int i=0;i<8;++i) packed|=uint64_t(bytes[24+i])<<(8*i);
   arp=sm::unpack_arpeggiator(packed);
   return sm::unpack_performance(performance_bytes,value) && sm::pack_arpeggiator(arp)==packed;
 }
 bool write_runtime(sm::PerformanceSettings value,sm::ArpPreset arp,const sm::bounce::Settings* kinetic=nullptr) {
   if(!sm::valid(value.keyboard) || !sm::valid(value.arp) || !sm::valid(value.chords) || !sm::valid_setup(value.setup) || !sm::valid_arp_preset(arp)) return false;
   std::array<uint8_t,48> bytes{}; const auto performance_bytes=sm::pack_performance(value);
   std::copy(performance_bytes.begin(),performance_bytes.end(),bytes.begin()); const auto packed=sm::pack_arpeggiator(arp);
   for(int i=0;i<8;++i) bytes[24+i]=uint8_t(packed>>(8*i));
   const auto k=sm::bounce::pack(kinetic?*kinetic:load_bounce());std::copy(k.begin(),k.end(),bytes.begin()+32);
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READWRITE,&h)!=ESP_OK) return false;
   auto error=nvs_set_blob(h,"runtime_v1",bytes.data(),bytes.size());
   if(error==ESP_OK) error=nvs_commit(h);
   nvs_close(h); return error==ESP_OK;
 }
 bool performance(sm::PerformanceSettings& value) {
   sm::ArpPreset arp; if(runtime(value,arp)) return true;
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READONLY,&h)!=ESP_OK) return false;
   std::array<uint8_t,24> bytes{}; size_t size=bytes.size();
   const auto error=nvs_get_blob(h,"performance_v1",bytes.data(),&size); nvs_close(h);
   return error==ESP_OK && size==bytes.size() && sm::unpack_performance(bytes,value);
 }
 bool harmony(sm::Selection& scale,sm::ChordSelection& chords) {
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READONLY,&h)!=ESP_OK) return false;
   uint64_t value=0; auto err=nvs_get_u64(h,"harmony",&value); nvs_close(h);
   return err==ESP_OK && sm::unpack_harmony(value,scale,chords);
 }
public:
 sm::bounce::Settings load_bounce()override{sm::PerformanceSettings performance;sm::ArpPreset arp;sm::bounce::Settings value;if(runtime(performance,arp,&value))return value;sm::bounce::Bytes bytes{};nvs_handle_t h;if(nvs_open("smartmidi",NVS_READONLY,&h)!=ESP_OK)return {};size_t size=bytes.size();const auto error=nvs_get_blob(h,"bounce_v1",bytes.data(),&size);nvs_close(h);return error==ESP_OK && size==bytes.size() && sm::bounce::unpack(bytes,value)?value:sm::bounce::Settings{};}
 bool save_bounce(sm::bounce::Settings value)override{if(!sm::bounce::valid_settings(value))return false;return write_runtime({load(),load_arp(),load_chords(),load_setup()},load_arpeggiator(),&value);}

 sm::ArpPreset load_arpeggiator() override {
   sm::PerformanceSettings saved; sm::ArpPreset arp; if(runtime(saved,arp)) return arp;
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READONLY,&h)!=ESP_OK) return {};
   uint64_t value=0; const auto error=nvs_get_u64(h,"arp_state_v1",&value); nvs_close(h);
   return error==ESP_OK?sm::unpack_arpeggiator(value):sm::ArpPreset{};
 }
 bool save_arpeggiator(sm::ArpPreset value) override {
   return write_runtime({load(),load_arp(),load_chords(),load_setup()},value);
 }
 bool read_slot_bank(sm::MusicSlotBank& bank) {
   nvs_handle_t h; const auto opened=nvs_open("smartmidi",NVS_READONLY,&h);
   if(opened==ESP_ERR_NVS_NOT_FOUND) { bank={}; return true; }
   if(opened!=ESP_OK) return false;
   sm::MusicBankBytes bytes{}; size_t size=bytes.size();
   const auto error=nvs_get_blob(h,"music_bank_v1",bytes.data(),&size);
   if(error==ESP_OK) { nvs_close(h); return sm::unpack_music_bank_data(bytes,size,bank); }
   if(error!=ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return false; }
   bank={};
   for(int i=0;i<sm::music_slot_count;++i) {
     char key[16]; std::snprintf(key,sizeof(key),"music_slot%d",i+1);
     sm::MusicSlotBytes slot{}; size_t length=slot.size(); sm::MusicPreset value;
     const auto read=nvs_get_blob(h,key,slot.data(),&length);
     if(read==ESP_OK && sm::unpack_music_slot_data(slot,length,value)) bank.slots[bank.count++]=value;
     else if(read!=ESP_OK && read!=ESP_ERR_NVS_NOT_FOUND && read!=ESP_ERR_NVS_INVALID_LENGTH) { nvs_close(h); return false; }
   }
   nvs_close(h); return true;
 }
 bool write_slot_bank(const sm::MusicSlotBank& bank) {
   const auto bytes=sm::pack_music_bank(bank); nvs_handle_t h;
   if(nvs_open("smartmidi",NVS_READWRITE,&h)!=ESP_OK) return false;
   auto error=nvs_set_blob(h,"music_bank_v1",bytes.data(),bytes.size());
   if(error==ESP_OK) error=nvs_commit(h);
   nvs_close(h); return error==ESP_OK;
 }
 bool load_slot(int slot,sm::MusicPreset& value) override {
   sm::MusicSlotBank bank; if(slot<0 || slot>=sm::music_slot_count || !read_slot_bank(bank) || slot>=bank.count) return false;
   value=bank.slots[slot]; return true;
 }
 bool save_slot(int slot,const sm::MusicPreset& value) override {
   sm::MusicSlotBank bank; return read_slot_bank(bank) && bank.put(slot,value) && write_slot_bank(bank);
 }
 bool delete_slot(int slot) override {
   sm::MusicSlotBank bank; return read_slot_bank(bank) && bank.erase(slot) && write_slot_bank(bank);
 }
 bool save_music(const sm::MusicPreset& value,sm::Setup setup) override {
   if(!sm::valid_music_preset(value)) return false;
   setup.global_scale=value.global_scale;
   return write_runtime({value.keyboard,value.arp,value.chords,setup},value.arpeggiator,&value.kinetic);
 }
 sm::Selection load_arp() override { sm::PerformanceSettings s; return performance(s)?s.arp:load(); }
 bool save_all(sm::Selection keyboard,sm::Selection arp,sm::ChordSelection chords,sm::Setup setup) override {
   return write_runtime({keyboard,arp,chords,setup},load_arpeggiator());
 }
 sm::ChordSelection load_chords() override {
   sm::PerformanceSettings saved; if(performance(saved)) return saved.chords;
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READONLY,&h)!=ESP_OK) return {};
   uint64_t rhythm_strum=0; const auto rhythm_err=nvs_get_u64(h,"chords_v10",&rhythm_strum);
   if(rhythm_err==ESP_OK) { nvs_close(h); return sm::unpack_chords(rhythm_strum); }
   uint64_t sync_strum=0; const auto sync_err=nvs_get_u64(h,"chords_v9",&sync_strum);
   if(sync_err==ESP_OK) { nvs_close(h); return sm::unpack_chords(sync_strum); }
   uint64_t strum_wide=0; const auto wide_err=nvs_get_u64(h,"chords_v8",&strum_wide);
   if(wide_err==ESP_OK) { nvs_close(h); return sm::unpack_chords(strum_wide); }
   uint64_t strummed=0; const auto strummed_err=nvs_get_u64(h,"chords_v7",&strummed);
   if(strummed_err==ESP_OK) { nvs_close(h); return sm::unpack_chords(strummed); }
   uint64_t shifted=0; const auto shifted_err=nvs_get_u64(h,"chords_v6",&shifted);
   if(shifted_err==ESP_OK) { nvs_close(h); return sm::unpack_chords(shifted); }
   uint64_t edited=0; const auto edited_err=nvs_get_u64(h,"chords_v5",&edited);
   if(edited_err==ESP_OK) { nvs_close(h); return sm::unpack_chords(edited); }
   uint32_t value=0; auto err=nvs_get_u32(h,"chords",&value); nvs_close(h);
   if(err==ESP_OK && ((value>>16)==3 || (value>>16)==4)) return sm::unpack_chords(value);
   sm::Selection scale; sm::ChordSelection chords;
   if(harmony(scale,chords)) return chords;
   return err==ESP_OK?sm::unpack_chords(value):sm::ChordSelection{};
 }
 bool save_chords(sm::ChordSelection s) override {
   if(!sm::valid(s)) return false;
   sm::PerformanceSettings saved; if(performance(saved)) return save_all(saved.keyboard,saved.arp,s,saved.setup);
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READWRITE,&h)!=ESP_OK) return false;
   auto err=nvs_set_u64(h,"chords_v10",sm::pack_chords(s));
   if(err==ESP_OK) err=nvs_commit(h);
   nvs_close(h); return err==ESP_OK;
 }
 sm::Setup load_setup() override {
   sm::PerformanceSettings saved; if(performance(saved)) return saved.setup;
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READONLY,&h)!=ESP_OK) return {};
   uint64_t value=0; auto err=nvs_get_u64(h,"setup",&value); nvs_close(h);
   return err==ESP_OK?sm::unpack_setup(value):sm::Setup{};
 }
 bool save_setup(sm::Setup s) override {
   if(!sm::valid_setup(s)) return false;
   sm::PerformanceSettings saved; if(performance(saved)) return save_all(saved.keyboard,saved.arp,saved.chords,s);
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READWRITE,&h)!=ESP_OK) return false;
   // One versioned NVS item keeps channel/color together across power loss.
   auto err=nvs_set_u64(h,"setup",sm::pack_setup(s));
   if(err==ESP_OK) err=nvs_commit(h);
   nvs_close(h); return err==ESP_OK;
 }
 sm::Selection load() override {
   sm::PerformanceSettings saved; if(performance(saved)) return saved.keyboard;
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READONLY,&h)!=ESP_OK) return {};
   uint32_t value=0; auto err=nvs_get_u32(h,"selection",&value); nvs_close(h);
   sm::Selection s{int((value>>8)&255),int(value&255)};
   if(err==ESP_OK && (value>>16)==2 && sm::valid(s)) return s;
   sm::Selection scale; sm::ChordSelection chords; if(harmony(scale,chords)) return scale;
   return err==ESP_OK && (value>>16)==1 && sm::valid(s)?s:sm::Selection{};
 }
 bool save(sm::Selection s) override {
   if(!sm::valid(s)) return false;
   sm::PerformanceSettings saved; if(performance(saved)) return save_all(s,saved.arp,saved.chords,saved.setup);
   nvs_handle_t h; if(nvs_open("smartmidi",NVS_READWRITE,&h)!=ESP_OK) return false;
   auto err=nvs_set_u32(h,"selection",0x20000u|(uint32_t(s.root)<<8)|uint32_t(s.mode));
   if(err==ESP_OK) err=nvs_commit(h);
   nvs_close(h); return err==ESP_OK;
 }

};
void diagnostic(int b,sm::ButtonEvent e,void* context) {
 (void)context;
 enqueue({b,e,uint32_t(esp_timer_get_time()/1000),false,false});
}
// Entered before BLE, audio, motion and input workers are started. Display lock
// is held on entry and exit; no ESP restart is needed to enter the instrument.
void run_charge_mode(lv_display_t* display,uint32_t accent) {
 sm::ChargeScreen screen; sm::ChargeSession session; sm::PmicPowerKey key;
 const bool key_ready=pmic.initialize_power_key();
 screen.build(accent); sm::BatteryState battery=pmic.battery_status();
 uint32_t sample_at=uint32_t(esp_timer_get_time()/1000),paint_at=sample_at;
 screen.update(battery,sample_at);
 lv_obj_invalidate(lv_display_get_screen_active(display)); lv_refr_now(display);
 ESP_ERROR_CHECK(bsp_display_brightness_set(65)); bsp_display_unlock();
 ESP_LOGI("charge","Charge-only screen active; PWR enters instrument");
 bool enter=false;
 for(;;) {
   const auto now=uint32_t(esp_timer_get_time()/1000);
   if(key_ready) {
     uint8_t status=0; const bool ok=pmic.poll_power_key(status);
     key.update(ok,status,now,[](int,sm::ButtonEvent event,void* context) {
       if(event==sm::ButtonEvent::Press) *static_cast<bool*>(context)=true;
     },&enter);
   }
   if(uint32_t(now-sample_at)>=1000) {
     const auto previous=battery; battery=pmic.battery_status(); sample_at=now;
     if(battery.valid!=previous.valid || battery.percent!=previous.percent || battery.charging!=previous.charging ||
        battery.charge_done!=previous.charge_done || battery.usb_present!=previous.usb_present)
       ESP_LOGI("charge","valid=%d percent=%d current_charging=%d complete=%d USB=%d",int(battery.valid),battery.percent,
         int(battery.charging),int(battery.charge_done),int(battery.usb_present));
   }
   if(usb_bootloader_requested()) { bsp_display_brightness_set(0); usb_enter_bootloader(); }
   const auto action=session.update(battery,now,enter);
   if(action==sm::ChargeAction::Instrument) break;
   if(uint32_t(now-paint_at)>=100) {
     // BSP returns esp_err_t: ESP_OK is zero, not boolean true. Every
     // successful acquisition must be released so the LVGL worker can draw.
     if(bsp_display_lock(100)==ESP_OK) {
       // Take the animation timestamp after acquiring the graphics lock so
       // waiting for a redraw cannot move the blink clock backwards.
       const auto paint_now=uint32_t(esp_timer_get_time()/1000);
       screen.update(battery,paint_now,uint32_t(paint_now-sample_at)<5000);
       bsp_display_unlock();
     }
     paint_at=now;
   }
   if(action==sm::ChargeAction::Shutdown) {
     ESP_LOGI("charge","Cable removed or charge complete for 60s: requesting PMIC shutdown");
     ESP_ERROR_CHECK(bsp_display_brightness_set(0));
     const bool accepted=pmic.shutdown();
     vTaskDelay(pdMS_TO_TICKS(1000));
     // If the CPU still executes, don't issue an endless series of PMIC writes.
     // Keep the screen/PWR usable and report the actual hardware limitation.
     ESP_LOGE("charge","PMIC shutdown did not cut SoC power (write=%d); charge screen remains active",int(accepted));
     ESP_ERROR_CHECK(bsp_display_brightness_set(65));
   }
   vTaskDelay(pdMS_TO_TICKS(10));
 }
 ESP_ERROR_CHECK(bsp_display_lock(uint32_t(-1)));
 ESP_ERROR_CHECK(bsp_display_brightness_set(0)); screen.close();
 ESP_LOGI("charge","PWR: starting instrument");
}
}
extern "C" void app_main() {
 SpeakerAudio::early_mute();
 const bool requested_charge=sm::consume_charge_handoff(charge_handoff,esp_reset_reason()==ESP_RST_SW);
 // Never erase NVS automatically: a failure needs inspection, not data loss.
 ESP_ERROR_CHECK(nvs_flash_init());
 ESP_LOGI("board","ESP32-S3-Touch-AMOLED-1.75C / SKU 33691; original orientation, centre swipes and two touch contacts");
 auto* display=bsp_display_start();
 if(!display) { ESP_LOGE("board","Display initialization failed"); return; }
 ESP_LOGI("display-memory","After display: DMA free=%u largest=%u bytes",
   unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA)),
   unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA)));
 static NvsSettings settings; static sm::Application app(settings);
 static SpeakerAudio audio; static sm::PerformanceOutput output(transport,audio);
 static sm::MidiRouter midi(output); static sm::Ui ui(app,midi,&output);
 ESP_ERROR_CHECK(bsp_display_lock(uint32_t(-1)));
 ESP_ERROR_CHECK(bsp_display_rotation_set(BSP_DISPLAY_ROTATE_0));
 // The original BSP touch calibration is used without a rotation wrapper.
 if(!pmic.initialize(bsp_i2c_get_handle())) ESP_LOGW("power","Long-press policy not confirmed; see power log");
 ESP_ERROR_CHECK(usb_transport.start(pmic));
 uint8_t source=0; pmic.read(0x20,source);
 ESP_LOGI("power","Boot source 0x20=0x%02x, ESP reset reason=%d",source,int(esp_reset_reason()));
 if(requested_charge || sm::usb_charge_boot(pmic,esp_reset_reason()==ESP_RST_POWERON)) run_charge_mode(display,app.setup.accent);
 ui.build();
 ui.show_boot_logo();
 // The local BSP starts at zero brightness, including its panel init table.
 // Force a complete first frame while still dark. The SPI brightness command
 // drains queued pixel transfers before restoring emission (panel_io_spi_tx_param).
 lv_obj_invalidate(lv_display_get_screen_active(display));
 lv_refr_now(display);
 ESP_ERROR_CHECK(bsp_display_brightness_set(65));
 bsp_display_unlock();
 ESP_ERROR_CHECK(ble_transport.start());
 // Timing stays independent of LVGL redraws; audio has higher priority.
 configASSERT(xTaskCreatePinnedToCore([](void* context) {
   auto& ui=*static_cast<sm::Ui*>(context);

   sm::MidiRealtime pending{}; bool have=false; uint32_t pending_epoch=0,clock_generation=transport.clock_generation(); TickType_t wake=xTaskGetTickCount();
   for(;;) {
     const uint64_t now=uint64_t(esp_timer_get_time());
     const auto generation=transport.clock_generation();
     if(generation!=clock_generation) { have=false; ui.midi_disconnected(); clock_generation=generation; }
     for(int i=0;i<128;++i) {
       if(!have) have=transport.receive_realtime(pending,&pending_epoch);
       if(have && pending_epoch!=transport.clock_generation()) { have=false; continue; }
       if(have && pending_epoch!=clock_generation) { ui.midi_disconnected(); clock_generation=pending_epoch; }
       if(!have || pending.time_us>now) break;
       if(pending.status==0xff) ui.midi_disconnected(); else ui.midi_realtime(pending.status,pending.time_us);
       have=false;
     }
     ui.performance_tick(now); vTaskDelayUntil(&wake,pdMS_TO_TICKS(1));
   }
 },"arp-clock",4096,&ui,8,nullptr,0)==pdPASS);
 ESP_LOGI("ble-midi","After BLE init: free internal RAM=%u, largest block=%u bytes",
   unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
   unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
 ESP_LOGI("display-memory","After UI/BLE/audio: DMA free=%u largest=%u bytes",
   unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA)),
   unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA)));
 // C schematic: BOOT=GPIO0; PWR via transistor T1/SYS_OUT=GPIO3.
 // SYS_OUT is high when PWR is pressed. No I/O expander on this board.
 gpio_config_t boot{}; boot.pin_bit_mask=1ULL<<GPIO_NUM_0; boot.mode=GPIO_MODE_INPUT;
 boot.pull_up_en=GPIO_PULLUP_ENABLE; ESP_ERROR_CHECK(gpio_config(&boot));
 gpio_config_t power{}; power.pin_bit_mask=1ULL<<GPIO_NUM_3; power.mode=GPIO_MODE_INPUT;
 ESP_ERROR_CHECK(gpio_config(&power));
 input_events=xQueueCreate(96,sizeof(InputEvent));
 configASSERT(input_events);
 const bool power_key_ready=pmic.initialize_power_key();
 battery_events=xQueueCreate(1,sizeof(sm::BatteryState));
 ESP_ERROR_CHECK(battery_events?ESP_OK:ESP_ERR_NO_MEM);
 const auto battery_task=xTaskCreate([](void*) {
   sm::BatteryState previous{}; bool first=true;
   for(;;) {
     const auto sample=pmic.battery_status(); xQueueOverwrite(battery_events,&sample);
     if(first || sample.valid!=previous.valid || sample.connected!=previous.connected ||
       sample.gauge_enabled!=previous.gauge_enabled || sample.percent!=previous.percent || sample.charging!=previous.charging) {
       ESP_LOGI("battery","AXP2101 valid=%d connected=%d gauge=%d percent=%d charging=%d",
         int(sample.valid),int(sample.connected),int(sample.gauge_enabled),sample.percent,int(sample.charging));
       previous=sample; first=false;
     }
     vTaskDelay(pdMS_TO_TICKS(1000));
   }
 },"battery-read",3072,nullptr,2,nullptr);
 ESP_ERROR_CHECK(battery_task==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
 power_diagnostics::start(pmic);
 motion.start(bsp_i2c_get_handle());
 ESP_ERROR_CHECK(bsp_display_lock(uint32_t(-1)));
 lv_timer_create([](lv_timer_t* timer) {
   auto* ui=static_cast<sm::Ui*>(lv_timer_get_user_data(timer));
   std::array<float,3> acceleration{};uint32_t sampled_at=0;
   if(motion.sample(acceleration,sampled_at))ui->motion_sample(acceleration[0],acceleration[1],acceleration[2],sampled_at);
   ui->motion_tick(uint32_t(esp_timer_get_time()/1000));
   const bool allow_pose_reset=ui->can_reset_play_mode();
   motion.enable(allow_pose_reset,ui->is_chords()?1:ui->is_arpeggiator()?2:ui->is_bounce()?3:0);
   if(motion.take_reset() && allow_pose_reset) {
     const bool chords=ui->is_chords(),bounce=ui->is_bounce();
     if(ui->reset_play_mode()) ESP_LOGI("pose-reset","%s reset; key/scale retained",chords?"Chord buttons":bounce?"Bounce defaults and balls":"Arpeggiator defaults and note selection");
     else ESP_LOGW("pose-reset","Cannot reset active play mode; settings retained");
   }
   sm::BatteryState battery;
   if(xQueueReceive(battery_events,&battery,0)==pdTRUE) ui->set_battery(battery);
   transport.service_ui([&]() { if(!app.setup.travel) ui->reset_midi_session(); });
   if(usb_bootloader_requested() && ui->prepare_shutdown()) {
     vTaskDelay(pdMS_TO_TICKS(250)); usb_enter_bootloader();
   }
   InputEvent event;
   for(int i=0;i<32 && xQueueReceive(input_events,&event,0)==pdTRUE;++i) {
     if(event.raw) {
       continue;
     }
     const bool setup_before=ui->is_setup();
     if(event.button==1) ui->power_button(event.event);
     power_diagnostics::record(sm::PowerTraceKind::Dispatch,event.time,uint8_t(event.button),
       uint16_t(event.event)|(setup_before?0x100:0)|(ui->is_setup()?0x200:0),
       uint32_t(esp_timer_get_time()/1000)-event.time,ui->power_off_visible()?1:0);
     // Octave changes use centre-started swipes; PWR hold opens setup.
     if(event.button==0 && event.event==sm::ButtonEvent::Press && !power_off_at) {
       boot_down=true; boot_started=event.time; shutdown_sent=false;
       ui->begin_power_off();
     }
     if(event.button==0 && event.event==sm::ButtonEvent::Release) {
       boot_down=false; ui->cancel_power_off();
     }
   }
   const auto lost=lost_events.exchange(0);
   if(lost) power_diagnostics::record(sm::PowerTraceKind::QueueLost,uint32_t(esp_timer_get_time()/1000),0,0,lost);
   const auto gap=delayed_scan_ms.exchange(0);
   if(gap) power_diagnostics::record(sm::PowerTraceKind::ScanGap,uint32_t(esp_timer_get_time()/1000),0,0,gap);
   if(boot_down && !shutdown_sent && gpio_get_level(GPIO_NUM_0)==0) {
     const uint32_t held=uint32_t(esp_timer_get_time()/1000)-boot_started;
     if(ui->update_power_off(held)) {
       shutdown_sent=true;
       if(!ui->prepare_shutdown()) { ESP_LOGE("power","Arpeggiator settings could not be saved; shutdown cancelled"); return; }
       // Leave BLE host running briefly so queued Note Off/CC123 can leave the radio.
       power_off_at=uint32_t(esp_timer_get_time()/1000)+250;
       ESP_LOGI("power","BOOT held 3s: sending final Note Off before shutdown");
     }
   }
   if(power_off_at && int32_t(uint32_t(esp_timer_get_time()/1000)-power_off_at)>=0) {
     power_off_at=0;
     SpeakerAudio::mute_amplifier();
     if(sm::charge_after_shutdown(pmic)) {
       ESP_LOGI("power","USB present: restarting into charge-only screen after final Note Off");
       charge_handoff=sm::charge_handoff_magic;
       bsp_display_brightness_set(0);
       esp_restart();
     }
     if(!pmic.shutdown()) {
       audio.configure(app.setup.travel,app.setup.synth);
       ui->cancel_shutdown(); ESP_LOGE("power","Shutdown failed; previous screen restored");
     }
   }
 },10,&ui);
 bsp_display_unlock();
 // app_main priority 1 can be starved by the BSP's priority-6 renderer.
 // A short, blocking-free priority-8 scanner preempts graphics for sampling.
 const auto created=xTaskCreatePinnedToCore([](void*) {
   sm::Buttons buttons(10,4); bool previous[2]={false,false};
   uint32_t last=uint32_t(esp_timer_get_time()/1000); TickType_t wake=xTaskGetTickCount();
   for(;;) {
     const uint32_t now=uint32_t(esp_timer_get_time()/1000),gap=now-last; last=now;
     if(gap>10) delayed_scan_ms.store(gap);
     const bool down[2]={gpio_get_level(GPIO_NUM_0)==0,gpio_get_level(GPIO_NUM_3)!=0};
     for(int b=0;b<2;++b) {
       if(down[b]!=previous[b]) { enqueue({b,sm::ButtonEvent::Press,now,true,down[b]}); previous[b]=down[b]; }
       // SYS_OUT is unreliable on battery; capture PWR GPIO only for diagnosis.
       if(b==0) buttons.update(b,down[b],now,diagnostic,nullptr);
     }
     vTaskDelayUntil(&wake,pdMS_TO_TICKS(2));
   }
 },"button-scan",3072,nullptr,8,nullptr,1);
 ESP_ERROR_CHECK(created==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
 if(power_key_ready) {
   // Keep I2C entirely out of the priority-8 BOOT/raw scanner and GUI task.
   const auto key_created=xTaskCreate([](void*) {
     sm::PmicPowerKey key;
     bool failed=false;
     for(;;) {
       uint8_t status=0;
       const bool ok=pmic.poll_power_key(status);
       const auto now=uint32_t(esp_timer_get_time()/1000);
       if(ok && (status&3)) power_diagnostics::record(sm::PowerTraceKind::PmicKey,now,1,status);
       if(!ok || failed) power_diagnostics::record(sm::PowerTraceKind::PmicKeyError,now,1,ok?0:1,status);
       failed=!ok;
       key.update(ok,status,now,diagnostic,nullptr);
       vTaskDelay(pdMS_TO_TICKS(10));
     }
   },"pmic-key",3072,nullptr,7,nullptr);
   ESP_ERROR_CHECK(key_created==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
 }
 ESP_LOGI("startup-memory","All workers started: internal free=%u largest=%u; DMA free=%u largest=%u; PSRAM free=%u bytes",
   unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
   unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
   unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA)),
   unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA)),
   unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
}

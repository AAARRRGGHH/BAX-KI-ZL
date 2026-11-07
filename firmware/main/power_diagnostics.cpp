#include "power_diagnostics.hpp"
#include "power_control.hpp"
#include "driver/gpio.h"
#include "usb_midi_transport.hpp"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <new>
#include <unistd.h>
namespace power_diagnostics {
namespace {
constexpr size_t capacity=2048;
using Trace=sm::PowerTrace<capacity>;
Trace* trace=nullptr;
portMUX_TYPE trace_lock=portMUX_INITIALIZER_UNLOCKED;
uint32_t now_ms() { return uint32_t(esp_timer_get_time()/1000); }
uint32_t inputs() {
 return (gpio_get_level(GPIO_NUM_0)==0?1u:0u) |
        (gpio_get_level(GPIO_NUM_3)!=0?2u:0u) |
        (usb_diagnostics_connected()?4u:0u);
}
const char* kind_name(sm::PowerTraceKind kind) {
 switch(kind) {
 case sm::PowerTraceKind::Start:return "START";
 case sm::PowerTraceKind::Raw:return "RAW";
 case sm::PowerTraceKind::Button:return "BUTTON";
 case sm::PowerTraceKind::Dispatch:return "DISPATCH";
 case sm::PowerTraceKind::GuardReady:return "GUARD_READY";
 case sm::PowerTraceKind::Pmic:return "PMIC";
 case sm::PowerTraceKind::ScanGap:return "SCAN_GAP";
 case sm::PowerTraceKind::QueueLost:return "QUEUE_LOST";
 case sm::PowerTraceKind::PmicKey:return "PMIC_KEY";
 case sm::PowerTraceKind::PmicKeyError:return "PMIC_KEY_ERROR";
 case sm::PowerTraceKind::AudioGap:return "AUDIO_GAP";
 }
 return "UNKNOWN";
}
void* allocate(size_t bytes) {
 auto* memory=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 return memory?memory:heap_caps_malloc(bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
}
void dump() {
 auto* copy=static_cast<sm::PowerTraceEntry*>(allocate(capacity*sizeof(sm::PowerTraceEntry)));
 if(!copy) { printf("PWRTRACE ERROR snapshot allocation failed\n"); return; }
 uint64_t first,end;
 portENTER_CRITICAL(&trace_lock); first=trace->begin(); end=trace->end(); portEXIT_CRITICAL(&trace_lock);
 size_t count=0,skipped=0;
 // Individual short locks keep serial output away from the scanner.
 for(auto i=first;i<end;++i) {
   sm::PowerTraceEntry entry;
   portENTER_CRITICAL(&trace_lock); const bool found=trace->get(i,entry); portEXIT_CRITICAL(&trace_lock);
   if(found) copy[count++]=entry; else ++skipped;
 }
 printf("PWRTRACE BEGIN v=1 first=%llu end=%llu retained=%u overwritten=%llu snapshot_skipped=%u reset_reason=%d\n",
   static_cast<unsigned long long>(first),static_cast<unsigned long long>(end),unsigned(count),
   static_cast<unsigned long long>(first),unsigned(skipped),int(esp_reset_reason()));
 printf("PWRTRACE FIELDS ms,kind,detail,flags,value,extra\n");
 size_t sent=0;
 for(size_t i=0;i<count;++i) {
   const auto& e=copy[i];
   printf("PWRTRACE %lu,%s,%u,0x%04x,0x%08lx,0x%08lx\n",static_cast<unsigned long>(e.time),
     kind_name(e.kind),unsigned(e.detail),unsigned(e.flags),static_cast<unsigned long>(e.value),static_cast<unsigned long>(e.extra));
   ++sent;
   if(i%16==15) vTaskDelay(1);
   if(!usb_diagnostics_connected()) break;
 }
 printf("PWRTRACE END count=%u\n",unsigned(sent)); fflush(stdout);
 heap_caps_free(copy);
}
void worker(void* context) {
 auto& pmic=*static_cast<PowerControl*>(context);
 // This diagnostic task only reads. The separate PWR task owns key IRQ ACKs.
 constexpr uint8_t registers[3][4]={{0x00,0x01,0x34,0x35},{0x10,0x22,0x27,0x30},{0x41,0x49,0x20,0x21}};
 std::array<uint32_t,3> previous{};
 std::array<uint16_t,3> previous_valid{};
 uint32_t last_sample=now_ms()-250,last_heartbeat=now_ms()-1000,previous_inputs=~0u;
 bool initial=true,discard=false;
 char command[32]{}; size_t length=0;
 for(;;) {
   const uint32_t now=now_ms();
   if(uint32_t(now-last_sample)>=250) {
     last_sample=now; const auto pins=inputs();
     const bool heartbeat=initial || uint32_t(now-last_heartbeat)>=1000;
     if(heartbeat) last_heartbeat=now;
     bool read_ok=pmic.diagnostic_ready();
     for(uint8_t group=0;group<3;++group) {
       uint32_t packed=0; uint16_t valid=0;
       for(unsigned byte=0;byte<4 && read_ok;++byte) {
         uint8_t value=0; read_ok=pmic.read(registers[group][byte],value);
         if(read_ok) { packed|=uint32_t(value)<<(byte*8); valid|=1u<<byte; }
       }
       if(heartbeat || pins!=previous_inputs || packed!=previous[group] || valid!=previous_valid[group])
         record(sm::PowerTraceKind::Pmic,now,group,valid,packed,pins);
       previous[group]=packed; previous_valid[group]=valid;
     }
     previous_inputs=pins; initial=false;
   }
   // Default IDF USB VFS reads return immediately; no other stdin reader exists.
   char bytes[32]; const auto size=read(STDIN_FILENO,bytes,sizeof(bytes));
   for(ssize_t i=0;i<size;++i) {
     const char c=bytes[i];
     if(c=='\n' || c=='\r') {
       command[length]='\0';
       if(!discard && std::strcmp(command,"pwr-trace")==0) dump();
       length=0; discard=false;
     } else if(!discard && c>=32 && c<127) {
       if(length+1<sizeof(command)) command[length++]=c; else discard=true;
     }
   }
   vTaskDelay(pdMS_TO_TICKS(20));
 }
}
}
void record(sm::PowerTraceKind kind,uint32_t time,uint8_t detail,uint16_t flags,uint32_t value,uint32_t extra) {
 if(!trace) return;
 const sm::PowerTraceEntry entry{time,kind,detail,flags,value,extra};
 portENTER_CRITICAL(&trace_lock); trace->push(entry); portEXIT_CRITICAL(&trace_lock);
}
bool start(PowerControl& pmic) {
 auto* memory=allocate(sizeof(Trace));
 if(!memory) { ESP_LOGE("pwr-trace","RAM allocation failed; recording disabled"); return false; }
 trace=new(memory) Trace;
 record(sm::PowerTraceKind::Start,now_ms(),0,0,uint32_t(esp_reset_reason()),inputs());
 if(xTaskCreate(worker,"pwr-trace",4096,&pmic,2,nullptr)!=pdPASS) {
   trace->~Trace(); heap_caps_free(trace); trace=nullptr;
   ESP_LOGE("pwr-trace","Task allocation failed; recording disabled"); return false;
 }
 ESP_LOGI("pwr-trace","RAM recording enabled: 2048 entries, command pwr-trace; read-only PMIC sampling");
 return true;
}
}

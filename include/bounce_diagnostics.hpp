#pragma once
#include <cstdint>
#ifdef ESP_PLATFORM
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <atomic>
namespace sm::bounce::diagnostics {
inline std::atomic<bool> active{false};
inline std::atomic<uint32_t> tick_max{0},paint_max{0},damage_max{0},frame_max{0},frames{0},flicks{0},zero_flicks{0},last_speed{0},touch_gap{0};
inline std::atomic<uint32_t> flush_wait_us{0},flush_call_us{0};inline uint64_t flush_wait_started=0,flush_call_started=0;
inline std::atomic<uint32_t> scene_calls{0},live_balls{0},flushed_pixels{0},flushes{0};
inline uint64_t frame_started=0;inline bool rendered=false;inline uint32_t reported=0,last_touch[2]{};
enum Kind {Tick,Paint,Damage};
inline void maximum(std::atomic<uint32_t>& target,uint32_t value){uint32_t old=target.load(std::memory_order_relaxed);while(old<value && !target.compare_exchange_weak(old,value,std::memory_order_relaxed)){} }
struct Scope {
 Kind kind;uint64_t started;
 explicit Scope(Kind k):kind(k),started(active.load(std::memory_order_relaxed)?uint64_t(esp_timer_get_time()):0){}
 ~Scope(){if(started)maximum(kind==Tick?tick_max:kind==Paint?paint_max:damage_max,uint32_t(uint64_t(esp_timer_get_time())-started));}
};
inline void enable(bool value){active=value;reported=0;last_touch[0]=last_touch[1]=0;frames=0;tick_max=0;paint_max=0;damage_max=0;frame_max=0;flicks=0;zero_flicks=0;touch_gap=0;scene_calls=0;live_balls=0;flushed_pixels=0;flushes=0;flush_wait_us=0;flush_call_us=0;flush_wait_started=0;flush_call_started=0;}
inline void work(uint32_t blits){scene_calls.fetch_add(blits,std::memory_order_relaxed);}
inline void scene(uint32_t balls){live_balls.store(balls,std::memory_order_relaxed);}
inline void flick(float speed){++flicks;if(speed<1)++zero_flicks;last_speed=uint32_t(speed);}
inline void touch(int contact,uint32_t now){if(contact<0 || contact>=2)return;if(last_touch[contact])maximum(touch_gap,uint32_t(now-last_touch[contact]));last_touch[contact]=now;}
inline void end_touch(int contact){if(contact>=0 && contact<2)last_touch[contact]=0;}
inline void attach(lv_display_t* display){lv_display_add_event_cb(display,[](lv_event_t* e){
 if(!active.load(std::memory_order_relaxed)){return;}
 const auto code=lv_event_get_code(e);
 if(code==LV_EVENT_REFR_START){frame_started=uint64_t(esp_timer_get_time());rendered=false;}
 if(code==LV_EVENT_RENDER_START)rendered=true;
 if(code==LV_EVENT_FLUSH_START){flush_call_started=uint64_t(esp_timer_get_time());const auto* area=static_cast<const lv_area_t*>(lv_event_get_param(e));if(area){flushed_pixels.fetch_add(uint32_t(area->x2-area->x1+1)*uint32_t(area->y2-area->y1+1),std::memory_order_relaxed);flushes.fetch_add(1,std::memory_order_relaxed);}}
 if(code==LV_EVENT_FLUSH_FINISH && flush_call_started){flush_call_us.fetch_add(uint32_t(uint64_t(esp_timer_get_time())-flush_call_started),std::memory_order_relaxed);flush_call_started=0;}
 if(code==LV_EVENT_FLUSH_WAIT_START)flush_wait_started=uint64_t(esp_timer_get_time());
 if(code==LV_EVENT_FLUSH_WAIT_FINISH && flush_wait_started){flush_wait_us.fetch_add(uint32_t(uint64_t(esp_timer_get_time())-flush_wait_started),std::memory_order_relaxed);flush_wait_started=0;}
 if(code==LV_EVENT_REFR_READY && rendered && frame_started){++frames;maximum(frame_max,uint32_t(uint64_t(esp_timer_get_time())-frame_started));frame_started=0;}
},LV_EVENT_ALL,nullptr);}
inline void report(uint32_t now){
 if(!active.load()){return;}
 if(!reported){reported=now;return;}const uint32_t elapsed=uint32_t(now-reported);if(elapsed<3000)return;reported=now;
 const uint32_t rendered_frames=frames.exchange(0);
 ESP_LOGI("bounce-perf","fps_x10=%lu frame_max_ms=%lu tick_max_us=%lu paint_max_us=%lu touch_gap_ms=%lu flicks=%lu zero=%lu last_speed=%lu balls=%lu scene_blits_per_frame=%lu pixels_per_frame=%lu flushes_per_frame=%lu damage_max_us=%lu flush_wait_us_per_frame=%lu flush_call_us_per_frame=%lu",
   (unsigned long)(rendered_frames*10000/elapsed),(unsigned long)(frame_max.exchange(0)/1000),(unsigned long)tick_max.exchange(0),(unsigned long)paint_max.exchange(0),
   (unsigned long)touch_gap.exchange(0),(unsigned long)flicks.exchange(0),(unsigned long)zero_flicks.exchange(0),(unsigned long)last_speed.load(),
   (unsigned long)live_balls.load(),(unsigned long)(scene_calls.exchange(0)/(rendered_frames?rendered_frames:1)),
   (unsigned long)(flushed_pixels.exchange(0)/(rendered_frames?rendered_frames:1)),(unsigned long)(flushes.exchange(0)/(rendered_frames?rendered_frames:1)),
   (unsigned long)damage_max.exchange(0),(unsigned long)(flush_wait_us.exchange(0)/(rendered_frames?rendered_frames:1)),(unsigned long)(flush_call_us.exchange(0)/(rendered_frames?rendered_frames:1)));
}
}
#else
namespace sm::bounce::diagnostics {
enum Kind {Tick,Paint,Damage};struct Scope{explicit Scope(Kind){}};
inline void enable(bool){}inline void flick(float){}inline void touch(int,uint32_t){}inline void end_touch(int){}inline void report(uint32_t){}
inline void work(uint32_t){}inline void scene(uint32_t){}
}
#endif

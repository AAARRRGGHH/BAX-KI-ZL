#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <limits>
namespace sm {
struct MidiRealtime { uint8_t status=0; uint64_t time_us=0; };
// Allow delayed BLE packets to arrive before their reconstructed clock pulses
// are due. The consumer already schedules future realtime timestamps. Delay
// transport controls by the same amount so they retain their relative order.
inline constexpr uint64_t ble_midi_clock_playout_us=30000;
inline MidiRealtime ble_midi_clock_playout(MidiRealtime event){event.time_us+=ble_midi_clock_playout_us;return event;}
// Decode the BLE timestamp/message framing before inspecting status bytes.
// In particular, a timestamp byte equal to F8 is NOT a Timing Clock message.
class BleMidiRealtimeDecoder {
 bool sysex_=false,anchored_=false;
 uint16_t remote_=0; uint64_t remote_us_=0,arrival_=0,last_output_=0;
 struct Offset { uint64_t second=UINT64_MAX; int64_t minimum=INT64_MAX; };
 std::array<Offset,5> offsets_{};
public:
 void reset() { sysex_=anchored_=false; arrival_=last_output_=0; offsets_={}; }
 template<class Callback> bool parse(const uint8_t* bytes,size_t length,uint64_t now,Callback callback) {
   if(length<2 || (bytes[0]&0xc0)!=0x80) return false;
   struct Event { uint8_t status; uint16_t stamp; };
   std::array<Event,128> events{}; size_t count=0,pos=1;
   bool sysex=sysex_; uint8_t running=0; uint16_t stamp=0,high=bytes[0]&63;
   int previous_low=-1;
   auto realtime=[&](uint8_t value) {
     if(value==0xf8 || value==0xfa || value==0xfb || value==0xfc || value==0xff) {
       if(count>=events.size()) return false;
       events[count++]={value,stamp};
     }
     return true;
   };
   while(pos<length) {
     // A SysEx continuation may start directly with data after the header.
     if(sysex && bytes[pos]<0x80) { ++pos; continue; }
     if(!(bytes[pos]&0x80) || pos+1>=length) return false;
     const int low=bytes[pos++]&127;
     if(previous_low>=0 && low<previous_low) high=(high+1)&63;
     previous_low=low; stamp=uint16_t((high<<7)|low);
     uint8_t status=bytes[pos];
     if(status>=0xf8) { ++pos; if(!realtime(status)) return false; continue; }
     if(sysex) {
       if(status==0xf7) { sysex=false; ++pos; continue; }
       // Timestamped SysEx payload or malformed nested message.
       if(status<0x80) continue;
       return false;
     }
     if(status==0xf0) { sysex=true; running=0; ++pos; continue; }
     if(status&0x80) { ++pos; if(status<0xf0) running=status; else running=0; }
     else { if(!running) return false; status=running; }
     int needed=0;
     if(status<0xf0) needed=((status&0xf0)==0xc0 || (status&0xf0)==0xd0)?1:2;
     else if(status==0xf1 || status==0xf3) needed=1;
     else if(status==0xf2) needed=2;
     else if(status!=0xf6) return false;
     for(int data=0;data<needed;) {
       if(pos>=length) return false;
       uint8_t value=bytes[pos++];
       if(value>=0xf8) { if(!realtime(value)) return false; }
       else if(value&0x80) return false;
       else ++data;
     }
   }
   sysex_=sysex;
   if(!count) return true;
   // Correlate clocks with a five-second minimum-delay window, not individual
   // packet arrival times. BLE packets may carry several delayed clock pulses.
   // Anchor using the LAST timestamp so the first batch never predicts clocks
   // in the future merely because its older events arrived in the same packet.
   if(!anchored_ || now>arrival_+4000000) {
     remote_=events[0].stamp; remote_us_=8192000;
     offsets_={}; anchored_=true;
   }
   // Replay the unwrapped deltas below instead of reserving another timestamp
   // array on the small NimBLE host stack.
   const auto first_remote=remote_; const auto first_us=remote_us_;
   size_t accepted=0;
   for(size_t i=0;i<count;++i) {
     const int delta=(int(events[i].stamp)-remote_+8192)%8192;
     if(delta>4096) { events[i].status=0; continue; } // Stale/out-of-order timestamp.
     remote_us_+=uint64_t(delta)*1000; remote_=events[i].stamp;
     ++accepted;
   }
   if(!accepted) return true;
   const auto second=now/1000000;
   auto& bucket=offsets_[second%offsets_.size()];
   if(bucket.second!=second) bucket={second,INT64_MAX};
   bucket.minimum=std::min(bucket.minimum,int64_t(now)-int64_t(remote_us_));
   int64_t offset=INT64_MAX;
   for(const auto& sample:offsets_)
     if(sample.second<=second && second-sample.second<offsets_.size()) offset=std::min(offset,sample.minimum);
   arrival_=now;
   auto cursor=first_remote; auto cursor_us=first_us;
   for(size_t i=0;i<count;++i) if(events[i].status) {
     cursor_us+=uint64_t((int(events[i].stamp)-cursor+8192)%8192)*1000;
     cursor=events[i].stamp;
     const auto mapped=int64_t(cursor_us)+offset;
     const uint64_t stamp=std::max(last_output_,uint64_t(std::max<int64_t>(0,mapped)));
     last_output_=stamp;
     callback(MidiRealtime{events[i].status,stamp});
   }
   return true;
 }
};
}

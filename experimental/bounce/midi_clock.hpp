#pragma once
#include <algorithm>
#include <cstdint>
namespace sm::bounce {
// MIDI Clock has 24 pulses/quarter. Eight units/pulse give exact straight,
// dotted and triplet divisions down to 1/32, including half-pulse boundaries.
class MidiClock {
 bool enabled_=false,running_=false,have_tick_=false,fresh_interval_=true,tempo_known_=false;
 uint64_t last_=0,units_=0;double interval_=60000000./(120*24);
public:
 bool enabled()const{return enabled_;}
 bool tempo_known()const{return tempo_known_;}
 void enable(bool value){if(enabled_==value)return;enabled_=value;disconnect();units_=0;}
 void disconnect(){running_=false;have_tick_=false;fresh_interval_=true;last_=0;}
 void realtime(uint8_t status,uint64_t now){
   if(!enabled_)return;
   if(status==0xfa){units_=0;running_=true;have_tick_=false;fresh_interval_=true;last_=0;}
   else if(status==0xfb){running_=true;have_tick_=false;fresh_interval_=true;last_=0;}
   else if(status==0xff){disconnect();units_=0;}
   else if(status==0xfc)running_=false;
   else if(status==0xf8){
     if(have_tick_ && now>last_){const auto gap=now-last_;if(gap>=2000 && gap<=150000){interval_=fresh_interval_?double(gap):.8*interval_+.2*gap;fresh_interval_=false;tempo_known_=true;}}
     if(running_ && have_tick_)units_+=8;
     last_=now;have_tick_=true;
   }
 }
 void service(uint64_t now){if(have_tick_ && now>=last_ && now-last_>500000)disconnect();}
 bool ready(uint64_t now)const{return enabled_ && running_ && have_tick_ && now>=last_ && now-last_<=500000;}
 double position(uint64_t now)const{return double(units_)+(ready(now)?std::min(7.999999,double(now-last_)*8/interval_):0.);}
 double quarter_us()const{return interval_*24;}
};
}

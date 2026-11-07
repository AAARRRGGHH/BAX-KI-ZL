#include "world.hpp"
#include "bounce_storage.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace sm;using namespace sm::bounce;
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"Bounce clock/gate failed at %d: %s\n",__LINE__,#v);std::exit(1);}}while(0)
struct Trace:ripples::Observer{std::vector<ripples::MidiTrace> events;void midi(const ripples::MidiTrace& e)override{events.push_back(e);}};
struct Output:MidiTransport{int reject_off=0;bool send(MidiEvent e)override{if(e.kind==MidiKind::NoteOff && reject_off>0){--reject_off;return false;}return true;}};
struct Rig{
 Output output;Trace trace;World world{output,1,&trace};uint64_t elapsed=0,next_clock=0,quarter=500000;double accumulator=0;bool send_clock=true;
 Rig(){auto s=world.settings();s.gate=100;s.division=8;s.timing=ArpTiming::Dotted;world.settings(s);world.clock_sync(true);world.realtime(0xfa,1000000);world.realtime(0xf8,1000000);next_clock=quarter/24;}
 void advance(uint64_t duration){const auto end=elapsed+duration;while(elapsed<end){elapsed+=1000;if(send_clock)while(next_clock<=elapsed){world.realtime(0xf8,1000000+next_clock);next_clock+=quarter/24;}world.clock_tick(1000000+elapsed);accumulator+=.001;while(accumulator>=1./240){world.step(1./240);accumulator-=1./240;}}}
 int hit(){int id=-1;for(size_t i=0;i<max_balls;++i)if(world.balls[i].alive){id=world.grab(world.balls[i].p);break;}if(id<0)id=world.grab({0,-213});world.move(id,{0,-213});world.release(id,{0,-500});return id;}
 std::vector<ripples::MidiTrace> ons()const{std::vector<ripples::MidiTrace> result;for(auto e:trace.events)if(e.event.kind==MidiKind::NoteOn)result.push_back(e);return result;}
 bool clean()const{int notes[128]{};for(auto e:trace.events){if(e.event.kind==MidiKind::NoteOn)++notes[e.event.note];else if(e.event.kind==MidiKind::NoteOff)--notes[e.event.note];else std::fill(std::begin(notes),std::end(notes),0);}return std::all_of(std::begin(notes),std::end(notes),[](int n){return n==0;});}
};
int main(){
 // RECEIVE CLOCK is a preference, not a mute switch: an idle/disconnected
 // source falls back to saved BPM until a real tempo has been measured.
 {Output out;Trace trace;World w(out,1,&trace);auto s=w.settings();s.bpm=90;s.gate=50;s.division=8;s.timing=ArpTiming::Dotted;CHECK(w.settings(s));w.clock_sync(true);trace.events.clear();CHECK(w.period_us()==500000);
   w.clock_tick(1000000);int id=w.grab({0,-213});w.release(id,{0,-500});for(int i=0;i<180;++i){w.clock_tick(1000000+uint64_t(i)*4167);w.step(1./240);}CHECK(!trace.events.empty() && trace.events[0].event.kind==MidiKind::NoteOn && trace.events[0].time==500000);
   w.realtime(0xff,1800000);w.clear();CHECK(w.period_us()==500000);}
 // Stop must not cut a currently sounding note; its Gate still expires and
 // new unquantized hits are audible while the external transport is stopped.
 {Rig r;r.hit();r.advance(380000);CHECK(r.ons().size()==1);const auto events=r.trace.events.size();r.world.realtime(0xfc,1380000);CHECK(r.trace.events.size()==events);
   for(auto& b:r.world.balls)b.v={};r.send_clock=false;r.advance(400000);CHECK(r.clean());
   auto s=r.world.settings();s.quantized=false;CHECK(r.world.settings(s));const auto count=r.ons().size();r.hit();r.advance(10000);CHECK(r.ons().size()>count);r.world.clear();CHECK(r.clean());}
 {Output out;World w(out);auto s=w.settings();for(int gate=5;gate<=100;gate+=5){s.gate=gate;CHECK(w.settings(s));CHECK(w.gate_us()==250000ull*gate/100);}for(int gate:{0,4,6,101}){s.gate=gate;CHECK(!w.settings(s));}}
 {Rig r;r.hit();r.advance(350000);CHECK(r.ons().empty());r.advance(30000);CHECK(r.ons().size()==1 && std::abs(int64_t(r.ons()[0].time)-375000)<5000);
   r.hit();r.advance(370000);CHECK(r.ons().size()==2);const auto on=r.ons()[1];CHECK(std::abs(int64_t(on.time)-750000)<5000);
   bool off_before=false;for(auto e:r.trace.events){if(e.event.kind==MidiKind::NoteOff && e.time<=on.time)off_before=true;}CHECK(off_before);
   r.world.realtime(0xfc,1000000+r.elapsed);const auto count=r.ons().size();r.hit();r.advance(400000);CHECK(r.ons().size()>count);r.world.realtime(0xfb,1000000+r.elapsed);r.advance(25000);r.hit();r.advance(450000);CHECK(r.ons().size()>count);}
 {Rig r;r.quarter=1000000;r.next_clock=r.quarter/24;r.advance(100000);CHECK(std::abs(int64_t(r.world.period_us())-750000)<100);r.hit();r.advance(500000);CHECK(r.ons().empty());r.advance(160000);CHECK(r.ons().size()==1 && std::abs(int64_t(r.ons()[0].time)-750000)<5000);r.send_clock=false;r.advance(550000);CHECK(std::abs(int64_t(r.world.period_us())-750000)<100);const auto count=r.ons().size();r.hit();r.advance(800000);CHECK(r.ons().size()>count);r.world.clear();CHECK(r.clean());}
 {Rig r;r.hit();r.advance(380000);CHECK(r.ons().size()==1);r.output.reject_off=5;r.hit();r.advance(450000);CHECK(r.ons().size()==2);r.world.realtime(0xfc,1000000+r.elapsed);r.world.clear();CHECK(r.clean());}
 {Rig r;auto s=r.world.settings();s.scale={3,4};s.gate=25;r.world.settings(s);r.advance(100000);r.hit();r.world.clear();CHECK(r.world.clock_enabled() && r.world.settings()==s && r.clean());for(const auto& b:r.world.balls)CHECK(!b.alive);r.hit();r.advance(300000);CHECK(r.ons().size()==1);}

 for(auto timing:{ArpTiming::Straight,ArpTiming::Dotted,ArpTiming::Triplet}){Rig r;auto s=r.world.settings();s.division=32;s.timing=timing;s.gate=50;CHECK(r.world.settings(s));r.advance(25000);r.hit();r.advance(80000);const auto ons=r.ons();CHECK(!ons.empty());const int expected=timing==ArpTiming::Straight?62500:timing==ArpTiming::Dotted?93750:41667;CHECK(std::abs(int64_t(ons[0].time)-expected)<5000);}
 {Output out;Trace trace;World w(out,1,&trace);auto s=w.settings();s.gate=50;s.division=8;CHECK(w.settings(s));const int id=w.grab({0,-213});w.release(id,{0,-500});for(int i=0;i<140;++i)w.step(1./240);CHECK(trace.events.size()==2 && trace.events[0].event.kind==MidiKind::NoteOn && trace.events[1].event.kind==MidiKind::NoteOff);CHECK(trace.events[1].time-trace.events[0].time==125000);}
 {sm::bounce::Settings s;s.gate=55;auto bytes=pack(s);sm::bounce::Settings loaded;CHECK(unpack(bytes,loaded) && loaded==s && bytes[0]==7);for(int version:{1,2}){bytes=pack(s);bytes[0]=uint8_t(version);bytes[10]=version==1?15:0;const auto hash=checksum(bytes);for(int i=0;i<4;++i)bytes[12+i]=uint8_t(hash>>(i*8));CHECK(unpack(bytes,loaded) && loaded.gate==50 && loaded.scale==s.scale);}bytes=pack(s);bytes[10]=0;const auto hash=checksum(bytes);for(int i=0;i<4;++i)bytes[12+i]=uint8_t(hash>>(i*8));CHECK(!unpack(bytes,loaded));}
 {sm::bounce::Settings s;auto bytes=pack(s);sm::bounce::Settings loaded;CHECK(bytes[0]==7 && unpack(bytes,loaded) && loaded==s);bytes[0]=3;bytes[9]&=1;auto hash=checksum(bytes);for(int i=0;i<4;++i)bytes[12+i]=uint8_t(hash>>(i*8));CHECK(unpack(bytes,loaded) && loaded==s);bytes=pack(s);bytes[9]=4;hash=checksum(bytes);for(int i=0;i<4;++i)bytes[12+i]=uint8_t(hash>>(i*8));CHECK(!unpack(bytes,loaded));}
 // Old collision-enabled records load safely with the experimental flag retired.
 {sm::bounce::Settings s;auto bytes=pack(s);bytes[0]=6;bytes[9]|=2;auto hash=checksum(bytes);for(int i=0;i<4;++i)bytes[12+i]=uint8_t(hash>>(i*8));sm::bounce::Settings loaded;CHECK(unpack(bytes,loaded) && loaded==s && pack(loaded)[9]==1);}
 // Old experimental curve selections migrate without changing other fields;
 // all now use fixed curve 11 and newly written records contain no selector.
 {for(int curve=1;curve<=12;++curve){sm::bounce::Settings s;auto bytes=pack(s);bytes[0]=5;bytes[8]=uint8_t(int(s.timing)|((curve-1)<<2));const auto hash=checksum(bytes);for(int i=0;i<4;++i)bytes[12+i]=uint8_t(hash>>(i*8));sm::bounce::Settings loaded;CHECK(unpack(bytes,loaded) && loaded==s);CHECK(pack(loaded)[0]==7 && pack(loaded)[8]==uint8_t(s.timing));}}
 std::puts("Bounce MIDI Clock phase/tempo/transport/loss, full-slot retrigger, gate and migration PASS");
}

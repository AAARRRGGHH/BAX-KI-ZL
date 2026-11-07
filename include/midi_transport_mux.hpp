#pragma once
#include "midi_endpoint.hpp"
#include <atomic>
namespace sm {
// Only service_ui changes the link, after blocking new Ons and clearing the
// application's held sources. No route lock is held while calling the UI.
class MidiTransportMux:public MidiTransport {
 MidiEndpoint &ble_,&usb_;
 std::recursive_mutex gate_;
 std::atomic<MidiLink> active_{MidiLink::Ble};
 std::atomic<MidiRoute> route_{MidiRoute::Auto};
 std::atomic<bool> enabled_{true},changing_{false};
 std::atomic<uint32_t> clock_generation_{0};
 MidiEndpoint& endpoint(MidiLink link) { return link==MidiLink::Usb?usb_:ble_; }
public:
 MidiTransportMux(MidiEndpoint& ble,MidiEndpoint& usb):ble_(ble),usb_(usb) {}
 void set_route(MidiRoute route) override { route_=route; }
 void set_enabled(bool enabled) override {
   std::lock_guard<std::recursive_mutex> lock(gate_); enabled_=enabled;
   ble_.set_enabled(enabled && active_==MidiLink::Ble);
   usb_.set_enabled(enabled && active_==MidiLink::Usb);
 }
 bool send(MidiEvent event) override {
   std::lock_guard<std::recursive_mutex> lock(gate_);
   if(!enabled_ || (changing_ && event.kind==MidiKind::NoteOn)) return event.kind!=MidiKind::NoteOn;
   return endpoint(active_).send(event);
 }
 template<class Reset> void service_ui(Reset reset) {
   const auto desired=choose_midi_link(route_,usb_.connected());
   uint32_t bg=0,ug=0;
   const bool br=ble_.reset_requested(bg),ur=usb_.reset_requested(ug);
   const bool needs_reset=desired!=active_ || (active_==MidiLink::Ble?br:ur);
   if(needs_reset) { changing_=true; reset(); }
   {
     std::lock_guard<std::recursive_mutex> lock(gate_);
     if(desired!=active_) {
       endpoint(active_).set_enabled(false); active_=desired;
       endpoint(active_).set_enabled(enabled_);
     }
     if(needs_reset) ++clock_generation_;
     // set_enabled may have opened a new endpoint session: acknowledge its
     // current generation only after the application has released its sources.
     if(ble_.reset_requested(bg)) ble_.acknowledge_reset(bg);
     if(usb_.reset_requested(ug)) usb_.acknowledge_reset(ug);
     changing_=false;
   }
 }
 uint32_t clock_generation() const { return clock_generation_; }
 bool receive_realtime(MidiRealtime& event,uint32_t* epoch=nullptr) {
   std::lock_guard<std::recursive_mutex> lock(gate_);
   // Drain the inactive source too; its old clock must never be replayed later.
   MidiRealtime discard;
   auto link=active_.load();
   for(int i=0;i<128 && endpoint(link==MidiLink::Usb?MidiLink::Ble:MidiLink::Usb).receive_realtime(discard);++i) {}
   const bool have=endpoint(link).receive_realtime(event);
   if(have && epoch) *epoch=clock_generation_;
   return have;
 }
 MidiLink active() const { return active_; }
};
}

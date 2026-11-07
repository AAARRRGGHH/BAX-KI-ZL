#pragma once
#include "music.hpp"
#include "ble_midi_receive.hpp"
namespace sm {
struct MidiEndpoint:MidiTransport {
 virtual bool connected()=0;
 virtual bool reset_requested(uint32_t& generation)=0;
 virtual void acknowledge_reset(uint32_t generation)=0;
 virtual bool receive_realtime(MidiRealtime& event)=0;
};
enum class MidiLink:uint8_t { Ble,Usb };
inline MidiLink choose_midi_link(MidiRoute route,bool usb_mounted) {
 return route==MidiRoute::Usb || (route==MidiRoute::Auto && usb_mounted)?MidiLink::Usb:MidiLink::Ble;
}
}

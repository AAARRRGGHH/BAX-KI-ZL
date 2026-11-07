#pragma once
#include "music.hpp"
namespace sm {
using UsbMidiPacket=std::array<uint8_t,4>;
inline bool encode_usb_midi(MidiEvent event,UsbMidiPacket& packet) {
 if(event.channel<1 || event.channel>16 ||
    (event.kind!=MidiKind::AllNotesOff && (event.note>127 || event.velocity>127))) return false;
 uint8_t status=event.kind==MidiKind::NoteOn?0x90:event.kind==MidiKind::NoteOff?0x80:0xb0;
 if(event.kind!=MidiKind::NoteOn && event.kind!=MidiKind::NoteOff && event.kind!=MidiKind::AllNotesOff && event.kind!=MidiKind::ControlChange) return false;
 packet={uint8_t(status>>4),uint8_t(status|(event.channel-1)),
   event.kind==MidiKind::AllNotesOff?uint8_t(123):event.note,
   event.kind==MidiKind::AllNotesOff?uint8_t(0):event.velocity};
 return true;
}
inline uint8_t usb_midi_realtime(const UsbMidiPacket& packet) {
 // Cable zero, single-byte system real-time event; never parse data bytes or
 // timestamps as Clock. All other incoming MIDI is intentionally drained.
 if(packet[0]!=0x0f) return 0;
 const auto s=packet[1]; return s==0xf8 || s==0xfa || s==0xfb || s==0xfc || s==0xff?s:0;
}
}

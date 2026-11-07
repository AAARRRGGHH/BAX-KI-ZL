#include "usb_midi_protocol.hpp"
#include "midi_transport_mux.hpp"
#include "setup_storage.hpp"
#include "performance_storage.hpp"
#include "file_settings.hpp"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
using namespace sm;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"USB MIDI FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct Endpoint:MidiEndpoint {
 bool plugged=true,enabled=true,reset=false; uint32_t generation=0;
 std::vector<MidiEvent> sent; std::deque<MidiRealtime> input;
 bool connected() override { return plugged; }
 bool send(MidiEvent e) override { if(!enabled || !plugged) return e.kind!=MidiKind::NoteOn; sent.push_back(e); return true; }
 void set_enabled(bool v) override { if(v!=enabled) { enabled=v; reset=true; ++generation; } }
 bool reset_requested(uint32_t& g) override { g=generation; return reset; }
 void acknowledge_reset(uint32_t g) override { if(g==generation) reset=false; }
 bool receive_realtime(MidiRealtime& e) override { if(input.empty()) return false; e=input.front(); input.pop_front(); return true; }
};
int main() {
 UsbMidiPacket packet{};
 for(int ch=1;ch<=16;++ch) for(int note=0;note<128;++note) {
   CHECK(encode_usb_midi({MidiKind::NoteOn,uint8_t(ch),uint8_t(note),100},packet));
   CHECK(packet[0]==9 && packet[1]==(0x90|ch-1) && packet[2]==note && packet[3]==100);
   CHECK(encode_usb_midi({MidiKind::NoteOff,uint8_t(ch),uint8_t(note),0},packet) && packet[0]==8);
 }
 CHECK(!encode_usb_midi({MidiKind::NoteOn,0,60,100},packet));
 CHECK(!encode_usb_midi({MidiKind::NoteOn,17,60,100},packet));
 CHECK(!encode_usb_midi({MidiKind::NoteOn,1,128,100},packet));
 CHECK(encode_usb_midi({MidiKind::AllNotesOff,16,0,0},packet) && packet==UsbMidiPacket({11,0xbf,123,0}));
 for(int s=0;s<256;++s) {
   const bool realtime=s==0xf8 || s==0xfa || s==0xfb || s==0xfc || s==0xff;
   CHECK(bool(usb_midi_realtime({15,uint8_t(s),0,0}))==realtime);
   CHECK(!usb_midi_realtime({9,uint8_t(s),0,0}));
   CHECK(!usb_midi_realtime({31,uint8_t(s),0,0}));
 }
 for(int r=0;r<3;++r) for(bool global:{false,true}) {
   Setup s; s.midi_route=MidiRoute(r); s.global_scale=global; s.channel=16; s.receive_clock=true;
   CHECK(unpack_setup(pack_setup(s))==s);
   PerformanceSettings p; p.setup=s; auto bytes=pack_performance(p); PerformanceSettings loaded;
   CHECK(unpack_performance(bytes,loaded) && loaded.setup==s);
   const std::string path="usb-midi-test.cfg"; FileSettings file(path);
   CHECK(file.save_setup(s) && FileSettings(path).load_setup()==s); std::filesystem::remove(path);
 }
 Setup old; CHECK((pack_setup(old)>>56)==5); old.global_scale=false; CHECK((pack_setup(old)>>56)==6);
 CHECK(unpack_setup(pack_setup(old)).midi_route==MidiRoute::Auto);
 Endpoint ble,usb; usb.plugged=false; usb.enabled=false; MidiTransportMux mux(ble,usb);
 int resets=0; const auto reset=[&]() { ++resets; CHECK(!mux.send({MidiKind::NoteOn,1,60,100})); mux.send({MidiKind::NoteOff,1,60,0}); };
 mux.service_ui(reset); CHECK(resets==0 && mux.active()==MidiLink::Ble);
 CHECK(mux.send({MidiKind::NoteOn,1,60,100}) && ble.sent.size()==1);
 usb.reset=true; ++usb.generation; mux.service_ui(reset); CHECK(resets==0); // Inactive link must not interrupt BLE.
 usb.plugged=true; mux.service_ui(reset);
 CHECK(resets==1 && mux.active()==MidiLink::Usb && !ble.enabled && usb.enabled);
 CHECK(ble.sent.back().kind==MidiKind::NoteOff);
 CHECK(mux.send({MidiKind::NoteOn,1,62,100}) && usb.sent.back().note==62);
 ble.input.push_back({0xf8,10}); usb.input.push_back({0xfa,20}); MidiRealtime e;
 CHECK(mux.receive_realtime(e) && e.status==0xfa && ble.input.empty()); CHECK(!mux.receive_realtime(e));
 const auto epoch=mux.clock_generation(); mux.set_route(MidiRoute::Ble); mux.service_ui(reset);
 CHECK(mux.active()==MidiLink::Ble && mux.clock_generation()!=epoch);
 mux.set_route(MidiRoute::Usb); usb.plugged=false; mux.service_ui(reset);
 CHECK(mux.active()==MidiLink::Usb && !mux.send({MidiKind::NoteOn,1,60,100})); // Forced USB never falls back.
 mux.set_route(MidiRoute::Auto); mux.service_ui(reset); CHECK(mux.active()==MidiLink::Ble);
 mux.set_enabled(false); mux.service_ui([](){});
 CHECK(!mux.send({MidiKind::NoteOn,1,60,100}) && !ble.enabled && !usb.enabled);
 mux.set_enabled(true); mux.service_ui(reset); CHECK(mux.send({MidiKind::NoteOn,1,60,100}));
 std::puts("USB packets, clock filtering, setup migration, persistence and route handover passed");
}

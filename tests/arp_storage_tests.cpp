#include "arp_storage.hpp"
#include "arpeggiator.hpp"
#include "file_settings.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
using namespace sm;
#define CHECK(v) do { if(!(v)) { std::fprintf(stderr,"Arp storage FAIL %d: %s\n",__LINE__,#v); std::exit(1); } } while(0)
struct Output:MidiTransport {
 std::vector<MidiEvent> events;
 bool send(MidiEvent e)override { events.push_back(e); return true; }
};
int main() {
 for(int low=0;low<=8;++low) for(int high=low;high<=8;++high)
 for(int pattern=0;pattern<arp_pattern_count;++pattern) for(int timing=0;timing<3;++timing)
 for(int division:{1,2,4,8,16,32}) for(int page=0;page<10;++page) {
   ArpPreset s; s.settings.low=low; s.settings.high=high; s.settings.pattern=arp_patterns[pattern];
   s.settings.timing=ArpTiming(timing); s.settings.division=division;
   s.settings.probability=(low*17+high*11)%101; s.settings.swing=(low*7+high*3)%51;
   s.settings.block_size=(high+page)%8; if(s.settings.block_size==1) s.settings.block_size=0;
   s.settings.gate=(low*19+page*13)%101; s.settings.clock=ArpClock(page%2); s.page=page; s.selected=uint16_t((low*21+high*3+page)%128);
   s.bpm=40+(pattern*40+page)%261; CHECK(unpack_arpeggiator(pack_arpeggiator(s))==s);
 }
 // Old 64-bit presets keep all their choices and acquire the requested 100% gate.
 for(int page=0;page<8;++page) for(auto clock:{ArpClock::Tap,ArpClock::Midi}) {
   ArpPreset old; old.page=page; old.settings.clock=clock; old.bpm=137; old.selected=0x53;
   old.settings.pattern=ArpPattern::Anchor; old.settings.swing=37;
   const auto modern=pack_arpeggiator(old);
   const auto legacy=(modern&((uint64_t(1)<<51)-1))|(uint64_t(clock)<<51)|(uint64_t(1)<<56);
   CHECK(unpack_arpeggiator(legacy)==old);
 }
 CHECK(unpack_arpeggiator(0)==ArpPreset{});
 CHECK(unpack_arpeggiator((pack_arpeggiator({})&~(uint64_t(127)<<53))|(uint64_t(127)<<53))==ArpPreset{});
 ArpPreset saved; saved.settings.low=1; saved.settings.high=7; saved.settings.division=32;
 saved.settings.probability=23; saved.settings.swing=45; saved.settings.block_size=4; saved.settings.gate=37;
 saved.settings.pattern=ArpPattern::Anchor; saved.settings.timing=ArpTiming::Triplet; saved.settings.clock=ArpClock::Midi;
 saved.bpm=173; saved.selected=0x55; saved.page=7;
 { // Restoration never replays an old Note On or remote transport state.
   Output out; MidiRouter router(out); Arpeggiator arp(router);
   CHECK(arp.restore(saved)); auto expected=saved; expected.settings.probability=25; expected.settings.gate=35; CHECK(arp.preset(saved.page)==expected);
   arp.tick(1000000); CHECK(out.events.empty() && !arp.state().enabled && !arp.state().running);
   arp.enable(true); arp.tick(1000001); CHECK(out.events.empty());
   arp.realtime(0xfa,2000000); arp.realtime(0xf8,2000000); arp.tick(2000000);
   CHECK(arp.state().running); arp.enable(false);
   auto bad=saved; bad.bpm=301; CHECK(!arp.restore(bad)); CHECK(arp.preset(saved.page)==expected);
 }
 { // Internal tempo playback starts normally after entering the mode.
   Output out; MidiRouter router(out); Arpeggiator arp(router); auto internal=saved;
   internal.settings.clock=ArpClock::Tap; internal.settings.probability=100;
   CHECK(arp.restore(internal)); arp.enable(true); arp.tick(1000000); CHECK(!out.events.empty());
   arp.reset_defaults(); CHECK(arp.preset()==ArpPreset{});
 }
 // All old percentage values remain decodable and restore to usable 5% steps.
 for(int percent=0;percent<=100;++percent) {
   Output out; MidiRouter router(out); Arpeggiator arp(router); auto old=saved;
   old.settings.probability=percent; old.settings.gate=100-percent;
   CHECK(unpack_arpeggiator(pack_arpeggiator(old))==old);
   CHECK(arp.restore(old)); const auto current=arp.state().settings;
   CHECK(current.probability==std::max(5,((percent+2)/5)*5));
   CHECK(current.gate==std::max(5,((102-percent)/5)*5));
   CHECK(current.pattern==old.settings.pattern && arp.state().bpm==old.bpm && arp.state().selected==old.selected);
 }
 const char* path="test-arp-persistence.cfg"; std::filesystem::remove(path);
 FileSettings file(path);
 CHECK(file.save_all({2,1},{5,4},{9,3},{}));
 CHECK(file.save_arpeggiator(saved)); CHECK(file.load_arpeggiator()==saved);
 auto setup=file.load_setup(); setup.global_scale=false;
 CHECK(file.save_all({6,7},{8,2},{10,5},setup)); CHECK(file.load_arpeggiator()==saved);
 CHECK(file.save_setup(setup) && file.save({1,3}) && file.save_chords({2,4}));
 CHECK(file.load_arpeggiator()==saved);
 { FileSettings reopened(path); CHECK(reopened.load_arpeggiator()==saved);
   CHECK(reopened.load()==Selection({1,3}) && reopened.load_arp()==Selection({8,2})); }
 auto bad=saved; bad.selected=128; CHECK(!file.save_arpeggiator(bad)); CHECK(file.load_arpeggiator()==saved);
 { std::ofstream legacy(path); legacy<<"1 2 1\n"; }
 CHECK(file.load_arpeggiator()==ArpPreset{}); CHECK(file.save_setup({})); CHECK(file.load_arpeggiator()==ArpPreset{});
 { std::ofstream legacy(path); legacy<<"16 2 1 1 3074023 0 2 10 400 0 0 0";
   for(int i=0;i<14;++i) legacy<<" 0";
   legacy<<" 0 0 0 0 6 7\n"; }
 CHECK(file.load()==Selection({2,1}) && file.load_arp()==Selection({6,7}));
 CHECK(file.load_arpeggiator()==ArpPreset{});
 CHECK(file.save_arpeggiator(saved)); CHECK(file.load_arp()==Selection({6,7}));
 CHECK(file.save_arpeggiator({})); CHECK(file.load_arpeggiator()==ArpPreset{});
 std::filesystem::remove(path);
 struct Failing:Settings {
   Selection load()override { return {}; } bool save(Selection)override { return false; }
   bool save_arpeggiator(ArpPreset)override { return false; }
 } fail;
 Application app(fail); CHECK(!app.save_arpeggiator(saved)); CHECK(app.load_arpeggiator()==ArpPreset{});
 std::puts("Arpeggiator preset persistence, migration and safe restore tests passed");
}

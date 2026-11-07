#include "music.hpp"
#include "chords.hpp"
#include "file_settings.hpp"
#include "performance_storage.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
using namespace sm;
#define CHECK(v) do { if(!(v)) { std::fprintf(stderr,"Global scale FAIL %d: %s\n",__LINE__,#v); std::exit(1); } } while(0)
struct Store:Settings {
 PerformanceSettings state{}; bool fail=false; int writes=0;
 Selection load()override { return state.keyboard; }
 Selection load_arp()override { return state.arp; }
 bool save(Selection)override { return false; }
 Setup load_setup()override { return state.setup; }
 ChordSelection load_chords()override { return state.chords; }
 bool save_all(Selection k,Selection a,ChordSelection c,Setup s)override {
   ++writes; if(fail)return false; state={k,a,c,s}; return true;
 }
};
void choose(Application& app,Selection s) { app.begin(); app.draft=s; CHECK(app.select()); }
int main() {
 Store store; Application app(store); CHECK(app.setup.global_scale && store.writes==0);
 for(int mode=0;mode<12;++mode) for(int root=0;root<12;++root) {
   app.use_arp_scale(mode%2); choose(app,{root,mode});
   CHECK(app.keyboard_scale()==app.arp_scale());
   CHECK(app.chords.root==root && app.chords.mode==mode);
   const auto palette=chord_palette(app.chords); CHECK(palette.size()==7);
   for(const auto& chord:palette) for(int i=0;i<chord.voices;++i) CHECK(chord.pitches[i]>=0 && chord.pitches[i]<=127);
 }
 auto custom=app.chords; custom.variants[2]=ChordVariant::Ninth; custom.octave_shifts[5]=-1; custom.strum_ms=70;
 custom.root=2; custom.mode=1; CHECK(app.save_chords(custom));
 CHECK(app.current==Selection({2,1})); choose(app,{4,0});
 CHECK(app.chords.variants[2]==ChordVariant::Ninth && app.chords.octave_shifts[5]==-1 && app.chords.strum_ms==70);
 app.begin(); app.draft={9,3}; app.back(); CHECK(app.current==Selection({4,0}));
 const auto unchanged=store.writes; app.begin(); CHECK(app.select()); CHECK(store.writes==unchanged);
 auto setup=app.setup; setup.global_scale=false; CHECK(app.save_setup(setup));
 app.use_arp_scale(false); choose(app,{2,1});
 app.use_arp_scale(true); choose(app,{5,4});
 custom=app.chords; custom.root=9; custom.mode=3; CHECK(app.save_chords(custom));
 CHECK(app.keyboard_scale()==Selection({2,1}) && app.arp_scale()==Selection({5,4}));
 { Application restarted(store); CHECK(!restarted.setup.global_scale);
   CHECK(restarted.current==Selection({2,1})); restarted.use_arp_scale(true); CHECK(restarted.current==Selection({5,4}));
   CHECK(restarted.chords.root==9 && restarted.chords.mode==3); }
 // Enabling from Chords uses that scale; enabling from Arp uses its own scale.
 setup.global_scale=true; CHECK(app.save_setup(setup,true)); CHECK(app.current==Selection({9,3}));
 setup.global_scale=false; CHECK(app.save_setup(setup)); app.use_arp_scale(true); choose(app,{7,6});
 setup.global_scale=true; CHECK(app.save_setup(setup)); CHECK(app.chords.root==7 && app.chords.mode==6);
 auto saved=store.state; store.fail=true; app.begin(); app.draft={1,2}; CHECK(!app.select());
 CHECK(app.current==saved.arp && app.chords==saved.chords); app.back();
 custom=app.chords; custom.root=8; CHECK(!app.save_chords(custom)); CHECK(app.chords==saved.chords);
 setup.global_scale=false; CHECK(!app.save_setup(setup)); CHECK(app.setup.global_scale); store.fail=false;
 for(bool global:{false,true}) {
   auto config=store.state; config.setup.global_scale=global;
   const auto bytes=pack_performance(config); PerformanceSettings loaded;
   CHECK(unpack_performance(bytes,loaded)); CHECK(loaded.setup==config.setup && loaded.chords==config.chords);
   CHECK(loaded.keyboard==config.keyboard && loaded.arp==config.arp);
   auto bad=bytes; bad[0]=0; CHECK(!unpack_performance(bad,loaded));
   bad=bytes; bad[6]=12; CHECK(!unpack_performance(bad,loaded));
   CHECK(unpack_setup(pack_setup(config.setup))==config.setup);
 }
 // Both pentatonics keep their displayed mode but use the appropriate 7-degree basis.
 for(int root=0;root<12;++root) for(int pad=0;pad<7;++pad) {
   CHECK(chord_palette({root,9})[pad].pitches==chord_palette({root,0})[pad].pitches);
   CHECK(chord_palette({root,10})[pad].pitches==chord_palette({root,1})[pad].pitches);
 }
 const char* path="test-global-settings.cfg";
 { std::ofstream f(path); f<<"1 2 1\n"; }
 FileSettings file(path); Application migrated(file); CHECK(migrated.setup.global_scale);
 CHECK(migrated.current==Selection({2,1}) && migrated.chords.root==2 && migrated.chords.mode==1);
 setup=migrated.setup; setup.global_scale=false; CHECK(migrated.save_setup(setup));
 migrated.use_arp_scale(true); choose(migrated,{6,7});
 { FileSettings reopened(path); Application loaded(reopened); CHECK(!loaded.setup.global_scale);
   CHECK(loaded.current==Selection({2,1})); loaded.use_arp_scale(true); CHECK(loaded.current==Selection({6,7})); }
 std::filesystem::remove(path);
 std::puts("Global/independent scale, persistence, migration and failure tests passed");
}

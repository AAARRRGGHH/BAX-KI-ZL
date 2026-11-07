#include "music_slot_storage.hpp"
#include "file_settings.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
using namespace sm;
#define CHECK(v) do { if(!(v)) { std::fprintf(stderr,"Slot FAIL %d: %s\n",__LINE__,#v); std::exit(1); } } while(0)
int main() {
 const std::string path="test-music-slots.cfg";
 std::filesystem::remove(path); std::filesystem::remove(path+".slots");
 for(int i=1;i<=8;++i) std::filesystem::remove(path+".slot"+std::to_string(i));
 FileSettings settings(path); Application app(settings);
 MusicPreset p; p.global_scale=false; p.keyboard={2,1}; p.arp={5,4}; p.chords={9,2};
 p.chords.variants[2]=ChordVariant::Ninth; p.chords.octave_shifts[4]=-1;
 p.chords.strum_ms=170; p.chords.strum_division=16; p.chords.strum_timing=2;
 p.arpeggiator.settings.pattern=ArpPattern::Anchor; p.arpeggiator.settings.gate=37;
 p.arpeggiator.selected=0x55; p.arpeggiator.page=8; p.arpeggiator.bpm=173; p.octave=7;
 p.kinetic.scale={8,6};p.kinetic.low=1;p.kinetic.high=7;p.kinetic.bpm=167;p.kinetic.division=8;p.kinetic.timing=ArpTiming::Triplet;p.kinetic.quantized=false;p.kinetic.energy_seconds=75;p.kinetic.gate=35;
 MusicPreset loaded;
 p.name={{'A','N','C','H','O','R','0','1'}};
 const auto bytes=pack_music_slot(p); CHECK(unpack_music_slot(bytes,loaded) && loaded==p);
 for(int byte=0;byte<int(bytes.size());++byte) for(int bit=0;bit<8;++bit) {
   auto corrupt=bytes; corrupt[byte]^=1<<bit; CHECK(!unpack_music_slot(corrupt,loaded));
 }
 // Old unnamed 32-byte slots migrate without losing musical content.
 LegacyMusicSlotBytes old{}; std::copy_n(bytes.begin(),26,old.begin()); old[3]=1;
 const auto hash=slot_checksum(old); for(int i=0;i<4;++i) old[28+i]=uint8_t(hash>>(8*i));
 MusicPreset legacy; CHECK(unpack_music_slot(old,legacy));
 auto unnamed=p; unnamed.name.fill(' ');unnamed.kinetic={}; CHECK(legacy==unnamed && music_slot_name(legacy)=="---");
 { std::ofstream f(path+".slot1",std::ios::binary); f.write(reinterpret_cast<const char*>(old.data()),old.size()); }
 CHECK(settings.load_slot(0,legacy) && legacy==unnamed); std::filesystem::remove(path+".slot1");
 auto invalid_name=p; invalid_name.name[0]='a'; CHECK(!valid_music_preset(invalid_name));
 CHECK(music_slot_name(p)=="ANCHOR01");
 for(int i=0;i<8;++i) {
   CHECK(!settings.load_slot(i,loaded)); p.octave=i; CHECK(app.save_slot(i,p));
   FileSettings reopened(path); CHECK(reopened.load_slot(i,loaded) && loaded==p);
 }
 // The old named slot and bank formats remain readable, including slot order.
 NamedLegacyMusicSlotBytes named{};std::copy_n(bytes.begin(),34,named.begin());named[3]=2;
 auto old_hash=slot_checksum(named);for(int i=0;i<4;++i)named[36+i]=uint8_t(old_hash>>(8*i));
 auto named_expected=p;named_expected.kinetic={};CHECK(unpack_music_slot(named,legacy) && legacy==named_expected);
 LegacyMusicBankBytes old_bank{{'P','B','B',1,1}};std::copy(named.begin(),named.end(),old_bank.begin()+8);
 old_hash=slot_checksum(old_bank);for(int i=0;i<4;++i)old_bank[328+i]=uint8_t(old_hash>>(8*i));
 MusicSlotBank old_decoded;CHECK(unpack_music_bank(old_bank,old_decoded) && old_decoded.count==1 && old_decoded.slots[0]==named_expected);
 // Check the actual file migration, then restore the current eight-slot bank.
 const auto current_bank_path=path+".slots";std::ifstream current_file(current_bank_path,std::ios::binary);std::vector<char> current_bytes((std::istreambuf_iterator<char>(current_file)),{});current_file.close();
 {std::ofstream f(current_bank_path,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(old_bank.data()),old_bank.size());}
 CHECK(settings.load_slot(0,legacy) && legacy==named_expected && !settings.load_slot(1,legacy));
 CHECK(settings.save_slot(1,p));CHECK(settings.load_slot(0,legacy) && legacy==named_expected);CHECK(settings.load_slot(1,legacy) && legacy==p);
 {std::ofstream f(current_bank_path,std::ios::binary|std::ios::trunc);f.write(current_bytes.data(),current_bytes.size());}
 // SAVE removes only leading blanks, preserving internal spacing and all music.
 MusicPreset original; CHECK(settings.load_slot(0,original));
 for(int leading=0;leading<=8;++leading) {
   auto padded=p; padded.name.fill(' ');
   if(leading<8) padded.name[leading]='A';
   if(leading<5) padded.name[leading+2]='B';
   auto expected=p; expected.name.fill(' ');
   if(leading<8) expected.name[0]='A';
   if(leading<5) expected.name[2]='B';
   CHECK(app.save_slot(0,padded)); FileSettings fresh(path);
   CHECK(fresh.load_slot(0,loaded) && loaded==expected);
 }
 CHECK(app.save_slot(0,original));
 CHECK(!app.save_slot(8,p) && !app.load_slot(-1,loaded));
 auto bad=p; bad.arpeggiator.settings.gate=101; CHECK(!app.save_slot(0,bad));
 CHECK(settings.load_slot(7,loaded) && loaded==p);
 auto setup=app.setup; setup.channel=12; setup.accent=0xabcdef; setup.travel=true; setup.receive_clock=true;
 CHECK(app.save_setup(setup)); CHECK(app.apply_music(p));
 CHECK(app.keyboard_scale()==p.keyboard && app.arp_scale()==p.arp && app.chords==p.chords);
 CHECK(!app.setup.global_scale && app.setup.channel==12 && app.setup.accent==0xabcdef && app.setup.travel && app.setup.receive_clock);
 CHECK(settings.load_arpeggiator()==p.arpeggiator && settings.load_bounce()==p.kinetic);
 // A failed atomic runtime write must leave all four modes unchanged.
 std::filesystem::create_directory(path+".tmp");auto changed=p;changed.kinetic.bpm=200;changed.keyboard={3,2};
 CHECK(!app.apply_music(changed));CHECK(settings.load()==p.keyboard && settings.load_bounce()==p.kinetic);
 std::filesystem::remove(path+".tmp");
 Application reboot(settings); CHECK(reboot.keyboard_scale()==p.keyboard && reboot.arp_scale()==p.arp && reboot.chords==p.chords);
 CHECK(settings.load_slot(7,loaded) && loaded==p);
 // Normal edits never overwrite stored ideas; only SAVE does.
 CHECK(reboot.save_chords({0,0})); CHECK(settings.load_slot(7,loaded) && loaded==p);
 p.global_scale=true; p.keyboard=p.arp={7,0}; p.chords.root=7; p.chords.mode=0;
 CHECK(app.save_slot(0,p) && app.load_slot(0,loaded) && loaded==p);
 CHECK(app.apply_music(p) && app.setup.global_scale && app.current==p.keyboard);
 // Deletion closes a middle gap and survives reopening, preserving order and data.
 std::array<MusicPreset,8> before_delete{};
 for(int i=0;i<8;++i) CHECK(settings.load_slot(i,before_delete[i]));
 CHECK(app.delete_slot(3)); FileSettings reopened(path);
 for(int i=0;i<7;++i) { CHECK(reopened.load_slot(i,loaded)); CHECK(loaded==before_delete[i<3?i:i+1]); }
 CHECK(!reopened.load_slot(7,loaded) && !reopened.delete_slot(7));
 CHECK(reopened.save_slot(7,p) && reopened.load_slot(7,loaded) && loaded==p);
 CHECK(reopened.delete_slot(0)); CHECK(reopened.load_slot(0,loaded) && loaded==before_delete[1]);
 // Bank writes are atomic; a corrupt bank never falls back to stale legacy slots.
 { std::ofstream f(path+".slots",std::ios::binary|std::ios::trunc); f<<"bad"; }
 CHECK(!app.load_slot(0,loaded) && !app.save_slot(0,p) && !app.delete_slot(0));
 std::filesystem::remove(path+".slots");
 // Sparse old slots migrate in order; deleting all must not resurrect them.
 for(int i:{2,6}) { std::ofstream f(path+".slot"+std::to_string(i),std::ios::binary); f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); }
 CHECK(settings.load_slot(0,loaded) && loaded.name==p.name);
 CHECK(settings.load_slot(1,loaded) && !settings.load_slot(2,loaded));
 CHECK(settings.delete_slot(0) && settings.delete_slot(0));
 CHECK(!settings.load_slot(0,loaded));
 CHECK(settings.save_slot(7,p) && settings.load_slot(0,loaded) && loaded==p);
 CHECK(!settings.load_slot(1,loaded));
 // Check the shared bank codec, including integrity of every byte.
 MusicSlotBank bank; CHECK(bank.put(7,p) && bank.count==1); const auto bank_bytes=pack_music_bank(bank);
 MusicSlotBank decoded; CHECK(unpack_music_bank(bank_bytes,decoded) && decoded.count==1 && decoded.slots[0]==p);
 for(size_t i=0;i<bank_bytes.size();++i) { auto corrupt=bank_bytes; corrupt[i]^=1; CHECK(!unpack_music_bank(corrupt,decoded)); }
 struct Failing:Settings { Selection load() override {return {};} bool save(Selection)override{return false;} } fail;
 Application rejected(fail); const auto before=rejected.music_preset({},4);
 CHECK(!rejected.apply_music(p)); CHECK(rejected.music_preset({},4)==before);
 std::filesystem::remove(path); std::filesystem::remove(path+".slots"); for(int i=1;i<=8;++i) std::filesystem::remove(path+".slot"+std::to_string(i));
 std::puts("Eight music slots, corruption detection, complete restore and preserved setup passed");
}

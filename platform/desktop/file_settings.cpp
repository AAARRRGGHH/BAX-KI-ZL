#include "file_settings.hpp"
#include "bounce_storage.hpp"
#include "chord_storage.hpp"
#include "arp_storage.hpp"
#include "music_slot_storage.hpp"
#include <fstream>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif
namespace sm {
namespace {
struct Configuration { Selection selection{}; Setup setup{}; ChordSelection chords{}; Selection arp{}; ArpPreset preset{}; bounce::Settings kinetic{}; };
Configuration read_configuration(const std::string& path) {
 std::ifstream f(path); int version=0; Configuration config;
 {bounce::Bytes bytes{};std::ifstream old(path+".bounce",std::ios::binary);if(old.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))bounce::unpack(bytes,config.kinetic);}
 if(!(f>>version>>config.selection.root>>config.selection.mode)) return config;
 if(version>=2 && version<=19) {
   uint64_t accent=0;
   if(!(f>>config.setup.channel>>accent)) return {};
   if(accent>0xffffff) config.setup={}; else config.setup.accent=uint32_t(accent);
   if(version>=3) {
     int enabled=0,wave=0,attack=0,decay=0;
     if(!(f>>enabled>>wave>>attack>>decay) || enabled<0 || enabled>1 || wave<0 || wave>=(version==3?4:waveform_count) || attack<0 || attack>2000 || decay<10 || decay>4000) return {};
     config.setup.travel=enabled!=0; config.setup.synth={Waveform(wave),uint16_t(attack),uint16_t(decay)};
   }
   if(version>=5) { int receive=0; if(!(f>>receive) || (receive!=0 && receive!=1)) return {}; config.setup.receive_clock=receive!=0; }
   if(version>=6) {
     int root=0,feel=0; if(!(f>>root>>feel)) return {};
     config.chords=version==6?legacy_chords(root,feel):version==7?legacy_ten_chords(root,feel):version==8?legacy_four_chords(root,feel):ChordSelection{root,feel};
     if(version>=10) for(auto& variant:config.chords.variants) {
       int option=0; if(!(f>>option) || option<0 || option>=chord_variant_count) return {};
       variant=ChordVariant(option);
     }
     if(version>=11) for(auto& shift:config.chords.octave_shifts) {
       int option=0; if(!(f>>option) || option<-1 || option>1) return {};
       shift=int8_t(option);
     }
     if(version>=12 && !(f>>config.chords.strum_ms)) return {};
     if(version>=14 && !(f>>config.chords.strum_division)) return {};
     if(version>=15 && !(f>>config.chords.strum_timing)) return {};
     if(version==12) {
       const int old=config.chords.strum_ms;
       if(old<0 || old>100 || old%5) return {};
       config.chords.strum_ms=((old+5)/10)*10;
     }
     if(!valid(config.chords)) config.chords={};
   }
 } else if(version!=1) return {};
 config.arp=valid(config.selection)?config.selection:Selection{};
 if(version>=16) {
   int global=0;
   if(!(f>>global>>config.arp.root>>config.arp.mode) || (global!=0 && global!=1) || !valid(config.arp)) return {};
   config.setup.global_scale=global!=0;
 }
 if(version>=17) { uint64_t packed=0; if(!(f>>packed)) return {}; config.preset=unpack_arpeggiator(packed); }
 if(version>=18) { int route=0; if(!(f>>route) || route<0 || route>2) return {}; config.setup.midi_route=MidiRoute(route); }
 if(version>=19){bounce::Bytes bytes{};for(auto& byte:bytes){int value;if(!(f>>value) || value<0 || value>255)return {};byte=uint8_t(value);}if(!bounce::unpack(bytes,config.kinetic))return {};}
 std::string extra; if(f>>extra) return {};
 if(!valid(config.selection)) config.selection={};
 if(!valid_setup(config.setup)) config.setup={};
 return config;
}
bool write_configuration(const std::string& path,const Configuration& config) {
 const auto temp=path+".tmp";
 { std::ofstream f(temp,std::ios::trunc); f<<19<<' '<<config.selection.root<<' '<<config.selection.mode<<' '<<config.setup.channel<<' '<<config.setup.accent<<' '<<int(config.setup.travel)<<' '<<int(config.setup.synth.waveform)<<' '<<config.setup.synth.attack_ms<<' '<<config.setup.synth.decay_ms<<' '<<int(config.setup.receive_clock)<<' '<<config.chords.root<<' '<<config.chords.mode;
   for(auto variant:config.chords.variants) f<<' '<<int(variant);
   for(auto shift:config.chords.octave_shifts) f<<' '<<int(shift);
   f<<' '<<config.chords.strum_ms<<' '<<config.chords.strum_division<<' '<<config.chords.strum_timing;
   f<<' '<<int(config.setup.global_scale)<<' '<<config.arp.root<<' '<<config.arp.mode;
   f<<' '<<pack_arpeggiator(config.preset)<<' '<<int(config.setup.midi_route);
   for(auto byte:bounce::pack(config.kinetic))f<<' '<<int(byte);
   f<<'\n'; f.flush(); if(!f) return false; }
#ifdef _WIN32
 return MoveFileExA(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
 std::error_code ec; std::filesystem::rename(temp,path,ec); return !ec;
#endif
}
}
MotionSettings FileSettings::load_motion(){MotionBytes b{};MotionSettings s;std::ifstream f(path_+".motion",std::ios::binary);if(f.read(reinterpret_cast<char*>(b.data()),b.size()))unpack_motion(b,s);return s;}
bool FileSettings::save_motion(MotionSettings s){
 if(!valid_motion(s))return false;const auto path=path_+".motion",tmp=path+".tmp";const auto b=pack_motion(s);
 {std::ofstream f(tmp,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(b.data()),b.size());f.flush();if(!f)return false;}
#ifdef _WIN32
 return MoveFileExA(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
 std::error_code ec;std::filesystem::rename(tmp,path,ec);return !ec;
#endif
}
bounce::Settings FileSettings::load_bounce(){return read_configuration(path_).kinetic;}
bool FileSettings::save_bounce(bounce::Settings value){if(!bounce::valid_settings(value))return false;auto config=read_configuration(path_);config.kinetic=value;return write_configuration(path_,config);}
namespace {
bool read_slot_bank(const std::string& path,MusicSlotBank& bank) {
 const auto bank_path=path+".slots";
 if(std::filesystem::exists(bank_path)) {
   std::ifstream f(bank_path,std::ios::binary|std::ios::ate); MusicBankBytes bytes{};
   const auto size=f.tellg();if(size!=332 && size!=460)return false;f.seekg(0);
   f.read(reinterpret_cast<char*>(bytes.data()),size);
   return bool(f) && unpack_music_bank_data(bytes,size_t(size),bank);
 }
 // Migrate old individual slots in their original order, closing all gaps.
 bank={};
 for(int i=0;i<music_slot_count;++i) {
   std::ifstream f(path+".slot"+std::to_string(i+1),std::ios::binary|std::ios::ate);
   if(!f) continue;
   const auto size=f.tellg(); if(size!=32 && size!=40 && size!=56) continue;
   f.seekg(0); MusicSlotBytes bytes{}; f.read(reinterpret_cast<char*>(bytes.data()),size); MusicPreset p;
   if(f && unpack_music_slot_data(bytes,size_t(size),p)) bank.slots[bank.count++]=p;
 }
 return true;
}
bool write_slot_bank(const std::string& config_path,const MusicSlotBank& bank) {
 const auto bytes=pack_music_bank(bank); const auto path=config_path+".slots",temp=path+".tmp";
 { std::ofstream f(temp,std::ios::binary|std::ios::trunc); f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); f.flush(); if(!f) return false; }
#ifdef _WIN32
 return MoveFileExA(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
 std::error_code ec; std::filesystem::rename(temp,path,ec); return !ec;
#endif
}
}
bool FileSettings::load_slot(int slot,MusicPreset& value) {
 MusicSlotBank bank; if(slot<0 || slot>=music_slot_count || !read_slot_bank(path_,bank) || slot>=bank.count) return false;
 value=bank.slots[slot]; return true;
}
bool FileSettings::save_slot(int slot,const MusicPreset& value) {
 MusicSlotBank bank; return read_slot_bank(path_,bank) && bank.put(slot,value) && write_slot_bank(path_,bank);
}
bool FileSettings::delete_slot(int slot) {
 MusicSlotBank bank; return read_slot_bank(path_,bank) && bank.erase(slot) && write_slot_bank(path_,bank);
}
bool FileSettings::save_music(const MusicPreset& value,Setup setup) {
 if(!valid_music_preset(value) || !valid_setup(setup)) return false;
 setup.global_scale=value.global_scale;
 return write_configuration(path_,{value.keyboard,setup,value.chords,value.arp,value.arpeggiator,value.kinetic});
}
Selection FileSettings::load() { return read_configuration(path_).selection; }
Selection FileSettings::load_arp() { return read_configuration(path_).arp; }
bool FileSettings::save_all(Selection keyboard,Selection arp,ChordSelection chords,Setup setup) {
 if(!valid(keyboard) || !valid(arp) || !valid(chords) || !valid_setup(setup)) return false;
 auto config=read_configuration(path_); config.selection=keyboard; config.setup=setup; config.chords=chords; config.arp=arp;
 return write_configuration(path_,config);
}
ArpPreset FileSettings::load_arpeggiator() { return read_configuration(path_).preset; }
bool FileSettings::save_arpeggiator(ArpPreset value) {
 if(!valid_arp_preset(value)) return false;
 auto config=read_configuration(path_); config.preset=value; return write_configuration(path_,config);
}
Setup FileSettings::load_setup() { return read_configuration(path_).setup; }
ChordSelection FileSettings::load_chords() { return read_configuration(path_).chords; }
bool FileSettings::save_chords(ChordSelection s) {
 if(!valid(s)) return false;
 auto config=read_configuration(path_); config.chords=s; return write_configuration(path_,config);
}
bool FileSettings::save(Selection s) {
 if(!valid(s)) return false;
 auto config=read_configuration(path_); config.selection=s; return write_configuration(path_,config);
}
bool FileSettings::save_setup(Setup s) {
 if(!valid_setup(s)) return false;
 auto config=read_configuration(path_); config.setup=s; return write_configuration(path_,config);
}
}

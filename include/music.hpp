#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include "arp_settings.hpp"
namespace sm {
enum class ModeType { Scale, Chord };
struct Mode { const char* name; ModeType type; std::vector<int> intervals; std::vector<int> degrees; };
const std::vector<Mode>& modes();
const std::array<const char*,12>& roots();
struct Selection { int root=0; int mode=0; bool operator==(const Selection& b) const { return root==b.root && mode==b.mode; } bool operator!=(const Selection& b) const { return !(*this==b); } };
bool valid(Selection s);
namespace bounce {
struct Settings {
 Selection scale{11,1};int low=3,high=5,bpm=120,division=8;
 ArpTiming timing=ArpTiming::Straight;bool quantized=true;int energy_seconds=15,gate=50;
 bool operator==(const Settings& b)const{return scale==b.scale && low==b.low && high==b.high && bpm==b.bpm && division==b.division && timing==b.timing && quantized==b.quantized && energy_seconds==b.energy_seconds && gate==b.gate;}
 bool operator!=(const Settings& b)const{return !(*this==b);}
};
inline bool valid_settings(Settings s){return valid(s.scale) && s.low>=0 && s.high<=8 && s.low<=s.high && s.bpm>=40 && s.bpm<=300 && (s.division==1 || s.division==2 || s.division==4 || s.division==8 || s.division==16 || s.division==32) && int(s.timing)>=0 && int(s.timing)<=2 && s.energy_seconds>=10 && s.energy_seconds<=90 && s.gate>=5 && s.gate<=100 && s.gate%5==0;}
}
// Preserve existing scale IDs. Blues is appended after the two pentatonics.
inline constexpr int blues_mode=11;
inline constexpr std::array<int,12> chord_modes{{0,1,2,3,4,5,6,7,8,9,10,blues_mode}};
inline constexpr int chord_scale_count=int(chord_modes.size());
inline int chord_mode_option(int mode) {
 for(int i=0;i<chord_scale_count;++i) if(chord_modes[i]==mode) return i;
 return 0;
}
enum class ChordVariant:uint8_t { Triad, Parallel, Dominant7, Seventh, Ninth, Sus4, Sus2, Diminished, Augmented };
inline constexpr int chord_variant_count=9;
inline constexpr std::array<int,9> chord_strum_divisions{{0,512,256,128,64,32,16,8,4}};
struct ChordStrumChoice { int division,timing; }; // 0=straight, 1=dotted, 2=triplet.
inline constexpr std::array<ChordStrumChoice,17> chord_strum_choices{{
 {0,0},{512,0},{256,0},{128,0},{64,0},
 {32,2},{32,0},{32,1},{16,2},{16,0},{16,1},
 {8,2},{8,0},{8,1},{4,2},{4,0},{4,1}}};
inline int chord_strum_option(int division,int timing=0) {
 for(int i=0;i<int(chord_strum_choices.size());++i) if(chord_strum_choices[i].division==division && chord_strum_choices[i].timing==timing) return i;
 return 0;
}
struct ChordSelection {
 int root=0; int mode=0;
 std::array<ChordVariant,7> variants{};
 std::array<int8_t,7> octave_shifts{};
 int strum_ms=0;
 int strum_division=0;
 int strum_timing=0;
 bool operator==(const ChordSelection& b) const { return root==b.root && mode==b.mode && variants==b.variants && octave_shifts==b.octave_shifts && strum_ms==b.strum_ms && strum_division==b.strum_division && strum_timing==b.strum_timing; }
 bool operator!=(const ChordSelection& b) const { return !(*this==b); }
};
inline bool valid(ChordSelection s) {
 if(s.root<0 || s.root>=12 || s.mode<0 || s.mode>=12) return false;
 for(auto variant:s.variants) if(int(variant)>=chord_variant_count) return false;
 for(auto shift:s.octave_shifts) if(shift<-1 || shift>1) return false;
 if(s.strum_ms<0 || s.strum_ms>250 || s.strum_ms%10) return false;
 const auto choice=chord_strum_choices[chord_strum_option(s.strum_division,s.strum_timing)];
 if(choice.division!=s.strum_division || choice.timing!=s.strum_timing) return false;
 return true;
}
struct Note { int pitch_class; int midi; std::string name; };
std::vector<Note> notes(Selection s, int octave=4);
enum class MidiRoute:uint8_t { Auto,Usb,Ble };
enum class MidiKind { NoteOn, NoteOff, AllNotesOff };
// fresh_attack is local articulation metadata; USB/BLE encode standard MIDI.
struct MidiEvent { MidiKind kind; uint8_t channel; uint8_t note; uint8_t velocity; bool fresh_attack=false; };
struct MidiTransport { virtual ~MidiTransport()=default; virtual bool send(MidiEvent)=0; virtual void set_enabled(bool) {} virtual void set_route(MidiRoute) {} };
class MidiRouter {
    mutable std::recursive_mutex mutex_;
    MidiTransport& transport_; std::array<int,16> held_{}; uint8_t channel_=1;
    std::array<bool,128> retrigger_{};
public:
    // Independent schedulers own their Note On/Off ledger; use the shared
    // channel and transport without losing per-event velocity.
    bool auxiliary(MidiEvent event) { std::lock_guard<std::recursive_mutex> lock(mutex_);event.channel=channel_;return transport_.send(event); }
    explicit MidiRouter(MidiTransport& t) : transport_(t) { held_.fill(-1); }
    void press(int source, int note,bool fresh_attack=false); void release(int source); void panic();
    void retry();
    bool source_active(int source) const { std::lock_guard<std::recursive_mutex> lock(mutex_); return source>=0 && source<16 && held_[source]>=0 && !retrigger_[held_[source]]; }
    bool channel(int c);
};
class NoteEngine {
    MidiRouter& router_; Selection selection_; int octave_=4;
public:
    NoteEngine(MidiRouter& router,Selection s):router_(router),selection_(s){}
    void selection(Selection s) { if(valid(s) && s!=selection_) { router_.panic(); selection_=s; } }
    void press(int source,int degree);
    int pitch(int degree) const;
    bool playable(int degree) const { const int value=pitch(degree); return value>=0 && value<=127; }
    int octave() const { return octave_; }
    bool set_octave(int value);
    void release(int source) { router_.release(source); }
    void panic() { router_.panic(); }
    bool channel(int value) { return router_.channel(value); }
};
enum class Waveform:uint8_t { Saw, Square, Triangle, Pulse, Sine };
inline constexpr int waveform_count=5;
struct SynthSettings {
 Waveform waveform=Waveform::Square;
 uint16_t attack_ms=50,decay_ms=400;
 bool operator==(const SynthSettings& b) const { return waveform==b.waveform && attack_ms==b.attack_ms && decay_ms==b.decay_ms; }
};
inline bool valid_synth(SynthSettings s) { return int(s.waveform)<waveform_count && s.attack_ms<=2000 && s.decay_ms>=10 && s.decay_ms<=4000; }
struct Setup {
 int channel=1; uint32_t accent=0x2ee7e7;
 bool travel=true; SynthSettings synth{};
 bool receive_clock=false;
 bool global_scale=true;
 MidiRoute midi_route=MidiRoute::Auto;
 bool operator==(const Setup& b) const { return channel==b.channel && accent==b.accent && travel==b.travel && synth==b.synth && receive_clock==b.receive_clock && global_scale==b.global_scale && midi_route==b.midi_route; }
 bool operator!=(const Setup& b) const { return !(*this==b); }
};
inline bool valid_setup(Setup s) { return s.channel>=1 && s.channel<=16 && s.accent<=0xffffff && valid_synth(s.synth) && int(s.midi_route)<=2; }
struct MusicPreset {
 Selection keyboard{},arp{}; ChordSelection chords{}; ArpPreset arpeggiator{};
 int octave=4; bool global_scale=true;
 std::array<char,8> name{{' ',' ',' ',' ',' ',' ',' ',' '}};
 bounce::Settings kinetic{};
 bool operator==(const MusicPreset& b) const { return keyboard==b.keyboard && arp==b.arp && chords==b.chords && arpeggiator==b.arpeggiator && octave==b.octave && global_scale==b.global_scale && name==b.name && kinetic==b.kinetic; }
};
inline bool valid_music_preset(const MusicPreset& p) {
 for(char c:p.name) if(c!=' ' && !(c>='A' && c<='Z') && !(c>='0' && c<='9')) return false;
 return bounce::valid_settings(p.kinetic) && valid(p.keyboard) && valid(p.arp) && valid(p.chords) && valid_arp_preset(p.arpeggiator) && p.octave>=0 && p.octave<=8 &&
   (!p.global_scale || (p.keyboard==p.arp && p.keyboard==Selection{p.chords.root,p.chords.mode}));
}
inline constexpr int music_slot_count=8;
inline constexpr const char* slot_characters=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
inline std::string music_slot_name(const MusicPreset& p) {
 std::string value(p.name.begin(),p.name.end());
 const auto end=value.find_last_not_of(' '); return end==std::string::npos?"---":value.substr(0,end+1);
}
struct Settings {
 virtual ~Settings()=default; virtual Selection load()=0; virtual bool save(Selection)=0;
 virtual Setup load_setup() { return {}; }
 virtual bool save_setup(Setup) { return false; }
 virtual ChordSelection load_chords() { return {}; }
 virtual bool save_chords(ChordSelection) { return false; }
 virtual Selection load_arp() { return load(); }
 // Persist the complete configuration atomically on each platform.
 virtual bool save_all(Selection,Selection,ChordSelection,Setup) { return false; }
 virtual bool load_slot(int,MusicPreset&) { return false; }
 virtual bool delete_slot(int) { return false; }
 virtual bool save_slot(int,const MusicPreset&) { return false; }
 virtual bool save_music(const MusicPreset&,Setup) { return false; }
 virtual ArpPreset load_arpeggiator() { return {}; }
 virtual bool save_arpeggiator(ArpPreset) { return false; }
 virtual bounce::Settings load_bounce(){return {};}
 virtual bool save_bounce(bounce::Settings){return false;}
};
class Application {
    Settings& settings_;
    Selection keyboard_{},arpeggiator_{};
    bool arp_active_=false;
    bool commit(Selection keyboard,Selection arp,ChordSelection chords,Setup setup);
public:
    Selection current, draft; bool selecting=false;
    Setup setup;
    ChordSelection chords;
    explicit Application(Settings& s);
    bounce::Settings load_bounce(){return settings_.load_bounce();}
    bool save_bounce(bounce::Settings s){return bounce::valid_settings(s) && settings_.save_bounce(s);}
    bool save_chords(ChordSelection value);
    void use_arp_scale(bool value);
    Selection keyboard_scale() const { return arp_active_?keyboard_:current; }
    Selection arp_scale() const { return arp_active_?current:arpeggiator_; }
    void begin() { draft=current; selecting=true; }
    void back() { draft=current; selecting=false; }
    bool select();
    bool save_setup(Setup value,bool from_chords=false,const Selection* global_source=nullptr);
    MusicPreset music_preset(ArpPreset arp,int octave) const { MusicPreset value{keyboard_scale(),arp_scale(),chords,arp,octave,setup.global_scale}; value.kinetic=settings_.load_bounce(); if(setup.global_scale)value.kinetic.scale=value.keyboard; return value; }
    bool delete_slot(int slot) { return slot>=0 && slot<music_slot_count && settings_.delete_slot(slot); }
    bool save_slot(int slot,MusicPreset value) {
      if(slot<0 || slot>=music_slot_count || !valid_music_preset(value)) return false;
      size_t first=0; while(first<value.name.size() && value.name[first]==' ') ++first;
      for(size_t i=0;i<value.name.size();++i) value.name[i]=i+first<value.name.size()?value.name[i+first]:' ';
      return settings_.save_slot(slot,value);
    }
    bool load_slot(int slot,MusicPreset& value) { return slot>=0 && slot<music_slot_count && settings_.load_slot(slot,value) && valid_music_preset(value); }
    bool apply_music(const MusicPreset& value) {
      if(!valid_music_preset(value)) return false;
      auto config=setup; config.global_scale=value.global_scale;
      if(!settings_.save_music(value,config)) return false;
      keyboard_=value.keyboard; arpeggiator_=value.arp; chords=value.chords; setup=config;
      current=arp_active_?arpeggiator_:keyboard_; draft=current; return true;
    }
    ArpPreset load_arpeggiator() { return settings_.load_arpeggiator(); }
    bool save_arpeggiator(ArpPreset value) {
      return valid_arp_preset(value) && (value==settings_.load_arpeggiator() || settings_.save_arpeggiator(value));
    }
};
enum class ButtonEvent { Press, Hold, Release, Cancel };
class Buttons {
    struct State { bool raw=false,down=false,held=false,armed=true,primed=false; uint32_t changed=0,started=0,quiet_ms=0; };
    std::array<State,2> states_{};
    uint32_t press_ms_,release_ms_;
public:
    explicit Buttons(uint32_t press_ms=20,uint32_t release_ms=20):press_ms_(press_ms),release_ms_(release_ms){}
    using Callback=void(*)(int,ButtonEvent,void*);
    // Discard a power-on/reset press, including its release. Require a fresh press.
    void require_release(int button,uint32_t quiet_ms=100);
    bool armed(int button) const { return button>=0 && button<2 && states_[button].armed; }
    void update(int button,bool down,uint32_t now,Callback cb,void* context);
};
}

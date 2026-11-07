#include "music.hpp"
namespace sm {
Application::Application(Settings& s):settings_(s),current(s.load()),draft(current),setup(s.load_setup()),chords(s.load_chords()) {
 motion=s.load_motion(); if(!valid_motion(motion))motion={};
 if(!valid(current)) current=draft=Selection{};
 if(!valid_setup(setup)) setup={};
 if(!valid(chords)) chords={};
 keyboard_=current; arpeggiator_=s.load_arp(); if(!valid(arpeggiator_)) arpeggiator_=current;
 // Legacy files share Keyboard/Arp. In global mode this becomes the common
 // selection without writing during boot; chord edits/voicings are retained.
 if(setup.global_scale) { arpeggiator_=current; chords.root=current.root; chords.mode=current.mode; }
}
bool Application::commit(Selection keyboard,Selection arp,ChordSelection value,Setup config) {
 if(!settings_.save_all(keyboard,arp,value,config)) return false;
 keyboard_=keyboard; arpeggiator_=arp; chords=value; setup=config;
 current=arp_active_?arpeggiator_:keyboard_; return true;
}
void Application::use_arp_scale(bool value) {
 if(arp_active_==value) return;
 if(arp_active_) arpeggiator_=current; else keyboard_=current;
 arp_active_=value; current=value?arpeggiator_:keyboard_; draft=current;
}
bool Application::save_setup(Setup value,bool from_chords,const Selection* global_source) {
 if(!valid_setup(value)) return false;
 if(value==setup) return true;
 auto keyboard=keyboard_scale(),arp=arp_scale(); auto chord=chords;
 if(value.global_scale) {
   const Selection chosen=global_source && valid(*global_source)?*global_source:from_chords?Selection{chords.root,chords.mode}:current;
   keyboard=arp=chosen; chord.root=chosen.root; chord.mode=chosen.mode;
 }
 return commit(keyboard,arp,chord,value);
}
bool Application::save_chords(ChordSelection value) {
 if(!valid(value)) return false;
 if(value==chords) return true;
 auto keyboard=keyboard_scale(),arp=arp_scale();
 if(setup.global_scale) keyboard=arp=Selection{value.root,value.mode};
 return commit(keyboard,arp,value,setup);
}
bool Application::select() {
 if(!valid(draft)) return false;
 if(draft!=current) {
   auto keyboard=keyboard_scale(),arp=arp_scale(); auto chord=chords;
   if(arp_active_) arp=draft; else keyboard=draft;
   if(setup.global_scale) { keyboard=arp=draft; chord.root=draft.root; chord.mode=draft.mode; }
   if(!commit(keyboard,arp,chord,setup)) return false;
 }
 selecting=false; return true;
}
void Buttons::require_release(int button,uint32_t quiet_ms) {
 if(button<0 || button>1) return;
 states_[button]={}; states_[button].armed=false; states_[button].quiet_ms=quiet_ms;
}
void Buttons::update(int button,bool down,uint32_t now,Callback cb,void* context) {
 if(button<0 || button>1) return;
 auto& s=states_[button];
 if(!s.armed) {
   if(!s.primed || s.raw!=down) { s.primed=true; s.raw=down; s.changed=now; }
   if(!down && uint32_t(now-s.changed)>=s.quiet_ms) s=State{};
   return;
 }
 if(s.raw!=down) { s.raw=down; s.changed=now; }
 if(s.down!=s.raw && uint32_t(now-s.changed)>=(s.raw?press_ms_:release_ms_)) {
   s.down=s.raw; s.held=false; s.started=now;
   cb(button,s.down?ButtonEvent::Press:ButtonEvent::Release,context);
 }
 if(s.down && !s.held && uint32_t(now-s.started)>=600) { s.held=true; cb(button,ButtonEvent::Hold,context); }
}
}

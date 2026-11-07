#include "ui.hpp"
#include "ui_fonts.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace sm {
namespace {
constexpr const char* patterns[]={"UP","DOWN","UP/DOWN","RANDOM","SHUFFLE","CONVERGE","ANCHOR","LOW & HIGH","GRAVITY","PIVOT"};
static_assert(sizeof(patterns)/sizeof(patterns[0])==arp_pattern_count);
constexpr const char* timings[]={"STRAIGHT","DOTTED","TRIPLET"};
constexpr const char* modes_short[]={"Major","Minor","Harm Min","Mel Min","Dorian","Phrygian","Lydian","Mixo","Locrian","Maj Pent","Min Pent","Blues"};
int arp_frame_index(int column) { return column>=31?column-19:column>=20?column-10:column-6; }
constexpr int arp_page_order[]={0,1,2,3,4,5,6,8,9,7};
constexpr int divisions[]={1,2,4,8,16,32};
lv_obj_t* text(lv_obj_t* parent,const char* value,int x,int y,int width,const lv_font_t* font) {
 auto* o=lv_label_create(parent); lv_label_set_text(o,value); lv_obj_set_pos(o,x,y); lv_obj_set_width(o,width);
 lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_font(o,font,0);
 lv_obj_set_style_text_color(o,lv_color_hex(0xffffff),0);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE); return o;
}
void changed(lv_obj_t* o,const char* s) { if(std::string(lv_label_get_text(o))!=s) lv_label_set_text(o,s); }
}
void Ui::sync_arp_pause() {
 arp_.suspend(setup_open_ || app_.selecting || shutting_down_);
}
bool Ui::can_reset_play_mode() const {
 if((!chord_mode_ && !arp_mode_ && !bounce_mode_) || booting() || setup_open_ || app_.selecting || is_editing_chord() || power_off_visible_ || shutting_down_ || power_pressed_) return false;
 if(bounce_mode_ && is_bounce_menu())return false;
 for(const auto& contact:contacts_) if(contact.input) return false;
 if(arp_mode_ && (arp_wheel_busy() || arp_gesture_.input)) return false;
 for(auto* input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input))
   if(lv_indev_get_type(input)==LV_INDEV_TYPE_POINTER && lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED) return false;
 return true;
}
bool Ui::has_resettable_state() const {
 if(!can_reset_play_mode())return false;
 if(bounce_mode_)return bounce_has_balls();
 if(chord_mode_){
   for(auto variant:app_.chords.variants)if(variant!=ChordVariant::Triad)return true;
   for(auto octave:app_.chords.octave_shifts)if(octave!=0)return true;
   return false;
 }
 const auto current=arp_.preset(arp_page_);ArpPreset defaults;defaults.settings.clock=current.settings.clock;
 return !(current==defaults);
}
bool Ui::reset_play_mode() {
 if(!can_reset_play_mode()) return false;
 if(chord_mode_) return reset_chord_buttons();
 if(bounce_mode_)return reset_bounce();
 arp_.reset_defaults(); arp_page_=0;
 refresh(); return true;
}
bool Ui::persist_arpeggiator(bool force) {
 if(!arp_save_ready_) return false;
 const auto value=arp_.preset(arp_page_);
 if(value==arp_saved_) {
   arp_save_failed_=false;
   if(arp_mode_ && arp_error_ && std::string(lv_label_get_text(arp_error_))=="Saving arp failed") changed(arp_error_,"");
   return true;
 }
 const uint32_t now=lv_tick_get();
 if(!force && arp_save_failed_ && uint32_t(now-arp_save_retry_at_)<1000) return false;
 if(!app_.save_arpeggiator(value)) {
   arp_save_failed_=true; arp_save_retry_at_=now;
   if(arp_mode_ && arp_error_) changed(arp_error_,"Saving arp failed");
   return false;
 }
 arp_saved_=value; arp_save_failed_=false;
 if(arp_mode_ && arp_error_ && std::string(lv_label_get_text(arp_error_))=="Saving arp failed") changed(arp_error_,"");
 return true;
}
void Ui::sync_arp_clock() {
 sync_bounce_clock();
 chord_.clock_sync(app_.setup.receive_clock);
 auto settings=arp_.state().settings;
 const auto clock=app_.setup.receive_clock?ArpClock::Midi:ArpClock::Tap;
 if(settings.clock!=clock) { settings.clock=clock; arp_.settings(settings); }
 if(app_.setup.receive_clock && arp_page_==4) arp_page_=5;
}
void Ui::toggle_arpeggiator() {
 if(setup_open_ || app_.selecting || shutting_down_ || power_off_visible_) return;
 arp_.enable(false); reset_midi_session();
 if(bounce_mode_){persist_bounce();activate_bounce(false);}
 if(chord_mode_) { chord_mode_=false; arp_mode_=true; }
 else if(arp_mode_) {arp_mode_=false;bounce_mode_=true;}
 else if(bounce_mode_)bounce_mode_=false;
 else chord_mode_=true;
 app_.use_arp_scale(arp_mode_);
 geometry_count_=-1; arp_gesture_={}; arp_display_note_=-1;
 if(bounce_mode_)activate_bounce(true);
 arp_.enable(arp_mode_); sync_arp_pause(); refresh();
}
int Ui::arp_selector_page(int column) const {
 if(column>=31) return 9;
 if(column==21) return 8;
 if(column==20) return 6;
 constexpr int pages[]={5,0,0,1,1,2,3,3,4,7}; return pages[column-6];
}
int Ui::arp_selector_count(int column) const {
 if(column==8) return int(modes().size());
 if(column==31) return 17;
 if(column==32) return 3;
 if(column==21) return 20;
 if(column==20) return 7;
 constexpr int counts[]={20,12,0,9,9,arp_pattern_count,6,3,261,51}; return counts[column-6];
}
const char* Ui::arp_selector_text(int column,int option,char* buffer,size_t size) const {
 switch(column) {
 case 31: if(!option) return "OFF"; std::snprintf(buffer,size,"%d",option); return buffer;
 case 32: std::snprintf(buffer,size,"%d",option+2); return buffer;
 case 21: std::snprintf(buffer,size,"%d %%",100-option*5); return buffer;
 case 6: std::snprintf(buffer,size,"%d %%",100-option*5); return buffer;
 case 7: return roots()[option];
 case 8: return modes_short[option];
 case 9: case 10: std::snprintf(buffer,size,"%d",option); return buffer;
 case 11: return patterns[option];
 case 12: std::snprintf(buffer,size,"1/%d",divisions[option]); return buffer;
 case 13: return timings[option];
 case 14: std::snprintf(buffer,size,"%d",40+option); return buffer;
 case 15: std::snprintf(buffer,size,"%d %%",option); return buffer;
 case 20: if(!option) return "OFF"; std::snprintf(buffer,size,"%d",8-option); return buffer;
 }
 return "";
}
void Ui::refresh_arp_notes() {
 for(auto*& label:arp_note_labels_) if(label) { lv_obj_delete(label); label=nullptr; }
 const auto ns=notes(app_.current);
 for(size_t i=0;i<ns.size();++i) {
   const float angle=(-90.f+360.f*i/ns.size())*3.14159265359f/180;
   arp_note_labels_[i]=text(wheel_,ns[i].name.c_str(),int(std::lround(233+190*std::cos(angle)))-34,
     int(std::lround(233+190*std::sin(angle)))-17,68,&barlow_28);
 }
 arp_display_note_=-1; highlight_.fill(0); fading_.fill(false); rendered_count_=-1;
 lv_obj_invalidate(wheel_);
}
void Ui::settle_arp_selectors() {
 release_arp_tap();
 for(int column:arp_columns) if(selectors_[column].input) {
   animate_selector(column,selectors_[column].offset); selectors_[column].input=nullptr;
 }
 arp_gesture_={};
}
bool Ui::arp_wheel_busy() const {
 if(arp_tap_input_) return true;
 for(int column:arp_columns) if(selectors_[column].input) return true;
 return false;
}
void Ui::apply_arp_option(int column,int option) {
 if(column==7 || column==8) {
   Selection selection=app_.current;
   if(column==7) selection.root=option; else selection.mode=option;
   if(selection==app_.current) return;
   app_.begin(); app_.draft=selection;
   if(!app_.select()) { app_.back(); changed(arp_error_,"Saving failed - try again"); return; }
   changed(arp_error_,""); arp_.selection(app_.current);
   refresh_arp_notes(); return;
 }
 auto settings=arp_.state().settings;
 switch(column) {
 case 6: settings.probability=100-option*5; break;
 case 9:
   settings.low=option;
   if(settings.high<option) {
     settings.high=option; auto& other=selectors_[10]; lv_anim_delete(&other,nullptr); other.target=option*other.step; set_selector_offset(10,other.target);
   }
   break;
 case 10:
   settings.high=option;
   if(settings.low>option) {
     settings.low=option; auto& other=selectors_[9]; lv_anim_delete(&other,nullptr); other.target=option*other.step; set_selector_offset(9,other.target);
   }
   break;
 case 11: settings.pattern=arp_patterns[option]; break;
 case 12: settings.division=divisions[option]; break;
 case 13: settings.timing=ArpTiming(option); break;
 case 14: arp_.tempo(40+option); update_arp(); return;
 case 15: settings.swing=option; break;
 case 31: settings.ratchet_every=option; break;
 case 32: settings.ratchet_count=option+2; break;
 case 21: settings.gate=100-option*5; break;
 case 20: settings.block_size=option?8-option:0; break;
 }
 arp_.settings(settings); update_arp();
}
void Ui::build_arp() {
 arp_panel_=lv_obj_create(wheel_); lv_obj_remove_style_all(arp_panel_);
 lv_obj_set_pos(arp_panel_,83,83); lv_obj_set_size(arp_panel_,300,300);
 lv_obj_remove_flag(arp_panel_,LV_OBJ_FLAG_SCROLLABLE); lv_obj_remove_flag(arp_panel_,LV_OBJ_FLAG_CLICKABLE);
 arp_title_=text(arp_panel_,"",6,38,288,&barlow_28);
 arp_ratchet_every_=text(arp_panel_,"EVERY",48,75,100,&barlow_18);
 arp_ratchet_count_=text(arp_panel_,"COUNT",152,75,100,&barlow_18);
 arp_octave_dash_=text(arp_panel_,"-",136,128,28,&barlow_32); lv_obj_align(arp_octave_dash_,LV_ALIGN_CENTER,0,-3);
 arp_tap_button_=lv_obj_create(arp_panel_); lv_obj_remove_style_all(arp_tap_button_);
 lv_obj_set_pos(arp_tap_button_,244-83,177-83); lv_obj_set_size(arp_tap_button_,112,112);
 lv_obj_set_style_radius(arp_tap_button_,56,0); lv_obj_set_style_bg_opa(arp_tap_button_,255,0);
 lv_obj_set_style_bg_color(arp_tap_button_,lv_color_hex(0),0);
 lv_obj_remove_flag(arp_tap_button_,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_flag(arp_tap_button_,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(arp_tap_button_,LV_OBJ_FLAG_ADV_HITTEST);
 lv_obj_add_event_cb(arp_tap_button_,arp_tap_event,LV_EVENT_ALL,this);
 make_menu_frame(arp_tap_frame_,0,0,112,112,56,56,arp_tap_button_,accent(.5f));
 arp_extra_=text(arp_tap_button_,"TAP",0,0,112,&barlow_28); lv_obj_center(arp_extra_);
 arp_error_=text(arp_panel_,"",6,252,288,&barlow_14);
 const auto settings=arp_.state().settings;
 int division_index=0; while(division_index<5 && divisions[division_index]!=settings.division) ++division_index;
 const int options[]={(100-settings.probability+2)/5,app_.current.root,app_.current.mode,settings.low,settings.high,arp_pattern_option(settings.pattern),division_index,int(settings.timing),arp_.state().bpm-40,settings.swing,settings.block_size?8-settings.block_size:0,(100-settings.gate+2)/5,settings.ratchet_every,settings.ratchet_count-2};
 // Root/mode and other pairs sit side by side, sharing the same row center.
 const int xs[]={163,115,194,148,246,118,98,204,110,163,163,163,135,239};
 const int widths[]={140,74,157,72,72,230,100,164,104,140,140,140,92,92};
 for(int column:arp_columns) {
   const int i=arp_frame_index(column),x=xs[i],width=widths[i],center=column>=31?248:233;
   auto* view=selector_views_[column]=lv_obj_create(arp_panel_);
   const int hit_width=width;
   lv_obj_remove_style_all(view); lv_obj_set_pos(view,x-(hit_width-width)/2-83,center-72-83); lv_obj_set_size(view,hit_width,144);
   lv_obj_remove_flag(view,LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(view,LV_OBJ_FLAG_CLICKABLE);
   auto& state=selectors_[column]; state.owner=this; state.column=column;
   state.center=center; state.half_height=72; state.step=48; state.font_size=column==8?28:32;
   lv_anim_delete(&state,nullptr); state.input=nullptr; state.target=options[i]*state.step; set_selector_offset(column,state.target);
   lv_obj_add_event_cb(view,selection_event,LV_EVENT_ALL,this);
   lv_obj_add_event_cb(view,[](lv_event_t* e) {
     auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
     for(int c:arp_columns) if(lv_event_get_target_obj(e)==self->selector_views_[c]) self->draw_selector(lv_event_get_layer(e),c);
   },LV_EVENT_DRAW_MAIN,this);
   const bool left=column==7 || column==12;
   const bool right=column==8 || column==13;
   const bool octave=column==9 || column==10;
   make_menu_frame(arp_frames_[i],x-83,center-26-83,width,52,octave?26:left?26:right?6:26,octave?26:right?26:left?6:26,arp_panel_,accent(.5f));
 }
 update_arp();
}
void Ui::update_arp() {
 if(!arp_panel_ || !arp_mode_) return;
 const auto state=arp_.state();
 if(app_.setup.receive_clock && arp_page_==4) arp_page_=5;
 const char* titles[]={"SCALE","OCTAVES","PATTERN","DIVISOR","BPM","PROBABILITY","BLOCKSIZE","SWING","GATE","RATCHET"};
 changed(arp_title_,titles[arp_page_]);
 const auto tint=accent(.5f);
 if(!lv_color_eq(lv_obj_get_style_text_color(arp_title_,0),tint)) lv_obj_set_style_text_color(arp_title_,tint,0);
 for(int column:arp_columns) {
   const bool visible=arp_page_==arp_selector_page(column);
   for(auto* object:{selector_views_[column],arp_frames_[arp_frame_index(column)].object}) {
     if(visible) lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
   }
 }
 for(auto* label:{arp_ratchet_every_,arp_ratchet_count_}) {
   if(arp_page_==9) lv_obj_remove_flag(label,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);
 }
 if(arp_page_==1) lv_obj_remove_flag(arp_octave_dash_,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(arp_octave_dash_,LV_OBJ_FLAG_HIDDEN);
 if(arp_page_==4) lv_obj_remove_flag(arp_tap_button_,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(arp_tap_button_,LV_OBJ_FLAG_HIDDEN);
 float level=arp_tap_input_?1.f:0.f;
 if(!arp_tap_input_ && arp_tap_fading_) {
   const uint32_t elapsed=uint32_t(lv_tick_get()-arp_tap_released_);
   const float t=1-std::min<uint32_t>(elapsed,500u)/500.f;
   level=t*t*(3-2*t); if(elapsed>=500) arp_tap_fading_=false;
 }
 const auto feedback=lv_color_mix(lv_color_hex(0xffffff),tint,72);
 const auto fill=lv_color_mix(feedback,lv_color_hex(0),uint8_t(56*level));
 if(!lv_color_eq(lv_obj_get_style_bg_color(arp_tap_button_,0),fill)) lv_obj_set_style_bg_color(arp_tap_button_,fill,0);
 auto& gate=selectors_[21];
 const int gate_option=(100-state.settings.gate+2)/5;
 if(!gate.input && gate.target/gate.step!=gate_option) {
   lv_anim_delete(&gate,nullptr); gate.target=gate_option*gate.step; set_selector_offset(21,gate.target);
 }
 auto& bpm=selectors_[14];
 if(bpm.target/bpm.step+40!=state.bpm) { lv_anim_delete(&bpm,nullptr); bpm.target=(state.bpm-40)*bpm.step; set_selector_offset(14,bpm.target); }
}
void Ui::arp_touch(lv_event_code_t code) {
 if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_PRESSING && code!=LV_EVENT_RELEASED && code!=LV_EVENT_PRESS_LOST) return;
 auto* input=lv_indev_active(); if(!input) return;
 for(auto& blocked:blocked_inputs_) if(blocked==input) {
   if(code==LV_EVENT_PRESSED) { blocked=nullptr; break; }
   if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) blocked=nullptr;
   return;
 }
 lv_point_t p; lv_indev_get_point(input,&p);
 if(code==LV_EVENT_PRESSED) {
   const int degree=hit(p.x,p.y);
   if(degree>=0) { arp_.toggle(degree); return; }
   if(degree!=-2 || arp_gesture_.input || arp_wheel_busy()) return;
   arp_gesture_={input,p,0,false};
 } else if(arp_gesture_.input==input) {
   auto& g=arp_gesture_; const int dx=p.x-g.start.x,dy=p.y-g.start.y;
   if(code==LV_EVENT_RELEASED) {
     if(std::abs(dx)>=40 && std::abs(dx)>std::abs(dy)*1.3f && !arp_wheel_busy()) {
       const int direction=dx<0?1:9;
       int position=0; while(arp_page_order[position]!=arp_page_) ++position;
       position=(position+direction)%10;
       if(app_.setup.receive_clock && arp_page_order[position]==4) position=(position+direction)%10;
       arp_page_=arp_page_order[position];
       update_arp();
     }
   }
   if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) g={};
 }
}
void Ui::release_arp_tap() {
 if(!arp_tap_input_) return;
 arp_tap_input_=nullptr; arp_tap_released_=lv_tick_get(); arp_tap_fading_=true;
}
void Ui::arp_tap_event(lv_event_t* e) {
 auto* self=static_cast<Ui*>(lv_event_get_user_data(e)); const auto code=lv_event_get_code(e);
 if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_RELEASED && code!=LV_EVENT_PRESS_LOST) return;
 if(!self->arp_mode_ || self->arp_page_!=4 || self->setup_open_ || self->power_off_visible_ || self->shutting_down_) return;
 auto* input=lv_indev_active(); if(!input) return;
 for(auto& blocked:self->blocked_inputs_) if(blocked==input) {
   if(code==LV_EVENT_PRESSED) { blocked=nullptr; break; }
   if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) blocked=nullptr;
   return;
 }
 if(code==LV_EVENT_PRESSED && !self->arp_tap_input_) {
   self->arp_tap_input_=input; self->arp_tap_fading_=false;
   self->arp_.tap(uint64_t(lv_tick_get())*1000);
 } else if(self->arp_tap_input_==input) self->release_arp_tap();
 self->update_arp();
}
}

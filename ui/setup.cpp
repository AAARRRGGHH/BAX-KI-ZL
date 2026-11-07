#include "ui.hpp"
#include "selector_mask.hpp"
#include "ui_fonts.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace sm {
namespace {
constexpr float pi=3.14159265358979323846f;
constexpr float color_outer=217.f; // Same outer radius as the note buttons.
constexpr float color_inner=color_outer-44.f;
constexpr float color_radius=(color_outer+color_inner)/2;
void plain(lv_obj_t* o) { lv_obj_remove_style_all(o); lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE); }
lv_obj_t* text(lv_obj_t* parent,const char* value,int x,int y,int w,const lv_font_t* font) {
 auto* o=lv_label_create(parent); lv_label_set_text(o,value); lv_obj_set_pos(o,x,y); lv_obj_set_width(o,w);
 lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(0xffffff),0);
 lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0); lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE); return o;
}
void bubble(lv_obj_t* o) { lv_obj_add_flag(o,LV_OBJ_FLAG_EVENT_BUBBLE); }
}
void Ui::build_setup() {
 setup_=lv_obj_create(lv_screen_active()); plain(setup_); lv_obj_set_size(setup_,466,466);
 lv_obj_set_style_radius(setup_,233,0); lv_obj_set_style_clip_corner(setup_,true,0);
 lv_obj_set_style_bg_color(setup_,lv_color_hex(0),0); lv_obj_set_style_bg_opa(setup_,255,0);
 lv_obj_add_flag(setup_,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(setup_,setup_event,LV_EVENT_ALL,this);
 setup_title_=text(setup_,"MIDI",53,59,360,&barlow_43);
 for(int i=0;i<3;++i) {
   auto* button=midi_route_buttons_[i]=lv_button_create(setup_); plain(button); bubble(button);
   lv_obj_set_pos(button,89+i*100,128); lv_obj_set_size(button,88,42);
   lv_obj_set_style_radius(button,21,0); lv_obj_set_style_bg_color(button,lv_color_hex(0),0);
   lv_obj_set_style_bg_opa(button,255,0); lv_obj_set_style_border_width(button,1,0);
   auto* label=text(button,i==0?"AUTO":i==1?"USB":"BLE",0,0,88,&barlow_20); lv_obj_center(label);
   lv_obj_add_event_cb(button,[](lv_event_t* e) {
     auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
     if(!self->setup_open_ || self->setup_page_!=0 || self->power_off_visible_) return;
     for(int i=0;i<3;++i) if(lv_event_get_target_obj(e)==self->midi_route_buttons_[i]) self->setup_draft_.midi_route=MidiRoute(i);
     self->update_setup();
   },LV_EVENT_CLICKED,this);
 }
 channel_caption_=text(setup_,"CHANNEL",72,199,144,&barlow_24);
 clock_caption_=text(setup_,"MIDI CLOCK",243,199,144,&barlow_24);
 clock_toggle_=lv_switch_create(setup_); lv_obj_set_pos(clock_toggle_,271,287); lv_obj_set_size(clock_toggle_,88,42); bubble(clock_toggle_);
 lv_obj_set_style_bg_color(clock_toggle_,lv_color_hex(0x303030),LV_PART_MAIN);
 lv_obj_set_style_bg_color(clock_toggle_,lv_color_hex(0xffffff),LV_PART_KNOB);
 lv_obj_add_event_cb(clock_toggle_,[](lv_event_t* e) {
   auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
   if(!self->setup_open_ || self->setup_page_!=0 || self->power_off_visible_) return;
   self->setup_draft_.receive_clock=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED); self->update_setup();
 },LV_EVENT_VALUE_CHANGED,this);
 clock_status_=text(setup_,"OFF",271,343,88,&barlow_24);
 clock_tempo_row_=lv_obj_create(setup_); plain(clock_tempo_row_);
 lv_obj_remove_flag(clock_tempo_row_,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_size(clock_tempo_row_,LV_SIZE_CONTENT,LV_SIZE_CONTENT);
 lv_obj_set_flex_flow(clock_tempo_row_,LV_FLEX_FLOW_ROW);
 lv_obj_set_flex_align(clock_tempo_row_,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
 lv_obj_set_style_pad_column(clock_tempo_row_,8,0);
 clock_tempo_value_=text(clock_tempo_row_,"--",0,0,LV_SIZE_CONTENT,&barlow_32);
 clock_tempo_unit_=text(clock_tempo_row_,"BPM",0,0,LV_SIZE_CONTENT,&barlow_32);
 lv_obj_align(clock_tempo_row_,LV_ALIGN_TOP_MID,0,393);
 lv_obj_add_flag(clock_tempo_row_,LV_OBJ_FLAG_HIDDEN);
 channel_panel_=selector_views_[2]=lv_obj_create(setup_); plain(channel_panel_);
 lv_obj_set_size(channel_panel_,110,144); lv_obj_set_pos(channel_panel_,89,236); bubble(channel_panel_);
 lv_obj_add_flag(channel_panel_,LV_OBJ_FLAG_CLICKABLE);
 selectors_[2].owner=this; selectors_[2].column=2;
 selectors_[2].step=56; selectors_[2].half_height=72; selectors_[2].center=308; selectors_[2].font_size=38;
 lv_obj_set_style_bg_color(channel_panel_,lv_color_hex(0),0); lv_obj_set_style_bg_opa(channel_panel_,255,0);
 lv_obj_add_event_cb(channel_panel_,selection_event,LV_EVENT_ALL,this);
 lv_obj_add_event_cb(channel_panel_,[](lv_event_t* e) {
   static_cast<Ui*>(lv_event_get_user_data(e))->draw_selector(lv_event_get_layer(e),2);
 },LV_EVENT_DRAW_MAIN,this);
 for(int bottom=0;bottom<2;++bottom) {
   auto* mask=lv_obj_create(channel_panel_); plain(mask); lv_obj_remove_flag(mask,LV_OBJ_FLAG_CLICKABLE);
   lv_obj_set_size(mask,110,30); lv_obj_set_pos(mask,0,bottom?114:0);
   lv_obj_set_style_bg_color(mask,lv_color_hex(0),0); lv_obj_set_style_bg_grad_color(mask,lv_color_hex(0),0);
   lv_obj_set_style_bg_opa(mask,255,0); lv_obj_set_style_bg_grad_dir(mask,LV_GRAD_DIR_VER,0);
   lv_obj_set_style_bg_main_opa(mask,bottom?0:255,0); lv_obj_set_style_bg_grad_opa(mask,bottom?255:0,0);
   selector_mask_never_covers(mask);
 }
 add_menu_frame(3,89,276,110,64,32,32,setup_);
 color_panel_=lv_obj_create(setup_); plain(color_panel_); lv_obj_set_size(color_panel_,466,466);
 lv_obj_set_pos(color_panel_,0,0); bubble(color_panel_);
 auto* ring=lv_obj_create(color_panel_); plain(ring); lv_obj_set_size(ring,466,466); lv_obj_set_pos(ring,0,0);
 lv_obj_add_flag(ring,LV_OBJ_FLAG_CLICKABLE); bubble(ring);
 lv_obj_add_event_cb(ring,[](lv_event_t* e) {
   auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
   if(self->color_pixels_.empty()) return;
   lv_draw_image_dsc_t d; lv_draw_image_dsc_init(&d); d.src=&self->color_image_;
   lv_area_t bounds; lv_obj_get_coords(lv_event_get_target_obj(e),&bounds); d.image_area=bounds;
   lv_draw_image(lv_event_get_layer(e),&d,&bounds);
 },LV_EVENT_DRAW_MAIN,this);
 color_value_=text(color_panel_,"UI COLOR",83,0,300,&barlow_43); lv_obj_center(color_value_);
 color_marker_=lv_obj_create(color_panel_); plain(color_marker_); lv_obj_set_size(color_marker_,22,22);
 lv_obj_set_style_radius(color_marker_,11,0); lv_obj_set_style_border_color(color_marker_,lv_color_hex(0xffffff),0);
 lv_obj_set_style_bg_color(color_marker_,lv_color_hex(0xffffff),0); lv_obj_set_style_bg_opa(color_marker_,LV_OPA_COVER,0);
 lv_obj_set_style_border_width(color_marker_,2,0); lv_obj_remove_flag(color_marker_,LV_OBJ_FLAG_CLICKABLE);
 setup_error_=text(setup_,"",83,404,300,&barlow_14);
 global_panel_=lv_obj_create(setup_); plain(global_panel_); lv_obj_set_size(global_panel_,466,466); bubble(global_panel_);
 global_title_=text(global_panel_,"GLOBAL SCALE",53,92,360,&barlow_43);
 global_toggle_=lv_switch_create(global_panel_); lv_obj_set_size(global_toggle_,88,42); lv_obj_center(global_toggle_); bubble(global_toggle_);
 lv_obj_set_style_bg_color(global_toggle_,lv_color_hex(0x303030),LV_PART_MAIN);
 lv_obj_set_style_bg_color(global_toggle_,lv_color_hex(0xffffff),LV_PART_KNOB);
 global_status_=text(global_panel_,"ON",189,273,88,&barlow_24);
 lv_obj_add_event_cb(global_toggle_,[](lv_event_t* e) {
   auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
   if(!self->setup_open_ || self->setup_page_!=3 || self->power_off_visible_) return;
   self->setup_draft_.global_scale=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED); self->update_setup();
 },LV_EVENT_VALUE_CHANGED,this);
 slots_panel_=lv_obj_create(setup_); plain(slots_panel_); lv_obj_set_size(slots_panel_,466,466); bubble(slots_panel_);
 slots_title_=text(slots_panel_,"LOAD / SAVE",53,59,360,&barlow_43);
 auto* view=selector_views_[22]=lv_obj_create(slots_panel_); plain(view); lv_obj_set_pos(view,109,146); lv_obj_set_size(view,248,144);
 lv_obj_add_flag(view,LV_OBJ_FLAG_CLICKABLE); bubble(view);
 auto& selector=selectors_[22]; selector.owner=this; selector.column=22; selector.step=56; selector.half_height=72; selector.center=218; selector.font_size=32;
 lv_obj_add_event_cb(view,selection_event,LV_EVENT_ALL,this);
 lv_obj_add_event_cb(view,[](lv_event_t* e) { static_cast<Ui*>(lv_event_get_user_data(e))->draw_selector(lv_event_get_layer(e),22); },LV_EVENT_DRAW_MAIN,this);
 make_menu_frame(slot_frames_[0],109,186,248,64,32,32,slots_panel_,accent(.5f));
 slot_delete_=lv_obj_create(slots_panel_); plain(slot_delete_); lv_obj_set_pos(slot_delete_,365,186); lv_obj_set_size(slot_delete_,64,64);
 lv_obj_add_flag(slot_delete_,LV_OBJ_FLAG_CLICKABLE); bubble(slot_delete_);
 lv_obj_add_event_cb(slot_delete_,slot_event,LV_EVENT_CLICKED,this);
 // Variant A: slender outline, with visible rows 0..63 matching the 64 px field.
 constexpr int strokes[][4]={{23,1,41,1},{23,1,23,9},{41,1,41,9},{9,9,55,9},
   {15,16,18,63},{18,63,46,63},{46,63,49,16},{25,22,27,54},{39,22,37,54}};
 for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
   float coverage=0;
   // Integrate a soft one-pixel edge instead of quantizing to 17 hard coverage
   // levels. The continuous distance also joins adjacent strokes without seams.
   for(int sy=0;sy<4;++sy) for(int sx=0;sx<4;++sx) {
     const float px=x+(sx+.5f)/4,py=y+(sy+.5f)/4;
     float distance=100;
     for(const auto& stroke:strokes) {
       const float dx=float(stroke[2]-stroke[0]),dy=float(stroke[3]-stroke[1]);
       const float t=std::clamp(((px-stroke[0])*dx+(py-stroke[1])*dy)/(dx*dx+dy*dy),0.f,1.f);
       distance=std::min(distance,std::hypot(px-stroke[0]-t*dx,py-stroke[1]-t*dy));
     }
     coverage+=std::clamp(1.5f-distance,0.f,1.f);
   }
   slot_trash_pixels_[y*64+x]=uint8_t(std::lround(coverage*255/16));
 }
 slot_trash_image_.header.magic=LV_IMAGE_HEADER_MAGIC; slot_trash_image_.header.cf=LV_COLOR_FORMAT_A8;
 slot_trash_image_.header.w=64; slot_trash_image_.header.h=64; slot_trash_image_.header.stride=64;
 slot_trash_image_.data_size=slot_trash_pixels_.size(); slot_trash_image_.data=slot_trash_pixels_.data();
 lv_obj_add_event_cb(slot_delete_,[](lv_event_t* e) {
   auto* self=static_cast<Ui*>(lv_event_get_user_data(e)); lv_area_t bounds; lv_obj_get_coords(lv_event_get_target_obj(e),&bounds);
   lv_draw_image_dsc_t draw; lv_draw_image_dsc_init(&draw); draw.src=&self->slot_trash_image_; draw.image_area=bounds;
   draw.recolor=lv_color_hex(self->slot_occupied_?self->setup_draft_.accent:0x505050); draw.recolor_opa=255;
   lv_draw_image(lv_event_get_layer(e),&draw,&bounds);
 },LV_EVENT_DRAW_MAIN,this);

 for(int i=0;i<2;++i) {
   auto* button=lv_obj_create(slots_panel_); plain(button); lv_obj_set_pos(button,83+i*156,343); lv_obj_set_size(button,144,58);
   lv_obj_add_flag(button,LV_OBJ_FLAG_CLICKABLE); bubble(button);
   make_menu_frame(slot_frames_[i+1],0,0,144,58,29,29,button,accent(.5f));
   auto* caption=text(button,i?"LOAD":"SAVE",0,0,144,&barlow_28); lv_obj_center(caption);
   lv_obj_add_event_cb(button,slot_event,LV_EVENT_CLICKED,this);
   if(i) slot_load_=button; else { slot_save_=button; slot_save_text_=caption; }
 }
 slot_dialog_panel_=lv_obj_create(setup_); plain(slot_dialog_panel_); lv_obj_set_size(slot_dialog_panel_,466,466);
 lv_obj_set_style_bg_color(slot_dialog_panel_,lv_color_hex(0),0); lv_obj_set_style_bg_opa(slot_dialog_panel_,255,0);
 lv_obj_add_flag(slot_dialog_panel_,LV_OBJ_FLAG_CLICKABLE);
 slot_dialog_name_=text(slot_dialog_panel_,"",53,210,360,&barlow_32);
 for(int i=0;i<8;++i) {
   const int column=23+i,x=67+42*i;
   auto* letter=selector_views_[column]=slot_letter_views_[i]=lv_obj_create(slot_dialog_panel_); plain(letter);
   lv_obj_set_pos(letter,x,161); lv_obj_set_size(letter,38,144); lv_obj_add_flag(letter,LV_OBJ_FLAG_CLICKABLE);
   auto& state=selectors_[column]; state.owner=this; state.column=column; state.step=56; state.half_height=72; state.center=233; state.font_size=32;
   lv_obj_add_event_cb(letter,selection_event,LV_EVENT_ALL,this);
   lv_obj_add_event_cb(letter,[](lv_event_t* e) {
     auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
     for(int c=23;c<31;++c) if(lv_event_get_target_obj(e)==self->selector_views_[c]) self->draw_selector(lv_event_get_layer(e),c);
   },LV_EVENT_DRAW_MAIN,this);
   make_menu_frame(slot_dialog_frames_[i],i==0?x-14:x,205,(i==0 || i==7)?52:38,56,i==0?28:8,i==7?28:8,slot_dialog_panel_,accent(.5f));
 }
 for(int i=0;i<2;++i) {
   auto* button=lv_obj_create(slot_dialog_panel_); plain(button); lv_obj_set_pos(button,83+i*156,343); lv_obj_set_size(button,144,58);
   lv_obj_add_flag(button,LV_OBJ_FLAG_CLICKABLE);
   make_menu_frame(slot_dialog_frames_[8+i],0,0,144,58,29,29,button,accent(.5f));
   auto* caption=text(button,i?"CANCEL":"SAVE",0,0,144,&barlow_28); lv_obj_center(caption);
   lv_obj_add_event_cb(button,slot_dialog_event,LV_EVENT_CLICKED,this);
   if(i) slot_cancel_=button; else { slot_confirm_=button; slot_confirm_text_=caption; }
 }
 lv_obj_add_flag(slot_dialog_panel_,LV_OBJ_FLAG_HIDDEN);
 build_travel();
 lv_obj_move_foreground(setup_error_);
 lv_obj_add_flag(setup_,LV_OBJ_FLAG_HIDDEN);
}
void Ui::update_setup() {
 for(int i=0;i<3;++i) {
   auto* button=midi_route_buttons_[i];
   if(setup_page_==0) lv_obj_remove_flag(button,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(button,LV_OBJ_FLAG_HIDDEN);
   lv_obj_set_style_border_color(button,lv_color_hex(int(setup_draft_.midi_route)==i?setup_draft_.accent:0x606060),0);
 }
 if(setup_page_==4) lv_obj_remove_flag(slots_panel_,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(slots_panel_,LV_OBJ_FLAG_HIDDEN);
 lv_obj_set_style_text_color(slots_title_,lv_color_hex(setup_draft_.accent),0);
 for(auto& frame:slot_frames_) if(!frame.pixels.empty() && (frame.pixels[0]&0xffffffu)!=setup_draft_.accent) {
   for(auto& pixel:frame.pixels) pixel=(pixel&0xff000000u)|setup_draft_.accent;
   lv_image_cache_drop(&frame.image); lv_obj_invalidate(frame.object);
 }
 for(auto& frame:slot_dialog_frames_) if(!frame.pixels.empty() && (frame.pixels[0]&0xffffffu)!=setup_draft_.accent) {
   for(auto& pixel:frame.pixels) pixel=(pixel&0xff000000u)|setup_draft_.accent;
   lv_image_cache_drop(&frame.image); lv_obj_invalidate(frame.object);
 }
 if(setup_page_==3) lv_obj_remove_flag(global_panel_,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(global_panel_,LV_OBJ_FLAG_HIDDEN);
 lv_obj_set_style_text_color(global_title_,lv_color_hex(setup_draft_.accent),0);
 lv_obj_set_style_bg_color(global_toggle_,lv_color_hex(setup_draft_.accent),uint32_t(LV_PART_INDICATOR)|uint32_t(LV_STATE_CHECKED));
 if(setup_draft_.global_scale) lv_obj_add_state(global_toggle_,LV_STATE_CHECKED); else lv_obj_remove_state(global_toggle_,LV_STATE_CHECKED);
 lv_label_set_text(global_status_,setup_draft_.global_scale?"ON":"OFF");
 lv_obj_set_style_text_color(setup_title_,lv_color_hex(setup_draft_.accent),0);
 lv_obj_set_style_text_color(color_value_,lv_color_hex(setup_draft_.accent),0);
 for(size_t i=3;i<menu_frames_.size();++i) {
   auto& current=menu_frames_[i];
   if(!current.pixels.empty() && (current.pixels[0]&0xffffffu)!=setup_draft_.accent) {
     for(auto& pixel:current.pixels) pixel=(pixel&0xff000000u)|setup_draft_.accent;
     lv_image_cache_drop(&current.image); lv_obj_invalidate(current.object);
   }
 }
 auto& frame=menu_frames_[3];
 const float a=(hue_for_accent(setup_draft_.accent)-270)*pi/180;
 lv_obj_set_pos(color_marker_,int(std::lround(233+color_radius*std::cos(a)))-11,int(std::lround(233+color_radius*std::sin(a)))-11);
 for(auto* object:{channel_panel_,setup_title_,frame.object,channel_caption_,clock_caption_,clock_toggle_,clock_status_}) {
   if(setup_page_!=0) lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN);
 }
 if(setup_draft_.receive_clock) lv_obj_add_state(clock_toggle_,LV_STATE_CHECKED); else lv_obj_remove_state(clock_toggle_,LV_STATE_CHECKED);
 lv_obj_set_style_bg_color(clock_toggle_,lv_color_hex(setup_draft_.accent),uint32_t(LV_PART_INDICATOR)|uint32_t(LV_STATE_CHECKED));
 lv_label_set_text(clock_status_,setup_draft_.receive_clock?"ON":"OFF");
 lv_obj_set_style_text_color(clock_tempo_unit_,lv_color_hex(setup_draft_.accent),0);
 update_clock_tempo(true);
 if(setup_page_==1) lv_obj_remove_flag(color_panel_,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(color_panel_,LV_OBJ_FLAG_HIDDEN);
 if(setup_page_==2) lv_obj_remove_flag(travel_panel_,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(travel_panel_,LV_OBJ_FLAG_HIDDEN);
 if(setup_draft_.travel) lv_obj_add_state(travel_toggle_,LV_STATE_CHECKED); else lv_obj_remove_state(travel_toggle_,LV_STATE_CHECKED);
 lv_obj_set_style_bg_color(travel_toggle_,lv_color_hex(setup_draft_.accent),uint32_t(LV_PART_INDICATOR)|uint32_t(LV_STATE_CHECKED));
 lv_obj_set_style_text_color(travel_title_,lv_color_hex(setup_draft_.accent),0);
 lv_label_set_text(travel_status_,setup_draft_.travel?"ON":"OFF");
}
void Ui::update_clock_tempo(bool force) {
 if(!clock_tempo_row_) return;
 if(!setup_open_ || setup_page_!=0 || !setup_draft_.receive_clock) {
   lv_obj_add_flag(clock_tempo_row_,LV_OBJ_FLAG_HIDDEN); return;
 }
 if(lv_obj_has_flag(clock_tempo_row_,LV_OBJ_FLAG_HIDDEN)) {
   lv_obj_remove_flag(clock_tempo_row_,LV_OBJ_FLAG_HIDDEN); force=true;
 }
 const uint32_t now=lv_tick_get();
 if(!force && uint32_t(now-clock_tempo_updated_)<100) return;
 clock_tempo_updated_=now;
 const auto state=arp_.state();
 const int bpm=state.clock_present?state.clock_bpm:0;
 if(force || bpm!=clock_tempo_display_) {
   clock_tempo_display_=bpm;
   char value[16]; if(bpm>0) std::snprintf(value,sizeof(value),"%d",bpm); else std::snprintf(value,sizeof(value),"--");
   lv_label_set_text(clock_tempo_value_,value);
   lv_obj_update_layout(clock_tempo_row_);
   lv_obj_align(clock_tempo_row_,LV_ALIGN_TOP_MID,0,393);
 }
}
void Ui::set_setup_page(int page) {
 constexpr int page_count=5;
 setup_page_=(page%page_count+page_count)%page_count;
 close_slot_dialog();
 if(setup_page_==4) {
   auto& s=selectors_[22]; lv_anim_delete(&s,nullptr); s.input=nullptr; s.target=slot_index_*s.step; set_selector_offset(22,s.target); update_slot();
 }
 // Build this static bitmap only once, on the first visit to the color page.
 if(setup_page_==1 && color_pixels_.empty()) {
   color_pixels_.resize(466*466);
   for(int y=0;y<466;++y) for(int x=0;x<466;++x) {
     const float dx=x-232.5f,dy=y-232.5f,r=std::hypot(dx,dy);
     const float coverage=std::clamp(std::min(r-color_inner,color_outer-r)+.5f,0.f,1.f);
     if(coverage<=0) continue;
     const int hue=int(std::lround(std::atan2(dy,dx)*180/pi+270));
     color_pixels_[y*466+x]=(uint32_t(coverage*255)<<24)|accent_for_hue(hue);
   }
   color_image_.header.magic=LV_IMAGE_HEADER_MAGIC; color_image_.header.cf=LV_COLOR_FORMAT_ARGB8888;
   color_image_.header.w=color_image_.header.h=466; color_image_.header.stride=466*4;
   color_image_.data_size=uint32_t(color_pixels_.size()*4); color_image_.data=reinterpret_cast<const uint8_t*>(color_pixels_.data());
 }
 update_setup();
}
void Ui::update_slot() {
 MusicPreset value;
 for(int i=0;i<music_slot_count;++i) slot_names_[i]=app_.load_slot(i,value)?music_slot_name(value):"---";
 slot_occupied_=app_.load_slot(slot_index_,value);
 lv_obj_invalidate(selector_views_[22]);
 lv_obj_invalidate(slot_delete_);
 if(slot_occupied_) lv_obj_remove_state(slot_delete_,LV_STATE_DISABLED); else lv_obj_add_state(slot_delete_,LV_STATE_DISABLED);
 lv_label_set_text(setup_error_,"");
 if(slot_occupied_) lv_obj_remove_state(slot_load_,LV_STATE_DISABLED); else lv_obj_add_state(slot_load_,LV_STATE_DISABLED);
 lv_obj_set_style_opa(slot_load_,slot_occupied_?255:90,0);
}
void Ui::close_slot_dialog() {
 slot_dialog_=0;
 for(int column=23;column<31;++column) { lv_anim_delete(&selectors_[column],nullptr); selectors_[column].input=nullptr; }
 if(slot_dialog_panel_) lv_obj_add_flag(slot_dialog_panel_,LV_OBJ_FLAG_HIDDEN);
}
void Ui::slot_action(int action) {
 const bool save=action==1;
 if(!setup_open_ || setup_page_!=4 || power_off_visible_ || slot_dialog_ || selectors_[22].input) return;
 MusicPreset stored; const bool occupied=app_.load_slot(slot_index_,stored);
 if(!save && !occupied) { update_slot(); return; }
 slot_draft_=save?app_.music_preset(arp_.preset(arp_page_),note_.octave()):stored;
 if(save){slot_draft_.kinetic=kinetic_settings();if(slot_draft_.global_scale)slot_draft_.kinetic.scale=slot_draft_.keyboard;}
 if(save && occupied) slot_draft_.name=stored.name;
 slot_dialog_=action;
 lv_label_set_text(slot_confirm_text_,save?"SAVE":action==3?"DELETE":"LOAD");
 lv_label_set_text(slot_dialog_name_,music_slot_name(slot_draft_).c_str());
 if(save) lv_obj_add_flag(slot_dialog_name_,LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(slot_dialog_name_,LV_OBJ_FLAG_HIDDEN);
 for(int i=0;i<8;++i) {
   for(auto* object:{slot_letter_views_[i],slot_dialog_frames_[i].object}) {
     if(save) lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
   }
   auto& state=selectors_[23+i]; lv_anim_delete(&state,nullptr); state.input=nullptr;
   const char* character=std::strchr(slot_characters,slot_draft_.name[i]);
   state.target=int(character-slot_characters)*state.step; set_selector_offset(23+i,state.target);
 }
 lv_label_set_text(setup_error_,""); lv_obj_remove_flag(slot_dialog_panel_,LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(slot_dialog_panel_); lv_obj_move_foreground(setup_error_);
}
void Ui::confirm_slot_dialog() {
 if(!setup_open_ || !slot_dialog_ || power_off_visible_) return;
 for(int c=23;c<31;++c) if(selectors_[c].input) return;
 if(slot_dialog_==1) {
   int target=slot_index_;
   for(int i=0;i<slot_index_;++i) { MusicPreset p; if(!app_.load_slot(i,p)) { target=i; break; } }
   if(!app_.save_slot(target,slot_draft_)) { lv_label_set_text(setup_error_,"Saving failed - try again"); return; }
   slot_index_=target;
   auto& s=selectors_[22]; lv_anim_delete(&s,nullptr); s.target=slot_index_*s.step; set_selector_offset(22,s.target);
   close_slot_dialog(); update_slot();
 } else if(slot_dialog_==3) {
   if(!app_.delete_slot(slot_index_)) { lv_label_set_text(setup_error_,"Deleting failed - try again"); return; }
   close_slot_dialog(); update_slot();
 } else {
   reset_midi_session();
   if(!app_.apply_music(slot_draft_)) { lv_label_set_text(setup_error_,"Loading failed - try again"); return; }
   const auto& value=slot_draft_;
   restore_kinetic(value.kinetic);
   arp_.restore(value.arpeggiator); arp_.enable(arp_mode_); arp_page_=value.arpeggiator.page;
   arp_saved_=value.arpeggiator; arp_save_ready_=true; arp_save_failed_=false;
   note_.selection(app_.current); note_.set_octave(value.octave); chord_.selection(app_.chords);
   setup_draft_.global_scale=value.global_scale;
   sync_arp_clock(); sync_arp_pause(); refresh(); close_slot_dialog(); update_slot(); update_setup();
 }
}
void Ui::slot_dialog_event(lv_event_t* e) {
 auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
 if(lv_event_get_target_obj(e)==self->slot_cancel_) { self->close_slot_dialog(); lv_label_set_text(self->setup_error_,""); }
 else self->confirm_slot_dialog();
}
void Ui::slot_event(lv_event_t* e) {
 auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
 if(self->setup_gesture_.moved || self->slot_dialog_) return;
 self->slot_action(lv_event_get_target_obj(e)==self->slot_delete_?3:lv_event_get_target_obj(e)==self->slot_save_?1:2);
}
void Ui::set_setup_color(lv_point_t p) {
 const float dx=float(p.x-233),dy=float(p.y-233);
 if(std::hypot(dx,dy)<150) return;
 const auto value=accent_for_hue(int(std::lround(std::atan2(dy,dx)*180/pi+270)));
 if(value!=setup_draft_.accent) { setup_draft_.accent=value; update_setup(); }
}
void Ui::setup_event(lv_event_t* e) {
 auto* self=static_cast<Ui*>(lv_event_get_user_data(e)); if(!self->setup_open_ || self->power_off_visible_ || self->slot_dialog_) return;
 const auto code=lv_event_get_code(e);
 if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_PRESSING && code!=LV_EVENT_RELEASED && code!=LV_EVENT_PRESS_LOST) return;
 auto* input=lv_indev_active(); if(!input) return;
 auto& g=self->setup_gesture_;
 if(g.input && g.input!=input) return;
 lv_point_t p; lv_indev_get_point(input,&p);
 if(code==LV_EVENT_PRESSED) {
   const float radius=std::hypot(float(p.x-233),float(p.y-233));
   g={input,p,self->setup_page_==1 && radius>=color_inner-10 && radius<=color_outer+10,false};
   lv_area_t channel_bounds; lv_obj_get_coords(self->channel_panel_,&channel_bounds);
   // Gesture ownership is fixed at contact start, even outside the wheel later.
   g.channel=self->setup_page_==0 && p.x>=channel_bounds.x1 && p.x<=channel_bounds.x2 &&
     p.y>=channel_bounds.y1 && p.y<=channel_bounds.y2;
   if(self->setup_page_==0) {
     lv_area_t bounds; lv_obj_get_coords(self->clock_toggle_,&bounds);
     if(p.x>=bounds.x1 && p.x<=bounds.x2 && p.y>=bounds.y1 && p.y<=bounds.y2) g.channel=true;
     for(auto* button:self->midi_route_buttons_) {
       lv_obj_get_coords(button,&bounds);
       if(p.x>=bounds.x1 && p.x<=bounds.x2 && p.y>=bounds.y1 && p.y<=bounds.y2) g.channel=true;
     }
   }
   if(self->setup_page_==4) for(auto* object:{self->selector_views_[22],self->slot_save_,self->slot_load_,self->slot_delete_}) {
     lv_area_t bounds; lv_obj_get_coords(object,&bounds);
     if(p.x>=bounds.x1 && p.x<=bounds.x2 && p.y>=bounds.y1 && p.y<=bounds.y2) g.channel=true;
   }
   if(self->setup_page_==3) {
     lv_area_t bounds; lv_obj_get_coords(self->global_toggle_,&bounds);
     if(p.x>=bounds.x1 && p.x<=bounds.x2 && p.y>=bounds.y1 && p.y<=bounds.y2) g.channel=true;
   }
   if(self->setup_page_==2) {
     lv_area_t toggle; lv_obj_get_coords(self->travel_toggle_,&toggle);
     g.channel=p.x>=toggle.x1 && p.x<=toggle.x2 && p.y>=toggle.y1 && p.y<=toggle.y2;
     for(int column=3;column<6;++column) {
       lv_area_t bounds; lv_obj_get_coords(self->selector_views_[column],&bounds);
       if(p.x>=bounds.x1 && p.x<=bounds.x2 && p.y>=bounds.y1 && p.y<=bounds.y2) g.channel=true;
     }
   }
   if(g.color) self->set_setup_color(p);
 } else if(g.input==input) {
   const int dx=p.x-g.start.x,dy=p.y-g.start.y;
   if(std::abs(dx)>12 || std::abs(dy)>12) g.moved=true;
   if(code==LV_EVENT_PRESSING && g.color) self->set_setup_color(p);
   if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) {
     if(code==LV_EVENT_RELEASED && !g.color && !g.channel && std::abs(dx)>=48 && std::abs(dx)*10>std::abs(dy)*13)
       self->set_setup_page(self->setup_page_+(dx<0?1:-1));
     g.completed=code==LV_EVENT_RELEASED?input:nullptr; g.input=nullptr;
   }
 }
}
void Ui::open_setup() {
 if(setup_open_ || shutting_down_ || power_off_visible_) return;
 settle_arp_selectors();
 setup_open_=true;
 arp_.suspend(true);
 reset_midi_session();
 if(app_.selecting) back();
 setup_draft_=app_.setup; setup_open_=true; setup_gesture_={};
 sync_arp_pause();
 sync_travel();
 auto& s=selectors_[2]; lv_anim_delete(&s,nullptr); s.input=nullptr;
 s.target=(setup_draft_.channel-1)*s.step; set_selector_offset(2,s.target); set_setup_page(setup_page_);
 lv_label_set_text(setup_error_,output_ && app_.setup.travel && !output_->ready()?"Audio unavailable - disable Travel Mode":"");
 lv_obj_add_flag(wheel_,LV_OBJ_FLAG_HIDDEN);
 lv_obj_remove_flag(setup_,LV_OBJ_FLAG_HIDDEN);
}
void Ui::cancel_setup() {
 if(!setup_open_ || power_off_visible_) return;
 close_slot_dialog();
 // A finger left on the outer color ring must not become a keyboard note.
 reset_midi_session();
 for(int column:{2,3,4,5,22}) { lv_anim_delete(&selectors_[column],nullptr); selectors_[column].input=nullptr; }
 setup_open_=false; setup_gesture_={}; lv_obj_add_flag(setup_,LV_OBJ_FLAG_HIDDEN);
 lv_obj_remove_flag(wheel_,LV_OBJ_FLAG_HIDDEN);
 sync_arp_clock();
 sync_arp_pause();
}
void Ui::recolor_scale_menu() {
 for(auto& frame:menu_frames_) {
   for(auto& pixel:frame.pixels) pixel=(pixel&0xff000000u)|app_.setup.accent;
   lv_image_cache_drop(&frame.image); lv_obj_invalidate(frame.object);
 }
 lv_obj_set_style_text_color(scale_title_,lv_color_hex(app_.setup.accent),0);
 lv_obj_set_style_text_color(chord_edit_title_,lv_color_hex(app_.setup.accent),0);
}
bool Ui::confirm_setup() {
 if(slot_dialog_) { close_slot_dialog(); lv_label_set_text(setup_error_,""); return false; }
 if(!setup_open_ || !valid_setup(setup_draft_) || power_off_visible_) return false;
 for(int column:{2,3,4,5,22}) if(selectors_[column].input) {
   animate_selector(column,selectors_[column].offset); selectors_[column].input=nullptr;
 }
 const auto previous=app_.setup;
 if(output_ && !output_->configure(setup_draft_)) {
   output_->configure(previous); lv_label_set_text(setup_error_,"Audio unavailable - try again"); return false;
 }
 if(setup_draft_.channel!=previous.channel && !note_.channel(setup_draft_.channel)) {
   if(output_) output_->configure(previous);
   lv_label_set_text(setup_error_,"Release notes and try again"); return false;
 }
 const auto global_source=bounce_scale();
 if(!app_.save_setup(setup_draft_,chord_mode_,bounce_mode_?&global_source:nullptr)) {
   if(output_) output_->configure(previous);
   if(setup_draft_.channel!=previous.channel) note_.channel(previous.channel);
   lv_label_set_text(setup_error_,"Saving failed - try again"); return false;
 }
 cancel_setup(); recolor_scale_menu(); refresh(); return true;
}
void Ui::power_button(ButtonEvent event) {
 if(booting()) { power_pressed_=false; power_held_=false; return; }
 if(event==ButtonEvent::Cancel) {
   power_pressed_=false; power_held_=false; power_context_=PowerContext::Play;return;
 }
 if(power_off_visible_ || shutting_down_) { power_pressed_=false; power_held_=true; return; }
 if(event==ButtonEvent::Press) {
   if(power_pressed_) return;
   power_pressed_=true; power_held_=false;
   power_context_=setup_open_?(slot_dialog_?PowerContext::SlotDialog:PowerContext::Setup):app_.selecting?PowerContext::Selection:is_bounce_menu()?PowerContext::KineticMenu:PowerContext::Play;
 } else if(event==ButtonEvent::Hold) {
   if(!power_pressed_ || power_held_) return;
   power_held_=true;if(power_context_!=PowerContext::SlotDialog)open_setup();
 } else if(event==ButtonEvent::Release) {
   const bool short_press=power_pressed_ && !power_held_;
   const auto context=power_context_;
   power_pressed_=false; power_held_=false;power_context_=PowerContext::Play;
   if(!short_press)return;
   // Act only on the menu where this press began. A touch confirmation or
   // cancellation between Press and Release must not also change play mode.
   switch(context){
   case PowerContext::Selection:if(app_.selecting && !setup_open_)confirm();break;
   case PowerContext::Setup:if(setup_open_ && !slot_dialog_)confirm_setup();break;
   case PowerContext::SlotDialog:break; // Only explicit touch buttons act here.
   case PowerContext::KineticMenu:if(is_bounce_menu() && !setup_open_)toggle_bounce_menu();break;
   case PowerContext::Play:if(!setup_open_ && !app_.selecting && !is_bounce_menu())toggle_arpeggiator();break;
   }
 }
}
}

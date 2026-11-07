#include "ui.hpp"
#include "selector_mask.hpp"
#include "ui_fonts.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace sm {
namespace {
std::pair<int,int> ink_bounds(const char* text,const lv_font_t* font) {
 int top=font->line_height,bottom=0;
 for(const char* p=text;*p;++p) {
   lv_font_glyph_dsc_t glyph{};
   if(!lv_font_get_glyph_dsc(font,&glyph,uint8_t(*p),0) || !glyph.box_h) continue;
   const int y=font->line_height-font->base_line-glyph.box_h-glyph.ofs_y;
   top=std::min(top,y); bottom=std::max(bottom,y+int(glyph.box_h));
 }
 return {top,bottom};
}
lv_obj_t* label(lv_obj_t* parent,const char* value,int x,int y,int width,const lv_font_t* font) {
 auto* text=lv_label_create(parent); lv_label_set_text(text,value); lv_obj_set_pos(text,x,y); lv_obj_set_width(text,width);
 lv_obj_set_style_text_font(text,font,0); lv_obj_set_style_text_color(text,lv_color_hex(0xffffff),0);
 lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_CENTER,0); lv_obj_remove_flag(text,LV_OBJ_FLAG_CLICKABLE); return text;
}
}
void Ui::build_travel() {
 build_waveforms();
 travel_panel_=lv_obj_create(setup_); lv_obj_remove_style_all(travel_panel_); lv_obj_set_size(travel_panel_,466,466);
 lv_obj_remove_flag(travel_panel_,LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(travel_panel_,LV_OBJ_FLAG_EVENT_BUBBLE);
 travel_title_=label(travel_panel_,"TRAVEL MODE",43,48,380,&barlow_43);
 travel_toggle_=lv_switch_create(travel_panel_); lv_obj_set_pos(travel_toggle_,183,103); lv_obj_set_size(travel_toggle_,100,44);
 lv_obj_set_style_bg_color(travel_toggle_,lv_color_hex(0x303030),LV_PART_MAIN);
 lv_obj_set_style_bg_color(travel_toggle_,lv_color_hex(0xffffff),LV_PART_KNOB);
 lv_obj_add_flag(travel_toggle_,LV_OBJ_FLAG_EVENT_BUBBLE);
 lv_obj_add_event_cb(travel_toggle_,travel_event,LV_EVENT_VALUE_CHANGED,this);
 travel_status_=label(travel_panel_,"OFF",293,109,60,&barlow_24);
 const char* captions[]={"WAVEFORM","ATTACK","DECAY"};
 for(int row=0;row<3;++row) {
   const int x=50+124*row,column=3+row;
   auto* caption=label(travel_panel_,captions[row],x,154,118,&barlow_24);
   lv_label_set_long_mode(caption,LV_LABEL_LONG_CLIP);
   auto* view=selector_views_[column]=lv_obj_create(travel_panel_);
   lv_obj_remove_style_all(view); lv_obj_set_pos(view,x,190); lv_obj_set_size(view,118,180);
   lv_obj_remove_flag(view,LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(view,LV_OBJ_FLAG_CLICKABLE);
   lv_obj_add_flag(view,LV_OBJ_FLAG_EVENT_BUBBLE);
   auto& state=selectors_[column]; state.owner=this; state.column=column;
   state.center=280; state.step=60; state.half_height=90; state.font_size=28;
   lv_obj_set_style_bg_color(view,lv_color_hex(0),0); lv_obj_set_style_bg_opa(view,255,0);
   lv_obj_add_event_cb(view,selection_event,LV_EVENT_ALL,this);
   lv_obj_add_event_cb(view,[](lv_event_t* e) {
     auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
     for(int c=3;c<6;++c) if(lv_event_get_target_obj(e)==self->selector_views_[c]) self->draw_selector(lv_event_get_layer(e),c);
   },LV_EVENT_DRAW_MAIN,this);
   for(int bottom=0;bottom<2;++bottom) {
     auto* mask=lv_obj_create(view); lv_obj_remove_style_all(mask); lv_obj_remove_flag(mask,LV_OBJ_FLAG_CLICKABLE);
     lv_obj_remove_flag(mask,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_pos(mask,0,bottom?150:0); lv_obj_set_size(mask,118,30);
     lv_obj_set_style_bg_color(mask,lv_color_hex(0),0); lv_obj_set_style_bg_grad_color(mask,lv_color_hex(0),0);
     lv_obj_set_style_bg_opa(mask,255,0); lv_obj_set_style_bg_grad_dir(mask,LV_GRAD_DIR_VER,0);
     lv_obj_set_style_bg_main_opa(mask,bottom?0:255,0); lv_obj_set_style_bg_grad_opa(mask,bottom?255:0,0);
     selector_mask_never_covers(mask);
   }
   add_menu_frame(size_t(4+row),x,252,118,56,row==0?28:6,row==2?28:6,travel_panel_);
 }
 // Center the switch between visible ink, not unequal font line-height boxes.
 const auto title_ink=ink_bounds("TRAVEL MODE",&barlow_43);
 const auto captions_ink=ink_bounds("WAVEFORM ATTACK DECAY",&barlow_24);
 const int center=(48+title_ink.second+154+captions_ink.first)/2;
 lv_obj_set_y(travel_toggle_,center-22);
 const auto status_ink=ink_bounds("ON OFF",&barlow_24);
 lv_obj_set_y(travel_status_,center-(status_ink.first+status_ink.second)/2);
 sync_travel();
 lv_obj_add_flag(travel_panel_,LV_OBJ_FLAG_HIDDEN);
}
void Ui::build_waveforms() {
 // Small procedural A8 glyphs at native sizes. Rounded segment-distance joins
 // avoid LVGL 9.2 line-cap artifacts, without transforms or background images.
 size_t bytes=0;
 for(int size=0;size<4;++size) bytes+=size_t(70+6*size)*(26+2*size)*waveform_count;
 waveform_pixels_.resize(bytes); size_t offset=0;
 for(int wave=0;wave<waveform_count;++wave) for(int size=0;size<4;++size) {
   const int width=70+6*size,height=26+2*size;
   const float left=3,right=float(width-4),top=3,bottom=float(height-4),mid=(left+right)/2;
   struct Point { float x,y; }; std::array<Point,65> points{}; int count=0;
   auto add=[&](float x,float y) { points[count++]={x,y}; };
   if(wave==0) { // Two complete periods, including both falling transitions.
     add(left,bottom); add(mid,top); add(mid,bottom); add(right,top); add(right,bottom);
   } else if(wave==1 || wave==3) {
     add(left,bottom);
     for(int cycle=0;cycle<2;++cycle) {
       const float start=left+(right-left)*cycle/2;
       const float duty=wave==3?.25f:.5f;
       const float edge=start+(right-left)*.5f*duty;
       add(start,top); add(edge,top);
       add(edge,bottom); add(start+(right-left)/2,bottom);
     }
   } else if(wave==2) {
     add(left,bottom); add(left+(right-left)*.25f,top); add(mid,bottom);
     add(left+(right-left)*.75f,top); add(right,bottom);
   } else for(int sample=0;sample<=64;++sample) {
     const float t=float(sample)/64;
     add(left+(right-left)*t,(top+bottom)/2-(bottom-top)*.5f*std::sin(12.56637061436f*t));
   }
   auto* pixels=waveform_pixels_.data()+offset;
   for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
     float distance2=std::numeric_limits<float>::max();
     for(int i=1;i<count;++i) {
       const auto a=points[i-1],b=points[i];
       const float dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
       if(length2==0) continue;
       const float t=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/length2,0.f,1.f);
       const float ex=x-a.x-t*dx,ey=y-a.y-t*dy;
       distance2=std::min(distance2,ex*ex+ey*ey);
     }
     pixels[y*width+x]=uint8_t(std::clamp(1.5f-std::sqrt(distance2),0.f,1.f)*255+.5f);
   }
   auto& image=waveform_images_[wave*4+size];
   image.header.magic=LV_IMAGE_HEADER_MAGIC; image.header.cf=LV_COLOR_FORMAT_A8;
   image.header.w=width; image.header.h=height; image.header.stride=width;
   image.data=pixels; image.data_size=width*height; offset+=image.data_size;
 }
}
void Ui::travel_event(lv_event_t* event) {
 auto* self=static_cast<Ui*>(lv_event_get_user_data(event));
 if(!self->setup_open_ || self->setup_page_!=2 || self->power_off_visible_) return;
 self->setup_draft_.travel=lv_obj_has_state(self->travel_toggle_,LV_STATE_CHECKED);
 self->update_setup();
}
void Ui::sync_travel() {
 const auto attack=std::lower_bound(synth_attacks.begin(),synth_attacks.end(),setup_draft_.synth.attack_ms);
 const auto decay=std::lower_bound(synth_decays.begin(),synth_decays.end(),setup_draft_.synth.decay_ms);
 const int options[]={int(setup_draft_.synth.waveform),int(attack-synth_attacks.begin()),int(decay-synth_decays.begin())};
 for(int column=3;column<6;++column) {
   auto& state=selectors_[column]; lv_anim_delete(&state,nullptr); state.input=nullptr;
   state.target=std::clamp(options[column-3],0,selector_count(column)-1)*state.step;
   set_selector_offset(column,state.target);
 }
}
}

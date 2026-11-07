#include "ui.hpp"
#include "bounce_mode.hpp"
#include "selector_mask.hpp"
#include "ui_fonts.hpp"
#include "selector_font.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
namespace sm {
namespace {
constexpr float pi=3.14159265358979323846f;
constexpr float segment_gap=7.5f;
constexpr float segment_outline=1.35f;
constexpr float segment_corner_radius=4.f;
constexpr int selector_root_width=74,selector_mode_width=183,selector_gap=5;
constexpr int selector_left=(466-selector_root_width-selector_gap-selector_mode_width)/2;
constexpr int selector_mode_left=selector_left+selector_root_width+selector_gap;
constexpr std::array<const char*,12> menu_modes{{"Major","Minor","Harm Min","Mel Min","Dorian",
 "Phrygian","Lydian","Mixo","Locrian","Maj Pent","Min Pent","Blues"}};
constexpr std::array<const char*,16> menu_channels{{"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"}};
constexpr std::array<const char*,10> attack_labels{{"0 ms","5 ms","10 ms","20 ms","50 ms","100 ms","200 ms","500 ms","1000 ms","2000 ms"}};
constexpr std::array<const char*,10> decay_labels{{"10 ms","25 ms","50 ms","100 ms","200 ms","400 ms","800 ms","1200 ms","2000 ms","4000 ms"}};
lv_color_t color(unsigned c) { return lv_color_hex(c); }
lv_point_precise_t point(float radius,float angle) { float a=angle*pi/180; return {lv_value_precise_t(233+radius*std::cos(a)),lv_value_precise_t(233+radius*std::sin(a))}; }
// Base half-planes keep opposite sides of each gap exactly parallel.
// The visual outline rounds the four corners; the generous hit area is retained.
float segment_distance(float x,float y,float radius,int i,int count,float inner) {
 float step=2*pi/count, a=-pi/2+i*step-step/2, b=a+step;
 float left=-std::sin(a)*x+std::cos(a)*y-segment_gap/2;
 float right=std::sin(b)*x-std::cos(b)*y-segment_gap/2;
 return std::min({radius-inner,217.f-radius,left,right});
}
lv_obj_t* label(lv_obj_t* parent,const char* text,int x,int y,int w,const lv_font_t* font,lv_color_t c) {
 auto* o=lv_label_create(parent); lv_label_set_text(o,text); lv_obj_set_width(o,w); lv_obj_set_pos(o,x,y);
 lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,c,0);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE); return o;
}
void plain(lv_obj_t* obj) { lv_obj_remove_style_all(obj); lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE); }
}
void Ui::add_menu_frame(size_t index,int x,int y,int width,int height,float left_radius,float right_radius,lv_obj_t* parent) {
 make_menu_frame(menu_frames_[index],x,y,width,height,left_radius,right_radius,parent?parent:selection_,accent(.5f));
}
void Ui::make_menu_frame(MenuFrame& frame,int x,int y,int width,int height,float left_radius,float right_radius,lv_obj_t* parent,lv_color_t tint) {
 frame.pixels.resize(width*height);
 for(int py=0;py<height;++py) for(int px=0;px<width;++px) {
   float dx=px+.5f-width/2.f,dy=py+.5f-height/2.f;
   float radius=dx<0?left_radius:right_radius;
   float qx=std::abs(dx)-(width/2.f-radius),qy=std::abs(dy)-(height/2.f-radius);
   float distance=radius-std::hypot(std::max(qx,0.f),std::max(qy,0.f))-std::min(std::max(qx,qy),0.f);
   float coverage=std::clamp(distance+.5f,0.f,1.f)*std::clamp(segment_outline+.5f-distance,0.f,1.f);
   auto pixel=tint;
   frame.pixels[py*width+px]=(uint32_t(coverage*255)<<24)|(uint32_t(pixel.red)<<16)|(uint32_t(pixel.green)<<8)|pixel.blue;
 }
 frame.image.header.magic=LV_IMAGE_HEADER_MAGIC; frame.image.header.cf=LV_COLOR_FORMAT_ARGB8888;
 frame.image.header.w=width; frame.image.header.h=height; frame.image.header.stride=width*4;
 frame.image.data_size=uint32_t(frame.pixels.size()*4); frame.image.data=reinterpret_cast<const uint8_t*>(frame.pixels.data());
 auto* image=lv_obj_create(parent); frame.object=image; plain(image); lv_obj_set_size(image,width,height); lv_obj_set_pos(image,x,y);
 lv_obj_add_event_cb(image,[](lv_event_t* e) {
   lv_draw_image_dsc_t d; lv_draw_image_dsc_init(&d); d.src=lv_event_get_user_data(e);
   lv_area_t bounds; lv_obj_get_coords(lv_event_get_target_obj(e),&bounds);
   d.image_area=bounds;
   lv_draw_image(lv_event_get_layer(e),&d,&bounds);
 },LV_EVENT_DRAW_MAIN,&frame.image);
 lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE);
}
lv_color_t Ui::accent(float x) const { return lv_color_mix(color(palette().accent_b),color(palette().accent_a),uint8_t(std::clamp(x,0.f,1.f)*255)); }
int Ui::hit(int x,int y,bool include_unplayable) const {
 float dx=float(x-233),dy=float(y-233),r=std::sqrt(dx*dx+dy*dy);
 if(r<(arp_mode_?156:91)) return -2;
 if(r<(arp_mode_?163:111) || r>233) return -1;
 int count=wheel_count();
 float step=2*pi/count;
 int i=int(std::floor((std::atan2(dy,dx)+pi/2+step/2+2*pi)/step))%count;
 // Extend only the invisible outer hit area; keep radial gaps, inner contour
 // and rendered geometry unchanged. Nothing outside the round display plays.
 return segment_distance(dx,dy,std::min(r,217.f),i,count,arp_mode_?164.f:112.f)>=0 && (include_unplayable || wheel_playable(i))?i:-1;
}
void Ui::release_segment(int source) {
 const int degree=contacts_[source].pressed;
 if(degree>=0) {
   if(chord_mode_) chord_.release(source);
   else if(!arp_mode_) note_.release(source);
   contacts_[source].pressed=-1;
   bool still_held=false;
   for(const auto& contact:contacts_) if(contact.pressed==degree) still_held=true;
   if(!still_held && !arp_mode_) { fading_[degree]=true; release_time_[degree]=lv_tick_get(); invalidate_segment(degree); }
 }
}
void Ui::release_contact(int source) { release_segment(source); contacts_[source]=Contact{}; }
void Ui::cancel_touch() {
 cancel_bounce_contacts();
 chord_.stop();
 release_arp_tap();
 for(int i=0;i<int(contacts_.size());++i) release_contact(i);
 tap_pending_=false;
}
void Ui::reset_midi_session() {
 motion_cc_.invalidate();
 stop_bounce_output();
 arp_.suspend(true);
 // Do not adopt a finger still resting on the screen as a fresh note after reconnect.
 for(const auto& contact:contacts_) if(contact.input) {
   bool known=false; for(auto* blocked:blocked_inputs_) if(blocked==contact.input) known=true;
   if(!known) for(auto& blocked:blocked_inputs_) if(!blocked) { blocked=contact.input; break; }
 }
 for(auto* input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input)) {
   if(lv_indev_get_type(input)!=LV_INDEV_TYPE_POINTER || lv_indev_get_state(input)!=LV_INDEV_STATE_PRESSED) continue;
   bool known=false; for(auto* blocked:blocked_inputs_) if(blocked==input) known=true;
   if(!known) for(auto& blocked:blocked_inputs_) if(!blocked) { blocked=input; break; }
 }
 cancel_touch(); note_.panic(); sync_arp_pause();
}
void Ui::invalidate_segment(int index) {
 if(!wheel_ || index<0 || index>=13) return;
 if(geometry_count_!=wheel_count()) { lv_obj_invalidate(wheel_); return; }
 if(!segment_pixels_[index].empty()) lv_obj_invalidate_area(wheel_,&segment_bounds_[index]);
}
void Ui::update_feedback() {
 update_motion_feedback();
 update_bounce();
 if(power_off_visible_) poll_power_off_contacts();
 chord_.retry();
 update_clock_tempo();
 if(arp_mode_) {
   const auto state=arp_.state();
   int active=-1;
   const auto& intervals=modes()[app_.current.mode].intervals;
   for(size_t i=0;i<intervals.size();++i) if(state.playing>=0 && (state.playing-app_.current.root+1200)%12==intervals[i]%12) active=int(i);
   if(active!=arp_display_note_) {
     if(arp_display_note_>=0) { fading_[arp_display_note_]=true; release_time_[arp_display_note_]=lv_tick_get(); }
     if(active>=0) { fading_[active]=false; highlight_[active]=255; invalidate_segment(active); }
     arp_display_note_=active;
   }
   for(int i=0;i<int(intervals.size());++i) if((state.selected^arp_display_selected_)&(1u<<i)) invalidate_segment(i);
   arp_display_selected_=state.selected;
   update_arp();
 }
 const uint32_t now=lv_tick_get();
 for(size_t i=0;i<fading_.size();++i) if(fading_[i]) {
   const uint32_t elapsed=uint32_t(now-release_time_[i]);
   float t=1-std::min<uint32_t>(elapsed,500u)/500.f;
   auto level=uint8_t(std::round(255*t*t*(3-2*t)));
   if(level!=highlight_[i]) { highlight_[i]=level; if(!app_.selecting) invalidate_segment(int(i)); }
   if(elapsed>=500) fading_[i]=false;
 }
 persist_arpeggiator();
}
void Ui::render_wheel() {
 const int count=wheel_count();
 uint16_t selected=arp_mode_?arp_.state().selected:0;
 if(chord_mode_) for(int pad=0;pad<chord_pad_count;++pad)
   if(app_.chords.variants[pad]!=ChordVariant::Triad || app_.chords.octave_shifts[pad]!=0) selected|=1u<<pad;
 uint16_t playable=0; for(int i=0;i<count;++i) if(wheel_playable(i)) playable|=1u<<i;
 if(count==rendered_count_ && playable==rendered_playable_ && selected==rendered_selected_ && highlight_==rendered_highlight_) return;
 // Cache geometry, antialias coverage and radial tint together. The thin Arp
 // ring is rebuilt on mode changes, even when the segment count stays the same.
 // Never run atan2/sqrt/trigonometry for each pixel during a press/fade.
 if(geometry_count_!=count) {
   wheel_pixels_.assign(466*466,0);
   for(int i=0;i<13;++i) {
     segment_pixels_[i].clear();
     if(i<count) segment_pixels_[i].reserve(110000/count+512);
     segment_bounds_[i]={465,465,0,0};
   }
   const float step=2*pi/count;
   const float inner=arp_mode_?164.f:112.f;
   // The preview's 4 px radius describes the stroke centreline. Our renderer
   // measures inwards from the outer edge of the stroke instead.
   constexpr float fillet=segment_corner_radius+segment_outline/2;
   struct Corner { float x,y,ax,ay,bx,by; };
   std::array<std::array<Corner,4>,13> corners{};
   std::array<float,13> sin_a{},cos_a{},sin_b{},cos_b{};
   for(int i=0;i<count;++i) {
     float a=-pi/2+i*step-step/2,b=a+step;
     sin_a[i]=std::sin(a); cos_a[i]=std::cos(a); sin_b[i]=std::sin(b); cos_b[i]=std::cos(b);
     const auto corner=[&](float angle,int side,bool outer) {
       const float ux=std::cos(angle),uy=std::sin(angle),nx=-side*uy,ny=side*ux;
       const float radius=outer?217.f-fillet:inner+fillet;
       const float height=segment_gap/2+fillet,along=std::sqrt(radius*radius-height*height);
       const float x=ux*along+nx*height,y=uy*along+ny*height;
       const float cx=(outer?1.f:-1.f)*x/radius,cy=(outer?1.f:-1.f)*y/radius;
       const bool line_first=outer?side==1:side==-1;
       return Corner{x,y,line_first?-nx:cx,line_first?-ny:cy,line_first?cx:-nx,line_first?cy:-ny};
     };
     corners[i]={corner(a,1,true),corner(b,-1,true),corner(b,-1,false),corner(a,1,false)};
   }
   for(int y=14;y<452;++y) for(int x=14;x<452;++x) {
     float dx=x-232.5f,dy=y-232.5f,r=std::sqrt(dx*dx+dy*dy);
     if(r<inner-2 || r>219) continue;
     int i=int(std::floor((std::atan2(dy,dx)+pi/2+step/2+2*pi)/step))%count;
     float d=std::min({r-inner,217.f-r,-sin_a[i]*dx+cos_a[i]*dy-segment_gap/2,
                      sin_b[i]*dx-cos_b[i]*dy-segment_gap/2});
     if(d<=-.5f) continue;
     for(const auto& c:corners[i]) {
       const float x=dx-c.x,y=dy-c.y;
       // Only the small sector between each pair of tangent normals uses
       // the fillet circle. Straight edges and concentric arcs stay exact.
       if(std::abs(x)<2*fillet && std::abs(y)<2*fillet &&
          c.ax*y-c.ay*x>=0 && x*c.by-y*c.bx>=0)
         d=std::min(d,fillet-std::sqrt(x*x+y*y));
     }
     if(d<=-.5f) continue;
     segment_pixels_[i].push_back({uint32_t(y*466+x),
       uint8_t(std::clamp(segment_outline+.5f-d,0.f,1.f)*255),uint8_t(std::clamp(d+.5f,0.f,1.f)*255),
       uint8_t(std::clamp((r-inner-segment_outline/2)/(217.f-inner-segment_outline),0.f,1.f)*255)});
     auto& bounds=segment_bounds_[i];
     bounds.x1=std::min(bounds.x1,int32_t(x)); bounds.y1=std::min(bounds.y1,int32_t(y));
     bounds.x2=std::max(bounds.x2,int32_t(x)); bounds.y2=std::max(bounds.y2,int32_t(y));
   }
   geometry_count_=count; rendered_count_=-1;
 }
 auto packed=[](lv_color_t c) { return (uint32_t(c.red)<<16)|(uint32_t(c.green)<<8)|c.blue; };
 const auto black=color(0);
 const bool keyboard=!arp_mode_ && !chord_mode_;
 std::array<lv_color_t,256> radial_tints{};
 if(keyboard) for(int radius=0;radius<256;++radius)
   radial_tints[radius]=lv_color_mix(color(0xffffff),accent(.5f),uint8_t(radius));
 for(int i=0;i<count;++i) {
   if(rendered_count_==count && (((rendered_playable_^playable)|(rendered_selected_^selected))&(1u<<i))==0 && rendered_highlight_[i]==highlight_[i]) continue;
   if(!wheel_playable(i)) { for(const auto& p:segment_pixels_[i]) wheel_pixels_[p.offset]=0; continue; }
   const float t=i*2*pi/count;
   const auto button_accent=accent(.5f+.75f*std::sin(t)-.5f*std::cos(t));
   const auto tint=(arp_mode_ || chord_mode_) && !(selected&(1u<<i))?color(0xffffff):button_accent;
   // A quiet, slightly desaturated wash; keep the accent outline unchanged.
   const auto feedback_tint=lv_color_mix(color(0xffffff),button_accent,72);
   const auto fill=lv_color_mix(feedback_tint,black,uint8_t(56*highlight_[i]/255.f));
   const auto fill_word=packed(fill);
   std::array<lv_color_t,256> outline_colors{};
   std::array<uint32_t,256> outline_words{};
   for(int alpha=0;alpha<256;++alpha) {
     outline_colors[alpha]=lv_color_mix(tint,fill,uint8_t(alpha));
     outline_words[alpha]=packed(outline_colors[alpha]);
   }
   for(const auto& p:segment_pixels_[i]) {
     if(p.outline==0 && p.coverage==255) { wheel_pixels_[p.offset]=fill_word; continue; }
     if(keyboard) {
       const auto stroke=lv_color_mix(radial_tints[p.radial],fill,p.outline);
       wheel_pixels_[p.offset]=packed(p.coverage==255?stroke:lv_color_mix(stroke,black,p.coverage));
     } else {
       wheel_pixels_[p.offset]=p.coverage==255?outline_words[p.outline]:packed(lv_color_mix(outline_colors[p.outline],black,p.coverage));
     }
   }
 }
 wheel_image_.header.magic=LV_IMAGE_HEADER_MAGIC;
 wheel_image_.header.cf=LV_COLOR_FORMAT_XRGB8888;
 wheel_image_.header.w=466; wheel_image_.header.h=466; wheel_image_.header.stride=466*4;
 wheel_image_.data_size=uint32_t(wheel_pixels_.size()*4);
 wheel_image_.data=reinterpret_cast<const uint8_t*>(wheel_pixels_.data());
 rendered_count_=count; rendered_playable_=playable; rendered_selected_=selected; rendered_highlight_=highlight_;
 lv_image_cache_drop(&wheel_image_);
}
void Ui::draw(lv_layer_t* layer) {
 if(bounce_mode_){draw_bounce(layer);return;}
 render_wheel();
 lv_draw_image_dsc_t d; lv_draw_image_dsc_init(&d); d.src=&wheel_image_;
 lv_area_t bounds{0,0,465,465}; lv_draw_image(layer,&d,&bounds);
}

void Ui::wheel_event(lv_event_t* e) {
 auto* self=static_cast<Ui*>(lv_event_get_user_data(e)); auto code=lv_event_get_code(e);
 if(code==LV_EVENT_DRAW_MAIN) self->draw(lv_event_get_layer(e)); else self->touch(code);
}
void Ui::touch(lv_event_code_t code) {
 if(app_.selecting || setup_open_ || shutting_down_ || power_off_visible_) return;
 if(bounce_mode_){bounce_touch(code);return;}
 if(arp_mode_) { arp_touch(code); return; }
 if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_PRESSING && code!=LV_EVENT_RELEASED && code!=LV_EVENT_PRESS_LOST) return;
 auto* input=lv_indev_active(); if(!input) return;
 for(auto& blocked:blocked_inputs_) if(blocked==input) {
   if(code==LV_EVENT_PRESSED) { blocked=nullptr; break; }
   if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) blocked=nullptr;
   return;
 }
 int source=-1;
 bool new_contact=false;
 for(int i=0;i<int(contacts_.size());++i) if(contacts_[i].input==input) source=i;
 if(source<0 && (code==LV_EVENT_PRESSED || code==LV_EVENT_PRESSING)) {
   for(int i=0;i<int(contacts_.size());++i) if(!contacts_[i].input) { source=i; contacts_[i].input=input; new_contact=true; break; }
 }
 if(source<0) return;
 auto& contact=contacts_[source];
 lv_point_t p; lv_indev_get_point(input,&p);
 auto distance_squared=[](lv_point_t a,lv_point_t b) { int x=a.x-b.x,y=a.y-b.y; return x*x+y*y; };
 if(code==LV_EVENT_PRESSED || new_contact) {
   contact.start=p; contact.started=lv_tick_get(); contact.centre=hit(p.x,p.y)==-2;
   contact.tap=contact.centre;
   if(!contact.centre) tap_pending_=false;
   for(int i=0;i<int(contacts_.size());++i) if(i!=source && contacts_[i].input) {
     // Two fingers can play/swipe independently, but never form a menu double tap.
     contact.tap=false; contacts_[i].tap=false; tap_pending_=false;
   }
 }
 if(code==LV_EVENT_PRESSED || code==LV_EVENT_PRESSING) {
   if(contact.centre) {
     const int dx=p.x-contact.start.x,dy=p.y-contact.start.y;
     if(distance_squared(p,contact.start)>12*12) contact.tap=tap_pending_=false;
     if(chord_mode_) return; // The centre owns the whole edit gesture, never a note.
     if(!contact.swiped && std::abs(dy)>=48 && std::abs(dy)*10>std::abs(dx)*13) {
       change_octave(dy<0?1:-1); contact.swiped=true; contact.tap=false;
     }
     return; // Centre-started contacts never become note contacts, even over a segment.
   }
   int degree=hit(p.x,p.y);
   if(code==LV_EVENT_PRESSING && contact.pressed>=0 && hit(p.x,p.y,true)==contact.pressed) degree=contact.pressed;
   if(degree!=contact.pressed) {
     release_segment(source);
     if(degree>=0) {
       contact.pressed=degree; fading_[degree]=false; highlight_[degree]=255;
       if(chord_mode_) chord_.press(source,degree); else note_.press(source,degree);
       invalidate_segment(degree);
     }
   }
 }
 if(code==LV_EVENT_RELEASED) {
   const int edit_pad=chord_mode_ && contact.centre && !contact.tap?hit(p.x,p.y):-1;
   const uint32_t now=lv_tick_get();
   const bool valid_tap=contact.centre && contact.tap && !contact.swiped && hit(p.x,p.y)==-2 &&
     distance_squared(p,contact.start)<=12*12 && uint32_t(now-contact.started)<=250;
   const bool double_tap=valid_tap && tap_pending_ && uint32_t(now-last_tap_time_)<=400 && distance_squared(p,last_tap_point_)<=24*24;
   release_contact(source); tap_pending_=false;
   if(edit_pad>=0) open_chord_editor(edit_pad);
   else if(double_tap) open();
   else if(valid_tap) { tap_pending_=true; last_tap_time_=now; last_tap_point_=p; }
 }
 if(code==LV_EVENT_PRESS_LOST) { release_contact(source); tap_pending_=false; }
}
void Ui::build() {
 build_bounce();
 if(output_) output_->configure(app_.setup);
 note_.channel(app_.setup.channel);
 sync_arp_clock();
 auto* screen=lv_screen_active(); lv_obj_set_style_text_font(screen,&barlow_18,0); lv_obj_set_style_bg_color(screen,color(0x000000),0); lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
 wheel_=lv_obj_create(screen); plain(wheel_); lv_obj_set_size(wheel_,466,466); lv_obj_add_flag(wheel_,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(wheel_,wheel_event,LV_EVENT_ALL,this); refresh();
 selection_=lv_obj_create(screen); plain(selection_); lv_obj_set_size(selection_,466,466);
 // All content already fits inside the display circle. Rounded child clipping
 // would allocate/composite ARGB layers across both halves on every wheel redraw.
 lv_obj_set_style_radius(selection_,233,0); lv_obj_set_style_clip_corner(selection_,false,0);
 lv_obj_set_style_bg_color(selection_,color(0x000000),0); lv_obj_set_style_bg_opa(selection_,255,0);
 // Shared subpixel outline rendering matches the note-button contours exactly.
 add_menu_frame(0,selector_left,203,selector_root_width,60,30,6);
 add_menu_frame(1,selector_mode_left,203,selector_mode_width,60,6,30);
 add_menu_frame(2,117,352,232,62,31,31);
 make_menu_frame(strum_frame_,304,203,110,60,30,30,selection_,accent(.5f));
 scale_title_=label(selection_,"SCALE",78,58,310,&barlow_48,accent(.5f));
 root_=lv_obj_create(selection_); mode_=lv_obj_create(selection_);
 for(int column:{0,1,16,19}) {
   auto* r=column==0?root_:column==1?mode_:lv_obj_create(selection_);
   const int w=column==19?110:column==16?149:column?selector_mode_width:selector_root_width, x=column==19?304:column==16?268:column?selector_mode_left:selector_left;
   plain(r); lv_obj_add_flag(r,LV_OBJ_FLAG_CLICKABLE);
   selectors_[column].owner=this; selectors_[column].column=column;
   lv_obj_set_size(r,w,238); lv_obj_set_pos(r,x,114);
   lv_obj_add_event_cb(r,selection_event,LV_EVENT_ALL,this);
   auto* viewport=selector_views_[column]=r;
   lv_obj_set_style_bg_color(viewport,color(0),0); lv_obj_set_style_bg_opa(viewport,255,0);
   lv_obj_add_event_cb(viewport,[](lv_event_t* e) {
     auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
     const auto* target=lv_event_get_target_obj(e);
     self->draw_selector(lv_event_get_layer(e),target==self->selector_views_[19]?19:target==self->selector_views_[16]?16:target==self->selector_views_[1]?1:0);
   },LV_EVENT_DRAW_MAIN,this);
   // These alpha gradients form a feathered mask across individual glyphs,
   // in addition to the continuous distance-based row scale and opacity.
   for(int bottom=0;bottom<2;++bottom) {
     auto* mask=lv_obj_create(viewport); plain(mask); lv_obj_remove_flag(mask,LV_OBJ_FLAG_CLICKABLE);
     lv_obj_set_size(mask,w,58); lv_obj_set_pos(mask,0,bottom?180:0);
     lv_obj_set_style_bg_color(mask,color(0x000000),0); lv_obj_set_style_bg_grad_color(mask,color(0x000000),0);
     lv_obj_set_style_bg_opa(mask,255,0); lv_obj_set_style_bg_grad_dir(mask,LV_GRAD_DIR_VER,0);
     lv_obj_set_style_bg_main_opa(mask,bottom?0:255,0); lv_obj_set_style_bg_grad_opa(mask,bottom?255:0,0);
     selector_mask_never_covers(mask);
   }
   if(column==16 || column==19) lv_obj_add_flag(r,LV_OBJ_FLAG_HIDDEN);
 }
 lv_timer_create([](lv_timer_t* t){ static_cast<Ui*>(lv_timer_get_user_data(t))->update_feedback(); },16,this);
 auto* select=lv_button_create(selection_); plain(select); lv_obj_set_size(select,232,62); lv_obj_set_pos(select,117,352);
 lv_obj_set_style_radius(select,32,0); lv_obj_set_style_bg_color(select,color(0),0);
 lv_obj_set_style_bg_opa(select,0,0); lv_obj_set_style_border_width(select,0,0);
 lv_obj_set_style_shadow_width(select,0,0);
 auto* caption=label(select,"SELECT",0,0,216,&barlow_28,color(palette().text)); lv_obj_center(caption);
 lv_obj_add_event_cb(select,[](lv_event_t* e){ static_cast<Ui*>(lv_event_get_user_data(e))->confirm(); },LV_EVENT_CLICKED,this);
 error_=label(selection_,"",80,327,306,&barlow_14,color(palette().text));
 for(auto& frame:menu_frames_) if(frame.object) lv_obj_move_foreground(frame.object);
 lv_obj_add_flag(selection_,LV_OBJ_FLAG_HIDDEN);
 build_setup();
 build_chord_editor();
 build_power_off();
 arp_.selection(app_.arp_scale());
 const auto saved_arp=app_.load_arpeggiator(); arp_.restore(saved_arp); arp_page_=saved_arp.page;
 sync_arp_clock();
 arp_saved_=arp_.preset(arp_page_); arp_save_ready_=true;
}
void Ui::refresh(bool preserve_feedback) {
 for(int column:arp_columns) { lv_anim_delete(&selectors_[column],nullptr); selectors_[column].input=nullptr; selector_views_[column]=nullptr; row_offsets_[column]=-99999; }
 for(auto& frame:arp_frames_) { lv_image_cache_drop(&frame.image); frame.object=nullptr; }
 lv_image_cache_drop(&arp_tap_frame_.image); arp_tap_frame_.object=nullptr; arp_tap_button_=nullptr; arp_tap_input_=nullptr; arp_tap_fading_=false;
 arp_note_labels_.fill(nullptr); arp_gesture_={};
 if(!preserve_feedback) { fading_.fill(false); highlight_.fill(0); rendered_count_=-1; }
 if(bounce_mode_){
   lv_obj_clean(wheel_);lv_image_cache_drop(&wheel_image_);wheel_image_={};
   std::vector<uint32_t>().swap(wheel_pixels_);for(auto& pixels:segment_pixels_)std::vector<WheelPixel>().swap(pixels);
   geometry_count_=-1;lv_obj_invalidate(wheel_);return;
 }
 if(!arp_mode_ && !chord_mode_) note_.selection(app_.current);
 if(chord_mode_) chord_.selection(app_.chords);
 arp_.selection(app_.current);
 octave_label_=nullptr;
 if(arp_mode_) {
   lv_obj_clean(wheel_); refresh_arp_notes();
   build_arp(); lv_obj_invalidate(wheel_); return;
 }
 arp_panel_=nullptr;
 if(chord_mode_) {
   lv_obj_clean(wheel_);
   const auto& chords=chord_.palette();
   for(int i=0;i<chord_pad_count;++i) {
     const auto p=point(164,270+float(i)*360/chord_pad_count);
     const auto* font=chords[i].name.size()>7?&barlow_18:&barlow_24;
     auto* caption=label(wheel_,chords[i].name.c_str(),int(p.x)-52,int(p.y)-17,104,font,color(0xffffff));
     lv_label_set_long_mode(caption,LV_LABEL_LONG_CLIP);
   }
   auto* centre=lv_obj_create(wheel_); plain(centre); lv_obj_remove_flag(centre,LV_OBJ_FLAG_CLICKABLE);
   lv_obj_set_size(centre,196,LV_SIZE_CONTENT); lv_obj_set_flex_flow(centre,LV_FLEX_FLOW_ROW);
   lv_obj_set_flex_align(centre,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
   lv_obj_set_style_pad_column(centre,8,0);
   auto* key_label=label(centre,roots()[app_.chords.root],0,0,LV_SIZE_CONTENT,&barlow_48,accent(.5f));
   const char* name=app_.chords.mode==1?"Minor":modes()[app_.chords.mode].name;
   lv_point_t root_size; lv_text_get_size(&root_size,roots()[app_.chords.root],&barlow_48,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
   const lv_font_t* mode_font=&barlow_18;
   for(const auto* candidate:{&barlow_28,&barlow_25,&barlow_24,&barlow_22,&barlow_18}) {
     lv_point_t size; lv_text_get_size(&size,name,candidate,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
     if(root_size.x+8+size.x<=190) { mode_font=candidate; break; }
   }
   auto* mode_label=label(centre,name,0,0,LV_SIZE_CONTENT,mode_font,color(0xffffff));
   lv_label_set_long_mode(mode_label,LV_LABEL_LONG_CLIP);
   lv_obj_update_layout(centre);
   // Align the text baselines rather than the unequal font boxes' centres.
   const int key_baseline=lv_obj_get_y(key_label)+barlow_48.line_height-barlow_48.base_line;
   const int mode_baseline=lv_obj_get_y(mode_label)+mode_font->line_height-mode_font->base_line;
   lv_obj_set_style_translate_y(mode_label,key_baseline-mode_baseline,0);
   lv_obj_center(centre);
   auto* subtitle=label(wheel_,"DIATONIC CHORDS",0,0,196,&barlow_18,color(0xffffff));
   lv_obj_align_to(subtitle,centre,LV_ALIGN_OUT_BOTTOM_MID,0,2);
   lv_obj_update_layout(wheel_);
   // Centre the combined visible text, excluding the fonts' empty ascender/
   // descender padding. Keep the key/mode baseline and subtitle gap intact.
   int ink_top=466,ink_bottom=0;
   for(auto* object:{key_label,mode_label,subtitle}) {
     lv_area_t bounds; lv_obj_get_coords(object,&bounds);
     const auto* font=lv_obj_get_style_text_font(object,0);
     const auto* text=lv_label_get_text(object);
     for(size_t i=0;text[i];++i) {
       lv_font_glyph_dsc_t glyph{};
       if(!lv_font_get_glyph_dsc(font,&glyph,uint8_t(text[i]),uint8_t(text[i+1])) || !glyph.box_h) continue;
       const int bottom=int(bounds.y1)+font->line_height-font->base_line-glyph.ofs_y-1;
       ink_top=std::min(ink_top,bottom-int(glyph.box_h)+1);
       ink_bottom=std::max(ink_bottom,bottom);
     }
   }
   const int block_shift=(465-ink_top-ink_bottom)/2;
   lv_obj_set_style_translate_y(centre,block_shift,0);
   lv_obj_set_style_translate_y(subtitle,block_shift,0);
   lv_obj_invalidate(wheel_); return;
 }
 lv_obj_clean(wheel_); auto ns=notes(app_.current); float step=360.f/float(ns.size());
 for(size_t i=0;i<ns.size();++i) { if(!note_.playable(int(i))) continue; auto p=point(164,270+float(i)*step); label(wheel_,ns[i].name.c_str(),int(p.x)-42,int(p.y)-17,84,&barlow_28,color(palette().text)); }
 auto* centre=lv_obj_create(wheel_); plain(centre); lv_obj_remove_flag(centre,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_size(centre,196,LV_SIZE_CONTENT); lv_obj_set_flex_flow(centre,LV_FLEX_FLOW_COLUMN);
 lv_obj_set_flex_align(centre,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
 lv_obj_set_style_pad_row(centre,3,0);
 auto* root_line=lv_obj_create(centre); plain(root_line); lv_obj_remove_flag(root_line,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_size(root_line,196,LV_SIZE_CONTENT); lv_obj_set_flex_flow(root_line,LV_FLEX_FLOW_ROW);
 lv_obj_set_flex_align(root_line,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
 lv_obj_set_style_pad_column(root_line,0,0);
 label(root_line,roots()[app_.current.root],0,0,LV_SIZE_CONTENT,&barlow_48,color(palette().text));
 const std::string octave=std::to_string(note_.octave());
 octave_label_=label(root_line,octave.c_str(),0,0,LV_SIZE_CONTENT,&barlow_48,accent(.5f));
 std::string name=modes()[app_.current.mode].name; if(app_.current.mode==1) name="Minor";
 const lv_font_t* mode_font=&barlow_18;
 for(const auto* candidate:{&barlow_28,&barlow_25,&barlow_18}) {
   lv_point_t size; lv_text_get_size(&size,name.c_str(),candidate,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
   if(size.x<=190) { mode_font=candidate; break; }
 }
 auto* l=label(centre,name.c_str(),0,0,190,mode_font,color(palette().text)); lv_label_set_long_mode(l,LV_LABEL_LONG_CLIP);
 lv_obj_update_layout(centre); lv_obj_center(centre);

 lv_obj_invalidate(wheel_);
}
void Ui::selection_layout() {
 for(int i=0;i<2;++i) {
   auto* view=selector_views_[i];
   const int width=i?selector_mode_width:chord_mode_?62:selector_root_width;
   lv_obj_set_pos(view,i?selector_mode_left:chord_mode_?51:selector_left,114);
   lv_obj_set_width(view,width); selectors_[i].font_size=32;
   for(uint32_t child=0;child<lv_obj_get_child_count(view);++child) lv_obj_set_width(lv_obj_get_child(view,child),width);
 }
 if(chord_mode_) {
   lv_obj_add_flag(mode_,LV_OBJ_FLAG_HIDDEN);
   lv_obj_remove_flag(selector_views_[16],LV_OBJ_FLAG_HIDDEN);
 } else {
   lv_obj_remove_flag(mode_,LV_OBJ_FLAG_HIDDEN);
   lv_obj_add_flag(selector_views_[16],LV_OBJ_FLAG_HIDDEN);
 }
 auto* chord_scale=selector_views_[16];
 lv_obj_set_pos(chord_scale,chord_mode_?117:selector_mode_left,114); lv_obj_set_width(chord_scale,chord_mode_?171:selector_mode_width);
 for(uint32_t child=0;child<lv_obj_get_child_count(chord_scale);++child) lv_obj_set_width(lv_obj_get_child(chord_scale,child),chord_mode_?171:selector_mode_width);
 selectors_[16].font_size=selectors_[1].font_size;
 for(int i=0;i<2;++i) {
   auto& frame=menu_frames_[i]; lv_image_cache_drop(&frame.image); lv_obj_delete(frame.object);
   make_menu_frame(frame,chord_mode_?(i?117:51):(i?selector_mode_left:selector_left),203,
     chord_mode_?(i?171:62):(i?selector_mode_width:selector_root_width),60,i?6:30,i?30:6,selection_,accent(.5f));
 }
 lv_obj_set_pos(scale_title_,78,58);
 lv_obj_set_width(scale_title_,310);
 lv_obj_set_style_text_font(scale_title_,&barlow_48,0);
 lv_image_cache_drop(&strum_frame_.image); lv_obj_delete(strum_frame_.object);
 make_menu_frame(strum_frame_,304,203,110,60,30,30,selection_,accent(.5f));
 for(auto* object:{selector_views_[19],strum_frame_.object}) {
   if(chord_mode_) lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
 }
}
void Ui::open() {
 if(setup_open_ || shutting_down_ || power_off_visible_ || is_editing_chord()) return;
 arp_.suspend(true); cancel_touch(); note_.panic(); app_.begin();
 chord_draft_=app_.chords;
 selection_layout();
 lv_label_set_text(scale_title_,"SCALE");
 for(int i:{0,1,16,19}) { lv_anim_delete(&selectors_[i],nullptr); selectors_[i].input=nullptr; selectors_[i].target=(i==19?(app_.setup.receive_clock?chord_strum_option(chord_draft_.strum_division,chord_draft_.strum_timing):chord_draft_.strum_ms/10):i==16?chord_mode_option(chord_draft_.mode):i?app_.draft.mode:chord_mode_?chord_draft_.root:app_.draft.root)*60; set_selector_offset(i,selectors_[i].target); }
 lv_label_set_text(error_,""); lv_obj_add_flag(wheel_,LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(selection_,LV_OBJ_FLAG_HIDDEN);
}
void Ui::back() {
 if(power_off_visible_) return;
 if(is_editing_chord()) chord_.selection(app_.chords); // Stop audition and restore saved palette.
 for(auto& s:selectors_) { lv_anim_delete(&s,nullptr); s.input=nullptr; }
 chord_edit_pad_=-1;
 lv_obj_add_flag(chord_editor_,LV_OBJ_FLAG_HIDDEN);
 app_.back(); lv_obj_add_flag(selection_,LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(wheel_,LV_OBJ_FLAG_HIDDEN); sync_arp_pause();
}
bool Ui::confirm() {
 if(power_off_visible_) return false;
 if(is_editing_chord()) {
   for(int column:{17,18}) {
     auto& selector=selectors_[column];
     if(selector.input) { animate_selector(column,selector.offset); selector.input=nullptr; }
   }
   auto candidate=app_.chords; candidate.variants[chord_edit_pad_]=chord_edit_draft_;
   candidate.octave_shifts[chord_edit_pad_]=int8_t(chord_edit_shift_);
   if(!app_.save_chords(candidate)) { lv_label_set_text(chord_edit_error_,"Saving failed - try again"); return false; }
   back(); refresh(); return true;
 }
 if(chord_mode_) {
   for(int column:{0,16,19}) if(selectors_[column].input) {
     animate_selector(column,selectors_[column].offset); selectors_[column].input=nullptr;
   }
   if(!app_.save_chords(chord_draft_)) { lv_label_set_text(error_,"Saving failed - try again"); return false; }
   app_.back();
 } else if(!app_.select()) { lv_label_set_text(error_,"Saving failed - try again"); return false; }
 for(auto& s:selectors_) lv_anim_delete(&s,nullptr);
 lv_obj_add_flag(selection_,LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(wheel_,LV_OBJ_FLAG_HIDDEN); refresh(); sync_arp_pause(); return true;
}
void Ui::set_selector_offset(int column,int offset) {
 if(selector_bounded(column)) offset=std::clamp(offset,0,(selector_count(column)-1)*selectors_[column].step);
 auto& s=selectors_[column];
 if((column==17 || column==18) && s.offset==offset && row_offsets_[column]!=-99999) return;
 selectors_[column].offset=offset; row_offsets_[column]=selectors_[column].center-offset;
 if(s.dirty_width>0) {
   lv_area_t area; lv_obj_get_coords(selector_views_[column],&area);
   const int center=(area.x1+area.x2+1)/2;
   area.x1=std::max<int32_t>(area.x1,center-(s.dirty_width+1)/2);
   area.x2=std::min<int32_t>(area.x2,center+(s.dirty_width+1)/2-1);
   lv_obj_invalidate_area(selector_views_[column],&area);
 } else lv_obj_invalidate(selector_views_[column]);
}
int Ui::selector_count(int column) const { return column>=33?(column==33?16:120):(column>=23 && column<=30)?37:column==22?music_slot_count:column==19?(app_.setup.receive_clock?int(chord_strum_choices.size()):26):column==18?3:column==17?chord_variant_count:column==16?chord_scale_count:column>=6?arp_selector_count(column):column>=4?10:column==3?waveform_count:column==2?16:column==1?int(modes().size()):12; }
void Ui::animate_selector(int column,int target) {
 auto& s=selectors_[column]; lv_anim_delete(&s,nullptr);
 const int count=selector_count(column);
 s.target=int(std::lround(float(target)/s.step))*s.step;
 if(selector_bounded(column)) s.target=std::clamp(s.target,0,(count-1)*s.step);
 const int option=(s.target/s.step%count+count)%count;
 if(column>=33) {if(column==33)motion_draft_.channel=option+1;else motion_draft_.cc[column-34]=motion_target(option);}
 else if(column>=23 && column<=30) slot_draft_.name[column-23]=slot_characters[option];
 else if(column==22) {
   if(slot_index_!=option) { slot_index_=option; update_slot(); }
 }
 else if(column==19) {
   if(app_.setup.receive_clock) {
     chord_draft_.strum_division=chord_strum_choices[option].division;
     chord_draft_.strum_timing=chord_strum_choices[option].timing;
   }
   else chord_draft_.strum_ms=option*10;
 }
 else if(column==18 || column==17) {
   const bool changed=column==18?chord_edit_shift_!=option-1:int(chord_edit_draft_)!=option;
   if(column==18) chord_edit_shift_=option-1; else chord_edit_draft_=ChordVariant(option);
   if(changed && is_editing_chord()) {
     auto preview=app_.chords;
     preview.variants[chord_edit_pad_]=chord_edit_draft_;
     preview.octave_shifts[chord_edit_pad_]=int8_t(chord_edit_shift_);
     chord_.preview(preview,chord_edit_pad_);
   }
 }
 else if(column==16) chord_draft_.mode=chord_modes[option];
 else if(column>=6) apply_arp_option(column,option);
 else if(column==3) setup_draft_.synth.waveform=Waveform(option);
 else if(column==4) setup_draft_.synth.attack_ms=synth_attacks[option];
 else if(column==5) setup_draft_.synth.decay_ms=synth_decays[option];
 else if(column==2) setup_draft_.channel=option+1;
 else if(column) app_.draft.mode=option; else if(chord_mode_) chord_draft_.root=option; else app_.draft.root=option;
 lv_anim_t animation; lv_anim_init(&animation); lv_anim_set_var(&animation,&s);
 lv_anim_set_values(&animation,s.offset,s.target); lv_anim_set_duration(&animation,180);
 lv_anim_set_path_cb(&animation,lv_anim_path_ease_out);
 lv_anim_set_exec_cb(&animation,[](void* var,int32_t value) { auto* state=static_cast<Selector*>(var); state->owner->set_selector_offset(state->column,value); });
 lv_anim_start(&animation);
}
void Ui::draw_selector(lv_layer_t* layer,int column) {
 const int first=row_offsets_[column];
 if(first==-99999) return;
 const auto& s=selectors_[column];
 const int nearest=int(std::round(float(s.center-first)/s.step));
 const int count=selector_count(column);
 lv_area_t viewport; lv_obj_get_coords(selector_views_[column],&viewport);
 lv_area_t clipped{std::max(viewport.x1,layer->_clip_area.x1),std::max(viewport.y1,layer->_clip_area.y1),
                   std::min(viewport.x2,layer->_clip_area.x2),std::min(viewport.y2,layer->_clip_area.y2)};
 if(clipped.x1>clipped.x2 || clipped.y1>clipped.y2) return;
 const lv_area_t saved_clip=layer->_clip_area;
 layer->_clip_area=clipped;
 for(int j=-2;j<=2;++j) {
   const int index=nearest+j,option=(index%count+count)%count,cy=first+index*s.step;
   if(selector_bounded(column) && (index<0 || index>=count)) continue;
   const float distance=float(std::abs(cy-s.center))/s.half_height;
   if(distance>=1.f) continue;
   const auto* font=selector_font(distance,s.font_size);
   float fade=1-distance*distance; fade*=fade;
   if(column==3) {
     const int size=std::clamp(int(std::lround(3*(1-distance))),0,3);
     const auto& image=waveform_images_[option*4+size];
     const int x=(viewport.x1+viewport.x2+1-int(image.header.w))/2;
     lv_area_t bounds{x,cy-int(image.header.h)/2,x+int(image.header.w)-1,cy+(int(image.header.h)+1)/2-1};
     lv_draw_image_dsc_t draw; lv_draw_image_dsc_init(&draw); draw.src=&image;
     draw.opa=uint8_t(fade*255); draw.recolor=color(palette().text); draw.recolor_opa=255; draw.image_area=bounds;
     lv_draw_image(layer,&draw,&bounds);
     continue;
   }
   lv_draw_label_dsc_t text; lv_draw_label_dsc_init(&text);
   char option_text[64]{};
   if(column>=33) {if(column==33)std::snprintf(option_text,sizeof(option_text),"%d",option+1);else if(!option)std::snprintf(option_text,sizeof(option_text),"OFF");else std::snprintf(option_text,sizeof(option_text),"CC %d",option);text.text=option_text;text.text_local=1;}
   else if(column>=23 && column<=30) { option_text[0]=slot_characters[option]; text.text=option_text; text.text_local=1; }
   else if(column==22) { text.text=slot_names_[option].c_str(); }
   else if(column==19) {
     if(app_.setup.receive_clock) {
       if(!option) std::snprintf(option_text,sizeof(option_text),"OFF");
       else {
         const auto choice=chord_strum_choices[option];
         std::snprintf(option_text,sizeof(option_text),"1/%d%s",choice.division,choice.timing==1?"D":choice.timing==2?"T":"");
       }
     } else std::snprintf(option_text,sizeof(option_text),"%d ms",option*10);
     text.text=option_text; text.text_local=1;
   }
   else if(column==18) text.text=option==0?"-1":option==1?"0":"+1";
   else if(column==17) { draw_chord_option(option,option_text,sizeof(option_text)); text.text=option_text; text.text_local=1; }
   else if(column==16) text.text=menu_modes[chord_modes[option]];
   else if(column>=6) { text.text=arp_selector_text(column,option,option_text,sizeof(option_text)); text.text_local=1; }
   else if(column>=4) text.text=column==4?attack_labels[option]:decay_labels[option];
   else text.text=column==2?menu_channels[option]:column?(size_t(option)<menu_modes.size()?menu_modes[option]:modes()[option].name):roots()[option];
   if(column==17) {
     for(int base:{28,25,24,22,18}) {
       const int pixels=selector_font_size(distance,base);
       font=selector_native_font(pixels);
       if(chord_edit_widths_[option][pixels-18]<=lv_area_get_width(&viewport)-32) break;
     }
   }
   text.font=font; text.color=color(palette().text); text.opa=uint8_t(fade*255);
   const bool mode_column=column==1 || column==8 || column==16;
   text.align=mode_column?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER;
   const int x=viewport.x1+(mode_column?16:column==0 || column==7?4:0),width=lv_area_get_width(&viewport)-(mode_column?32:0);
   lv_area_t bounds{x,cy-int(font->line_height)/2-3,x+width-1,cy-int(font->line_height)/2-3+font->line_height-1};
   lv_draw_label(layer,&text,&bounds);
 }
 layer->_clip_area=saved_clip;
}
void Ui::selection_event(lv_event_t* e) {
 auto* self=static_cast<Ui*>(lv_event_get_user_data(e));
 if(self->power_off_visible_) return;
 int column=-1;
 for(size_t i=0;i<self->selector_views_.size();++i) if(lv_event_get_target_obj(e)==self->selector_views_[i]) column=int(i);
 if(column<0) return;
 if(column>=33) {if(!self->setup_open_ || self->setup_page_!=5)return;}
 else if(column>=23 && column<=30) { if(!self->setup_open_ || self->slot_dialog_!=1) return; }
 else if(column==22) { if(!self->setup_open_ || self->setup_page_!=4 || self->slot_dialog_) return; }
 else if(column==17 || column==18) { if(!self->is_editing_chord() || !self->app_.selecting || self->setup_open_) return; }
 else if(column==16 || column==19) { if(!self->chord_mode_ || !self->app_.selecting || self->setup_open_ || self->is_editing_chord()) return; }
 else if(column>=6) { if(!self->arp_mode_ || self->arp_page_!=self->arp_selector_page(column) || self->setup_open_ || self->app_.selecting) return; }
 else if(column>=2?(!self->setup_open_ || self->setup_page_!=(column==2?0:2)):!self->app_.selecting) return;
 auto& s=self->selectors_[column]; const auto code=lv_event_get_code(e);
 if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_PRESSING && code!=LV_EVENT_RELEASED && code!=LV_EVENT_PRESS_LOST) return;
 auto* input=lv_indev_active(); if(!input) return;
 lv_point_t p; lv_indev_get_point(input,&p);
 if(s.input && s.input!=input) return;
 if(code!=LV_EVENT_PRESSED && s.input!=input) return;
 if(code==LV_EVENT_PRESSED) {
   s.input=input;
   lv_anim_delete(&s,nullptr); s.last_y=s.start_y=p.y; s.dragged=false; s.velocity=0; s.drag_remainder=0; s.last_move=lv_tick_get();
 } else if(code==LV_EVENT_PRESSING) {
   const int delta=p.y-s.last_y; s.last_y=p.y;
   if(delta) {
     if(std::abs(p.y-s.start_y)>4) s.dragged=true;
     // Global 80% touch sensitivity. Carry fractional pixels between samples so
     // slow movement stays continuous instead of being rounded away each time.
     const int scaled=delta*4+s.drag_remainder,move=scaled/5;
     s.drag_remainder=scaled%5;
     s.velocity=move; s.last_move=lv_tick_get(); self->set_selector_offset(column,s.offset-move);
   }
 } else {
   s.input=nullptr;
   const int extra=s.dragged && uint32_t(lv_tick_get()-s.last_move)<80?std::clamp(-s.velocity*2,-s.step,s.step):0;
   const int tap=s.dragged?0:int(std::lround(float(p.y-s.center)/s.step))*s.step;
   self->animate_selector(column,s.offset+extra+tap);
 }
}
bool Ui::change_octave(int delta) {
 if(bounce_mode_)return false;
 if(arp_mode_ || chord_mode_ || app_.selecting || setup_open_ || shutting_down_ || power_off_visible_) return false;
 const int target=std::clamp(note_.octave()+delta,0,8);
 if(target==note_.octave()) return false;
 for(auto& contact:contacts_) { contact.tap=false; }
 tap_pending_=false;
 uint16_t before=0,after=0;
 const int count=int(modes()[app_.current.mode].intervals.size());
 for(int i=0;i<count;++i) if(note_.playable(i)) before|=1u<<i;
 note_.set_octave(target);
 for(int i=0;i<count;++i) if(note_.playable(i)) after|=1u<<i;
 if(octave_label_ && before==after) lv_label_set_text_fmt(octave_label_,"%d",target);
 else refresh(true); // Rebuild only when MIDI-limit visibility actually changes.
 return true;
}
void Ui::button(int index,ButtonEvent event) {
 if(index>=0 && index<2 && event==ButtonEvent::Press) change_octave(index==0?1:-1);
}
void Ui::scroll(int x,int y,int delta) {
 if(power_off_visible_) return;
 if(bounce_mode_ && !setup_open_ && !app_.selecting){if(bounce_)bounce_->scroll(x,y,delta);return;}
 if(is_editing_chord()) {
   for(int column:{17,18}) {
     lv_area_t bounds; lv_obj_get_coords(selector_views_[column],&bounds);
     if(x>=bounds.x1 && x<=bounds.x2 && y>=bounds.y1 && y<=bounds.y2)
       animate_selector(column,selectors_[column].target-delta*selectors_[column].step);
   }
   return;
 }
 if(arp_mode_ && !setup_open_ && !app_.selecting) {
   for(int column:arp_columns) if(arp_selector_page(column)==arp_page_) {
     lv_area_t bounds; lv_obj_get_coords(selector_views_[column],&bounds);
     if(x>=bounds.x1 && x<=bounds.x2 && y>=bounds.y1 && y<=bounds.y2) animate_selector(column,selectors_[column].target-delta*selectors_[column].step);
   }
   return;
 }
 if(setup_open_ && slot_dialog_) {
   if(slot_dialog_==1) for(int column=23;column<31;++column) {
     lv_area_t bounds; lv_obj_get_coords(selector_views_[column],&bounds);
     if(x>=bounds.x1 && x<=bounds.x2 && y>=bounds.y1 && y<=bounds.y2) animate_selector(column,selectors_[column].target-delta*selectors_[column].step);
   }
   return;
 }
 if(setup_open_) {
   for(int column:{2,3,4,5,22,33,34,35}) {
     if(setup_page_!=(column>=33?5:column==22?4:column==2?0:2)) continue;
     lv_area_t bounds; lv_obj_get_coords(selector_views_[column],&bounds);
     if(x>=bounds.x1 && x<=bounds.x2 && y>=bounds.y1 && y<=bounds.y2) animate_selector(column,selectors_[column].target-delta*selectors_[column].step);
   }
   return;
 }
 if(!app_.selecting) return;
 for(int column:{0,1,16,19}) {
   if(chord_mode_?column==1:(column==16 || column==19)) continue;
   lv_area_t a; lv_obj_get_coords(selector_views_[column],&a);
   if(x>=a.x1 && x<=a.x2 && y>=a.y1 && y<=a.y2) animate_selector(column,selectors_[column].target-delta*60);
 }
}
}

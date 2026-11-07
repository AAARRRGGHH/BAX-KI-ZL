#include "ui.hpp"
#include "ui_fonts.hpp"
#include "battery_icon.hpp"
#include <algorithm>
#include <cmath>
namespace sm {
namespace {
constexpr int battery_width=battery_icon_width,battery_height=battery_icon_height;
constexpr float battery_scale=battery_icon_scale;
}
void Ui::build_power_off() {
 power_off_screen_=lv_obj_create(lv_screen_active());
 lv_obj_remove_style_all(power_off_screen_); lv_obj_set_size(power_off_screen_,466,466);
 lv_obj_remove_flag(power_off_screen_,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_flag(power_off_screen_,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_style_bg_color(power_off_screen_,lv_color_hex(0),0);
 lv_obj_set_style_bg_opa(power_off_screen_,LV_OPA_COVER,0);
 power_off_arc_=lv_arc_create(power_off_screen_);
 lv_obj_remove_style_all(power_off_arc_); lv_obj_set_size(power_off_arc_,326,326); lv_obj_center(power_off_arc_);
 lv_obj_remove_flag(power_off_arc_,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_remove_flag(power_off_arc_,LV_OBJ_FLAG_SCROLLABLE);
 lv_arc_set_rotation(power_off_arc_,270); lv_arc_set_bg_angles(power_off_arc_,0,360);
 lv_arc_set_range(power_off_arc_,0,3000); lv_arc_set_value(power_off_arc_,3000);
 lv_obj_set_style_arc_width(power_off_arc_,2,LV_PART_INDICATOR);
 lv_obj_set_style_arc_color(power_off_arc_,accent(.5f),LV_PART_INDICATOR);
 lv_obj_set_style_arc_opa(power_off_arc_,LV_OPA_COVER,LV_PART_INDICATOR);
 lv_obj_set_style_arc_rounded(power_off_arc_,true,LV_PART_INDICATOR);
 // Only the remaining part is visible; no track or draggable knob.
 lv_obj_set_style_arc_opa(power_off_arc_,LV_OPA_TRANSP,LV_PART_MAIN);
 auto* caption=lv_label_create(power_off_screen_); lv_label_set_text(caption,"POWER OFF");
 lv_obj_set_style_text_font(caption,&barlow_43,0);
 lv_obj_set_style_text_color(caption,lv_color_hex(0xffffff),0);
 lv_obj_remove_flag(caption,LV_OBJ_FLAG_CLICKABLE); lv_obj_center(caption);
 // Small procedural alpha masks: one continuous outline and a filled bolt.
 constexpr float bolt[][2]={{25,13},{10,34},{20,34},{14,49},{30,26},{21,26}};
 make_battery_outline(battery_outline_.data());
 for(int y=0;y<battery_height;++y) for(int x=0;x<battery_width;++x) {
   const float px=(x+.5f)/battery_scale,py=(y+.5f)/battery_scale;
   bool inside=false; float distance2=10000;
   for(int i=0,j=5;i<6;j=i++) {
     const float ax=bolt[j][0],ay=bolt[j][1],bx=bolt[i][0],by=bolt[i][1];
     if((ay>py)!=(by>py) && px<(bx-ax)*(py-ay)/(by-ay)+ax) inside=!inside;
     const float dx=bx-ax,dy=by-ay,t=std::clamp(((px-ax)*dx+(py-ay)*dy)/(dx*dx+dy*dy),0.f,1.f);
     const float ex=px-ax-t*dx,ey=py-ay-t*dy; distance2=std::min(distance2,ex*ex+ey*ey);
   }
   battery_bolt_[y*battery_width+x]=uint8_t(255*std::clamp(.5f+(inside?1.f:-1.f)*std::sqrt(distance2)*battery_scale,0.f,1.f));
 }
 auto mask=[](lv_image_dsc_t& image,const uint8_t* pixels) {
   image.header.magic=LV_IMAGE_HEADER_MAGIC; image.header.cf=LV_COLOR_FORMAT_A8;
   image.header.w=battery_width; image.header.h=battery_height; image.header.stride=battery_width;
   image.data_size=battery_width*battery_height; image.data=pixels;
 };
 mask(battery_outline_image_,battery_outline_.data()); mask(battery_bolt_image_,battery_bolt_.data());
 power_battery_=lv_obj_create(power_off_screen_); lv_obj_remove_style_all(power_battery_);
 lv_obj_set_size(power_battery_,battery_width,battery_height);
 // Center the visible shape between the caption's actual ink and the ring,
 // compensating for transparent margins in the icon and font line box.
 lv_obj_update_layout(power_off_screen_);
 lv_area_t caption_bounds,ring_bounds; lv_obj_get_coords(caption,&caption_bounds); lv_obj_get_coords(power_off_arc_,&ring_bounds);
 int ink_bottom=0;
 for(const char* p="POWER OFF";*p;++p) {
   lv_font_glyph_dsc_t glyph{};
   if(lv_font_get_glyph_dsc(&barlow_43,&glyph,uint8_t(*p),0) && glyph.box_h)
     ink_bottom=std::max(ink_bottom,int(barlow_43.line_height-barlow_43.base_line-glyph.ofs_y));
 }
 const float center_y=(caption_bounds.y1+ink_bottom+ring_bounds.y2-1)/2.f;
 lv_obj_set_pos(power_battery_,233-battery_width/2,int(std::lround(center_y-29*battery_scale)));
 lv_obj_remove_flag(power_battery_,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_remove_flag(power_battery_,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_event_cb(power_battery_,[](lv_event_t* event) {
   static_cast<Ui*>(lv_event_get_user_data(event))->draw_power_battery(lv_event_get_layer(event));
 },LV_EVENT_DRAW_MAIN,this);
 update_power_battery();
 lv_obj_add_flag(power_off_screen_,LV_OBJ_FLAG_HIDDEN);
}
void Ui::set_battery(BatteryState value) {
 battery_=value; battery_updated_=lv_tick_get(); update_power_battery();
}
void Ui::update_power_battery() {
 if(!power_battery_) return;
 const auto display=battery_display(battery_,uint32_t(lv_tick_get()-battery_updated_)<=5000);
 if(display.bars!=battery_display_.bars || display.external_power!=battery_display_.external_power || display.missing!=battery_display_.missing) {
   battery_display_=display; lv_obj_invalidate(power_battery_);
 }
}
void Ui::draw_power_battery(lv_layer_t* layer) {
 lv_area_t bounds; lv_obj_get_coords(power_battery_,&bounds);
 auto draw_mask=[&](const lv_image_dsc_t& image,lv_color_t color) {
   lv_draw_image_dsc_t draw; lv_draw_image_dsc_init(&draw); draw.src=&image;
   draw.recolor=color; draw.recolor_opa=255; draw.image_area=bounds; lv_draw_image(layer,&draw,&bounds);
 };
 draw_mask(battery_outline_image_,lv_color_hex(0xffffff));
 if(battery_display_.external_power) draw_mask(battery_bolt_image_,accent(.5f));
 else if(battery_display_.bars>=0) {
   lv_draw_rect_dsc_t bar; lv_draw_rect_dsc_init(&bar); bar.bg_color=accent(.5f); bar.bg_opa=255; bar.radius=1;
   for(int i=0;i<battery_display_.bars;++i) {
     // Integer pitch avoids alternating gaps from independently rounded edges.
     const int top=54-i*10;
     lv_area_t area{bounds.x1+int(std::lround(8*battery_scale)),bounds.y1+top,
       bounds.x1+int(std::lround(32*battery_scale))-1,bounds.y1+top+6};
     lv_draw_rect(layer,&bar,&area);
   }
 } else {
   lv_draw_label_dsc_t mark; lv_draw_label_dsc_init(&mark); mark.font=&barlow_20;
   mark.text=battery_display_.missing?"X":"?"; mark.color=lv_color_hex(0xffffff); mark.align=LV_TEXT_ALIGN_CENTER;
   lv_area_t area{bounds.x1+7,bounds.y1+25,bounds.x1+40,bounds.y1+55}; lv_draw_label(layer,&mark,&area);
 }
}
void Ui::begin_power_off() {
 cancel_bounce_contacts();
 if(power_off_visible_ || shutting_down_) return;
 power_off_visible_=true;
 update_power_battery();
 lv_obj_invalidate(power_battery_);
 // The overlay is also the battery view: keep sounding notes/strums and the
 // arpeggiator alive until shutdown is actually confirmed.
 tap_pending_=false; release_arp_tap(); arp_gesture_={};
 for(int i=0;i<int(contacts_.size());++i) if(contacts_[i].pressed<0) release_contact(i);
 for(size_t i=0;i<selectors_.size();++i) {
   auto& s=selectors_[i];
   if(s.input && (i>=6?arp_mode_:i>=2?setup_open_:app_.selecting)) animate_selector(int(i),s.offset);
   s.input=nullptr;
 }
 setup_gesture_={};
 lv_obj_set_style_arc_color(power_off_arc_,accent(.5f),LV_PART_INDICATOR);
 power_off_visible_=true; lv_arc_set_value(power_off_arc_,3000);
 lv_obj_move_foreground(power_off_screen_); lv_obj_remove_flag(power_off_screen_,LV_OBJ_FLAG_HIDDEN);
}
bool Ui::prepare_shutdown() {
 shutting_down_=true;stop_bounce_output();sync_arp_pause(); cancel_touch(); note_.panic();
 if(persist_arpeggiator(true) && persist_bounce()) return true;
 cancel_shutdown(); return false;
}
bool Ui::update_power_off(uint32_t held_ms) {
 if(!power_off_visible_) return false;
 update_power_battery();
 const auto elapsed=std::min<uint32_t>(held_ms,3000);
 lv_arc_set_value(power_off_arc_,int(3000-elapsed));
 return elapsed==3000;
}
void Ui::cancel_power_off() {
 if(!power_off_visible_ || shutting_down_) return;
 poll_power_off_contacts();
 power_off_visible_=false; lv_obj_add_flag(power_off_screen_,LV_OBJ_FLAG_HIDDEN);
}
void Ui::poll_power_off_contacts() {
 // The covering object can steal PRESS_LOST/RELEASE events from the wheel.
 // Follow physical release state, preserving existing notes but never starting
 // a new note or gesture through the overlay.
 for(int i=0;i<int(contacts_.size());++i)
   if(contacts_[i].input && lv_indev_get_state(contacts_[i].input)!=LV_INDEV_STATE_PRESSED) release_contact(i);
 for(auto* input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input)) {
   if(lv_indev_get_type(input)!=LV_INDEV_TYPE_POINTER || lv_indev_get_state(input)!=LV_INDEV_STATE_PRESSED) continue;
   bool known=false;
   for(const auto& contact:contacts_) if(contact.input==input) known=true;
   for(auto* blocked:blocked_inputs_) if(blocked==input) known=true;
   if(!known) for(auto& blocked:blocked_inputs_) if(!blocked) { blocked=input; break; }
 }
}
}

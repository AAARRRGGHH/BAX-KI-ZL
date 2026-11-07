#include "charge_screen.hpp"
#include "ui_fonts.hpp"
namespace sm {
namespace {
lv_obj_t* shape(lv_obj_t* parent,int x,int y,int w,int h,uint32_t color,int radius) {
 auto* obj=lv_obj_create(parent); lv_obj_remove_style_all(obj);
 lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,w,h);
 lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE); lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_style_bg_color(obj,lv_color_hex(color),0); lv_obj_set_style_bg_opa(obj,255,0);
 lv_obj_set_style_radius(obj,radius,0); return obj;
}
}
void ChargeScreen::build(uint32_t accent) {
 root_=shape(lv_screen_active(),0,0,466,466,0,0);
 lv_obj_add_flag(root_,LV_OBJ_FLAG_CLICKABLE);
 make_battery_outline(outline_pixels_.data());
 outline_.header.magic=LV_IMAGE_HEADER_MAGIC; outline_.header.cf=LV_COLOR_FORMAT_A8;
 outline_.header.w=battery_icon_width; outline_.header.h=battery_icon_height; outline_.header.stride=battery_icon_width;
 outline_.data_size=uint32_t(outline_pixels_.size()); outline_.data=outline_pixels_.data();
 auto* icon=lv_image_create(root_); lv_image_set_src(icon,&outline_);
 // Center the visible silhouette, compensating for its transparent margins.
 const int left=233-battery_icon_width/2,top=int(std::lround(233-29*battery_icon_scale));
 lv_obj_set_pos(icon,left,top);
 lv_obj_set_style_image_recolor(icon,lv_color_hex(0xffffff),0); lv_obj_set_style_image_recolor_opa(icon,255,0);
 for(int i=0;i<5;++i) bars_[i]=shape(root_,left+10,top+54-i*10,28,7,accent,1);
 unknown_=lv_label_create(root_); lv_label_set_text(unknown_,"?");
 lv_obj_set_style_text_font(unknown_,&barlow_20,0);
 lv_obj_set_style_text_color(unknown_,lv_color_hex(0xffffff),0);
 lv_obj_center(unknown_); update({},0);
 blink_timer_=lv_timer_create([](lv_timer_t* timer) {
   auto* self=static_cast<ChargeScreen*>(lv_timer_get_user_data(timer));
   self->paint(self->animation_clock_.now(lv_tick_get()));
 },500,this);
}
void ChargeScreen::update(BatteryState sample,uint32_t now,bool fresh) {
 sample_=sample; sampled_at_=lv_tick_get(); fresh_=fresh;
 animation_clock_.sync(now,sampled_at_); paint(now);
}
void ChargeScreen::paint(uint32_t now) {
 const auto state=charge_display(sample_,now,fresh_ && uint32_t(lv_tick_get()-sampled_at_)<=5000);
 for(int i=0;i<5;++i) {
   const bool visible=i<state.bars && !(state.blink && i==state.bars-1);
   if(visible) lv_obj_remove_flag(bars_[i],LV_OBJ_FLAG_HIDDEN);
   else lv_obj_add_flag(bars_[i],LV_OBJ_FLAG_HIDDEN);
 }
 lv_label_set_text(unknown_,state.missing?"X":"?");
 if(state.bars<0) lv_obj_remove_flag(unknown_,LV_OBJ_FLAG_HIDDEN);
 else lv_obj_add_flag(unknown_,LV_OBJ_FLAG_HIDDEN);
}
void ChargeScreen::close() {
 if(blink_timer_) lv_timer_delete(blink_timer_);
 blink_timer_=nullptr;
 lv_image_cache_drop(&outline_);
 if(root_) lv_obj_delete(root_);
 root_=unknown_=nullptr; bars_.fill(nullptr);
}
}

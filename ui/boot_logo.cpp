#include "ui.hpp"
#include "boot_logo_asset.hpp"
#include "ui_fonts.hpp"
#include <algorithm>
#include <cstdio>
namespace sm {
void Ui::show_boot_logo() {
 if(boot_logo_) return;
 cancel_touch();
 const auto decode=[&](int index,const auto& runs) {
   auto& pixels=boot_logo_pixels_[index];
   pixels.assign(boot_asset::width*boot_asset::height,0);
   size_t offset=0;
   for(auto run:runs) {
     const size_t count=run&255;
     LV_ASSERT(offset+count<=pixels.size());
     std::fill_n(pixels.begin()+offset,count,uint8_t(run>>8)); offset+=count;
   }
   LV_ASSERT(offset==pixels.size());
   auto& image=boot_logo_images_[index]; image={};
   image.header.magic=LV_IMAGE_HEADER_MAGIC; image.header.cf=LV_COLOR_FORMAT_A8;
   image.header.w=boot_asset::width; image.header.h=boot_asset::height;
   image.header.stride=boot_asset::width;
   image.data_size=uint32_t(pixels.size()); image.data=pixels.data();
 };
 decode(0,boot_asset::white); decode(1,boot_asset::accent);
 boot_logo_=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(boot_logo_);
 lv_obj_set_size(boot_logo_,466,466); lv_obj_set_pos(boot_logo_,0,0);
 lv_obj_remove_flag(boot_logo_,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_flag(boot_logo_,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_style_bg_color(boot_logo_,lv_color_hex(0),0);
 lv_obj_set_style_bg_opa(boot_logo_,255,0);
 boot_logo_art_=lv_obj_create(boot_logo_); lv_obj_remove_style_all(boot_logo_art_);
 lv_obj_set_size(boot_logo_art_,boot_asset::width,boot_asset::height);
 // The capsule's side centres are at SVG y=146, rather than the canvas centre.
 lv_obj_set_pos(boot_logo_art_,(466-boot_asset::width)/2,233-(146*boot_asset::height+125)/250);
 lv_obj_remove_flag(boot_logo_art_,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_remove_flag(boot_logo_art_,LV_OBJ_FLAG_CLICKABLE);
 boot_version_=lv_obj_create(boot_logo_);lv_obj_remove_style_all(boot_version_);
 lv_obj_set_size(boot_version_,LV_SIZE_CONTENT,LV_SIZE_CONTENT);
 lv_obj_set_flex_flow(boot_version_,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(boot_version_,0,0);
 lv_obj_remove_flag(boot_version_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_remove_flag(boot_version_,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_style_text_font(boot_version_,&barlow_28,0);
 auto* prefix=lv_label_create(boot_version_);lv_label_set_text(prefix,"V");lv_obj_set_style_text_color(prefix,accent(.5f),0);
 auto* number=lv_label_create(boot_version_);lv_label_set_text(number,"1.1");lv_obj_set_style_text_color(number,lv_color_hex(0xffffff),0);
 lv_obj_align(boot_version_,LV_ALIGN_BOTTOM_MID,0,-22);
 boot_logo_opacity_=255;
 lv_obj_add_event_cb(boot_logo_art_,[](lv_event_t* event) {
   auto* self=static_cast<Ui*>(lv_event_get_user_data(event));
   lv_area_t bounds; lv_obj_get_coords(lv_event_get_target_obj(event),&bounds);
   for(int i=0;i<2;++i) {
     lv_draw_image_dsc_t draw; lv_draw_image_dsc_init(&draw);
     draw.src=&self->boot_logo_images_[i]; draw.recolor_opa=255;
     draw.recolor=i?self->accent(.5f):lv_color_hex(0xffffff);
     draw.opa=self->boot_logo_opacity_; draw.image_area=bounds;
     lv_draw_image(lv_event_get_layer(event),&draw,&bounds);
   }
 },LV_EVENT_DRAW_MAIN,this);
 auto* timer=lv_timer_create([](lv_timer_t* timer) {
   auto* self=static_cast<Ui*>(lv_timer_get_user_data(timer));
   lv_anim_t animation; lv_anim_init(&animation); lv_anim_set_var(&animation,self);
   lv_anim_set_values(&animation,255,0); lv_anim_set_duration(&animation,300);
   lv_anim_set_exec_cb(&animation,[](void* context,int32_t value) {
     auto* self=static_cast<Ui*>(context); self->boot_logo_opacity_=uint8_t(value);
     lv_obj_set_style_text_opa(self->boot_version_,lv_opa_t(value),0);
     lv_obj_invalidate(self->boot_logo_art_);
   });
   lv_anim_set_completed_cb(&animation,[](lv_anim_t* animation) {
     auto* self=static_cast<Ui*>(animation->var);
     // Reveal the UI through the existing black cover. Fade only its
     // background, not the object's group opacity: no full-screen temporary
     // alpha layer is needed on the ESP32. Keep input blocked until finished.
     lv_anim_t reveal; lv_anim_init(&reveal); lv_anim_set_var(&reveal,self);
     lv_anim_set_values(&reveal,255,0); lv_anim_set_duration(&reveal,300);
     lv_anim_set_exec_cb(&reveal,[](void* context,int32_t value) {
       auto* self=static_cast<Ui*>(context);
       lv_obj_set_style_bg_opa(self->boot_logo_,lv_opa_t(value),0);
     });
     lv_anim_set_completed_cb(&reveal,[](lv_anim_t* animation) {
       auto* self=static_cast<Ui*>(animation->var);
       // Ignore contacts begun on the splash until the fingers have lifted.
       for(auto* input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input))
         if(lv_indev_get_type(input)==LV_INDEV_TYPE_POINTER) lv_indev_wait_release(input);
       lv_obj_delete(self->boot_logo_); self->boot_logo_=self->boot_logo_art_=self->boot_version_=nullptr;
       for(auto& image:self->boot_logo_images_) lv_image_cache_drop(&image);
       for(auto& pixels:self->boot_logo_pixels_) std::vector<uint8_t>().swap(pixels);
     });
     lv_anim_start(&reveal);
   });
   lv_anim_start(&animation);
 },3000,this);
 lv_timer_set_repeat_count(timer,1);
}
}

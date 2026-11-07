#include "ui.hpp"
#include "ui_fonts.hpp"
#include "selector_font.hpp"
#include "selector_mask.hpp"
#include <cstdio>
namespace sm {
bool Ui::can_reset_chord_buttons() const {
 return chord_mode_ && can_reset_play_mode();
}
bool Ui::reset_chord_buttons() {
 if(!can_reset_chord_buttons()) return false;
 auto value=app_.chords;
 value.variants.fill(ChordVariant::Triad); value.octave_shifts.fill(0);
 if(value==app_.chords) return true;
 // Save the complete palette atomically; leave it untouched if persistence fails.
 if(!app_.save_chords(value)) return false;
 chord_.stop(); refresh(); return true;
}
namespace {
void plain(lv_obj_t* object) { lv_obj_remove_style_all(object); lv_obj_remove_flag(object,LV_OBJ_FLAG_SCROLLABLE); }
lv_obj_t* caption(lv_obj_t* parent,const char* text,int y,int width,const lv_font_t* font,lv_color_t color) {
 auto* object=lv_label_create(parent); lv_label_set_text(object,text);
 lv_obj_set_width(object,width); lv_obj_set_pos(object,(466-width)/2,y);
 lv_obj_set_style_text_font(object,font,0); lv_obj_set_style_text_color(object,color,0);
 lv_obj_set_style_text_align(object,LV_TEXT_ALIGN_CENTER,0); lv_obj_remove_flag(object,LV_OBJ_FLAG_CLICKABLE);
 return object;
}
}
void Ui::build_chord_editor() {
 chord_editor_=lv_obj_create(lv_screen_active()); plain(chord_editor_);
 lv_obj_set_size(chord_editor_,466,466); lv_obj_set_style_radius(chord_editor_,233,0);
 lv_obj_set_style_clip_corner(chord_editor_,false,0);
 lv_obj_set_style_bg_color(chord_editor_,lv_color_hex(0),0); lv_obj_set_style_bg_opa(chord_editor_,255,0);
 for(int column:{17,18}) {
 const int width=column==17?224:100;
 auto* view=selector_views_[column]=lv_obj_create(chord_editor_); plain(view);
 lv_obj_set_pos(view,column==17?67:299,114); lv_obj_set_size(view,width,238); lv_obj_add_flag(view,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_style_bg_color(view,lv_color_hex(0),0); lv_obj_set_style_bg_opa(view,255,0);
 auto& selector=selectors_[column]; selector.owner=this; selector.column=column; selector.font_size=column==17?28:32;
 lv_obj_add_event_cb(view,selection_event,LV_EVENT_ALL,this);
 lv_obj_add_event_cb(view,[](lv_event_t* event) {
   auto* self=static_cast<Ui*>(lv_event_get_user_data(event));
   self->draw_selector(lv_event_get_layer(event),lv_event_get_target_obj(event)==self->selector_views_[18]?18:17);
 },LV_EVENT_DRAW_MAIN,this);
 for(int bottom=0;bottom<2;++bottom) {
   auto* mask=lv_obj_create(view); plain(mask); lv_obj_remove_flag(mask,LV_OBJ_FLAG_CLICKABLE);
   lv_obj_set_size(mask,width,58); lv_obj_set_pos(mask,0,bottom?180:0);
   lv_obj_set_style_bg_color(mask,lv_color_hex(0),0); lv_obj_set_style_bg_grad_color(mask,lv_color_hex(0),0);
   lv_obj_set_style_bg_opa(mask,255,0); lv_obj_set_style_bg_grad_dir(mask,LV_GRAD_DIR_VER,0);
   lv_obj_set_style_bg_main_opa(mask,bottom?0:255,0); lv_obj_set_style_bg_grad_opa(mask,bottom?255:0,0);
   selector_mask_never_covers(mask);
 }
 }
 chord_edit_title_=caption(chord_editor_,"CHORD",58,310,&barlow_48,accent(.5f));
 add_menu_frame(7,67,203,224,60,30,6,chord_editor_);
 add_menu_frame(9,299,203,100,60,6,30,chord_editor_);
 auto* select=lv_button_create(chord_editor_); plain(select); lv_obj_set_pos(select,117,352); lv_obj_set_size(select,232,62);
 auto* text=caption(select,"SELECT",0,216,&barlow_28,lv_color_hex(0xffffff)); lv_obj_center(text);
 lv_obj_add_event_cb(select,[](lv_event_t* event) { static_cast<Ui*>(lv_event_get_user_data(event))->confirm(); },LV_EVENT_CLICKED,this);
 add_menu_frame(8,117,352,232,62,31,31,chord_editor_);
 chord_edit_error_=caption(chord_editor_,"",327,306,&barlow_14,lv_color_hex(0xffffff));
 lv_obj_add_flag(chord_editor_,LV_OBJ_FLAG_HIDDEN);
}
void Ui::open_chord_editor(int pad) {
 if(!chord_mode_ || pad<0 || pad>=chord_pad_count || app_.selecting || setup_open_ || power_off_visible_ || shutting_down_) return;
 reset_midi_session();
 app_.begin(); chord_edit_pad_=pad; chord_edit_draft_=app_.chords.variants[pad];
 chord_edit_shift_=app_.chords.octave_shifts[pad];
 // Resolve notes/names once. Drawing and dragging must not allocate scale vectors.
 for(int option=0;option<chord_variant_count;++option) {
   const auto chord=chord_for(app_.chords,pad,ChordVariant(option));
   const char* hint=option==0?" (Triad)":option==1?" (Maj/Min)":option==2?" (Dom7)":option==3?" (Scale7)":"";
   chord_edit_options_[option]=chord.name+hint;
   // Cache all native sizes used by the fitting loop. No glyph measurement is
   // needed during dragging or the snap animation, including neighboring rows.
   for(int pixels=18;pixels<=28;++pixels) {
     lv_point_t size;
     lv_text_get_size(&size,chord_edit_options_[option].c_str(),selector_native_font(pixels),0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
     chord_edit_widths_[option][pixels-18]=uint16_t(size.x);
   }
 }
 int fallback_width=0;
 for(const auto& widths:chord_edit_widths_) fallback_width=std::max(fallback_width,int(widths[0]));
 selectors_[17].dirty_width=std::min(224,std::max(192,fallback_width)+8);
 int octave_width=0;
 for(const char* text:{"-1","0","+1"}) {
   lv_point_t size; lv_text_get_size(&size,text,&barlow_32,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
   octave_width=std::max(octave_width,int(size.x));
 }
 selectors_[18].dirty_width=std::min(100,octave_width+8);
 for(int column:{17,18}) {
   auto& selector=selectors_[column]; lv_anim_delete(&selector,nullptr); selector.input=nullptr;
   selector.target=(column==17?int(chord_edit_draft_):chord_edit_shift_+1)*selector.step;
   set_selector_offset(column,selector.target);
 }
 lv_label_set_text(chord_edit_error_,"");
 lv_obj_add_flag(wheel_,LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(chord_editor_,LV_OBJ_FLAG_HIDDEN);
 sync_arp_pause();
}
void Ui::draw_chord_option(int option,char* text,size_t size) const {
 if(chord_edit_pad_<0 || option<0 || option>=chord_variant_count) { text[0]=0; return; }
 std::snprintf(text,size,"%s",chord_edit_options_[option].c_str());
}
}

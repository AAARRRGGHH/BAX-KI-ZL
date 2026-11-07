#pragma once
#include "music.hpp"
#include "motion_cc.hpp"
#include "lvgl.h"
#include "theme.hpp"
#include "synth.hpp"
#include "arpeggiator.hpp"
#include "battery.hpp"
#include "chords.hpp"
#include <memory>
namespace sm {
namespace bounce {class Mode;}
inline constexpr std::array<int,14> arp_columns{{6,7,8,9,10,11,12,13,14,15,20,21,31,32}};
class Ui {
 lv_obj_t* boot_logo_=nullptr;
 lv_obj_t* boot_logo_art_=nullptr;
 lv_obj_t* boot_version_=nullptr;
 uint8_t boot_logo_opacity_=255;
 std::array<std::vector<uint8_t>,2> boot_logo_pixels_{};
 std::array<lv_image_dsc_t,2> boot_logo_images_{};
 Application& app_; NoteEngine note_; Arpeggiator arp_; ChordEngine chord_;
 bool chord_mode_=true;
 MidiRouter& router_;std::shared_ptr<bounce::Mode> bounce_;
 bool bounce_mode_=false,bounce_save_failed_=false;uint32_t bounce_save_retry_=0;
 bounce::Settings bounce_saved_{};Selection bounce_seen_scale_{11,1};
 void build_bounce();void update_bounce();void bounce_touch(lv_event_code_t code);
 void draw_bounce(lv_layer_t* layer);void stop_bounce_output();void cancel_bounce_contacts();
 void activate_bounce(bool active);bool persist_bounce();
 void sync_bounce_clock();bool reset_bounce();
 Selection bounce_scale() const;bool bounce_has_balls() const;
 bounce::Settings kinetic_settings() const;void restore_kinetic(bounce::Settings value);
 ChordSelection chord_draft_{};
 int chord_edit_pad_=-1;
 ChordVariant chord_edit_draft_=ChordVariant::Triad;
 int chord_edit_shift_=0;
 std::array<std::string,chord_variant_count> chord_edit_options_{};
 std::array<std::array<uint16_t,11>,chord_variant_count> chord_edit_widths_{};
 lv_obj_t *chord_editor_=nullptr,*chord_edit_title_=nullptr,*chord_edit_error_=nullptr;
 void build_chord_editor(); void open_chord_editor(int pad);
 void draw_chord_option(int option,char* text,size_t size) const;
 int wheel_count() const { return chord_mode_?chord_pad_count:int(modes()[app_.current.mode].intervals.size()); }
 bool wheel_playable(int degree) const { return chord_mode_ || arp_mode_ || note_.playable(degree); }
 bool arp_mode_=false;
 int arp_page_=0,arp_display_note_=-2;
 lv_obj_t *arp_ratchet_every_=nullptr,*arp_ratchet_count_=nullptr;
 ArpPreset arp_saved_{};
 bool arp_save_ready_=false,arp_save_failed_=false;
 uint32_t arp_save_retry_at_=0;
 bool persist_arpeggiator(bool force=false);
 uint16_t arp_display_selected_=0;
 lv_obj_t *arp_panel_=nullptr,*arp_title_=nullptr,*arp_extra_=nullptr,*arp_error_=nullptr,*arp_octave_dash_=nullptr;
 lv_obj_t* arp_tap_button_=nullptr;
 lv_indev_t* arp_tap_input_=nullptr;
 uint32_t arp_tap_released_=0;
 bool arp_tap_fading_=false;
 void release_arp_tap();
 static void arp_tap_event(lv_event_t* e);
 std::array<lv_obj_t*,13> arp_note_labels_{};
 struct ArpGesture { lv_indev_t* input=nullptr; lv_point_t start{}; int control=-1; bool moved=false; } arp_gesture_;
 void build_arp(); void update_arp(); void arp_touch(lv_event_code_t code);
 void sync_arp_pause();
 void sync_arp_clock();
 int arp_selector_count(int column) const;
 int arp_selector_page(int column) const;
 const char* arp_selector_text(int column,int option,char* buffer,size_t size) const;
 void apply_arp_option(int column,int option);
 void refresh_arp_notes();
 void settle_arp_selectors();
 bool arp_wheel_busy() const;
 bool selector_bounded(int column) const { return column>=4 && column<33 && (column<23 || column>=31) && column!=7 && column!=8 && column!=11 && column!=13 && column!=16; }
 PerformanceOutput* output_=nullptr;
 lv_obj_t *wheel_=nullptr,*selection_=nullptr,*root_=nullptr,*mode_=nullptr,*error_=nullptr;
 lv_obj_t* octave_label_=nullptr;
 struct Contact { lv_indev_t* input=nullptr; int pressed=-1; bool centre=false,tap=false,swiped=false; uint32_t started=0; lv_point_t start{}; };
 std::array<Contact,2> contacts_{};
 std::array<lv_indev_t*,2> blocked_inputs_{};
 bool shutting_down_=false;
 bool power_off_visible_=false;
 lv_obj_t *power_off_screen_=nullptr,*power_off_arc_=nullptr;
 lv_obj_t* power_battery_=nullptr;
 BatteryState battery_{};
 uint32_t battery_updated_=0;
 BatteryDisplay battery_display_{};
 std::array<uint8_t,48*72> battery_outline_{},battery_bolt_{};
 lv_image_dsc_t battery_outline_image_{},battery_bolt_image_{};
 void update_power_battery();
 void draw_power_battery(lv_layer_t* layer);
 void build_power_off();
 void poll_power_off_contacts();
 bool setup_open_=false,power_held_=false,power_pressed_=false;
 enum class PowerContext { Play,Setup,Selection,KineticMenu,SlotDialog };
 PowerContext power_context_=PowerContext::Play;
 int setup_page_=0,slot_index_=0;
 bool slot_occupied_=false;
 std::array<uint8_t,64*64> slot_trash_pixels_{};
 lv_image_dsc_t slot_trash_image_{};
 int slot_dialog_=0;
 MusicPreset slot_draft_{};
 std::array<std::string,music_slot_count> slot_names_{};
 lv_obj_t *slot_dialog_panel_=nullptr,*slot_dialog_name_=nullptr,*slot_confirm_=nullptr,*slot_cancel_=nullptr,*slot_confirm_text_=nullptr;
 std::array<lv_obj_t*,8> slot_letter_views_{};
 void close_slot_dialog();
 void confirm_slot_dialog();
 static void slot_dialog_event(lv_event_t* event);
 lv_obj_t *slots_panel_=nullptr,*slots_title_=nullptr,*slot_save_=nullptr,*slot_load_=nullptr,*slot_delete_=nullptr,*slot_save_text_=nullptr;
 void update_slot();
 void slot_action(int action);
 static void slot_event(lv_event_t* event);
 lv_obj_t *global_panel_=nullptr,*global_toggle_=nullptr,*global_status_=nullptr,*global_title_=nullptr;
 uint32_t motion_now_=0;
 MotionCc motion_cc_; MotionSettings motion_draft_{};
 lv_obj_t *motion_panel_=nullptr,*motion_title_=nullptr,*motion_calibrate_=nullptr,*motion_calibrate_text_=nullptr;
 bool motion_calibrated_feedback_=false;uint32_t motion_calibrated_at_=0;
 void build_motion();void update_motion();bool calibrate_motion();
 void reset_motion_feedback();void update_motion_feedback();
 Setup setup_draft_{};
 lv_obj_t *setup_=nullptr,*setup_title_=nullptr,*channel_panel_=nullptr,
   *color_panel_=nullptr,*color_marker_=nullptr,*color_value_=nullptr,*setup_error_=nullptr,*scale_title_=nullptr;
 lv_obj_t *channel_caption_=nullptr,*clock_caption_=nullptr,*clock_toggle_=nullptr,*clock_status_=nullptr;
 std::array<lv_obj_t*,3> midi_route_buttons_{};
 lv_obj_t *clock_tempo_row_=nullptr,*clock_tempo_value_=nullptr,*clock_tempo_unit_=nullptr;
 uint32_t clock_tempo_updated_=0;
 int clock_tempo_display_=-1;
 void update_clock_tempo(bool force=false);
 struct SetupGesture { lv_indev_t* input=nullptr; lv_point_t start{}; bool color=false,moved=false; lv_indev_t* completed=nullptr; bool channel=false; } setup_gesture_;
 std::vector<uint16_t> color_pixels_;
 lv_image_dsc_t color_image_{};
 void build_setup();
 lv_obj_t *travel_panel_=nullptr,*travel_toggle_=nullptr,*travel_title_=nullptr,*travel_status_=nullptr;
 static void travel_event(lv_event_t* e);
 void build_travel();
 std::vector<uint8_t> waveform_pixels_;
 std::array<lv_image_dsc_t,waveform_count*4> waveform_images_{};
 void build_waveforms();
 void sync_travel();
 int selector_count(int column) const;
 void update_color_preview();
 void update_setup();
 void set_setup_page(int page);
 void set_setup_color(lv_point_t point);
 void recolor_scale_menu();
 static void setup_event(lv_event_t* e);
 std::array<uint8_t,13> highlight_{},rendered_highlight_{};
 std::array<uint32_t,13> release_time_{};
 std::array<bool,13> fading_{};
 void release_segment(int source);
 void release_contact(int source);
 void update_feedback();
 bool tap_pending_=false;
 uint32_t last_tap_time_=0;
 lv_point_t last_tap_point_{};
 std::vector<uint32_t> wheel_pixels_;
 struct WheelPixel { uint32_t offset; uint8_t outline,coverage,radial; };
 std::array<std::vector<WheelPixel>,13> segment_pixels_;
 std::array<lv_area_t,13> segment_bounds_{};
 int geometry_count_=-1;
 void invalidate_segment(int index);
 struct MenuFrame { std::vector<uint32_t> pixels; lv_image_dsc_t image{}; lv_obj_t* object=nullptr; };
 std::array<MenuFrame,10> slot_dialog_frames_{};
 std::array<MenuFrame,3> slot_frames_{};
 std::array<MenuFrame,3> motion_frames_{};
 std::array<MenuFrame,10> menu_frames_{};
 void selection_layout();
 std::array<MenuFrame,14> arp_frames_{};
 MenuFrame strum_frame_{};
 MenuFrame arp_tap_frame_{};
 void make_menu_frame(MenuFrame& frame,int x,int y,int width,int height,float left_radius,float right_radius,lv_obj_t* parent,lv_color_t tint);
 void add_menu_frame(size_t index,int x,int y,int width,int height,float left_radius,float right_radius,lv_obj_t* parent=nullptr);
 ColorScheme palette() const { const auto c=setup_open_?setup_draft_.accent:app_.setup.accent; return {"Custom",c,c,0xffffff,0xffffff}; }
 lv_color_t accent(float x) const;
 lv_image_dsc_t wheel_image_{};
 int rendered_count_=-1; uint16_t rendered_playable_=0,rendered_selected_=0;
 std::array<lv_obj_t*,36> selector_views_{};
 struct Selector { Ui* owner=nullptr; lv_indev_t* input=nullptr; int column=0,offset=0,target=0,last_y=0,start_y=0,step=60,half_height=119,font_size=32,center=233,dirty_width=0; uint32_t last_move=0; int velocity=0,drag_remainder=0; bool dragged=false; };
 std::array<Selector,36> selectors_{};
 void set_selector_offset(int column,int offset);
 void animate_selector(int column,int target);
 void draw_selector(lv_layer_t* layer,int column);
 std::array<int,36> row_offsets_=[] { std::array<int,36> a{}; a.fill(-99999); return a; }();
 static void wheel_event(lv_event_t* e);
 static void selection_event(lv_event_t* e);
 void draw(lv_layer_t* layer);
 void render_wheel();
 int hit(int x,int y,bool include_unplayable=false) const;
 void touch(lv_event_code_t code);
public:
 Ui(Application& a,MidiRouter& m,PerformanceOutput* output=nullptr):app_(a),note_(m,a.current),arp_(m),chord_(m),router_(m),output_(output){}
 void motion_sample(float x,float y,float z,uint32_t now){motion_cc_.sample(x,y,z,now);}
 void motion_tick(uint32_t now){motion_now_=now;motion_cc_.configure(app_.motion);motion_cc_.update(now,!setup_open_ && !app_.selecting && !is_bounce_menu() && !booting() && !shutting_down_ && !app_.setup.travel,[this](MidiEvent e){return router_.control(e);});}
 void performance_tick(uint64_t now);
 void midi_realtime(uint8_t status,uint64_t now);void midi_disconnected();
 bool is_bounce()const{return bounce_mode_;}
 bool toggle_bounce_menu();bool is_bounce_menu()const;
 bool is_chords() const { return chord_mode_; }
 bool can_reset_chord_buttons() const;
 bool reset_chord_buttons();
 bool can_reset_play_mode() const;
 bool has_resettable_state() const;
 bool reset_play_mode();
 bool is_editing_chord() const { return chord_edit_pad_>=0; }
 int edited_chord() const { return chord_edit_pad_; }
 Arpeggiator& arpeggiator() { return arp_; }
 ChordEngine& chord_engine() { return chord_; }
 bool is_arpeggiator() const { return arp_mode_; }
 int arp_page() const { return arp_page_; }
 void toggle_arpeggiator();
 void build(); void refresh(bool preserve_feedback=false); void open(); void back(); bool confirm();
 void show_boot_logo();
 bool booting() const { return boot_logo_!=nullptr; }
 void scroll(int x,int y,int delta);
 void cancel_touch();
 void reset_midi_session();
 void button(int index,ButtonEvent event);
 void power_button(ButtonEvent event);
 void open_setup();
 void cancel_setup();
 bool confirm_setup();
 bool is_setup() const { return setup_open_; }
 int setup_page() const { return setup_page_; }
 uint32_t accent_color() const { return app_.setup.accent; }
 bool change_octave(int delta);
 bool prepare_shutdown();
 void cancel_shutdown() { shutting_down_=false; cancel_power_off(); }
 void begin_power_off();
 void set_battery(BatteryState value);
 bool update_power_off(uint32_t held_ms);
 void cancel_power_off();
 bool power_off_visible() const { return power_off_visible_; }
 int octave() const { return note_.octave(); }
 bool is_selecting() const { return app_.selecting; }
};
}

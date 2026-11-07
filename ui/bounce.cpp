#include "ui.hpp"
#include "bounce_mode.hpp"
#include "bounce_diagnostics.hpp"
#include <cstdio>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
namespace sm {
void Ui::build_bounce(){
#ifdef ESP_PLATFORM
 auto* memory=heap_caps_malloc(sizeof(bounce::Mode),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(memory)bounce_=std::shared_ptr<bounce::Mode>(new(memory)bounce::Mode(router_),[](bounce::Mode* p){p->~Mode();heap_caps_free(p);});
#else
 bounce_=std::make_shared<bounce::Mode>(router_);
#endif
 if(!bounce_){std::fprintf(stderr,"Bounce allocation failed\n");return;}
#ifdef ESP_PLATFORM
 bounce::diagnostics::attach(lv_display_get_default());
#endif
 auto config=app_.load_bounce();if(!bounce::valid_settings(config))config={};bounce_->settings(config);bounce_saved_=config;bounce_seen_scale_=config.scale;
}
bounce::Settings Ui::kinetic_settings() const{return bounce_?bounce_->settings():bounce_saved_;}
void Ui::restore_kinetic(bounce::Settings value){if(bounce_){bounce_->reset();bounce_->settings(value);}bounce_saved_=value;bounce_seen_scale_=value.scale;bounce_save_failed_=false;}
void Ui::performance_tick(uint64_t now){arp_.tick(now);chord_.tick(now,arp_.clock_quarter_us());if(bounce_)bounce_->tick(now);}
void Ui::midi_realtime(uint8_t status,uint64_t now){arp_.realtime(status,now);if(bounce_)bounce_->realtime(status,now);}
void Ui::midi_disconnected(){arp_.disconnected();if(bounce_)bounce_->disconnected();}
void Ui::sync_bounce_clock(){if(bounce_)bounce_->clock_sync(app_.setup.receive_clock);}
bool Ui::bounce_has_balls()const{return bounce_ && bounce_->has_balls();}
bool Ui::reset_bounce(){if(!bounce_)return false;bounce_->reset();persist_bounce();lv_obj_invalidate(wheel_);return true;}
void Ui::activate_bounce(bool active){
 if(!bounce_)return;
 if(active && app_.setup.global_scale){auto s=bounce_->settings();s.scale=app_.current;bounce_->settings(s);bounce_seen_scale_=s.scale;}
 bounce_->activate(active);if(!active)bounce_->release_view();
}
bool Ui::persist_bounce(){
 if(!bounce_){return true;}
 const auto value=bounce_->settings();if(value==bounce_saved_){bounce_save_failed_=false;return true;}
 if(app_.save_bounce(value)){bounce_saved_=value;bounce_save_failed_=false;return true;}
 bounce_save_failed_=true;bounce_save_retry_=lv_tick_get();std::fprintf(stderr,"Bounce settings could not be saved\n");return false;
}
void Ui::update_bounce(){
 if(!bounce_)return;
 const uint32_t now=lv_tick_get();bounce_->suspend(setup_open_ || app_.selecting || shutting_down_ || booting());bounce_->ui_tick(now);
 if(!bounce_mode_)return;
 bounce::diagnostics::report(now);
 auto s=bounce_->settings();
 if(app_.setup.global_scale){
   if(s.scale!=bounce_seen_scale_){app_.begin();app_.draft=s.scale;if(!app_.select()){app_.back();s.scale=app_.current;bounce_->settings(s);}}
   else if(s.scale!=app_.current){s.scale=app_.current;bounce_->settings(s);}
 }
 bounce_seen_scale_=s.scale;
 if(!bounce_save_failed_ || uint32_t(now-bounce_save_retry_)>=1000)persist_bounce();
 if(!setup_open_ && !app_.selecting && !power_off_visible_ && !shutting_down_)bounce_->invalidate(wheel_);
}
void Ui::draw_bounce(lv_layer_t* layer){if(bounce_)bounce_->draw(layer,app_.setup.accent);}
void Ui::cancel_bounce_contacts(){if(bounce_)bounce_->cancel_contacts();}
void Ui::stop_bounce_output(){if(bounce_){bounce_->suspend(setup_open_ || app_.selecting || shutting_down_ || booting());bounce_->silence();}}
bool Ui::toggle_bounce_menu(){if(!bounce_mode_ || !bounce_ || booting() || setup_open_ || power_off_visible_ || shutting_down_)return false;bounce_->toggle_menu();lv_obj_invalidate(wheel_);return true;}
bool Ui::is_bounce_menu()const{return bounce_mode_ && bounce_ && bounce_->menu_open();}
Selection Ui::bounce_scale()const{return bounce_?bounce_->settings().scale:app_.current;}
void Ui::bounce_touch(lv_event_code_t code){
 if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_PRESSING && code!=LV_EVENT_RELEASED && code!=LV_EVENT_PRESS_LOST)return;
 auto* input=lv_indev_active();if(!input || !bounce_)return;
 for(auto*& blocked:blocked_inputs_)if(blocked==input){if(code==LV_EVENT_PRESSED)blocked=nullptr;else{if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST)blocked=nullptr;return;}}
 int contact=-1;for(int i=0;i<2;++i)if(contacts_[i].input==input)contact=i;
 bool began=false;if(contact<0 && (code==LV_EVENT_PRESSED || code==LV_EVENT_PRESSING))for(int i=0;i<2;++i)if(!contacts_[i].input){contact=i;contacts_[i].input=input;began=true;break;}
 if(contact<0){return;}
 lv_point_t p;lv_indev_get_point(input,&p);const bounce::Vec point{double(p.x),double(p.y)};
 if(code==LV_EVENT_PRESSED || began)bounce_->down(contact,point,lv_tick_get());
 else if(code==LV_EVENT_PRESSING)bounce_->move(contact,point,lv_tick_get());
 else{bounce_->up(contact,point,lv_tick_get());contacts_[contact]={};}
}
}

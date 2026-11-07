#include "bounce_mode.hpp"
#include "bounce_diagnostics.hpp"
#include <algorithm>
#include <chrono>
namespace sm::bounce {
Mode::Mode(MidiRouter& router):output_(router),world_(output_,uint32_t(std::chrono::steady_clock::now().time_since_epoch().count())),menu_(world_),gesture_([this](TouchGesture::Action a){input(a);}){}
void Mode::input(TouchGesture::Action a){
 if(a.kind==TouchGesture::Kind::Menu){toggle_menu();return;}
 auto& c=contacts_[a.contact];const Vec arena{a.p.x-233,a.p.y-233};
 if(a.kind==TouchGesture::Kind::Down){c.menu=menu_.opened;if(c.menu){if(menu_contact_<0){menu_contact_=a.contact;menu_.down(a.p);}return;}c.ball=world_.grab(arena);c.motion.down(arena,a.time);return;}
 if(c.menu){if(a.contact!=menu_contact_)return;if(a.kind==TouchGesture::Kind::Move)menu_.move(a.p);else{menu_.up(a.p,a.time);menu_contact_=-1;}return;}
 if(c.ball<0){return;}
 if(a.kind==TouchGesture::Kind::Move){world_.move(c.ball,arena);c.motion.move(arena,a.time);}
 if(a.kind==TouchGesture::Kind::Up){
   const Vec v=c.motion.velocity(a.time);
   diagnostics::flick(float(std::hypot(v.x,v.y)));world_.fling(c.ball,v);c.ball=-1;
 }
}
void Mode::cancel_contacts(){std::lock_guard<std::recursive_mutex> lock(mutex_);for(auto& c:contacts_){world_.release(c.ball,{});c={};}gesture_.cancel(last_ui_);menu_.cancel();menu_contact_=-1;}
void Mode::activate(bool value){std::lock_guard<std::recursive_mutex> lock(mutex_);cancel_contacts();if(active_ && !value)world_.clear();active_=value;diagnostics::enable(value);paused_=false;accumulator_=0;repaint_all_=true;if(menu_.opened)menu_.toggle();view_.capture(View::snapshot(world_));}
void Mode::suspend(bool value){std::lock_guard<std::recursive_mutex> lock(mutex_);if(value && !paused_){cancel_contacts();world_.silence();}paused_=value;}
void Mode::silence(){std::lock_guard<std::recursive_mutex> lock(mutex_);cancel_contacts();world_.silence();}
void Mode::release_view(){std::lock_guard<std::recursive_mutex> lock(mutex_);view_.release();}
bool Mode::has_balls(){std::lock_guard<std::recursive_mutex> lock(mutex_);return std::any_of(world_.balls.begin(),world_.balls.end(),[](const Ball& b){return b.alive;});}
void Mode::reset(){std::lock_guard<std::recursive_mutex> lock(mutex_);cancel_contacts();world_.clear();menu_.sync();accumulator_=0;repaint_all_=true;view_.capture(View::snapshot(world_));}
void Mode::clock_sync(bool enabled){std::lock_guard<std::recursive_mutex> lock(mutex_);if(world_.clock_enabled()==enabled)return;world_.clock_sync(enabled);menu_.sync();repaint_all_=true;}
void Mode::realtime(uint8_t status,uint64_t now){std::lock_guard<std::recursive_mutex> lock(mutex_);world_.realtime(status,now);}
void Mode::disconnected(){std::lock_guard<std::recursive_mutex> lock(mutex_);world_.realtime(0xff,last_performance_);}
void Mode::tick(uint64_t now){
 diagnostics::Scope measurement(diagnostics::Tick);
 std::lock_guard<std::recursive_mutex> lock(mutex_);world_.clock_tick(now);if(!last_performance_ || now<last_performance_){last_performance_=now;return;}const double dt=std::min(.05,double(now-last_performance_)/1000000);last_performance_=now;
 if(!active_ || paused_){world_.retry();return;}accumulator_+=dt;while(accumulator_>=1./240){world_.step(1./240);accumulator_-=1./240;}
}
void Mode::ui_tick(uint32_t now){
 View::Frame frame;
 {std::lock_guard<std::recursive_mutex> lock(mutex_);const double dt=last_ui_?std::min(.05,uint32_t(now-last_ui_)/1000.):0;last_ui_=now;if(active_ && !paused_)gesture_.tick(now);menu_.tick(dt);frame=View::snapshot(world_);}
 view_.capture(frame);
 diagnostics::scene(uint32_t(std::count_if(frame.balls.begin(),frame.balls.end(),[](const Ball& b){return b.alive;})));
}
void Mode::down(int c,Vec p,uint32_t now){diagnostics::touch(c,now);std::lock_guard<std::recursive_mutex> lock(mutex_);if(active_ && !paused_)gesture_.down(c,p,now);}
void Mode::move(int c,Vec p,uint32_t now){diagnostics::touch(c,now);std::lock_guard<std::recursive_mutex> lock(mutex_);gesture_.move(c,p,now);}
void Mode::up(int c,Vec p,uint32_t now){diagnostics::touch(c,now);diagnostics::end_touch(c);std::lock_guard<std::recursive_mutex> lock(mutex_);gesture_.up(c,p,now);}
void Mode::toggle_menu(){std::lock_guard<std::recursive_mutex> lock(mutex_);cancel_contacts();menu_.toggle();repaint_all_=true;}
bool Mode::menu_open(){std::lock_guard<std::recursive_mutex> lock(mutex_);return menu_.opened;}
void Mode::scroll(int x,int y,int delta){std::lock_guard<std::recursive_mutex> lock(mutex_);menu_.scroll({double(x),double(y)},delta);}
Settings Mode::settings(){std::lock_guard<std::recursive_mutex> lock(mutex_);return world_.settings();}
void Mode::collision_handler(CollisionHandler* handler){std::lock_guard<std::recursive_mutex> lock(mutex_);world_.collision_handler(handler);}
void Mode::settings(Settings s){std::lock_guard<std::recursive_mutex> lock(mutex_);if(world_.settings(s))menu_.sync();}
void Mode::draw(lv_layer_t* layer,uint32_t accent){diagnostics::Scope measurement(diagnostics::Paint);menu_.accent(accent);if(menu_.opened)menu_.draw(layer);else{const auto before=view_.work();view_.draw(layer,accent);const auto after=view_.work();diagnostics::work(after.scene_blits-before.scene_blits);}}
void Mode::invalidate(lv_obj_t* object){diagnostics::Scope measurement(diagnostics::Damage);if(repaint_all_){lv_obj_invalidate(object);view_.reset_damage();menu_.reset_damage();repaint_all_=false;}else if(menu_.opened)menu_.invalidate_regions(object);else view_.invalidate_regions(object);}
View::Frame Mode::snapshot(){std::lock_guard<std::recursive_mutex> lock(mutex_);return View::snapshot(world_);}
}

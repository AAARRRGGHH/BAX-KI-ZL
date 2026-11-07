#pragma once
#include "world.hpp"
#include "menu.hpp"
#include "view.hpp"
#include "touch_gesture.hpp"
#include "flick_tracker.hpp"
#include <mutex>
namespace sm::bounce {
class Mode {
 struct Output:MidiTransport{MidiRouter& router;explicit Output(MidiRouter& r):router(r){}bool send(MidiEvent e)override{return router.auxiliary(e);}} output_;
 World world_;Menu menu_;View view_;std::recursive_mutex mutex_;TouchGesture gesture_;
 struct Contact{int ball=-1;bool menu=false;FlickTracker motion;};
 std::array<Contact,2> contacts_{};
 bool active_=false,paused_=false,repaint_all_=true;int menu_contact_=-1;uint64_t last_performance_=0;uint32_t last_ui_=0;double accumulator_=0;
 void input(TouchGesture::Action action);
public:
 explicit Mode(MidiRouter& router);
 void activate(bool active);void suspend(bool paused);void tick(uint64_t now);void ui_tick(uint32_t now);
 void cancel_contacts();void silence();void release_view();
 bool has_balls();void reset();void clock_sync(bool enabled);void realtime(uint8_t status,uint64_t now);void disconnected();
 void down(int contact,Vec p,uint32_t now);void move(int contact,Vec p,uint32_t now);void up(int contact,Vec p,uint32_t now);
 void toggle_menu();bool menu_open();void scroll(int x,int y,int delta);
 void invalidate(lv_obj_t* object);
 Settings settings();void settings(Settings s);void draw(lv_layer_t* layer,uint32_t accent);
 // Caller owns the optional handler and must unregister it before destruction.
 void collision_handler(CollisionHandler* handler);
 View::Frame snapshot();
};
}

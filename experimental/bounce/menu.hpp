#pragma once
#include "world.hpp"
#include "lvgl.h"
#include <vector>
namespace sm::bounce {
class Menu {
 World& world_;
 enum Field {Root,Mode,Low,High,Bpm,Division,Timing,Gate,Energy};
 struct Wheel {Field field=Root;int x=0,width=0,cy=233;double offset=0,target=0;};
 struct Frame {std::vector<uint32_t> pixels;lv_image_dsc_t image{};int width=0,left=-1,right=-1;};
 std::array<Wheel,2> wheels_{};std::array<Frame,2> frames_{};
 int wheel_count_=0,drag_wheel_=-1,axis_=0;bool pressed_=false;
 Vec start_{},last_{};double drag_offset_=0,tap_flash_=0;
 uint64_t last_tap_=0;std::array<uint64_t,4> tap_intervals_{};unsigned tap_count_=0;
 uint32_t accent_=sm::default_accent;
 int painted_page_=-1;std::array<double,2> painted_offsets_{};double painted_flash_=0;bool painted_quantized_=true;
 int count(Field f) const;int value(Field f) const;bool looping(Field f) const;
 void apply(Field f,int option);void layout();void tap(uint64_t now);
 void frame(lv_layer_t* layer,int index,const Wheel& wheel,int left,int right);
public:
 explicit Menu(World& world):world_(world){layout();}
 bool opened=false;int page=0;
 void accent(uint32_t value){if(value!=accent_){accent_=value;for(auto& f:frames_)f.width=0;}}
 void sync(){cancel();if(world_.clock_enabled() && page==2)page=3;layout();}
 void toggle();void navigate(int delta);
 void down(Vec p);void move(Vec p);void up(Vec p,uint64_t now);void cancel();
 void scroll(Vec p,int direction);
 void tick(double dt);void draw(lv_layer_t* layer);
 void invalidate_regions(lv_obj_t* object);void reset_damage();
};
}

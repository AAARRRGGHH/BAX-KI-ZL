#pragma once
#include "world.hpp"
#include "lvgl.h"
#include "render_memory.hpp"
namespace sm::bounce {
class View {
public:
 struct Work{uint32_t scene_blits=0,ring_plan_builds=0;};
 struct Frame {std::array<Ball,max_balls> balls{};std::array<double,7> flashes{};int count=7;};
 static Frame snapshot(const World& world){return {world.balls,world.flashes(),world.segment_count()};}
private:
 struct Tile {lv_area_t area{};uint8_t segments=0;RenderVector<uint8_t> coverage,owners;RenderVector<uint16_t> pixels;};
 RenderVector<Tile> tiles_;int geometry_count_=0;
 struct RingPlan{bool ready=false;RenderVector<lv_area_t> areas;};
 std::array<RingPlan,128> ring_plans_{};
 std::array<uint32_t,7> colors_{};std::array<std::array<uint16_t,256>,7> palette_{};
 RenderVector<uint16_t> scene_;lv_image_dsc_t scene_image_{};bool scene_ready_=false;
 std::array<RenderVector<uint8_t>,13> circles_{};
 std::array<lv_area_t,max_balls> ball_bounds_{};std::array<bool,max_balls> painted_balls_{};Work work_{};
 bool dirty_=true,have_previous_=false;uint32_t accent_=0;Frame current_{},previous_{};
 void geometry();void prepare(uint32_t accent);
 void composite(uint32_t accent,bool rebuilt,uint8_t changed);
 const RenderVector<lv_area_t>& ring_plan(uint8_t changed);
 static uint32_t ring_color(double flash,uint32_t accent);
public:
 void draw(lv_layer_t* layer,const World& world,uint32_t accent=sm::default_accent){if(dirty_)current_=snapshot(world);draw(layer,accent);}
 void draw(lv_layer_t* layer,uint32_t accent=sm::default_accent);
 void capture(Frame frame){current_=frame;dirty_=true;}
 void invalidate_regions(lv_obj_t* object);
 void reset_damage(){previous_=current_;have_previous_=true;}
 void release();void invalidate(){dirty_=true;}
 // Independent full composition used to verify the incremental compositor.
 void rebuild_scene(){scene_ready_=false;dirty_=true;}
 Work work()const{return work_;}
};
}

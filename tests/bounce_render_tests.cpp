#include "view.hpp"
#include "bounce_mode.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace sm;using namespace sm::bounce;
namespace {
uint32_t now=1;uint64_t transferred=0;std::vector<uint16_t> screen(466*466);
 Menu* active_menu=nullptr;
uint32_t ticks(){return now;}
void flush(lv_display_t* d,const lv_area_t* a,uint8_t* bytes){const auto* pixels=reinterpret_cast<uint16_t*>(bytes);const int width=a->x2-a->x1+1;for(int y=a->y1;y<=a->y2;++y){std::copy_n(pixels,width,screen.begin()+y*466+a->x1);pixels+=width;transferred+=width;}lv_display_flush_ready(d);}
struct Output:MidiTransport{bool send(MidiEvent)override{return true;}};
void require(bool ok,int line){if(!ok){std::fprintf(stderr,"Bounce render/motion failed at %d\n",line);std::exit(1);}}
#define REQUIRE(...) require((__VA_ARGS__),__LINE__)
}
int main(){
 lv_init();lv_tick_set_cb(ticks);auto* display=lv_display_create(466,466);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);std::vector<uint16_t> buffer(466*20);lv_display_set_buffers(display,buffer.data(),nullptr,uint32_t(buffer.size()*2),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
 auto* root=lv_screen_active();lv_obj_set_style_bg_color(root,lv_color_hex(0),0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
 auto* panel=lv_obj_create(root);lv_obj_remove_style_all(panel);lv_obj_set_size(panel,466,466);lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
 View view;lv_obj_add_event_cb(panel,[](lv_event_t* e){if(active_menu)active_menu->draw(lv_event_get_layer(e));else static_cast<View*>(lv_event_get_user_data(e))->draw(lv_event_get_layer(e));},LV_EVENT_DRAW_MAIN,&view);
 Output out;World world(out);
 auto partial=[&](){now+=17;view.capture(View::snapshot(world));view.invalidate_regions(panel);lv_refr_now(display);};
 partial();const auto before=transferred;for(int frame=0;frame<30;++frame){for(int k=0;k<4;++k)world.step(1./240);partial();}
 const auto pixels_per_frame=(transferred-before)/30;REQUIRE(pixels_per_frame==0); // Static white ring requires no refresh.
 int ball=world.grab({0,0});world.release(ball,{300,-450});ball=world.grab({80,-60});world.release(ball,{-390,200});
 for(int frame=0;frame<180;++frame){
   for(int k=0;k<4;++k)world.step(1./240);partial();
   if(frame%11==0){const auto expected=screen;view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);if(screen!=expected){size_t differences=0;for(size_t i=0;i<screen.size();++i)if(screen[i]!=expected[i]){if(differences<12)std::fprintf(stderr,"diff (%zu,%zu): partial=%u full=%u\n",i%466,i/466,unsigned(expected[i]),unsigned(screen[i]));++differences;}std::fprintf(stderr,"Partial rendering differs: frame=%d pixels=%zu\n",frame,differences);REQUIRE(false);}}
 }
 // The held-ball pickup animation must invalidate its old and new pixels,
 // including simultaneous growth, without leaving a copy behind on hardware.
 world.clear();const int picked=world.grab({0,0});world.release(picked,{});world.balls[picked].energy=.04;partial();REQUIRE(world.grab({40,0})==picked);
 for(int frame=0;frame<12;++frame){for(int k=0;k<3;++k)world.step(1./240);partial();const auto expected_pickup=screen;view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);REQUIRE(screen==expected_pickup);}
 auto scale=world.settings();scale.scale={0,9};world.settings(scale);partial();const auto expected=screen;view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);REQUIRE(screen==expected);
 // A deliberately sparse 300 ms touch sample must retain its flick velocity.
 MidiRouter router(out);sm::bounce::Mode mode(router);mode.activate(true);mode.tick(1000);mode.down(0,{233,233},1);mode.move(0,{233,113},301);mode.up(0,{460,10},321);
 auto first=mode.snapshot();int live=-1;for(size_t i=0;i<max_balls;++i)if(first.balls[i].alive)live=i;const auto expected_fling=World::fling_velocity({0,-400});REQUIRE(live>=0 && !first.balls[live].held && std::hypot(first.balls[live].v.x-expected_fling.x,first.balls[live].v.y-expected_fling.y)<.0001);
 for(uint64_t t=2000;t<=102000;t+=1000)mode.tick(t);auto second=mode.snapshot();REQUIRE(std::hypot(second.balls[live].p.x-first.balls[live].p.x,second.balls[live].p.y-first.balls[live].p.y)>2);
 mode.activate(false);mode.activate(true);mode.down(0,{233,233},1000);mode.move(0,{233,113},1040);mode.move(0,{233,113},1300);mode.up(0,{233,113},1320);first=mode.snapshot();for(const auto& b:first.balls)if(b.alive)REQUIRE(std::hypot(b.v.x,b.v.y)==0);
 mode.activate(false);
 View::Frame center_frame;center_frame.balls[0].alive=true;center_frame.balls[0].p={0,0};center_frame.balls[0].whiteness=255;view.capture(center_frame);view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);
 REQUIRE(screen[233*466+233]==0xffff && screen[8*466+233]==0xffff);
 const auto before_work=view.work();lv_area_t center{220,220,246,246};lv_obj_invalidate_area(panel,&center);lv_refr_now(display);const auto after_work=view.work();
 REQUIRE(after_work.scene_blits-before_work.scene_blits==1);
 center_frame.balls[0].retiring=true;center_frame.balls[0].fade_elapsed=Ball::fade_seconds/2;view.capture(center_frame);view.invalidate_regions(panel);lv_refr_now(display);REQUIRE(screen[233*466+233]>0 && screen[233*466+233]<0xffff);
 // A countdown alone never redraws or dims the indicator. Only its two
 // state changes transfer ring pixels; the ball's end-of-life fade is separate.
 View::Frame pulse;pulse.flashes[0]=World::flash_seconds;view.capture(pulse);view.invalidate_regions(panel);lv_refr_now(display);REQUIRE(screen[8*466+233]==lv_color_to_u16(lv_color_hex(sm::default_accent)));
 const auto held_pixels=transferred;for(int frame=1;frame<=10;++frame){pulse.flashes[0]=World::flash_seconds-frame*.008;view.capture(pulse);view.invalidate_regions(panel);lv_refr_now(display);REQUIRE(transferred==held_pixels && screen[8*466+233]==lv_color_to_u16(lv_color_hex(sm::default_accent)));}
 pulse.flashes[0]=0;view.capture(pulse);view.invalidate_regions(panel);lv_refr_now(display);REQUIRE(screen[8*466+233]==0xffff && transferred>held_pixels);
 View::Frame many;for(size_t i=0;i<max_balls;++i){many.balls[i].alive=true;many.balls[i].p={double((int(i)%3-1)*60),double((int(i)/3)*90-45)};}
 view.capture(many);view.reset_damage();view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);
 for(auto& b:many.balls){b.p.x+=1;b.p.y+=1;}
 const auto many_before=view.work();view.capture(many);view.invalidate_regions(panel);lv_refr_now(display);const auto many_after=view.work();
 REQUIRE(many_after.scene_blits-many_before.scene_blits<=12);
 const auto many_pixels=screen;view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);REQUIRE(screen==many_pixels);

 std::printf("6 moving balls: LVGL image tasks=%u\n",many_after.scene_blits-many_before.scene_blits);
 const auto feedback_before=transferred;const auto feedback_work=view.work();
 for(int frame=0;frame<24;++frame){
   for(auto& f:many.flashes)f=frame<6?World::flash_seconds-frame*.017:0.;
   for(auto& b:many.balls){b.p.x+=.5;b.p.y+=.25;}
   view.capture(many);view.invalidate_regions(panel);lv_refr_now(display);
   if(frame==0)REQUIRE(screen[8*466+233]==lv_color_to_u16(lv_color_hex(sm::default_accent)));
 }
 const auto feedback_pixels=(transferred-feedback_before)/24;const auto feedback_after=view.work();
 REQUIRE(feedback_after.ring_plan_builds-feedback_work.ring_plan_builds<=1); // ON/OFF reuse the same ring geometry.
 const auto feedback_screen=screen;view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);REQUIRE(screen==feedback_screen);
 std::printf("6 balls + seven 100ms hit indicators: pixels/frame=%llu LVGL image tasks/frame=%u\n",static_cast<unsigned long long>(feedback_pixels),(feedback_after.scene_blits-feedback_work.scene_blits)/24);
 REQUIRE(feedback_pixels<50000);
 // Vary segment count with large disjoint ball movements and fade-out,
 // exercising geometry rebuilds, cache disposal and the invalid-area budget.
 for(int count:{5,6,7})for(int frame=0;frame<60;++frame){
   many.count=count;
   for(int i=0;i<7;++i)many.flashes[i]=frame%4==0?0.:double((i+frame)%9)/9;
   for(size_t i=0;i<max_balls;++i){auto& b=many.balls[i];const double angle=(int(i)*60+frame*11)*3.141592653589793/180;b.p={190*std::cos(angle),190*std::sin(angle)};b.energy=.05+.95*((int(i)+frame)%11)/10.;b.whiteness=i%2?255:0;b.held=frame%5==0;b.retiring=frame%20>=10;b.fade_elapsed=(frame%10)*Ball::fade_seconds/10;}
   view.capture(many);view.invalidate_regions(panel);lv_refr_now(display);const auto swept=screen;view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);REQUIRE(screen==swept);
 }
 View::Frame empty;for(int count:{5,6,7})for(int frame=0;frame<180;++frame){empty.count=count;view.capture(empty);view.invalidate_regions(panel);lv_refr_now(display);const auto swept=screen;view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);REQUIRE(screen==swept);}
 // Restore ring pixels beneath a departing/fading ball and preserve overlap
 // compositing when two balls have different colors and opacities.
 for(int phase=0;phase<12;++phase){View::Frame overlap;overlap.balls[0].alive=phase<10;overlap.balls[0].p={0.,phase<4?-213.:-180.};overlap.balls[0].retiring=true;overlap.balls[0].fade_elapsed=phase*.1;overlap.balls[1]=overlap.balls[0];overlap.balls[1].p.x=phase*2.;overlap.balls[1].whiteness=255;
   view.capture(overlap);view.invalidate_regions(panel);lv_refr_now(display);const auto partial_pixels=screen;view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);REQUIRE(screen==partial_pixels);
 }
 Menu menu(world);menu.toggle();active_menu=&menu;menu.invalidate_regions(panel);lv_refr_now(display);
 const auto idle_pixels=transferred;for(int i=0;i<10;++i){now+=17;menu.tick(.017);menu.invalidate_regions(panel);lv_refr_now(display);}REQUIRE(transferred==idle_pixels);
 auto check_menu=[&](){now+=17;menu.tick(.017);menu.invalidate_regions(panel);lv_refr_now(display);const auto partial_pixels=screen;view.rebuild_scene();lv_obj_invalidate(panel);lv_refr_now(display);REQUIRE(screen==partial_pixels);};
 menu.scroll({140,233},-1);for(int i=0;i<10;++i)check_menu();menu.navigate(2);check_menu();menu.down({300,233});menu.up({300,233},now);for(int i=0;i<10;++i)check_menu();menu.navigate(1);check_menu();menu.down({233,173});menu.up({233,173},now);check_menu();active_menu=nullptr;
 view.release();lv_display_delete(display);lv_deinit();std::printf("Bounce sparse flick + delayed release + stationary hold + culling + exact partial/full pixels PASS; idle ring pixels/frame=%llu (full=217156)\n",static_cast<unsigned long long>(pixels_per_frame));
}

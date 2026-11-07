#include "world.hpp"
#include "menu.hpp"
#include "view.hpp"
#include "touch_gesture.hpp"
#include "flick_tracker.hpp"
#include "local_audio.hpp"
#include "device_mask.hpp"
#include "lvgl.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>
namespace {
SDL_Renderer* renderer=nullptr;SDL_Texture* texture=nullptr;
sm::bounce::Menu* menu_view=nullptr;sm::bounce::View visual;
struct AudioOutput:sm::MidiTransport {
 DesktopAudio audio;unsigned strikes=0;
 bool send(sm::MidiEvent e)override{if(e.kind==sm::MidiKind::NoteOn)++strikes;return audio.send(e);}
};
void flush(lv_display_t* display,const lv_area_t* area,uint8_t* pixels){
 SDL_Rect r{area->x1,area->y1,area->x2-area->x1+1,area->y2-area->y1+1};
 SDL_UpdateTexture(texture,&r,pixels,r.w*4);lv_display_flush_ready(display);
}
void draw(lv_event_t* event){
 auto& world=*static_cast<sm::bounce::World*>(lv_event_get_user_data(event));auto* layer=lv_event_get_layer(event);
 if(menu_view && menu_view->opened)menu_view->draw(layer);else visual.draw(layer,world);
}

}
int main(int argc,char** argv){
 bool smoke=false;std::string screenshot;int menu_page=-1;
 for(int i=1;i<argc;++i){const std::string a=argv[i];if(a=="--smoke-test")smoke=true;else if(a=="--screenshot" && i+1<argc)screenshot=argv[++i];else if(a=="--menu-page" && i+1<argc)menu_page=std::clamp(std::atoi(argv[++i]),0,5);}
 SDL_SetMainReady();if(smoke)SDL_setenv("SDL_AUDIODRIVER","dummy",1);
 if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0){std::fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
 SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
 auto* window=SDL_CreateWindow("BAX:KI:ZL - Kinetic - right-click / P: menu - Space: clear",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,680,680,SDL_WINDOW_RESIZABLE);
 if(!window){SDL_Quit();return 1;}
 renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
 if(!renderer){SDL_DestroyWindow(window);SDL_Quit();return 1;}
 texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_XRGB8888,SDL_TEXTUREACCESS_STREAMING,466,466);
 DeviceMask mask;char* path=SDL_GetBasePath();const std::string base=path?path:"";SDL_free(path);
 if(!texture || !mask.load(renderer,(base+"assets/simulator_mask.png").c_str())){std::fprintf(stderr,"Display texture/mask unavailable\n");SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 1;}
 SDL_RenderSetLogicalSize(renderer,mask.width,mask.height);SDL_SetWindowMinimumSize(window,240,240);
 AudioOutput output;
 if(!output.audio.configure(true,{sm::Waveform::Triangle,5,200}))std::fprintf(stderr,"Audio unavailable: %s (visual simulation still usable)\n",SDL_GetError());
 sm::bounce::World world(output,uint32_t(SDL_GetPerformanceCounter()));
 sm::bounce::Menu menu(world);menu_view=&menu;if(menu_page>=0){menu.toggle();menu.navigate(menu_page);}
 lv_init();auto* display=lv_display_create(466,466);lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
 std::vector<uint32_t> pixels(466*466);lv_display_set_buffers(display,pixels.data(),nullptr,uint32_t(pixels.size()*4),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);
 auto* screen=lv_screen_active();lv_obj_set_style_bg_color(screen,lv_color_hex(0),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
 lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(screen,draw,LV_EVENT_DRAW_MAIN,&world);
 auto pointer=[&](int x,int y){return sm::bounce::Vec{(x-mask.opening.x)*466/mask.opening.w-233,(y-mask.opening.y)*466/mask.opening.h-233};};
 struct Contact{int held=-1;sm::bounce::FlickTracker motion;bool menu=false;};std::array<Contact,3> contacts{};
 auto cancel_contacts=[&](){for(auto& c:contacts){world.release(c.held,{});c.held=-1;c.motion={};}menu.cancel();SDL_CaptureMouse(SDL_FALSE);};
 auto toggle_menu=[&](){cancel_contacts();menu.toggle();};
 using Gesture=sm::bounce::TouchGesture;
 auto input=[&](Gesture::Action action){
   if(action.kind==Gesture::Kind::Menu){toggle_menu();return;}
   auto& c=contacts[action.contact];const auto p=action.p;const sm::bounce::Vec arena{p.x-233,p.y-233};
   if(action.kind==Gesture::Kind::Down){c.menu=menu.opened;if(c.menu){menu.down(p);return;}c.held=world.grab(arena);c.motion={};c.motion.down(arena,action.time);return;}
   if(c.menu){if(!menu.opened)return;if(action.kind==Gesture::Kind::Move)menu.move(p);else if(action.kind==Gesture::Kind::Up)menu.up(p,action.time);return;}
   if(c.held<0)return;
   if(action.kind==Gesture::Kind::Move){world.move(c.held,arena);c.motion.move(arena,action.time);}
   if(action.kind==Gesture::Kind::Up){world.fling(c.held,c.motion.velocity(action.time));c.held=-1;c.motion={};}
 };
 Gesture touch(input);
 auto display_pointer=[&](int x,int y){auto p=pointer(x,y);return sm::bounce::Vec{p.x+233,p.y+233};};
 bool running=true;uint64_t last=SDL_GetTicks64();double accumulator=0;unsigned frames=0;
 if(smoke || !screenshot.empty()){int id=world.grab({0,30});world.release(id,{165,-390});id=world.grab({65,10});world.release(id,{-320,170});}
 while(running){
   SDL_Event e;while(SDL_PollEvent(&e)){
     if(e.type==SDL_QUIT)running=false;
     if(e.type==SDL_KEYDOWN && !e.key.repeat){if(e.key.keysym.sym==SDLK_ESCAPE){if(menu.opened)toggle_menu();else running=false;}if(e.key.keysym.sym==SDLK_p)toggle_menu();if(e.key.keysym.sym==SDLK_SPACE){cancel_contacts();world.clear();}}
     if(e.type==SDL_MOUSEBUTTONDOWN && e.button.which!=SDL_TOUCH_MOUSEID){
       if(e.button.button==SDL_BUTTON_RIGHT || (e.button.button==SDL_BUTTON_LEFT && mask.power_hit(float(e.button.x),float(e.button.y))))toggle_menu();
       else if(e.button.button==SDL_BUTTON_LEFT){input({Gesture::Kind::Down,2,display_pointer(e.button.x,e.button.y),SDL_GetTicks64()});SDL_CaptureMouse(SDL_TRUE);}
     }
     if(e.type==SDL_MOUSEMOTION && e.motion.which!=SDL_TOUCH_MOUSEID && (e.motion.state&SDL_BUTTON_LMASK))input({Gesture::Kind::Move,2,display_pointer(e.motion.x,e.motion.y),SDL_GetTicks64()});
     if(e.type==SDL_MOUSEBUTTONUP && e.button.which!=SDL_TOUCH_MOUSEID && e.button.button==SDL_BUTTON_LEFT){input({Gesture::Kind::Up,2,display_pointer(e.button.x,e.button.y),SDL_GetTicks64()});SDL_CaptureMouse(SDL_FALSE);}
     if(e.type==SDL_MOUSEWHEEL && menu.opened){int x,y;SDL_GetMouseState(&x,&y);float lx,ly;SDL_RenderWindowToLogical(renderer,x,y,&lx,&ly);menu.scroll(display_pointer(int(lx),int(ly)),e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-e.wheel.y:e.wheel.y);}
     if(e.type==SDL_FINGERDOWN || e.type==SDL_FINGERMOTION || e.type==SDL_FINGERUP){int w,h;SDL_GetWindowSize(window,&w,&h);float x,y;SDL_RenderWindowToLogical(renderer,int(e.tfinger.x*w),int(e.tfinger.y*h),&x,&y);const auto p=display_pointer(int(x),int(y));const auto now=SDL_GetTicks64();if(e.type==SDL_FINGERDOWN){if(std::hypot(p.x-233,p.y-233)<=233)touch.down(e.tfinger.fingerId,p,now);}else if(e.type==SDL_FINGERMOTION)touch.move(e.tfinger.fingerId,p,now);else touch.up(e.tfinger.fingerId,p,now);}
     if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){cancel_contacts();touch.cancel(SDL_GetTicks64());}
   }
   const auto now=SDL_GetTicks64();const double elapsed=smoke?1./60:std::min(.05,double(now-last)/1000.);last=now;accumulator+=elapsed;
   while(accumulator>=1./240){world.step(1./240);accumulator-=1./240;}
   touch.tick(now);menu.tick(elapsed);visual.invalidate();
   if(smoke && menu_page<0 && frames%30==0){if(!menu.opened)menu.toggle();else menu.navigate(1);}
   lv_tick_inc(smoke?17:uint32_t(std::round(elapsed*1000)));lv_obj_invalidate(screen);lv_timer_handler();
   SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderCopyF(renderer,texture,nullptr,&mask.opening);
   SDL_FRect frame{0,0,float(mask.width),float(mask.height)};SDL_RenderCopyF(renderer,mask.texture,nullptr,&frame);
   ++frames;
   if(!screenshot.empty() && frames==60){
     int w=0,h=0;SDL_GetRendererOutputSize(renderer,&w,&h);auto* surface=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
     if(surface){SDL_RenderReadPixels(renderer,nullptr,surface->format->format,surface->pixels,surface->pitch);SDL_SaveBMP(surface,screenshot.c_str());SDL_FreeSurface(surface);}running=false;
   }
   SDL_RenderPresent(renderer);
   if(smoke && frames>=180)running=false;
   if(!smoke)SDL_Delay(8);
 }
 world.clear();output.audio.close();menu_view=nullptr;visual.release();lv_display_delete(display);lv_deinit();
 SDL_DestroyTexture(mask.texture);SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
 if(smoke){std::printf("Bounce simulator: %u strikes\n",output.strikes);return output.strikes?0:1;}return 0;
}

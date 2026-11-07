#include "ui.hpp"
#include "file_settings.hpp"
#include "device_mask.hpp"
#include "local_audio.hpp"
#include "charge_screen.hpp"
#include "companion_controls.hpp"
#include <SDL.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <SDL_syswm.h>
#endif
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
namespace {
struct CheckResult {
 int value=0;
 operator int() const { return value; }
 CheckResult& operator=(int code) { value=code; if(code) std::fprintf(stderr,"Simulator checkpoint failed: %d\n",code); return *this; }
};
SDL_Renderer* renderer; SDL_Texture* texture;
DeviceMask device_mask;
bool power_mouse=false,power_hover=false;
bool boot_mouse=false,boot_hover=false;
SDL_Cursor *hand_cursor=nullptr,*default_cursor=nullptr;
bool corner_controls=true,corner_reset=false;
uint32_t corner_accent=0x2ee7e7;
companion::Action corner_hover=companion::Action::None,corner_pressed=companion::Action::None;
int mouse_x=0,mouse_y=0; bool mouse_down=false;
int second_x=0,second_y=0; bool second_down=false;
void render_frame() {
 SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
 SDL_RenderCopyF(renderer,texture,nullptr,&device_mask.opening);
 const SDL_FRect frame{0,0,float(device_mask.width),float(device_mask.height)};
 SDL_RenderCopyF(renderer,device_mask.texture,nullptr,&frame);
 device_mask.draw_power(renderer,power_hover,power_mouse);
 device_mask.draw_boot(renderer,boot_hover,boot_mouse);
 if(corner_controls)companion::draw(renderer,device_mask.width,device_mask.height,corner_reset,corner_hover,corner_accent);
}
void present() { render_frame(); SDL_RenderPresent(renderer); }
bool power_pointer(const SDL_Event& e) {
 if(e.type==SDL_MOUSEMOTION) {
   const bool hovered=device_mask.power_hit(float(e.motion.x),float(e.motion.y));
   const bool hovered_boot=device_mask.boot_hit(float(e.motion.x),float(e.motion.y));
   if(hovered!=power_hover || hovered_boot!=boot_hover) {
     power_hover=hovered; boot_hover=hovered_boot; if(hand_cursor) SDL_SetCursor(hovered || hovered_boot?hand_cursor:default_cursor); present();
   }
   return power_mouse || boot_mouse;
 }
 if((e.type==SDL_MOUSEBUTTONDOWN || e.type==SDL_MOUSEBUTTONUP) && e.button.button==SDL_BUTTON_LEFT) {
   if(e.type==SDL_MOUSEBUTTONDOWN && device_mask.power_hit(float(e.button.x),float(e.button.y))) {
     power_mouse=true; mouse_down=false; SDL_CaptureMouse(SDL_TRUE); present(); return true;
   }
   if(e.type==SDL_MOUSEBUTTONDOWN && device_mask.boot_hit(float(e.button.x),float(e.button.y))) {
     boot_mouse=true; mouse_down=false; SDL_CaptureMouse(SDL_TRUE); present(); return true;
   }
   if(e.type==SDL_MOUSEBUTTONUP && (power_mouse || boot_mouse)) {
     power_mouse=boot_mouse=false; SDL_CaptureMouse(SDL_FALSE); present(); return true;
   }
 }
 return false;
}
lv_point_t logical_to_display(float x,float y) {
 return {int(std::floor((x-device_mask.opening.x)*466/device_mask.opening.w)),
         int(std::floor((y-device_mask.opening.y)*466/device_mask.opening.h))};
}
void display_to_window(int x,int y,int* wx,int* wy) {
 SDL_RenderLogicalToWindow(renderer,device_mask.opening.x+x*device_mask.opening.w/466,
   device_mask.opening.y+y*device_mask.opening.h/466,wx,wy);
}
lv_point_t window_to_display(int x,int y) {
 float logical_x,logical_y; SDL_RenderWindowToLogical(renderer,x,y,&logical_x,&logical_y);
 return logical_to_display(logical_x,logical_y);
}
void flush(lv_display_t* d,const lv_area_t* area,uint8_t* pixels) {
 SDL_Rect r{area->x1,area->y1,lv_area_get_width(area),lv_area_get_height(area)};
 if(lv_display_get_color_format(d)==LV_COLOR_FORMAT_RGB565) {
   std::vector<uint32_t> expanded(size_t(r.w)*r.h);
   SDL_ConvertPixels(r.w,r.h,SDL_PIXELFORMAT_RGB565,pixels,r.w*2,
                     SDL_PIXELFORMAT_ARGB8888,expanded.data(),r.w*4);
   SDL_UpdateTexture(texture,&r,expanded.data(),r.w*4);
 } else SDL_UpdateTexture(texture,&r,pixels,r.w*4);
 if(lv_display_flush_is_last(d)) present();
 lv_display_flush_ready(d);
}
void read_pointer(lv_indev_t*,lv_indev_data_t* d) {
 d->point={mouse_x,mouse_y}; int x=mouse_x-233,y=mouse_y-233;
 d->state=mouse_down && x*x+y*y<=233*233?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
void read_second_pointer(lv_indev_t*,lv_indev_data_t* d) {
 d->point={second_x,second_y}; const int x=second_x-233,y=second_y-233;
 d->state=second_down && x*x+y*y<=233*233?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
struct ConsoleMidi : sm::MidiTransport {
 int ons=0,offs=0,last_on=-1,last_off=-1,last_channel=1;
 bool send(sm::MidiEvent e) override {
   if(e.kind==sm::MidiKind::NoteOn) { ++ons; last_on=e.note; last_channel=e.channel; } if(e.kind==sm::MidiKind::NoteOff) { ++offs; last_off=e.note; }
   if(e.kind!=sm::MidiKind::AllNotesOff) std::printf("MIDI preview: %s ch=%u note=%u velocity=%u\n",e.kind==sm::MidiKind::NoteOn?"on":"off",e.channel,e.note,e.velocity); return true;
 }
};
void diagnostic(int button,sm::ButtonEvent e,void* context) { std::printf("BUTTON %d %s\n",button+1,e==sm::ButtonEvent::Press?"PRESS":e==sm::ButtonEvent::Hold?"HOLD":"RELEASE"); std::fflush(stdout); if(context) static_cast<sm::Ui*>(context)->button(button,e); }
void capture(const char* path) {
 lv_timer_handler(); lv_refr_now(nullptr);
 // SDL invalidates the backbuffer after Present. Compose a fresh frame before
 // reading it so static menu screenshots retain the complete device mask.
 render_frame();
 int width,height; SDL_GetRendererOutputSize(renderer,&width,&height);
 auto* s=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
 SDL_FillRect(s,nullptr,SDL_MapRGBA(s->format,0,0,0,255));
 SDL_Rect bounds{0,0,width,height};
 SDL_RenderReadPixels(renderer,&bounds,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch); SDL_SaveBMP(s,path); SDL_FreeSurface(s);
 SDL_RenderPresent(renderer);
}
int brightness(int x,int y) {
 int wx,wy; display_to_window(x,y,&wx,&wy);
 SDL_Rect pixel{wx,wy,1,1}; uint32_t value=0;
 SDL_RenderReadPixels(renderer,&pixel,SDL_PIXELFORMAT_ARGB8888,&value,4);
 return int((value>>16)&255)+int((value>>8)&255)+int(value&255);
}
}
int main(int argc,char** argv) {
 SDL_SetMainReady(); bool smoke=false,panel_rendering=false,selector_render_test=false,controls_test=false; uintptr_t parent_window=0; std::string manual_dir; SDL_Keycode key1=SDLK_PLUS,key2=SDLK_MINUS;
 for(int i=1;i<argc;++i) {
   if(std::string(argv[i])=="--parent-window" && i+1<argc) {
     try {parent_window=std::stoull(argv[++i]);}catch(...){std::fprintf(stderr,"Invalid parent window.\n");return 2;}
   }
   if(std::string(argv[i])=="--companion-controls-test")controls_test=true;
 }
 for(int i=1;i<argc;++i) { std::string a=argv[i]; if(a=="--smoke-test") smoke=true; if(a=="--panel-rendering") panel_rendering=true; if(a=="--selector-render-test") selector_render_test=panel_rendering=true; if(a=="--manual-screenshots" && i+1<argc) manual_dir=argv[++i]; if(a=="--key1" && i+1<argc) key1=SDL_GetKeyFromName(argv[++i]); if(a=="--key2" && i+1<argc) key2=SDL_GetKeyFromName(argv[++i]); }
 if(!manual_dir.empty())corner_controls=false;
 if(key1==SDLK_UNKNOWN || key2==SDLK_UNKNOWN || key1==key2) { std::fprintf(stderr,"Use two distinct valid SDL key names.\n"); return 1; }
 if(smoke || controls_test || selector_render_test || !manual_dir.empty()) SDL_setenv("SDL_AUDIODRIVER","dummy",1);
 if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0) { std::fprintf(stderr,"SDL: %s\n",SDL_GetError()); return 1; }
 SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
 if(parent_window)SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH,"1");
 // Start hidden with SDL's resizable flag intact; remove native decorations
 // only after creation. SDL 2 clears that flag for borderless creation on Windows.
 auto* window=SDL_CreateWindow("Smart MIDI - Hold PWR: setup - click PWR: save",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,466,466,parent_window?(SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE):SDL_WINDOW_RESIZABLE);
 renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
 texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGB888,SDL_TEXTUREACCESS_STREAMING,466,466);
 if(!window || !renderer || !texture) { std::fprintf(stderr,"SDL: %s\n",SDL_GetError()); SDL_Quit(); return 1; }
 SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
 char* base=SDL_GetBasePath(); if(!base) { std::fprintf(stderr,"Cannot determine application directory\n"); return 1; }
 const std::string base_path=base; SDL_free(base);
 const std::string mask_path=base_path+"assets/simulator_mask.png";
 if(!device_mask.load(renderer,mask_path.c_str())) {
   std::fprintf(stderr,"Cannot load device mask with transparent display opening: %s\n",mask_path.c_str());
   SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
 }
 // Default window leaves the visible display approximately 466 pixels across.
 if(!parent_window) {
 SDL_SetWindowSize(window,int(std::lround(device_mask.width*466/device_mask.opening.w)),
   int(std::lround(device_mask.height*466/device_mask.opening.h)));
 SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
 }
#ifdef _WIN32
 HWND parent=reinterpret_cast<HWND>(parent_window);
 HWND embedded_child=nullptr;
 if(parent) {
   SDL_SysWMinfo info{};SDL_VERSION(&info.version);
   if(!IsWindow(parent) || !SDL_GetWindowWMInfo(window,&info)){std::fprintf(stderr,"Companion parent unavailable.\n");SDL_Quit();return 2;}
   HWND child=info.info.win.window;
   embedded_child=child;
   const auto style=GetWindowLongPtrW(child,GWL_STYLE);
   SetWindowLongPtrW(child,GWL_STYLE,(style&~(WS_POPUP|WS_CAPTION|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX))|WS_CHILD|WS_CLIPSIBLINGS);
   SetWindowLongPtrW(child,GWL_EXSTYLE,GetWindowLongPtrW(child,GWL_EXSTYLE)&~(WS_EX_APPWINDOW|WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE));
   SetLastError(0);
   if(!SetParent(child,parent) && GetLastError()!=0){std::fprintf(stderr,"Cannot embed SDL window: %lu\n",GetLastError());SDL_Quit();return 2;}
   RECT bounds{};GetClientRect(parent,&bounds);
   SetWindowPos(child,nullptr,0,0,bounds.right,bounds.bottom,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
   SDL_ShowWindow(window);
   SetWindowPos(child,nullptr,0,0,bounds.right,bounds.bottom,SWP_NOZORDER|SWP_NOACTIVATE);
   std::printf("Companion: embedded simulator; no separate window.\n");std::fflush(stdout);
 }
#else
 if(parent_window){std::fprintf(stderr,"Embedding is Windows-only.\n");SDL_Quit();return 2;}
#endif
 SDL_SetWindowMinimumSize(window,96,96);
 // SDL maps events to mask coordinates; logical_to_display maps the opening to LVGL.
 SDL_RenderSetLogicalSize(renderer,device_mask.width,device_mask.height);
 default_cursor=SDL_GetCursor(); hand_cursor=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
 lv_init(); lv_tick_set_cb(SDL_GetTicks);
 auto* display=lv_display_create(466,466); lv_display_set_color_format(display,panel_rendering?LV_COLOR_FORMAT_RGB565:LV_COLOR_FORMAT_XRGB8888);
 std::vector<uint32_t> buffer(panel_rendering?466*20/2:466*64),second_buffer(panel_rendering?buffer.size():0);
 lv_display_set_buffers(display,buffer.data(),panel_rendering?second_buffer.data():nullptr,uint32_t(buffer.size()*4),LV_DISPLAY_RENDER_MODE_PARTIAL); lv_display_set_flush_cb(display,flush);
 auto* pointer=lv_indev_create(); lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(pointer,read_pointer);
 auto* second_pointer=lv_indev_create(); lv_indev_set_type(second_pointer,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(second_pointer,read_second_pointer);
 std::string settings_path=selector_render_test?"selector-render-settings.cfg":(smoke||controls_test)?"smoke-settings.cfg":base_path+"settings.cfg";
 if(!manual_dir.empty()) {
   std::filesystem::create_directories(manual_dir);
   settings_path=(std::filesystem::path(manual_dir)/"capture-settings.cfg").string();
 }
 if(smoke || controls_test || selector_render_test || !manual_dir.empty()) {
   std::filesystem::remove(settings_path); std::filesystem::remove(settings_path+".slots");std::filesystem::remove(settings_path+".bounce");
   for(int slot=1;slot<=sm::music_slot_count;++slot) std::filesystem::remove(settings_path+".slot"+std::to_string(slot));
 }
 sm::FileSettings settings(settings_path);
 sm::Setup regression_setup;regression_setup.travel=false;regression_setup.synth={sm::Waveform::Triangle,10,400};
 if(smoke)settings.save_setup(regression_setup);
 sm::Application app(settings); ConsoleMidi transport; DesktopAudio audio;
 sm::PerformanceOutput output(transport,audio); sm::MidiRouter midi(output); sm::Ui ui(app,midi,&output); ui.build();
 sm::Buttons buttons,power_button,boot_button; bool down[2]={},power_down=false,boot_key=false; bool running=true; CheckResult result;
 auto corner_action=[&](companion::Action action) {
   if(action==companion::Action::Reset && !ui.has_resettable_state())return;
   mouse_down=second_down=false;ui.cancel_touch();
   if(action==companion::Action::Reset){ui.reset_play_mode();std::printf("Companion action: reset\n");}
   std::fflush(stdout);
   corner_reset=ui.has_resettable_state();present();
 };
 struct BootState { sm::Ui* ui; bool down=false,committed=false; uint32_t started=0,off_at=0; } boot{&ui};
 auto poll_power=[&]() {
   power_button.update(0,power_down || power_mouse,SDL_GetTicks(),[](int,sm::ButtonEvent event,void* context) {
     static_cast<sm::Ui*>(context)->power_button(event);
   },&ui);
   const uint32_t now=SDL_GetTicks();
   boot_button.update(0,boot_key || boot_mouse,now,[](int,sm::ButtonEvent event,void* context) {
     auto& state=*static_cast<BootState*>(context);
     if(event==sm::ButtonEvent::Press && !state.committed) {
       state.down=true; state.started=SDL_GetTicks(); state.ui->begin_power_off();
     }
     if(event==sm::ButtonEvent::Release) { state.down=false; state.ui->cancel_power_off(); }
   },&boot);
   if(boot.down && !boot.committed && ui.update_power_off(uint32_t(SDL_GetTicks()-boot.started))) {
    if(ui.prepare_shutdown()) { boot.committed=true; boot.off_at=now+250;std::printf("Companion action: shutdown\n");std::fflush(stdout); }
     else { boot.down=false; std::fprintf(stderr,"Arpeggiator settings could not be saved; shutdown cancelled.\n"); }
   }
   if(boot.committed && int32_t(now-boot.off_at)>=0) running=false;
 };
 std::string key1_name=SDL_GetKeyName(key1),key2_name=SDL_GetKeyName(key2);
 std::printf("Settings: %s\nButtons: %s / %s; hold >=600ms. Console MIDI preview only.\n",settings_path.c_str(),key1_name.c_str(),key2_name.c_str());
 // Sample synthetic contacts explicitly: a slow render during a concurrent build
 // must not consume a whole short test press before the input timer can run.
 auto pump=[&](int ms) { auto end=SDL_GetTicks()+ms; do { poll_power(); const auto now=uint64_t(SDL_GetTicks())*1000; ui.performance_tick(now); lv_indev_read(pointer); lv_indev_read(second_pointer); lv_timer_handler(); SDL_Delay(5); } while(SDL_GetTicks()<end); };
 // Keep the existing three-mode regression sequences explicit. Bounce has
 // its own integration test, including the actual four-mode PWR order.
 auto legacy_cycle=[&](){ui.toggle_arpeggiator();if(ui.is_bounce())ui.toggle_arpeggiator();};
 auto legacy_power=[&](sm::ButtonEvent e){ui.power_button(e);if(e==sm::ButtonEvent::Release && ui.is_bounce() && !ui.is_bounce_menu())ui.toggle_arpeggiator();};
 if(smoke) {
   sm::ChargeScreen charge; charge.build(app.setup.accent);
   const sm::BatteryState charging{true,true,true,62,true,true,false};
   charge.update(charging,0); pump(60); capture("smoke-charge-blink-on.bmp");
   charge.update(charging,500); pump(60); capture("smoke-charge-blink-off.bmp");
   auto* charge_root=lv_obj_get_child(lv_screen_active(),lv_obj_get_child_count(lv_screen_active())-1);
   auto* outline=lv_obj_get_child(charge_root,0);
   if(lv_obj_get_width(outline)!=sm::battery_icon_width || lv_obj_get_height(outline)!=sm::battery_icon_height) result=350;
   auto* top_bar=lv_obj_get_child(charge_root,4); // Fourth lit bar at 62%.
   // PMIC host timestamps and LVGL ticks can have opposite blink phases.
   // Timer callbacks must preserve the phase selected by telemetry updates.
   lv_timer_t* charge_timer=nullptr;
   for(auto* timer=lv_timer_get_next(nullptr);timer;timer=lv_timer_get_next(timer))
     if(lv_timer_get_user_data(timer)==&charge) { charge_timer=timer; break; }
   if(!charge_timer) result=352;
   else {
     const uint32_t host_time=(lv_tick_get()/500)%2?100:600;
     for(uint32_t elapsed:{0u,100u,200u}) {
       charge.update(charging,host_time+elapsed);
       const bool hidden=lv_obj_has_flag(top_bar,LV_OBJ_FLAG_HIDDEN);
       lv_timer_ready(charge_timer); pump(20);
       if(lv_obj_has_flag(top_bar,LV_OBJ_FLAG_HIDDEN)!=hidden) result=353;
     }
   }
   auto standby=charging; standby.charging=false;
   charge.update(standby,lv_tick_get());
   const bool was_hidden=lv_obj_has_flag(top_bar,LV_OBJ_FLAG_HIDDEN);
   const auto blink_deadline=SDL_GetTicks()+1200;
   while(lv_obj_has_flag(top_bar,LV_OBJ_FLAG_HIDDEN)==was_hidden && SDL_GetTicks()<blink_deadline) pump(20);
   if(lv_obj_has_flag(top_bar,LV_OBJ_FLAG_HIDDEN)==was_hidden) result=351;
   const int before=transport.ons;
   mouse_x=233; mouse_y=69; mouse_down=true; pump(30); mouse_down=false; pump(30);
   if(transport.ons!=before) result=310;
   charge.update({true,true,true,100,false,true,true},500); pump(60); capture("smoke-charge-full.bmp");
   charge.update({},0); pump(60); capture("smoke-charge-unknown.bmp");
   charge.close();
 }
 if(selector_render_test) {
   // Render the same menu with the board's RGB565/double/20-row profile.
   // No boot animation or personal configuration is used by this regression.
   pump(60);
   mouse_x=233; mouse_y=233; mouse_down=true; pump(30);
   mouse_y=69; pump(60); mouse_down=false; pump(80);
   if(!ui.is_editing_chord()) result=410;
   const auto clean_band=[&]() {
     render_frame();
     int w,h; SDL_GetRendererOutputSize(renderer,&w,&h);
     std::vector<uint32_t> pixels(size_t(w)*h);
     const bool read=SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),w*4)==0;
     bool clean=read;
     for(int y=316;y<=326 && clean;++y) for(int x=100;x<=270;++x) {
       int wx,wy; display_to_window(x,y,&wx,&wy);
       if(wx<0 || wx>=w || wy<0 || wy>=h || (pixels[size_t(wy)*w+wx]&0xffffffu)) { clean=false; break; }
     }
     SDL_RenderPresent(renderer); return clean;
   };
   if(!clean_band()) result=411;
   for(int i=0;i<sm::chord_variant_count;++i) {
     ui.scroll(170,233,-1); ui.scroll(349,233,i%2?-1:1); pump(260);
     if(!clean_band()) result=412;
   }
   capture("selector-render-regression.bmp");
   std::printf("Selector RGB565 partial rendering: %s (%d)\n",result?"FAIL":"PASS",int(result));
   running=false;
 }
 ui.show_boot_logo();
 if(!manual_dir.empty()) {
   const auto shot=[&](const char* name) { pump(60);corner_reset=ui.has_resettable_state();corner_accent=app.setup.accent; capture((std::filesystem::path(manual_dir)/(std::string(name)+".bmp")).string().c_str()); };
   const auto click=[&](int x,int y) { mouse_x=x; mouse_y=y; mouse_down=true; pump(40); mouse_down=false; pump(60); };
   const auto twice=[&]() { click(233,233); click(233,233); };
   const auto swipe=[&](int x,int y,int end_x,int end_y) {
     mouse_x=x; mouse_y=y; mouse_down=true; pump(30);
     for(int i=1;i<=6;++i) { mouse_x=x+(end_x-x)*i/6; mouse_y=y+(end_y-y)*i/6; pump(20); }
     mouse_down=false; pump(80);
   };
   const auto save_setup=[&]() { legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(60); };
   shot("boot"); pump(3650); shot("chords");
   twice(); shot("chord-select"); ui.back();
   swipe(233,233,233,69); ui.scroll(170,233,-3); pump(220); shot("chord-edit"); ui.confirm(); shot("chord-custom");
   ui.reset_chord_buttons(); twice(); ui.scroll(359,233,-5); pump(220); shot("strum-ms"); ui.confirm();
   ui.open_setup(); shot("setup-midi"); click(315,308); save_setup();
   twice(); ui.scroll(359,233,-sm::chord_strum_option(8)); pump(220); shot("strum-sync"); ui.confirm();
   ui.open_setup();
   const uint64_t clock_end=uint64_t(SDL_GetTicks())*1000;
   for(int i=0;i<=24;++i) ui.midi_realtime(0xf8,clock_end-(24-i)*20833ull);
   pump(160); // Allow the throttled clock-BPM label to refresh before capture.
   shot("setup-clock"); click(315,308); save_setup();
   legacy_cycle(); legacy_cycle();
   twice(); ui.scroll(139,233,-2); ui.scroll(290,233,-1); pump(220); shot("keyboard-select"); ui.confirm(); shot("keyboard");
   legacy_cycle(); legacy_cycle();
   auto arp_settings=ui.arpeggiator().state().settings; arp_settings.pattern=sm::ArpPattern::Anchor;
   ui.arpeggiator().settings(arp_settings);
   for(int degree:{0,2,4}) ui.arpeggiator().toggle(degree);
   ui.refresh();
   const char* pages[]={"arp-scale","arp-octaves","arp-pattern","arp-divisor","arp-bpm","arp-probability","arp-blocksize","arp-gate","arp-ratchet","arp-swing"};
   const int page_ids[]={0,1,2,3,4,5,6,8,9,7};
   for(int page=0;page<10;++page) {
     if(ui.arp_page()!=page_ids[page]) result=1;
     if(page==6) { ui.scroll(233,233,-4); pump(220); }
     if(page==8) { ui.scroll(176,248,-4); ui.scroll(290,248,-1); pump(220); }
     if(page==9) { ui.scroll(233,233,-25); pump(220); }
     shot(pages[page]); if(page<9) swipe(290,150,180,150);
   }
   // Kinetic uses the actual shared runtime and touch gestures. Keep the
   // application setting names/storage keys for compatibility with Bounce.
   ui.toggle_arpeggiator();if(!ui.is_bounce())result=6;
   swipe(130,300,210,190);swipe(320,260,270,160);swipe(210,320,300,280);
   shot("kinetic-play");corner_controls=true;shot("simulator-reset");corner_controls=false;
   if(!ui.toggle_bounce_menu())result=7;
   const char* kinetic_pages[]={"kinetic-scale","kinetic-octaves","kinetic-bpm","kinetic-timing","kinetic-gate","kinetic-lifetime"};
   for(int page=0;page<6;++page){shot(kinetic_pages[page]);if(page<5)swipe(300,390,160,390);}
   click(233,233);ui.toggle_bounce_menu();ui.toggle_arpeggiator();
   ui.open_setup(); swipe(233,390,150,390); if(ui.setup_page()!=1) result=2; shot("setup-color");
   swipe(233,390,150,390); if(ui.setup_page()!=2) result=3;
   shot("setup-travel");
   swipe(233,410,150,410); if(ui.setup_page()!=3) result=4; shot("setup-global");
   swipe(233,410,150,410); if(ui.setup_page()!=4) result=5; shot("setup-slots-empty");
   click(155,370); shot("slot-save-empty");
   // A concrete example name, using exactly the same wheels as normal input.
   const int letters[]={1,14,3,8,15,18}; // ANCHOR
   for(int i=0;i<6;++i) ui.scroll(86+i*42,233,-letters[i]);
   pump(240); shot("slot-save"); click(155,370); shot("setup-slots");
   click(310,370); shot("slot-load"); click(310,370);
   click(397,218); shot("slot-delete"); click(310,370);
   save_setup();
   ui.set_battery({true,true,true,60,false}); ui.begin_power_off(); ui.update_power_off(1000); shot("power-off");
   ui.set_battery({true,true,true,60,true,true}); shot("power-charging"); ui.cancel_power_off();
   sm::ChargeScreen charge; charge.build(app.setup.accent);
   charge.update({true,true,true,62,true,true,false},0); shot("charge-screen");
   charge.update({true,true,true,100,false,true,true},0); shot("charge-full"); charge.close();
   std::filesystem::remove(settings_path);
   std::printf("Manual screenshots: %s (%d)\n",result?"FAIL":"PASS",int(result)); running=false;
 }
 if(smoke) {
   pump(60); capture("smoke-boot-logo.bmp");
   if(!ui.booting()) result=220;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Hold); legacy_power(sm::ButtonEvent::Release);
   if(ui.is_setup() || !ui.is_chords() || ui.is_arpeggiator()) result=221;
   pump(3360);
   if(!ui.booting()) result=433;
   const auto cover_opacity=lv_obj_get_style_bg_opa(lv_obj_get_child(lv_screen_active(),-1),0);
   if(cover_opacity==0 || cover_opacity==255) result=434;
   capture("smoke-boot-ui-fade.bmp");
   pump(290); if(ui.booting()) result=222;
   capture("smoke-start-chords.bmp");
   mouse_x=233; mouse_y=69; mouse_down=true; pump(40);
   const int chord_power_ons=transport.ons,chord_power_offs=transport.offs;
   ui.begin_power_off(); pump(60);
   if(transport.offs!=chord_power_offs) result=424;
   ui.cancel_power_off(); pump(40);
   if(transport.ons!=chord_power_ons || transport.offs!=chord_power_offs) result=425;
   mouse_down=false; pump(40);
   if(transport.offs<=chord_power_offs) result=426;
   // Startup is Chords; continue through Arpeggiator to the keyboard checks.
   legacy_cycle(); if(!ui.is_arpeggiator() || ui.is_chords()) result=223;
   legacy_cycle(); if(ui.is_arpeggiator() || ui.is_chords()) result=224;
 }
 if(smoke) {
   pump(60); capture("smoke-default.bmp"); if(app.current!=sm::Selection{0,0}) result=11;
   ui.open(); pump(80); capture("smoke-default-selection.bmp");
   // ESP32 regression: moving selector rows must never need software-scaled
   // offscreen layers, and the covered keyboard must not draw underneath.
   auto* wheel_object=lv_obj_get_child(lv_screen_active(),0);
   auto* menu_object=lv_obj_get_child(lv_screen_active(),1);
   auto native_scale=[](auto&& self,lv_obj_t* object)->bool {
     if(lv_obj_get_style_clip_corner(object,0)) return false; // Avoid costly intermediate clipping layers.
     if(lv_obj_check_type(object,&lv_roller_class)) return false;
     if(lv_obj_get_style_transform_scale_x(object,0)!=256 || lv_obj_get_style_transform_scale_y(object,0)!=256) return false;
     for(uint32_t i=0;i<lv_obj_get_child_count(object);++i) if(!self(self,lv_obj_get_child(object,i))) return false;
     return true;
   };
   auto consistent_redraw=[&]() {
     std::vector<uint32_t> before(466*466),after(466*466);
     SDL_Rect bounds{0,0,466,466};
     SDL_RenderReadPixels(renderer,&bounds,SDL_PIXELFORMAT_ARGB8888,before.data(),466*4);
     lv_obj_invalidate(lv_screen_active()); pump(80);
     SDL_RenderReadPixels(renderer,&bounds,SDL_PIXELFORMAT_ARGB8888,after.data(),466*4);
     return before==after;
   };
   if(!lv_obj_has_flag(wheel_object,LV_OBJ_FLAG_HIDDEN) || !native_scale(native_scale,menu_object)) result=44;
   if(!consistent_redraw()) result=47;
   if(brightness(139,203)==0 || brightness(270,203)==0 || brightness(233,352)==0) result=42;
   if(brightness(178,233)!=0 || brightness(102,203)!=0 || brightness(363,203)!=0) result=43;
   ui.back();
   if(lv_obj_has_flag(wheel_object,LV_OBJ_FLAG_HIDDEN)) result=45;
   // Drive the real LVGL pointer input, then the SDL wheel adapter.
   app.current={2,1}; ui.refresh(); pump(60); capture("smoke-main.bmp");
   auto click=[&](int x,int y){ mouse_x=x; mouse_y=y; mouse_down=true; pump(60); mouse_down=false; pump(80); };
   auto double_click=[&](){ click(233,233); click(233,233); };
   click(233,233); if(ui.is_selecting()) result=15;
   pump(400); click(233,233); if(ui.is_selecting()) result=16;
   // A drag into the centre must not count as a tap or arm the next click.
   mouse_x=233; mouse_y=69; mouse_down=true; pump(60);
   mouse_y=233; pump(60); mouse_down=false; pump(80);
   click(233,233); if(ui.is_selecting()) result=17;
   pump(400); transport.ons=transport.offs=0;
   double_click(); if(!ui.is_selecting()) result=2;
   // Fast wheel events accumulate even before the snap animation finishes.
   ui.scroll(290,220,-1); ui.scroll(290,220,-1); pump(220);
   if(app.draft.mode!=3 || app.draft.root!=2) result=49;
   ui.scroll(290,220,4); pump(220);
   if(app.draft.mode!=int(sm::modes().size())-1) result=50; // Wrap backwards past Major.
   ui.scroll(290,220,-2); pump(220);
   if(app.draft.mode!=1) result=51; // Wrap forwards, restore for drag test.
   // A real pointer drag must snap independently of the mode column.
   mouse_x=139; mouse_y=285; mouse_down=true; pump(60);
   for(int y=285;y>=205;y-=5) { mouse_y=y; pump(20); if(!native_scale(native_scale,menu_object)) result=46; }
   mouse_down=false; pump(500);
   if(app.draft.root==2) result=5;
   if(app.draft.mode!=1 || app.current!=sm::Selection{2,1}) result=6;
   ui.back(); ui.open();
   auto draft_before=app.draft; ui.scroll(10,10,-4); if(app.draft!=draft_before) result=8;
   ui.scroll(139,220,2); ui.scroll(290,220,-8); pump(400); capture("smoke-selection.bmp");
   if(!consistent_redraw()) result=48;
   ui.back(); if(ui.is_selecting() || app.current!=sm::Selection{2,1}) result=3;
   click(233,233); if(ui.is_selecting()) result=18;
   click(233,233); ui.scroll(139,220,2); ui.scroll(290,220,-8); pump(400); click(233,394);
   if(ui.is_selecting() || app.current!=sm::Selection{0,9} || settings.load()!=app.current) result=4;
   sm::Application restarted(settings); if(restarted.current!=app.current) result=7;
   pump(60); capture("smoke-pentatonic.bmp");
   int resting=brightness(233,38);
   mouse_x=233; mouse_y=69; mouse_down=true; pump(60);
   int held=brightness(233,38); capture("smoke-feedback-held.bmp");
   mouse_down=false; pump(60);
   if(transport.ons!=1 || transport.offs!=1) result=9; // No delayed MIDI Note Off.
   pump(190); int halfway=brightness(233,38); capture("smoke-feedback-halfway.bmp");
   pump(300); int finished=brightness(233,38); capture("smoke-feedback-finished.bmp");
   if(!(held>halfway && halfway>resting && finished==resting)) result=21;
   double_click(); ui.scroll(290,220,-1); pump(400); click(233,394);
   if(app.current!=sm::Selection{0,10} || sm::notes(app.current).size()!=5) result=10;
   capture("smoke-minor-pentatonic.bmp");
   // Side-button octave actions are shared with the hardware adapter.
   if(ui.octave()!=4) result=22;
   mouse_x=233; mouse_y=69; mouse_down=true; pump(60);
   int ons_before=transport.ons,offs_before=transport.offs;
   ui.button(0,sm::ButtonEvent::Press); pump(80);
   if(ui.octave()!=5 || transport.ons!=ons_before || transport.offs!=offs_before) result=23;
   ui.button(0,sm::ButtonEvent::Hold); ui.button(0,sm::ButtonEvent::Release);
   if(ui.octave()!=5) result=24;
   mouse_down=false; pump(60);
   if(transport.offs!=offs_before+1 || transport.last_off!=60) result=29;
   click(233,69); if(transport.last_on!=72 || transport.last_off!=72) result=30;
   capture("smoke-octave-5.bmp");
   ui.open(); ui.button(1,sm::ButtonEvent::Press); if(ui.octave()!=5) result=25; ui.back();
   for(int i=0;i<12;++i) ui.button(0,sm::ButtonEvent::Press);
   if(ui.octave()!=8) result=26; pump(60); capture("smoke-octave-8.bmp");
   for(int i=0;i<20;++i) ui.button(1,sm::ButtonEvent::Press);
   if(ui.octave()!=0) result=27; pump(60); capture("smoke-octave-0.bmp");
   click(233,69); if(transport.last_on!=12 || transport.last_off!=12) result=37;
   for(int i=0;i<4;++i) ui.button(0,sm::ButtonEvent::Press);
   if(ui.octave()!=4) result=28;
   // B8 Minor keeps seven geometric slots but omits A9 (> MIDI 127).
   app.current={11,1}; ui.refresh(); ui.change_octave(4); pump(60);
   auto slot=[](int degree) { float a=(270+degree*360.f/7)*3.14159265358979323846f/180; return SDL_Point{int(233+164*std::cos(a)),int(233+164*std::sin(a))}; };
   auto gap=slot(6),last_valid=slot(5);
   int before_gap=transport.ons;
   click(gap.x,gap.y); if(transport.ons!=before_gap || brightness(gap.x,gap.y)!=0) result=31;
   click(last_valid.x,last_valid.y); if(transport.last_on!=127 || transport.ons!=before_gap+1) result=32;
   capture("smoke-midi-gap.bmp");
   ui.change_octave(-1); pump(60);
   mouse_x=gap.x; mouse_y=gap.y; mouse_down=true; pump(60);
   int before_hidden_off=transport.offs;
   if(transport.last_on!=117) result=33;
   ui.change_octave(1); pump(80);
   if(transport.offs!=before_hidden_off || brightness(gap.x,gap.y)!=0) result=34;
   mouse_down=false; pump(60);
   if(transport.last_off!=117 || transport.offs!=before_hidden_off+1) result=35;
   ui.change_octave(-1); pump(60); capture("smoke-midi-gap-restored.bmp");
   if(brightness(gap.x,gap.y)==0) result=36;
   ui.change_octave(-3); app.current={0,10}; ui.refresh(); pump(60);
   const int swipe_ons=transport.ons;
   auto swipe=[&](int x1,int y1,int x2,int y2) {
     mouse_x=x1; mouse_y=y1; mouse_down=true; pump(40);
     mouse_x=x2; mouse_y=y2; pump(40); mouse_down=false; pump(40);
   };
   mouse_x=233; mouse_y=233; mouse_down=true; pump(40);
   mouse_y=160; pump(40); if(ui.octave()!=5) result=52;
   mouse_y=40; pump(40); if(ui.octave()!=5) result=53;
   mouse_down=false; pump(40);
   swipe(233,233,233,310); if(ui.octave()!=4) result=54;
   swipe(233,233,410,233); if(ui.octave()!=4) result=55;
   if(transport.ons!=swipe_ons || ui.is_selecting()) result=56;
   // Finger one stays on C4 while finger two swipes from the centre.
   second_x=233; second_y=69; second_down=true; pump(40);
   const int held_on=transport.ons,held_off=transport.offs,original_pitch=transport.last_on;
   click(233,233); click(233,233); if(ui.is_selecting()) result=62;
   swipe(233,233,233,150);
   if(ui.octave()!=5 || transport.ons!=held_on || transport.offs!=held_off) result=57;
   // New C5 in the same segment is independent of the still-held C4.
   mouse_x=233; mouse_y=69; mouse_down=true; pump(40);
   if(transport.last_on!=72 || transport.ons!=held_on+1) result=63;
   second_down=false; pump(40);
   if(transport.last_off!=original_pitch || transport.offs!=held_off+1) result=58;
   mouse_down=false; pump(40); if(transport.last_off!=72) result=64;
   ui.change_octave(-1);
   // Two different simultaneous degrees, then release each independently.
   const int two_ons=transport.ons,two_offs=transport.offs;
   second_x=233; second_y=69; second_down=true; pump(40);
   mouse_x=389; mouse_y=182; mouse_down=true; pump(40);
   if(transport.ons!=two_ons+2 || transport.last_on!=63) result=65;
   mouse_down=false; pump(40); if(transport.offs!=two_offs+1 || transport.last_off!=63) result=66;
   second_down=false; pump(40); if(transport.offs!=two_offs+2 || transport.last_off!=60) result=67;
   // Connection reset must release both notes and ignore resting fingers until lift.
   second_down=true; mouse_down=true; pump(40);
   const int reset_ons=transport.ons,reset_offs=transport.offs;
   ui.reset_midi_session(); pump(80);
   if(transport.offs!=reset_offs+2 || transport.ons!=reset_ons) result=71;
   mouse_down=false; second_down=false; pump(40);
   second_down=true; pump(40);
   if(transport.ons!=reset_ons+1) result=72;
   second_down=false; pump(40);
   // Same pitch: no premature Note Off when just one of the fingers lifts.
   const int same_ons=transport.ons,same_offs=transport.offs;
   second_down=true; pump(40); mouse_x=240; mouse_y=75; mouse_down=true; pump(40);
   if(transport.ons!=same_ons+1) result=68;
   second_down=false; pump(40); if(transport.offs!=same_offs) result=69;
   mouse_down=false; pump(40); if(transport.offs!=same_offs+1) result=70;
   // Entering the centre from a note is not an octave gesture.
   swipe(233,69,233,160); if(ui.octave()!=4) result=71;
   ui.change_octave(4); swipe(233,233,233,120); if(ui.octave()!=8) result=59;
   ui.change_octave(-8); swipe(233,233,233,350); if(ui.octave()!=0) result=60;
   ui.change_octave(4);
   // Invisible note targets reach the circular display edge, while leaving
   // square corners outside it inactive. An outward slide must keep its note.
   const int edge_ons=transport.ons,edge_offs=transport.offs;
   click(233,1); if(transport.ons!=edge_ons+1 || transport.offs!=edge_offs+1) result=137;
   mouse_x=233; mouse_y=69; mouse_down=true; pump(40);
   const int edge_pitch=transport.last_on;
   mouse_y=1; pump(40);
   if(transport.ons!=edge_ons+2 || transport.offs!=edge_offs+1 || transport.last_on!=edge_pitch) result=138;
   mouse_down=false; pump(40);
   if(transport.offs!=edge_offs+2 || transport.last_off!=edge_pitch) result=139;
   click(1,1); if(transport.ons!=edge_ons+2) result=140;
   // Reproduce power-on with PWR still down, then release and use the same UI.
   sm::Buttons startup_power(20,40); startup_power.require_release(1,100);
   auto startup_event=[](int,sm::ButtonEvent event,void* ctx) { static_cast<sm::Ui*>(ctx)->power_button(event); };
   for(uint32_t t=0;t<=2000;t+=2) startup_power.update(1,true,t,startup_event,&ui);
   for(uint32_t t=2002;t<=2120;t+=2) startup_power.update(1,false,t,startup_event,&ui);
   if(ui.is_setup()) result=110;
   for(uint32_t t=2122;t<=2800;t+=2) startup_power.update(1,true,t,startup_event,&ui);
   if(!ui.is_setup()) result=111;
   for(uint32_t t=2802;t<=2842;t+=2) startup_power.update(1,false,t,startup_event,&ui);
   if(!ui.is_setup()) result=112; // opening release must not immediately close
   legacy_power(sm::ButtonEvent::Release); // orphan/duplicate release cannot save
   legacy_power(sm::ButtonEvent::Hold); // orphan hold cannot poison next press
   if(!ui.is_setup()) result=113;
   for(uint32_t t=2844;t<=2864;t+=2) startup_power.update(1,true,t,startup_event,&ui);
   for(uint32_t t=2866;t<=2906;t+=2) startup_power.update(1,false,t,startup_event,&ui);
   if(ui.is_setup()) result=114;
   legacy_power(sm::ButtonEvent::Hold); legacy_power(sm::ButtonEvent::Release);
   if(ui.is_setup()) result=115;
   // An interrupted PMIC cycle must not save on a later/orphan release.
   ui.open_setup();
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Cancel);
   legacy_power(sm::ButtonEvent::Release);
   if(!ui.is_setup()) result=116;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(ui.is_setup()) result=117;
   // PWR hold enters setup; opening ends a held note on its original channel.
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(ui.is_setup()) result=80;
   if(!ui.is_chords() || ui.is_arpeggiator()) result=190;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(!ui.is_arpeggiator() || ui.is_chords()) result=121;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(ui.is_chords() || ui.is_arpeggiator()) result=191;
   mouse_x=233; mouse_y=69; mouse_down=true; pump(40);
   const int before_setup_off=transport.offs;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Hold); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(!ui.is_setup() || ui.setup_page()!=0 || transport.offs!=before_setup_off+1) result=81;
   mouse_down=false; pump(40);
   auto visible_label=[](auto&& self,lv_obj_t* object,const char* value)->lv_obj_t* {
     if(lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN)) return nullptr;
     if(lv_obj_check_type(object,&lv_label_class) && std::string(lv_label_get_text(object))==value) return object;
     for(uint32_t i=0;i<lv_obj_get_child_count(object);++i) if(auto* found=self(self,lv_obj_get_child(object,i),value)) return found;
     return nullptr;
   };
   // Same wheel as KEY: a one-row drag, snap, then mouse-wheel navigation.
   // A contact starting in the channel wheel never becomes page navigation,
   // including horizontal/diagonal moves that leave its original bounds.
   swipe(144,308,350,267); if(ui.setup_page()!=0) result=118;
   swipe(144,308,55,267); if(ui.setup_page()!=0) result=119;
   swipe(144,308,350,195); if(ui.setup_page()!=0) result=120;
   ui.cancel_setup(); ui.open_setup(); // Restore the unconfirmed channel draft.
   mouse_x=144; mouse_y=308; mouse_down=true; pump(40);
   mouse_y=252; pump(40); pump(100); mouse_down=false; pump(220);
   ui.scroll(144,308,-14); pump(220);
   if(app.setup!=regression_setup || settings.load_setup()!=regression_setup) result=82;
   capture("smoke-setup-channel.bmp");
   swipe(233,390,150,390);
   if(ui.setup_page()!=1 || !ui.is_setup()) result=83;
   // Continue beyond either end: pages wrap in both swipe directions.
   swipe(233,390,150,390); if(ui.setup_page()!=2) result=92;
   capture("smoke-travel-mode.bmp");
   swipe(233,410,150,410); if(ui.setup_page()!=3) result=320;
   capture("smoke-global-scale-on.bmp");
   click(233,233); pump(60); capture("smoke-global-scale-off.bmp");
   if(!app.setup.global_scale || !settings.load_setup().global_scale) result=321;
   click(233,233); // Restore draft ON; confirmation below must retain the default.
   swipe(233,410,150,410); if(ui.setup_page()!=4) result=370;
   capture("smoke-save-load-empty.bmp");
   swipe(233,218,350,218); if(ui.setup_page()!=4) result=371;
   click(155,370); pump(60);
   sm::MusicPreset slot_saved; if(settings.load_slot(0,slot_saved)) result=372; // No write before confirmation.
   capture("smoke-save-name-empty.bmp");
   swipe(233,410,150,410); if(ui.setup_page()!=4) result=373;
   // Blank -> A; blank -> 9 in the other direction, then blank -> A again.
   ui.scroll(86,233,-1); ui.scroll(128,233,1); pump(220);
   click(310,370); pump(60); if(settings.load_slot(0,slot_saved)) result=374; // CANCEL.
   click(155,370); pump(40);
   ui.scroll(86,233,-1); ui.scroll(128,233,-2); ui.scroll(170,233,1); pump(220);
   capture("smoke-save-name-filled.bmp");
   click(155,370); pump(60);
   if(!settings.load_slot(0,slot_saved) || slot_saved.name[0]!='A' || slot_saved.name[1]!='B' || slot_saved.name[2]!='9') result=375;
   capture("smoke-save-load-saved.bmp");
   // A filled slot prefills the name; CANCEL must preserve its existing data.
   click(155,370); pump(40); ui.scroll(86,233,-1); pump(220);
   click(310,370); pump(40); sm::MusicPreset unchanged;
   if(!settings.load_slot(0,unchanged) || !(unchanged==slot_saved)) result=376;
   ui.scroll(233,218,-100); pump(220); click(155,370); pump(60);
   capture("smoke-save-name-new-slot.bmp");
   click(155,370); pump(60); sm::MusicPreset eighth;
   if(!settings.load_slot(1,eighth) || sm::music_slot_name(eighth)!="---") result=377;
   ui.scroll(233,218,100); pump(220);
   if(!app.save_chords({5,1})) result=381;
   const auto before_load=app.chords;
   click(310,370); pump(60); capture("smoke-load-confirm.bmp");
   if(app.chords!=before_load) result=382;
   click(310,370); pump(40); if(app.chords!=before_load) result=383; // CANCEL.
   click(310,370); pump(40); click(155,370); pump(80);
   if(app.chords!=slot_saved.chords || app.setup.channel!=1 || settings.load_setup().channel!=1) result=384;
   capture("smoke-save-load-loaded.bmp");
   const auto count_before_delete=transport.ons;
   click(397,218); pump(60); capture("smoke-delete-confirm.bmp");
   sm::MusicPreset delete_check;
   if(!settings.load_slot(0,delete_check) || delete_check.name!=slot_saved.name) result=390;
   click(310,370); pump(40); if(!settings.load_slot(1,delete_check)) result=391;
   click(397,218); pump(40); click(155,370); pump(60);
   if(!settings.load_slot(0,delete_check) || sm::music_slot_name(delete_check)!="---" || settings.load_slot(1,delete_check)) result=392;
   if(transport.ons!=count_before_delete) result=393;
   capture("smoke-slots-compacted.bmp");
   // Delete the final occupied slot; the icon becomes disabled and stays inert.
   click(397,218); pump(40); click(155,370); pump(60);
   click(397,218); pump(40);
   if(settings.load_slot(0,delete_check) || visible_label(visible_label,lv_screen_active(),"DELETE")) result=394;
   capture("smoke-slots-empty-trash.bmp");
   swipe(233,410,150,410); if(ui.setup_page()!=0) result=121;
   swipe(233,410,320,410); if(ui.setup_page()!=4) result=378;
   swipe(233,410,320,410); if(ui.setup_page()!=3) result=322;
   swipe(233,410,320,410); if(ui.setup_page()!=2) result=122;
   swipe(233,410,320,410); if(ui.setup_page()!=1) result=93;
   mouse_x=445; mouse_y=233; mouse_down=true; pump(40); capture("smoke-setup-color.bmp");
   if(settings.load_setup()!=regression_setup) result=84;
   const auto setup_ons=transport.ons;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(transport.ons!=setup_ons) result=91;
   mouse_down=false; pump(40);
   const sm::Setup chosen{16,sm::accent_for_hue(270),false,{sm::Waveform::Triangle,10,400}};
   if(ui.is_setup() || app.setup!=chosen || settings.load_setup()!=chosen) result=85;
   click(233,69); if(transport.last_channel!=16) result=86;
   capture("smoke-setup-applied.bmp");
   // Saving scales must preserve the confirmed setup; cancel must not write a draft.
   ui.open(); if(!ui.confirm() || settings.load_setup()!=chosen) result=87;
   ui.open_setup(); if(ui.setup_page()!=1) result=102; // Reopen the last-used color page.
   swipe(233,390,320,390);
   ui.scroll(144,308,1); swipe(233,390,150,390); click(21,233);
   swipe(233,390,320,390); if(ui.setup_page()!=0) result=88;
   swipe(233,390,320,390); if(ui.setup_page()!=4) result=379;
   swipe(233,390,320,390); if(ui.setup_page()!=3) result=323;
   swipe(233,390,320,390); if(ui.setup_page()!=2) result=94;
   swipe(233,410,150,410); if(ui.setup_page()!=3) result=324;
   swipe(233,410,150,410); if(ui.setup_page()!=4) result=380;
   swipe(233,410,150,410); if(ui.setup_page()!=0) result=95;
   ui.cancel_setup(); // Escape remains an optional discard shortcut in the simulator.
   if(ui.is_setup() || app.setup!=chosen || settings.load_setup()!=chosen) result=89;
   ui.open_setup(); if(ui.setup_page()!=0) result=103; // Channel page is remembered as well.
   ui.scroll(144,308,-1); pump(220); // 16 wraps to 1, like KEY.
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(ui.is_setup() || app.setup.channel!=1 || settings.load_setup().channel!=1) result=90;
   // Travel controls are persisted and route playable notes exclusively locally.
   ui.open_setup(); swipe(233,410,320,410); swipe(233,410,320,410); swipe(233,410,320,410); if(ui.setup_page()!=2) result=123;
   click(233,125);
   ui.scroll(109,280,-3); ui.scroll(233,280,-1); ui.scroll(357,280,-1); pump(220);
   capture("smoke-travel-on.bmp");
   if(brightness(108,280)==0) result=133; // Saw reset stroke must be visible.
   if(brightness(88,280)==0) result=135; // Its sloping ramp must be continuous too.
   if(brightness(149,280)==0) result=136; // Second reset completes two full cycles.
   const int travel_ons=transport.ons;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(!app.setup.travel || !settings.load_setup().travel || app.setup.synth.waveform!=sm::Waveform::Saw || app.setup.synth.attack_ms!=20 || app.setup.synth.decay_ms!=800) result=124;
   click(233,69); if(transport.ons!=travel_ons) result=125;
   // Time wheels have hard endpoints, including repeated outward scrolling.
   ui.open_setup(); ui.scroll(233,280,100); ui.scroll(357,280,100); pump(220);
   capture("smoke-travel-min.bmp");
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(app.setup.synth.attack_ms!=0 || app.setup.synth.decay_ms!=10) result=127;
   ui.open_setup(); ui.scroll(233,280,1); ui.scroll(357,280,1); pump(220);
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(app.setup.synth.attack_ms!=0 || app.setup.synth.decay_ms!=10) result=128;
   ui.open_setup(); ui.scroll(233,280,-100); ui.scroll(357,280,-100); pump(220);
   capture("smoke-travel-max.bmp");
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(app.setup.synth.attack_ms!=2000 || app.setup.synth.decay_ms!=4000) result=129;
   ui.open_setup(); ui.scroll(233,280,-1); ui.scroll(357,280,-1); pump(220);
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(app.setup.synth.attack_ms!=2000 || app.setup.synth.decay_ms!=4000) result=130;
   // Five icons wrap cyclically; the new warm sine has its own persistent ID.
   ui.open_setup(); ui.scroll(109,280,1); pump(220); capture("smoke-travel-sine.bmp");
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(app.setup.synth.waveform!=sm::Waveform::Sine || settings.load_setup().synth.waveform!=sm::Waveform::Sine) result=142;
   ui.open_setup(); ui.scroll(109,280,1); pump(220); capture("smoke-travel-pulse.bmp");
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(app.setup.synth.waveform!=sm::Waveform::Pulse || settings.load_setup().synth.waveform!=sm::Waveform::Pulse) result=141;
   ui.open_setup(); ui.scroll(109,280,1); pump(220); capture("smoke-travel-triangle.bmp");
   ui.scroll(109,280,1); pump(220); capture("smoke-travel-square.bmp");
   if(brightness(109,280)==0) result=134; // Square vertical transition, not dashes.
   ui.scroll(109,280,1); pump(220); capture("smoke-travel-saw.bmp");
   swipe(75,280,160,280); if(ui.setup_page()!=2) result=131;
   swipe(400,280,315,280); if(ui.setup_page()!=2) result=132;
   ui.cancel_setup(); // Preserve confirmed max values and Pulse.
   ui.open_setup(); click(233,125);
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(app.setup.travel || settings.load_setup().travel) result=126;
   ui.open_setup(); swipe(233,410,150,410); // Restore the channel page for later tests.
   ui.cancel_setup();
   ui.open_setup(); for(int i=0;i<5 && ui.setup_page()!=0;++i) swipe(233,410,150,410);
   click(233,149); // USB draft does not change the stored route until PWR.
   if(app.setup.midi_route!=sm::MidiRoute::Auto) result=430;
   ui.cancel_setup(); ui.open_setup();
   click(333,149); legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(app.setup.midi_route!=sm::MidiRoute::Ble || settings.load_setup().midi_route!=sm::MidiRoute::Ble) result=431;
   ui.open_setup(); click(133,149); legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(app.setup.midi_route!=sm::MidiRoute::Auto) result=432;
   // The photographed PWR key is a real mouse target using the same debounce/hold path.
   const int power_ons=transport.ons;
   SDL_Event power_event{}; power_event.type=SDL_MOUSEBUTTONDOWN; power_event.button.button=SDL_BUTTON_LEFT;
   power_event.button.x=int(device_mask.width*.814f); power_event.button.y=int(device_mask.height*.206f);
   if(!power_pointer(power_event)) result=97;
   pump(720); if(!ui.is_setup()) result=98; capture("smoke-power-button-held.bmp");
   power_event.type=SDL_MOUSEBUTTONUP; power_pointer(power_event); pump(60);
   if(!ui.is_setup()) result=99; // Release of the opening hold must not confirm.
   power_event.type=SDL_MOUSEBUTTONDOWN; power_pointer(power_event); pump(60);
   power_event.type=SDL_MOUSEBUTTONUP; power_pointer(power_event); pump(60);
   if(ui.is_setup() || transport.ons!=power_ons) result=100;
   // BOOT countdown is also the battery view: preserve held notes on abort.
   SDL_Event boot_event{}; boot_event.type=SDL_MOUSEBUTTONDOWN; boot_event.button.button=SDL_BUTTON_LEFT;
   boot_event.button.x=int(device_mask.width*.831f); boot_event.button.y=int(device_mask.height*.763f);
   second_x=233; second_y=69; second_down=true; pump(40);
   const int shutdown_ons=transport.ons,shutdown_offs=transport.offs;
   if(!power_pointer(boot_event)) result=104;
   pump(120); capture("smoke-power-off-start.bmp");
   ui.set_battery({true,true,true,72}); pump(40);
   lv_area_t battery_bounds;
   lv_obj_get_coords(lv_obj_get_child(lv_obj_get_child(lv_screen_active(),-1),-1),&battery_bounds);
   if(brightness(battery_bounds.x1+24,battery_bounds.y1+19)!=0 || brightness(battery_bounds.x1+13,battery_bounds.y1+56)==0) result=168; // Four bars.
   capture("smoke-power-off-battery.bmp");
   ui.set_battery({true,true,true,100}); pump(40);
   if(brightness(battery_bounds.x1+24,battery_bounds.y1+19)==0) result=169; // Five bars.
   std::vector<int> bar_heights,bar_gaps; bool in_bar=false; int run=0;
   for(int y=13;y<=62;++y) {
     const bool lit=brightness(battery_bounds.x1+24,battery_bounds.y1+y)>5;
     if(lit!=in_bar) {
       if(in_bar) bar_heights.push_back(run); else if(!bar_heights.empty()) bar_gaps.push_back(run);
       run=0; in_bar=lit;
     }
     ++run;
   }
   if(bar_heights.size()!=5 || bar_gaps.size()!=4 ||
     !std::all_of(bar_heights.begin(),bar_heights.end(),[&](int n){return n==bar_heights.front();}) ||
     !std::all_of(bar_gaps.begin(),bar_gaps.end(),[&](int n){return n==bar_gaps.front();})) result=173;
   capture("smoke-power-off-battery-full.bmp");
   ui.set_battery({true,true,true,72,false,true}); pump(40);
   if(brightness(battery_bounds.x1+13,battery_bounds.y1+56)!=0 || brightness(battery_bounds.x1+26,battery_bounds.y1+38)==0) result=172; // Bolt, no bars.
   capture("smoke-power-off-battery-charging.bmp");
   ui.set_battery({true,false,false,-1}); pump(40);
   capture("smoke-power-off-no-battery.bmp");
   ui.set_battery({true,true,false,-1}); pump(40);
   ui.set_battery({true,true,true,72}); pump(40);
   pump(1380); capture("smoke-power-off-half.bmp");
   if(!ui.power_off_visible() || transport.offs!=shutdown_offs || brightness(233,38)!=0) result=105;
   boot_event.type=SDL_MOUSEBUTTONUP; power_pointer(boot_event); pump(60);
   if(ui.power_off_visible() || transport.ons!=shutdown_ons || !running) result=106;
   if(!(settings.load_arpeggiator()==ui.arpeggiator().preset(ui.arp_page()))) result=340;
   second_down=false; pump(40);
   if(transport.offs!=shutdown_offs+1) result=420;
   // Physical release while the wheel is covered still sends exactly one Off.
   second_down=true; pump(40);
   const int battery_view_ons=transport.ons,battery_view_offs=transport.offs;
   ui.begin_power_off(); pump(40);
   if(transport.offs!=battery_view_offs) result=421;
   second_down=false; pump(50);
   if(transport.offs!=battery_view_offs+1) result=422;
   // A new contact begun on the overlay must not become a note on dismissal.
   second_down=true; pump(40); ui.cancel_power_off(); pump(40);
   if(transport.ons!=battery_view_ons) result=423;
   second_down=false; pump(40);
   // An unconfirmed setup draft and the scale draft survive a cancelled countdown.
   ui.open_setup(); for(int i=0;i<5 && ui.setup_page()!=0;++i) swipe(233,390,150,390);
   const int setup_page=ui.setup_page(); ui.scroll(144,308,-1); pump(220);
   boot_event.type=SDL_MOUSEBUTTONDOWN; power_pointer(boot_event); pump(400);
   ui.scroll(233,233,-3); if(ui.change_octave(1)) result=113;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(!ui.is_setup() || !ui.power_off_visible() || ui.setup_page()!=setup_page) result=107;
   boot_event.type=SDL_MOUSEBUTTONUP; power_pointer(boot_event); pump(60);
   if(ui.power_off_visible() || !ui.is_setup() || !ui.confirm_setup() || app.setup.channel!=2) result=108;
   ui.open(); ui.scroll(139,233,-1); pump(220); const auto scale_draft=app.draft;
   second_x=139; second_y=233; second_down=true; pump(40);
   boot_event.type=SDL_MOUSEBUTTONDOWN; power_pointer(boot_event); pump(100);
   second_y=173; pump(100); // A still-held menu finger cannot scroll behind the overlay.
   boot_event.type=SDL_MOUSEBUTTONUP; power_pointer(boot_event); pump(60);
   second_down=false; pump(40);
   if(ui.power_off_visible() || !ui.is_selecting() || app.draft!=scale_draft) result=109;
   ui.back();
   // Verify scaling and letterbox coordinates at small and rectangular sizes.
   for(auto size:std::vector<SDL_Point>{{180,180},{800,600}}) {
     SDL_SetWindowSize(window,size.x,size.y); SDL_PumpEvents();
     SDL_RenderSetLogicalSize(renderer,device_mask.width,device_mask.height); present();
     auto centre=window_to_display(size.x/2,size.y/2);
     if(std::abs(centre.x-233)>1 || std::abs(centre.y-233)>1) result=12;
     int wx,wy; display_to_window(233,69,&wx,&wy);
     auto root=window_to_display(wx,wy);
     const int pixel_tolerance=int(std::ceil(466.f*device_mask.width/device_mask.opening.w/std::min(size.x,size.y)))+1;
     if(std::abs(root.x-233)>pixel_tolerance || std::abs(root.y-69)>pixel_tolerance) result=13;
     auto bezel=logical_to_display(device_mask.opening.x-25,device_mask.opening.y+device_mask.opening.h/2);
     if(bezel.x>=0 || std::abs(bezel.y-233)>1) result=96;
     SDL_RenderLogicalToWindow(renderer,device_mask.width*.814f,device_mask.height*.206f,&wx,&wy);
     float power_x,power_y; SDL_RenderWindowToLogical(renderer,wx,wy,&power_x,&power_y);
     if(!device_mask.power_hit(power_x,power_y)) result=101;
     SDL_RenderLogicalToWindow(renderer,device_mask.width*.831f,device_mask.height*.763f,&wx,&wy);
     SDL_RenderWindowToLogical(renderer,wx,wy,&power_x,&power_y);
     if(!device_mask.boot_hit(power_x,power_y)) result=110;
     if(size.x>size.y && window_to_display(0,size.y/2).x>=0) result=14;
     capture(size.x==180?"smoke-small.bmp":"smoke-resized.bmp");
   }
   // Arpeggiator: latching ring, central pages and a bounded probability wheel.
   SDL_SetWindowSize(window,670,670); SDL_PumpEvents();
   SDL_RenderSetLogicalSize(renderer,device_mask.width,device_mask.height);
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(!ui.is_chords()) result=225;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(!ui.is_arpeggiator()) result=123;
   const auto previous_scale=app.current;
   ui.arpeggiator().toggle(0); ui.arpeggiator().toggle(3);
   ui.scroll(138,245,-1); pump(220);
   if(ui.is_selecting() || app.current.root!=(previous_scale.root+1)%12 || settings.load()!=app.current) result=141;
   ui.scroll(270,245,-1); pump(220);
   if(ui.is_selecting() || app.current.mode!=(previous_scale.mode+1)%int(sm::modes().size()) || ui.arpeggiator().state().selected!=9) result=142;
   ui.arpeggiator().clear();
   capture("smoke-arp-notes-inactive.bmp");
   const int arp_before=transport.ons;
   click(233,43); pump(300); // Live selection joins the existing beat, not immediately.
   if(ui.arpeggiator().state().selected!=1 || transport.ons<=arp_before) result=124;
   capture("smoke-arp-scale.bmp");
   const int arp_power_ons=transport.ons;
   const int arp_power_offs=transport.offs,arp_power_note=ui.arpeggiator().state().playing;
   const auto arp_power_selected=ui.arpeggiator().state().selected;
   ui.begin_power_off();
   if(ui.arpeggiator().state().suspended || !ui.arpeggiator().state().enabled ||
      transport.offs!=arp_power_offs || ui.arpeggiator().state().playing!=arp_power_note) result=427;
   pump(300);
   if(transport.ons<=arp_power_ons || ui.arpeggiator().state().selected!=arp_power_selected) result=428;
   const int arp_cancel_offs=transport.offs,arp_cancel_note=ui.arpeggiator().state().playing;
   ui.cancel_power_off();
   if(ui.arpeggiator().state().suspended || !ui.arpeggiator().state().enabled ||
      transport.offs!=arp_cancel_offs || ui.arpeggiator().state().playing!=arp_cancel_note) result=429;
   swipe(138,245,260,245); if(ui.arp_page()!=0) result=143;
   swipe(290,150,180,150); if(ui.arp_page()!=1) result=125;
   ui.scroll(190,230,1); ui.scroll(270,230,-1); pump(220);
   if(ui.arpeggiator().state().settings.low!=2 || ui.arpeggiator().state().settings.high!=6) result=126;
   capture("smoke-arp-range.bmp");
   swipe(290,150,180,150); if(ui.arp_page()!=2) result=127;
   ui.scroll(233,245,-1); pump(220); if(ui.arpeggiator().state().settings.pattern!=sm::ArpPattern::Down) result=128;
   capture("smoke-arp-pattern.bmp");
   ui.scroll(233,233,-3); pump(220); if(ui.arpeggiator().state().settings.pattern!=sm::ArpPattern::Shuffle) result=144;
   capture("smoke-arp-shuffle.bmp");
   ui.scroll(233,233,-1); pump(220); if(ui.arpeggiator().state().settings.pattern!=sm::ArpPattern::Converge) result=145;
   capture("smoke-arp-converge.bmp");
   ui.scroll(233,233,-1); pump(220); if(ui.arpeggiator().state().settings.pattern!=sm::ArpPattern::Anchor) result=146;
   capture("smoke-arp-anchor.bmp");
   ui.scroll(233,233,-1); pump(220); if(ui.arpeggiator().state().settings.pattern!=sm::ArpPattern::LowHigh) result=404;
   capture("smoke-arp-low-high.bmp");
   for(const auto pattern:{sm::ArpPattern::Gravity,sm::ArpPattern::Pivot}) {
     ui.scroll(233,233,-1); pump(220); if(ui.arpeggiator().state().settings.pattern!=pattern) result=406;
   }
   capture("smoke-arp-pivot.bmp");
   ui.scroll(233,233,-1); pump(220); if(ui.arpeggiator().state().settings.pattern!=sm::ArpPattern::Up) result=166;
   swipe(290,150,180,150); if(ui.arp_page()!=3) result=129;
   ui.scroll(280,245,-1); pump(220); if(ui.arpeggiator().state().settings.timing!=sm::ArpTiming::Dotted) result=130;
   capture("smoke-arp-divisor.bmp");
   swipe(290,150,180,150); if(ui.arp_page()!=4) result=131;
   mouse_x=300; mouse_y=233; mouse_down=true; pump(60);
   const int tap_lit=brightness(300,270); capture("smoke-arp-tap-held.bmp");
   mouse_down=false; pump(220); const int tap_half=brightness(300,270);
   pump(320); const int tap_dark=brightness(300,270);
   if(!(tap_lit>tap_half && tap_half>tap_dark && tap_dark==0)) result=161;
   capture("smoke-arp-transport.bmp");
   if(ui.arpeggiator().state().bpm!=120) result=147;
   swipe(300,233,350,233); if(ui.arp_page()!=4) result=162;
   mouse_x=300; mouse_y=233; mouse_down=true; pump(40);
   ui.open_setup(); mouse_down=false; pump(40); ui.cancel_setup(); pump(540);
   if(brightness(300,270)!=0) result=163; // Interrupted TAP never remains lit.
   ui.scroll(162,233,-1000); pump(220); if(ui.arpeggiator().state().bpm!=300) result=148;
   ui.scroll(162,233,1000); pump(220); if(ui.arpeggiator().state().bpm!=40) result=149;
   ui.arpeggiator().tempo(100); pump(40);
   click(300,233); pump(360); click(300,233); pump(40);
   const int tapped_bpm=ui.arpeggiator().state().bpm;
   if(tapped_bpm<110 || tapped_bpm>130) result=150;
   capture("smoke-arp-bpm-tap.bmp");
   ui.scroll(162,233,-1); pump(220);
   if(ui.arpeggiator().state().bpm!=tapped_bpm+1) result=151; // Wheel follows TAP.
   ui.open_setup();
   for(int i=0;i<5 && ui.setup_page()!=0;++i) swipe(233,390,150,390);
   click(315,308); if(app.setup.receive_clock) result=152;
   ui.cancel_setup(); if(app.setup.receive_clock || settings.load_setup().receive_clock) result=153;
   ui.open_setup(); click(315,308); capture("smoke-setup-midi-clock-on.bmp");
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(!app.setup.receive_clock || !settings.load_setup().receive_clock || ui.arpeggiator().state().settings.clock!=sm::ArpClock::Midi || ui.arp_page()!=5) result=154;
   // External BPM remains measurable with setup open and playback suspended.
   ui.open_setup();

   const uint64_t clock_now=uint64_t(SDL_GetTicks())*1000;
   for(int i=0;i<=24;++i) ui.midi_realtime(0xf8,clock_now-500000+uint64_t(i)*500000/24);
   pump(120);
   auto* measured=visible_label(visible_label,lv_screen_active(),"120");
   auto* unit=visible_label(visible_label,lv_screen_active(),"BPM");
   if(!measured || !unit) result=218;
   else {
     const auto number_color=lv_obj_get_style_text_color(measured,0),unit_color=lv_obj_get_style_text_color(unit,0);
     const auto accent=lv_color_hex(ui.accent_color());
     if(number_color.red!=255 || number_color.green!=255 || number_color.blue!=255 ||
        unit_color.red!=accent.red || unit_color.green!=accent.green || unit_color.blue!=accent.blue) result=219;
     lv_area_t row; lv_obj_get_coords(lv_obj_get_parent(measured),&row);
     if(std::abs(row.x1+row.x2-465)>1 || row.y1<350) result=220;
   }
   capture("smoke-setup-clock-bpm.bmp");
   swipe(233,410,150,410);
   if(visible_label(visible_label,lv_screen_active(),"BPM")) result=221;
   swipe(180,233,290,233); pump(600); // Start inside the color ring's free centre.
   if(!visible_label(visible_label,lv_screen_active(),"--") || ui.arpeggiator().state().clock_bpm!=0) result=222;
   capture("smoke-setup-clock-missing.bmp");
   ui.cancel_setup();
   swipe(290,150,180,150); if(ui.arp_page()!=6) result=174;
   if(ui.arpeggiator().state().settings.block_size!=0) result=250;
   ui.scroll(233,233,-1); pump(200); if(ui.arpeggiator().state().settings.block_size!=7) result=251;
   capture("smoke-arp-blocksize.bmp");
   ui.scroll(233,233,-100); pump(200); if(ui.arpeggiator().state().settings.block_size!=2) result=252;
   ui.scroll(233,233,100); pump(200); if(ui.arpeggiator().state().settings.block_size!=0) result=253;
   swipe(290,150,180,150); if(ui.arp_page()!=8) result=405;
   swipe(290,150,180,150); if(ui.arp_page()!=9) result=396;
   if(ui.arpeggiator().state().settings.ratchet_every!=0 || ui.arpeggiator().state().settings.ratchet_count!=2) result=397;
   ui.scroll(181,248,-4); ui.scroll(285,248,-1); pump(220);
   if(ui.arpeggiator().state().settings.ratchet_every!=4 || ui.arpeggiator().state().settings.ratchet_count!=3 || ui.arpeggiator().state().settings.gate!=50 || ui.arpeggiator().state().settings.gate_before_ratchet!=100) result=398;
   capture("smoke-arp-ratchet.bmp");
   swipe(181,248,300,248); if(ui.arp_page()!=9) result=399;
   ui.scroll(181,248,-100); ui.scroll(285,248,-100); pump(220);
   if(ui.arpeggiator().state().settings.ratchet_every!=16 || ui.arpeggiator().state().settings.ratchet_count!=4) result=400;
   ui.scroll(181,248,100); ui.scroll(285,248,100); pump(220);
   if(ui.arpeggiator().state().settings.ratchet_every!=0 || ui.arpeggiator().state().settings.ratchet_count!=2 || ui.arpeggiator().state().settings.gate!=100 || ui.arpeggiator().state().settings.gate_before_ratchet!=0) result=401;
   swipe(290,150,180,150); if(ui.arp_page()!=7) result=254;
   swipe(290,150,180,150); if(ui.arp_page()!=0) result=155;
   swipe(180,150,290,150); if(ui.arp_page()!=7) result=175;
   swipe(180,150,290,150); if(ui.arp_page()!=9) result=402;
   swipe(180,150,290,150); if(ui.arp_page()!=8) result=342;
   swipe(180,150,290,150); if(ui.arp_page()!=6) result=255;
   swipe(180,150,290,150); if(ui.arp_page()!=5) result=156;
   swipe(180,150,290,150); if(ui.arp_page()!=3) result=157; // BPM skipped backwards.
   swipe(290,150,180,150); if(ui.arp_page()!=5) result=158; // And forwards.
   swipe(180,150,290,150); ui.open_setup(); click(315,308);
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(app.setup.receive_clock || settings.load_setup().receive_clock || ui.arpeggiator().state().settings.clock!=sm::ArpClock::Tap) result=159;
   swipe(290,150,180,150); if(ui.arp_page()!=4) result=160; // BPM restored.
   swipe(290,150,180,150); if(ui.arp_page()!=5) result=132;
   ui.scroll(233,245,-1000); pump(220);
   if(ui.arpeggiator().state().settings.probability!=5) result=133;
   swipe(233,245,300,245); if(ui.arp_page()!=5) result=134;
   ui.scroll(233,245,1000); pump(220);
   if(ui.arpeggiator().state().settings.probability!=100) result=135;
   ui.scroll(233,245,-1); pump(220);
   if(ui.arpeggiator().state().settings.probability!=95) result=395;
   ui.scroll(233,245,1); pump(220);
   capture("smoke-arp-probability.bmp");
   second_x=233; second_y=245; second_down=true; pump(40); second_y=265; pump(40);
   ui.open_setup(); second_down=false; pump(40); ui.cancel_setup(); pump(220);
   ui.scroll(233,245,1000); pump(220);
   if(ui.arpeggiator().state().settings.probability!=100) result=140;
   swipe(290,150,180,150); if(ui.arp_page()!=6) result=176;
   swipe(290,150,180,150); if(ui.arp_page()!=8) result=343;
   if(ui.arpeggiator().state().settings.gate!=100) result=344;
   ui.scroll(233,233,-10); pump(220); if(ui.arpeggiator().state().settings.gate!=50) result=345;
   capture("smoke-arp-gate.bmp");
   ui.scroll(233,233,-1000); pump(220); if(ui.arpeggiator().state().settings.gate!=5) result=346;
   swipe(233,233,300,233); if(ui.arp_page()!=8) result=347;
   if(settings.load_arpeggiator().settings.gate!=5) result=348;
   ui.scroll(233,233,1000); pump(220); if(ui.arpeggiator().state().settings.gate!=100) result=349;
   swipe(290,150,180,150); if(ui.arp_page()!=9) result=403;
   swipe(290,150,180,150); if(ui.arp_page()!=7) result=256;
   if(ui.arpeggiator().state().settings.swing!=0) result=177;
   ui.scroll(233,233,-1000); pump(220); if(ui.arpeggiator().state().settings.swing!=50) result=178;
   swipe(233,233,300,233); if(ui.arp_page()!=7) result=179;
   capture("smoke-arp-swing.bmp");
   ui.scroll(233,233,1000); pump(220); if(ui.arpeggiator().state().settings.swing!=0) result=180;
   swipe(290,150,180,150); if(ui.arp_page()!=0) result=136;
   const auto before_blues=app.current;
   ui.scroll(270,245,app.current.mode-sm::blues_mode); pump(220);
   if(app.current.mode!=sm::blues_mode || settings.load()!=app.current) result=226;
   capture("smoke-arp-blues.bmp");
   ui.scroll(270,245,sm::blues_mode-before_blues.mode); pump(220);
   if(app.current!=before_blues) result=227;
   // Setup pauses playback, resumes the selected mode and retains the pool.
   ui.open_setup(); pump(40); const int paused_ons=transport.ons; pump(100);
   if(transport.ons!=paused_ons) result=137;
   ui.cancel_setup(); pump(40); if(!ui.is_arpeggiator()) result=138;
   // Shake reset clears the arpeggiator but keeps its scale and global setup.
   const auto reset_scale=app.current; const auto reset_setup=app.setup; const auto reset_chords=app.chords;
   auto dirty_arp=ui.arpeggiator().state().settings;
   dirty_arp.low=2; dirty_arp.high=7; dirty_arp.pattern=sm::ArpPattern::Anchor;
   dirty_arp.division=32; dirty_arp.timing=sm::ArpTiming::Dotted; dirty_arp.swing=35; dirty_arp.probability=23;
   ui.arpeggiator().settings(dirty_arp); ui.arpeggiator().tempo(177);
   if(!ui.arpeggiator().state().selected) ui.arpeggiator().toggle(0);
   swipe(290,150,180,150); if(ui.arp_page()==0) result=245;
   if(!ui.reset_play_mode() || ui.arp_page()!=0) result=246;
   const auto reset_arp=ui.arpeggiator().state();
   if(reset_arp.selected || reset_arp.playing!=-1 || reset_arp.bpm!=120 || midi.source_active(14)) result=247;
   if(reset_arp.settings.low!=3 || reset_arp.settings.high!=5 || reset_arp.settings.pattern!=sm::ArpPattern::Up ||
      reset_arp.settings.division!=8 || reset_arp.settings.timing!=sm::ArpTiming::Straight || reset_arp.settings.swing!=0 || reset_arp.settings.probability!=100) result=248;
   if(app.current!=reset_scale || settings.load()!=reset_scale || app.setup!=reset_setup || app.chords!=reset_chords) result=249;
   capture("smoke-arp-shake-reset.bmp");
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(ui.is_arpeggiator()) result=139;
   if(ui.is_chords()) result=228;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(!ui.is_chords()) result=181;
   // Global ON synchronizes all modes; switch OFF through the actual setup UI.
   auto linked=app.chords; linked.root=5; linked.mode=4;
   if(!app.save_chords(linked)) result=330;
   ui.refresh();
   if(app.current!=sm::Selection{5,4} || app.arp_scale()!=app.current) result=331;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   if(app.current!=sm::Selection{5,4} || ui.is_chords() || ui.is_arpeggiator()) result=332;
   ui.open(); ui.scroll(105,233,-1); pump(200); ui.confirm();
   if(app.chords.root!=app.current.root || app.chords.mode!=app.current.mode) result=333;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release);
   ui.open_setup(); for(int i=0;i<5 && ui.setup_page()!=3;++i) swipe(233,390,150,390);
   capture("smoke-global-before-confirm.bmp");
   click(233,233); capture("smoke-global-draft-off.bmp");
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(app.setup.global_scale || settings.load_setup().global_scale) std::fprintf(stderr,"Global OFF confirm: app=%d file=%d setup=%d page=%d\n",int(app.setup.global_scale),int(settings.load_setup().global_scale),int(ui.is_setup()),ui.setup_page());
   if(app.setup.global_scale || settings.load_setup().global_scale) result=334;
   if(!app.save_chords({})) result=335; ui.refresh();
   capture("smoke-chords.bmp");
   double_click(); ui.scroll(359,233,-5); pump(200);
   capture("smoke-chord-strum-menu.bmp");
   if(!ui.confirm() || app.chords.strum_ms!=50 || settings.load_chords().strum_ms!=50) result=257;
   double_click(); ui.scroll(359,233,-5); pump(200); ui.back();
   if(app.chords.strum_ms!=50) result=258;
   double_click(); ui.scroll(359,233,-100); pump(200);
   capture("smoke-chord-strum-max.bmp");
   if(!ui.confirm() || app.chords.strum_ms!=250 || settings.load_chords().strum_ms!=250) result=260;
   double_click(); ui.scroll(359,233,100); pump(200);
   if(!ui.confirm() || app.chords.strum_ms!=0) result=259;
   auto clock_setup=app.setup; clock_setup.receive_clock=true;
   if(!app.save_setup(clock_setup)) result=261;
   ui.open_setup(); ui.cancel_setup();
   double_click(); ui.scroll(359,233,-sm::chord_strum_option(8)); pump(200);
   capture("smoke-chord-strum-clock.bmp");
   if(!ui.confirm() || app.chords.strum_division!=8 || app.chords.strum_ms!=0 || settings.load_chords().strum_division!=8) result=262;
   double_click(); ui.scroll(359,233,-1); pump(200);
   capture("smoke-chord-strum-dotted.bmp");
   if(!ui.confirm() || app.chords.strum_division!=8 || app.chords.strum_timing!=1 || settings.load_chords().strum_timing!=1) result=265;
   double_click(); ui.scroll(359,233,2); pump(200);
   capture("smoke-chord-strum-triplet.bmp");
   if(!ui.confirm() || app.chords.strum_timing!=2 || settings.load_chords().strum_timing!=2) result=266;
   double_click(); ui.scroll(359,233,-1); pump(200);
   if(!ui.confirm() || app.chords.strum_timing!=0) result=267;
   clock_setup.receive_clock=false; if(!app.save_setup(clock_setup)) result=263;
   ui.open_setup(); ui.cancel_setup(); double_click();
   capture("smoke-chord-strum-free-restored.bmp"); ui.back();
   if(app.chords.strum_ms!=0 || app.chords.strum_division!=8) result=264;
   const int chord_before=transport.ons, chord_offs=transport.offs;
   mouse_x=233; mouse_y=69; mouse_down=true; pump(60);
   if(transport.ons!=chord_before+4) result=182;
   mouse_down=false; pump(80); if(transport.offs!=chord_offs+4) result=183;
   const int seven_ons=transport.ons,seven_offs=transport.offs;
   for(int pad=0;pad<sm::chord_pad_count;++pad) {
     const double angle=(-90.+pad*360./sm::chord_pad_count)*3.141592653589793/180.;
     click(int(std::lround(233+164*std::cos(angle))),int(std::lround(233+164*std::sin(angle))));
   }
   if(transport.ons!=seven_ons+28 || transport.offs!=seven_offs+28) result=194;
   // Centre-to-pad gestures edit all seven slots without sounding a chord.
   const auto unedited=app.chords;
   const int edit_ons=transport.ons;
   for(int pad=0;pad<sm::chord_pad_count;++pad) {
     const double angle=(-90.+pad*360./sm::chord_pad_count)*3.141592653589793/180.;
     swipe(233,233,int(std::lround(233+164*std::cos(angle))),int(std::lround(233+164*std::sin(angle))));
     if(!ui.is_editing_chord() || ui.edited_chord()!=pad || transport.ons!=edit_ons) result=201;
     if(!ui.confirm() || app.chords!=unedited) result=202;
   }
   swipe(233,233,233,69); ui.scroll(233,233,-4); pump(220);
   capture("smoke-chord-editor-ninth.bmp");
   if(!consistent_redraw()) result=360; // Narrow redraws must leave no old glyph pixels behind.
   if(app.chords!=unedited || transport.ons!=edit_ons+5) result=203;
   ui.back(); if(app.chords!=unedited || ui.is_editing_chord() || ui.is_selecting()) result=204;
   for(int source=2;source<7;++source) if(midi.source_active(source)) result=250;
   if(ui.chord_engine().palette()[0].pitches!=sm::chord_palette(unedited)[0].pitches) result=251;
   swipe(233,233,233,69); ui.scroll(233,233,-4); pump(220); click(233,383);
   auto edited=unedited; edited.variants[0]=sm::ChordVariant::Ninth;
   if(ui.is_selecting() || app.chords!=edited || settings.load_chords()!=edited) result=205;
   capture("smoke-chords-custom-ninth.bmp");
   const int ninth_ons=transport.ons,ninth_offs=transport.offs;
   click(233,69);
   if(transport.ons!=ninth_ons+5 || transport.offs!=ninth_offs+5) result=206;
   // Reopening starts at the saved option; selecting one step lower edits only this pad.
   swipe(233,233,233,69); ui.scroll(233,233,1); pump(220); ui.confirm();
   edited.variants[0]=sm::ChordVariant::Seventh;
   if(app.chords!=edited) result=207;
   swipe(233,233,233,69); ui.scroll(233,233,3); pump(220); ui.confirm();
   if(app.chords!=unedited) result=208;
   // Independent, bounded octave wheel; a shift alone marks/changes the pad.
   swipe(233,233,233,69); const int shift_preview_ons=transport.ons;
   ui.scroll(349,233,-9); pump(220);
   if(app.chords!=unedited) result=211;
   if(transport.ons!=shift_preview_ons+4) result=252;
   ui.scroll(349,233,-1); pump(220); // Already at +1: no duplicate audition.
   if(transport.ons!=shift_preview_ons+4) result=253;
   pump(700);
   for(int source=2;source<7;++source) if(midi.source_active(source)) result=254;
   capture("smoke-chord-editor-octave-up.bmp");
   if(!consistent_redraw()) result=361;
   click(233,383);
   auto shifted_up=unedited; shifted_up.octave_shifts[0]=1;
   if(app.chords!=shifted_up || settings.load_chords()!=shifted_up) result=212;
   capture("smoke-chords-octave-shift.bmp");
   click(233,69);
   if(transport.last_on!=sm::chord_palette(shifted_up)[0].pitches[3]) result=213;
   // Reopening at +1 and moving one step returns to default 0.
   swipe(233,233,233,69); ui.scroll(349,233,1); pump(220); ui.confirm();
   if(app.chords!=unedited) result=214;
   swipe(233,233,233,69); ui.scroll(349,233,9); pump(220); ui.confirm();
   auto shifted_down=unedited; shifted_down.octave_shifts[0]=-1;
   if(app.chords!=shifted_down) result=215;
   click(233,69);
   if(transport.last_on!=sm::chord_palette(shifted_down)[0].pitches[3]) result=216;
   swipe(233,233,233,69); ui.scroll(349,233,-1); pump(220); ui.confirm();
   if(app.chords!=unedited) result=217;
   const int no_edit_ons=transport.ons,edit_octave=ui.octave();
   swipe(233,233,233,397); // Ends in the radial gap, not a segment.
   if(ui.is_selecting() || transport.ons!=no_edit_ons || ui.octave()!=edit_octave) result=209;
   // Opening setup discards an unfinished per-pad edit.
   swipe(233,233,233,69); ui.scroll(233,233,-2); pump(220);
   ui.open_setup(); pump(40); ui.cancel_setup(); pump(40);
   if(ui.is_selecting() || ui.is_editing_chord() || app.chords!=unedited) result=210;
   double_click(); if(!ui.is_selecting()) result=184;
   const auto saved_chord=app.chords; const auto saved_scale=app.current;
   ui.scroll(210,233,-1); ui.scroll(82,233,-1); pump(220); ui.back();
   if(app.chords!=saved_chord || app.current!=saved_scale) result=185;
   double_click(); ui.scroll(82,233,-2); ui.scroll(210,233,-2); pump(220);
   capture("smoke-chord-selection.bmp");
   click(233,383);
   if(ui.is_selecting() || app.chords.root!=(saved_chord.root+2)%12 || app.chords.mode!=sm::chord_modes[(sm::chord_mode_option(saved_chord.mode)+2)%sm::chord_scale_count] || settings.load_chords()!=app.chords || app.current!=saved_scale || settings.load()!=app.current) result=186;
   capture("smoke-chords-harmonic-minor.bmp");
   // All ten chord scale options, including Blues, wrap without changing other modes.
   const auto scene=app.chords;
   double_click();
   for(int i=0;i<sm::chord_scale_count;++i) { ui.scroll(210,233,-1); pump(220); }
   if(!ui.confirm() || app.chords!=scene || settings.load_chords()!=scene) result=192;
   double_click(); ui.scroll(210,233,sm::chord_mode_option(app.chords.mode)-sm::chord_mode_option(sm::blues_mode)); pump(220);
   if(!ui.confirm() || app.chords.mode!=sm::blues_mode || app.current!=saved_scale || settings.load_chords()!=app.chords) result=223;
   capture("smoke-chords-blues.bmp");
   double_click(); ui.scroll(210,233,1); pump(220);
   if(!ui.confirm() || app.chords.mode!=10) result=224;
   double_click(); ui.scroll(82,233,app.chords.root-9); ui.scroll(210,233,sm::chord_mode_option(app.chords.mode)-1); pump(220);
   auto expected_minor=scene; expected_minor.root=9; expected_minor.mode=1;
   if(!ui.confirm() || app.chords!=expected_minor || app.current!=saved_scale) result=195;
   capture("smoke-chords-natural-minor.bmp");
   // Setup and shutdown still stop a chord and return to its own mode.
   mouse_x=233; mouse_y=69; mouse_down=true; pump(40); const int before_setup=transport.offs;
   ui.open_setup(); pump(40); mouse_down=false; pump(40);
   if(transport.offs<before_setup+4) result=187;
   ui.cancel_setup(); pump(40); if(!ui.is_chords()) result=188;
   // Shake reset affects only per-pad edits and persists the complete reset.
   auto reset_selection=app.chords; reset_selection.variants[0]=sm::ChordVariant::Ninth; reset_selection.octave_shifts[6]=1;
   if(!app.save_chords(reset_selection)) result=240;
   ui.refresh();
   mouse_x=233; mouse_y=69; mouse_down=true; pump(40);
   if(ui.can_reset_chord_buttons() || ui.reset_chord_buttons()) result=244;
   mouse_down=false; pump(40);
   ui.open();
   if(ui.can_reset_chord_buttons() || ui.reset_chord_buttons()) result=241;
   ui.back();
   if(!ui.can_reset_chord_buttons() || !ui.reset_chord_buttons()) result=242;
   reset_selection.variants.fill(sm::ChordVariant::Triad); reset_selection.octave_shifts.fill(0);
   if(app.chords!=reset_selection || settings.load_chords()!=reset_selection) result=243;
   capture("smoke-chords-shake-reset.bmp");
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(!ui.is_arpeggiator()) result=229;
   legacy_power(sm::ButtonEvent::Press); legacy_power(sm::ButtonEvent::Release); pump(40);
   if(ui.is_chords() || ui.is_arpeggiator()) result=189;
   const auto shared_scale=app.current;
   double_click(); if(app.draft!=shared_scale) result=193;
   ui.back();
   double_click(); ui.scroll(138,233,app.current.root); ui.scroll(290,233,app.current.mode-sm::blues_mode); pump(220);
   if(!ui.confirm() || app.current!=sm::Selection{0,sm::blues_mode} || settings.load()!=app.current) result=230;
   capture("smoke-keyboard-blues.bmp");
   auto remembered_arp=ui.arpeggiator().state().settings;
   remembered_arp.low=1; remembered_arp.high=7; remembered_arp.pattern=sm::ArpPattern::Anchor;
   remembered_arp.division=32; remembered_arp.timing=sm::ArpTiming::Triplet;
   remembered_arp.probability=67; remembered_arp.swing=23; remembered_arp.block_size=4;
   ui.arpeggiator().settings(remembered_arp); ui.arpeggiator().tempo(173);
   ui.arpeggiator().toggle(0);
   pump(60); // Changed values are saved without waiting for power-off.
   if(!(settings.load_arpeggiator()==ui.arpeggiator().preset(ui.arp_page()))) result=342;
   // Complete countdown last: only after the ring is empty does shutdown commit.
   SDL_SetWindowSize(window,670,670); SDL_PumpEvents();
   SDL_RenderSetLogicalSize(renderer,device_mask.width,device_mask.height);
   boot_event.type=SDL_MOUSEBUTTONDOWN; power_pointer(boot_event); pump(3050);
   if(!boot.committed || !ui.power_off_visible()) result=111;
   if(!(settings.load_arpeggiator()==ui.arpeggiator().preset(ui.arp_page()))) result=341;
   capture("smoke-power-off-finished.bmp");
   boot_event.type=SDL_MOUSEBUTTONUP; power_pointer(boot_event); pump(300);
   if(running || !ui.power_off_visible()) result=112;
   std::printf("UI smoke test: %s (%d)\n",result?"FAIL":"PASS",int(result)); running=false;
 }
 if(controls_test) {
   pump(3800);
   if(companion::hit(85,85,1000,1000)!=companion::Action::Reset || companion::hit(500,500,1000,1000)!=companion::Action::None ||
      companion::hit(85,915,1000,1000)!=companion::Action::None || companion::hit(915,915,1000,1000)!=companion::Action::None)result=401;
   if(ui.has_resettable_state())result=407;
   auto modified=app.chords;modified.variants[0]=sm::ChordVariant::Seventh;app.save_chords(modified);ui.refresh();
   if(!ui.has_resettable_state())result=408;
   corner_action(companion::Action::Reset);if(ui.has_resettable_state())result=409;
   ui.toggle_arpeggiator();if(!ui.is_arpeggiator() || ui.has_resettable_state())result=410;
   ui.arpeggiator().toggle(0);ui.refresh();if(!ui.has_resettable_state())result=411;
   corner_action(companion::Action::Reset);if(ui.has_resettable_state())result=412;
   for(int i=0;i<4 && !ui.is_bounce();++i)ui.toggle_arpeggiator();
   if(ui.has_resettable_state())result=404;
   mouse_x=233;mouse_y=233;mouse_down=true;pump(140);mouse_down=false;pump(100);
   if(!ui.has_resettable_state())result=405;
   corner_action(companion::Action::Reset);
   if(ui.has_resettable_state() || companion::hit(85,85,1000,1000,false)!=companion::Action::None)result=406;
   running=false;
   std::printf("Companion controls test: %s (%d)\n",result?"FAIL":"PASS",int(result));
 }
 while(running) {
#ifdef _WIN32
   if(parent) {
     if(!IsWindow(parent)){running=false;break;}
     RECT bounds{},child_bounds{};GetClientRect(parent,&bounds);GetClientRect(embedded_child,&child_bounds);
     // SDL's cached desktop coordinates must never be reused as child coordinates.
     if(bounds.right>0 && bounds.bottom>0 && (child_bounds.right!=bounds.right || child_bounds.bottom!=bounds.bottom)) {
       SetWindowPos(embedded_child,nullptr,0,0,bounds.right,bounds.bottom,SWP_NOZORDER|SWP_NOACTIVATE);
     }
   }
#endif
   const bool can_reset=ui.has_resettable_state();
   if(can_reset!=corner_reset || corner_accent!=app.setup.accent){corner_reset=can_reset;corner_accent=app.setup.accent;present();}
   SDL_Event e;
   while(SDL_PollEvent(&e)) {
     if(e.type==SDL_MOUSEMOTION || e.type==SDL_MOUSEBUTTONDOWN || e.type==SDL_MOUSEBUTTONUP) {
       const float x=float(e.type==SDL_MOUSEMOTION?e.motion.x:e.button.x),y=float(e.type==SDL_MOUSEMOTION?e.motion.y:e.button.y);
       const auto action=companion::hit(x,y,device_mask.width,device_mask.height,corner_reset);
       if(action!=corner_hover){corner_hover=action;present();}
       if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT && action!=companion::Action::None) {
         corner_pressed=action;mouse_down=false;continue;
       }
       if(e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_LEFT && corner_pressed!=companion::Action::None) {
         const auto pressed=corner_pressed;corner_pressed=companion::Action::None;
         if(action==pressed)corner_action(pressed);
         continue;
       }
     }
     const bool on_power=power_pointer(e);
     if(e.type==SDL_QUIT) running=false;
     if(e.type==SDL_MOUSEMOTION && !on_power) { auto p=logical_to_display(float(e.motion.x),float(e.motion.y)); mouse_x=p.x; mouse_y=p.y; }
     if(!on_power && (e.type==SDL_MOUSEBUTTONDOWN || e.type==SDL_MOUSEBUTTONUP)) if(e.button.button==SDL_BUTTON_LEFT) { auto p=logical_to_display(float(e.button.x),float(e.button.y)); mouse_x=p.x; mouse_y=p.y; mouse_down=e.type==SDL_MOUSEBUTTONDOWN; }
     if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_RIGHT) {
       if(ui.is_bounce()){ui.toggle_bounce_menu();std::printf("Companion action: kinetic menu\n");std::fflush(stdout);}
       else{auto p=logical_to_display(float(e.button.x),float(e.button.y)); second_x=p.x; second_y=p.y; second_down=true;}
     }
     if(e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_RIGHT) second_down=false;
     if(e.type==SDL_MOUSEWHEEL) { int x,y; SDL_GetMouseState(&x,&y); auto p=window_to_display(x,y); ui.scroll(p.x,p.y,e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-e.wheel.y:e.wheel.y); }
     if(e.type==SDL_WINDOWEVENT && (e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event==SDL_WINDOWEVENT_EXPOSED)) present();
     if(e.type==SDL_KEYDOWN || e.type==SDL_KEYUP) {
       auto matches=[&](SDL_Keycode key) {
         if(e.key.keysym.sym==key) return true;
         if(key==SDLK_PLUS) return e.key.keysym.sym==SDLK_KP_PLUS || (e.key.keysym.sym==SDLK_EQUALS && (e.key.keysym.mod&KMOD_SHIFT || e.type==SDL_KEYUP));
         if(key==SDLK_MINUS) return e.key.keysym.sym==SDLK_KP_MINUS;
         return false;
       };
       if(matches(key1)) down[0]=e.type==SDL_KEYDOWN;
       if(matches(key2)) down[1]=e.type==SDL_KEYDOWN;
       if(e.key.keysym.sym==SDLK_p) power_down=e.type==SDL_KEYDOWN;
       if(e.key.keysym.sym==SDLK_b) boot_key=e.type==SDL_KEYDOWN;
       if(e.type==SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym==SDLK_r && (e.key.keysym.mod&KMOD_CTRL) && (e.key.keysym.mod&KMOD_SHIFT)) ui.reset_play_mode();
       if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE && ui.is_setup()) ui.cancel_setup();
       if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE && ui.is_selecting()) ui.back();
     }
     if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST) { down[0]=down[1]=power_down=power_mouse=power_hover=boot_key=boot_mouse=boot_hover=false; SDL_CaptureMouse(SDL_FALSE); if(default_cursor) SDL_SetCursor(default_cursor); mouse_down=second_down=false; ui.cancel_touch(); midi.panic(); }
   }
   for(int b=0;b<2;++b) buttons.update(b,down[b],SDL_GetTicks(),diagnostic,&ui);
   poll_power();
   const auto now=uint64_t(SDL_GetTicks())*1000;
   ui.performance_tick(now);
   lv_timer_handler(); SDL_Delay(5);
 }
 if(!smoke && !controls_test && !selector_render_test && manual_dir.empty() && !boot.committed && !ui.prepare_shutdown()) std::fprintf(stderr,"Arpeggiator settings could not be saved on simulator exit.\n");
 midi.panic(); audio.close(); lv_deinit(); if(default_cursor) SDL_SetCursor(default_cursor); if(hand_cursor) SDL_FreeCursor(hand_cursor); companion::release();SDL_DestroyTexture(device_mask.texture); SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return result;
}

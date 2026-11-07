#include "menu.hpp"
#include "ui_fonts.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace sm::bounce {
namespace {
constexpr double spacing=56;
constexpr int divisions[]{1,2,4,8,16,32};
constexpr const char* titles[]{"SCALE","OCTAVES","BPM","TIMING","GATE","LIFETIME"};
constexpr const char* short_modes[]{"Major","Minor","Harm Min","Mel Min","Dorian","Phrygian","Lydian","Mixo","Locrian","Maj Pent","Min Pent","Blues"};
void text(lv_layer_t* layer,const char* value,int x,int cy,int width,const lv_font_t* font,lv_color_t color,uint8_t opacity=255){
 lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=font;d.text=value;d.text_local=1;d.color=color;d.opa=opacity;d.align=LV_TEXT_ALIGN_CENTER;
 lv_area_t a{x,cy-int(font->line_height)/2-3,x+width-1,cy+(int(font->line_height)+1)/2-4};lv_draw_label(layer,&d,&a);
}
bool inside(Vec p,int x,int y,int w,int h){return p.x>=x && p.x<x+w && p.y>=y && p.y<y+h;}
}
int Menu::count(Field f)const{switch(f){case Root:return 12;case Mode:return int(modes().size());case Low:case High:return 9;case Bpm:return 261;case Division:return 6;case Timing:return 3;case Gate:return 20;case Energy:return 17;}return 0;}
bool Menu::looping(Field f)const{return f==Root || f==Mode;}
int Menu::value(Field f)const{
 const auto& s=world_.settings();switch(f){case Root:return s.scale.root;case Mode:return s.scale.mode;case Low:return s.low;case High:return s.high;case Bpm:return s.bpm-40;case Division:for(int i=0;i<6;++i)if(divisions[i]==s.division)return i;return 0;case Timing:return int(s.timing);case Gate:return (100-s.gate)/5;case Energy:return (s.energy_seconds-10)/5;}return 0;
}
void Menu::apply(Field f,int option){
 const int n=count(f);option=looping(f)?(option%n+n)%n:std::clamp(option,0,n-1);
 if(option==value(f))return;
 auto s=world_.settings();switch(f){case Root:s.scale.root=option;break;case Mode:s.scale.mode=option;break;case Low:s.low=option;s.high=std::max(s.high,s.low);break;case High:s.high=option;s.low=std::min(s.low,s.high);break;case Bpm:s.bpm=option+40;break;case Division:s.division=divisions[option];break;case Timing:s.timing=ArpTiming(option);break;case Gate:s.gate=100-5*option;break;case Energy:s.energy_seconds=10+5*option;break;}
 world_.settings(s);
 if(f==Low || f==High)for(int i=0;i<wheel_count_;++i)if(wheels_[i].field!=f)wheels_[i].offset=wheels_[i].target=value(wheels_[i].field)*spacing;
}
void Menu::layout(){
 wheel_count_=2;
 switch(page){
 case 0:wheels_[0]={Root,110,74};wheels_[1]={Mode,190,166};break;
 case 1:wheels_[0]={Low,125,96};wheels_[1]={High,245,96};break;
 case 2:wheel_count_=1;wheels_[0]={Bpm,110,104};break;
 case 3:wheels_[0]={Division,75,100,287};wheels_[1]={Timing,183,208,287};break;
 case 4:wheel_count_=1;wheels_[0]={Gate,147,172};break;

 default:wheel_count_=1;wheels_[0]={Energy,147,172};break;
 }
 for(int i=0;i<wheel_count_;++i)wheels_[i].offset=wheels_[i].target=value(wheels_[i].field)*spacing;
}
void Menu::toggle(){cancel();opened=!opened;layout();last_tap_=0;tap_count_=0;}
void Menu::navigate(int delta){cancel();const int direction=delta<0?-1:1;for(int i=0;i<std::abs(delta);++i){do{page=(page+direction+6)%6;}while(world_.clock_enabled() && page==2);}layout();last_tap_=0;tap_count_=0;}
void Menu::cancel(){pressed_=false;axis_=0;drag_wheel_=-1;for(auto& w:wheels_)w.target=std::round(w.offset/spacing)*spacing;}
void Menu::down(Vec p){if(!opened)return;pressed_=true;start_=last_=p;axis_=0;drag_wheel_=-1;for(int i=0;i<wheel_count_;++i)if(inside(p,wheels_[i].x,wheels_[i].cy-84,wheels_[i].width,168)){drag_wheel_=i;drag_offset_=wheels_[i].offset;}}
void Menu::move(Vec p){
 if(!pressed_){return;}
 last_=p;const double dx=p.x-start_.x,dy=p.y-start_.y;
 if(axis_==0){if(std::abs(dx)>20 && std::abs(dx)>std::abs(dy)*1.3)axis_=1;else if(drag_wheel_>=0 && std::abs(dy)>8 && std::abs(dy)>std::abs(dx))axis_=2;}
 if(axis_==2){auto& w=wheels_[drag_wheel_];w.offset=drag_offset_-.65*dy;if(!looping(w.field))w.offset=std::clamp(w.offset,0.,(count(w.field)-1)*spacing);w.target=w.offset;apply(w.field,int(std::lround(w.offset/spacing)));}
}
void Menu::tap(uint64_t now){
 tap_flash_=.5;
 if(last_tap_ && now>last_tap_ && now-last_tap_>=200 && now-last_tap_<=1500){
   tap_intervals_[tap_count_%4]=now-last_tap_;++tap_count_;double sum=0;for(unsigned i=0;i<std::min(tap_count_,4u);++i)sum+=tap_intervals_[i];
   auto s=world_.settings();s.bpm=std::clamp(int(std::lround(60000*std::min(tap_count_,4u)/sum)),40,300);world_.settings(s);layout();
 }else tap_count_=0;
 last_tap_=now;
}
void Menu::up(Vec p,uint64_t now){
 if(!pressed_){return;}
 move(p);const int axis=axis_,wheel=drag_wheel_;const double dx=p.x-start_.x,dy=p.y-start_.y;cancel();
 if(axis==1 && std::abs(dx)>45){navigate(dx<0?1:-1);return;}
 if(axis==2)return;
 if(std::hypot(dx,dy)>10)return;
 if(page==3 && inside(p,166,151,134,44)){auto s=world_.settings();s.quantized=!s.quantized;world_.settings(s);return;}
 if(page==2 && std::hypot(p.x-300,p.y-233)<=56){tap(now);return;}
 if(wheel>=0 && std::abs(p.y-wheels_[wheel].cy)>26){auto& w=wheels_[wheel];apply(w.field,value(w.field)+(p.y>w.cy?1:-1));w.offset=w.target=value(w.field)*spacing;}
}
void Menu::scroll(Vec p,int direction){if(!opened)return;for(int i=0;i<wheel_count_;++i)if(inside(p,wheels_[i].x,wheels_[i].cy-84,wheels_[i].width,168)){auto& w=wheels_[i];apply(w.field,value(w.field)-direction);w.target=value(w.field)*spacing;return;}}
void Menu::tick(double dt){if(world_.clock_enabled() && page==2)sync();tap_flash_=std::max(0.,tap_flash_-dt);for(auto& w:wheels_)if(!(pressed_ && axis_==2 && &w==&wheels_[drag_wheel_])){w.offset+=(w.target-w.offset)*(1-std::exp(-dt*22));if(std::abs(w.target-w.offset)<.03)w.offset=w.target;}}
void Menu::frame(lv_layer_t* layer,int index,const Wheel& w,int left,int right){
 auto& f=frames_[index];constexpr int height=56;
 if(f.width!=w.width || f.left!=left || f.right!=right){
   f.width=w.width;f.left=left;f.right=right;f.pixels.resize(size_t(w.width)*height);
   for(int y=0;y<height;++y)for(int x=0;x<w.width;++x){
     const double dx=x+.5-w.width/2.,dy=y+.5-height/2.,r=dx<0?left:right,qx=std::abs(dx)-(w.width/2.-r),qy=std::abs(dy)-(height/2.-r);
     const double d=r-std::hypot(std::max(qx,0.),std::max(qy,0.))-std::min(std::max(qx,qy),0.);
     const auto a=uint32_t(255*std::clamp(d+.5,0.,1.)*std::clamp(2.-d,0.,1.));f.pixels[size_t(y)*w.width+x]=(a<<24)|accent_;
   }
   f.image={};f.image.header.magic=LV_IMAGE_HEADER_MAGIC;f.image.header.cf=LV_COLOR_FORMAT_ARGB8888;f.image.header.w=w.width;f.image.header.h=height;f.image.header.stride=w.width*4;f.image.data_size=uint32_t(f.pixels.size()*4);f.image.data=reinterpret_cast<const uint8_t*>(f.pixels.data());
 }
 lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);d.src=&f.image;lv_area_t a{w.x,w.cy-28,w.x+w.width-1,w.cy+27};d.image_area=a;lv_draw_image(layer,&d,&a);
}
void Menu::draw(lv_layer_t* layer){
 lv_draw_rect_dsc_t clear;lv_draw_rect_dsc_init(&clear);clear.bg_color=lv_color_hex(0);clear.bg_opa=255;const auto clip=layer->_clip_area;lv_draw_rect(layer,&clear,&clip);
 const auto cyan=lv_color_hex(accent_),white=lv_color_hex(0xffffff);
 text(layer,titles[page],0,page==3?79:104,466,&barlow_43,cyan);
 if(page==1)text(layer,"-",219,233,28,&barlow_32,white);
 if(page==3){
   text(layer,"QUANTIZE",0,130,466,&barlow_20,white);
   lv_draw_rect_dsc_t toggle;lv_draw_rect_dsc_init(&toggle);toggle.radius=LV_RADIUS_CIRCLE;toggle.bg_color=world_.settings().quantized?cyan:lv_color_hex(0x303030);
   lv_area_t a{185,152,280,193};lv_draw_rect(layer,&toggle,&a);toggle.bg_color=white;const int x=world_.settings().quantized?242:189;a={x,156,x+33,189};lv_draw_rect(layer,&toggle,&a);
 }
 if(page==2){
   lv_draw_rect_dsc_t button;lv_draw_rect_dsc_init(&button);button.radius=LV_RADIUS_CIRCLE;button.bg_color=lv_color_mix(cyan,lv_color_hex(0),uint8_t(tap_flash_*65));button.border_color=cyan;button.border_width=2;
   lv_area_t a{244,177,355,288};lv_draw_rect(layer,&button,&a);text(layer,"TAP",244,233,112,&barlow_28,white);
 }
 for(int i=0;i<wheel_count_;++i){
   const auto& w=wheels_[i];const auto saved=layer->_clip_area;lv_area_t clip{w.x,w.cy-84,w.x+w.width-1,w.cy+83};
   clip={std::max(clip.x1,saved.x1),std::max(clip.y1,saved.y1),std::min(clip.x2,saved.x2),std::min(clip.y2,saved.y2)};
   if(clip.x1>clip.x2 || clip.y1>clip.y2){continue;}
   layer->_clip_area=clip;
   const int center=int(std::lround(w.offset/spacing));
   for(int k=center-2;k<=center+2;++k){
     const int n=count(w.field),option=looping(w.field)?(k%n+n)%n:k;if(option<0 || option>=n)continue;
     const double cy=w.cy+k*spacing-w.offset,distance=std::abs(cy-w.cy)/84.;if(distance>=1)continue;
     char buffer[48]{};const char* label=buffer;
     switch(w.field){case Root:label=roots()[option];break;case Mode:label=short_modes[option];break;case Low:case High:std::snprintf(buffer,sizeof(buffer),"%d",option);break;case Bpm:std::snprintf(buffer,sizeof(buffer),"%d",option+40);break;case Division:std::snprintf(buffer,sizeof(buffer),"1/%d",divisions[option]);break;case Timing:label=option==0?"STRAIGHT":option==1?"DOTTED":"TRIPLET";break;case Gate:std::snprintf(buffer,sizeof(buffer),"%d %%",100-option*5);break;case Energy:std::snprintf(buffer,sizeof(buffer),"%d s",10+option*5);break;}
     const bool small=w.field==Mode || w.field==Timing;
     const auto* font=distance>.6?&barlow_20:distance>.3?&barlow_24:small?&barlow_28:&barlow_32;
     double fade=1-distance*distance;fade*=fade;text(layer,label,w.x,int(std::lround(cy)),w.width,font,white,uint8_t(255*fade));
   }
   // Per-glyph feathering at the top/bottom of each viewport.
   for(int end=0;end<2;++end){lv_draw_rect_dsc_t fade;lv_draw_rect_dsc_init(&fade);fade.bg_color=lv_color_hex(0);fade.bg_opa=255;fade.bg_grad.dir=LV_GRAD_DIR_VER;fade.bg_grad.stops_count=2;fade.bg_grad.stops[0].color=fade.bg_grad.stops[1].color=lv_color_hex(0);fade.bg_grad.stops[0].frac=0;fade.bg_grad.stops[1].frac=255;fade.bg_grad.stops[0].opa=end?0:255;fade.bg_grad.stops[1].opa=end?255:0;lv_area_t a{w.x,end?w.cy+50:w.cy-84,w.x+w.width-1,end?w.cy+83:w.cy-51};lv_draw_rect(layer,&fade,&a);}
   layer->_clip_area=saved;
   const bool pair=page==0 || page==3;frame(layer,i,w,pair && i==1?6:28,pair && i==0?6:28);
 }
}
void Menu::reset_damage(){painted_page_=page;for(int i=0;i<wheel_count_;++i)painted_offsets_[i]=wheels_[i].offset;painted_flash_=tap_flash_;painted_quantized_=world_.settings().quantized;}
void Menu::invalidate_regions(lv_obj_t* object){
 if(painted_page_!=page){lv_obj_invalidate(object);reset_damage();return;}
 for(int i=0;i<wheel_count_;++i)if(wheels_[i].offset!=painted_offsets_[i]){const auto& w=wheels_[i];lv_area_t area{w.x,w.cy-84,w.x+w.width-1,w.cy+83};lv_obj_invalidate_area(object,&area);}
 if(page==2 && painted_flash_!=tap_flash_){lv_area_t area{242,175,357,290};lv_obj_invalidate_area(object,&area);}
 if(page==3 && painted_quantized_!=world_.settings().quantized){lv_area_t area{183,150,282,195};lv_obj_invalidate_area(object,&area);}
 reset_damage();
}
}

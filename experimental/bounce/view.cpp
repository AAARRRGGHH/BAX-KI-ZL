#include "view.hpp"
#include <algorithm>
#include <cmath>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
namespace sm::bounce {
namespace {
constexpr float pi=3.14159265358979323846f,ring_radius=224.5f;
constexpr int tile_size=32;
bool intersects(lv_area_t a,lv_area_t b){return a.x1<=b.x2 && a.x2>=b.x1 && a.y1<=b.y2 && a.y2>=b.y1;}
lv_area_t merge(lv_area_t a,lv_area_t b){return {std::min(a.x1,b.x1),std::min(a.y1,b.y1),std::max(a.x2,b.x2),std::max(a.y2,b.y2)};}
// Every rectangle is clipped to 466x466, so area costs safely fit in 32 bits.
int32_t pixels(lv_area_t a){return (a.x2-a.x1+1)*(a.y2-a.y1+1);}
void compact(lv_area_t* areas,size_t& size,size_t limit){
 while(size>1){size_t first=0,second=1;int32_t cost=INT32_MAX;
   for(size_t i=0;i<size;++i)for(size_t j=i+1;j<size;++j){const auto extra=pixels(merge(areas[i],areas[j]))-pixels(areas[i])-pixels(areas[j]);if(extra<cost){cost=extra;first=i;second=j;}}
   if(size<=limit && cost>192)break;
   areas[first]=merge(areas[first],areas[second]);areas[second]=areas[--size];
 }
}
}
uint32_t View::ring_color(double flash,uint32_t accent){return flash>0?(accent&0xffffffu):0xffffffu;}
void View::geometry(){
 for(auto& plan:ring_plans_){plan.ready=false;RenderVector<lv_area_t>().swap(plan.areas);}
 tiles_.clear();const float step=2*pi/current_.count,half_arc=step/2-1.6f*pi/180;
 // Only sparse annulus tiles are stored, never a full-screen alpha image.
 for(int top=0;top<466;top+=tile_size)for(int left=0;left<466;left+=tile_size){
   const int width=std::min(tile_size,466-left),height=std::min(tile_size,466-top);
   const float near_x=std::max({left+.5f-233,233-(left+width-.5f),0.f}),near_y=std::max({top+.5f-233,233-(top+height-.5f),0.f});
   const float far_x=std::max(std::abs(left+.5f-233),std::abs(left+width-.5f-233)),far_y=std::max(std::abs(top+.5f-233),std::abs(top+height-.5f-233));
   if(near_x*near_x+near_y*near_y>(ring_radius+2)*(ring_radius+2) || far_x*far_x+far_y*far_y<(ring_radius-2)*(ring_radius-2))continue;
   std::array<uint8_t,tile_size*tile_size> coverage{},owners{};int x1=width,y1=height,x2=-1,y2=-1;uint8_t segments=0;
   for(int y=0;y<height;++y)for(int x=0;x<width;++x){
     const float dx=left+x+.5f-233,dy=top+y+.5f-233,r=std::sqrt(dx*dx+dy*dy);
     if(std::abs(r-ring_radius)>=2)continue;
     float angle=std::atan2(dx,-dy);if(angle<0)angle+=2*pi;
     const int index=int(std::floor(angle/step+.5f))%current_.count;
     const float local=std::remainder(angle-index*step,2*pi);float distance=std::abs(r-ring_radius);
     if(std::abs(local)>half_arc){const float end=index*step+(local<0?-half_arc:half_arc),ex=dx-ring_radius*std::sin(end),ey=dy+ring_radius*std::cos(end);distance=std::sqrt(ex*ex+ey*ey);}
     const auto alpha=uint8_t(std::clamp(2.f-distance,0.f,1.f)*255+.5f);if(!alpha)continue;
     coverage[y*width+x]=alpha;owners[y*width+x]=uint8_t(index);segments|=uint8_t(1u<<index);
     x1=std::min(x1,x);x2=std::max(x2,x);y1=std::min(y1,y);y2=std::max(y2,y);
   }
   if(x2<0)continue;
   x1&=~1;x2|=1;const int w=x2-x1+1,h=y2-y1+1;
   Tile tile;tile.area={left+x1,top+y1,left+x2,top+y2};tile.segments=segments;
   tile.coverage.resize(size_t(w)*h);tile.owners.resize(size_t(w)*h);tile.pixels.resize(size_t(w)*h);
   for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto dst=size_t(y)*w+x,src=size_t(y+y1)*width+x+x1;tile.coverage[dst]=coverage[src];tile.owners[dst]=owners[src];}
   tiles_.push_back(std::move(tile));
 }
 geometry_count_=current_.count;
#ifdef ESP_PLATFORM
 size_t bytes=tiles_.capacity()*sizeof(Tile);for(const auto& tile:tiles_)bytes+=tile.coverage.capacity()+tile.owners.capacity()+2*tile.pixels.capacity();
 ESP_LOGI("bounce-render","Static 3px ring: %u tiles, %u bytes PSRAM; max %u balls",unsigned(tiles_.size()),unsigned(bytes),unsigned(max_balls));
#endif
}
void View::prepare(uint32_t accent){
 const bool rebuilt=geometry_count_!=current_.count;if(rebuilt)geometry();uint8_t changed=0;
 for(int i=0;i<current_.count;++i){const auto color=ring_color(current_.flashes[i],accent);if(!rebuilt && color==colors_[i])continue;colors_[i]=color;changed|=uint8_t(1u<<i);
   for(unsigned a=0;a<256;++a){const unsigned r=(((color>>16)&255)*a+127)/255,g=(((color>>8)&255)*a+127)/255,b=((color&255)*a+127)/255;palette_[i][a]=uint16_t((r>>3)<<11 | (g>>2)<<5 | (b>>3));}
 }
 for(auto& tile:tiles_)if(tile.segments&changed)for(size_t i=0;i<tile.pixels.size();++i)tile.pixels[i]=palette_[tile.owners[i]][tile.coverage[i]];
 composite(accent,rebuilt,changed);
 accent_=accent;dirty_=false;
}
void View::composite(uint32_t accent,bool rebuilt,uint8_t changed){
 const bool fresh=rebuilt || !scene_ready_;
 if(scene_.empty()){
   scene_.resize(466*466);scene_image_.header.magic=LV_IMAGE_HEADER_MAGIC;scene_image_.header.cf=LV_COLOR_FORMAT_RGB565;
   scene_image_.header.w=466;scene_image_.header.h=466;scene_image_.header.stride=466*2;scene_image_.data_size=uint32_t(scene_.size()*2);scene_image_.data=reinterpret_cast<const uint8_t*>(scene_.data());
#ifdef ESP_PLATFORM
   ESP_LOGI("bounce-render","Composite cache: %u bytes PSRAM; one LVGL image task per clip",unsigned(scene_.size()*2));
#endif
 }
 if(fresh)std::fill(scene_.begin(),scene_.end(),0);
 else for(size_t i=0;i<max_balls;++i)if(painted_balls_[i]){const auto a=ball_bounds_[i];for(int y=std::max<int32_t>(0,a.y1);y<=std::min<int32_t>(465,a.y2);++y)std::fill(scene_.begin()+y*466+std::max<int32_t>(0,a.x1),scene_.begin()+y*466+std::min<int32_t>(465,a.x2)+1,0);}
 for(const auto& tile:tiles_){bool restore=fresh || (tile.segments&changed);if(!restore)for(size_t i=0;i<max_balls;++i)if(painted_balls_[i] && intersects(tile.area,ball_bounds_[i])){restore=true;break;}
   if(!restore)continue;
   const int width=tile.area.x2-tile.area.x1+1;for(int y=tile.area.y1;y<=tile.area.y2;++y)std::copy_n(tile.pixels.data()+(y-tile.area.y1)*width,width,scene_.data()+y*466+tile.area.x1);
 }
 auto blend=[](uint16_t src,uint16_t dst,unsigned a){if(a==255)return src;if(!a)return dst;const unsigned inv=255-a,r=(((src>>11)&31)*a+((dst>>11)&31)*inv+127)/255,g=(((src>>5)&63)*a+((dst>>5)&63)*inv+127)/255,b=((src&31)*a+(dst&31)*inv+127)/255;return uint16_t((r<<11)|(g<<5)|b);};
 for(size_t i=0;i<max_balls;++i){const auto& b=current_.balls[i];painted_balls_[i]=b.alive;if(!b.alive)continue;
   const int r=std::clamp(int(std::lround(b.radius())),1,12),width=2*r+1,x=int(std::lround(233+b.p.x)),y=int(std::lround(233+b.p.y));ball_bounds_[i]={x-r,y-r,x+r,y+r};
   auto& mask=circles_[r];if(mask.empty()){mask.resize(size_t(width)*width);for(int cy=0;cy<width;++cy)for(int cx=0;cx<width;++cx){const float dx=float(cx-r),dy=float(cy-r);mask[cy*width+cx]=uint8_t(std::clamp(r+.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f)*255+.5f);}}
   const auto color=lv_color_to_u16(lv_color_hex(b.whiteness==255?0xffffff:accent)),outline=lv_color_to_u16(lv_color_hex(accent));const unsigned opacity=b.opacity();
   for(int cy=0;cy<width;++cy){const int py=y-r+cy;if(py<0 || py>=466)continue;for(int cx=0;cx<width;++cx){const int px=x-r+cx;if(px<0 || px>=466)continue;const unsigned alpha=(unsigned(mask[cy*width+cx])*opacity+127)/255;if(!alpha)continue;
     auto& dst=scene_[py*466+px];const bool edge=b.held && (cx==0 || cy==0 || cx==width-1 || cy==width-1 || mask[cy*width+cx]<255);dst=blend(edge?outline:color,dst,alpha);
   }}
 }
 scene_ready_=true;
}
void View::release(){dirty_=true;have_previous_=false;scene_ready_=false;geometry_count_=0;tiles_.clear();for(auto& plan:ring_plans_){plan.ready=false;RenderVector<lv_area_t>().swap(plan.areas);}lv_image_cache_drop(&scene_image_);RenderVector<uint16_t>().swap(scene_);for(auto& mask:circles_)RenderVector<uint8_t>().swap(mask);painted_balls_={};}
void View::draw(lv_layer_t* layer,uint32_t accent){
 if(dirty_ || accent_!=accent)prepare(accent);
 lv_draw_image_dsc_t image;lv_draw_image_dsc_init(&image);image.src=&scene_image_;const lv_area_t full{0,0,465,465};image.image_area=full;lv_draw_image(layer,&image,&full);++work_.scene_blits;
}
const RenderVector<lv_area_t>& View::ring_plan(uint8_t changed){
 auto& plan=ring_plans_[changed];if(plan.ready)return plan.areas;
 std::array<lv_area_t,128> areas{};size_t size=0;
 for(const auto& tile:tiles_)if(tile.segments&changed)areas[size++]=tile.area;
 compact(areas.data(),size,16);plan.areas.assign(areas.begin(),areas.begin()+size);plan.ready=true;++work_.ring_plan_builds;return plan.areas;
}
void View::invalidate_regions(lv_obj_t* object){
 if(!have_previous_ || current_.count!=previous_.count){lv_obj_invalidate(object);previous_=current_;have_previous_=true;return;}
 // Two old/new rectangles per ball plus at most 16 cached ring rectangles.
 std::array<lv_area_t,2*max_balls+16> damage{};size_t size=0;
 auto add=[&](lv_area_t a){a.x1=std::max<int32_t>(0,a.x1);a.y1=std::max<int32_t>(0,a.y1);a.x2=std::min<int32_t>(465,a.x2);a.y2=std::min<int32_t>(465,a.y2);if(a.x1<=a.x2 && a.y1<=a.y2)damage[size++]=a;};
 auto ball_area=[](const Ball& b){const int x=int(std::lround(233+b.p.x)),y=int(std::lround(233+b.p.y)),r=int(std::round(b.radius()))+2;return lv_area_t{x-r,y-r,x+r,y+r};};
 for(size_t i=0;i<current_.balls.size();++i){const auto& a=previous_.balls[i];const auto& b=current_.balls[i];if(!a.alive && !b.alive)continue;
   if(a.alive && b.alive && std::lround(a.p.x)==std::lround(b.p.x) && std::lround(a.p.y)==std::lround(b.p.y) && std::lround(a.radius())==std::lround(b.radius()) && a.held==b.held && a.whiteness==b.whiteness && a.opacity()==b.opacity())continue;
   if(a.alive)add(ball_area(a));
   if(b.alive)add(ball_area(b));
 }
 uint8_t changed=0;for(int i=0;i<current_.count;++i)if(ring_color(current_.flashes[i],accent_)!=ring_color(previous_.flashes[i],accent_))changed|=uint8_t(1u<<i);
 if(changed)for(const auto& area:ring_plan(changed))add(area);
 // Merge cheaply adjacent areas and cap the list: balance transfer count and
 // pixel volume instead of minimizing pixel volume alone. No full-screen fallback.
 compact(damage.data(),size,20);
 for(size_t i=0;i<size;++i)lv_obj_invalidate_area(object,&damage[i]);
 previous_=current_;
}
}

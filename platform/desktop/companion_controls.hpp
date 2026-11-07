#pragma once
#include <SDL.h>
#include <algorithm>
#include <vector>
#include "reset_text_asset.hpp"

namespace companion {
enum class Action { None, Reset };
inline SDL_FRect bounds(int width,int height){
 const float height_px=std::clamp(float(std::min(width,height))*.038f,24.f,48.f);
 const float width_px=height_px*text_art::width/text_art::height;
 return {std::max(12.f,.085f*width-width_px/2),std::max(12.f,.085f*height-height_px/2),width_px,height_px};
}
inline Action hit(float x,float y,int width,int height,bool enabled=true){
 if(!enabled)return Action::None;
 const auto b=bounds(width,height);constexpr float padding=10;
 return x>=b.x-padding && x<=b.x+b.w+padding && y>=b.y-padding && y<=b.y+b.h+padding?Action::Reset:Action::None;
}
inline SDL_Texture* texture=nullptr;
inline void release(){SDL_DestroyTexture(texture);texture=nullptr;}
inline void draw(SDL_Renderer* renderer,int width,int height,bool reset,Action hover,uint32_t accent){
 if(!reset)return;
 if(!texture){
   std::vector<uint8_t> rgba(size_t(text_art::width)*text_art::height*4,255);size_t offset=0;
   for(auto run:text_art::mask)for(int i=0;i<(run&255);++i)rgba[offset++*4+3]=uint8_t(run>>8);
   auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(rgba.data(),text_art::width,text_art::height,32,text_art::width*4,SDL_PIXELFORMAT_RGBA32);
   if(!surface)return;texture=SDL_CreateTextureFromSurface(renderer,surface);SDL_FreeSurface(surface);
   if(!texture)return;SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_ScaleModeLinear);
 }
 const uint32_t color=hover==Action::Reset?accent:0xffffff;
 SDL_SetTextureColorMod(texture,uint8_t(color>>16),uint8_t(color>>8),uint8_t(color));
 const auto b=bounds(width,height);SDL_RenderCopyF(renderer,texture,nullptr,&b);
}
}

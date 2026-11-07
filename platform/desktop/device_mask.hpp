#pragma once
#include <SDL.h>
struct DeviceMask {
 SDL_Texture* texture=nullptr;
 int width=466,height=466;
 SDL_FRect opening{0,0,466,466};
 bool load(SDL_Renderer* renderer,const char* path);
 bool power_hit(float x,float y) const;
 void draw_power(SDL_Renderer* renderer,bool hovered,bool pressed) const;
 bool boot_hit(float x,float y) const;
 void draw_boot(SDL_Renderer* renderer,bool hovered,bool pressed) const;
};

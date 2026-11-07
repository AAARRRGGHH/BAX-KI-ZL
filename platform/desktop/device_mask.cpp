#include "device_mask.hpp"
#include <filesystem>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif
namespace {
// Physical keys in the supplied photograph, in normalized mask coordinates.
bool key_hit(float x,float y,int width,int height,float ax,float ay,float bx,float by) {
 const float x1=ax*width,y1=ay*height,dx=(bx-ax)*width,dy=(by-ay)*height;
 const float t=std::clamp(((x-x1)*dx+(y-y1)*dy)/(dx*dx+dy*dy),0.f,1.f);
 return std::hypot(x-x1-t*dx,y-y1-t*dy)<=.022f*std::min(width,height);
}
void draw_key(SDL_Renderer* renderer,bool hovered,bool pressed,int width,int height,float ax,float ay,float bx,float by) {
 if(!hovered && !pressed) return;
 const float x1=ax*width,y1=ay*height,x2=bx*width,y2=by*height;
 const float angle=std::atan2(y2-y1,x2-x1),radius=.015f*std::min(width,height);
 constexpr float pi=3.14159265358979323846f;
 SDL_FPoint contour[27]; int count=0;
 for(int i=0;i<=12;++i) {
   const float a=angle-pi/2+i*pi/12;
   contour[count++]={x2+radius*std::cos(a),y2+radius*std::sin(a)};
 }
 for(int i=0;i<=12;++i) {
   const float a=angle+pi/2+i*pi/12;
   contour[count++]={x1+radius*std::cos(a),y1+radius*std::sin(a)};
 }
 contour[count++]=contour[0];
 SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
 SDL_SetRenderDrawColor(renderer,255,255,255,pressed?255:160);
 SDL_RenderDrawLinesF(renderer,contour,count);
 if(pressed) { SDL_SetRenderDrawColor(renderer,255,255,255,100); SDL_RenderDrawLineF(renderer,x1,y1,x2,y2); }
 SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
}
}
bool DeviceMask::power_hit(float x,float y) const { return key_hit(x,y,width,height,.782f,.170f,.845f,.241f); }
bool DeviceMask::boot_hit(float x,float y) const { return key_hit(x,y,width,height,.852f,.734f,.811f,.792f); }
void DeviceMask::draw_power(SDL_Renderer* renderer,bool hovered,bool pressed) const { draw_key(renderer,hovered,pressed,width,height,.782f,.170f,.845f,.241f); }
void DeviceMask::draw_boot(SDL_Renderer* renderer,bool hovered,bool pressed) const { draw_key(renderer,hovered,pressed,width,height,.852f,.734f,.811f,.792f); }
bool DeviceMask::load(SDL_Renderer* renderer,const char* path) {
#ifdef _WIN32
 const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 if(FAILED(initialized) && initialized!=RPC_E_CHANGED_MODE) return false;
 const bool loaded=[&]() {
   using Microsoft::WRL::ComPtr;
   ComPtr<IWICImagingFactory> factory;
   if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())))) return false;
   ComPtr<IWICBitmapDecoder> decoder;
   const auto file=std::filesystem::u8path(path).wstring();
   if(FAILED(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))) return false;
   ComPtr<IWICBitmapFrameDecode> frame;
   if(FAILED(decoder->GetFrame(0,&frame))) return false;
   ComPtr<IWICFormatConverter> converter;
   if(FAILED(factory->CreateFormatConverter(&converter))) return false;
   if(FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return false;
   UINT w=0,h=0;
   if(FAILED(converter->GetSize(&w,&h)) || w<2 || h<2 || w>4096 || h>4096) return false;
   std::vector<uint8_t> pixels(size_t(w)*h*4);
   if(FAILED(converter->CopyPixels(nullptr,w*4,UINT(pixels.size()),pixels.data()))) return false;
   auto clear=[&](int x,int y) { return pixels[(size_t(y)*w+x)*4+3]<128; };
   const int cx=int(w)/2,cy=int(h)/2;
   if(!clear(cx,cy)) return false;
   int left=cx,right=cx,top=cy,bottom=cy;
   while(left>0 && clear(left-1,cy)) --left;
   while(right+1<int(w) && clear(right+1,cy)) ++right;
   while(top>0 && clear(cx,top-1)) --top;
   while(bottom+1<int(h) && clear(cx,bottom+1)) ++bottom;
   // A central round, bounded opening is required; never crop a different image silently.
   if(left==0 || top==0 || right+1==int(w) || bottom+1==int(h) || std::abs((right-left)-(bottom-top))>2) return false;
   auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),int(w),int(h),32,int(w)*4,SDL_PIXELFORMAT_RGBA32);
   if(!surface) return false;
   auto* image=SDL_CreateTextureFromSurface(renderer,surface); SDL_FreeSurface(surface);
   if(!image) return false;
   SDL_SetTextureBlendMode(image,SDL_BLENDMODE_BLEND);
   texture=image; width=int(w); height=int(h);
   opening={float(left),float(top),float(right-left+1),float(bottom-top+1)};
   return true;
 }();
 if(SUCCEEDED(initialized)) CoUninitialize();
 return loaded;
#else
 (void)renderer; (void)path;
 return false;
#endif
}

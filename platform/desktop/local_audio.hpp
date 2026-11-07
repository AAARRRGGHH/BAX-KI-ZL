#pragma once
#include "synth.hpp"
#include "SDL.h"
class DesktopAudio:public sm::LocalAudio {
 sm::Synth synth_;
 SDL_AudioDeviceID device_=0;
 bool enabled_=false;
 static void callback(void* context,Uint8* bytes,int count) {
   auto& self=*static_cast<DesktopAudio*>(context);
   self.synth_.render(reinterpret_cast<int16_t*>(bytes),size_t(count)/sizeof(int16_t));
 }
public:
 ~DesktopAudio() override { close(); }
 void close() { if(device_) { SDL_CloseAudioDevice(device_); device_=0; } }
 bool configure(bool enabled,sm::SynthSettings settings) override {
   if(!sm::valid_synth(settings)) return false;
   if(enabled && !device_) {
     if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) return false;
     SDL_AudioSpec want{}; want.freq=48000; want.format=AUDIO_S16SYS; want.channels=1; want.samples=256;
     want.callback=callback; want.userdata=this; device_=SDL_OpenAudioDevice(nullptr,0,&want,nullptr,0);
     if(!device_) return false;
   }
   if(device_) SDL_LockAudioDevice(device_);
   synth_.configure(settings); enabled_=enabled;
   if(device_) { SDL_UnlockAudioDevice(device_); SDL_PauseAudioDevice(device_,enabled?0:1); }
   return true;
 }
 bool send(sm::MidiEvent event) override {
   if(!device_ || !enabled_) return event.kind!=sm::MidiKind::NoteOn;
   SDL_LockAudioDevice(device_); synth_.event(event); SDL_UnlockAudioDevice(device_); return true;
 }
};

#pragma once
#include "synth.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>
class SpeakerAudio:public sm::LocalAudio {
 sm::Synth synth_;
 SemaphoreHandle_t mutex_=nullptr;
 void* codec_=nullptr;
 bool started_=false;
 std::atomic<bool> enabled_{false},healthy_{true};
 static void worker(void* context);
public:
 // Also usable before I2C/codec initialization and from the shutdown handler.
 static void early_mute();
 static void mute_amplifier();
 bool configure(bool enabled,sm::SynthSettings settings) override;
 bool send(sm::MidiEvent event) override;
};

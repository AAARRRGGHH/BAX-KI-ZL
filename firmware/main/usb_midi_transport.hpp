#pragma once
#include "midi_endpoint.hpp"
#include "esp_err.h"
class PowerControl;
class UsbMidiTransport final:public sm::MidiEndpoint {
public:
 esp_err_t start(PowerControl& pmic);
 bool connected() override;
 bool send(sm::MidiEvent event) override;
 void set_enabled(bool enabled) override;
 bool reset_requested(uint32_t& generation) override;
 void acknowledge_reset(uint32_t generation) override;
 bool receive_realtime(sm::MidiRealtime& event) override;
};
bool usb_diagnostics_connected();
bool usb_bootloader_requested();
[[noreturn]] void usb_enter_bootloader();

#pragma once
#include "music.hpp"
#include "midi_endpoint.hpp"
#include "ble_midi_receive.hpp"
#include "esp_err.h"
// The host task owns BLE. UI calls only enqueue and service_ui().
class BleMidiTransport final:public sm::MidiEndpoint {
public:
 esp_err_t start();
 bool send(sm::MidiEvent event) override;
 bool connected() override;
 void set_enabled(bool enabled) override;
 bool reset_requested(uint32_t& generation);
 void acknowledge_reset(uint32_t generation);
 bool receive_realtime(sm::MidiRealtime& event);
};

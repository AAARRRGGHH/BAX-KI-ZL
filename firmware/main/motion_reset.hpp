#pragma once
#include "driver/i2c_master.h"
#include <atomic>
#include <cstdint>
class MotionReset {
 std::atomic<bool> enabled_{false};
 std::atomic<int> mode_{0};
 std::atomic<int> pending_mode_{0};
 std::atomic<uint32_t> heartbeat_{0},pending_{0};
 void run();
public:
 bool start(i2c_master_bus_handle_t bus);
 void enable(bool value,int mode);
 bool take_reset();
};

#pragma once
#include "driver/i2c_master.h"
#include <atomic>
#include <mutex>
#include <array>
#include <cstdint>
class MotionReset {
 std::atomic<bool> enabled_{false};
 std::atomic<int> mode_{0};
 std::atomic<int> pending_mode_{0};
 std::atomic<uint32_t> heartbeat_{0},pending_{0};
 std::mutex sample_mutex_;std::array<float,3> sample_{};uint32_t sampled_at_=0;
 void run();
public:
 bool start(i2c_master_bus_handle_t bus);
 void enable(bool value,int mode);
 bool take_reset();
 bool sample(std::array<float,3>& out,uint32_t& time){std::lock_guard<std::mutex> lock(sample_mutex_);out=sample_;time=sampled_at_;return time && uint32_t(esp_time()-time)<100;}
 static uint32_t esp_time();
};

#pragma once
#include "axp2101_policy.hpp"
#include "pmic_power_key.hpp"
#include "battery.hpp"
#include "driver/i2c_master.h"
class PowerControl:public sm::PmicRegisters {
 i2c_master_dev_handle_t device_=nullptr;
 bool verified_=false;
 bool key_ready_=false;
public:
 bool initialize(i2c_master_bus_handle_t bus);
 bool read(uint8_t reg,uint8_t& value) override;
 bool write(uint8_t reg,uint8_t value) override;
 bool diagnostic_ready() const { return verified_; }
 sm::BatteryState battery_status() { return verified_?sm::read_battery(*this):sm::BatteryState{}; }
 bool initialize_power_key();
 bool poll_power_key(uint8_t& status) { return key_ready_ && sm::poll_pmic_power_key(*this,status); }
 bool shutdown() { return verified_ && sm::request_pmic_shutdown(*this); }
};

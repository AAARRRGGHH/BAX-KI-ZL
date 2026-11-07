#pragma once
#include "axp2101_policy.hpp"
namespace sm {
struct BatteryState {
 bool valid=false,connected=false,gauge_enabled=false;
 int percent=-1;
 bool charging=false;
 bool usb_present=false,charge_done=false;
};
struct BatteryDisplay { int bars=-1; bool external_power=false,missing=false; };
inline BatteryDisplay battery_display(BatteryState state,bool fresh=true) {
 if(!fresh || !state.valid) return {};
 // Instrument view shows the power source, not the instantaneous charge current.
 if(state.usb_present) return {-1,true,false};
 if(!state.connected) return {-1,false,true};
 if(!state.gauge_enabled || state.percent<0 || state.percent>100) return {};
 return {(state.percent+19)/20,false,false};
}
// Read-only, verified against Waveshare's 1.75C example and bundled XPowersLib:
// STATUS1[3]=battery present, CHARGE_GAUGE_WDT_CTRL[3]=fuel gauge, A4=SOC %.
inline BatteryState read_battery(PmicRegisters& pmic) {
 uint8_t status=0,modules=0,percent=0,charge=0;
 if(!pmic.read(0x00,status)) return {};
 const bool usb=(status&0x20)!=0;
 if(!(status&0x08)) return {true,false,false,-1,false,usb,false};
 if(!pmic.read(0x01,charge)) return {};
 const bool charging=(charge>>5)==1;
 const bool done=(charge&7)==4;
 if(!pmic.read(0x18,modules)) return {};
 if(!(modules&0x08)) return {true,true,false,-1,charging,usb,done};
 if(!pmic.read(0xa4,percent)) return {};
 return {true,true,true,percent<=100?int(percent):-1,charging,usb,done};
}
}

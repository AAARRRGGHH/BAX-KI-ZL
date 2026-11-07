#pragma once
#include "battery.hpp"
#include <algorithm>
namespace sm {
inline constexpr uint32_t charge_handoff_magic=0x50424c43;
// Only an explicitly requested software restart may consume this RTC marker.
// Clearing it on every boot prevents restart loops and stale cold-boot requests.
inline bool consume_charge_handoff(uint32_t& marker,bool software_reset) {
 const bool requested=software_reset && marker==charge_handoff_magic;
 marker=0; return requested;
}
inline bool charge_after_shutdown(PmicRegisters& pmic) {
 uint8_t status=0;
 return pmic.read(0x00,status) && (status&0x20);
}
// Read-only PMIC boot classification. Never infer cable insertion from GPIO3.
inline bool usb_charge_boot(PmicRegisters& pmic,bool cold_start) {
 uint8_t source=0,status=0;
 return cold_start && pmic.read(0x20,source) && pmic.read(0x00,status) &&
   (source&0x3f)==4 && (status&0x20);
}
enum class ChargeAction { None,Instrument,Shutdown };
struct ChargeDisplay {
 int bars=-1; bool full=false,blink=false,missing=false;
};
// The host uses esp_timer, whereas LVGL starts its own tick counter later.
// Map timer callbacks into the host timeline instead of mixing their phases.
class ChargeAnimationClock {
 uint32_t host_=0,local_=0;
public:
 void sync(uint32_t host,uint32_t local) { host_=host; local_=local; }
 uint32_t now(uint32_t local) const { return host_+uint32_t(local-local_); }
};
inline ChargeDisplay charge_display(BatteryState s,uint32_t now,bool fresh=true) {
 if(!fresh || !s.valid) return {};
 if(!s.connected) return {-1,false,false,true};
 if(s.usb_present && s.charge_done && !s.charging) return {5,true,false,false};
 if(!s.gauge_enabled || s.percent<0 || s.percent>100) return {};
 // A small/zero instantaneous charge current does not mean charging is done.
 // Keep the charge-session indicator animated until the PMIC reports completion.
 const int bars=s.usb_present?std::max(1,(s.percent+19)/20):(s.percent+19)/20;
 return {bars,false,s.usb_present && (now/500)%2==1,false};
}
class ChargeSession {
 uint32_t full_at_=0; bool full_=false,requested_=false;
public:
 ChargeAction update(BatteryState s,uint32_t now,bool power_press=false) {
   if(power_press) return ChargeAction::Instrument;
   if(requested_) return ChargeAction::None;
   if(s.valid && !s.usb_present) { requested_=true; return ChargeAction::Shutdown; }
   if(!charge_display(s,now).full) { full_=false; return ChargeAction::None; }
   if(!full_) { full_=true; full_at_=now; }
   if(uint32_t(now-full_at_)<60000) return ChargeAction::None;
   requested_=true; return ChargeAction::Shutdown;
 }
};
}

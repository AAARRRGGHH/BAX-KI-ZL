#pragma once
#include "music.hpp"
#include "axp2101_policy.hpp"
namespace sm {
// AXP2101 SWcharge V1.0, sections 6.13.2.42 / 6.13.2.45:
// 0x41[1:0] enables negative/positive PWRON edges; 0x49[1:0] is W1C.
inline bool enable_pmic_power_key(PmicRegisters& pmic) {
 uint8_t old=0,actual=0;
 if(!pmic.read(0x41,old)) return false;
 bool ok=(old&3)==3 || pmic.write(0x41,old|3);
 if(ok) ok=pmic.read(0x41,actual) && actual==uint8_t(old|3);
 // Drop the power-on press and any events left by the previous run.
 if(ok) ok=pmic.write(0x49,3);
 if(!ok && pmic.read(0x41,actual) && (actual&3)!=(old&3))
   pmic.write(0x41,(actual&uint8_t(~3))|(old&3));
 return ok;
}
inline bool poll_pmic_power_key(PmicRegisters& pmic,uint8_t& status) {
 if(!pmic.read(0x49,status)) return false;
 const uint8_t edges=status&3;
 // Never acknowledge VBUS, battery, or the independent short/long IRQs.
 return !edges || pmic.write(0x49,edges);
}
class PmicPowerKey {
 Buttons buttons_{20,40};
 bool synchronized_=false,input_=false;
 uint32_t last_=0;
public:
 void update(bool ok,uint8_t status,uint32_t now,Buttons::Callback callback,void* context) {
   if(!ok || (synchronized_ && uint32_t(now-last_)>100)) {
     if(synchronized_) callback(1,ButtonEvent::Cancel,context);
     buttons_=Buttons(20,40); input_=false; synchronized_=false;
     return;
   }
   last_=now;
   if(!synchronized_) {
     // First successful poll after start/error drains stale events. A held
     // power-on key can release without producing an application click.
     synchronized_=true; return;
   }
   const auto edges=status&3;
   if(edges==3) {
     // Order of two coalesced edges is unknowable. Drop this cycle instead
     // of guessing whether it was a click or release/repress while held.
     callback(1,ButtonEvent::Cancel,context);
     buttons_=Buttons(20,40); input_=false; return;
   }
   if(edges==2) input_=true;  // PWRON falling edge: key down
   if(edges==1) input_=false; // PWRON rising edge: key up
   buttons_.update(1,input_,now,callback,context);
 }
};
}

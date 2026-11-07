#pragma once
#include <cstdint>
namespace sm {
// AXP2101 SWcharge V1.0, sections 6.13.2.7 and 6.13.2.20.
struct PmicRegisters {
 virtual bool read(uint8_t address,uint8_t& value)=0;
 virtual bool write(uint8_t address,uint8_t value)=0;
 virtual ~PmicRegisters()=default;
};
inline bool disable_pwr_shutdown(PmicRegisters& pmic) {
 uint8_t old22=0,old10=0;
 if(!pmic.read(0x22,old22) || !pmic.read(0x10,old10) || (old10&0x03)) return false;
 const uint8_t want22=old22&uint8_t(~0x02),want10=old10&uint8_t(~0x04);
 bool ok=true;
 if(want22!=old22) ok=pmic.write(0x22,want22);
 if(ok && want10!=old10) ok=pmic.write(0x10,want10);
 uint8_t actual22=0,actual10=0;
 if(ok) ok=pmic.read(0x22,actual22) && pmic.read(0x10,actual10) && actual22==want22 && actual10==want10;
 if(!ok) {
   // Restore only the two policy bits, never replay PMU action bits 1/0.
   uint8_t current=0;
   if(pmic.read(0x22,current) && (current&2)!=(old22&2)) pmic.write(0x22,(current&uint8_t(~2))|(old22&2));
   if(pmic.read(0x10,current) && !(current&3) && (current&4)!=(old10&4)) pmic.write(0x10,(current&uint8_t(~4))|(old10&4));
 }
 return ok;
}
inline bool request_pmic_shutdown(PmicRegisters& pmic) {
 uint8_t current=0;
 if(!pmic.read(0x10,current) || (current&0x03)) return false;
 return pmic.write(0x10,current|0x01);
}
}

#include "charge_mode.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace sm;
#define CHECK(v) do { if(!(v)) { std::fprintf(stderr,"Charge FAIL %d: %s\n",__LINE__,#v); std::exit(1); } } while(0)
struct Registers:PmicRegisters {
 std::array<uint8_t,256> values{}; int fail=-1,writes=0;
 bool read(uint8_t reg,uint8_t& value)override { if(reg==fail) return false; value=values[reg]; return true; }
 bool write(uint8_t,uint8_t)override { ++writes; return false; }
};
int main() {
 Registers pmic; pmic.values[0]=0x28;
 CHECK(charge_after_shutdown(pmic));
 pmic.values[0]=8; CHECK(!charge_after_shutdown(pmic));
 pmic.values[0]=0x28; pmic.fail=0; CHECK(!charge_after_shutdown(pmic)); pmic.fail=-1;
 uint32_t handoff=0; CHECK(!consume_charge_handoff(handoff,true));
 handoff=charge_handoff_magic; CHECK(consume_charge_handoff(handoff,true) && handoff==0);
 CHECK(!consume_charge_handoff(handoff,true)); // A later restart cannot re-enter charging accidentally.
 handoff=charge_handoff_magic; CHECK(!consume_charge_handoff(handoff,false) && handoff==0);
 handoff=0x12345678; CHECK(!consume_charge_handoff(handoff,true) && handoff==0);
 for(int source=0;source<64;++source) {
   pmic.values[0x20]=source;
   CHECK(usb_charge_boot(pmic,true)==(source==4));
   CHECK(!usb_charge_boot(pmic,false));
 }
 pmic.values[0x20]=4; pmic.values[0]=8; CHECK(!usb_charge_boot(pmic,true));
 pmic.values[0]=0x28; pmic.fail=0x20; CHECK(!usb_charge_boot(pmic,true));
 pmic.fail=0; CHECK(!usb_charge_boot(pmic,true)); pmic.fail=-1;
 pmic.values[0x18]=8; pmic.values[0xa4]=100;
 for(int status=0;status<256;++status) {
   pmic.values[1]=status; const auto s=read_battery(pmic);
   CHECK(s.usb_present && s.charge_done==((status&7)==4));
 }
 CHECK(pmic.writes==0);
 const BatteryState charging{true,true,true,62,true,true,false};
 const BatteryState full{true,true,true,100,false,true,true};
 { ChargeAnimationClock clock;
   // Host uptime and LVGL ticks need not even agree on the current half-second.
   clock.sync(7100,600);
   CHECK(clock.now(600)==7100 && clock.now(1100)==7600);
   CHECK(charge_display(charging,clock.now(600)).blink==charge_display(charging,7100).blink);
   CHECK(charge_display(charging,clock.now(1100)).blink==charge_display(charging,7600).blink);
   CHECK(charge_display(charging,1100).blink!=charge_display(charging,clock.now(1100)).blink);
   // Frequent host updates must preserve the same progression as the timer.
   for(uint32_t elapsed=0;elapsed<2000;elapsed+=100) {
     const uint32_t host=7100+elapsed,local=600+elapsed;
     clock.sync(host,local);
     for(uint32_t delta:{0u,20u,99u})
       CHECK(charge_display(charging,clock.now(local+delta)).blink==charge_display(charging,host+delta).blink);
   }
   clock.sync(UINT32_MAX-100,UINT32_MAX-50);
   CHECK(clock.now(49)==UINT32_MAX-100+100u);
   clock.sync(0,UINT32_MAX-10); CHECK(clock.now(9)==20);
 }
 { ChargeSession session;
   CHECK(session.update(charging,1)==ChargeAction::None);
   CHECK(session.update(full,1000)==ChargeAction::None);
   CHECK(session.update(full,60999)==ChargeAction::None);
   CHECK(session.update(full,61000)==ChargeAction::Shutdown);
   CHECK(session.update(full,62000)==ChargeAction::None);
   CHECK(session.update(full,63000,true)==ChargeAction::Instrument);
 }
 { ChargeSession session; session.update(full,1000); session.update(charging,50000);
   CHECK(session.update(full,61000)==ChargeAction::None);
   CHECK(session.update(full,120999)==ChargeAction::None);
   CHECK(session.update(full,121000)==ChargeAction::Shutdown);
 }
 { ChargeSession session; session.update(full,1000); session.update({},50000);
   CHECK(session.update(full,61000)==ChargeAction::None);
   auto unplugged=charging; unplugged.usb_present=false;
   CHECK(session.update(unplugged,62000)==ChargeAction::Shutdown);
 }
 { ChargeSession session; auto missing=full; missing.connected=false;
   session.update(missing,0); CHECK(session.update(missing,100000)==ChargeAction::None);
   auto paused=charging; paused.charging=false; paused.percent=100;
   session.update(paused,200000); CHECK(session.update(paused,300000)==ChargeAction::None);
 }
 { ChargeSession session; const uint32_t start=UINT32_MAX-30000;
   session.update(full,start); CHECK(session.update(full,start+60000)==ChargeAction::Shutdown);
 }
 for(int percent=0;percent<=100;++percent) {
   auto s=charging; s.percent=percent;
   const auto a=charge_display(s,0),b=charge_display(s,500);
   CHECK(a.bars>=1 && a.bars<=5 && a.bars==b.bars && !a.blink && b.blink);
 }
 CHECK(charge_display(full,500).bars==5 && !charge_display(full,500).blink);
 CHECK(charge_display(full,500).full && !charge_display(full,500,false).full);
 CHECK(charge_display({},0).bars==-1);
 auto standby=charging; standby.charging=false;
 CHECK(charge_display(standby,500).blink && !charge_display(standby,500).full);
 std::puts("Charge-only boot, telemetry, animation and shutdown timing tests passed");
}

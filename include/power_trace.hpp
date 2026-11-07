#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace sm {
enum class PowerTraceKind:uint8_t { Start, Raw, Button, Dispatch, GuardReady, Pmic, ScanGap, QueueLost, PmicKey, PmicKeyError, AudioGap };
struct PowerTraceEntry {
 uint32_t time=0;
 PowerTraceKind kind=PowerTraceKind::Start;
 uint8_t detail=0;
 uint16_t flags=0;
 uint32_t value=0,extra=0;
};
static_assert(sizeof(PowerTraceEntry)==16,"Bounded diagnostic RAM budget");
// Caller provides synchronization. Absolute indices distinguish overwritten data.
template<size_t Capacity> class PowerTrace {
 static_assert(Capacity>0,"Trace needs storage");
 std::array<PowerTraceEntry,Capacity> entries_{};
 uint64_t end_=0;
public:
 void push(PowerTraceEntry entry) { entries_[end_%Capacity]=entry; ++end_; }
 uint64_t end() const { return end_; }
 uint64_t begin() const { return end_>Capacity?end_-Capacity:0; }
 bool get(uint64_t index,PowerTraceEntry& entry) const {
   if(index<begin() || index>=end_) return false;
   entry=entries_[index%Capacity]; return true;
 }
};
}

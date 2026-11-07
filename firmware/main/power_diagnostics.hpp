#pragma once
#include "power_trace.hpp"
class PowerControl;
namespace power_diagnostics {
bool start(PowerControl& pmic);
void record(sm::PowerTraceKind kind,uint32_t time,uint8_t detail=0,uint16_t flags=0,uint32_t value=0,uint32_t extra=0);
}

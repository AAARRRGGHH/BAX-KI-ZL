#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint16_t x,y; uint8_t id; } sm_cst_point;
// Layout and ACK follow Waveshare's bundled SensorLib TouchDrvCST92xx.
static inline int sm_cst9217_decode(const uint8_t* data,size_t size,sm_cst_point points[2]) {
 if(!data || size<15 || data[6]!=0xab || (data[5]&0x7f)>2) return -1;
 int count=0;
 for(unsigned i=0;i<(unsigned)(data[5]&0x7f);++i) {
   const uint8_t* p=data+i*5+(i?2:0);
   if((p[0]&15)!=6) continue;
   const uint8_t id=p[0]>>4;
   if(count && points[0].id==id) return -1;
   points[count].x=(uint16_t)((p[1]<<4)|(p[3]>>4));
   points[count].y=(uint16_t)((p[2]<<4)|(p[3]&15));
   points[count++].id=id;
 }
 return count;
}
#ifdef __cplusplus
}
#endif

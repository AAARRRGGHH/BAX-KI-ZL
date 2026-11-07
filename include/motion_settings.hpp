#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace sm {
inline constexpr int motion_off=-1;
inline bool valid_motion_target(int value){return value==motion_off || (value>=1 && value<=119);}
inline int motion_option(int target){return target==motion_off?0:target;}
inline int motion_target(int option){return option==0?motion_off:option;}
struct MotionSettings {
 int channel=1; std::array<int,2> cc{{-1,-1}};
 std::array<float,3> neutral{{0,0,1}};
 bool operator==(const MotionSettings& b)const{return channel==b.channel && cc==b.cc && neutral==b.neutral;}
 bool operator!=(const MotionSettings& b)const{return !(*this==b);}
};
inline bool valid_motion(const MotionSettings& s){
 float norm=0;for(float v:s.neutral){if(!std::isfinite(v))return false;norm+=v*v;}
 return s.channel>=1 && s.channel<=16 && valid_motion_target(s.cc[0]) && valid_motion_target(s.cc[1]) && norm>.95f && norm<1.05f;
}
using MotionBytes=std::array<uint8_t,10>;
inline MotionBytes pack_motion(const MotionSettings& s){
 MotionBytes b{{3,uint8_t(s.channel),uint8_t(s.cc[0]+1),uint8_t(s.cc[1]+1)}};
 for(int i=0;i<3;++i){const auto v=uint16_t(int16_t(std::lround(s.neutral[i]*16384)));b[4+i*2]=uint8_t(v);b[5+i*2]=uint8_t(v>>8);}return b;
}
inline bool unpack_motion(const MotionBytes& b,MotionSettings& out){
 if(b[0]<1 || b[0]>3)return false;
 MotionSettings s;s.channel=b[1];
 // Preserve channel, calibration and CC assignments; retire experimental Pitch.
 for(int i=0;i<2;++i){s.cc[i]=b[0]==2 && b[2+i]==255?motion_off:int(b[2+i])-1;if(b[0]==1 && s.cc[i]==0)s.cc[i]=motion_off;}
 for(int i=0;i<3;++i)s.neutral[i]=int16_t(uint16_t(b[4+i*2])|(uint16_t(b[5+i*2])<<8))/16384.f;
 if(!valid_motion(s))return false;
 out=s;return true;
}
}

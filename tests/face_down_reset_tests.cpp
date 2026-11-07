#include "face_down_reset.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using sm::FaceDownReset;
#define CHECK(v) do { if(!(v)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v); std::exit(1); } } while(0)
struct Trace {
 FaceDownReset detector; uint32_t time=0; int triggers=0; bool enabled=true;
 void sample(float x=0,float y=0,float z=1) { time+=10; triggers+=detector.update(x,y,z,time,enabled); }
 void hold(int ms,float x=0,float y=0,float z=1) { for(int t=0;t<ms;t+=10) sample(x,y,z); }
 void arm() { hold(600); CHECK(detector.armed()); }
};
int main() {
 { // Quick editing between resets must rearm even while the hand is moving.
   Trace t; t.arm(); t.hold(1100,0,0,-1); CHECK(t.triggers==1);
   t.enabled=false; // Touching a pad disables triggering, not pose tracking.
   for(int i=0;i<16;++i) t.sample(i%2?.08f:-.08f,0,1);
   CHECK(t.detector.armed());
   t.enabled=true; t.hold(1100,0,0,-1); CHECK(t.triggers==2);
 }
 { // A single upright spike cannot bypass the repeat latch.
   Trace t; t.arm(); t.hold(1100,0,0,-1); CHECK(t.triggers==1);
   t.sample(); t.hold(3000,0,0,-1); CHECK(t.triggers==1);
 }
 { Trace t; t.arm(); t.hold(1000,0,0,-1); CHECK(t.triggers==0);
   t.hold(30,0,0,-1); CHECK(t.triggers==1);
   t.hold(10000,0,0,-1); CHECK(t.triggers==1);
   t.hold(600); t.hold(1100,0,0,-1); CHECK(t.triggers==2); }
 { // Booting while face down must not erase settings.
   Trace t; t.hold(10000,0,0,-1); CHECK(t.triggers==0);
   t.arm(); t.hold(1100,0,0,-1); CHECK(t.triggers==1); }
 { // Tilting away cancels the complete countdown.
   Trace t; t.arm(); t.hold(700,0,0,-1); t.hold(200,1,0,0);
   t.hold(700,0,0,-1); CHECK(t.triggers==0);
   t.hold(400,0,0,-1); CHECK(t.triggers==1); }
 { // Modest hand tremor and a 20-degree tilt are accepted.
   Trace t; t.arm(); t.hold(100,.342f,0,-.94f);
   for(int i=0;i<210;++i) t.sample(.342f+(i%2?.002f:-.002f),0,-.94f);
   CHECK(t.triggers==1); }
 { // The wider tolerance accepts 39 degrees but rejects 41 degrees.
   Trace t; t.arm(); t.hold(1100,.62932f,0,-.77715f); CHECK(t.triggers==1);
   Trace outside; outside.arm(); outside.hold(4000,.65606f,0,-.75471f);
   CHECK(outside.triggers==0);
 }
 { // Face up, vertical and a 45-degree tilt never trigger.
   Trace t; t.arm(); t.hold(6000); t.hold(6000,1,0,0);
   t.hold(6000,.707f,0,-.707f); CHECK(t.triggers==0); }
 { // Shaking, free fall and sustained excess acceleration cannot trigger.
   Trace t; t.arm(); t.hold(4000,0,0,-2); t.hold(4000,0,0,0);
   for(int i=0;i<500;++i) t.sample(i%2?4.f:-4.f,0,-1);
   CHECK(t.triggers==0); }
 { // Disabled menus/touches cancel the timer and cannot bypass the latch.
   Trace t; t.arm(); t.hold(700,0,0,-1); t.enabled=false;
   t.hold(4000,0,0,-1); CHECK(t.triggers==0); t.enabled=true;
   t.hold(700,0,0,-1); CHECK(t.triggers==0);
   t.hold(400,0,0,-1); CHECK(t.triggers==1);
   t.enabled=false; t.hold(1000,0,0,-1); t.enabled=true;
   t.hold(5000,0,0,-1); CHECK(t.triggers==1); }
 { // Missing and invalid data restart the continuous one-second hold.
   Trace t; t.arm(); t.hold(700,0,0,-1); t.time+=100;
   t.hold(700,0,0,-1); CHECK(t.triggers==0);
   t.sample(0,0,std::numeric_limits<float>::quiet_NaN());
   t.hold(700,0,0,-1); CHECK(t.triggers==0);
   t.hold(1100,0,0,-1); CHECK(t.triggers==1); }
 { Trace t; t.time=UINT32_MAX-2000; t.arm();
   t.hold(1100,0,0,-1); CHECK(t.triggers==1); }
 std::puts("Face-down reset tests passed");
}

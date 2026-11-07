#include "menu.hpp"
#include <cstdio>
#include <cstdlib>
using namespace sm;using namespace sm::bounce;
void check(bool ok,int line){if(!ok){std::fprintf(stderr,"Bounce menu check failed at %d\n",line);std::exit(1);}}
#define REQUIRE(...) check((__VA_ARGS__),__LINE__)
struct Output:MidiTransport{unsigned ons=0;bool send(MidiEvent e)override{ons+=e.kind==MidiKind::NoteOn;return true;}};
int main(){
 Output out;World world(out);Menu menu(world);menu.toggle();REQUIRE(menu.opened && menu.page==0);
 menu.scroll({140,233},-1);REQUIRE(world.settings().scale.root==0);
 menu.down({240,233});menu.move({240,143});menu.move({320,130});menu.up({320,130},1000);
 REQUIRE(menu.page==0 && world.settings().scale.mode==2); // Vertical lock prevents sideways page changes.
 menu.down({280,390});menu.up({150,390},1200);REQUIRE(menu.page==1);
 menu.scroll({170,233},-1);REQUIRE(world.settings().low==4);
 for(int i=0;i<9;++i)menu.scroll({290,233},1);REQUIRE(world.settings().low==0 && world.settings().high==0);
 menu.navigate(1);REQUIRE(menu.page==2);menu.scroll({150,233},-1);REQUIRE(world.settings().bpm==121);
 menu.down({300,233});menu.up({300,233},2000);menu.down({300,233});menu.up({300,233},2500);REQUIRE(world.settings().bpm==120);
 menu.navigate(1);menu.down({233,173});menu.up({233,173},3000);REQUIRE(!world.settings().quantized);
 menu.down({233,173});menu.up({233,173},3200);REQUIRE(world.settings().quantized);
 menu.scroll({250,287},-1);REQUIRE(world.settings().timing==ArpTiming::Dotted);

 menu.navigate(1);REQUIRE(menu.page==4);menu.scroll({233,233},-1);REQUIRE(world.settings().gate==45);menu.scroll({233,233},-100);REQUIRE(world.settings().gate==5);menu.scroll({233,233},100);REQUIRE(world.settings().gate==100);
 menu.navigate(1);menu.scroll({233,233},-1);REQUIRE(world.settings().energy_seconds==20);
 menu.navigate(1);REQUIRE(menu.page==0);menu.navigate(-1);REQUIRE(menu.page==5);menu.navigate(-1);REQUIRE(menu.page==4);
 menu.navigate(4);REQUIRE(menu.page==2);world.clock_sync(true);menu.sync();REQUIRE(menu.page==3);menu.navigate(-1);REQUIRE(menu.page==1);menu.navigate(1);REQUIRE(menu.page==3);world.clock_sync(false);menu.navigate(-1);REQUIRE(menu.page==2);
 const int ball=world.grab({0,0});world.release(ball,{0,-500});
 for(int i=0;i<480;++i){world.step(1./240);menu.tick(1./240);}REQUIRE(out.ons>0 && world.balls[ball].alive);
 menu.toggle();REQUIRE(!menu.opened && world.balls[ball].alive);world.clear();
 std::puts("Bounce menu: scroll/swipe locking, timing toggle, tap tempo and continuing playback PASS");
}

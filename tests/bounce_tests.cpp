#include "world.hpp"
#include "touch_gesture.hpp"
#include "flick_tracker.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace sm;using namespace sm::bounce;
void verify(bool v,int line){if(!v){std::fprintf(stderr,"Bounce check failed at line %d\n",line);std::exit(1);}}
#define check(...) verify((__VA_ARGS__),__LINE__)
struct Output:MidiTransport {std::vector<MidiEvent> events;bool send(MidiEvent e)override{events.push_back(e);return true;}};
int main(){
 // Slow/medium flicks have a softer response; strong flicks still reach the
 // original limit. Zero impulse and direction are preserved.
 check(std::hypot(World::fling_velocity({}).x,World::fling_velocity({}).y)==0);
 double last_speed=-1;for(int speed=0;speed<=3000;speed+=25){const auto v=World::fling_velocity({double(speed),0});check(v.x+1e-8>=last_speed && v.x<=World::max_speed+1e-8 && v.y==0);last_speed=v.x;}
 check(World::fling_velocity({100,0}).x>44 && World::fling_velocity({100,0}).x<46);
 check(World::fling_velocity({300,0}).x>142 && World::fling_velocity({300,0}).x<145);
 check(World::fling_velocity({600,0}).x>502 && World::fling_velocity({600,0}).x<505);
 check(World::fling_velocity({900,0}).x==World::max_speed);
 const auto diagonal=World::fling_velocity({1800,1800});check(std::abs(std::hypot(diagonal.x,diagonal.y)-World::max_speed)<.0001 && diagonal.x==diagonal.y);
 {FlickTracker motion;motion.down({0,0},1000);for(int i=1;i<=10;++i)motion.move({double(i),0},1000+i*20);check(std::abs(motion.velocity(1220).x-50)<1e-8);
   motion.move({200,200},1220);check(std::abs(motion.velocity(1220).x-50)<1e-8); // Release-time outlier ignored.
   check(motion.velocity(1400).x==0);}
 {FlickTracker motion;motion.down({0,0},0);motion.move({2,0},1);check(motion.velocity(2).x<65); // Tiny sample interval cannot amplify jitter into a full-speed throw.
   motion.down({0,0},0);motion.move({0,-120},300);check(std::abs(motion.velocity(321).y+400)<1e-8);check(motion.velocity(501).y==0);}
 // Pickup position and size use exactly the same eased 120-ms progress.
 {Output out;World world(out);const int id=world.grab({0,0});world.release(id,{});world.balls[id].energy=.04;
   const double radius=world.balls[id].radius();check(world.grab({40,0})==id);world.move(id,{40,0});check(world.balls[id].p.x==0 && world.balls[id].radius()==radius);
   for(int i=0;i<14;++i)world.step(1./240);const auto& ball=world.balls[id];check(ball.p.x>0 && ball.p.x<40 && ball.radius()>radius && ball.radius()<Ball::full_radius);
   check(std::abs(ball.p.x/40-(ball.radius()-radius)/(Ball::full_radius-radius))<1e-8);
   for(int i=0;i<15;++i)world.step(1./240);check(std::abs(ball.p.x-40)<1e-8 && ball.radius()==Ball::full_radius);world.move(id,{60,0});check(ball.p.x==60);
 }
 // Releasing during pickup starts flight at the displayed position, no snap.
 {Output out;World world(out);const int id=world.grab({0,0});world.release(id,{});world.balls[id].energy=.04;check(world.grab({40,0})==id);
   for(int i=0;i<10;++i)world.step(1./240);const Vec before=world.balls[id].p;world.release(id,{100,0});check(world.balls[id].p.x==before.x);world.step(1./240);check(world.balls[id].p.x>before.x && world.balls[id].p.x<before.x+1);
 }
 using Touch=TouchGesture;std::vector<Touch::Action> gestures;Touch gesture([&](Touch::Action a){gestures.push_back(a);});
 gesture.down(10,{100,100},0);gesture.down(11,{150,150},50);gesture.up(10,{101,100},100);gesture.up(11,{150,151},130);
 check(gestures.size()==1 && gestures[0].kind==Touch::Kind::Menu); // No ball-generating events.
 gestures.clear();gesture.down(20,{100,100},200);gesture.move(20,{125,100},220);gesture.up(20,{140,100},250);
 check(gestures.size()==3 && gestures[0].kind==Touch::Kind::Down && gestures.back().kind==Touch::Kind::Up);
 gestures.clear();gesture.down(30,{100,100},400);gesture.down(31,{150,150},450);gesture.move(31,{180,150},470);gesture.up(30,{100,100},490);gesture.up(31,{180,150},500);
 for(const auto& event:gestures)check(event.kind!=Touch::Kind::Menu); // Moving pair stays a play gesture.
 gestures.clear();gesture.down(40,{100,100},600);gesture.down(41,{150,150},650);gesture.tick(900);gesture.up(40,{100,100},910);gesture.up(41,{150,150},920);for(const auto& event:gestures)check(event.kind!=Touch::Kind::Menu);
 Output configured;World configurable(configured);auto settings=configurable.settings();settings.quantized=false;settings.scale={0,0};settings.low=settings.high=4;
 check(configurable.settings(settings) && configurable.segment_count()==7);int cb=configurable.grab({0,-214});configurable.release(cb,{0,-500});
 for(int i=0;i<3;++i)configurable.step(1./240);check(!configured.events.empty() && configured.events.front().note==60);
 const auto position=configurable.balls[cb].p;settings.scale.mode=9;check(configurable.settings(settings) && configurable.segment_count()==5);check(configurable.balls[cb].p.x==position.x && configurable.balls[cb].p.y==position.y);
 settings.bpm=60;settings.division=8;settings.timing=ArpTiming::Dotted;settings.quantized=true;check(configurable.settings(settings) && configurable.period_us()==750000);
 settings.timing=ArpTiming::Triplet;check(configurable.settings(settings) && configurable.period_us()==333333);
 settings.low=7;settings.high=2;check(!configurable.settings(settings));configurable.clear();
 // Contact feedback stays fully on for 100 ms, then turns off directly.
 Output feedback_out;World feedback(feedback_out);const int fb=feedback.grab({0,-213});feedback.release(fb,{0,-500});feedback.step(1./240);check(feedback.flashes()[0]==World::flash_seconds);
 for(int i=0;i<21;++i)feedback.step(1./240);check(feedback.flashes()[0]>0);
 for(int i=0;i<4;++i)feedback.step(1./240);check(feedback.flashes()[0]==0);feedback.clear();
 for(int octave=0;octave<4;++octave)for(int degree=0;degree<7;++degree){
   int hit[3];for(int i=0;i<3;++i){const int d=degree+i*2;hit[i]=World::pitches[d%7]+12*(octave+d/7);}
   if(hit[2]>93)continue;
    const auto result=nearest_triad(hit,3);const std::array<int,3> original{{hit[0],hit[1],hit[2]}};
    if((hit[2]-hit[0])%12==7)check(result==original);else check(result!=original); // Preserve major/minor voicings; soften diminished ones.
 }
 int dyad[]{47,50};check(nearest_triad(dyad,2)==(std::array<int,3>{{47,50,54}}));
 struct Trace:ripples::Observer {std::vector<ripples::MidiTrace> events;void midi(const ripples::MidiTrace& e)override{events.push_back(e);}} trace;
 Output quantized;World q(quantized,1,&trace);
 auto qs=q.settings();qs.low=2;qs.timing=ArpTiming::Dotted;qs.division=8;q.settings(qs); // Explicit dotted-eighth scheduling fixture.
 int seed=q.grab({0,-214});q.release(seed,{0,-500});
 for(int i=0;i<24;++i)q.step(1./240);check(quantized.events.empty()); // Hit has occurred, raster point not yet reached.
 for(int i=0;i<66;++i)q.step(1./240);
 check(trace.events.size()==1 && trace.events[0].time==375000 && trace.events[0].event.note==47);
 for(int i=0;i<2400;++i)q.step(1./240);
 for(const auto& e:trace.events)if(e.event.kind==MidiKind::NoteOn)check(e.time%q.period_us()==0);
 q.clear();seed=q.grab({0,-214});q.release(seed,{0,-500});q.step(1./240);q.step(1./240);
 q.clear();const auto stopped=quantized.events.size();for(int i=0;i<60;++i)q.step(1./240);check(quantized.events.size()==stopped);
 Output simultaneous;Trace chord_trace;World together(simultaneous,1,&chord_trace);
 auto ts=together.settings();ts.low=2;ts.timing=ArpTiming::Dotted;ts.division=8;together.settings(ts);
 for(int degree:{0,2}){const double angle=degree*6.28318530718/7;Vec n{std::sin(angle),-std::cos(angle)};int id=together.grab(n*214);together.release(id,n*500);}
 for(int i=0;i<90;++i)together.step(1./240);
 check(chord_trace.events.size()==4);
 for(size_t i=0;i<4;++i){check(chord_trace.events[i].time==375000);check(chord_trace.events[i].event.note==std::array<int,4>{{47,50,54,59}}[i]);}
 together.clear();for(int i=0;i<60;++i)together.step(1./240);check(chord_trace.events.size()==9); // Four Offs plus panic, no queued attacks.
 // Two hits on the same pitch class retain unison/octaves, rather than
 // inventing a third and fifth merely because two balls made contact.
 for(bool octave_pair:{false,true}){Output pair_out;Trace pair_trace;World same_class(pair_out,1,&pair_trace);
   auto ps=same_class.settings();ps.low=2;ps.timing=ArpTiming::Dotted;ps.division=8;same_class.settings(ps);
   for(int side:{-1,1}){const double angle=side*.2617993878;const Vec n{std::sin(angle),-std::cos(angle)};const int id=same_class.grab(n*213);check(id>=0);if(side==1 && octave_pair)same_class.balls[id].energy=.25;same_class.release(id,n*500);}
   for(int i=0;i<90;++i)same_class.step(1./240);
   check(pair_trace.events.size()==size_t(octave_pair?2:1));for(const auto& event:pair_trace.events)check(event.event.note%12==11 && event.time==375000);same_class.clear();
 }
 Output out;World w(out);auto legacy=w.settings();legacy.low=2;w.settings(legacy);check(w.grab({230,0})==-1);
 // Constant 44px pickup radius also covers a tiny ball; closest wins even
 // when another ball was allocated later. Held balls stay independent.
 int near=w.grab({-20,0});w.move(near,{-20,0});w.release(near,{});w.balls[near].energy=.04;
 int far=w.grab({70,0});w.move(far,{20,0});w.release(far,{});check(far!=near && w.grab({-3,0})==near);w.release(near,{});check(w.grab({-62,0})==near);w.move(near,{213,0});w.release(near,{});check(w.grab({232,0})==near);w.clear();out.events.clear();
 int a=w.grab({0,0});check(a>=0);w.release(a,{0,-500});
 for(int i=0;i<200;++i)w.step(1./240);
 check(!out.events.empty());check(out.events.front().note==47);check(w.balls[a].v.y>0);
 const auto initial=w.balls[a].radius();const auto color=w.balls[a].whiteness;for(int i=0;i<2400;++i)w.step(1./240);
 check(w.balls[a].radius()<initial);check(w.balls[a].whiteness==color);
 const double faded_radius=w.balls[a].radius();
 check(w.grab(w.balls[a].p)==a);check(w.balls[a].energy==1 && w.balls[a].age==0);
 check(std::abs(w.balls[a].radius()-faded_radius)<.000001);
 for(int i=0;i<20;++i)w.step(1./240);
 const double mid_growth=w.balls[a].radius();check(mid_growth>faded_radius && mid_growth<Ball::full_radius);
 w.release(a,{500,0});check(w.balls[a].radius()==mid_growth); // No size jump on an early flick.
 check(w.grab(w.balls[a].p)==a);for(int i=0;i<48;++i)w.step(1./240);
 w.move(a,{224,0});w.release(a,{500,0});
 check(w.balls[a].energy==1 && w.balls[a].age==0 && w.balls[a].radius()==Ball::full_radius && w.balls[a].octave()==2);
 check(w.balls[a].whiteness==color && std::hypot(w.balls[a].p.x,w.balls[a].p.y)<=World::boundary-Ball::full_radius);
 w.clear();int b=w.grab({-18,0});int c=w.grab({18,0});w.release(b,{100,0});w.release(c,{-100,0});
 for(int i=0;i<24;++i)w.step(1./240);
 check(w.balls[b].v.x<0 && w.balls[c].v.x>0);
 w.clear();for(size_t i=0;i<manual_balls;++i){int id=w.grab({0,0});check(id>=0);check(w.balls[id].whiteness==0 || w.balls[id].whiteness==255);}check(w.grab({0,0})==-1);w.clear();
 a=w.grab({0,0});w.release(a,{620,183});
 for(int i=0;i<30000;++i){w.step(1./240);for(const auto& ball:w.balls)if(ball.alive)check(std::hypot(ball.p.x,ball.p.y)<=World::boundary-ball.radius()+.001);}
 check(!w.balls[a].alive);
 int active[128]{};bool octaves[4]{};for(auto e:out.events){if(e.kind==MidiKind::NoteOn){check(e.velocity>=40 && e.velocity<=110);bool found=false;for(int o=0;o<4;++o)for(int p:World::pitches)if(p+12*o==e.note){found=true;octaves[o]=true;}check(found);++active[e.note];}else if(e.kind==MidiKind::NoteOff)--active[e.note];else for(auto& n:active)n=0;}
 for(bool reached:octaves)check(reached);
 Ball size_test;int previous=2;
 for(int i=0;i<=1000;++i){const double radius=Ball::full_radius-(Ball::full_radius-(Ball::base_radius+Ball::radius_span*std::sqrt(.035)))*i/1000.;size_test.energy=std::pow((radius-Ball::base_radius)/Ball::radius_span,2);check(size_test.octave()>=previous && size_test.octave()<=5);previous=size_test.octave();}check(previous==5);
 w.clear();for(auto n:active)check(n==0);
 for(int i=0;i<7;++i){const double angle=i*6.28318530718/7;check(World::segment({std::sin(angle),-std::cos(angle)})==i);}
 for(int count:{5,6,7})for(int i=0;i<count;++i){const double angle=i*6.28318530718/count;check(World::segment({std::sin(angle),-std::cos(angle)},count)==i);}
 // Fade-out is gradual, can be rescued, and releases the slot only at its end.
 Output fade_out;World fading(fade_out);int f=fading.grab({0,0});fading.release(f,{});fading.balls[f].energy=.034;
 fading.step(1./240);check(fading.balls[f].alive && fading.balls[f].retiring && fading.balls[f].opacity()>=254);
 for(int i=0;i<140;++i)fading.step(1./240);check(fading.balls[f].opacity()>100 && fading.balls[f].opacity()<150);
 const auto fading_radius=fading.balls[f].radius();check(fading.grab({0,0})==f);check(!fading.balls[f].retiring && fading.balls[f].opacity()==255 && fading.balls[f].radius()==fading_radius);
 fading.release(f,{});fading.balls[f].energy=.034;for(int i=0;i<290;++i)fading.step(1./240);check(!fading.balls[f].alive);
 // Collision hook is opt-in, reports one real impact, and uses the same
 // quantized MIDI scheduler with reliable Note Off if a rule returns notes.
 struct Handler:CollisionHandler{int calls=0;CollisionEvent last;CollisionNotes collide(const CollisionEvent& event)override{++calls;last=event;CollisionNotes result;result.notes[0]={47,80};result.count=1;return result;}} handler;
 Output collisions;World impact(collisions);auto is=impact.settings();is.timing=ArpTiming::Dotted;is.division=8;impact.settings(is);auto pair=[&](){int x=impact.grab({-18,0});int y=impact.grab({18,0});impact.release(x,{100,0});impact.release(y,{-100,0});};
 pair();for(int i=0;i<24;++i)impact.step(1./240);check(collisions.events.empty());impact.clear();collisions.events.clear();impact.collision_handler(&handler);pair();
 for(int i=0;i<24;++i)impact.step(1./240);check(handler.calls==1 && handler.last.speed>190 && handler.last.first!=handler.last.second && handler.last.scale==impact.settings().scale && collisions.events.empty());
 for(int i=0;i<180;++i)impact.step(1./240);check(collisions.events.size()>=2 && collisions.events[collisions.events.size()-2].kind==MidiKind::NoteOn && collisions.events.back().kind==MidiKind::NoteOff);impact.clear();
 // Legacy experiment flags no longer split balls; physical impacts still bounce.
 Output normal;World no_split(normal);int x=no_split.grab({-18,0}),y=no_split.grab({18,0});no_split.release(x,{100,0});no_split.release(y,{-100,0});
 for(int i=0;i<24;++i)no_split.step(1./240);size_t count=0;for(const auto& ball:no_split.balls)count+=ball.alive;check(count==2 && no_split.balls[x].v.x<0);
 std::puts("Bounce physics, scale, decay, limits and note lifecycle: PASS");
}

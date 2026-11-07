#pragma once
#include "scheduler.hpp"
#include "midi_clock.hpp"
#include <array>
#include <cmath>
namespace sm::bounce {
struct Vec { double x=0,y=0; Vec operator+(Vec b) const {return {x+b.x,y+b.y};} Vec operator-(Vec b) const {return {x-b.x,y-b.y};} Vec operator*(double s) const {return {x*s,y*s};} };
inline constexpr size_t manual_balls=7,max_balls=12;
struct Ball {
 static constexpr double full_radius=12,base_radius=4,radius_span=8,fade_seconds=1.2,pickup_seconds=.12;
 bool alive=false,held=false;Vec p{},v{};double energy=1,age=0;uint8_t whiteness=0;
 double grow_from=full_radius,grow_elapsed=pickup_seconds;bool retiring=false;double fade_elapsed=0;
 // Negative remaining time selects the unchanged normal lifetime model.
 double remaining_seconds=-1,decay_seconds=0,collision_grace=0,decay_factor=1;
 Vec touch_target{},pickup_offset{};double pickup_elapsed=pickup_seconds;
 double radius()const;uint8_t opacity()const;int octave(int low=2,int high=5)const;
};
struct CollisionEvent {
 uint64_t time_us=0;size_t first=0,second=0;Vec position{};double speed=0,first_energy=0,second_energy=0;
 Selection scale{};int first_octave=0,second_octave=0;
};
struct CollisionNotes {struct Note{int pitch=0,velocity=0;};std::array<Note,4> notes{};size_t count=0;};
// Optional future musical rule. Called on the physics worker: no blocking/UI.
// Returned notes use the existing quantizer, MIDI router and Note-Off scheduler.
struct CollisionHandler {virtual ~CollisionHandler()=default;virtual CollisionNotes collide(const CollisionEvent&)=0;};
// Nearest stable major/minor triad using only active-scale notes, with octave placements within the ball register.
std::array<int,3> nearest_triad(const int* hits,size_t count,Selection scale={11,1},int low=2,int high=5);
class ChordSearch {
 struct Candidate {std::array<uint8_t,3> notes{};uint16_t sum=0;};
 std::vector<Candidate> candidates_;Selection scale_{-1,-1};int low_=-1,high_=-1;
public:
 void configure(Selection scale,int low,int high);
 std::array<int,3> find(const int* hits,size_t count)const;
 std::array<int,4> find_four(const int* hits,size_t count)const;
};
class World {
 ripples::Scheduler notes_; double time_=0; std::array<double,7> flashes_{}; bool next_white_=true;
 Settings settings_{};uint64_t grid_origin_=0;
 MidiClock clock_;uint64_t wall_now_=0;bool external_active_=false;
 void update_clock_source();
 CollisionHandler* collision_handler_=nullptr;std::array<uint64_t,max_balls*max_balls> last_collisions_{};
 double cached_dt_=-1,energy_decay_=1,speed_decay_=1;
 uint64_t period_=250000;ChordSearch chords_;
 struct Pending { bool used=false; uint64_t due=0; int pitch=0,velocity=0; unsigned contacts=0;bool external=false; };
 std::array<Pending,48> pending_{};
 void queue_hit(uint64_t now,int pitch,int velocity);
 void play_due(uint64_t now);
 void follow_touch(Ball& ball);
 double remaining_lifetime(const Ball& ball)const;
 void assign_lifetime(Ball& ball,double energy,double remaining);
 int step_units()const;
public:
 static constexpr double boundary=225;
 static constexpr double pickup_radius=44,max_speed=650;
 static constexpr double flash_seconds=.100;
 static constexpr uint64_t quantize_us=250000; // Straight eighth (1/8), fixed 120 BPM.
 std::array<Ball,max_balls> balls{};
 explicit World(MidiTransport& out,uint32_t =0x61c8a3u,ripples::Observer* observer=nullptr):notes_(out,observer){chords_.configure(settings_.scale,settings_.low,settings_.high);}
 int grab(Vec p); void move(int id,Vec p); void release(int id,Vec velocity);
 static Vec fling_velocity(Vec finger_velocity);
 void fling(int id,Vec finger_velocity){release(id,fling_velocity(finger_velocity));}
 void collision_handler(CollisionHandler* handler){collision_handler_=handler;last_collisions_={};}
 void step(double dt); void clear();
 void silence(){pending_={};notes_.stop(uint64_t(std::llround(time_*1000000)));}
 void retry(){notes_.service(uint64_t(std::llround(time_*1000000)));}
 const Settings& settings() const {return settings_;}
 bool settings(Settings value);
 uint64_t period_us() const;
 uint64_t gate_us()const;
 void clock_sync(bool enabled);
 bool clock_enabled()const{return clock_.enabled();}
 void realtime(uint8_t status,uint64_t now);
 void clock_tick(uint64_t now);
 int segment_count() const {return int(modes()[settings_.scale.mode].intervals.size());}
 const std::array<double,7>& flashes() const {return flashes_;}
 static int segment(Vec p,int count=7);
 static constexpr std::array<int,7> pitches{{47,49,50,52,54,55,57}}; // B2 natural minor; transpose with ball size.
};
}

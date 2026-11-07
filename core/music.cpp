#include "music.hpp"
#include <stdexcept>
namespace sm {
const std::vector<Mode>& modes() {
 static const std::vector<Mode> m={
 {"Major",ModeType::Scale,{0,2,4,5,7,9,11},{0,1,2,3,4,5,6}},
 {"Natural Minor",ModeType::Scale,{0,2,3,5,7,8,10},{0,1,2,3,4,5,6}},
 {"Harmonic Minor",ModeType::Scale,{0,2,3,5,7,8,11},{0,1,2,3,4,5,6}},
 {"Melodic Minor",ModeType::Scale,{0,2,3,5,7,9,11},{0,1,2,3,4,5,6}},
 {"Dorian",ModeType::Scale,{0,2,3,5,7,9,10},{0,1,2,3,4,5,6}},
 {"Phrygian",ModeType::Scale,{0,1,3,5,7,8,10},{0,1,2,3,4,5,6}},
 {"Lydian",ModeType::Scale,{0,2,4,6,7,9,11},{0,1,2,3,4,5,6}},
 {"Mixolydian",ModeType::Scale,{0,2,4,5,7,9,10},{0,1,2,3,4,5,6}},
 {"Locrian",ModeType::Scale,{0,1,3,5,6,8,10},{0,1,2,3,4,5,6}},
 {"Major Pentatonic",ModeType::Scale,{0,2,4,7,9},{0,1,2,4,5}},
 {"Minor Pentatonic",ModeType::Scale,{0,3,5,7,10},{0,2,3,4,6}},
 {"Blues",ModeType::Scale,{0,3,5,6,7,10},{0,2,3,4,4,6}}}; return m;
}
const std::array<const char*,12>& roots() { static const std::array<const char*,12> r={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}; return r; }
bool valid(Selection s) { return s.root>=0 && s.root<12 && s.mode>=0 && s.mode<int(modes().size()); }
std::vector<Note> notes(Selection s,int octave) {
 if(!valid(s)) throw std::invalid_argument("Invalid selection");
 const auto& m=modes()[s.mode];
 // Keep the explicit sharp root chosen by the user. Spell each scale degree
 // by its diatonic letter, including double accidentals where necessary.
 static const int root_letter[]={0,0,1,1,2,3,3,4,4,5,5,6};
 static const int natural[]={0,2,4,5,7,9,11};
 const char* letters="CDEFGAB";
 std::vector<Note> out;
 for(size_t i=0;i<m.intervals.size();++i) {
   int pc=(s.root+m.intervals[i])%12;
   int letter=(root_letter[s.root]+m.degrees[i])%7;
   int delta=(pc-natural[letter]+18)%12-6;
   std::string name(1,letters[letter]);
   name.append(size_t(delta<0?-delta:delta),delta<0?'b':'#');
   int midi=12*(octave+1)+s.root+m.intervals[i];
   if(midi<0 || midi>127) throw std::out_of_range("MIDI octave");
   out.push_back({pc,midi,name});
 } return out;
}
void MidiRouter::release(int source) {
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(source<0 || source>=16 || held_[source]<0) return;
 int n=held_[source];
 for(int i=0;i<16;++i) if(i!=source && held_[i]==n) { held_[source]=-1; return; }
 // A failed retrigger On means its Off already succeeded. With no remaining
 // owners, cancel it instead of resurrecting a note after the last release.
 if(retrigger_[n]) { retrigger_[n]=false; held_[source]=-1; return; }
 if(transport_.send({MidiKind::NoteOff,channel_,uint8_t(n),0})) held_[source]=-1;
}
void MidiRouter::press(int source,int note,bool fresh_attack) {
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(source<0 || source>=16 || note<0 || note>127) return;
 release(source);
 if(held_[source]>=0) return; // Retry a failed Note Off before starting a new note.
 for(int n:held_) if(n==note) {
   if(fresh_attack && !retrigger_[note]) {
     // Balance the existing On before articulating the shared pitch again.
     // Ownership is retained for both fingers; only the final owner sends Off.
     if(!transport_.send({MidiKind::NoteOff,channel_,uint8_t(note),0})) return;
     retrigger_[note]=true;
   }
   held_[source]=note;
   if(retrigger_[note] && transport_.send({MidiKind::NoteOn,channel_,uint8_t(note),100,true})) retrigger_[note]=false;
   return;
 }
 if(transport_.send({MidiKind::NoteOn,channel_,uint8_t(note),100,fresh_attack})) held_[source]=note;
}
void MidiRouter::retry() {
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 for(int note=0;note<128;++note) if(retrigger_[note]) {
   bool owned=false; for(int held:held_) if(held==note) owned=true;
   if(!owned || transport_.send({MidiKind::NoteOn,channel_,uint8_t(note),100,true})) retrigger_[note]=false;
 }
}
void MidiRouter::panic() { std::lock_guard<std::recursive_mutex> lock(mutex_); for(int i=0;i<16;++i) release(i); if(transport_.send({MidiKind::AllNotesOff,channel_,0,0})) { held_.fill(-1); retrigger_.fill(false); } }
bool MidiRouter::channel(int c) { std::lock_guard<std::recursive_mutex> lock(mutex_); if(c<1 || c>16) return false; panic(); for(int n:held_) if(n>=0) return false; channel_=uint8_t(c); return true; }
bool NoteEngine::set_octave(int value) {
 if(value<0 || value>8) return false;
 octave_=value; // Existing sources retain their original pitch in the router.
 return true;
}
int NoteEngine::pitch(int degree) const {
 const auto& intervals=modes()[selection_.mode].intervals;
 if(degree<0 || degree>=int(intervals.size())) return -1;
 return 12*(octave_+1)+selection_.root+intervals[degree];
}
void NoteEngine::press(int source,int degree) {
 const int value=pitch(degree); if(value<0) return;
 // Keep the ascending scale across B8. Standard MIDI cannot encode >127;
 // do not transpose or clamp those notes to a different pitch.
 if(!playable(degree)) { router_.release(source); return; }
 router_.press(source,value);
}
}

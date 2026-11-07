#include "engine.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace sm;
using namespace sm::ripples;
namespace fs=std::filesystem;
namespace {
struct Recording:MidiTransport,Observer {
 std::vector<MidiTrace> events; std::vector<PhraseTrace> phrases;
 bool send(MidiEvent) override { return true; }
 void midi(const MidiTrace& e) override { events.push_back(e); }
 void phrase(const PhraseTrace& p) override { phrases.push_back(p); }
};
using Bytes=std::vector<uint8_t>;
void be(Bytes& out,uint32_t value,int count) { for(int i=count-1;i>=0;--i) out.push_back(uint8_t(value>>(8*i))); }
void vlq(Bytes& out,uint32_t value) { uint8_t data[4]{}; int n=0; data[n++]=uint8_t(value&127); while((value>>=7)) { if(n==4) throw std::runtime_error("MIDI delta too long"); data[n++]=uint8_t((value&127)|128); } while(n) out.push_back(data[--n]); }
Bytes meta(uint8_t type,const std::string& text) { Bytes b{255,type}; vlq(b,uint32_t(text.size())); b.insert(b.end(),text.begin(),text.end()); return b; }
void check(const std::ofstream& file) { if(!file) throw std::runtime_error("Cannot write output file"); }
void midi_file(const fs::path& path,const Recording& rec,const Config& config,uint64_t end) {
 struct Item { uint32_t tick; int priority; Bytes bytes; };
 std::vector<Item> items; const auto tick=[&](uint64_t us){ return uint32_t(std::llround(double(us)*config.bpm*960/60000000.)); };
 Bytes tempo{255,81,3}; be(tempo,uint32_t(60000000/config.bpm),3); items.push_back({0,0,tempo});
 items.push_back({0,1,meta(3,"RIPPLES LAB | seed "+std::to_string(config.seed)+(config.coupled?" | coupled":" | uncoupled"))});
 for(const auto& p:rec.phrases) items.push_back({tick(p.time),2,meta(6,"P"+std::to_string(p.process)+" cycle="+std::to_string(p.cycle)+" motif="+std::to_string(p.motif.id)+" parent="+std::to_string(p.motif.parent)+(p.sounding?"":" rest"))});
 for(const auto& e:rec.events) {
   const auto m=e.event;
   const uint8_t status=m.kind==MidiKind::NoteOn?0x90:m.kind==MidiKind::NoteOff?0x80:0xb0;
   items.push_back({tick(e.time),m.kind==MidiKind::NoteOn?4:3,{uint8_t(status|(m.channel-1)),m.kind==MidiKind::AllNotesOff?uint8_t(123):m.note,m.velocity}});
 }
 std::stable_sort(items.begin(),items.end(),[](const Item& a,const Item& b){ return a.tick<b.tick || (a.tick==b.tick && a.priority<b.priority); });
 Bytes track; uint32_t previous=0;
 for(const auto& item:items) { vlq(track,item.tick-previous); previous=item.tick; track.insert(track.end(),item.bytes.begin(),item.bytes.end()); }
 vlq(track,std::max(previous,tick(end))-previous); track.insert(track.end(),{255,47,0});
 Bytes file{'M','T','h','d'}; be(file,6,4); be(file,0,2); be(file,1,2); be(file,960,2); file.insert(file.end(),{'M','T','r','k'}); be(file,uint32_t(track.size()),4); file.insert(file.end(),track.begin(),track.end());
 std::ofstream output(path,std::ios::binary); output.write(reinterpret_cast<const char*>(file.data()),file.size()); check(output);
}
void export_run(const fs::path& out,const Config& config,int note,int velocity,uint64_t duration) {
 Recording rec; Engine engine(config,rec,&rec);
 if(!engine.stone(0,note,velocity)) throw std::invalid_argument("Seed note must belong to selected scale and pitch range");
 for(uint64_t t=1000;t<duration;t+=1000) engine.advance(t);
 engine.stop(duration); if(!engine.clean()) throw std::runtime_error("Unreleased MIDI note");
 fs::create_directories(out); const auto stem=config.coupled?"B-mit-kopplung":"A-ohne-kopplung";
 midi_file(out/(std::string(stem)+".mid"),rec,config,duration);
 std::ofstream events(out/(std::string(stem)+"-events.csv")); events<<"time_us,event,channel,note,velocity,ticket,process,motif,parent,impulse,cell\n";
 struct Note { MidiTrace on; uint64_t off=0; }; std::vector<Note> notes;
 std::map<uint32_t,size_t> active; std::vector<unsigned> bins(size_t(duration/1000000+1),0);
 std::map<int,unsigned> intervals; std::set<uint32_t> signatures,rhythms; std::array<int,2> last{{-1,-1}};
 unsigned repeated=0,rest_phrases=0,after60=0; double similarity=0,gap_sum=0,max_gap=0; uint64_t previous=0;
 int low=127,high=0,min_velocity=127,max_velocity=0;
 for(const auto& e:rec.events) {
   events<<e.time<<','<<(e.event.kind==MidiKind::NoteOn?"on":e.event.kind==MidiKind::NoteOff?"off":"panic")<<','<<int(e.event.channel)<<','<<int(e.event.note)<<','<<int(e.event.velocity)<<','<<e.ticket<<','<<e.origin.process<<','<<e.origin.motif<<','<<e.origin.parent<<','<<e.origin.impulse<<','<<e.origin.cell<<'\n';
   if(e.event.kind==MidiKind::NoteOn) {
     active[e.ticket]=notes.size(); notes.push_back({e,0}); ++bins[size_t(e.time/1000000)]; after60+=e.time>=60000000;
     low=std::min(low,int(e.event.note)); high=std::max(high,int(e.event.note)); min_velocity=std::min(min_velocity,int(e.event.velocity)); max_velocity=std::max(max_velocity,int(e.event.velocity));
     max_gap=std::max(max_gap,double(e.time-previous)/1000000.); previous=e.time;
     if(e.origin.process>=0) { auto& prior=last[size_t(e.origin.process)]; if(prior>=0) ++intervals[int(e.event.note)-prior]; prior=e.event.note; }
   } else if(e.event.kind==MidiKind::NoteOff) { const auto it=active.find(e.ticket); if(it==active.end()) throw std::runtime_error("Unmatched MIDI Off"); notes[it->second].off=e.time; active.erase(it); }
   else { for(auto item:active) notes[item.second].off=e.time; active.clear(); }
 }
 if(!active.empty()) throw std::runtime_error("MIDI ownership imbalance");
 max_gap=std::max(max_gap,double(duration-previous)/1000000.);
 std::ofstream phrases(out/(std::string(stem)+"-phrases.csv")); phrases<<"time_us,grid,process,cycle,motif,parent,sounding,energy,cycles_per_beat,phase_gap,coupling,signature,relative_degrees,positions,rests\n";
 for(const auto& p:rec.phrases) {
   repeated+=!signatures.insert(signature(p.motif)).second; rest_phrases+=!p.sounding; similarity+=1.-double(differences(p.motif,initial_motif(config.seed)))/p.motif.length; gap_sum+=std::abs(p.phase_gap);
   uint32_t r=0; for(unsigned i=0;i<p.motif.length;++i) r=r*101+p.motif.cells[i].position; rhythms.insert(r);
   phrases<<p.time<<','<<p.grid<<','<<p.process<<','<<p.cycle<<','<<p.motif.id<<','<<p.motif.parent<<','<<p.sounding<<','<<p.energy<<','<<p.rate<<','<<p.phase_gap<<','<<p.coupling<<','<<signature(p.motif)<<',';
   for(unsigned i=0;i<p.motif.length;++i) phrases<<(i?";":"")<<int(p.motif.cells[i].degree); phrases<<',';
   for(unsigned i=0;i<p.motif.length;++i) phrases<<(i?";":"")<<int(p.motif.cells[i].position); phrases<<',';
   for(unsigned i=0;i<p.motif.length;++i) phrases<<(i?";":"")<<p.motif.cells[i].rest; phrases<<'\n';
 }
 const auto s=engine.stats(); const auto m=engine.midi_stats();
 std::ofstream json(out/(std::string(stem)+".json")); json<<std::fixed<<std::setprecision(6);
 json<<"{\n  \"version\": 1, \"phase\": 2, \"seed\": "<<config.seed<<", \"coupled\": "<<(config.coupled?"true":"false")<<", \"bpm\": "<<config.bpm<<", \"root\": "<<config.scale.root<<", \"mode\": "<<config.scale.mode<<", \"seed_note\": "<<note<<", \"seed_velocity\": "<<velocity<<", \"duration_seconds\": "<<double(duration)/1000000<<",\n";
 json<<"  \"metrics\": {\"notes\": "<<m.ons<<", \"offs\": "<<m.offs<<", \"notes_after_60_seconds\": "<<after60<<", \"max_polyphony\": "<<m.max_polyphony<<", \"joined\": "<<m.joined<<", \"dropped\": "<<m.dropped<<", \"rejected\": "<<m.rejected<<", \"pitch_min\": "<<low<<", \"pitch_max\": "<<high<<", \"velocity_min\": "<<min_velocity<<", \"velocity_max\": "<<max_velocity<<", \"max_ons_per_second\": "<<*std::max_element(bins.begin(),bins.end())<<", \"max_onset_gap_seconds\": "<<max_gap<<", \"phrases\": "<<s.phrases<<", \"rest_phrases\": "<<rest_phrases<<", \"mutations\": "<<s.mutations<<", \"recalls\": "<<s.recalls<<", \"motif_signatures\": "<<signatures.size()<<", \"rhythmic_variants\": "<<rhythms.size()<<", \"repeat_fraction\": "<<double(repeated)/rec.phrases.size()<<", \"mean_core_similarity\": "<<similarity/rec.phrases.size()<<", \"mean_abs_phase_gap_at_phrase_boundary\": "<<gap_sum/rec.phrases.size()<<", \"state_bytes\": "<<sizeof(Engine)<<", \"stalls\": "<<s.stalls<<"},\n";
 json<<"  \"onsets_per_second\": ["; for(size_t i=0;i<bins.size();++i) json<<(i?",":"")<<bins[i]; json<<"],\n  \"melodic_intervals_semitones\": {"; bool first=true; for(auto item:intervals) { json<<(first?"":",")<<'\"'<<item.first<<"\":"<<item.second; first=false; } json<<"},\n  \"notes\": [\n";
 for(size_t i=0;i<notes.size();++i) { const auto n=notes[i]; json<<(i?",\n":"")<<"    {\"on_us\":"<<n.on.time<<",\"off_us\":"<<n.off<<",\"note\":"<<int(n.on.event.note)<<",\"velocity\":"<<int(n.on.event.velocity)<<",\"process\":"<<n.on.origin.process<<",\"motif\":"<<n.on.origin.motif<<",\"parent\":"<<n.on.origin.parent<<",\"ticket\":"<<n.on.ticket<<'}'; }
 json<<"\n  ]\n}\n"; check(events); check(phrases); check(json);
 std::cout<<stem<<": "<<m.ons<<" notes, "<<s.phrases<<" phrases, "<<s.mutations<<" variants, "<<s.recalls<<" recalls, max polyphony "<<m.max_polyphony<<"\n";
}
uint32_t number(const std::string& value) { size_t end=0; if(value.empty() || value[0]=='-') throw std::invalid_argument("Unsigned integer expected"); const auto n=std::stoull(value,&end); if(end!=value.size() || n>UINT32_MAX) throw std::invalid_argument("Invalid integer"); return uint32_t(n); }
}
int main(int argc,char** argv) {
 try {
   Config config; int note=60,velocity=100; uint32_t seconds=120; fs::path out="proposals/ripples_lab_v1"; bool suite=false;
   for(int i=1;i<argc;++i) {
     const std::string key=argv[i];
     if(key=="--suite") { suite=true; continue; }
     if(key=="--help") { std::cout<<"RipplesLab [--suite] [--seed UINT32] [--duration 1..600] [--bpm 40..300] [--note MIDI] [--velocity 1..127] [--root 0..11] [--mode 0..11] [--coupling on|off] [--out PATH]\nPhase 2: one impulse, two related processes. No external MIDI-clock implementation.\n"; return 0; }
     if(i+1>=argc) throw std::invalid_argument("Missing option value"); const std::string value=argv[++i];
     if(key=="--out") out=value;
     else if(key=="--coupling") { if(value!="on" && value!="off") throw std::invalid_argument("Coupling must be on/off"); config.coupled=value=="on"; }
     else if(key=="--seed") config.seed=number(value);
     else if(key=="--duration") seconds=number(value);
     else if(key=="--bpm") { const auto n=number(value); if(n>300) throw std::invalid_argument("BPM range"); config.bpm=int(n); }
     else if(key=="--note") { const auto n=number(value); if(n>127) throw std::invalid_argument("MIDI note range"); note=int(n); }
     else if(key=="--velocity") { const auto n=number(value); if(n>127) throw std::invalid_argument("Velocity range"); velocity=int(n); }
     else if(key=="--root") { const auto n=number(value); if(n>11) throw std::invalid_argument("Root range"); config.scale.root=int(n); }
     else if(key=="--mode") { const auto n=number(value); if(n>=modes().size()) throw std::invalid_argument("Mode range"); config.scale.mode=int(n); }
     else throw std::invalid_argument("Unknown option: "+key);
   }
   if(seconds<1 || seconds>600) throw std::invalid_argument("Duration must be 1..600 seconds");
   if(suite) { config.coupled=false; export_run(out,config,note,velocity,seconds*1000000ull); config.coupled=true; }
   export_run(out,config,note,velocity,seconds*1000000ull); return 0;
 } catch(const std::exception& e) { std::cerr<<"RipplesLab: "<<e.what()<<'\n'; return 1; }
}

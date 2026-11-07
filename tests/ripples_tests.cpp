#include "engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>
using namespace sm;
using namespace sm::ripples;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"RIPPLES FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct Output:MidiTransport,Observer {
 bool fail_on=false,fail_off=false,fail_panic=false;
 std::vector<MidiTrace> events; std::vector<PhraseTrace> phrases;
 bool send(MidiEvent e) override { return !(e.kind==MidiKind::NoteOn?fail_on:e.kind==MidiKind::NoteOff?fail_off:fail_panic); }
 void midi(const MidiTrace& e) override { events.push_back(e); }
 void phrase(const PhraseTrace& p) override { phrases.push_back(p); }
 void balanced() const {
   int held[128]{}; uint64_t previous=0;
   for(const auto& t:events) { CHECK(t.time>=previous); previous=t.time; const auto e=t.event; CHECK(e.channel==1);
     if(e.kind==MidiKind::NoteOn) { CHECK(held[e.note]==0 && e.velocity>=1 && e.velocity<=127); held[e.note]=1; }
     else if(e.kind==MidiKind::NoteOff) { CHECK(held[e.note]==1); held[e.note]=0; }
     else for(auto& h:held) h=0;
   }
   for(auto h:held) CHECK(h==0);
 }
};
bool same(const Output& a,const Output& b) {
 if(a.events.size()!=b.events.size() || a.phrases.size()!=b.phrases.size()) return false;
 for(size_t i=0;i<a.events.size();++i) { const auto x=a.events[i],y=b.events[i]; if(x.time!=y.time || x.event.kind!=y.event.kind || x.event.note!=y.event.note || x.event.velocity!=y.event.velocity || x.ticket!=y.ticket || x.origin.motif!=y.origin.motif || x.origin.process!=y.origin.process) return false; }
 for(size_t i=0;i<a.phrases.size();++i) if(a.phrases[i].time!=b.phrases[i].time || signature(a.phrases[i].motif)!=signature(b.phrases[i].motif)) return false;
 return true;
}
Output run(Config config,uint64_t duration,int stride=1) {
 Output out; Engine engine(config,out,&out); CHECK(engine.stone(0,notes(config.scale,4)[0].midi,100));
 for(uint64_t t=uint64_t(stride)*1000;t<duration;t+=uint64_t(stride)*1000) CHECK(engine.advance(t));
 engine.advance(duration); engine.stop(duration); CHECK(engine.clean()); out.balanced(); return out;
}
int main() {
 { // Overlapping same-pitch owners share one On, released at the last deadline.
   Output out; Scheduler s(out,&out); CHECK(s.strike(0,60,100,100000,{0,0,1})); CHECK(s.strike(1000,60,40,200000,{1,0,2}));
   CHECK(out.events.size()==1); s.service(100000); CHECK(out.events.size()==1); s.service(201000);
   CHECK(out.events.size()==2 && out.events[1].ticket==out.events[0].ticket && s.clean()); out.balanced();
 }
 { // Rejected Ons create no Off obligations. Failed Off/Panic retries keep owners.
   Output out; Scheduler s(out,&out); out.fail_on=true; CHECK(!s.strike(0,60,100,1000,{})); CHECK(s.clean());
   out.fail_on=false; CHECK(s.strike(1000,60,50,1000,{})); out.fail_off=out.fail_panic=true; s.stop(1500);
   CHECK(!s.clean() && !s.strike(1600,64,100,1000,{}));
   out.fail_off=out.fail_panic=false; s.service(2000); CHECK(s.clean()); out.balanced();
   CHECK(!s.strike(1999,64,100,1000,{}));
 }
 { // CC123 provides a real fallback when individual Offs are rejected.
   Output out; Scheduler s(out,&out); CHECK(s.strike(0,60,90,10000,{})); out.fail_off=true; s.stop(1000);
   CHECK(s.clean() && s.stats().panic_cleared==1); out.balanced();
 }
 { // Fixed polyphony and burst budget; sustained load cannot exceed 32 Ons/sec.
   Output out; Scheduler s(out,&out);
   for(int i=0;i<8;++i) s.strike(uint64_t(i/4)*1000,60+i,100,1000000,{});
   CHECK(s.polyphony()==8); CHECK(!s.strike(2000,80,100,1000,{})); s.stop(3000); out.balanced();
   Output rate; Scheduler limited(rate,&rate);
   for(uint64_t t=0;t<3000000;t+=10000) { limited.service(t); limited.strike(t,60,80,1000,{}); }
   limited.stop(3000000); CHECK(limited.stats().dropped>0); rate.balanced();
   for(const auto& e:rate.events) if(e.event.kind==MidiKind::NoteOn) { unsigned n=0; for(const auto& f:rate.events) if(f.event.kind==MidiKind::NoteOn && f.time>=e.time && f.time<e.time+1000000) ++n; CHECK(n<=32); }
 }
 { // Same seed is byte/timing reproducible across different host polling cadences.
   Config config; const auto a=run(config,120000000,1),b=run(config,120000000,7); CHECK(same(a,b));
   Config free=config; free.coupled=false; const auto c=run(free,120000000); CHECK(!same(a,c));
   CHECK(a.events.size()>200); unsigned after60=0; std::set<uint32_t> motifs,roles[2],rhythms; unsigned rests=0;
   for(const auto& e:a.events) if(e.event.kind==MidiKind::NoteOn && e.time>=60000000) ++after60;
   for(const auto& p:a.phrases) { motifs.insert(signature(p.motif)); roles[p.process].insert(p.motif.id); rests+=!p.sounding; uint32_t rhythm=0; for(unsigned i=0;i<p.motif.length;++i) rhythm=rhythm*101+p.motif.cells[i].position; rhythms.insert(rhythm); CHECK(differences(p.motif,initial_motif(config.seed))<=2); }
   CHECK(after60>30 && motifs.size()>1 && rhythms.size()>1 && rests>0);
   bool shared=false; for(auto id:roles[0]) shared|=roles[1].count(id)>0; CHECK(shared);
 }
 { // Three-minute development, all Roots and mode cardinalities, varied seeds.
   run({},180000000,13);
   for(int root=0;root<12;++root) for(int mode=0;mode<int(modes().size());++mode) {
     Config c; c.scale={root,mode}; c.seed=uint32_t(root*100+mode);
     const auto out=run(c,6000000,11);
     for(const auto& e:out.events) if(e.event.kind==MidiKind::NoteOn) { bool contained=false; for(auto interval:modes()[mode].intervals) contained|=(int(e.event.note)-root+132)%12==interval; CHECK(contained && e.event.note>=c.low && e.event.note<=c.high); }
   }
 }
 { // Stop never restarts automatically; reset is explicit; late updates skip Ons.
   Output out; Engine engine({},out,&out); CHECK(engine.stone(1000000,60,100)); CHECK(!engine.stone(1001000,67,100));
   for(uint64_t t=1001000;t<=7000000;t+=1000) engine.advance(t);
   engine.stop(7000000); const auto n=out.events.size(); engine.advance(9000000); CHECK(out.events.size()==n && engine.clean());
   CHECK(!engine.advance(8999999)); CHECK(engine.reset(9000000)); CHECK(engine.stone(9000000,60,100));
   const auto before=out.events.size(); engine.advance(90000000); CHECK(out.events.size()<=before+1 && engine.stats().stalls==1);
   engine.stop(90000000); out.balanced();
 }
 CHECK(sizeof(Engine)<=16*1024);
 { // No zero-length new note at the exact stop/export boundary.
   Config c; Output out; Engine engine(c,out,&out); CHECK(engine.stone(0,60,100));
   const auto motif=initial_motif(c.seed);
   const uint64_t boundary=uint64_t(std::ceil(motif.cells[1].position*4.*500000/96/1000))*1000;
   engine.advance(boundary-1000); const auto before=engine.midi_stats().ons; engine.stop(boundary);
   CHECK(engine.midi_stats().ons==before && engine.clean()); out.balanced();
 }
 std::puts("RIPPLES: PASS");
}

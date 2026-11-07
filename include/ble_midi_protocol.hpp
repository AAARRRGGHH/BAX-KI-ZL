#pragma once
#include "music.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
namespace sm {
using BleMidiPacket=std::array<uint8_t,5>;
inline bool encode_ble_midi(MidiEvent e,uint32_t milliseconds,BleMidiPacket& p) {
 if(e.channel<1 || e.channel>16) return false;
 if(e.kind!=MidiKind::AllNotesOff && (e.note>127 || e.velocity>127)) return false;
 uint8_t status;
 switch(e.kind) {
 case MidiKind::NoteOn: status=0x90; break;
 case MidiKind::NoteOff: status=0x80; break;
 case MidiKind::ControlChange:
 case MidiKind::AllNotesOff: status=0xb0; break;
 default: return false;
 }
 p={uint8_t(0x80|((milliseconds>>7)&0x3f)),uint8_t(0x80|(milliseconds&0x7f)),
    uint8_t(status|(e.channel-1)),e.kind==MidiKind::AllNotesOff?uint8_t(123):e.note,
    e.kind==MidiKind::AllNotesOff?uint8_t(0):e.velocity};
 return true;
}
// Caller supplies synchronization. All operations have bounded time and no allocation.
// A new subscription or an overflow starts a fresh session: no old Note On is replayed.
class BleMidiOutbox {
 struct Entry { MidiEvent event{}; uint32_t time=0; };
 std::array<Entry,128> entries_{};
 size_t head_=0,count_=0;
 uint32_t generation_=0,serial_=0;
 unsigned cleanup_=0;
 bool subscribed_=false,reset_=false;
public:
 struct Pending { BleMidiPacket packet{}; uint32_t generation=0,serial=0; unsigned count=1; };
 struct Batch { std::array<uint8_t,20> packet{}; size_t length=0; uint32_t age_ms=0; Pending token{}; };
 void session(bool subscribed) {
   subscribed_=subscribed; head_=count_=0; cleanup_=subscribed?32:0;
   ++generation_; reset_=true;
 }
 bool reset_pending() const { return reset_; }
 uint32_t generation() const { return generation_; }
 void acknowledge_reset(uint32_t generation) { if(generation==generation_) reset_=false; }
 bool ready() const { return subscribed_ && !reset_ && cleanup_==0; }
 bool enqueue(MidiEvent e,uint32_t now) {
   BleMidiPacket packet; if(!encode_ble_midi(e,now,packet)) return false;
   if(!ready()) return e.kind!=MidiKind::NoteOn;
   if(count_==entries_.size()) { session(true); return e.kind!=MidiKind::NoteOn; }
   entries_[(head_+count_)%entries_.size()]={e,now}; ++count_; return true;
 }
 bool peek(uint32_t now,Pending& pending) const {
   if(!subscribed_) return false;
   pending.generation=generation_; pending.serial=serial_;
   pending.count=1;
   if(cleanup_) {
     const unsigned index=32-cleanup_;
     encode_ble_midi({MidiKind::AllNotesOff,uint8_t(index/2+1),0,0},now,pending.packet);
     pending.packet[3]=index%2?120:123; // All Sound Off follows All Notes Off, all channels.
     return true;
   }
   if(!count_) return false;
   return encode_ble_midi(entries_[head_].event,entries_[head_].time,pending.packet);
 }
 // BLE-only packing. USB keeps using peek(): its packet format is different.
 // Never cross timestampHigh boundaries; four messages fit even ATT-MTU 23.
 bool peek_batch(uint32_t now,Batch& batch) const {
   if(!peek(now,batch.token)) return false;
   batch.age_ms=cleanup_?0:uint32_t(now-entries_[head_].time);
   batch.length=5;
   for(size_t i=0;i<5;++i) batch.packet[i]=batch.token.packet[i];
   if(cleanup_) return true;
   for(size_t i=1;i<count_ && batch.length+4<=batch.packet.size();++i) {
     BleMidiPacket packet;
     const auto& entry=entries_[(head_+i)%entries_.size()];
     if(!encode_ble_midi(entry.event,entry.time,packet) || packet[0]!=batch.packet[0]) break;
     for(size_t j=1;j<5;++j) batch.packet[batch.length++]=packet[j];
     ++batch.token.count;
   }
   return true;
 }
 void delivered(const Pending& pending) {
   if(pending.generation!=generation_ || pending.serial!=serial_) return;
   if(cleanup_) --cleanup_;
   else if(count_) { const auto n=std::min(count_,size_t(pending.count)); head_=(head_+n)%entries_.size(); count_-=n; }
   ++serial_;
 }
};
}

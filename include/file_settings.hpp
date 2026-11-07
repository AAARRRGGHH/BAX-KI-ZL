#pragma once
#include "music.hpp"
namespace sm {
class FileSettings : public Settings {
 std::string path_;
public:
 explicit FileSettings(std::string path):path_(std::move(path)){}
 MotionSettings load_motion() override;
 bool save_motion(MotionSettings s) override;
 Selection load() override;
 bool save(Selection s) override;
 Setup load_setup() override;
 bool save_setup(Setup s) override;
 ChordSelection load_chords() override;
 bool save_chords(ChordSelection s) override;
 Selection load_arp() override;
 bool save_all(Selection keyboard,Selection arp,ChordSelection chords,Setup setup) override;
 bool load_slot(int slot,MusicPreset& value) override;
 bool delete_slot(int slot) override;
 bool save_slot(int slot,const MusicPreset& value) override;
 bool save_music(const MusicPreset& value,Setup setup) override;
 ArpPreset load_arpeggiator() override;
 bool save_arpeggiator(ArpPreset value) override;
 bounce::Settings load_bounce() override;
 bool save_bounce(bounce::Settings value) override;
};
}

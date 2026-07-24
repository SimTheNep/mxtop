#include "state.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

stateLayer::stateLayer(const moduleDef& module) : module_(module) {

    // Yay... if chains...
    for (const auto& obj : module_.objects) {
        if (obj.type == kind::CC && obj.cc) {
            ccIndex_[*obj.cc].push_back(&obj);
        } else if (obj.type == kind::PitchBend) {
            pitchBendIndex_.push_back(&obj);
        }
    }
}

void stateLayer::storeValue(channelState& ch, const std::vector<const ModuleObject*>& targets, int value) {
    for (const ModuleObject* obj : targets)
        ch.rawValues[obj->id] = value;
}

void stateLayer::eventHandler(const RawEvent& ev) {
    channelState& ch = channels_[ev.channel];

    switch (ev.kind) {
        case MsgKind::CC:
            storeValue(ch, ccIndex_[ev.data[1]], ev.data[2]);
            break;
        
        case MsgKind::PitchBend:
            storeValue(ch, pitchBendIndex_, (ev.data[2] << 7) | ev.data[1]);
            break;

        default:
            break;
    }
}

const channelState* stateLayer::getChannel(int channel) const {
    auto it = channels_.find(channel);
    return it == channels_.end() ? nullptr : &it->second;
}

int main(){ // Scratch

    // PARAMETERS
    //
    //

    ModuleObject volumeObj;
    volumeObj.id = "volume";
    volumeObj.type = kind::CC;
    volumeObj.cc = 7;

    ModuleObject sustainObj;
    sustainObj.id = "sustain";
    sustainObj.type = kind::CC;
    sustainObj.cc = 64;

    ModuleObject pitchObj;
    pitchObj.id = "pitchbend";
    pitchObj.type = kind::PitchBend;


    moduleDef module;
    module.objects = { volumeObj, sustainObj, pitchObj };

    stateLayer state(module); // Built after objects are populared

    // OUTPUTS
    //
    //

    // ch 0: volume
    RawEvent ev1;
    ev1.kind = MsgKind::CC;
    ev1.channel = 0;
    ev1.data = { 0xB0, 7, 100 };
    state.eventHandler(ev1);

    // ch 0: sustain
    RawEvent ev2;
    ev2.kind = MsgKind::CC;
    ev2.channel = 0;
    ev2.data = { 0xB0, 64, 127 };
    state.eventHandler(ev2);

    // ch 1: pitchbend
    RawEvent ev3;
    ev3.kind = MsgKind::PitchBend;
    ev3.channel = 1;
    ev3.data = { 0xE1, 0x00, 0x50 };
    state.eventHandler(ev3);

    // Checks
    auto print = [&](int channel) {
        const channelState* ch = state.getChannel(channel);
        if (!ch) { printf("ch=%d -> no state\n", channel); return; }
        for (const auto& [id, value] : ch->rawValues) printf("ch=%d %s=%d\n", channel, id.c_str(), value);
    };

    print(0);
    print(1);
    print(2); // Should print "no state"

    // Fallback so an unknown kind doesn't crash
    RawEvent junk;
    junk.kind = MsgKind::NoteOn;
    junk.channel = 5;
    junk.data = { 0x95, 60, 100 };
    state.eventHandler(junk);
    print(5);
}
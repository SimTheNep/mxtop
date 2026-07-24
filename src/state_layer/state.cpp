#include "state.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#include <optional>
#include <cstdio>

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;

    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));

    return out;
}

// Builds a patch sequence from fields, anything not present defaults to 0
static channelState::patchState makeDefaultPatchState(const ModuleObject& obj) {
    channelState::patchState p;

    if (auto it = obj.fields.find("msb"); it != obj.fields.end())
        p.msb = it->second;
    if (auto it = obj.fields.find("lsb"); it != obj.fields.end())
        p.lsb = it->second;
    if (auto it = obj.fields.find("program"); it != obj.fields.end())
        p.program = it->second;

    return p;
}

// Constructor for the module (had to be in between both decoders cus order)
stateLayer::stateLayer(const moduleDef& module) : module_(module) {

    // Yay... if chains...
    for (const auto& obj : module_.objects) {
        if (obj.type == kind::CC && obj.cc) {
            ccIndex_[*obj.cc].push_back(&obj);
        } else if (obj.type == kind::PitchBend) {
            pitchBendIndex_.push_back(&obj);
        } else if (obj.type == kind::SysEx && obj.address) {
            sysexIndex_.emplace_back(hexToBytes(*obj.address), &obj);
        } else if (obj.type == kind::Patch) {
            patchBinding pb;
            pb.obj = &obj;
            pb.hasMsb     = std::find(obj.sequence.begin(), obj.sequence.end(), "cc0")  != obj.sequence.end();
            pb.hasLsb     = std::find(obj.sequence.begin(), obj.sequence.end(), "cc32") != obj.sequence.end();
            pb.hasProgram = std::find(obj.sequence.begin(), obj.sequence.end(), "pc")   != obj.sequence.end();
            patchIndex_.push_back(pb);
        }
    }
}


// IMPORTANT: SYSEX DECODER/HANDLER
//
//

void stateLayer::handleSysEx(const RawEvent& ev, channelState& ch) {
    for (const auto& [addrBytes, obj] : sysexIndex_) {
        int valueBytes = obj->bytes.value_or(1);

        // Try both 5-byte and 6-byte header offsets (GS vs SD-90/Modern Roland)
        for (size_t headerLen : {5, 6}) {
            size_t valueStart = headerLen + addrBytes.size();

            // Check packet bounds
            if (ev.data.size() < valueStart + valueBytes + 1) continue;

            // Check address match
            if (std::equal(addrBytes.begin(), addrBytes.end(), ev.data.begin() + headerLen)) {
                int value = 0;

                // NIBBLE DECODER
                if (obj->encoding && *obj->encoding == "nibbles") {
                    for (int i = 0; i < valueBytes; ++i)
                        value = (value << 4) | (ev.data[valueStart + i] & 0x0F);
                } else {
                    for (int i = 0; i < valueBytes; ++i) {
                        value = (value << 8) | ev.data[valueStart + i];
                    }
                }

                ch.rawValues[obj->id] = value;
                break; // Found match for this object!
            }
        }
    }
}

// CC, PB & PATCH STORAGE & HANDLER
//
//

void stateLayer::storeValue(channelState& ch, const std::vector<const ModuleObject*>& targets, int value) {
    for (const ModuleObject* obj : targets)
        ch.rawValues[obj->id] = value;
}

// Update every patch that has CC0 and CC32... Expandable if a module by any chance has more than 1 way to define a patch.
void stateLayer::handlePatchCC(channelState& ch, int ccNum, int value) {
    for (const auto& pb : patchIndex_) {
        if (ccNum == 0  && !pb.hasMsb) continue;
        if (ccNum == 32 && !pb.hasLsb) continue;

        auto [it, inserted] = ch.patches.try_emplace(pb.obj->id, makeDefaultPatchState(*pb.obj));
        if (ccNum == 0) it->second.msb = value;
        else            it->second.lsb = value;
    }
}

// Completes the sequence with the PC when it arrives (this is separate because MSB and LSB do not affect it)
void stateLayer::handlePC(channelState& ch, int program) {
    for (const auto& pb : patchIndex_) {
        if (!pb.hasProgram) continue;

        auto [it, inserted] = ch.patches.try_emplace(pb.obj->id, makeDefaultPatchState(*pb.obj));
        it->second.program = program;
    }
}

void stateLayer::eventHandler(const RawEvent& ev) {
    channelState& ch = channels_[ev.channel];

    switch (ev.kind) {
        case MsgKind::CC:
            if (ev.data.size() >= 3 && ev.data[1] < 128) {
                int ccNum = ev.data[1];
                int value = ev.data[2];

                const auto& targets = ccIndex_[ccNum];
                if (!targets.empty()) {
                    storeValue(ch, targets, value);
                }

                if (ccNum == 0 || ccNum == 32)
                    handlePatchCC(ch, ccNum, value);
            }
            break;

        case MsgKind::PitchBend:
            if (ev.data.size() >= 3) {
                storeValue(ch, pitchBendIndex_, (ev.data[2] << 7) | ev.data[1]);
            }
            break;

        case MsgKind::SysEx:
            handleSysEx(ev, ch);
            break;

        case MsgKind::ProgramChange:
            if (ev.data.size() >= 2) {
                handlePC(ch, ev.data[1]);
            }
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

    ModuleObject sysexObj;
    sysexObj.id = "sysex";
    sysexObj.type = kind::SysEx;
    sysexObj.address = "01000000";
    sysexObj.bytes = 4;
    sysexObj.encoding = "nibbles";

    ModuleObject sysexObjRaw;
    sysexObjRaw.id = "sysex_raw";
    sysexObjRaw.type = kind::SysEx;
    sysexObjRaw.address = "01000000";
    sysexObjRaw.bytes = 4;
    sysexObjRaw.encoding = std::nullopt;

    ModuleObject patchObj;
    patchObj.id = "patch";
    patchObj.type = kind::Patch;
    patchObj.sequence = { "cc0", "cc32", "pc" };
    patchObj.fields = { {"msb", 0}, {"lsb", 0}, {"program", 0} };

    moduleDef module;
    module.objects = { volumeObj, sustainObj, pitchObj, sysexObj, sysexObjRaw, patchObj };

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

    // ch -1: sysex
    RawEvent ev4;
    ev4.kind = MsgKind::SysEx;
    ev4.channel = -1;
    ev4.data = {
        0xF0, 0x41, 0x10, 0x00, 0x48, 0x12,   // header
        0x01, 0x00, 0x00, 0x00,               // address
        0x00, 0x04, 0x00, 0x00,               // value
        0x00,                                  // checksum
        0xF7
    };
    state.eventHandler(ev4);

    RawEvent ev5;
    ev5.kind = MsgKind::SysEx;
    ev5.channel = -1;
    ev5.data = {
        0xF0, 0x41, 0x10, 0x00, 0x48, 0x12,
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x00,
        0x00,
        0xF7
    };
    state.eventHandler(ev5);

    // ch 0: bank select MSB
    RawEvent ev6;
    ev6.kind = MsgKind::CC;
    ev6.channel = 0;
    ev6.data = { 0xB0, 0, 80 }; // Should change to 80
    state.eventHandler(ev6);

    // ch 0: bank select LSB
    RawEvent ev7;
    ev7.kind = MsgKind::CC;
    ev7.channel = 0;
    ev7.data = { 0xB0, 32, 1 }; // Should change to 1
    state.eventHandler(ev7);

    // ch 0: program change
    RawEvent ev8;
    ev8.kind = MsgKind::ProgramChange;
    ev8.channel = 0;
    ev8.data = { 0xC0, 5 }; // Should change to 5
    state.eventHandler(ev8);

    // Checks (for patch logic)
    const channelState* ch0 = state.getChannel(0);
    if (ch0) {
        auto it = ch0->patches.find("patch");
        if (it != ch0->patches.end()) {
            printf("ch=0 patch msb=%d lsb=%d program=%d\n",
                it->second.msb, it->second.lsb, it->second.program);
        }
        printf("ch=0 volume=%d\n", ch0->rawValues.at("volume"));
    }

    // Checks
    auto print = [&](int channel) {
        const channelState* ch = state.getChannel(channel);
        if (!ch) { printf("ch=%d -> no state\n", channel); return; }
        for (const auto& [id, value] : ch->rawValues) printf("ch=%d %s=%d\n", channel, id.c_str(), value);
    };

    print(0); // Should print sustain and volume
    print(1); // Should print PB
    print(2); // Should print "no state"
    print(-1); // Should print "1024" and raw string

    // Fallback so an unknown kind doesn't crash
    RawEvent junk;
    junk.kind = MsgKind::NoteOn;
    junk.channel = 5;
    junk.data = { 0x95, 60, 100 };
    state.eventHandler(junk);
    print(5);
}
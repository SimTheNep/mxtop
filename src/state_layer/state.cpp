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
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <cmath>

// CONVERT HEX TO BYTES
//
//
static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;

    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));

    return out;
}

// DEFAULT PATCH STATE
//
// Builds a patch sequence from fields, anything not present defaults to 0
static channelState::patchState defaultPatch(const ModuleObject& obj) {
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
stateLayer::stateLayer(const moduleDef& module, const dictionaryDef& dictionary) : module_(module), dictionary_(dictionary) {

    // Yay... if chains...
    for (const auto& obj : module_.objects) {
        objectById_[obj.id] = &obj;

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

    // For patch and effect names
    for (const auto& [name, dict] : dictionary_.enums)
    {
        auto& lookup = enumLookup_[name];

        for (const auto& item : dict.values)
            lookup[item.id] = item.name;
    }
}


// IMPORTANT: SYSEX DECODER/HANDLER
//
//

static bool verifyRolandChecksum(const std::vector<uint8_t>& data, size_t addressStart, size_t checksumIndex)
    {
        // CHecks roland checksum and calculates it
        int sum = 0;

        for (size_t i = addressStart; i < checksumIndex; ++i)
            sum += data[i];

        int expected = (128 - (sum & 0x7F)) & 0x7F;

        return (data[checksumIndex] & 0x7F) == expected;
    }

void stateLayer::handleSysEx(const RawEvent& ev, channelState& ch) {

    // Validade SysEx structure from module.json
    if (ev.data.size() < headerLen_ + 2)
        return;

    if (ev.data.front() != 0xF0)
        return;

    if (ev.data.back() != 0xF7)
        return;

    if (ev.data[1] != module_.manufacturer)
        return;

    if (ev.data[2] != module_.deviceId)
        return;

    if (ev.data[4] != module_.model)
        return;

    if (ev.data[5] != 0x12)
    return;

    // Roland checksum check
    if (module_.checksum == "roland")
    {
        const size_t checksumIndex = ev.data.size() - 2;

        if (!verifyRolandChecksum(
                ev.data,
                headerLen_,
                checksumIndex))
            return;
    }

    for (const auto& [addrBytes, obj] : sysexIndex_) {
        const int valueBytes = obj->bytes.value_or(1);

        const size_t addressStart = headerLen_;
        const size_t valueStart   = addressStart + addrBytes.size();

        // Check packet bounds
        if (ev.data.size() < valueStart + valueBytes + 1) continue;

        // Check address match
        if (std::equal(addrBytes.begin(), addrBytes.end(), ev.data.begin() + headerLen_)) {
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

// CC, PB & PATCH STORAGE & HANDLER
//
//

void stateLayer::storeValue(channelState& ch, const std::vector<const ModuleObject*>& targets, int value) {
    for (const ModuleObject* obj : targets)
        ch.rawValues[obj->id] = value;
}

// Update every patch that has CC0 and CC32... Expandable if a module by any chance has more than 1 way to define a patch.
void stateLayer::updtBank(channelState& ch, int ccNum, int value) {
    for (const auto& pb : patchIndex_) {
        if (ccNum == 0  && !pb.hasMsb) continue;
        if (ccNum == 32 && !pb.hasLsb) continue;

        auto [it, inserted] = ch.patches.try_emplace(pb.obj->id, defaultPatch(*pb.obj));
        if (ccNum == 0) it->second.msb = value;
        else            it->second.lsb = value;
    }
}

// Completes the sequence with the PC when it arrives (this is separate because MSB and LSB do not affect it)
void stateLayer::updtPC(channelState& ch, int program) {
    for (const auto& pb : patchIndex_) {
        if (!pb.hasProgram) continue;

        auto [it, inserted] = ch.patches.try_emplace(pb.obj->id, defaultPatch(*pb.obj));
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
                    updtBank(ch, ccNum, value);
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
                updtPC(ch, ev.data[1]);
            }
            break;

        case MsgKind::NoteOn:
            if (ev.data.size() >= 3) {
                updtNote(ch, ev.data[1], ev.velocity, /*on=*/true);
            }
            break;

        case MsgKind::NoteOff:
            if (ev.data.size() >= 2) {
                updtNote(ch, ev.data[1], 0, /*on=*/false);
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

// This is for reverb, delay, etc... the lookup on the module.json
std::optional<std::string> stateLayer::effLookup(const ModuleObject& obj, int value) const 
{
    if (!obj.lookup)
    return std::nullopt;

    auto dict = enumLookup_.find(*obj.lookup);

    if (dict == enumLookup_.end())
        return std::nullopt;

    auto it = dict->second.find(value);

    if (it == dict->second.end())
        return std::nullopt;

    return it->second;

    return std::nullopt;
}

// Assigns patch names to the patch numbers
std::optional<std::string>
stateLayer::patchLookup(const channelState::patchState& patch) const
{
    auto groupIt = dictionary_.bankGroups.find("patches");

    if (groupIt == dictionary_.bankGroups.end())
        return std::nullopt;

    if (groupIt == dictionary_.bankGroups.end())
        return std::nullopt;

    for (const auto& bank : groupIt->second)
    {
        // Check MSB/LSB
        if (bank.bank.bankMSB &&
            *bank.bank.bankMSB != patch.msb)
            continue;

        if (bank.bank.bankLSB &&
            *bank.bank.bankLSB != patch.lsb)
            continue;

        // Find rpogram
        for (const auto& item : bank.items)
        {
            if (item.program != patch.program)
                continue;

            // Per-patch overrides
            if (item.bankMSB &&
                *item.bankMSB != patch.msb)
                continue;

            if (item.bankLSB &&
                *item.bankLSB != patch.lsb)
                continue;

            return item.name;
        }
    }

    return std::nullopt;
}

// CALCULATIONS
//
//

std::string stateLayer::mathVal(const ModuleObject& obj, int raw) const {
    // Enum lookups take priority
    if (auto name = effLookup(obj, raw))
        return *name;

    const std::string& transform = obj.displayOffset.transform;

    if (transform == "pan_lcr") {
        // Generic offset pan gets its own L/C/R formatting
        int v = raw - 64;
        if (v < 0) return std::to_string(-v) + "L";
        if (v > 0) return std::to_string(v) + "R";
        return "C";
    }

    if (transform == "frequency") {
        // Master tuning calculation: 440*((X-1024)/8192)^2
        double hz = 440.0 * std::pow(2.0, (raw - 1024.0) / 8192.0);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f Hz", hz);
        return buf;
    }

    // Generic offset, -64 turns 0-127 into -64/+63... etc...
    return std::to_string(raw + obj.displayOffset.offset);
}

std::optional<std::string> stateLayer::finalVal(int channel, const std::string& objectId) const {
    const channelState* ch = getChannel(channel);
    if (!ch) return std::nullopt;

    auto valueIt = ch->rawValues.find(objectId);
    if (valueIt == ch->rawValues.end()) return std::nullopt;

    auto objIt = objectById_.find(objectId);
    if (objIt == objectById_.end()) return std::nullopt;

    return mathVal(*objIt->second, valueIt->second);
}

// NOTE STATE (Contributes to polyphony count)
//
//

void stateLayer::updtNote(channelState& ch, int note, int velocity, bool on) {
    if (on) {
        ch.activeNotes[note] = velocity;
    } else {
        ch.activeNotes.erase(note);
    }
    ch.lastNote = note;
    ch.lastVelo = velocity;
}

// SAVE SNAPSHOT
//
//

takeSnapshot stateLayer::snapshot(int channel) const {
    takeSnapshot snap;

    const channelState* ch = getChannel(channel);
    if (!ch) return snap; // If channel isn't touched, it doesn't crash

    for (const auto& [id, raw] : ch->rawValues) {
        auto objIt = objectById_.find(id);
        if (objIt == objectById_.end()) continue; // Shouldn't happen, but don't crash if it does
        snap.values[id] = mathVal(*objIt->second, raw);
    }

    for (const auto& [id, patch] : ch->patches) {
        snap.patchNames[id] = patchLookup(patch);
    }

    snap.polyCount = static_cast<int>(ch->activeNotes.size());
    snap.lastNote = ch->lastNote;
    snap.lastVelo = ch->lastVelo;

    return snap;
}





// TEST VALUES
//
//

int main(){
    std::filesystem::path moduleDir = "../../modules/sd-90";

    using json = nlohmann::json;

    std::ifstream moduleFile(moduleDir / "module.json");
    if (!moduleFile.is_open()) {
        printf("Couldn't open: %s\n", (moduleDir / "module.json").c_str());
        return 1;
    }
    json moduleJson;
    moduleFile >> moduleJson;
    moduleDef module = parseModule(moduleJson);

    std::ifstream dictFile(moduleDir / "dictionary.json");
    if (!dictFile.is_open()) {
        printf("Couldn't open: %s\n", (moduleDir / "dictionary.json").c_str());
        return 1;
    }
    json dictJson;
    dictFile >> dictJson;
    dictionaryDef dictionary = parseDictionary(dictJson);

    // Fake SysEx: master tuning
    RawEvent ev;
    ev.kind = MsgKind::SysEx;
    ev.channel = -1;
    ev.data = {
        0xF0, module.manufacturer, module.deviceId, 0x00, module.model, 0x12,
        0x01, 0x00, 0x00, 0x00,   // Address
        0x00, 0x04, 0x00, 0x00,   // Value (4 nibbles = 0x0400)
        0x7B,                      // Checksum
        0xF7
    };

    // Pan test object
    ModuleObject panObj;
    panObj.id = "pan";
    panObj.type = kind::CC;
    panObj.cc = 10;
    panObj.displayOffset.transform = "pan_lcr";

    // Bipolar test object
    ModuleObject bipolarObj;
    bipolarObj.id = "bipolar_test";
    bipolarObj.type = kind::CC;
    bipolarObj.cc = 20;
    bipolarObj.displayOffset.offset = -64; // Expect 36

    module.objects.push_back(panObj);
    module.objects.push_back(bipolarObj);

    stateLayer state(module, dictionary); // Built after every object exists

    RawEvent panEv;
    panEv.kind = MsgKind::CC;
    panEv.channel = 0;
    panEv.data = { 0xB0, 10, 20 };
    state.eventHandler(panEv);

    RawEvent bipolarEv;
    bipolarEv.kind = MsgKind::CC;
    bipolarEv.channel = 0;
    bipolarEv.data = { 0xB0, 20, 100 };
    state.eventHandler(bipolarEv);

    RawEvent noteOn;
    noteOn.kind = MsgKind::NoteOn;
    noteOn.channel = 0;
    noteOn.velocity = 100;
    noteOn.data = { 0x90, 60, 100 };
    state.eventHandler(noteOn);

    RawEvent noteOn2;
    noteOn2.kind = MsgKind::NoteOn;
    noteOn2.channel = 0;
    noteOn2.velocity = 80;
    noteOn2.data = { 0x90, 64, 80 };
    state.eventHandler(noteOn2);

    // Snapshot ch0
    {
        auto snap = state.snapshot(0);
        for (const auto& [id, display] : snap.values)
            printf("  %s = %s\n", id.c_str(), display.c_str());
        printf("  poly = %d lastNote = %d lastVel = %d\n", snap.polyCount, snap.lastNote, snap.lastVelo);
    }

    RawEvent noteOff;
    noteOff.kind = MsgKind::NoteOff;
    noteOff.channel = 0;
    noteOff.data = { 0x80, 60, 0 };
    state.eventHandler(noteOff);

    // Snapshot channel 0 to see poly change
    {
        auto snap = state.snapshot(0);
        for (const auto& [id, display] : snap.values)
            printf("  %s = %s\n", id.c_str(), display.c_str());
        printf("  poly after note off = %d\n", snap.polyCount);
    }

    state.eventHandler(ev);

    // Snapshot sysex
    {
        auto snap = state.snapshot(-1);
        for (const auto& [id, display] : snap.values)
            printf("  %s = %s\n", id.c_str(), display.c_str());
    }
}
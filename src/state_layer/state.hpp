#pragma once

#include "../midi_reader/types.hpp"
#include "../json_parser/parser.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>


// PATCH IDENTIFICATION LOGIC FROM DICTIONARY

std::unordered_map<std::string, std::unordered_map<int, std::string>> enumLookup_;

struct patchID
{
    int msb;
    int lsb;
    int program;

    bool operator==(const patchID& other) const
    {
        return msb == other.msb &&
               lsb == other.lsb &&
               program == other.program;
    }
};

struct patchIDhash
{
    size_t operator()(const patchID& k) const
    {
        return (k.msb << 16) ^
               (k.lsb << 8) ^
               k.program;
    }
};

std::unordered_map<patchID, std::string, patchIDhash> patchLookup_;


// Other stuff
struct channelState {
    std::unordered_map<std::string, int> rawValues; // Object id outputs last raw value

    struct patchState { // Patches
        int msb = 0;
        int lsb = 0;
        int program = 0;
    };
    std::unordered_map<std::string, patchState> patches;

    // Note tracking
    std::unordered_map<int, int> activeNotes; // Poly count
    int lastNote = -1;
    int lastVelo = 0;

};

// Snapshot
struct takeSnapshot {
    std::unordered_map<std::string, std::string> values; // Object ID
    std::unordered_map<std::string, std::optional<std::string>> patchNames; // Patch ID
    int polyCount = 0;
    int lastNote = -1;
    int lastVelo = 0;
};

// State
class stateLayer {
public:
    explicit stateLayer(const moduleDef& module, const dictionaryDef& dictionary);
    void eventHandler(const RawEvent& ev);

    const channelState* getChannel(int channel) const;

    std::optional<std::string> effLookup(const ModuleObject& obj, int value) const;
    std::optional<std::string> patchLookup(const channelState::patchState& patch) const;
    std::string mathVal(const ModuleObject& obj, int raw) const;
    std::optional<std::string> finalVal(int channel, const std::string& objectId) const;

    takeSnapshot snapshot(int channel) const;

private:
    void handleSysEx(const RawEvent& ev, channelState& ch);
    void updtPC(channelState& ch, int program);
    void updtBank(channelState& ch, int ccNum, int value);
    void updtNote(channelState& ch, int note, int velocity, bool on);
    void storeValue(channelState& ch, const std::vector<const ModuleObject*>& targets, int value);

    const moduleDef& module_;
    const dictionaryDef& dictionary_;

    static constexpr size_t headerLen_ = 6;

    std::array<std::vector<const ModuleObject*>, 128> ccIndex_{}; // Zero-initialized array
    std::vector<const ModuleObject*> pitchBendIndex_;
    std::vector<std::pair<std::vector<uint8_t>, const ModuleObject*>> sysexIndex_;
    std::unordered_map<std::string, const ModuleObject*> objectById_;

    struct patchBinding {
        const ModuleObject* obj = nullptr;
        bool hasMsb = false;     // Sequence has CC0
        bool hasLsb = false;     // Sequence has CC32
        bool hasProgram = false; // Sequence has PC
    };

    std::vector<patchBinding> patchIndex_;


    std::unordered_map<int, channelState> channels_;
};
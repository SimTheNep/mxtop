#pragma once

#include "../midi_reader/types.hpp"
#include "../midi_reader/midi_load.hpp"
#include "../json_parser/parser.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <optional>

// Other stuff
struct channelState {
    std::unordered_map<std::string, int> rawValues; // Object id outputs last raw value

    struct patchState { // Patches
        int msb = 0;
        int lsb = 0;
        int program = 0;

        std::unordered_map<std::string, int> values;
    };
    std::unordered_map<std::string, patchState> patches;

    // Note tracking
    std::unordered_map<int, int> activeNotes; // Poly count
    int lastNote = -1;
    int lastVelo = 0;
};

// Universal Data Bind struct
struct dataBind {
    std::vector<std::optional<uint8_t>> pattern; // nullopt = [VAL]
    const ModuleObject* obj = nullptr;
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
    explicit stateLayer(const moduleDef& module, const dictionaryDef& dictionary, MidiReader& reader);

    void eventHandler(const RawEvent& ev);
    void advance(double elapsedMs);

    const channelState* getChannel(int channel) const;

    // Which channels have data so far
    std::vector<int> activeCh() const;

    std::optional<std::string> effLookup(const ModuleObject& obj, int value) const;
    std::optional<std::string> patchLookup(int channel, const channelState::patchState& patch) const;

    std::string mathVal(const ModuleObject& obj, int raw) const;
    std::optional<std::string> finalVal(int channel, const std::string& objectId) const;

    takeSnapshot snapshot(int channel) const;

private:
    enum class patchField { Msb, Lsb, Program };
    const ModuleObject* defaultPatchObject_ = nullptr;

    struct sysexBinding { // Allows per-patch SysEx
        std::vector<uint8_t> addr;
        const ModuleObject* obj = nullptr;
        int channel = -1; // Defaults to the SysEx channel
    };

    struct patchSysexBinding {
        std::vector<uint8_t> addr;
        const ModuleObject* obj = nullptr;
        patchField field;
        int channel = 0; // Part A or B in SD-90 case
    };

    struct patchBinding {
        const ModuleObject* obj = nullptr;
        bool hasMsb = false;     // Sequence has CC0
        bool hasLsb = false;     // Sequence has CC32
        bool hasProgram = false; // Sequence has PC
    };

    bool dataHandler(const RawEvent& ev, channelState& sysCh);

    void handleSysEx(const RawEvent& ev, channelState& ch);
    void updtPC(channelState& ch, int program);
    void updtBank(channelState& ch, int ccNum, int value);
    void updtNote(channelState& ch, int note, int velocity, bool on);
    void storeValue(channelState& ch, const std::vector<const ModuleObject*>& targets, int value);
    void initCh(channelState& ch, int channel);
    void setMFX(channelState& targetCh, int outputAssign, int mfxSelect);
    
    void setPatch(channelState& targetCh, const ModuleObject& obj, patchField field, int value);
    void applyPatch(
        channelState& targetCh,
        const patchSysexBinding& binding,
        const RawEvent& ev,
        const patchSysexPart& partTemplate,
        const std::optional<std::string>& addrOpt,
        patchField field,
        int packedOffset,
        size_t valueStart,
        size_t checksumIndex);

    channelState::patchState* activePatch(channelState& ch);
    const channelState::patchState* activePatch(const channelState& ch) const;

    const moduleDef& module_;
    const dictionaryDef& dictionary_;
    MidiReader& reader_;

    static constexpr size_t headerLen_ = 6;

    std::array<std::vector<const ModuleObject*>, 128> ccIndex_{}; 
    std::vector<const ModuleObject*> pitchBendIndex_;

    std::vector<sysexBinding> sysexIndex_;
    std::vector<patchSysexBinding> patchSysexIndex_;
    std::unordered_map<std::string, const ModuleObject*> objectById_;

    std::vector<dataBind> dataIndex_;

    // Effect/enum name (kind -> value -> display name).
    std::unordered_map<std::string, std::unordered_map<int, std::string>> enumLookup_;

    std::vector<patchBinding> patchIndex_;

    std::unordered_map<int, channelState> channels_;
};
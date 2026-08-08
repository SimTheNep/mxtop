#pragma once

#include "../midi_reader/types.hpp"
#include "../midi_reader/midi_load.hpp"
#include "../json_parser/parser.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct channelState {
    std::unordered_map<std::string, int> rawValues;

    bool rhythmFromSysEx = false;
    bool rhythmFromBank  = false;

    bool isRhythm() const {
        return rhythmFromSysEx || rhythmFromBank;
    }

    struct patchState {
        int msb = 0;
        int lsb = 0;
        int program = 0;

        std::unordered_map<std::string, int> values;
    };
    std::unordered_map<std::string, patchState> patches;

    std::unordered_map<int, int> activeNotes;
    int lastNote = -1;
    int lastVelo = 0;
};

struct dataBind {
    std::vector<std::optional<uint8_t>> pattern;
    const ModuleObject* obj = nullptr;
};

struct takeSnapshot {
    std::unordered_map<std::string, std::string> values;
    std::unordered_map<std::string, int> rawValues;
    std::unordered_map<std::string, std::optional<std::string>> patchNames;
    int polyCount = 0;
    int lastNote = -1;
    int lastVelo = 0;
    bool isRhythm = false;
    std::vector<int> activeNotes;
};

class stateLayer {
public:
    explicit stateLayer(const moduleDef& module, const dictionaryDef& dictionary, MidiReader& reader);

    void eventHandler(const RawEvent& ev);
    void advance(double elapsedMs);

    const channelState* getChannel(int channel) const;
    std::vector<int> activeCh() const;

    std::optional<std::string> effLookup(const ModuleObject& obj, int value) const;
    std::optional<std::string> patchLookup(bool isRhythm, const channelState::patchState& patch, int* resolvedLsb = nullptr) const;
    std::optional<std::string> bankLookup(bool isRhythm, int msb, int lsb) const;

    std::string mathVal(const ModuleObject& obj, int raw) const;
    std::optional<std::string> finalVal(int channel, const std::string& objectId) const;

    takeSnapshot snapshot(int channel) const;

private:
    enum class patchField { Msb, Lsb, Program };
    const ModuleObject* defaultPatchObject_ = nullptr;

    struct sysexBinding {
        std::vector<uint8_t> addr;
        const ModuleObject* obj = nullptr;
        int channel = -1;
    };

    struct patchSysexBinding {
        std::vector<uint8_t> addr;
        const ModuleObject* obj = nullptr;
        patchField field;
        int channel = 0;
    };

    struct patchBinding {
        const ModuleObject* obj = nullptr;
        bool hasMsb = false;
        bool hasLsb = false;
        bool hasProgram = false;
    };

    bool dataHandler(const RawEvent& ev, channelState& sysCh);
    
    void handleSysEx(const RawEvent& ev, channelState& ch);

    void updtPC(channelState& ch, int channel, int program);
    void updtBank(channelState& ch, int channel, int ccNum, int value);
    void updtNote(channelState& ch, int note, int velocity, bool on);

    void storeValue(channelState& ch, const std::vector<const ModuleObject*>& targets, int value);
    void initCh(channelState& ch, int channel);
    void setMFX(channelState& targetCh, int outputAssign, int mfxSelect);

    uint32_t toAddrNum(const std::vector<uint8_t>& addr);
    std::string hexAddr(uint32_t n);
    std::vector<uint8_t> hexToBytes(const std::string& hex);

    std::vector<uint8_t> getRolandPart(const std::vector<uint8_t>& baseAddr, int channel);
    bool isRolandPart(const std::vector<uint8_t>& addr);

    channelState::patchState defaultPatch(const ModuleObject& obj);
    int initOffset(const ModuleObject& obj, int channel);
    void setPatch(channelState& targetCh, int channel, const ModuleObject& obj, patchField field, int value);

    channelState::patchState* activePatch(channelState& ch);
    const channelState::patchState* activePatch(const channelState& ch) const;

    const moduleDef& module_;
    const dictionaryDef& dictionary_;
    MidiReader& reader_;

    size_t headerLen_;
    size_t addrWidth_;

    std::array<std::vector<const ModuleObject*>, 128> ccIndex_{}; 
    std::vector<const ModuleObject*> pitchBendIndex_;

    std::vector<sysexBinding> sysexIndex_;
    std::vector<patchSysexBinding> patchSysexIndex_;
    std::unordered_map<std::string, const ModuleObject*> objectById_;

    std::vector<dataBind> dataIndex_;
    std::unordered_map<std::string, std::unordered_map<int, std::string>> enumLookup_;
    std::vector<patchBinding> patchIndex_;
    std::unordered_map<int, channelState> channels_;
};
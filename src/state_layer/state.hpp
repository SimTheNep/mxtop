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

struct channelState {
    std::unordered_map<std::string, int> rawValues; // Object id outputs last raw value
};

class stateLayer {
public:
    explicit stateLayer(const moduleDef& module);
    void eventHandler(const RawEvent& ev);
    void handleSysEx(const RawEvent& ev, channelState& ch); // Fixed syntax & added missing ';'

    const channelState* getChannel(int channel) const;

private:
    void storeValue(channelState& ch, const std::vector<const ModuleObject*>& targets, int value);

    const moduleDef& module_;

    std::array<std::vector<const ModuleObject*>, 128> ccIndex_{}; // Zero-initialized array
    std::vector<const ModuleObject*> pitchBendIndex_;
    std::vector<std::pair<std::vector<uint8_t>, const ModuleObject*>> sysexIndex_;

    std::unordered_map<int, channelState> channels_;
};
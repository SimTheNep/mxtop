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
    std::unordered_map<std::string, int> rawValues; // object id outputs last raw value
};

class stateLayer {
public:
    stateLayer(const moduleDef& module);
    void eventHandler(const RawEvent& ev);

    const channelState* getChannel(int channel) const;

private:
    void storeValue(channelState& ch, const std::vector<const ModuleObject*>& targets, int value);

    const moduleDef& module_;

    std::array<std::vector<const ModuleObject*>, 128> ccIndex_; // CC values
    std::vector<const ModuleObject*> pitchBendIndex_; // Pitch bend Values

    std::unordered_map<int, channelState> channels_; // Channel count
};
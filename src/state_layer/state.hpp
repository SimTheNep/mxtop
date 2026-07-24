#pragma once

#include "../midi_reader/types.hpp"
#include "../json_parser/parser.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

class stateLayer {
public:
    stateLayer(const moduleDef& module);
    void eventHandler(const RawEvent& ev);

private:
    const moduleDef& module_;
    std::array<std::vector<const ModuleObject*>, 128> ccIndex_;
};
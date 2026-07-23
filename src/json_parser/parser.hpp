#pragma once
#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>

struct LayoutVar {
    std::map<std::string, std::vector<std::string>> views; // Actual MIDI data to display
    std::map<std::string, std::vector<std::string>> widgets; // System info, logs, etc...
};

struct LayoutsDef {
    std::map<std::string, LayoutVar> variants; // "Full", "Compact", "Tiny"
};

LayoutsDef parseLayouts(const nlohmann::json& j);
void debugPrintLayouts(const LayoutsDef& layouts);
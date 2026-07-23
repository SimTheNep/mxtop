#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

// From types.hpp
enum class kind
{
    Patch,
    CC,
    PitchBend,
    SysEx
};

// LAYOUTS
// These do not pass through the state layer, they go straight to the UI
//
// 


struct layoutSect
{
    std::vector<std::string> items;
};

struct layoutVary
{
    std::unordered_map<std::string, layoutSect> views;
    std::unordered_map<std::string, layoutSect> widgets;
};

struct layoutDef
{
    std::unordered_map<std::string, layoutVary> variants;
};

layoutDef parseLayouts(const nlohmann::json& j);
void debugLayouts(const layoutDef& layouts);
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include <optional>
#include <cstdint>

using json = nlohmann::json;

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

struct layoutType
{
    std::unordered_map<std::string, layoutSect> views; // Stuff that shows in the channel view
    std::unordered_map<std::string, layoutSect> widgets; // Everything else
};

struct layoutDef
{
    std::unordered_map<std::string, layoutType> variants;
};

layoutDef parseLayouts(const nlohmann::json& j);
void debugLayouts(const layoutDef& layouts);

// MODULES
//
//
//

struct display // This dictates the 0-127 offset
{
    int offset = 0;
    std::string transform;
};

struct ModuleObject
{
    std::string id;

    kind type;
    display displayOffset;

    // CC
    std::optional<uint8_t> cc;

    // Roland SysEx
    std::optional<std::string> address;

    // Generic SysEx
    std::optional<std::string> data;

    // Dictionary lookup (where SysEx addresses go pick their data)
    std::optional<std::string> lookup;

    // For tuning in Hz, nibbles encoding in this case
    std::optional<int> bytes;
    std::optional<std::string> encoding; 

};

struct moduleDef // What actuall defines what module it is
{
    std::string id;
    std::string name;

    uint8_t manufacturer = 0;
    uint8_t model = 0;
    uint8_t deviceId = 0;

    std::string checksum;
    std::string packet;

    std::vector<ModuleObject> objects;
};

moduleDef parseModule(const json& j);
void debugModule(const moduleDef& module);

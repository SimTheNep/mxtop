#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include <optional>
#include <cstdint>

using json = nlohmann::json;



// KIND DEFINITIONS
//
//

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
    std::unordered_map<std::string, layoutSect> views;   // Stuff that shows in the channel view
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



struct patchSysexPart { // One address-set per Part (A, B, ...)
    int channel = 0;
    std::optional<std::string> msb;
    std::optional<std::string> lsb;
    std::optional<std::string> program;
};



struct sysexPart { // Required for per-part SysEx
    int channel = 0;
    std::string address;
};



struct ModuleObject
{
    std::string id;

    kind type;
    display displayOffset;

    bool perPatch = false;

    // CC
    std::optional<uint8_t> cc;

    // Roland SysEx
    std::optional<std::string> address;

    // Patch selection sequence
    std::vector<std::string> sequence;

    // Patch selection fields
    std::unordered_map<std::string, int> fields;

    // Bank selection MSB
    std::optional<std::string> bankSelectMsbLabel;

    // Bank selection LSB
    std::optional<std::string> bankSelectLsbLabel;

    // Generic SysEx
    std::optional<std::string> data;

    // Dictionary lookup (where SysEx addresses go pick their data)
    std::optional<std::string> lookup;

    // For tuning in Hz, nibbles encoding in this case
    std::optional<int> bytes;
    std::optional<std::string> encoding; 

    // SysEx patch assignment
    std::vector<patchSysexPart> patchSysexParts;

    // Per part SysEx
    std::vector<sysexPart> parts;

    // Default value override
    std::optional<int> defaultValue;
};



struct moduleDef // What actually defines what module it is
{
    std::string id;
    std::string name;

    uint8_t manufacturer = 0;
    uint8_t model = 0;
    uint8_t deviceId = 0;

    std::optional<std::string> checksum;
    std::string packet;

    std::vector<ModuleObject> objects;
};



moduleDef parseModule(const json& j);
void debugModule(const moduleDef& module);



// DICTIONARY
//
//

struct enumValue // Effect value + name + short name
{
    int id = 0;
    std::string name;

    std::optional<std::string> shortName;
};



struct enumDef // Effect definition
{
    std::vector<enumValue> values;
};



struct bankSelec // Bank selector
{
    std::optional<int> bankMSB;
    std::optional<int> bankLSB;
};



struct patchSelec // Patch selector
{
    int program = 0;

    std::optional<int> bankMSB;
    std::optional<int> bankLSB;

    std::string name;
};



struct patchBank {
    std::string id;
    std::string name;
    bankSelec bank;
    std::vector<patchSelec> items;
};



struct dictionaryDef { // Full dictionary
    std::unordered_map<std::string, enumDef> enums;
    std::unordered_map<std::string, std::vector<patchBank>> bankGroups;
};



dictionaryDef parseDictionary(const json& j);
void debugDictionary(const dictionaryDef& dictionary);
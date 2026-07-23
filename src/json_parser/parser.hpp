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




// Dictionary
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

// patchBank and drumBank are used as an alternative way to write bank MSBs so you don't have to put it in every single program
// I did this for the SD-90 banks because the presets were too many per banks
// I didn't do it for GS and XGlite because I scraped the tables from the manuals and they have those numbers already there

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

// OLD LOGIC, DO NOT MIND

// struct patchBank
// {
//     std::string id;
//     std::string name;

//     bankSelec bank;

//     std::vector<patchSelec> patches;
// };

// struct drumBank
// {
//     std::string id;
//     std::string name;

//     bankSelec bank;

//     std::vector<patchSelec> drumKits;
// };

// struct dictionaryDef // Full dictionary
// {
//     std::unordered_map<std::string, enumDef> enums;

//     std::vector<patchBank> patches;

//     std::vector<drumBank> drumKits;
// };

dictionaryDef parseDictionary(const json& j);
void debugDictionary(const dictionaryDef& dictionary);
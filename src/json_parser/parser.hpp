#pragma once

#include "midi_reader/midi_load.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

// KIND DEFINITIONS
enum class kind {
    Patch,
    CC,
    PitchBend,
    SysEx
};

// LAYOUTS

struct layoutColumn {
    std::vector<std::string> fields;
    std::string label;
    std::string join = " / ";

    int width = 0;
};

struct layoutSect {
    std::vector<layoutColumn> columns;
};

struct layoutGeometry {
    // Minimum recommended terminal dimensions.
    int minWidth = 80;
    int minHeight = 20;

    // Number of channels displayed per page.
    int pageSize = 16;

    // Number of channel columns/groups displayed simultaneously.
    int channelColumns = 1;
};

struct layoutType {
    layoutGeometry geometry;

    std::unordered_map<std::string, layoutSect> views;   // Channel view items
    std::unordered_map<std::string, std::vector<std::string>> widgets; // Widget view items
};

struct layoutDef {
    int version = 1;

    std::unordered_map<std::string, layoutType> variants;
};

layoutDef parseLayouts(const nlohmann::json& j);
void debugLayouts(const layoutDef& layouts);

// MODULES
struct display {
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

struct ModuleObject {
    std::string id;

    kind type;
    display displayOffset;

    bool perPatch = false;
    bool controlsRhythm = false;

    // Rhythm channel
    std::optional<int> rhythmChannel;

    // Drum bank MSB
    std::optional<std::vector<int>> drumBankMsb;

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

    // Dictionary lookup
    std::optional<std::string> lookup;

    // Nibbles encoding / byte count
    std::optional<int> bytes;
    std::optional<std::string> encoding; 

    // SysEx patch assignment
    std::vector<patchSysexPart> patchSysexParts;

    // Per part SysEx
    std::vector<sysexPart> parts;

    // Default value override
    std::optional<int> defaultValue;

    // Default per-part values
    std::unordered_map<int, int> partDefaults;
};

struct detectDef {
    std::string kind;
    std::vector<std::string> patterns;
};

struct moduleDef {
    std::string id;
    std::string name;

    int headerLen = 6;
    int addressWidth = 4;

    uint8_t manufacturer = 0;
    uint8_t model = 0;
    uint8_t deviceId = 0;

    std::optional<std::string> checksum;
    std::string packet;

    std::vector<ModuleObject> objects;
    std::optional<detectDef> detect;
    std::optional<MidiMode> mode;
};

moduleDef parseModule(const json& j);
void debugModule(const moduleDef& module);

// DICTIONARY
struct enumValue {
    int id = 0;
    std::string name;
    std::optional<std::string> shortName;
};

struct enumDef {
    std::vector<enumValue> values;
};

struct bankSelec {
    std::optional<int> bankMSB;
    std::optional<int> bankLSB;
};

struct patchSelec {
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

struct dictionaryDef {
    std::unordered_map<std::string, enumDef> enums;
    std::unordered_map<std::string, std::vector<patchBank>> bankGroups;
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> efxFlags;
};

dictionaryDef parseDictionary(const json& j);
void debugDictionary(const dictionaryDef& dictionary);
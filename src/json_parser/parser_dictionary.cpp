#include "parser.hpp"
#include "../log.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

// PARSER_DICTIONARY.CPP
//
// Parses dictionary definitions

using json = nlohmann::json;

// Parses enums and individual values (Base-10 default with 0x Hex support)
static enumDef parseEnum(const json& j) {
    enumDef def;

    for (const auto& value : j.at("values")) {
        enumValue entry;

        if (value.contains("id")) {
            if (value["id"].is_string()) {
                std::string s = value["id"].get<std::string>();
                try {
                    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) {
                        entry.id = std::stoi(s, nullptr, 16);
                    } else {
                        entry.id = std::stoi(s, nullptr, 10);
                    }
                } catch (...) {
                    entry.id = 0;
                }
            } else {
                entry.id = value["id"].get<int>();
            }

            entry.name = value.at("name").get<std::string>();

            if (value.contains("short"))
                entry.shortName = value["short"].get<std::string>();
            else if (value.contains("short_name"))
                entry.shortName = value["short_name"].get<std::string>();

        } else if (value.is_object()) {
            // Key-value object mapping e.g. {"0": "MFX", "1": "BYP", "13": "PAT"}
            for (const auto& [idStr, nameVal] : value.items()) {
                enumValue simple;
                try {
                    if (idStr.rfind("0x", 0) == 0 || idStr.rfind("0X", 0) == 0) {
                        simple.id = std::stoi(idStr, nullptr, 16);
                    } else {
                        simple.id = std::stoi(idStr, nullptr, 10);
                    }
                } catch (...) {
                    simple.id = 0;
                }
                simple.name = nameVal.get<std::string>();
                def.values.push_back(simple);
            }
            continue;
        }

        def.values.push_back(entry);
    }

    return def;
}

// Parses bank selection MSB and LSB selectors
static bankSelec parseSelector(const json& j) {
    bankSelec bank;

    if (j.contains("bank_msb"))
        bank.bankMSB = j["bank_msb"].get<int>();

    if (j.contains("bank_lsb"))
        bank.bankLSB = j["bank_lsb"].get<int>();

    return bank;
}

// Parses patch selectors and their patch names
static patchSelec parsePatchSelec(const json& j) {
    patchSelec patch;

    patch.program = j.at("program").get<int>();
    patch.name = j.at("name").get<std::string>();

    if (j.contains("bank_msb"))
        patch.bankMSB = j["bank_msb"].get<int>();

    if (j.contains("bank_lsb"))
        patch.bankLSB = j["bank_lsb"].get<int>();

    return patch;
}

// Parses grouped patch bank structures and selectors
static patchBank parsePatchBank(const json& j) {
    patchBank programBank;

    programBank.id   = j.at("id").get<std::string>();
    programBank.name = j.at("name").get<std::string>();

    if (j.contains("selector"))
        programBank.bank = parseSelector(j["selector"]);

    for (const auto& [key, val] : j.items()) {
        if (key == "id" || key == "name" || key == "selector")
            continue;

        if (val.is_array()) {
            for (const auto& item : val) {
                programBank.items.push_back(parsePatchSelec(item));
            }
            break;
        }
    }

    return programBank;
}

// Parses dictionary definitions (enums, bank groups, and effects)
dictionaryDef parseDictionary(const json& j) {
    dictionaryDef def;

    for (const auto& [key, value] : j.items()) {
        if (value.is_object() && value.contains("type") && value["type"] == "enum") {
            def.enums[key] = parseEnum(value);
        }
        else if (value.is_array()) {
            std::vector<patchBank> bankList;
            for (const auto& bankJson : value) {
                bankList.push_back(parsePatchBank(bankJson));
            }
            def.bankGroups[key] = std::move(bankList);
        }
        else if (value.is_object()) {
            std::unordered_map<std::string, bool> flags;
            bool allBool = true;
            for (const auto& [flagKey, flagVal] : value.items()) {
                if (!flagVal.is_boolean()) { allBool = false; break; }
                flags[flagKey] = flagVal.get<bool>();
            }
            if (allBool && !flags.empty())
                def.efxFlags[key] = flags;
        }
    }

    logDbg("[parser_dictionary] Parsed %zu enum(s), %zu bank group(s), %zu efx flag set(s)",
        def.enums.size(), def.bankGroups.size(), def.efxFlags.size());

    return def;
}
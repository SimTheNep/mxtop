#include "parser.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>

using json = nlohmann::json;



// LAYOUTS.JSON
//
//

static std::unordered_map<std::string, layoutSect> parseGroups(const json& j) {
    std::unordered_map<std::string, layoutSect> out;

    for (const auto& [key, value] : j.items()) {
        out[key] = layoutSect{
            value.get<std::vector<std::string>>()
        };
    }

    return out;
}



layoutDef parseLayouts(const json& j) {
    layoutDef def;

    for (const auto& [variantName, variantJson] : j.items()) {
        layoutType variant;

        if (variantJson.contains("views"))   { variant.views   = parseGroups(variantJson.at("views")); }
        if (variantJson.contains("widgets")) { variant.widgets = parseGroups(variantJson.at("widgets")); } // Only Full has widgets

        def.variants[variantName] = std::move(variant);
    }

    return def;
}



// MODULE.JSON
//
//

static kind parseKind(const std::string& s) {
    if (s == "cc")        return kind::CC;
    if (s == "patch")     return kind::Patch;
    if (s == "pitchbend") return kind::PitchBend;
    if (s == "sysex")     return kind::SysEx;

    throw std::runtime_error("Unknown kind: " + s);
}



moduleDef parseModule(const json& j) {
    moduleDef def;

    if (j.contains("header_length"))  def.headerLen     = j["header_length"].get<int>();
    if (j.contains("address_width"))  def.addressWidth  = j["address_width"].get<int>();

    def.id = j.at("id").get<std::string>();
    def.name = j.at("name").get<std::string>();

    if (j.contains("manufacturer"))
        def.manufacturer = j["manufacturer"].get<uint8_t>();

    if (j.contains("model"))
        def.model = j["model"].get<uint8_t>();

    if (j.contains("device_id"))
        def.deviceId = j["device_id"].get<uint8_t>();

    if (j.contains("checksum"))
        def.checksum = j["checksum"].get<std::string>();

    if (j.contains("packet"))
        def.packet = j["packet"].get<std::string>();

    for (const auto& objJson : j.at("objects")) {
        ModuleObject obj;

        obj.id = objJson.at("id").get<std::string>();
        obj.type = parseKind(objJson.at("kind").get<std::string>());
        obj.perPatch = objJson.value("per_patch", false);
        obj.controlsRhythm = objJson.value("controls_rhythm", false);

        if (objJson.contains("drum_msb")) {
            auto drumJson = objJson["drum_msb"];
            
            // Array support [104, 105, 106, 107]
            if (drumJson.is_array()) {
                obj.drumBankMsb = drumJson.get<std::vector<int>>();
            } 
            // Scalable Range support {"start": 104, "end": 107}
            else if (drumJson.is_object() && drumJson.contains("start") && drumJson.contains("end")) {
                std::vector<int> msbs;
                int start = drumJson["start"].get<int>();
                int end = drumJson["end"].get<int>();
                for (int i = start; i <= end; ++i) {
                    msbs.push_back(i);
                }
                obj.drumBankMsb = msbs;
            }
        }

        if (objJson.contains("display")) {
            auto d = objJson["display"];

            if (d.contains("offset"))
                obj.displayOffset.offset = d["offset"].get<int>();

            if (d.contains("transform"))
                obj.displayOffset.transform = d["transform"].get<std::string>();
        }

        if (objJson.contains("cc"))
            obj.cc = objJson["cc"].get<uint8_t>();

        if (objJson.contains("address"))
            obj.address = objJson["address"].get<std::string>();

        if (objJson.contains("data"))
            obj.data = objJson["data"].get<std::string>();

        if (objJson.contains("lookup"))
            obj.lookup = objJson["lookup"].get<std::string>();

        if (objJson.contains("sequence"))
            obj.sequence = objJson["sequence"].get<std::vector<std::string>>();

        if (objJson.contains("fields")) {
            for (const auto& [fieldName, fieldValue] : objJson["fields"].items())
                obj.fields[fieldName] = fieldValue.get<int>();
        }

        if (objJson.contains("bank_select")) {
            auto bs = objJson["bank_select"];

            if (bs.contains("msb"))
                obj.bankSelectMsbLabel = bs["msb"].get<std::string>();

            if (bs.contains("lsb"))
                obj.bankSelectLsbLabel = bs["lsb"].get<std::string>();

            if (obj.bankSelectMsbLabel && obj.bankSelectLsbLabel &&
                *obj.bankSelectMsbLabel == *obj.bankSelectLsbLabel)
            {
                fprintf(
                    stderr,
                    "[parser] warning: object \"%s\" has bank_select msb and lsb both set to \"%s\"\n",
                    obj.id.c_str(),
                    obj.bankSelectMsbLabel->c_str()
                );
            }
        }

        if (objJson.contains("bytes"))
            obj.bytes = objJson["bytes"].get<int>();

        if (objJson.contains("encoding"))
            obj.encoding = objJson["encoding"].get<std::string>();

        // Default init value override
        if (objJson.contains("default"))
            obj.defaultValue = objJson["default"].get<int>();

        // Per-patch SysEx
        if (objJson.contains("parts") && obj.type == kind::SysEx) {
            auto partsJson = objJson["parts"];
            
            // Array support
            if (partsJson.is_array()) {
                for (const auto& partJson : partsJson) {
                    sysexPart part;
                    part.channel = partJson.value("channel", 0);
                    part.address = partJson.at("address").get<std::string>();
                    obj.parts.push_back(part);
                    
                    printf("PART %2d -> %s\n",
                        part.channel,
                        part.address.c_str());
                }
            } 
            // Scalable formula support
            else if (partsJson.is_object()) {
                std::string baseStr = partsJson.value("base", "");
                std::string strideStr = partsJson.value("stride", "");
                auto channels = partsJson.value("channels", std::vector<int>{});

                if (!baseStr.empty() && !strideStr.empty()) {
                    uint32_t baseVal = std::stoul(baseStr, nullptr, 16);
                    uint32_t strideVal = std::stoul(strideStr, nullptr, 16);
                    
                    int width = baseStr.length(); 
                    std::string fmt = "%0" + std::to_string(width) + "X";

                    for (size_t i = 0; i < channels.size(); ++i) {
                        sysexPart part;
                        part.channel = channels[i];
                        uint32_t addr = baseVal + (i * strideVal);
                        
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), fmt.c_str(), addr);
                        part.address = std::string(buf);
                        
                        obj.parts.push_back(part);
                    }
                }
            }
        }

        // SysEx patch assignment
        if (objJson.contains("sysex") && obj.type == kind::Patch) {
            auto sysexJson = objJson["sysex"];
            
            if (sysexJson.is_array()) {
                for (const auto& partJson : sysexJson) {
                    patchSysexPart part;
                    part.channel = partJson.value("channel", 0);

                    if (partJson.contains("msb"))     part.msb     = partJson["msb"].get<std::string>();
                    if (partJson.contains("lsb"))     part.lsb     = partJson["lsb"].get<std::string>();
                    if (partJson.contains("program")) part.program = partJson["program"].get<std::string>();

                    obj.patchSysexParts.push_back(part);
                }
            } else if (sysexJson.is_object()) {
                patchSysexPart part;
                part.channel = 0;

                std::string baseStr = sysexJson.value("base", "");
                if (!baseStr.empty()) {
                    uint32_t baseVal = std::stoul(baseStr, nullptr, 16);
                    char buf[16];

                    std::snprintf(buf, sizeof(buf), "%08X", baseVal);
                    part.msb = std::string(buf);

                    std::snprintf(buf, sizeof(buf), "%08X", baseVal + 1);
                    part.lsb = std::string(buf);

                    std::snprintf(buf, sizeof(buf), "%08X", baseVal + 2);
                    part.program = std::string(buf);
                }
                
                obj.patchSysexParts.push_back(part);
            }
        }

        def.objects.push_back(obj);
    }

    return def;
}



// DICTIONARY.JSON
//
//

static enumDef parseEnum(const json& j) {
    enumDef def;

    for (const auto& value : j.at("values")) {
        enumValue entry;

        if (value.contains("id")) {
            if (value["id"].is_string())
                entry.id = std::stoi(value["id"].get<std::string>());
            else
                entry.id = value["id"].get<int>();

            entry.name = value.at("name").get<std::string>();

            if (value.contains("short"))
                entry.shortName = value["short"].get<std::string>();
            else if (value.contains("short_name"))
                entry.shortName = value["short_name"].get<std::string>();

        } else {
            for (const auto& [id, name] : value.items()) {
                enumValue simple;
                simple.id   = std::stoi(id);
                simple.name = name.get<std::string>();
                def.values.push_back(simple);
            }
            continue;
        }

        def.values.push_back(entry);
    }

    return def;
}



static bankSelec parseSelector(const json& j) {
    bankSelec bank;

    if (j.contains("bank_msb"))
        bank.bankMSB = j["bank_msb"].get<int>();

    if (j.contains("bank_lsb"))
        bank.bankLSB = j["bank_lsb"].get<int>();

    return bank;
}



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
    }

    return def;
}



// DEBUG
//
//

void debugLayouts(const layoutDef& layouts) {
    for (const auto& [variantName, variant] : layouts.variants) {
        printf("Display: %s\n", variantName.c_str());

        for (const auto& [sectionName, section] : variant.views) {
            printf("    Parameter: %s\n", sectionName.c_str());
            for (const auto& id : section.items) { printf("        %s\n", id.c_str()); }
        }

        for (const auto& [sectionName, section] : variant.widgets) {
            printf("    Widget: %s\n", sectionName.c_str());
            for (const auto& id : section.items) { printf("        %s\n", id.c_str()); }
        }
    }
}



void debugModule(const moduleDef& module) {
    printf("Module: %s\n", module.name.c_str());
    printf("ID: %s\n", module.id.c_str());

    printf("Manufacturer: %u\n", module.manufacturer);
    printf("Model: %u\n", module.model);
    printf("Device ID: %u\n", module.deviceId);

    printf("Checksum: %s\n", module.checksum ? module.checksum->c_str() : "<none>");
    printf("Packet: %s\n", module.packet.c_str());

    printf("Objects:\n");

    for (const auto& obj : module.objects) {
        printf("    Object: %s\n", obj.id.c_str());

        printf("        Type: ");
        switch (obj.type) {
            case kind::Patch:      printf("Patch\n"); break;
            case kind::CC:         printf("CC\n"); break;
            case kind::PitchBend:  printf("PitchBend\n"); break;
            case kind::SysEx:      printf("SysEx\n"); break;
        }

        if (obj.displayOffset.offset != 0)
            printf("        Display offset: %d\n", obj.displayOffset.offset);

        if (!obj.displayOffset.transform.empty())
            printf("        Transform: %s\n", obj.displayOffset.transform.c_str());

        if (obj.cc)
            printf("        CC: %u\n", *obj.cc);

        if (obj.address)
            printf("        Address: %s\n", obj.address->c_str());

        if (obj.data)
            printf("        Data: %s\n", obj.data->c_str());

        if (obj.lookup)
            printf("        Lookup: %s\n", obj.lookup->c_str());

        if (obj.bytes)
            printf("        Bytes: %d\n", *obj.bytes);

        if (obj.encoding)
            printf("        Encoding: %s\n", obj.encoding->c_str());

        if (!obj.sequence.empty()) {
            printf("        Sequence: ");
            for (const auto& step : obj.sequence) printf("%s ", step.c_str());
            printf("\n");
        }

        if (!obj.fields.empty()) {
            printf("        Fields: ");
            for (const auto& [fieldName, fieldValue] : obj.fields)
                printf("%s=%d ", fieldName.c_str(), fieldValue);
            printf("\n");
        }

        if (obj.bankSelectMsbLabel)
            printf("        Bank Select MSB means: %s\n", obj.bankSelectMsbLabel->c_str());

        if (obj.bankSelectLsbLabel)
            printf("        Bank Select LSB means: %s\n", obj.bankSelectLsbLabel->c_str());
    }
}



void debugDictionary(const dictionaryDef& dictionary) {
    printf("ENUMS:\n");

    for (const auto& [id, def] : dictionary.enums) {
        printf("    %s\n", id.c_str());

        for (const auto& value : def.values) {
            printf("        %d: %s",
                value.id,
                value.name.c_str());

            if (value.shortName)
                printf(" (%s)", value.shortName->c_str());

            printf("\n");
        }
    }

    printf("\nBANK GROUPS:\n");

    for (const auto& [groupName, bankList] : dictionary.bankGroups) {
        printf("    Group: [%s]\n", groupName.c_str());

        for (const auto& bank : bankList) {
            printf("        Bank: %s - %s\n", bank.id.c_str(), bank.name.c_str());

            if (bank.bank.bankMSB) printf("        MSB: %d\n", *bank.bank.bankMSB);
            if (bank.bank.bankLSB) printf("        LSB: %d\n", *bank.bank.bankLSB);

            for (const auto& item : bank.items) {
                printf("            PC: %d", item.program);
                if (item.bankMSB) printf(" MSB:%d", *item.bankMSB);
                if (item.bankLSB) printf(" LSB:%d", *item.bankLSB);
                printf(" - %s\n", item.name.c_str());
            }
        }
    }
}

uint8_t calculateRolandChecksum(const uint8_t* data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return static_cast<uint8_t>(127 - (sum % 128));
}
#include "parser.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "../log.hpp"

// PARSER_MODULE.CPP
//
// Parses module definitions

using json = nlohmann::json;

// Converts string representations into their object kind enums
static kind parseKind(const std::string& s) {
    if (s == "cc")        return kind::CC;
    if (s == "patch")     return kind::Patch;
    if (s == "pitchbend") return kind::PitchBend;
    if (s == "sysex")     return kind::SysEx;

    throw std::runtime_error("Unknown kind: " + s);
}

// Parses module specifications and parameter objects
moduleDef parseModule(const json& j) {
    moduleDef def;

    if (j.contains("header_length"))  def.headerLen    = j["header_length"].get<int>();
    if (j.contains("address_width"))  def.addressWidth = j["address_width"].get<int>();

    def.id   = j.at("id").get<std::string>();
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

    if (j.contains("detect")) {
        detectDef d;
        d.kind = j["detect"].value("kind", "");

        if (j["detect"].contains("data")) {
            auto dataJson = j["detect"]["data"];
            if (dataJson.is_array()) {
                for (const auto& pat : dataJson) {
                    d.patterns.push_back(pat.get<std::string>());
                }
            } else if (dataJson.is_string()) {
                d.patterns.push_back(dataJson.get<std::string>());
            }
        }

        if (!d.kind.empty() && !d.patterns.empty())
            def.detect = d;
    }

    if (j.contains("mode")) {
        std::string mode = j["mode"].get<std::string>();

        if (mode == "GS")
            def.mode = MidiMode::GS;
        else if (mode == "XG")
            def.mode = MidiMode::XG;
        else if (mode == "Native")
            def.mode = MidiMode::Native;
        else if (mode == "GM2")
            def.mode = MidiMode::GM2;
    }

    for (const auto& objJson : j.at("objects")) {
        ModuleObject obj;

        obj.id             = objJson.at("id").get<std::string>();
        obj.type           = parseKind(objJson.at("kind").get<std::string>());
        obj.perPatch       = objJson.value("per_patch", false);
        obj.controlsRhythm = objJson.value("controls_rhythm", false);

        if (objJson.contains("rhythm_channel"))
            obj.rhythmChannel = objJson["rhythm_channel"].get<int>();

        if (objJson.contains("drum_msb")) {
            auto drumJson = objJson["drum_msb"];
            
            if (drumJson.is_array()) {
                obj.drumBankMsb = drumJson.get<std::vector<int>>();
            } 
            else if (drumJson.is_object() && drumJson.contains("start") && drumJson.contains("end")) {
                std::vector<int> msbs;
                int start = drumJson["start"].get<int>();
                int end   = drumJson["end"].get<int>();
                for (int i = start; i <= end; ++i) {
                    msbs.push_back(i);
                }
                obj.drumBankMsb = msbs;
            }
        }

        if (objJson.contains("part_defaults")) {
            for (const auto& [part, value] : objJson["part_defaults"].items()) {
                obj.partDefaults[std::stoi(part)] = value.get<int>();
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
                std::fprintf(
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

        if (objJson.contains("default"))
            obj.defaultValue = objJson["default"].get<int>();

        // Per-patch SysEx
        if (objJson.contains("parts") && obj.type == kind::SysEx) {
            auto partsJson = objJson["parts"];
            
            if (partsJson.is_array()) {
                for (const auto& partJson : partsJson) {
                    sysexPart part;
                    part.channel = partJson.value("channel", 0);
                    part.address = partJson.at("address").get<std::string>();
                    obj.parts.push_back(part);
                }
            } 
            else if (partsJson.is_object()) {
                std::string baseStr   = partsJson.value("base", "");
                std::string strideStr = partsJson.value("stride", "");
                auto channels         = partsJson.value("channels", std::vector<int>{});

                if (!baseStr.empty() && !strideStr.empty()) {
                    uint32_t baseVal   = std::stoul(baseStr, nullptr, 16);
                    uint32_t strideVal = std::stoul(strideStr, nullptr, 16);
                    
                    int width = static_cast<int>(baseStr.length()); 
                    std::string fmt = "%0" + std::to_string(width) + "X";

                    for (size_t i = 0; i < channels.size(); ++i) {
                        sysexPart part;
                        part.channel = channels[i];
                        uint32_t addr = baseVal + static_cast<uint32_t>(i * strideVal);
                        
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
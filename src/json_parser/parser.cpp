#include "parser.hpp"
#include <cstdio>
#include <fstream>

using json = nlohmann::json;

static std::unordered_map<std::string, layoutSect>

parseGroups(const json& j)
{
    std::unordered_map<std::string, layoutSect> out;

    for (const auto& [key, value] : j.items())
    {
        out[key] = layoutSect{
            value.get<std::vector<std::string>>()
        };
    }

    return out;
}

// LAYOUTS.JSON
// 
//

layoutDef parseLayouts(const json& j)
{
    layoutDef def;

    for (const auto& [variantName, variantJson] : j.items())
    {
        layoutType variant;

        if (variantJson.contains("views")){variant.views = parseGroups(variantJson.at("views"));}
        if (variantJson.contains("widgets")){variant.widgets = parseGroups(variantJson.at("widgets"));} // Only Full has widgets

        def.variants[variantName] = std::move(variant);
    }

    return def;
}

// MODULE.JSON
//
//

static kind parseKind(const std::string& s)
{
    if (s == "cc")         return kind::CC;
    if (s == "patch")      return kind::Patch;
    if (s == "pitchbend")  return kind::PitchBend;
    if (s == "sysex")      return kind::SysEx;

    throw std::runtime_error("Unknown kind: " + s);
}

moduleDef parseModule(const json& j)
{
    moduleDef def;

    def.id = j.at("id").get<std::string>();
    def.name = j.at("name").get<std::string>();

    // THe fact I have to use ifs for everything pmo
    if (j.contains("manufacturer"))
        def.manufacturer = j["manufacturer"].get<uint8_t>();

    if (j.contains("model"))
        def.model = j["model"].get<uint8_t>();

    if (j.contains("deviceId"))
        def.deviceId = j["deviceId"].get<uint8_t>();

    if (j.contains("checksum"))
        def.checksum = j["checksum"].get<std::string>();

    if (j.contains("packet"))
        def.packet = j["packet"].get<std::string>();




    for (const auto& objJson : j.at("objects"))
    {
        ModuleObject obj;

        obj.id = objJson.at("id").get<std::string>();
        obj.type = parseKind(objJson.at("kind").get<std::string>());

        if (objJson.contains("display"))
        {
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

        if (objJson.contains("bytes"))
            obj.bytes = objJson["bytes"].get<int>();

        if (objJson.contains("encoding"))
            obj.encoding = objJson["encoding"].get<std::string>();

        def.objects.push_back(obj);
    }

    return def;
}

// DEBUG
//
//

void debugLayouts(const layoutDef& layouts)
{
    for (const auto& [variantName, variant] : layouts.variants)
    {
    printf("Display: %s\n", variantName.c_str());

        for (const auto& [sectionName, section] : variant.views)
        {
            printf("    Parameter: %s\n", sectionName.c_str());
            for (const auto& id : section.items) { printf("        %s\n", id.c_str()); }
        }

        for (const auto& [sectionName, section] : variant.widgets)
        {
            printf("    Widget: %s\n", sectionName.c_str());
            for (const auto& id : section.items) { printf("        %s\n", id.c_str()); }
        }
    }
}

void debugModule(const moduleDef& module)
{
    printf("Module: %s\n", module.name.c_str());
    printf("ID: %s\n", module.id.c_str());

    printf("Manufacturer: %u\n", module.manufacturer);
    printf("Model: %u\n", module.model);
    printf("Device ID: %u\n", module.deviceId);

    printf("Checksum: %s\n", module.checksum.c_str());
    printf("Packet: %s\n", module.packet.c_str());

    printf("Objects:\n");

    for (const auto& obj : module.objects)
    {
        printf("    Object: %s\n", obj.id.c_str());

        printf("        Type: ");
        switch (obj.type)
        {
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

    }
}
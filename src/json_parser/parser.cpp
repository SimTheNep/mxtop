#include "parser.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>

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


// Expect less comments, a lot of the code here is very legible even if you don't know much


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

// Main parser
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

    if (j.contains("device_id"))
        def.deviceId = j["device_id"].get<uint8_t>();

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

        // patch sequence and its default values
        if (objJson.contains("sequence"))
            obj.sequence = objJson["sequence"].get<std::vector<std::string>>();

        if (objJson.contains("fields"))
        {
            for (const auto& [fieldName, fieldValue] : objJson["fields"].items())
                obj.fields[fieldName] = fieldValue.get<int>();
        }

        // What MSB/LSB actually mean on this device (varies per module)
        if (objJson.contains("bank_select"))
        {
            auto bs = objJson["bank_select"];

            if (bs.contains("msb"))
                obj.bankSelectMsbLabel = bs["msb"].get<std::string>();

            if (bs.contains("lsb"))
                obj.bankSelectLsbLabel = bs["lsb"].get<std::string>();

            // msb/lsb can be "bank" or "variation" (or vice-versa), but never the same
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

        def.objects.push_back(obj);
    }

    return def;
}




// DICTIONARY.JSON
//
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


// This selects the bank 
static bankSelec parseSelector(const json& j)
{
    bankSelec bank;

    if (j.contains("bank_msb"))
        bank.bankMSB = j["bank_msb"].get<int>();

    if (j.contains("bank_lsb"))
        bank.bankLSB = j["bank_lsb"].get<int>();

    return bank;
}

static patchSelec parsePatchSelec(const json& j)
{
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

    // This used to be hard coded to Drums and Patches on my first attempt, now it's dynamic
    // Find the array that holds the patches
    //
    // for (const auto& patch : j.at("patches"))
    // {
    //     programBank.patches.push_back(
    //         parsePatchSelec(patch)
    //     );
    // }
    
    for (const auto& [key, val] : j.items()) {
        // Skip metadata keys
        if (key == "id" || key == "name" || key == "selector")
            continue;

        // If it's an array, assume it contains the patch items
        if (val.is_array()) {
            for (const auto& item : val) {
                programBank.items.push_back(parsePatchSelec(item));
            }
            break;
        }
    }

    return programBank;
}

// OLD LOGIC, DO NOT MIND

// // Same as parsePatchSelec, left here just in case we need it in the future
// static patchSelec parseDrumSelec(const json& j)
// {
//     patchSelec kit;

//     kit.program = j.at("program").get<int>();
//     kit.name = j.at("name").get<std::string>();

//     if (j.contains("bank_msb"))
//         kit.bankMSB = j["bank_msb"].get<int>();

//     if (j.contains("bank_lsb"))
//         kit.bankLSB = j["bank_lsb"].get<int>();

//     return kit;
// }

// static patchBank parsePatchBank(const json& j)
// {
//     patchBank programBank;

// //    printf("Parsing patch bank...\n"); // Istg
// //    std::cout << j.dump(2) << "\n";

//     programBank.id = j.at("id").get<std::string>();
//     programBank.name = j.at("name").get<std::string>();

//     if (j.contains("selector"))
//         programBank.bank = parseSelector(j["selector"]);


//     for (const auto& patch : j.at("patches"))
//     {
//         programBank.patches.push_back(
//             parsePatchSelec(patch)
//         );
//     }

//     return programBank;
// }

// static drumBank parseDrumBank(const json& j)
// {
//     drumBank programBank;

// //    printf("Parsing drum bank...\n"); // Istg
// //    std::cout << j.dump(2) << "\n";

//     programBank.id = j.at("id").get<std::string>();
//     programBank.name = j.at("name").get<std::string>();

//     if (j.contains("selector"))
//         programBank.bank = parseSelector(j["selector"]);


//     for (const auto& kit : j.at("drum_kits"))
//     {
//         programBank.drumKits.push_back(
//             parseDrumSelec(kit)
//         );
//     }

//     return programBank;
// }


// Main parser
dictionaryDef parseDictionary(const json& j) {
//   printf("ENTERED PARSE DICTIONARY\n"); // Needed for bug on test run, still compiled somehow

    dictionaryDef def;

    for (const auto& [key, value] : j.items()) {
        // Parse Enum objects

        if (value.is_object() && value.contains("type") && value["type"] == "enum") {
            def.enums[key] = parseEnum(value);
        }
        
        // Parse any array as a bank group

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

        if (!obj.sequence.empty())
        {
            printf("        Sequence: ");
            for (const auto& step : obj.sequence) printf("%s ", step.c_str());
            printf("\n");
        }

        if (!obj.fields.empty())
        {
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

void debugDictionary(const dictionaryDef& dictionary)
{
    printf("ENUMS:\n");

    for (const auto& [id, def] : dictionary.enums)
    {
        printf("    %s\n", id.c_str());

        for (const auto& value : def.values)
        {
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
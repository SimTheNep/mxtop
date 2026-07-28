#include "parser.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>

// PARSER_DEBUG.CPP
//
// Debug utilis

using json = nlohmann::json;

// Prints layout variants, views, and widget contents
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

// Prints module metadata, identifiers, and object parameters
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

        if (obj.rhythmChannel)
            printf("        Rhythm channel: %d\n", *obj.rhythmChannel);

        if (obj.drumBankMsb) {
            printf("        Drum bank MSB: ");
            for (int m : *obj.drumBankMsb) printf("%d ", m);
            printf("\n");
        }
    }
}

// Prints dictionary enums, effect flags, and bank groups
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

    printf("EFX FLAGS:\n");
    for (const auto& [groupName, flags] : dictionary.efxFlags) {
        printf("    %s:\n", groupName.c_str());
        for (const auto& [flagName, val] : flags)
            printf("        %s = %s\n", flagName.c_str(), val ? "true" : "false");
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

// Roland SysEx checksum
uint8_t calculateRolandChecksum(const uint8_t* data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return static_cast<uint8_t>(127 - (sum % 128));
}
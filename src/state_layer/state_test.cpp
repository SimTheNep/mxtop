// Standalone debug tool
// Plays MIDI file(s) through a module file and prints the proper display channel state every 50ms
// Used to be part of the main in state.cpp but was moved here when it was merged with the rest of the code

#include "state.hpp"
#include "json_parser/parser.hpp"
#include "midi_reader/midi_load.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: %s <module_folder> <file1.mid> [file2.mid ...]\n", argv[0]);
        return 1;
    }

    std::filesystem::path moduleDir = argv[1];
    std::vector<std::string> files(argv + 2, argv + argc);

    using json = nlohmann::json;

    std::ifstream moduleFile(moduleDir / "module.json");
    if (!moduleFile.is_open()) {
        printf("Couldn't open: %s\n", (moduleDir / "module.json").c_str());
        return 1;
    }
    json moduleJson;
    moduleFile >> moduleJson;
    moduleDef module = parseModule(moduleJson);

    std::ifstream dictFile(moduleDir / "dictionary.json");
    if (!dictFile.is_open()) {
        printf("Couldn't open: %s\n", (moduleDir / "dictionary.json").c_str());
        return 1;
    }
    json dictJson;
    dictFile >> dictJson;
    dictionaryDef dictionary = parseDictionary(dictJson);

    MidiReader reader;
    reader.dataInit(files);

    stateLayer state(module, dictionary, reader);

    auto playbackStart = std::chrono::steady_clock::now();

    // Channels to print, abides to channel offset and number 1...16...32...
    const int numChannels = static_cast<int>(reader.sourceCount()) * reader.midChannels();

    double lastPrintMs = 0.0;
    constexpr double printIntervalMs = 50.0; // Print every 50ms

    while (reader.hasMoreEvents()) {
        double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - playbackStart).count();

        state.advance(elapsedMs); // Pulls through eventHandler

        if (elapsedMs - lastPrintMs >= printIntervalMs) {
            lastPrintMs = elapsedMs;

            printf("t=%.0fms\n", elapsedMs);

            // channel -1 as SysEx channel
            {
                auto snap = state.snapshot(-1);
                if (!snap.values.empty()) {
                    printf("sys:");
                    for (const auto& [id, display] : snap.values)
                        printf(" %s=%s", id.c_str(), display.c_str());
                    printf("\n");
                }
            }

            for (int ch = 0; ch < numChannels; ++ch) {
                auto snap = state.snapshot(ch);
                if (snap.values.empty() && snap.polyCount == 0 && snap.patchNames.empty()) continue;

                printf("ch %d:", ch);
                for (const auto& [id, display] : snap.values)
                    printf(" %s=%s", id.c_str(), display.c_str());
                for (const auto& [id, name] : snap.patchNames)
                    printf(" %s=%s", id.c_str(), name ? name->c_str() : "(unresolved)");
                if (snap.polyCount > 0)
                    printf(" poly=%d lastVel=%d", snap.polyCount, snap.lastVelo);
                printf("\n");
            }
        }
    }
}

// OLD MAIN
//
// int main(){
//     std::filesystem::path moduleDir = "../../modules/sd-90";
//
//     using json = nlohmann::json;
//
//     std::ifstream moduleFile(moduleDir / "module.json");
//     if (!moduleFile.is_open()) {
//         printf("Couldn't open: %s\n", (moduleDir / "module.json").c_str());
//         return 1;
//     }
//     json moduleJson;
//     moduleFile >> moduleJson;
//     moduleDef module = parseModule(moduleJson);
//
//     std::ifstream dictFile(moduleDir / "dictionary.json");
//     if (!dictFile.is_open()) {
//         printf("Couldn't open: %s\n", (moduleDir / "dictionary.json").c_str());
//         return 1;
//     }
//     json dictJson;
//     dictFile >> dictJson;
//     dictionaryDef dictionary = parseDictionary(dictJson);
//
//     // Fake SysEx: master tuning
//     RawEvent ev;
//     ev.kind = MsgKind::SysEx;
//     ev.channel = -1;
//     ev.data = {
//         0xF0, module.manufacturer, module.deviceId, 0x00, module.model, 0x12,
//         0x01, 0x00, 0x00, 0x00,   // Address
//         0x00, 0x04, 0x00, 0x00,   // Value (4 nibbles = 0x0400)
//         0x7B,                      // Checksum
//         0xF7
//     };
//
//     // Pan test object
//     ModuleObject panObj;
//     panObj.id = "pan";
//     panObj.type = kind::CC;
//     panObj.cc = 10;
//     panObj.displayOffset.transform = "pan_lcr";
//
//     // Bipolar test object
//     ModuleObject bipolarObj;
//     bipolarObj.id = "bipolar_test";
//     bipolarObj.type = kind::CC;
//     bipolarObj.cc = 20;
//     bipolarObj.displayOffset.offset = -64; // Expect 36
//
//     module.objects.push_back(panObj);
//     module.objects.push_back(bipolarObj);
//
//     MidiReader reader; // Unused in testing but constructor needs it
//     stateLayer state(module, dictionary, reader); // Built after every object exists
//
//     RawEvent panEv;
//     panEv.kind = MsgKind::CC;
//     panEv.channel = 0;
//     panEv.data = { 0xB0, 10, 20 };
//     state.eventHandler(panEv);
//
//     RawEvent bipolarEv;
//     bipolarEv.kind = MsgKind::CC;
//     bipolarEv.channel = 0;
//     bipolarEv.data = { 0xB0, 20, 100 };
//     state.eventHandler(bipolarEv);
//
//     RawEvent noteOn;
//     noteOn.kind = MsgKind::NoteOn;
//     noteOn.channel = 0;
//     noteOn.velocity = 100;
//     noteOn.data = { 0x90, 60, 100 };
//     state.eventHandler(noteOn);
//
//     RawEvent noteOn2;
//     noteOn2.kind = MsgKind::NoteOn;
//     noteOn2.channel = 0;
//     noteOn2.velocity = 80;
//     noteOn2.data = { 0x90, 64, 80 };
//     state.eventHandler(noteOn2);
//
//     // Snapshot ch0
//     {
//         auto snap = state.snapshot(0);
//         for (const auto& [id, display] : snap.values)
//             printf("  %s = %s\n", id.c_str(), display.c_str());
//         printf("  poly = %d lastNote = %d lastVel = %d\n", snap.polyCount, snap.lastNote, snap.lastVelo);
//     }
//
//     RawEvent noteOff;
//     noteOff.kind = MsgKind::NoteOff;
//     noteOff.channel = 0;
//     noteOff.data = { 0x80, 60, 0 };
//     state.eventHandler(noteOff);
//
//     // Snapshot channel 0 to see poly change
//     {
//         auto snap = state.snapshot(0);
//         for (const auto& [id, display] : snap.values)
//             printf("  %s = %s\n", id.c_str(), display.c_str());
//         printf("  poly after note off = %d\n", snap.polyCount);
//     }
//
//     state.eventHandler(ev);
//
//     // Snapshot sysex
//     {
//         auto snap = state.snapshot(-1);
//         for (const auto& [id, display] : snap.values)
//             printf("  %s = %s\n", id.c_str(), display.c_str());
//     }
// }
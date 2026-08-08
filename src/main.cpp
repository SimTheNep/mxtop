#include "midi_reader/midi_load.hpp"
#include "midi_reader/types.hpp"
#include "tui/debug/debug_ui.hpp"
#include "tui/ui_main.hpp"
#include "tui/menu/menu_ui.hpp"
#include "json_parser/parser.hpp"
#include "state_layer/state.hpp"
#include "log.hpp"

#include <CLI/CLI.hpp>
#include <RtMidi.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <unordered_set>

// VARS
std::vector<unsigned int> ports;
std::vector<std::string> inptFiles;
std::vector<std::string> portNames; // Needed for debug UI, port names

std::vector<std::unique_ptr<RtMidiOut>> outs; // Different pointers are assigned to different instances of RtMidiOut

double tCount = 0; // Ms

std::atomic<bool> running(true); // THIS IS CRUCIAL! WITHOUT THIS SYNTHS WILL NOT STOP PLAYING SOUND (tested on Linux so far)

// FUNCTS
void handleSignal(int) { running = false; }

// Just prints every output port RtMidi can see, nothing fancy:
// [0] Port 1
// [1] Port 2
// [2] Port 3
void printPrt() {
    RtMidiOut probe;
    unsigned int n = probe.getPortCount();

    if (n == 0) std::printf("no MIDI output ports found\n");
    else for (unsigned int i = 0; i < n; ++i) std::printf("[%u] %s\n", i, probe.getPortName(i).c_str());
}

// --debug asks this once
bool statePrompt() {
    std::printf("Debug view - [r]eader data or [s]tate layer data? ");
    std::fflush(stdout);

    std::string choice;
    std::cin >> choice;

    return !choice.empty() && (choice[0] == 's' || choice[0] == 'S');
}

// Loads module
nlohmann::json loadJsonFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("couldn't open " + path);

    nlohmann::json j;
    f >> j;
    return j;
}

int main(int argc, char** argv) {
    // Temporary menu render
    menuUi menu({"Port 0", "Port 1"});
    menu.run();
    return 0;

    CLI::App app{ "mxtop MIDI Visualizer" };

    // ARGUMENTS
    bool debug = false;
    bool listOuts = false;
    std::string moduleFolder;

    app.add_option("-f,--file", inptFiles, "MIDI file(s) to play, one per port")->required(false);
    app.add_option("-p,--port", ports, "Output port per file, same argument order as --file")->required(false);
    app.add_flag("--debug", debug, "Print the MIDI reader script's direct event table instead of the GUI");
    app.add_flag("--list-outptPorts", listOuts, "List available MIDI output outptPorts");
    app.add_option("-m,--module", moduleFolder, "Module folder (module.json/dictionary.json/layouts.json)")->required(false);

    CLI11_PARSE(app, argc, argv); // argument call
    
    logDbg("[main] CLI parsed: %zu file(s), %zu port(s)", inptFiles.size(), ports.size());
    if (!moduleFolder.empty()) {
        logDbg("[main] User specified module override folder: %s", moduleFolder.c_str());
    }

    // Print list outptPorts... simple
    if (listOuts) {
        printPrt();
        return 0;
    }

    if (inptFiles.empty() || ports.empty()) {
        std::fprintf(stderr, "need at least one --file and one --port\n");
        return 1;
    }

    if (inptFiles.size() > 1 && inptFiles.size() != ports.size()) {
        std::fprintf(stderr,
            "when using multiple files, the number of --port values must match the number of --file values\n");
        return 1;
    }

    // MIDI port opening
    for (size_t i = 0; i < ports.size(); ++i) {

        auto out = std::make_unique<RtMidiOut>();

        if (debug) {
            if (inptFiles.size() == 1) {
                std::fprintf(
                    stderr,
                    "[main] opening port %u for broadcast...\n",
                    ports[i]
                );
            } else {
                std::fprintf(
                    stderr,
                    "[main] opening port %u for file '%s'...\n",
                    ports[i],
                    inptFiles[i].c_str()
                );
            }
        }

        ports[i] >= out->getPortCount() ? (std::fprintf(stderr, "Port [%u] is invalid.\n", ports[i]), exit(1)) : (void)0;

        out->openPort(ports[i]);
        portNames.push_back(out->getPortName(ports[i]));
        outs.push_back(std::move(out));

        logDbg("[main] Opened output port [%u]: %s", ports[i], portNames.back().c_str());
    }

    // DEBUG MODE CHOICE
    bool useStateLayer = false;
    moduleDef module;
    dictionaryDef dictionary;
    layoutDef layouts;

    // MIDI reader invoc
    MidiReader reader;

    // Register module paths to scan
    std::vector<std::string> knownModulePaths = {
        "../modules/gs",
        "../modules/xg",
        "../modules/sd-90",
        "../modules/gm2"
    };

    // Automatically load detection rules directly mapped to their module directory path
    for (const auto& path : knownModulePaths) {
        try {
            auto j = loadJsonFile(path + "/module.json");
            auto mod = parseModule(j);

            if (mod.detect && mod.detect->kind == "sysex") {
                for (const auto& pattern : mod.detect->patterns) {
                    reader.addDetectionRule({ path, pattern });
                }
            }
        } catch (...) {
            // Silently skip unreadable paths
        }
    }

    reader.dataInit(inptFiles, outs.size());

    if (debug) {
        useStateLayer = statePrompt();
    } else {
        useStateLayer = true; // Always enable state layer for TUI visualizer
    }

    if (useStateLayer) {
        if (moduleFolder.empty()) {
            auto folder = reader.detectedModuleFolder();

            if (folder) {
                moduleFolder = *folder;
                logDbg("[main] auto-selected module: " + moduleFolder);
            } else {
                moduleFolder = "../modules/sd-90"; 
                logDbg("[main] auto-selected module: " + moduleFolder);
            }
        }

        try {
            module = parseModule(loadJsonFile(moduleFolder + "/module.json"));
            dictionary = parseDictionary(loadJsonFile(moduleFolder + "/dictionary.json"));
            layouts = parseLayouts(loadJsonFile(moduleFolder + "/layouts.json"));

            logDbg("[main] Successfully loaded module '%s' (%s) from '%s'",
                module.name.c_str(), module.id.c_str(), moduleFolder.c_str());
        }
        catch (const std::exception& e) {
            std::fprintf(stderr,
                "failed to load module folder \"%s\": %s\n",
                moduleFolder.c_str(),
                e.what());
            return 1;
        }
    }

    if (debug) {
        std::printf("Loading %zu file(s)...\n", inptFiles.size());
    }

    // State mode translator
    std::unique_ptr<stateLayer> state;
    if (useStateLayer) {
        state = std::make_unique<stateLayer>(module, dictionary, reader);
    }

    // Register AFTER loading
    std::signal(SIGINT, handleSignal);

    // TUI or Debug UI invoc
    std::unique_ptr<MidiUi> ui;
    std::unique_ptr<DebugUi> debugUi;

    if (!debug) {
        ui = std::make_unique<MidiUi>(layouts, module, reader.metadata());
        ui->start();
    } else {
        debugUi = std::make_unique<DebugUi>(inptFiles, portNames);
    }

    auto initTime = std::chrono::steady_clock::now();
    std::vector<RawEvent> dataDump;
    auto lastUiFrame = std::chrono::steady_clock::now();
    const auto uiFrameInterval = std::chrono::microseconds(16667); // ~60 fps

// PLAYBACK LOOP

    logDbg("[main] Starting playback loop...");

    while (running && reader.hasMoreEvents()) {

        tCount = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - initTime).count(); // Time count from playback

        dataDump.clear();

        if (reader.backlog(tCount, dataDump) && running) {

            std::unordered_set<int> touchedChannels;
            bool sawStateLayer = false;
            bool sawSysEx = false;

            for (const auto& ev : dataDump) {

                // Route data to output ports regardless of debug flag
                if (!ev.data.empty()) {
                    for (int port : ev.sourcePorts) {
                        if (port >= 0 && static_cast<size_t>(port) < outs.size()) {
                            outs[port]->sendMessage(&ev.data);
                        }
                    }
                }

                if (useStateLayer) {
                    sawStateLayer = true;
                    state->eventHandler(ev);

                    if (ui) {
                        ui->addEvent(ev);
                    }

                    if (ev.kind == MsgKind::SysEx) {
                        sawSysEx = true;
                    } else if (ev.channel >= 0) {
                        touchedChannels.insert(ev.channel);
                    }
                }

                if (debugUi && !useStateLayer) {
                    debugUi->addEvent(ev);
                }
            }

            if (useStateLayer && sawStateLayer) {
                auto now = std::chrono::steady_clock::now();

                if (now - lastUiFrame >= uiFrameInterval) {
                    lastUiFrame = now;

                    if (ui) {
                        ui->addSnap(-1, state->snapshot(-1), tCount);

                        // Always push snapshots for all channels so VU meters and note decays update every frame
                        for (int ch : state->activeCh()) {
                            if (ch >= 0) {
                                ui->addSnap(ch, state->snapshot(ch), tCount);
                            }
                        }
                    }

                    if (debugUi) {
                        debugUi->addSnap(-1, state->snapshot(-1), tCount);
                        for (int ch : state->activeCh()) {
                            debugUi->addSnap(ch, state->snapshot(ch), tCount);
                        }
                    }
                }
            }

        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } // playback loop end

    if (ui) {
        ui->stop();
    }

    logDbg("[main] Playback ended at t=%.2f ms. Cleaning up...", tCount);

    // PANIC (Cleanup)
    for (size_t i = 0; i < outs.size(); ++i) {
        if (outs[i] && outs[i]->isPortOpen()) {
            for (int ch = 0; ch < 16; ++ch) {
                unsigned char stByte = static_cast<unsigned char>(0xB0 | ch); // 0xB0 = CC Message

                std::vector<unsigned char> notesOff = { stByte, 123, 0 }; // CC 123 = "all notes off"
                std::vector<unsigned char> sndOff = { stByte, 120, 0 }; // CC 120 = "all sound off"

                outs[i]->sendMessage(&notesOff);
                outs[i]->sendMessage(&sndOff);
            }
        }
    }

    return 0;
}
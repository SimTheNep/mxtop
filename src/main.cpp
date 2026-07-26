#include "midi_reader/midi_load.hpp"
#include "midi_reader/types.hpp"
#include "tui/debug/debug_ui.hpp"
#include "json_parser/parser.hpp"
#include "state_layer/state.hpp"

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
    CLI::App app{ "mxtop MIDI Visualizer" };

    // ARGUMENTS
    bool debug = false;
    bool listOuts = false;
    std::string moduleFolder;

    app.add_option("-f,--file", inptFiles, "MIDI file(s) to play, one per port")->required(false);
    app.add_option("-p,--port", ports, "Output port per file, same argument order as --file")->required(false);
    app.add_flag("--debug", debug, "Print the MIDI reader script's direct event table instead of the GUI");
    app.add_flag("--list-outptPorts", listOuts, "List available MIDI output outptPorts");
    app.add_option("-m,--module", moduleFolder, "Module folder (module.json/dictionary.json) - only needed for state layer debug view")->required(false);

    CLI11_PARSE(app, argc, argv); // argument call

    // Print list outptPorts... simple
    if (listOuts) {
        printPrt();
        return 0;
    }

    if (inptFiles.empty() || ports.empty() || inptFiles.size() != ports.size()) {
        std::fprintf(stderr, "need at least one --file and one --port, and counts must match (see --help)\n");
        return 1;
    }

    // DEBUG MODE CHOICE
    bool useStateLayer = false;
    moduleDef module;
    dictionaryDef dictionary;

    if (debug) {
        useStateLayer = statePrompt();

        if (useStateLayer) {
            if (moduleFolder.empty()) {
                std::fprintf(stderr, "state layer debug view needs --module <folder>\n");
                return 1;
            }

            try {
                module = parseModule(loadJsonFile(moduleFolder + "/module.json"));
                dictionary = parseDictionary(loadJsonFile(moduleFolder + "/dictionary.json"));
            } catch (const std::exception& e) {
                std::fprintf(stderr, "failed to load module folder \"%s\": %s\n", moduleFolder.c_str(), e.what());
                return 1;
            }
        }
    }

    // MIDI port opening
    for (size_t i = 0; i < ports.size(); ++i) {

        auto out = std::make_unique<RtMidiOut>();

        if (debug) {
            std::fprintf(
                stderr,
                "[main] opening port %u for file '%s'...\n",
                ports[i],
                inptFiles[i].c_str()
            );
        }

        ports[i] >= out->getPortCount() ? (std::fprintf(stderr, "Port [%u] is invalid.\n", ports[i]), exit(1)) : (void)0;

        out->openPort(ports[i]);
        portNames.push_back(out->getPortName(ports[i]));
        outs.push_back(std::move(out));
    }

    // MIDI reader invoc
    MidiReader reader;

    if (debug) {
        std::printf("Loading %zu file(s)...\n", inptFiles.size());
    }
    
    reader.dataInit(inptFiles);

    // Only built in state mode (translates messages to module/dictionary json data)
    std::unique_ptr<stateLayer> state;
    if (useStateLayer) {
        state = std::make_unique<stateLayer>(module, dictionary, reader);
    }

    // Register AFTER loading
    std::signal(SIGINT, handleSignal);

    // Debug UI invoc
    MidiUi ui(inptFiles, portNames);

    auto initTime = std::chrono::steady_clock::now();
    std::vector<RawEvent> dataDump;

    // PLAYBACK LOOP
    while (running && reader.hasMoreEvents()) {

        tCount = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - initTime).count(); // Time count from playback

        dataDump.clear();

        if (reader.backlog(tCount, dataDump) && running) {

            for (const auto& ev : dataDump) {

                // Route data to output ports regardless of debug flag
                if (!ev.data.empty() && ev.sourcePort >= 0 && static_cast<size_t>(ev.sourcePort) < outs.size()) {
                    outs[ev.sourcePort]->sendMessage(&ev.data);
                }

                // Only display debug UI output on --debug
                if (useStateLayer) {
                    state->eventHandler(ev);

                    if (ev.kind == MsgKind::SysEx) {
                        // A SysEx message can update any channel's per-part data this way... now if you'll excuse me...
                        // FUCK YOU THIS TOOK SO LONG TO FIX BECAUSE I KEPT LOOKING AT STATE.CPP BECAUSE I COULD SWEAR IT WAS THERE AHHHHHH
                        ui.addSnap(-1, state->snapshot(-1), tCount);
                        for (int ch : state->activeCh())
                            ui.addSnap(ch, state->snapshot(ch), tCount);
                    } else {
                        ui.addSnap(ev.channel, state->snapshot(ev.channel), tCount);
                    }
                }
            }

        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } // playback loop end

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
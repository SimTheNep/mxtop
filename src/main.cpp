#include "midi_reader/midi_load.hpp"
#include "midi_reader/types.hpp"
#include "tui/debug/debug_ui.hpp"

#include <CLI/CLI.hpp>
#include <RtMidi.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// VARS
//
//
//

std::vector<unsigned int> ports;


std::vector<std::string> inptFiles;
std::vector<std::string> portNames; // Needed for debug UI, port names

std::vector<std::unique_ptr<RtMidiOut>> outs; // Different pointers are assigned to different instances of RtMidiOut


double tCount = 0; // Ms

std::atomic<bool> running(true); // THIS IS CRUCIAL! WITHOUT THIS SYNTHS WILL NOT STOP PLAYING SOUND (tested on Linux so far)

// FUNCTS
//
//

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


int main(int argc, char** argv) {
    CLI::App app{ "mxtop MIDI Visualizer" };

    // ARGUMENTS
    //
    // -f and -p can both be repeated, order matters between them (-f 1 2 -> -p 1 2)
    //
    //

    bool debug = false;
    bool listOuts = false;

    app.add_option("-f,--file", inptFiles, "MIDI file(s) to play, one per port")->required(false);
    app.add_option("-p,--port", ports, "Output port per file, same argument order as --file")->required(false);
    app.add_flag("--debug", debug, "Print the MDI reader script's direct event table instead of the GUI");
    app.add_flag("--list-outptPorts", listOuts, "List available MIDI output outptPorts");

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


    // MIDI port opening, could be moved to the loader maybe?
    for (size_t i = 0; i < ports.size(); ++i) {

        auto out = std::make_unique<RtMidiOut>();

        // The port opening is weird, I hope this doesn't fail again (watch me jinx it)
        std::fprintf(
            stderr,
            "[main] opening port %u for file '%s'...\n",
            ports[i],
            inptFiles[i].c_str()

        );

        ports[i] >= out->getPortCount() ? (std::fprintf(stderr, "Port [%u] is invalid.\n", ports[i]), exit(1)) : (void)0;

        out->openPort(ports[i]);
        portNames.push_back(out->getPortName(ports[i]));
        outs.push_back(std::move(out));

    }

    // MIDI reader invoc
    MidiReader reader;

    std::printf("Loading %zu file(s)...\n", inptFiles.size());
    reader.dataInit(inptFiles);


    // Register AFTER loading, you don't need to be looking for a Ctrl+C during init load
    std::signal(SIGINT, handleSignal);

    // Debug UI invoc
    // *Might need to make another header file for them, depends on if I need them for more important tasks
    //
    MidiUi ui(inptFiles, portNames);


    auto initTime = std::chrono::steady_clock::now();
    std::vector<RawEvent> dataDump;

    // PLAYBACK LOOP
    //
    // Don't touch this, it may cause lag and drift between inptFiles... for now it works nominally
    //
    while (running && reader.hasMoreEvents()) {

        tCount = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - initTime
        ).count(); // Time count from playback


        dataDump.clear();

        if (debug) {
            std::printf("[DEBUG] mode initialization...\n", tCount);
        }

        if (reader.backlog(tCount, dataDump) && running) {

            for (const auto& ev : dataDump) {

                // This is what routes the data to the outptPorts
                if (!ev.data.empty() && ev.sourcePort >= 0 && static_cast<size_t>(ev.sourcePort) < outs.size()) {
                    outs[ev.sourcePort]->sendMessage(&ev.data);
                }

                ui.addEvent(ev, debug);
            }

        } else {
            // This is enough tbh
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } // playback loop end




    // PANIC (Cleanup)
    //
    // Every port gets hard-silenced... Why? I don't know... even ALSA does this on Linux on my SD-90... haha 1-up on ALSA
    //

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
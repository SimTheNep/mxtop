#include "midi_load.hpp"
#include "MidiFile.h"
#include "../log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <sstream>

// MIDI_LOAD.CPP
//
// Main MIDI data loader and multi-midi/port/track sequencer

// The actual main MIDI data loader
void MidiReader::dataInit( const std::vector<std::string>& filenames, size_t outputCount, int midChannels ) {
    detectedModuleFolder_.reset();

    if (filenames.empty()) {
        std::printf("[midi_load] Loader expects at least one file\n");
        return;
    }

    close();
    sourceCount_ = filenames.size();
    midChannels_ = midChannels;

    // Load each file into its own temporary vector so we can combine their data without disturbing each event order.
    std::vector<std::vector<RawEvent>> tempFileLists(filenames.size());

    // Reads every file one at a time into its own list
    for (size_t fileIdx = 0; fileIdx < filenames.size(); ++fileIdx) {
        std::fprintf(
            stderr,
            "[midi_load] reading file %zu/%zu: %s...\n",
            fileIdx + 1,
            filenames.size(),
            filenames[fileIdx].c_str()
        );

        smf::MidiFile midifile;

        if (!midifile.read(filenames[fileIdx])) {
            logDbg("[midi_load] Failed to read MIDI file: " + filenames[fileIdx]);
        }

        midifile.doTimeAnalysis(); // Converts MIDI delta-ticks since last message into a timestamp

        const bool multiPortFile = (filenames.size() == 1 && outputCount > 1);

        // REMINDER! Most MIDIs only have 16 channels (0-15) so we offset every extra file into 16-31, 32-47, etc... 
        const int chOffset = static_cast<int>(fileIdx) * midChannels; 
        int skippedEvents = 0;

        if (multiPortFile) {
            const int trackCount = midifile.getTrackCount();
            std::vector<std::vector<RawEvent>> tempTrackLists(trackCount);
            std::vector<int> trackPorts(trackCount, -1);
            bool explicitPort = false;

            // Try real Port meta-events
            for (int track = 0; track < trackCount; ++track) {
                for (int i = 0; i < midifile[track].size(); ++i) {
                    auto& e = midifile[track][i];
                    if (e.isMeta() && e.getMetaType() == 0x21) {
                        auto content = e.getMetaContent();
                        if (!content.empty()) {
                            trackPorts[track] = static_cast<uint8_t>(content[0]) % static_cast<int>(outputCount);
                            explicitPort = true;
                        }
                        break;
                    }
                }
            }

            if (!explicitPort) {
                std::fprintf(stderr, "[midi_load] no Port meta-events found\n");

                int dataTrackIndex = 0;
                for (int track = 0; track < trackCount; ++track) {
                    bool hasChannelData = false;
                    for (int i = 0; i < midifile[track].size(); ++i) {
                        auto& e = midifile[track][i];
                        if (!e.isMeta() && e.size() > 0) {
                            uint8_t status = e[0];
                            if (status >= 0x80 && status <= 0xEF) { hasChannelData = true; break; }
                        }
                    }

                    if (!hasChannelData) {
                        trackPorts[track] = 0; // Meta/tempo-only track
                        continue;
                    }

                    trackPorts[track] = (dataTrackIndex / midChannels) % static_cast<int>(outputCount);
                    ++dataTrackIndex;
                }
            }

            for (int track = 0; track < trackCount; ++track) {
                int trackPort = trackPorts[track];
                logDbg("[midi_load] track %d -> port %d\n", track, trackPort);

                for (int i = 0; i < midifile[track].size(); ++i) {
                    auto& midiEvent = midifile[track][i];
                    if (midiEvent.isMeta()) continue;
                    if (midiEvent.size() == 0) continue;

                    RawEvent ev;
                    ev.kind = MsgKind::Unknown;
                    ev.channel = -1;
                    ev.velocity = 0;
                    ev.sourcePorts = { trackPort };
                    ev.timestamp = midiEvent.seconds * 1000.0;
                    ev.data.assign(midiEvent.begin(), midiEvent.end());

                    const uint8_t status = ev.data[0];

                    if (status == 0xF0 || status == 0xF7) {
                        ev.kind = MsgKind::SysEx;

                        if (!detectedModuleFolder_) {
                            if (auto folder = detectSysEx(ev)) {
                                detectedModuleFolder_ = *folder;
                            }
                        }

                        tempTrackLists[track].push_back(std::move(ev));
                        continue;
                    }

                    const uint8_t command = status & 0xF0;
                    ev.channel = (status & 0x0F) + trackPort * midChannels;

                    switch (command) {
                        case 0x80: if (ev.data.size() < 3) { skippedEvents++; continue; } ev.kind = MsgKind::NoteOff; break;
                        case 0x90:
                            if (ev.data.size() < 3) { skippedEvents++; continue; }
                            if (ev.data[2] == 0) { ev.kind = MsgKind::NoteOff; ev.velocity = 0; }
                            else { ev.kind = MsgKind::NoteOn; ev.velocity = ev.data[2]; }
                            break;
                        case 0xA0: if (ev.data.size() < 3) { skippedEvents++; continue; } ev.kind = MsgKind::PolyAftertouch; ev.velocity = ev.data[2]; break;
                        case 0xB0: if (ev.data.size() < 3) { skippedEvents++; continue; } ev.kind = MsgKind::CC; break;
                        case 0xC0: if (ev.data.size() < 2) { skippedEvents++; continue; } ev.kind = MsgKind::ProgramChange; break;
                        case 0xD0: if (ev.data.size() < 2) { skippedEvents++; continue; } ev.kind = MsgKind::ChannelAftertouch; break;
                        case 0xE0: if (ev.data.size() < 3) { skippedEvents++; continue; } ev.kind = MsgKind::PitchBend; break;
                        default: skippedEvents++; continue;
                    }

                    tempTrackLists[track].push_back(std::move(ev));
                }
            }

            // Merge all tracks by timestamp into this one file's bucket
            std::vector<size_t> tIndices(trackCount, 0);
            while (true) {
                int nextTrack = -1;
                double earliest = -1.0;
                for (int t = 0; t < trackCount; ++t) {
                    if (tIndices[t] < tempTrackLists[t].size()) {
                        double ts = tempTrackLists[t][tIndices[t]].timestamp;
                        if (nextTrack == -1 || ts < earliest) { earliest = ts; nextTrack = t; }
                    }
                }
                if (nextTrack == -1) break;

                for (int t = 0; t < trackCount; ++t) {
                    while (tIndices[t] < tempTrackLists[t].size() &&
                        tempTrackLists[t][tIndices[t]].timestamp == earliest) {
                        tempFileLists[fileIdx].push_back(std::move(tempTrackLists[t][tIndices[t]]));
                        tIndices[t]++;
                    }
                }
            }

        } else {
            midifile.joinTracks();
            const int track = 0;

            // Plays back data byte by byte
            for (int i = 0; i < midifile[track].size(); i++) {
                auto& midiEvent = midifile[track][i];

                // Meta events are ignored for now, will be used for the UI later on
                if (midiEvent.isMeta())
                    continue;

                if (midiEvent.size() == 0)
                    continue;

                RawEvent ev;
                ev.kind = MsgKind::Unknown;
                ev.channel = -1;
                ev.velocity = 0;
                // This branch only ever runs when !multiPortFile
                ev.sourcePorts.push_back(static_cast<int>(fileIdx));
                ev.timestamp = midiEvent.seconds * 1000.0;
                ev.data.assign(midiEvent.begin(), midiEvent.end());

                const uint8_t status = ev.data[0];

                if (status == 0xF0 || status == 0xF7) {
                    ev.kind = MsgKind::SysEx;
                    // Detect MIDI standard
                    if (!detectedModuleFolder_) {
                        if (auto folder = detectSysEx(ev)) {
                            detectedModuleFolder_ = *folder;
                        }
                    }

                    tempFileLists[fileIdx].push_back(std::move(ev));
                    continue;
                }

                const uint8_t command = status & 0xF0; // Keeps only the status byte
                ev.channel = (status & 0x0F) + chOffset; // Keeps only the channel byte

                // Every case checks ev.data.size for corruptions
                switch (command) {
                    case 0x80:
                        if (ev.data.size() < 3) { skippedEvents++; continue; }
                        ev.kind = MsgKind::NoteOff;
                        break;
                    case 0x90:
                        if (ev.data.size() < 3) { skippedEvents++; continue; }
                        if (ev.data[2] == 0) {
                            // Note On with velocity 0 is equal to Note Off
                            ev.kind = MsgKind::NoteOff;
                            ev.velocity = 0;
                        } else {
                            ev.kind = MsgKind::NoteOn;
                            ev.velocity = ev.data[2];
                        }
                        break;
                    case 0xA0:
                        if (ev.data.size() < 3) { skippedEvents++; continue; }
                        ev.kind = MsgKind::PolyAftertouch;
                        ev.velocity = ev.data[2]; // Often treated as velocity too
                        break;

                    case 0xB0:
                        if (ev.data.size() < 3) { skippedEvents++; continue; }
                        ev.kind = MsgKind::CC;
                        break;

                    case 0xC0:
                        if (ev.data.size() < 2) { skippedEvents++; continue; }
                        ev.kind = MsgKind::ProgramChange;
                        break;

                    case 0xD0:
                        if (ev.data.size() < 2) { skippedEvents++; continue; }
                        ev.kind = MsgKind::ChannelAftertouch;
                        break;

                    case 0xE0:
                        if (ev.data.size() < 3) { skippedEvents++; continue; }
                        ev.kind = MsgKind::PitchBend;
                        break;

                    default:
                        // Invalid status byte is skipped
                        skippedEvents++;
                        continue;
                }

                tempFileLists[fileIdx].push_back(std::move(ev));
            }
        }

        if (skippedEvents > 0) {
            std::fprintf(
                stderr,
                "[midi_load] warning: failed to read %d event(s) in %s\n",
                skippedEvents,
                filenames[fileIdx].c_str()
            );
        }
    }

    // FILE MERGE INTO STREAM
    //
    // A simple scan is fast enough since the number of files ports is not gonna be too big
    // Actual logic that forces the files to sync by comparing timestamps
    //

    std::vector<size_t> indices(filenames.size(), 0);
    size_t queuedFiles = 0;

    while (true) {
        int nextFile = -1;
        double earliestTime = -1.0;

        // Checks all files for the next event to define priority...
        for (size_t f = 0; f < tempFileLists.size(); ++f) {
            if (indices[f] < tempFileLists[f].size()) {
                double t = tempFileLists[f][indices[f]].timestamp;

                if (nextFile == -1 || t < earliestTime) {
                    earliestTime = t;
                    nextFile = static_cast<int>(f);
                }
            }
        }

        if (nextFile == -1)
            break; // every file fully consumed

        // Pull in all events across all files that share this exact timestamp (synchroooooonizeeeeed)
        for (size_t f = 0; f < tempFileLists.size(); ++f) {
            while (
                indices[f] < tempFileLists[f].size() &&
                tempFileLists[f][indices[f]].timestamp == earliestTime
            ) {
                // IMPORTANT! USE pushNoCap() AND NOT push(), CHECK THE COMMENT ON pushNoCap()
                pushNoCap(std::move(tempFileLists[f][indices[f]]));
                indices[f]++;
                queuedFiles++;
            }
        }
    }

    std::fprintf(stderr, "[midi_load] loaded %zu file(s), %zu total event(s) queued\n", filenames.size(), queuedFiles );
}
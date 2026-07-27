#include "midi_load.hpp"
#include "MidiFile.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <iostream>
#include <iomanip>

std::optional<MidiMode> MidiReader::midiStdSwitch(const RawEvent& ev) { // Basic MIDI SysEx parser for MIDI mode

    const auto& d = ev.data;

    // GM

    if (
        d.size() == 6 &&
        d[0] == 0xF0 &&
        d[1] == 0x7E &&
        d[3] == 0x09
    ) 
    {
        switch (d[4]) {

            // case 0x01:
            //     return MidiMode::GM1;

            case 0x02:
                return MidiMode::Native;

            case 0x03:
                return MidiMode::GM2;
        }
    }

    // GS reset

    if (
        d.size() >= 11 &&
        d[0] == 0xF0 &&
        d[1] == 0x41 &&
        d[3] == 0x42 &&
        d[4] == 0x12 &&
        d[5] == 0x40 &&
        d[6] == 0x00 &&
        d[7] == 0x7F &&
        d[8] == 0x00
    ) {
        return MidiMode::GS;
    }

    // XG

    if (
        d.size() >= 9 &&
        d[0] == 0xF0 &&
        d[1] == 0x43 &&
        d[2] == 0x10 &&
        d[3] == 0x4C &&
        d[4] == 0x00 &&
        d[5] == 0x00 &&
        d[6] == 0x7E &&
        d[7] == 0x00
    ) {
        return MidiMode::XG;
    }

    return std::nullopt;
}

// BASIC READER-SCOPE FUNCTIONS
//
//

MidiReader::~MidiReader() {
    close();
}

void MidiReader::close() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.clear();
}

bool MidiReader::forceFront(RawEvent& out) {
    std::lock_guard<std::mutex> lock(queueMutex_);

    if (queue_.empty())
        return false;

    out = std::move(queue_.front());
    queue_.pop_front();

    return true;
}

bool MidiReader::hasMoreEvents() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return !queue_.empty();
}

bool MidiReader::backlog(double elapsedMs, std::vector<RawEvent>& out) {
    std::lock_guard<std::mutex> lock(queueMutex_);

    bool any = false;

    // queue_ is always kept in timestamp order (dataInit merges messages happening at the same time before outputting)
    while (
        !queue_.empty() &&
        queue_.front().timestamp <= elapsedMs
    ) {
        out.push_back(std::move(queue_.front()));
        queue_.pop_front();
        any = true;
    }

    return any;
}

void MidiReader::push(RawEvent&& ev) {
    std::lock_guard<std::mutex> lock(queueMutex_);

    queue_.push_back(std::move(ev));


    // Cap the queue doesn't eat all the RAM in the eventuality of an infinite queue
    // Intentionally not applied to the initial bulk load, see pushNoCap() right below
    // 

    constexpr size_t kMaxQueue = 4096;

    while (queue_.size() > kMaxQueue) {
        queue_.pop_front();
    }
}

void MidiReader::pushNoCap(RawEvent&& ev) { // DIABOLICAL function name (purposeful shitpost... yet accurate)
    std::lock_guard<std::mutex> lock(queueMutex_);



    // No cap here on purpose, dataInit() already knows every event before this is even called
    //
    // *Using push() here doesn't work because the cap deletes the oldest timestamps  
    // *At some point this broke MIDI playback entirely because backlog() needs timestaps all the way from 0 to do the desync failsafe math
    // *Since the cap gets rid of already read events it just freezes playback since the full information isn't there on load
    queue_.push_back(std::move(ev));
}

// LESS BASIC READER-SCOPE FUNCTIONS
// 
// The actual main MIDI data loader
//
//

void MidiReader::dataInit( const std::vector<std::string>& filenames, size_t outputCount, int midChannels ) {

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
            std::printf("[midi_load] Failed to read MIDI file: %s\n", filenames[fileIdx].c_str());
            continue;
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
                std::fprintf(stderr, "[midi_load] track %d -> port %d\n", track, trackPort);

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

                        std::ostringstream ss;
                        for (uint8_t b : ev.data) {
                            ss << std::hex
                            << std::uppercase
                            << std::setw(2)
                            << std::setfill('0')
                            << (int)b << ' ';
                        }

                        std::fprintf(stderr, "[loader] %s\n", ss.str().c_str());

                        if (auto mode = midiStdSwitch(ev)) {
                            std::fprintf(stderr, "[midi_load] detected mode switch: %d\n", static_cast<int>(*mode));
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
                    if (auto mode = midiStdSwitch(ev)) {
                        std::fprintf(stderr, "[midi_load] detected mode switch: %d\n", static_cast<int>(*mode));
                        // TODO: send to state_layer
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
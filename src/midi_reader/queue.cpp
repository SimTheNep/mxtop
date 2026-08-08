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
#include <fstream>

// MIDI_READER_QUEUE.CPP
//
// MIDI mode switching and queue management

// MIDI SysEx parser for MIDI mode

bool MidiReader::sysexMatches(const std::vector<unsigned char>& midi, const std::string& pattern){
    std::stringstream ss(pattern);
    std::string byte;

    std::vector<unsigned char> expected;

    while (ss >> byte) {
        expected.push_back(
            static_cast<unsigned char>(
                std::stoul(byte, nullptr, 16)
            )
        );
    }

    return midi == expected;
}

std::optional<std::string> MidiReader::detectSysEx(const RawEvent& ev){
    if (ev.kind != MsgKind::SysEx)
        return std::nullopt;


    for (const auto& rule : detectRules_)
    {
        if (sysexMatches(ev.data, rule.data))
        {
            logDbg("[queue] SysEx detection match! Detected module folder: %s", rule.moduleFolder.c_str());
            return rule.moduleFolder;
        }
    }


    return std::nullopt;
}

void MidiReader::addDetectionRule(const DetectRule& rule){
    detectRules_.push_back(rule);
}

// Destroys the MidiReader
MidiReader::~MidiReader() {
    close();
}

// Clears and resets the internal event queue 
void MidiReader::close() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.clear();
}

// Forces retrieval and removal of the front event
bool MidiReader::forceFront(RawEvent& out) {
    std::lock_guard<std::mutex> lock(queueMutex_);

    if (queue_.empty())
        return false;

    out = std::move(queue_.front());
    queue_.pop_front();

    return true;
}

// Checks if any events remain in the locked queue
bool MidiReader::hasMoreEvents() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return !queue_.empty();
}

// Retrieves all backlog events matching or preceding the elapsed timestamp
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

// Pushes event into the queue with cap
void MidiReader::push(RawEvent&& ev) {
    std::lock_guard<std::mutex> lock(queueMutex_);

    queue_.push_back(std::move(ev));

    // Cap the queue doesn't eat all the RAM in the eventuality of an infinite queue
    // Intentionally not applied to the initial bulk load, see pushNoCap() right below
    constexpr size_t kMaxQueue = 4096;

    while (queue_.size() > kMaxQueue) {
        queue_.pop_front();
    }
}

// Pushes event into the queue without size cap
void MidiReader::pushNoCap(RawEvent&& ev) { // DIABOLICAL function name (purposeful shitpost... yet accurate)
    std::lock_guard<std::mutex> lock(queueMutex_);

    // No cap here on purpose, dataInit() already knows every event before this is even called
    //
    // *Using push() here doesn't work because the cap deletes the oldest timestamps  
    // *At some point this broke MIDI playback entirely because backlog() needs timestaps all the way from 0 to do the desync failsafe math
    // *Since the cap gets rid of already read events it just freezes playback since the full information isn't there on load
    queue_.push_back(std::move(ev));
}
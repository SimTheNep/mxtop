#pragma once

#include "types.hpp"

#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

enum class MidiMode {
    Native,
    GM2,
    GS,
    XG
};

// SysEx detection rule loaded from module definitions
struct DetectRule {
    std::string moduleFolder;
    std::string data;
};

// Turns MIDI files into a synchronized RawEvent stream.
struct MidiReader {

    MidiReader() = default;
    ~MidiReader();

    void dataInit(
        const std::vector<std::string>& filenames,
        size_t outputCount = 1,
        int midChannels = 16
    );

    // Returns detected module folder path after loading
    std::optional<std::string> detectedModuleFolder() const {
        return detectedModuleFolder_;
    }

    bool forceFront(RawEvent& out);
    bool hasMoreEvents() const;
    bool backlog(
        double elapsedMs,
        std::vector<RawEvent>& out
    );

    // Allows external/live MIDI injection
    void push(RawEvent&& ev);

    // Clears queue
    void close();

    size_t sourceCount() const {
        return sourceCount_;
    }

    int midChannels() const {
        return midChannels_;
    }

    // Add detection rule from parsed module.json
    void addDetectionRule(
        const DetectRule& rule
    );

private:
    // Queue handling
    void pushNoCap(RawEvent&& ev);

    // SysEx detection
    bool sysexMatches(
        const std::vector<unsigned char>& midi,
        const std::string& pattern
    );

    std::optional<std::string> detectSysEx(
        const RawEvent& ev
    );

    // Detected module folder path
    std::optional<std::string> detectedModuleFolder_;

    // Loaded JSON detection rules
    std::vector<DetectRule> detectRules_;

    // Event queue
    std::deque<RawEvent> queue_;

    mutable std::mutex queueMutex_;

    size_t sourceCount_ = 0;
    int midChannels_ = 16;
};
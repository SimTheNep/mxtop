#pragma once

#include "types.hpp"

#include <deque>
#include <mutex> // This stuff is the holy grail of this btw, wouldn't be possible without
#include <optional>
#include <string>
#include <vector>

enum class MidiMode { // This is goated
    Native,
    GM1,
    GM2,
    GS,
    XG
};

// THE MAGIC SAUCE OF READING MIDI
//
// This turns the MIDIs into a single stream of RawEvents, with their assigned port noted.
// That's what lets the files output to the ports after parsing in perfect sync (See dataInit() in the debug_ui.cpp)
//
//

struct MidiReader {

    MidiReader() = default;
    ~MidiReader();

    void dataInit(
        const std::vector<std::string>& filenames,
        int midChannels = 16 // Offsets the channels (see types.hpp)
    );


    bool forceFront(RawEvent& out); // Pops the next queued event into output
    bool hasMoreEvents() const; // True if there's at least one event left in the queue.
    bool backlog( double elapsedMs, std::vector<RawEvent>& out ); //  If an events timestamp is <= elapsedMs it gets output, simple enough

    // Pushes one already-parsed event onto the back of the queue (capped, see push() in midi_load.cpp)
    // Public for scalability, maybe live input from a synth or keyboard... if you care about this, lmk.
    void push(RawEvent&& ev);

    // Clears the queue, automatically called
    void close();

    size_t sourceCount() const { return sourceCount_; }
    int midChannels() const { return midChannels_; }

private: // Private because this stuff can mess up the queue so be careful

    // Same as push(), but it's uncapped ...
    void pushNoCap(RawEvent&& ev);

    std::deque<RawEvent> queue_;
    mutable std::mutex queueMutex_;

    size_t sourceCount_ = 0;
    int midChannels_ = 16;

    std::optional<MidiMode> midiStdSwitch(const RawEvent& ev);
};
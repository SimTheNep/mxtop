#pragma once

#include <cstdint>
#include <vector>

// Kind of message... simple distinctions
enum class MsgKind {
    NoteOff,
    NoteOn,
    CC,
    ProgramChange,
    PolyAftertouch,
    ChannelAftertouch,
    PitchBend,
    SysEx,
    Unknown
};

struct RawEvent {
    MsgKind kind = MsgKind::Unknown;

    int channel = -1;     // -1 for SysEx and anything else that isn't a channel message.

    int velocity;

    // Which port this event came from, used to know where to send the data after it gets merged into the stream
    int sourcePort = 0;

    // Ms from playback start.
    double timestamp = 0.0;

    // Layout depends on kind:
    //   CC                 -> { statusByte, ccNumber, ccValue }
    //   ProgramChange      -> { statusByte, program }
    //   PitchBend          -> { statusByte, lsb, msb } (not part of CC, I could've sworn it was)
    //   NoteOn / NoteOff   -> { statusByte, note, velocity }
    //   PolyAftertouch     -> { statusByte, note, pressure }
    //   ChannelAftertouch  -> { statusByte, pressure }
    //   SysEx              -> F0 [...] F7 byte sequence
    std::vector<uint8_t> data;
};

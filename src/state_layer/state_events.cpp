#include "state.hpp"
#include "../log.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#include <optional>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>

// STATE_EVENTS.CPP
//
// Reads the RawEvents and stores them

void stateLayer::storeValue(channelState& ch, const std::vector<const ModuleObject*>& targets, int value) {
    for (const ModuleObject* obj : targets)
    {
        if (obj->perPatch)
        {
            if (auto* patch = activePatch(ch))
                patch->values[obj->id] = value;


            // log("[PER PATCH] ",
            //     obj->id,
            //     " = ",
            //     value);
        }
        else
        {
            ch.rawValues[obj->id] = value;
        }
    }
}


// Bank updating, PC, MSB and LSB work differently than the rest
void stateLayer::updtBank(channelState& ch, int channel, int ccNum, int value)
{
    logDbg("[state_events] Ch %02d -> Bank Select CC%d = %d", channel + 1, ccNum, value);
    for (const auto& pb : patchIndex_)
    {
        if (ccNum == 0)
        {
            if (pb.hasMsb)
                setPatch(ch, channel, *pb.obj, patchField::Msb, value);

            if (pb.obj->drumBankMsb)
            {
                const auto& list = *pb.obj->drumBankMsb;
                ch.rhythmFromBank = std::find(list.begin(), list.end(), value) != list.end();
                
                if (ch.rhythmFromBank) {
                    logDbg("[state_events] Ch %02d switched to Rhythm mode via MSB %d", channel + 1, value);
                }
            }
        }

        if (ccNum == 32 && pb.hasLsb)
            setPatch(ch, channel, *pb.obj, patchField::Lsb, value);
    }
}


// Updates the program change
void stateLayer::updtPC(channelState& ch, int channel, int program)
{
    logDbg("[state_events] Ch %02d -> Program Change: %d", channel + 1, program + 1);
    for (const auto& pb : patchIndex_)
    {
        if (pb.hasProgram)
            setPatch(ch, channel, *pb.obj, patchField::Program, program);
    }
}


// Handles all the RawEvents
void stateLayer::eventHandler(const RawEvent& ev) {

    auto [chIt, inserted] = channels_.try_emplace(ev.channel);
    channelState& ch = chIt->second;

    if (inserted) initCh(ch, ev.channel);

    switch (ev.kind) {
        case MsgKind::NoteOn:
            if (ev.data.size() >= 3) {
                updtNote(ch, ev.data[1], ev.velocity, true);
            }
            break;

        case MsgKind::NoteOff:
            if (ev.data.size() >= 2) {
                updtNote(ch, ev.data[1], 0, false);
            }
            break;
        
        case MsgKind::CC:
            if (ev.data.size() >= 3 && ev.data[1] < 128) {
                int ccNum = ev.data[1];
                int value = ev.data[2];

                const auto& targets = ccIndex_[ccNum];
                if (!targets.empty()) {
                    storeValue(ch, targets, value);
                }

                if (ccNum == 0 || ccNum == 32)
                    updtBank(ch, ev.channel, ccNum, value);
            }
            break;

        case MsgKind::PitchBend:
            if (ev.data.size() >= 3) {
                storeValue(ch, pitchBendIndex_, (ev.data[2] << 7) | ev.data[1]);
            }
            break;

        case MsgKind::SysEx:

            handleSysEx(ev, ch);
            break;

        case MsgKind::ProgramChange:
            if (ev.data.size() >= 2) {
                updtPC(ch, ev.channel, ev.data[1]);
            }
            break;

        default:
            break;
    }
}




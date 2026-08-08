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

// STATE_QUERY.CPP
//
// Converts everything from state_events and state_sysex to their actual names


// Gets channel state by channel index
const channelState* stateLayer::getChannel(int channel) const {
    auto it = channels_.find(channel);

    return it == channels_.end() ? nullptr : &it->second;
}


// Generates a list of all active MIDI channels
std::vector<int> stateLayer::activeCh() const
{
    // Start with default 16 channels, but dynamically expand if higher channels exist in channels_
    int maxCh = std::max(16, static_cast<int>(reader_.sourceCount()) * reader_.midChannels());

    for (const auto& [ch, cState] : channels_) {
        if (ch >= 0) {
            maxCh = std::max(maxCh, ch + 1);
        }
    }

    // Round up to nearest 16-channel port block (16, 32, 48, 64...)
    const int totalChannels = ((maxCh + 15) / 16) * 16;

    std::vector<int> result;
    result.reserve(totalChannels);

    for (int ch = 0; ch < totalChannels; ++ch)
        result.push_back(ch);

    return result;
}


// Looks up effect names from dictionary/module.json
std::optional<std::string> stateLayer::effLookup(const ModuleObject& obj, int value) const {
    if (!obj.lookup)
        return std::nullopt;

    auto dict = enumLookup_.find(*obj.lookup);

    if (dict == enumLookup_.end())
        return std::nullopt;

    auto it = dict->second.find(value);

    if (it == dict->second.end())
        return std::nullopt;

    return it->second;
}


// Looks up bank name from dictionary.json bankGroups
std::optional<std::string> stateLayer::bankLookup(bool isRhythm, int msb, int lsb) const {
    const std::string groupKey = isRhythm ? "drum_kits" : "patches";
    auto groupIt = dictionary_.bankGroups.find(groupKey);
    if (groupIt == dictionary_.bankGroups.end()) {
        groupIt = dictionary_.bankGroups.find("patches");
    }
    if (groupIt == dictionary_.bankGroups.end()) return std::nullopt;

    for (const auto& bank : groupIt->second) {
        if (bank.bank.bankMSB && *bank.bank.bankMSB != msb) continue;
        if (bank.bank.bankLSB && *bank.bank.bankLSB != lsb) continue;
        return bank.name;
    }

    return std::nullopt;
}


// Assigns patch names from dictionary.json
std::optional<std::string> stateLayer::patchLookup(bool isRhythm, const channelState::patchState& patch, int* resolvedLsb) const {
    channelState::patchState lookupPatch = patch;

    // GS drum kits ignore bank MSB values, force to 0 for lookup
    if (module_.id == "gs" && isRhythm)
        lookupPatch.msb = 0;

    // Assumes the patch lsb is the one set, if not, it will fallback to the closest one below it
    if (resolvedLsb) *resolvedLsb = patch.lsb;

    auto tryGroup = [&](const std::string& groupName, int lsbToMatch) -> std::optional<std::string> {
    auto groupIt = dictionary_.bankGroups.find(groupName);
    if (groupIt == dictionary_.bankGroups.end())
            return std::nullopt;

        for (const auto& bank : groupIt->second) {
            if (bank.bank.bankMSB && *bank.bank.bankMSB != lookupPatch.msb) continue;
            if (bank.bank.bankLSB && *bank.bank.bankLSB != lsbToMatch) continue;

            for (const auto& item : bank.items) {
                bool programMatch = (item.program == patch.program);
                if (!programMatch) continue;
                if (item.bankMSB && *item.bankMSB != lookupPatch.msb) continue;
                if (item.bankLSB && *item.bankLSB != lsbToMatch) continue;
                return item.name;
            }
        }
        return std::nullopt;
    };

    const std::string groupKey = isRhythm ? "drum_kits" : "patches";

    if (module_.id == "gs") {
        // Tier search for GS maps
        // Basically checks whether the patch is available for the 55, 88, 88Pro or 8850 and dictactes whether it will be used or not
        static const int tiers[] = {4, 3, 2, 1};
        const int startTier = (patch.lsb == 0) ? 4 : patch.lsb;
        for (int tier : tiers) {
            if (tier > startTier) continue;
            if (auto name = tryGroup(groupKey, tier)) {
                if (resolvedLsb) *resolvedLsb = tier;
                return name;
            }
        }
    } else {
        // Standard lookup for non-GS modules
        if (auto name = tryGroup(groupKey, patch.lsb)) return name;
        if (auto name = tryGroup("patches", patch.lsb)) return name;
    }

    return std::nullopt;
}

// Formats some parameters into display strings  (offset and transform from module.json)
std::string stateLayer::mathVal(const ModuleObject& obj, int raw) const {
    if (auto name = effLookup(obj, raw))
        return *name;

    const std::string& transform = obj.displayOffset.transform;

    if (transform == "pan_lcr") {
        int v = raw - 64;

        if (v < 0) return std::to_string(-v) + "L";
        if (v > 0) return std::to_string(v) + "R";

        return "C";
    }

    if (transform == "frequency") {
        double hz = 440.0 * std::pow(2.0, (raw - 1024.0) / 8192.0);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f Hz", hz);

        return buf;
    }

    if (transform == "pitchbend") {
        int v = raw - 8192;

        if (v == 0) return "0";

        return (v > 0 ? "+" : "") + std::to_string(v);
    }

    return std::to_string(raw + obj.displayOffset.offset);
}


// Fetches the final value for an objectID in a channel
std::optional<std::string> stateLayer::finalVal(int channel, const std::string& objectId) const {
    const channelState* ch = getChannel(channel);

    if (!ch) return std::nullopt;

    auto objIt = objectById_.find(objectId);

    if (objIt == objectById_.end())
        return std::nullopt;

    const ModuleObject& obj = *objIt->second;

    if (obj.perPatch)
    {
        if (auto patch = activePatch(*ch))
        {
            auto it = patch->values.find(objectId);

            if (it != patch->values.end())
                return mathVal(obj, it->second);
        }

        return std::nullopt;
    }

    auto valueIt = ch->rawValues.find(objectId);

    if (valueIt == ch->rawValues.end())
        return std::nullopt;

    return mathVal(obj, valueIt->second);
}


// Updates active note trackers for polyphony and velocity
void stateLayer::updtNote(channelState& ch, int note, int velocity, bool on) {
    if (on) {
        ch.activeNotes[note] = velocity;
    } else {
        ch.activeNotes.erase(note);
    }

    ch.lastNote = note;
    ch.lastVelo = velocity;
}


// Sends channel parameters into a UI snapshot 
takeSnapshot stateLayer::snapshot(int channel) const {
    takeSnapshot snap;

    if (channel == -1) {
        if (const channelState* sysCh = getChannel(-1)) {
            for (const auto& [id, raw] : sysCh->rawValues) {
                auto objIt = objectById_.find(id);

                if (objIt == objectById_.end()) continue;

                snap.rawValues[id] = raw;
                snap.values[id] = mathVal(*objIt->second, raw);
            }
        }

        for (const auto& [channelNumber, channelState] : channels_) {
            if (channelNumber < 0) {
                continue;
            }

            snap.polyCount += static_cast<int>(
                channelState.activeNotes.size());

            if (channelState.lastNote != -1) {
                snap.lastNote = channelState.lastNote;
                snap.lastVelo = channelState.lastVelo;
            }
        }

        return snap;
    }

    const channelState* ch = getChannel(channel);

    if (!ch) {
        return snap;
    }

    for (const auto& [id, raw] : ch->rawValues) {
        auto objIt = objectById_.find(id);

        if (objIt == objectById_.end()) continue;

        snap.rawValues[id] = raw;
        snap.values[id] = mathVal(*objIt->second, raw);
    }
    
    for (const auto& [patchId, patch] : ch->patches)
    {
        for (const auto& [objId, raw] : patch.values)
        {
            auto obj = objectById_.find(objId);

            if (obj == objectById_.end())
                continue;

            snap.rawValues[objId] = raw;
            snap.values[objId] = mathVal(*obj->second, raw);
        }

        snap.values["program"] = std::to_string(patch.program);
        snap.values["msb"] = std::to_string(patch.msb);

        bool rhythm = ch->rhythmFromSysEx || ch->rhythmFromBank;

        // Auto-detect rhythm mode if patch MSB matches a drum bank MSB
        if (defaultPatchObject_ && defaultPatchObject_->drumBankMsb) {
            const auto& list = *defaultPatchObject_->drumBankMsb;
            if (std::find(list.begin(), list.end(), patch.msb) != list.end()) {
                rhythm = true;
            }
        }

        snap.isRhythm = rhythm;

        if (auto bName = bankLookup(rhythm, patch.msb, patch.lsb)) {
            snap.values["bank_name"] = *bName;
        } else {
            snap.values["bank_name"] = "";
        }

        // patchLookup falls back to the nearest low variation
        int resolvedLsb = patch.lsb;
        snap.patchNames[patchId] = patchLookup(rhythm, patch, &resolvedLsb);
        snap.values["lsb"] = std::to_string(resolvedLsb);
    }

    // Aggregates global polyphony or individual channel notes
    if (channel == -1) {
        int totalPoly = 0;
        int globalLastNote = -1;
        int globalLastVelo = 0;

        for (const auto& [chNum, cState] : channels_) {
            totalPoly += static_cast<int>(cState.activeNotes.size());

            if (cState.lastNote != -1) {
                globalLastNote = cState.lastNote;
                globalLastVelo = cState.lastVelo;
            }
        }

        snap.polyCount = totalPoly;
        snap.lastNote = globalLastNote;
        snap.lastVelo = globalLastVelo;

    } else {

        snap.polyCount = static_cast<int>(ch->activeNotes.size());
        snap.lastNote = ch->lastNote;
        snap.lastVelo = ch->lastVelo;

        // Populate active notes
        snap.activeNotes.reserve(ch->activeNotes.size());
        for (const auto& [note, velo] : ch->activeNotes) {
            snap.activeNotes.push_back(note);
        }
    }

    return snap;
}
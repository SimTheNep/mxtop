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

// STATE_SYSEX.CPP
//
// SysEx decoding, accounts for different standards

// Checks what type of format it is
bool stateLayer::dataHandler(const RawEvent& ev, channelState& sysCh) {
    for (const auto& binding : dataIndex_) {
        if (ev.data.size() != binding.pattern.size()) continue;

        bool match = true;
        std::vector<uint8_t> wildcards;
        for (size_t i = 0; i < binding.pattern.size(); ++i) {
            if (binding.pattern[i]) {
                if (ev.data[i] != *binding.pattern[i]) { match = false; break; }
            } else {
                wildcards.push_back(ev.data[i]);
            }
        }
        if (!match) continue;

        // If it's a 2-byte value like Master Volume, combine them (LSB, MSB)
        int value = binding.obj->defaultValue.value_or(1);
        if (wildcards.size() >= 2) {
            value = (wildcards[1] << 7) | wildcards.front();
        } else if (wildcards.size() == 1) {
            value = wildcards.front();
        }

        sysCh.rawValues[binding.obj->id] = value;

        // log("[DATA TEMPLATE] matched ", binding.obj->id, " value=", value);
        return true;
    }
    return false;
}


// Processes the MIDI reader backlog
void stateLayer::advance(double elapsedMs) {
    std::vector<RawEvent> batch;

    if (reader_.backlog(elapsedMs, batch)) {
        for (const auto& ev : batch)
            eventHandler(ev);
    }
}



// Validates Roland checksum
static bool verifyRolandChecksum(const std::vector<uint8_t>& data, size_t addressStart, size_t checksumIndex) {
    int sum = 0;

    for (size_t i = addressStart; i < checksumIndex; ++i)
        sum += data[i];

    int expected = (128 - (sum & 0x7F)) & 0x7F;

    return (data[checksumIndex] & 0x7F) == expected;
}



// PATCH SYSEX LOGIC
//
//

void stateLayer::setPatch(channelState& targetCh, int channel, const ModuleObject& obj, patchField field, int value) {
    
    auto [it, patchInserted] = targetCh.patches.try_emplace(obj.id, defaultPatch(obj));

    if (patchInserted)
    {
        for (const auto& object : module_.objects)
        {
            if (object.type == kind::SysEx && object.perPatch)
                it->second.values.emplace(object.id, initOffset(object, channel));
        }
    }

    // log("setPatch ",
    // obj.id,
    // " field=", (int)field,
    // " value=", value);

    switch (field) {
        case patchField::Msb:
            it->second.msb = value;
            break;
        case patchField::Lsb:
            it->second.lsb = value;
            break;
        case patchField::Program:
            it->second.program = value;
            break;
    }

    // log(" -> MSB=", it->second.msb,
    // " LSB=", it->second.lsb,
    // " Program=", it->second.program);

    // log("PATCH ",
    // obj.id,
    // " MSB=", it->second.msb,
    // " LSB=", it->second.lsb,
    // " Program=", it->second.program);

    // dbg << "[PATCH] "
    //     << obj.id
    //     << " MSB=" << it->second.msb
    //     << " LSB=" << it->second.lsb
    //     << " PC=" << it->second.program
    //     << '\n';

    // dbg.flush();
}

// Sets MFX output routing and its per-patch MFX selection
void stateLayer::setMFX(channelState& targetCh, int outputAssign, int MFXSelect) {
    auto* patch = activePatch(targetCh);
    if (!patch) return;

    patch->values["output_assign"] = outputAssign;
    patch->values["mfx"] = (outputAssign == 0x0D) ? MFXSelect : 0;

    // dbg << "[MFX ASSIGNMENT] OutputAssign=" << outputAssign 
    //     << " MFXSelect=" << patch->values["mfx"] << '\n';
    // dbg.flush();
}


// SYSEX HANDLING
//
//

void stateLayer::handleSysEx(const RawEvent& ev, channelState& ch) {

    // Minimum SysEx requirements
    if (ev.data.size() < 4) return;
    if (ev.data.front() != 0xF0) return;
    if (ev.data.back() != 0xF7) return;

    // -1 is the bucket, check for it and init if it doesn't exist
    auto [sysChIt, sysInserted] = channels_.try_emplace(-1);
    if (sysInserted) initCh(sysChIt->second, -1);

    // Checks for fixed-data ones (such as system on messages)
    bool handled = dataHandler(ev, sysChIt->second);
    if (handled)
        return;

    // Filters packets intended for other hardware manufacturers based on ID bytes
    if (module_.manufacturer && ev.data[1] != module_.manufacturer) {
        return;
    }

    const bool hasCommand = (module_.checksum == "roland");

    // Checks device model byte position depending on header lenght formatting is used
    size_t modelIdx = hasCommand ? headerLen_ - 2 : headerLen_ - 1;
    if (module_.model && ev.data[modelIdx] != module_.model) {
        return;
    }

    // For Roland, checks if it's a read or write message
    if (hasCommand && ev.data[headerLen_ - 1] != 0x12) {
        return;
    }

    // Checksum validation on Roland data
    if (module_.checksum == "roland") {
        const size_t checksumIndex = ev.data.size() - 2;

        bool ok = verifyRolandChecksum(ev.data, headerLen_, checksumIndex);

        if (!ok)
            return;
    }

    // Dumps raw packet bytes into a hex string stream for logging
    std::ostringstream dump;
    for (uint8_t b : ev.data) {
        dump << std::hex
            << std::uppercase
            << std::setw(2)
            << std::setfill('0')
            << (int)b << ' ';
    }

    // Double checks the command byte configuration
    if (ev.data[headerLen_ - 1] != 0x12) {
        return;
    }

    uint32_t msgAddr = 0;

    // Extracts the base memory address fields from module.json
    for (size_t i = headerLen_; i < headerLen_ + addrWidth_; ++i)
        msgAddr = (msgAddr << 8) | ev.data[i];

    // Calculates the payload length based on address width
    int msgDataLen = static_cast<int>(ev.data.size()) - headerLen_ - addrWidth_ - 2;

    if (msgDataLen <= 0)
        return;

    // Locates the matching sysex index
    auto it = std::lower_bound(
        sysexIndex_.begin(),
        sysexIndex_.end(),
        msgAddr,
        [this](const sysexBinding& b, uint32_t addr) {
            return toAddrNum(b.addr) < addr;
        });

    // Iterates through overlapping addresses (had to be done because of afx in the sd-90)
    for (; it != sysexIndex_.end() && toAddrNum(it->addr) < msgAddr + msgDataLen; ++it) {
        const auto& binding = *it;
        uint32_t bindAddr = toAddrNum(binding.addr);

        const ModuleObject* obj = binding.obj;
        const int valueBytes = obj->bytes.value_or(1);

        // Verifies that the address is within the valid range
        if (bindAddr >= msgAddr && bindAddr + valueBytes <= msgAddr + msgDataLen) {
            int offset = bindAddr - msgAddr;
            size_t valueStart = headerLen_ + addrWidth_ + offset;

            int value = 0;
            // Decodes nibble encoded messages
            if (obj->encoding && *obj->encoding == "nibbles") {
                for (int i = 0; i < valueBytes; ++i)
                    value = (value << 4) | (ev.data[valueStart + i] & 0x0F);
            } else {
                for (int i = 0; i < valueBytes; ++i)
                    value = (value << 8) | ev.data[valueStart + i];
            }

            // Checks if the target channel exists, if not, init it
            auto [chIt, inserted] = channels_.try_emplace(binding.channel);
            channelState& targetCh = chIt->second;
            if (inserted) initCh(targetCh, binding.channel);

            // Stores the decoded parameter in the channel
            if (obj->perPatch) {
                if (auto* patch = activePatch(targetCh))
                    patch->values[obj->id] = value;
            } else {
                targetCh.rawValues[obj->id] = value;
            }

            // Updates rhythm channel state flags
            if (obj->controlsRhythm) {
                targetCh.rhythmFromSysEx = (value != 0);
                targetCh.rhythmFromBank = false;
            }
        }
    }

    // Scans per-patch sysex
    auto patchIt = std::lower_bound(patchSysexIndex_.begin(), patchSysexIndex_.end(), msgAddr,
        [this](const patchSysexBinding& b, uint32_t addr) { return toAddrNum(b.addr) < addr; });

    for (; patchIt != patchSysexIndex_.end() && toAddrNum(patchIt->addr) < msgAddr + msgDataLen; ++patchIt) {
        const auto& binding = *patchIt;
        uint32_t bindAddr = toAddrNum(binding.addr);

        // Checks if the patch parameter address matches the current data size
        if (bindAddr >= msgAddr && bindAddr < (msgAddr + msgDataLen)) {
            int offset = bindAddr - msgAddr;
            size_t valueStart = headerLen_ + addrWidth_ + offset;

            // Verifies the channel exists before modifying parameters
            auto [chIt, inserted] = channels_.try_emplace(binding.channel);
            channelState& targetCh = chIt->second;
            if (inserted) initCh(targetCh, binding.channel);

            // Applies the patch assignment (MSB, LSB, PC)
            setPatch(targetCh, binding.channel, *binding.obj, binding.field, ev.data[valueStart]);
        }
    }
}
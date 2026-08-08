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

        int value = binding.obj->defaultValue.value_or(1);
        if (wildcards.size() >= 2) {
            value = (wildcards[1] << 7) | wildcards.front();
        } else if (wildcards.size() == 1) {
            value = wildcards.front();
        }

        sysCh.rawValues[binding.obj->id] = value;
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
void stateLayer::setPatch(channelState& targetCh, int channel, const ModuleObject& obj, patchField field, int value) {
    auto [it, patchInserted] = targetCh.patches.try_emplace(obj.id, defaultPatch(obj));

    if (patchInserted) {
        for (const auto& object : module_.objects) {
            if (object.type == kind::SysEx && object.perPatch)
                it->second.values.emplace(object.id, initOffset(object, channel));
        }
    }

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
}

// Sets MFX output routing and its per-patch MFX selection
void stateLayer::setMFX(channelState& targetCh, int outputAssign, int MFXSelect) {
    auto* patch = activePatch(targetCh);
    if (!patch) return;

    patch->values["output_assign"] = outputAssign;
    patch->values["mfx"] = (outputAssign == 0x0D) ? MFXSelect : 0;
}

// SYSEX HANDLING
void stateLayer::handleSysEx(const RawEvent& ev, channelState& ch) {
    if (ev.data.size() < 4) return;
    if (ev.data.front() != 0xF0) return;
    if (ev.data.back() != 0xF7) return;

    auto [sysChIt, sysInserted] = channels_.try_emplace(-1);
    if (sysInserted) initCh(sysChIt->second, -1);

    bool handled = dataHandler(ev, sysChIt->second);
    if (handled) return;

    if (module_.manufacturer && ev.data[1] != module_.manufacturer) {

        logDbg("[state_sysex] Dropped SysEx: Manufacturer ID mismatch (0x%02X != 0x%02X)",
            ev.data[1], module_.manufacturer);

        return;
    }

    const bool hasCommand = (module_.checksum == "roland");

    size_t modelIdx = hasCommand ? headerLen_ - 2 : headerLen_ - 1;
    if (module_.model && ev.data[modelIdx] != module_.model) {
        return;
    }

    if (hasCommand && ev.data[headerLen_ - 1] != 0x12) {
        return;
    }

    if (module_.checksum == "roland") {
        const size_t checksumIndex = ev.data.size() - 2;
        bool ok = verifyRolandChecksum(ev.data, headerLen_, checksumIndex);
        if (!ok) {
            logDbg("[state_sysex] Dropped SysEx: Roland checksum verification failed");
            return;
        }
    }

    uint32_t msgAddr = 0;
    for (size_t i = headerLen_; i < headerLen_ + addrWidth_; ++i)
        msgAddr = (msgAddr << 8) | ev.data[i];

    int msgDataLen = static_cast<int>(ev.data.size()) - headerLen_ - addrWidth_ - 2;
    if (msgDataLen <= 0) return;

    auto it = std::lower_bound(
        sysexIndex_.begin(),
        sysexIndex_.end(),
        msgAddr,
        [this](const sysexBinding& b, uint32_t addr) {
            return toAddrNum(b.addr) < addr;
        });

    for (; it != sysexIndex_.end() && toAddrNum(it->addr) < msgAddr + msgDataLen; ++it) {
        const auto& binding = *it;
        uint32_t bindAddr = toAddrNum(binding.addr);

        const ModuleObject* obj = binding.obj;
        const int valueBytes = obj->bytes.value_or(1);

        if (bindAddr >= msgAddr && bindAddr + valueBytes <= msgAddr + msgDataLen) {
            int offset = bindAddr - msgAddr;
            size_t valueStart = headerLen_ + addrWidth_ + offset;

            int value = 0;
            if (obj->encoding && *obj->encoding == "nibbles") {
                for (int i = 0; i < valueBytes; ++i)
                    value = (value << 4) | (ev.data[valueStart + i] & 0x0F);
            } else {
                for (int i = 0; i < valueBytes; ++i)
                    value = (value << 8) | ev.data[valueStart + i];
            }

            auto [chIt, inserted] = channels_.try_emplace(binding.channel);
            channelState& targetCh = chIt->second;
            if (inserted) initCh(targetCh, binding.channel);

            if (obj->perPatch) {
                if (auto* patch = activePatch(targetCh))
                    patch->values[obj->id] = value;
            } else {
                targetCh.rawValues[obj->id] = value;
            }

            logDbg("[state_sysex] Parameter '%s' set to raw %d on Ch %d via SysEx (Addr: 0x%06X)",
                   obj->id.c_str(), value, binding.channel, bindAddr);

            // Only set rhythmFromSysEx if value represents Drum (1) or Drum2 (2)
            if (obj->controlsRhythm) {
                targetCh.rhythmFromSysEx = (value == 1 || value == 2);
                targetCh.rhythmFromBank = false;
            }
        }
    }

    auto patchIt = std::lower_bound(patchSysexIndex_.begin(), patchSysexIndex_.end(), msgAddr,
        [this](const patchSysexBinding& b, uint32_t addr) { return toAddrNum(b.addr) < addr; });

    for (; patchIt != patchSysexIndex_.end() && toAddrNum(patchIt->addr) < msgAddr + msgDataLen; ++patchIt) {
        const auto& binding = *patchIt;
        uint32_t bindAddr = toAddrNum(binding.addr);

        if (bindAddr >= msgAddr && bindAddr < (msgAddr + msgDataLen)) {
            int offset = bindAddr - msgAddr;
            size_t valueStart = headerLen_ + addrWidth_ + offset;

            auto [chIt, inserted] = channels_.try_emplace(binding.channel);
            channelState& targetCh = chIt->second;
            if (inserted) initCh(targetCh, binding.channel);

            setPatch(targetCh, binding.channel, *binding.obj, binding.field, ev.data[valueStart]);
        }
    }
}
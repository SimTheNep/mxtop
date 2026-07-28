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

// STATE_MAIN.CPP
//
// Mostly helper functions tbh

// HELPER FUNCTIONS
//
// Implemented later because of repetition and stuff

// Helper function to decode the SysEx just to be more efficient
uint32_t stateLayer::toAddrNum(const std::vector<uint8_t>& addr) {
    uint32_t n = 0;
    for (uint8_t b : addr) n = (n << 8) | b;

    return n;
}

std::string stateLayer::hexAddr(uint32_t n) {
    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setw(6) << std::setfill('0') << n;
    return ss.str();
}

channelState::patchState* stateLayer::activePatch(channelState& ch)
{
    if (!defaultPatchObject_)
        return nullptr;

    auto it = ch.patches.find(defaultPatchObject_->id);

    if (it == ch.patches.end())
        return nullptr;

    return &it->second;
}

const channelState::patchState* stateLayer::activePatch(const channelState& ch) const
{
    if (!defaultPatchObject_)
        return nullptr;

    auto it = ch.patches.find(defaultPatchObject_->id);

    if (it == ch.patches.end())
        return nullptr;

    return &it->second;
}

// PART ADDRESS CALCULATION (ROLAND - TESTED ON SD-90)
//
//

std::vector<uint8_t> stateLayer::getRolandPart(const std::vector<uint8_t>& baseAddr, int channel) {
    if (baseAddr.size() < 3) return baseAddr;
    
    std::vector<uint8_t> addr = baseAddr;

    int port = channel / 16;  // 0, 1, 2, 3... for each 16-channel set
    int ch = channel % 16;    // 0 to 15

    // Standard Roland GS 3-byte address mapping (0x40 0x1x 0xyy)
    if (baseAddr.size() == 3 && baseAddr[0] == 0x40) {
        uint8_t partByte = 0x10;

        // GS Part mapping: Part 10 = 0x10, Part 1..9 = 0x11..0x19, Part 11..16 = 0x1A..0x1F
        if (ch == 9) { 
            partByte = 0x10;
        } else if (ch < 9) { 
            partByte = static_cast<uint8_t>(0x11 + ch);
        } else { 
            partByte = static_cast<uint8_t>(0x1A + (ch - 10));
        }

        // Handle multi-port offset dynamically for N sets of 16
        partByte = (partByte & 0x0F) | ((port + 1) << 4);

        addr[1] = partByte;
        return addr;
    }

    // SD-90 / Sound Canvas Part mapping:
    // Channels 0-15 (Part A): 0x20 to 0x2F
    // Channels 16-31 (Part B): 0x30 to 0x3F
    // and so on for N ports
    addr[2] = static_cast<uint8_t>((0x20 + (port << 4)) + ch);

    return addr;
}



bool stateLayer::isRolandPart(const std::vector<uint8_t>& addr) {
    // GS 3-byte address space
    if (addr.size() == 3 && addr[0] == 0x40) {
        uint8_t portNibble = addr[1] & 0xF0;
        return portNibble >= 0x10;
    }

    // SD-90 4-byte native address space
    if (addr.size() >= 4 && addr[0] == 0x10 && addr[1] == 0x00) {
        uint8_t portNibble = addr[2] & 0xF0;
        return portNibble >= 0x20;
    }

    return false;
}



// CONVERT HEX TO BYTES
//
//

std::vector<uint8_t> stateLayer::hexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;

    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));

    return out;
}



// DEFAULT PATCH STATE
//
// Builds a patch sequence from fields, anything not present defaults to 0

channelState::patchState stateLayer::defaultPatch(const ModuleObject& obj) {
    channelState::patchState p;

    if (auto it = obj.fields.find("msb"); it != obj.fields.end())
        p.msb = it->second;

    if (auto it = obj.fields.find("lsb"); it != obj.fields.end())
        p.lsb = it->second;

    if (auto it = obj.fields.find("program"); it != obj.fields.end())
        p.program = it->second;

    return p;
}



// DEFAULT VALUES INIT
//
//

int stateLayer::initOffset(const ModuleObject& obj, int channel)
{
    if (auto it = obj.partDefaults.find(channel);
        it != obj.partDefaults.end())
    {
        return it->second;
    }

    if (obj.defaultValue)
        return *obj.defaultValue;

    if (obj.displayOffset.transform == "pan_lcr")
        return 64;

    if (obj.displayOffset.transform == "pitchbend")
        return 8192;

    if (obj.displayOffset.transform == "frequency")
        return 1024;

    return -obj.displayOffset.offset;
}




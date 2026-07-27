#include "state.hpp"

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

static std::ofstream dbg("state_debug.log", std::ios::trunc);

template<typename... Args>
static void log(Args&&... args)
{
    (dbg << ... << args);
    dbg << '\n';
    dbg.flush();
}

// HELPER FUNCTIONS
//
// Implemented later because of repetition and stuff

// Helper function to decode the SysEx just to be more efficient
static uint32_t toAddrNum(const std::vector<uint8_t>& addr) {
    uint32_t n = 0;
    for (uint8_t b : addr) n = (n << 8) | b;

    return n;
}

static std::string hexAddr(uint32_t n) {
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

static std::vector<uint8_t> getRolandPart(const std::vector<uint8_t>& baseAddr, int channel) {
    if (baseAddr.size() < 3) return baseAddr;
    
    std::vector<uint8_t> addr = baseAddr;

    // Standard Roland GS 3-byte address mapping (0x40 0x1x 0xyy)
    if (baseAddr.size() == 3 && baseAddr[0] == 0x40) {
        int ch = channel % 16;
        uint8_t partByte = 0x10;

        // GS Part mapping: Part 10 = 0x10, Part 1..9 = 0x11..0x19, Part 11..16 = 0x1A..0x1F
        if (ch == 9) { 
            partByte = 0x10;
        } else if (ch < 9) { 
            partByte = static_cast<uint8_t>(0x11 + ch);
        } else { 
            partByte = static_cast<uint8_t>(0x1A + (ch - 10));
        }

        // Handle Part B offset (channels 16-31)
        if (channel >= 16) { 
            partByte = (partByte & 0x0F) | 0x20;
        }

        addr[1] = partByte;
        return addr;
    }

    // SD-90 / Sound Canvas Part mapping:
    // Channels 0-15 (Part A): 0x20 to 0x2F
    // Channels 16-31 (Part B): 0x30 to 0x3F
    if (channel < 16) {
        addr[2] = static_cast<uint8_t>(0x20 + channel);
    } else {
        addr[2] = static_cast<uint8_t>(0x30 + (channel - 16));
    }

    return addr;
}



static bool isRolandPart(const std::vector<uint8_t>& addr) {
    // GS 3-byte address space (0x40 1x xx or 0x40 2x xx)
    if (addr.size() == 3 && addr[0] == 0x40 && ((addr[1] & 0xF0) == 0x10 || (addr[1] & 0xF0) == 0x20))
        return true;

    // SD-90 4-byte native address space (0x10 0x00 0x20 xx)
    return addr.size() >= 4 &&
           addr[0] == 0x10 &&
           addr[1] == 0x00 &&
           (addr[2] & 0xF0) == 0x20;
}



// CONVERT HEX TO BYTES
//
//

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;

    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));

    return out;
}



// DEFAULT PATCH STATE
//
// Builds a patch sequence from fields, anything not present defaults to 0

static channelState::patchState defaultPatch(const ModuleObject& obj) {
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

static int initOffset(const ModuleObject& obj) {
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



void stateLayer::initCh(channelState& ch, int channel) {
    const bool isSysBucket = (channel == -1);

    if (!isSysBucket) {
        const int block = reader_.midChannels();
        ch.rhythmFromSysEx = false;
        ch.rhythmFromBank  = false;

        if (block > 0 && (channel % block == 9))
            ch.rhythmFromBank = true;
    }

    for (const auto& obj : module_.objects) {
        if (obj.type == kind::SysEx) { // SysEx with part support
            if (isSysBucket && obj.parts.empty() && !obj.address) {
                ch.rawValues.try_emplace(obj.id, initOffset(obj));

            } else if (!isSysBucket && !obj.parts.empty()) {

                // Initialize explicitly defined manual parts
                if (obj.parts.size() == 1 && obj.parts.front().channel == 0) {
                    ch.rawValues.try_emplace(obj.id, initOffset(obj));
                } else {
                    for (const auto& part : obj.parts) {
                        if (part.channel == channel) {
                            ch.rawValues.try_emplace(obj.id, initOffset(obj));
                            break;
                        }
                    }
                }

            } else if (!isSysBucket && obj.address && obj.parts.empty()) {

                auto addr = hexToBytes(*obj.address);

                // Use the full check
                // The AFX was displaying in every channel without this
                if (isRolandPart(addr)) {
                    ch.rawValues.try_emplace(obj.id, initOffset(obj));
                }
            }

        } else if (obj.type == kind::Patch) {
            if (!isSysBucket){
                auto& patch = ch.patches.try_emplace(obj.id, defaultPatch(obj)).first->second;

            for (const auto& object : module_.objects)
            {
                if (object.type == kind::SysEx && object.perPatch)
                    patch.values.emplace(object.id, initOffset(object));
            }

            }
        } else { // CC, PitchBend
            if (!isSysBucket)
                ch.rawValues.try_emplace(obj.id, initOffset(obj));
            
        }

        if (isSysBucket && obj.address) {
            auto addr = hexToBytes(*obj.address);

            if (!(isRolandPart(addr))) {
                ch.rawValues.try_emplace(obj.id, initOffset(obj));
            }
        }
    }
}



stateLayer::stateLayer(const moduleDef& module, const dictionaryDef& dictionary, MidiReader& reader)
    : module_(module), dictionary_(dictionary), reader_(reader),
      headerLen_(static_cast<size_t>(module.headerLen)),
      addrWidth_(static_cast<size_t>(module.addressWidth)) {

    log("Module loaded:", module_.id);
    log("headerLen=", headerLen_);
    log("addrWidth=", addrWidth_);
    log("manufacturer=", (int)module_.manufacturer);
    log("model=", (int)module_.model);
    log("sysexIndex size=", sysexIndex_.size());

    // Real channel count in use,
    // This cuts down the cost so the module doesn't lag (my SD-90 doesn't like 64-channels+ being sent to it)
    const int totalChannels = std::max(1, static_cast<int>(reader_.sourceCount()) * reader_.midChannels());

    defaultPatchObject_ = nullptr;

        for (const auto& obj : module_.objects) {
            objectById_[obj.id] = &obj;

            if (obj.type == kind::CC && obj.cc) {
                ccIndex_[*obj.cc].push_back(&obj);
            } else if (obj.type == kind::PitchBend) {
                pitchBendIndex_.push_back(&obj);
            } else if (obj.type == kind::SysEx) {
                if (!obj.parts.empty()) {
                    if (obj.parts.size() == 1 && obj.parts.front().channel == 0) {
                        const auto base = hexToBytes(obj.parts.front().address);

                        for (int ch = 0; ch < totalChannels; ++ch)
                            sysexIndex_.push_back({
                                getRolandPart(base, ch),
                                &obj,
                                ch
                            });
                    } else {
                        for (const auto& part : obj.parts) {
                            sysexIndex_.push_back({hexToBytes(part.address), &obj, part.channel});
                        }
                    }
                } else if (obj.address) {
                    auto addr = hexToBytes(*obj.address);
                    
                    if (isRolandPart(addr)) {
                        for (int ch = 0; ch < totalChannels; ++ch)
                            sysexIndex_.push_back({
                                getRolandPart(addr, ch),
                                &obj,
                                ch
                            });

                    } else {
                        sysexIndex_.push_back({
                            addr,
                            &obj,
                            -1
                        });
                    }
                } else if (obj.data) {
                    std::vector<std::optional<uint8_t>> pattern;
                    std::istringstream iss(*obj.data);
                    std::string tok;
                    while (iss >> tok) {
                        if (tok == "[VAL]")
                            pattern.push_back(std::nullopt);
                        else
                            pattern.push_back(static_cast<uint8_t>(std::stoul(tok, nullptr, 16)));
                    }
                    dataIndex_.push_back({ std::move(pattern), &obj });
                }
                
            } else if (obj.type == kind::Patch) {
                if (!defaultPatchObject_)
                    defaultPatchObject_ = &obj;

                patchBinding pb;
                pb.obj = &obj;
                pb.hasMsb     = std::find(obj.sequence.begin(), obj.sequence.end(), "cc0")  != obj.sequence.end();
                pb.hasLsb     = std::find(obj.sequence.begin(), obj.sequence.end(), "cc32") != obj.sequence.end();
                pb.hasProgram = std::find(obj.sequence.begin(), obj.sequence.end(), "pc")   != obj.sequence.end();
                patchIndex_.push_back(pb);

            if (!obj.patchSysexParts.empty()) {
                for (const auto& part : obj.patchSysexParts) {
                    if (obj.patchSysexParts.size() == 1 && part.channel == 0 && part.msb) {
                        const auto baseMsb = hexToBytes(*part.msb);
                        const auto baseLsb = part.lsb ? hexToBytes(*part.lsb) : baseMsb;
                        const auto basePrg = part.program ? hexToBytes(*part.program) : baseMsb;

                        for (int ch = 0; ch < totalChannels; ++ch) {
                            if (part.msb)
                                patchSysexIndex_.push_back({getRolandPart(baseMsb, ch), &obj, patchField::Msb, ch});
                            if (part.lsb)
                                patchSysexIndex_.push_back({getRolandPart(baseLsb, ch), &obj, patchField::Lsb, ch});
                            if (part.program)
                                patchSysexIndex_.push_back({getRolandPart(basePrg, ch), &obj, patchField::Program, ch});
                        }
                    } else {
                        // Handle explicit multi-channel definitions if provided
                        if (part.msb)
                            patchSysexIndex_.push_back({hexToBytes(*part.msb), &obj, patchField::Msb, part.channel});
                        if (part.lsb)
                            patchSysexIndex_.push_back({hexToBytes(*part.lsb), &obj, patchField::Lsb, part.channel});
                        if (part.program)
                            patchSysexIndex_.push_back({hexToBytes(*part.program), &obj, patchField::Program, part.channel});
                    }
                }
            }
        }
    }

    for (const auto& b : patchSysexIndex_) {
        std::ostringstream ss;

        for (auto x : b.addr)
            ss << std::hex
               << std::uppercase
               << std::setw(2)
               << std::setfill('0')
               << (int)x << ' ';

    }

    for (const auto& [name, dict] : dictionary_.enums) {
        auto& lookup = enumLookup_[name];

        for (const auto& item : dict.values)
            lookup[item.id] = item.name;
    }

    // Sort once here instead of scanning every entry for every incoming SysEx message
    std::sort(sysexIndex_.begin(), sysexIndex_.end(),
        [](const sysexBinding& a, const sysexBinding& b) {
            return toAddrNum(a.addr) < toAddrNum(b.addr);
        });

    for (const auto& b : sysexIndex_) {
        if (b.obj->id == "rhythm_part") {
            std::ostringstream ss;
            ss << std::hex << std::uppercase
            << std::setw(6) << std::setfill('0')
            << toAddrNum(b.addr);

            log("Indexed rhythm addr=", ss.str(),
                " ch=", b.channel);
        }
    }

    std::sort(patchSysexIndex_.begin(), patchSysexIndex_.end(),
        [](const patchSysexBinding& a, const patchSysexBinding& b) {
            return toAddrNum(a.addr) < toAddrNum(b.addr);
        });

    for (const auto& obj : module_.objects) {
        log("Object ", obj.id, " type=", (int)obj.type);

        if (!obj.parts.empty()) {
            for (const auto& p : obj.parts)
                log("  part ", p.channel, " addr=", p.address);
        }
    }
}


// Fixed-format SysEx (Master Volume, GS/XG resets, etc...)
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
            value = (wildcards[1] << 7) | wildcards[0];
        } else if (wildcards.size() == 1) {
            value = wildcards[0];
        }

        sysCh.rawValues[binding.obj->id] = value;

        // log("[DATA TEMPLATE] matched ", binding.obj->id, " value=", value);
        return true;
    }
    return false;
}


void stateLayer::advance(double elapsedMs) {
    std::vector<RawEvent> batch;

    if (reader_.backlog(elapsedMs, batch)) {
        for (const auto& ev : batch)
            eventHandler(ev);
    }
}



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

void stateLayer::setPatch(channelState& targetCh, const ModuleObject& obj, patchField field, int value) {
    
    auto [it, patchInserted] = targetCh.patches.try_emplace(obj.id, defaultPatch(obj));

    if (patchInserted)
    {
        for (const auto& object : module_.objects)
        {
            if (object.type == kind::SysEx && object.perPatch)
                it->second.values.emplace(object.id, initOffset(object));
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

    if (ev.data.size() < 4) return;
    if (ev.data.front() != 0xF0) return;
    if (ev.data.back() != 0xF7) return;

    auto [sysChIt, sysInserted] = channels_.try_emplace(-1);
    if (sysInserted) initCh(sysChIt->second, -1);

    bool handled = dataHandler(ev, sysChIt->second);
    if (handled)
        return;

    if (module_.manufacturer && ev.data[1] != module_.manufacturer) {
        log("RETURN manufacturer");
        return;
    }

    if (module_.deviceId && ev.data[2] != module_.deviceId) {
        log("RETURN device");
        return;
    }

    if (module_.model && ev.data[headerLen_ - 2] != module_.model) {
        log("RETURN model");
        return;
    }

    log("Passed header checks");

    if (module_.checksum == "roland") {
        const size_t checksumIndex = ev.data.size() - 2;

        bool ok = verifyRolandChecksum(ev.data, headerLen_, checksumIndex);

        log("Checksum=", ok);

        if (!ok)
            return;
    }

    std::ostringstream dump;
    for (uint8_t b : ev.data) {
        dump << std::hex
            << std::uppercase
            << std::setw(2)
            << std::setfill('0')
            << (int)b << ' ';
    }

    log("Packet: ", dump.str());

    std::ostringstream modelExpected, modelActual, cmdActual;

    modelExpected << std::hex << std::uppercase << (int)module_.model;
    modelActual   << std::hex << std::uppercase << (int)ev.data[headerLen_ - 2];
    cmdActual     << std::hex << std::uppercase << (int)ev.data[headerLen_ - 1];

    log("Model expected=0x", modelExpected.str(),
        " actual=0x", modelActual.str(),
        " index=", headerLen_ - 2);

    log("Command expected=0x12",
        " actual=0x", cmdActual.str(),
        " index=", headerLen_ - 1);

    if (ev.data[headerLen_ - 1] != 0x12) {
        log("RETURN command");
        return;
    }

    // Dump the three address bytes
    std::ostringstream addrBytes;
    addrBytes << std::hex << std::uppercase
            << std::setw(2) << std::setfill('0') << (int)ev.data[headerLen_] << ' '
            << std::setw(2) << (int)ev.data[headerLen_ + 1] << ' '
            << std::setw(2) << (int)ev.data[headerLen_ + 2];

    log("Address bytes=", addrBytes.str());

    uint32_t msgAddr = 0;

    for (size_t i = headerLen_; i < headerLen_ + addrWidth_; ++i)
        msgAddr = (msgAddr << 8) | ev.data[i];

    int msgDataLen = static_cast<int>(ev.data.size()) - headerLen_ - addrWidth_ - 2;

    if (msgDataLen <= 0)
        return;

    std::ostringstream addr;
    addr << std::hex
        << std::uppercase
        << std::setw(6)
        << std::setfill('0')
        << msgAddr;

    log("Searching for address ", addr.str(),
        " dataLen=", msgDataLen,
        " indexSize=", sysexIndex_.size());

    auto it = std::lower_bound(
        sysexIndex_.begin(),
        sysexIndex_.end(),
        msgAddr,
        [](const sysexBinding& b, uint32_t addr) {
            return toAddrNum(b.addr) < addr;
        });

    if (it == sysexIndex_.end()) {
        log("lower_bound -> end (nothing indexed at or above this address)");
    } else {
        std::ostringstream found;
        found << std::hex
            << std::uppercase
            << std::setw(6)
            << std::setfill('0')
            << toAddrNum(it->addr);

        log("lower_bound landed on ",
            found.str(),
            " obj=", it->obj->id,
            " ch=", it->channel,
            " (not yet confirmed as an actual match - see range check below)");
    }  

        for (; it != sysexIndex_.end() && toAddrNum(it->addr) < msgAddr + msgDataLen; ++it) {
            const auto& binding = *it;
            uint32_t bindAddr = toAddrNum(binding.addr);

            const ModuleObject* obj = binding.obj;
            const int valueBytes = obj->bytes.value_or(1);

            if (bindAddr >= msgAddr && bindAddr + valueBytes <= msgAddr + msgDataLen) {
                log("MATCHED ", obj->id, " ch=", binding.channel, " at address ", hexAddr(bindAddr));

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

                log("MATCH ",
                obj->id,
                " ch=", binding.channel,
                " value=", value);

                if (obj->controlsRhythm) {
                    if (obj->controlsRhythm)


                    targetCh.rhythmFromSysEx = (value != 0);
                    targetCh.rhythmFromBank = false;
                }
            }
        }

    // log("Entering patch scan");
    auto patchIt = std::lower_bound(patchSysexIndex_.begin(), patchSysexIndex_.end(), msgAddr,
        [](const patchSysexBinding& b, uint32_t addr) { return toAddrNum(b.addr) < addr; });

    for (; patchIt != patchSysexIndex_.end() && toAddrNum(patchIt->addr) < msgAddr + msgDataLen; ++patchIt) {
        const auto& binding = *patchIt;
        uint32_t bindAddr = toAddrNum(binding.addr);

        if (bindAddr >= msgAddr && bindAddr < (msgAddr + msgDataLen)) {
            int offset = bindAddr - msgAddr;
            size_t valueStart = headerLen_ + addrWidth_ + offset;

            auto [chIt, inserted] = channels_.try_emplace(binding.channel);
            channelState& targetCh = chIt->second;
            if (inserted) initCh(targetCh, binding.channel);

            setPatch(targetCh, *binding.obj, binding.field, ev.data[valueStart]);

            // log("[PATCH SYSEX] matched ",
            //     binding.obj->id,
            //     " field=", (int)binding.field,
            //     " ch=", binding.channel,
            //     " value=", ev.data[valueStart]);
        }
    }
}



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



void stateLayer::updtBank(channelState& ch, int channel, int ccNum, int value)
{
    for (const auto& pb : patchIndex_)
    {
        if (ccNum == 0)
        {
            if (pb.hasMsb)
                setPatch(ch, *pb.obj, patchField::Msb, value);

            if (pb.obj->drumBankMsb)
            {
                const auto& list = *pb.obj->drumBankMsb;
                const int block = reader_.midChannels();
                ch.rhythmFromBank = std::find(list.begin(), list.end(), value) != list.end();
        }

        if (ccNum == 32 && pb.hasLsb)
            setPatch(ch, *pb.obj, patchField::Lsb, value);
        }

        // if (channel == 8)
        // {
        //     log("CH8 CC", ccNum,
        //         " value=", value,
        //         " rhythmFromBank=", ch.rhythmFromBank);
        // }
    }
}



void stateLayer::updtPC(channelState& ch, int program)
{
    for (const auto& pb : patchIndex_)
    {
        if (pb.hasProgram)
            setPatch(ch, *pb.obj, patchField::Program, program);
    }
}



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
                updtPC(ch, ev.data[1]);
            }
            break;

        default:
            break;
    }
}



const channelState* stateLayer::getChannel(int channel) const {
    auto it = channels_.find(channel);

    return it == channels_.end() ? nullptr : &it->second;
}



std::vector<int> stateLayer::activeCh() const {
    std::vector<int> result;
    result.reserve(channels_.size());

    for (const auto& [channel, ch] : channels_)
        result.push_back(channel);

    return result;
}



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

std::optional<std::string> stateLayer::patchLookup(bool isRhythm, const channelState::patchState& patch) const {
    channelState::patchState lookupPatch = patch;

    // GS drum kits don't use the incoming Bank MSB, force to 0
    if (module_.id == "gs" && isRhythm)
        lookupPatch.msb = 0;

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
        // GS-only: SC-8850 > SC-88Pro > SC-88 > SC-55 fallback. lsb=0 loops back to 4.
        static const int tiers[] = {4, 3, 2, 1};
        const int startTier = (patch.lsb == 0) ? 4 : patch.lsb;
        for (int tier : tiers) {
            if (tier > startTier) continue;
            if (auto name = tryGroup(groupKey, tier)) return name;
        }
    } else {
        // Every other module
        if (auto name = tryGroup(groupKey, patch.lsb)) return name;
        if (auto name = tryGroup("patches", patch.lsb)) return name;
    }

    // log(
    //     "Lookup failed: rhythm=", isRhythm,
    //     " msb=", patch.msb,
    //     " lsb=", patch.lsb,
    //     " program=", patch.program
    // );


    // log("PATCH LOOKUP FAILED",
    // " rhythm=", isRhythm,
    // " msb=", lookupPatch.msb,
    // " lsb=", patch.lsb,
    // " program=", patch.program);

    return std::nullopt;
}


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

    return mathVal(*objIt->second, valueIt->second);
}



void stateLayer::updtNote(channelState& ch, int note, int velocity, bool on) {
    if (on) {
        ch.activeNotes[note] = velocity;
    } else {
        ch.activeNotes.erase(note);
    }

    ch.lastNote = note;
    ch.lastVelo = velocity;
}



takeSnapshot stateLayer::snapshot(int channel) const {
    takeSnapshot snap;

    const channelState* ch = getChannel(channel);

    if (!ch) return snap;

    for (const auto& [id, raw] : ch->rawValues) {
        auto objIt = objectById_.find(id);

        if (objIt == objectById_.end()) continue;

        snap.values[id] = mathVal(*objIt->second, raw);
    }
    
    for (const auto& [patchId, patch] : ch->patches)
    {
        for (const auto& [objId, raw] : patch.values)
        {
            auto obj = objectById_.find(objId);

            if (obj == objectById_.end())
                continue;

            snap.values[objId] = mathVal(*obj->second, raw);
        }

        // log(
        //     "[PATCH STATE] ",
        //     patchId,
        //     " msb=", patch.msb,
        //     " lsb=", patch.lsb,
        //     " pc=", patch.program,
        //     " values=", patch.values.size()
        // );

        const bool rhythm =
            ch->rhythmFromSysEx ||
            ch->rhythmFromBank;

        // if (channel == 8)
        // {
        //     log("CH8",
        //         " bank=", ch->rhythmFromBank,
        //         " sysex=", ch->rhythmFromSysEx,
        //         " final=", rhythm,
        //         " msb=", patch.msb,
        //         " lsb=", patch.lsb,
        //         " pc=", patch.program);
        // }

            

        snap.patchNames[patchId] =
            patchLookup(rhythm, patch);
    }

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
    }

    // log("===== SNAPSHOT =====");

    // for (const auto& [id, value] : snap.values)
    //     log(id, " = ", value);

    // for (const auto& [id, name] : snap.patchNames)
    //     log(id, " -> ", name.value_or("<none>"));

    return snap;
}
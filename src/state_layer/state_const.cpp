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

// STATE_CONSTRUCT.CPP
//
// Initializes all the indexes and default values at startup

void stateLayer::initCh(channelState& ch, int channel) {
    const bool isSysBucket = (channel == -1);

    if (!isSysBucket) {
        if (channel < 0) return;

        ch.rhythmFromSysEx = false;
        ch.rhythmFromBank  = false;

        if (defaultPatchObject_ && defaultPatchObject_->rhythmChannel) {
            const int rhythmCh0Based = *defaultPatchObject_->rhythmChannel - 1;
            const int block = reader_.midChannels();
            if (block > 0 && (channel % block == rhythmCh0Based))
                ch.rhythmFromBank = true;
        }
    }

    for (const auto& obj : module_.objects) {
        if (obj.type == kind::SysEx) {
            if (isSysBucket && obj.parts.empty() && !obj.address) {
                ch.rawValues.try_emplace(obj.id, initOffset(obj, channel));

            } else if (!isSysBucket && !obj.parts.empty()) {
                if (obj.parts.size() == 1 && obj.parts.front().channel == 0) {
                    ch.rawValues.try_emplace(obj.id, initOffset(obj, channel));
                } else {
                    for (const auto& part : obj.parts) {
                        if (part.channel == channel) {
                            ch.rawValues.try_emplace(obj.id, initOffset(obj, channel));
                            break;
                        }
                    }
                }

            } else if (!isSysBucket && obj.address && obj.parts.empty()) {
                auto addr = hexToBytes(*obj.address);
                if (isRolandPart(addr)) {
                    ch.rawValues.try_emplace(obj.id, initOffset(obj, channel));
                }
            }

        } else if (obj.type == kind::Patch) {
            if (!isSysBucket) {
                auto& patch = ch.patches.try_emplace(obj.id, defaultPatch(obj)).first->second;

                for (const auto& object : module_.objects) {
                    if (object.type == kind::SysEx && object.perPatch)
                        patch.values.emplace(object.id, initOffset(object, channel));
                }
            }
        } else {
            if (!isSysBucket)
                ch.rawValues.try_emplace(obj.id, initOffset(obj, channel));
        }

        if (isSysBucket && (obj.address || obj.data)) {
            if (obj.address) {
                auto addr = hexToBytes(*obj.address);
                if (!(isRolandPart(addr))) {
                    ch.rawValues.try_emplace(obj.id, initOffset(obj, channel));
                }
            } else if (obj.data) {
                ch.rawValues.try_emplace(obj.id, initOffset(obj, channel));
            }
        }
    }

    if (ch.rhythmFromBank) {
        logDbg("[state_const] Channel %d set to Rhythm mode from bank defaults", channel);
        for (const auto& obj : module_.objects) {
            if (obj.controlsRhythm) {
                ch.rawValues[obj.id] = 1; 
            }
        }
    }
}

// Constructs state layer and builds enum lookups with full names
stateLayer::stateLayer(const moduleDef& module, const dictionaryDef& dictionary, MidiReader& reader)
    : module_(module), dictionary_(dictionary), reader_(reader),
      headerLen_(static_cast<size_t>(module.headerLen)),
      addrWidth_(static_cast<size_t>(module.addressWidth)) {

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

    // Assigns lookups in module.json to dictionary.json (Uses FULL NAME for now)
    for (const auto& [name, dict] : dictionary_.enums) {
        auto& lookup = enumLookup_[name];

        for (const auto& item : dict.values)
            lookup[item.id] = item.name;
    }

    std::sort(sysexIndex_.begin(), sysexIndex_.end(),
        [this](const sysexBinding& a, const sysexBinding& b) {
            return toAddrNum(a.addr) < toAddrNum(b.addr);
        });

    std::sort(patchSysexIndex_.begin(), patchSysexIndex_.end(),
        [this](const patchSysexBinding& a, const patchSysexBinding& b) {
            return toAddrNum(a.addr) < toAddrNum(b.addr);
        });

    for (int ch = 0; ch < totalChannels; ++ch) {
        auto [it, inserted] = channels_.try_emplace(ch);
        if (inserted)
            initCh(it->second, ch);
    }

    auto [sys, inserted] = channels_.try_emplace(-1);
    if (inserted)
        initCh(sys->second, -1);

    logDbg("[state_const] stateLayer initialized for '%s': %zu objects, %zu sysex bindings, %zu patch-sysex bindings",
        module_.name.c_str(), objectById_.size(), sysexIndex_.size(), patchSysexIndex_.size());
}
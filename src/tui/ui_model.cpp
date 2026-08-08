#include "ui_model.hpp"
#include "../log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

// UI_MODEL.CPP
//
// Gets the state the TUI reads from, calculates VU meter physics, the event log, settings


// INIT & SETUP
//
//

void UiModel::init(const layoutDef& layouts, const moduleDef& module) {
    layouts_ = layouts;
    module_ = module;
    activeProfileName_ = "full";
    //selChannel_ = 0
    currentPage_ = 0;

    // Automatically load settings.toml on startup
    settings_ = loadSettings("settings.toml");

    objectMap_.clear();
    for (const auto& obj : module_.objects) {
        objectMap_[obj.id] = &obj;
    }
}

// void UiModel::selNextCh() {
//     int maxCh = activeProfile().geometry.pageSize;
//     if (maxCh > 0) {
//         selChannel_ = (selChannel_ + 1) % maxCh;
//     }
// }

// void UiModel::selPrevCh() {
//     int maxCh = activeProfile().geometry.pageSize;
//     if (maxCh > 0) {
//         selChannel_ = (selChannel_ - 1 + maxCh) % maxCh;
//     }
// }

// void UiModel::giveSelChannel(int ch) {
//     int maxCh = activeProfile().geometry.pageSize;
//     if (ch >= 0 && ch < maxCh) {
//         selChannel_ = ch;
//     }
// }

void UiModel::setTotalChannels(int totalChannels) {
    totalChannels_ = totalChannels;
}

// PAGE NAVIGATION
//
// currentPage_ picks which set of 16 channels is sent to getChRows

int UiModel::totalPages() const {
    const auto& profile = activeProfile();
    const int pageSize = (profile.geometry.pageSize > 0) ? profile.geometry.pageSize : 16;

    int maxCh = std::max(16, totalChannels_);

    for (const auto& [ch, snap] : snapshots_) {
        if (ch >= 0) {
            maxCh = std::max(maxCh, ch + 1);
        }
    }

    for (const auto& [ch, time] : lastActiveMs_) {
        if (ch >= 0) {
            maxCh = std::max(maxCh, ch + 1);
        }
    }

    return (maxCh + pageSize - 1) / pageSize;
}

void UiModel::selNextPage() {
    int total = totalPages();
    if (total > 0) {
        currentPage_ = (currentPage_ + 1) % total;
        logDbg("[ui_model] Active view page switched to Page %d of %d", currentPage_ + 1, total);
    }
}

void UiModel::selPrevPage() {
    int total = totalPages();
    if (total > 0) {
        currentPage_ = (currentPage_ - 1 + total) % total;
    }
}

// PROFILE SELECTION
//
// 

std::string UiModel::selProfile(int width, int height) const {
    if (layouts_.variants.empty()) return "full";

    std::string bestFit = "";
    int maxArea = -1;

    for (const auto& [name, profile] : layouts_.variants) {
        const auto& geo = profile.geometry;
        if (width >= geo.minWidth && height >= geo.minHeight) {
            int area = geo.minWidth * geo.minHeight;
            if (area > maxArea) {
                maxArea = area;
                bestFit = name;
            }
        }
    }

    if (bestFit.empty()) {
        return "too_small";
    }

    return bestFit;
}

void UiModel::updtSize(int width, int height) {
    termWidth_ = width;
    termHeight_ = height;
    std::string newProfile = selProfile(width, height);

    if (newProfile != activeProfileName_) {
        logDbg("[ui_model] Terminal size changed to %dx%d. Switching layout profile: '%s' -> '%s'",
               width, height, activeProfileName_.c_str(), newProfile.c_str());
    }

    activeProfileName_ = newProfile;
}

const layoutType& UiModel::activeProfile() const {
    static const layoutType dummyProfile{};
    if (layouts_.variants.empty()) return dummyProfile;

    auto it = layouts_.variants.find(activeProfileName_);
    if (it != layouts_.variants.end()) return it->second;

    return layouts_.variants.begin()->second;
}

// FIELD FORMATTING
//
//

std::string UiModel::formatDefaultField(const std::string& fieldId) const {
    auto it = objectMap_.find(fieldId);
    if (it == objectMap_.end()) return "0";

    const auto& obj = *it->second;

    if (obj.defaultValue) {
        return std::to_string(*obj.defaultValue);
    }

    if (obj.displayOffset.transform == "pan_lcr") return "C";
    if (obj.displayOffset.transform == "pitchbend") return "0c";
    if (obj.displayOffset.transform == "frequency") return "440.0 Hz";

    return "0";
}

// PADDING FORMATTING
//
//

std::string UiModel::formatField(int ch, const std::string& fieldId, const takeSnapshot& snap) const {
    // Pitch Bend formatted in cents with sign
    if (fieldId == "pitch") {
        if (settings_.numeralFormat == NumeralFormat::Hex) {
            auto rawIt = snap.rawValues.find("pitch");
            if (rawIt != snap.rawValues.end()) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%04X", rawIt->second);
                return std::string(buf);
            }
        }

        auto valIt = snap.values.find("pitch");
        if (valIt != snap.values.end()) {
            try {
                int raw = std::stoi(valIt->second);
                // Pitch bend is clamped to +/- 1200 cents
                int cents = static_cast<int>((raw * 1200.0) / 4096.0);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%+4dc", cents);
                return std::string(buf);
            } catch (...) {
                return valIt->second + "c";
            }
        }
        return "   +0c";
    }

    // Pan LCR dormatting
    if (fieldId == "pan") {
        if (settings_.numeralFormat == NumeralFormat::Hex) {
            auto rawIt = snap.rawValues.find("pan");
            if (rawIt != snap.rawValues.end()) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%02X", rawIt->second);
                return std::string(buf);
            }
        }

        auto valIt = snap.values.find("pan");
        if (valIt != snap.values.end()) {
            std::string rawStr = valIt->second;
            if (rawStr == "C") return " C  ";
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%4s", rawStr.c_str());
            return std::string(buf);
        }
        return " C  ";
    }

    // Parameter integer formating
    if (fieldId == "volume" || fieldId == "cutoff" || fieldId == "resonance" ||
        fieldId == "attack" || fieldId == "decay" || fieldId == "release" ||
        fieldId == "chorus_send" || fieldId == "reverb_send" || fieldId == "expression" ||
        fieldId == "modulation" || fieldId == "sustain" || fieldId == "vibrato_depth" ||
        fieldId == "vibrato_rate" || fieldId == "vibrato_delay" || fieldId == "portamento_time" ||
        fieldId == "portamento_switch" || fieldId == "part_tune" || fieldId == "tune") {
        
        if (settings_.numeralFormat == NumeralFormat::Hex) {
            auto rawIt = snap.rawValues.find(fieldId);
            if (rawIt != snap.rawValues.end()) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%02X", rawIt->second);
                return std::string(buf);
            }
        }

        auto valIt = snap.values.find(fieldId);
        if (valIt != snap.values.end()) {
            try {
                int val = std::stoi(valIt->second);
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%3d", val);
                return std::string(buf);
            } catch (...) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%3s", valIt->second.c_str());
                return std::string(buf);
            }
        }
        return "  0";
    }

    // Dynamic patch column formatting
    if (fieldId == "patch") {
        auto patchIt = snap.patchNames.find("patch");
        std::string patchName = (patchIt != snap.patchNames.end() && patchIt->second) 
                                ? *patchIt->second 
                                : "---";

        std::string bankName = "";
        auto bankIt = snap.values.find("bank_name");
        if (bankIt != snap.values.end() && !bankIt->second.empty()) {
            bankName = bankIt->second;
        }

        int pc = 1;
        int msb = 0;
        int lsb = 0;

        // Each of these can be missing on a channel that hasn't had a program change yet sooo
        auto pcIt = snap.values.find("program");
        if (pcIt != snap.values.end()) {
            try { pc = std::stoi(pcIt->second) + 1; } catch (...) {}
        }

        auto msbIt = snap.values.find("msb");
        if (msbIt != snap.values.end()) {
            try { msb = std::stoi(msbIt->second); } catch (...) {}
        }

        auto lsbIt = snap.values.find("lsb");
        if (lsbIt != snap.values.end()) {
            try { lsb = std::stoi(lsbIt->second); } catch (...) {}
        }

        std::string tag = "";
        std::string modId = module_.id;

        // A settings override wins over the SysEx-sniffed module id
        if (settings_.moduleOverride != ModuleOverride::Auto) {
            switch (settings_.moduleOverride) {
                case ModuleOverride::SD90: modId = "sd90"; break;
                case ModuleOverride::GS:   modId = "gs";   break;
                case ModuleOverride::XG:   modId = "xg";   break;
                case ModuleOverride::GM2:  modId = "gm2";  break;
                default: break;
            }
        }

        if (modId == "gs") {
            switch (lsb) {
                case 1:  tag = "55"; break;
                case 2:  tag = "88"; break;
                case 3:  tag = "88Pro"; break;
                case 4:  tag = "8850"; break;
                default: tag = "GS"; break;
            }
        } else if (modId == "gm2") {
            tag = "GM2";
        } else if (modId == "xg") {
            if (msb == 126 || bankName.find("SFX") != std::string::npos) tag = "XG SFX";
            else tag = "XG";
        } else if (modId == "sd90" || modId == "sd-90") {
            if (!bankName.empty()) tag = bankName;
            else tag = "CONTEM";
        } else {
            tag = !bankName.empty() ? bankName : module_.name;
        }

        // Rhythm channels get an "R " prefix, unless the tag already implies it
        if (snap.isRhythm && tag.rfind("R ", 0) != 0 && tag.rfind("R CONT", 0) != 0 && tag.rfind("R CLAS", 0) != 0 && tag.rfind("R SOLO", 0) != 0 && tag.rfind("R ENHA", 0) != 0) {
            tag = "R " + tag;
        }

        std::string bracketTag = "[" + tag + "]";
        char tagPadded[16];
        std::snprintf(tagPadded, sizeof(tagPadded), "%-10s", bracketTag.c_str());

        // Uses parser metadata from module.json
        bool useMsbForVar = false;

        auto patchObjIt = std::find_if(module_.objects.begin(), module_.objects.end(),
            [](const ModuleObject& o) { return o.type == kind::Patch; });

        if (patchObjIt != module_.objects.end()) {
            const auto& patchObj = *patchObjIt;

            // Check if bank_select.msb or bank_select.lsb is labeled variation
            if (patchObj.bankSelectMsbLabel && *patchObj.bankSelectMsbLabel == "variation") {
                useMsbForVar = true;
            } else if (patchObj.bankSelectLsbLabel && *patchObj.bankSelectLsbLabel == "variation") {
                useMsbForVar = false;
            } 
            // Fallback, check sequence token order in module.json
            else {
                const auto& seq = patchObj.sequence;
                auto it0 = std::find(seq.begin(), seq.end(), "cc0");
                auto it32 = std::find(seq.begin(), seq.end(), "cc32");

                if (it0 != seq.end() && (it32 == seq.end() || it0 < it32)) {
                    useMsbForVar = true;
                }
            }
        }

        int varVal = useMsbForVar ? msb : lsb;

        char varBuf[16];
        // 0 means "no variation selected" so I just set it  to --- to avoid particular cases, like the SD-90 special sets
        if (varVal == 0) {
            std::snprintf(varBuf, sizeof(varBuf), "---");
        } else {
            std::snprintf(varBuf, sizeof(varBuf), "%03d", varVal);
        }

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s %03d %s %s", tagPadded, pc, varBuf, patchName.c_str());

        return std::string(buf);
    }

    auto valIt = snap.values.find(fieldId);
    if (valIt != snap.values.end()) {
        return valIt->second;
    }

    return formatDefaultField(fieldId);
}

// CHANNEL ROWS
//
//

std::vector<std::string> UiModel::getColHeader() const {
    std::vector<std::string> headers;
    const auto& profile = activeProfile();

    auto it = profile.views.find("channels");
    if (it == profile.views.end()) return headers;

    for (const auto& col : it->second.columns) {
        headers.push_back(col.label);
    }

    return headers;
}

std::vector<ChRow> UiModel::getChRows() const {
    std::vector<ChRow> rows;
    const auto& profile = activeProfile();

    auto viewIt = profile.views.find("channels");
    if (viewIt == profile.views.end()) return rows;

    const auto& columns = viewIt->second.columns;
    const int pageSize = (profile.geometry.pageSize > 0) ? profile.geometry.pageSize : 16;
    // Gets just the page channel range
    const int startCh = currentPage_ * pageSize;
    const int endCh = startCh + pageSize;

    for (int ch = startCh; ch < endCh; ++ch) {
        ChRow row;
        row.channelId = ch;
        // row.isSelected = ((ch - startCh) == selChannel_);

        auto snapIt = snapshots_.find(ch);
        row.hasData = (snapIt != snapshots_.end());
        if (snapIt != snapshots_.end()) {
            const auto& snap = snapIt->second;
            row.activeNotes = snap.polyCount;

            row.heldNotes = snap.activeNotes; // Snapshots active notes

            // Debounced rather than a raw polyCount>0 check to avoid something like a strummed chord killing poly
            bool playingNow = channelRinging(ch, lastElapsedMs_);
            bool recentlyActive = false;
            if (auto laIt = lastActiveMs_.find(ch); laIt != lastActiveMs_.end()) {
                recentlyActive = (lastElapsedMs_ - laIt->second) < settings_.highlightTimeoutMs;
            }
            row.isActive = playingNow || recentlyActive;

            for (const auto& col : columns) {
                ChCell cell;

                std::string joinedVal;
                size_t fieldCount = (col.label == "PITCH") ? 1 : col.fields.size();

                for (size_t i = 0; i < fieldCount; ++i) {
                    joinedVal += formatField(ch, col.fields[i], snap);
                    if (i + 1 < fieldCount) joinedVal += col.join;
                }

                cell.text = joinedVal;
                cell.width = static_cast<int>(joinedVal.length());
                row.cells.push_back(cell);
            }
        } else {
            // No snapshot yet so just initialize all the parameters beforehand
            takeSnapshot dummySnap;

            for (const auto& col : columns) {
                ChCell cell;

                std::string joinedVal;
                size_t fieldCount = (col.label == "PITCH") ? 1 : col.fields.size();

                for (size_t i = 0; i < fieldCount; ++i) {
                    joinedVal += formatField(ch, col.fields[i], dummySnap);
                    if (i + 1 < fieldCount) joinedVal += col.join;
                }

                cell.text = joinedVal;
                cell.width = static_cast<int>(joinedVal.length());
                row.cells.push_back(cell);
            }
        }

        rows.push_back(row);
    }

    return rows;
}

// SYSTEM & MASTER OBJECTS
//
//

std::vector<std::pair<std::string, std::string>> UiModel::getSystemFx() const {
    std::vector<std::pair<std::string, std::string>> fxList;
    const auto& profile = activeProfile();

    auto it = profile.views.find("system_fx");
    if (it == profile.views.end()) return fxList;

    for (const auto& col : it->second.columns) {
        for (const auto& field : col.fields) {
            auto valIt = sysSnapshot_.values.find(field);
            if (valIt == sysSnapshot_.values.end()) valIt = sysSnapshot_.values.find(field + "_type");
            if (valIt == sysSnapshot_.values.end()) valIt = sysSnapshot_.values.find(field + "_send");

            std::string val = (valIt != sysSnapshot_.values.end()) ? valIt->second : formatDefaultField(field);
            fxList.push_back({col.label, val}); // Passes the parsed label to the UI
        }
    }
    return fxList;
}

std::vector<std::pair<std::string, std::string>> UiModel::getMasterOut() const {
    std::vector<std::pair<std::string, std::string>> outList;
    const auto& profile = activeProfile();

    auto it = profile.views.find("master_output");
    if (it == profile.views.end()) return outList;

    for (const auto& col : it->second.columns) {
        for (const auto& field : col.fields) {
            auto valIt = sysSnapshot_.values.find(field);
            if (valIt == sysSnapshot_.values.end()) valIt = sysSnapshot_.values.find(field + "_type");
            if (valIt == sysSnapshot_.values.end()) valIt = sysSnapshot_.values.find(field + "_send");

            std::string val = (valIt != sysSnapshot_.values.end()) ? valIt->second : formatDefaultField(field);
            outList.push_back({col.label, val}); // Passes the parsed label to the UI
        }
    }
    return outList;
}

// VU METER & NOTE-OFF DEBOUNCE
//
//

// "Still counts as held" check
bool UiModel::channelRinging(int channel, double nowMs) const {
    auto it = lastActiveMs_.find(channel);
    if (it == lastActiveMs_.end()) return false;
    return (nowMs - it->second) < settings_.noteOffGraceMs;
}

// Uses the previous function to freeze the meter target instead of snapping to 0
void UiModel::pushSnap(int channel, const takeSnapshot& snap, double elapsedMs) {
    // lastElapsedMs_ is clock used for the channel activity
    lastElapsedMs_ = std::max(lastElapsedMs_, elapsedMs);

    if (channel == -1) {
        sysSnapshot_ = snap;
        totalPoly_ = snap.polyCount;
        return;
    }

    snapshots_[channel] = snap;

    if (snap.polyCount > 0) {
        lastActiveMs_[channel] = elapsedMs;
    }

    // VU meter math
    MeterState& m = meters_[channel];

    // Deltatime here, caused some issues in main because of the way it sent snapshots before
    double dt = (m.lastUpdateMs >= 0.0) ? std::max(0.0, elapsedMs - m.lastUpdateMs) : 0.0;
    m.lastUpdateMs = elapsedMs;

    float volNorm = 1.0f;
    if (auto it = snap.values.find("volume"); it != snap.values.end()) {
        try {
            volNorm = std::clamp(std::stof(it->second) / 127.0f, 0.0f, 1.0f);
        } catch (...) {}
    }

    // veloNorm lsitens to the most recent Note On's velocity
    float veloNorm = std::clamp(static_cast<float>(snap.lastVelo) / 127.0f, 0.0f, 1.0f);
    // Denser chords read a little louder, capped
    float polyBoost = std::clamp(1.0f + 0.08f * static_cast<float>(std::max(0, snap.polyCount - 1)), 1.0f, 1.6f);

    bool notesHeld = snap.polyCount > 0;
    float target = notesHeld
        ? std::clamp(volNorm * veloNorm * polyBoost * 100.0f, 0.0f, 100.0f)
        : 0.0f;

    if (!notesHeld && channelRinging(channel, elapsedMs)) {
        // Grace window for more precision in the note releases
        target = m.level;
    }

    // Attack is much faster than decay
    constexpr float kAttackPerMs    = 0.9f;
    constexpr float kDecayPerMs     = 0.06f;
    constexpr double kPeakHoldMs    = 800.0;
    constexpr float kPeakDecayPerMs = 0.03f;

    if (target > m.level) {
        m.level = std::min(target, m.level + kAttackPerMs * static_cast<float>(dt));
    } else {
        m.level = std::max(target, m.level - kDecayPerMs * static_cast<float>(dt));
    }

    // Peak marker, just an extra addition
    if (m.level >= m.peak) {
        m.peak = m.level;
        m.peakHeldMs = 0.0;
    } else {
        m.peakHeldMs += dt;
        if (m.peakHeldMs > kPeakHoldMs) {
            m.peak = std::max(m.level, m.peak - kPeakDecayPerMs * static_cast<float>(dt));
        }
    }
}

float UiModel::meterLevel(int channel) const {
    auto it = meters_.find(channel);
    return it != meters_.end() ? it->second.level : 0.0f;
}

float UiModel::meterPeak(int channel) const {
    auto it = meters_.find(channel);
    return it != meters_.end() ? it->second.peak : 0.0f;
}

// EVENT LOG & ACTIVITY BINS
//
//

void UiModel::pushEvent(const RawEvent& ev, double elapsedMs) {
    // Skip Note On and Note Off events so they don't clutter the event log
    if (ev.kind == MsgKind::NoteOn || ev.kind == MsgKind::NoteOff) {
        return;
    }

    LogEntry entry;

    int totalSec = static_cast<int>(elapsedMs / 1000.0);
    int hrs = totalSec / 3600; // Just in case, hours included
    int mins = (totalSec % 3600) / 60;
    int secs = totalSec % 60;

    char timeBuf[32];
    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", hrs, mins, secs);
    entry.timecode = timeBuf;

    char textBuf[128];
    int chNum = (ev.channel >= 0) ? (ev.channel + 1) : 0;

    switch (ev.kind) {
        case MsgKind::Meta:
            // BPM change event
            if (ev.data.size() >= 6 && ev.data[0] == 0xFF && ev.data[1] == 0x51) {
                entry.type = "tempo";
                uint32_t mpqn = (static_cast<uint32_t>(ev.data[3]) << 16) |
                                (static_cast<uint32_t>(ev.data[4]) << 8)  |
                                 static_cast<uint32_t>(ev.data[5]);
                if (mpqn > 0) {
                    double bpm = 60000000.0 / static_cast<double>(mpqn);
                    std::snprintf(textBuf, sizeof(textBuf), "Tempo Change -> %.2f BPM", bpm);
                } else {
                    std::snprintf(textBuf, sizeof(textBuf), "Tempo Change Event");
                }
            } 
            // Time signature event
            else if (ev.data.size() >= 5 && ev.data[0] == 0xFF && ev.data[1] == 0x58) {
                entry.type = "timesig";
                int num = ev.data[3];
                int denom = 1 << ev.data[4];
                std::snprintf(textBuf, sizeof(textBuf), "Time Signature -> %d/%d", num, denom);
            } else {
                return; // Ignore other non-display meta events
            }
            break;

        case MsgKind::CC:
            entry.type = "cc";
            if (ev.data.size() >= 3) {
                std::snprintf(textBuf, sizeof(textBuf), "Ch %02d CC%02d val=%d", 
                              chNum, ev.data[1], ev.data[2]);
            } else {
                std::snprintf(textBuf, sizeof(textBuf), "Ch %02d CC Event", chNum);
            }
            break;

        case MsgKind::ProgramChange:
            entry.type = "pc";
            if (ev.data.size() >= 2) {
                std::snprintf(textBuf, sizeof(textBuf), "Ch %02d Program Change -> %d", 
                              chNum, ev.data[1] + 1);
            } else {
                std::snprintf(textBuf, sizeof(textBuf), "Ch %02d Program Change", chNum);
            }
            break;

        case MsgKind::PitchBend:
            entry.type = "cc";
            if (ev.data.size() >= 3) {
                int pbVal = ((ev.data[2] << 7) | ev.data[1]) - 8192;
                std::snprintf(textBuf, sizeof(textBuf), "Ch %02d Pitch Bend %s%d", 
                              chNum, (pbVal >= 0 ? "+" : ""), pbVal);
            } else {
                std::snprintf(textBuf, sizeof(textBuf), "Ch %02d Pitch Bend", chNum);
            }
            break;

        case MsgKind::SysEx:
            entry.type = "sysex";
            std::snprintf(textBuf, sizeof(textBuf), "SysEx Block (%zu bytes) -> %s", 
                          ev.data.size(), module_.name.c_str());
            break;

        default:
            entry.type = "cc";
            std::snprintf(textBuf, sizeof(textBuf), "Ch %02d Event", chNum);
            break;
    }

    entry.text = textBuf;

    logBuffer_.push_front(entry);

    if (logBuffer_.size() > kMaxLogEntries) {
        logBuffer_.pop_back();
    }

    int binIdx = (ev.channel >= 0) ? (ev.channel % static_cast<int>(activityBins_.size())) : 0;
    activityBins_[binIdx] = std::min(100, activityBins_[binIdx] + 15);
}
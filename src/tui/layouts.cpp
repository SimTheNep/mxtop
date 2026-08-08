#include "layouts.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

using namespace ftxui;
namespace {
    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }
}

// LAYOUTS.CPP
//
// Builds the FTXUI tables from a snapshot, according to the profile (full/compact/tiny)

static std::string formatTimeMs(double ms) {
    int totalSec = static_cast<int>(ms / 1000.0);
    int mins = totalSec / 60;
    int secs = totalSec % 60;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
    return std::string(buf);
}

static std::string leftAlignText(const std::string& str, int width) {
    int len = static_cast<int>(str.length());
    if (len >= width) return str.substr(0, width);
    return " " + str + std::string(width - len - 1, ' ');
}

static std::string centerAlignText(const std::string& str, int width) {
    int len = static_cast<int>(str.length());
    if (len >= width) return str.substr(0, width);
    int totalPad = width - len;
    int padLeft = totalPad / 2;
    int padRight = totalPad - padLeft;
    return std::string(padLeft, ' ') + str + std::string(padRight, ' ');
}

// Manual VU meter bar, eighths of a block character
static const char* kPartialBlocks[8] = {
    "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"
};

// Color depends on the cell's position in the bar, not the current level
static Color vuGradientColor(float posRatio, const ColorPalette& palette) {
    if (posRatio >= 0.85f) return palette.vuHigh;
    if (posRatio >= 0.60f) return palette.vuMid;
    return palette.vuLow;
}

static Element drawVuBar(float level, float peak, int width, bool showPeak, const ColorPalette& palette) {
    level = std::clamp(level, 0.0f, 100.0f);
    peak = std::clamp(peak, 0.0f, 100.0f);
    if (width <= 0) return hbox(Elements{});

    // Exact amount of fill for each cell
    float fillExact = (level / 100.0f) * static_cast<float>(width);
    int filledFull = static_cast<int>(std::floor(fillExact));
    float remainder = fillExact - static_cast<float>(filledFull);
    int partialIdx = static_cast<int>(std::round(remainder * 8.0f)); // 0..8 eighths
    if (partialIdx >= 8) {
        // Rounded to a full cell
        filledFull += 1;
        partialIdx = 0;
    }
    filledFull = std::clamp(filledFull, 0, width);

    // peakPos - 1 so the marker sits past the last filled cell to not overlap it
    int peakPos = static_cast<int>(std::round((peak / 100.0f) * width)) - 1;
    peakPos = std::clamp(peakPos, 0, width - 1);

    Elements segs;
    for (int i = 0; i < width; ++i) {
        float posRatio = (width > 1) ? (static_cast<float>(i) / static_cast<float>(width - 1)) : 0.0f;
        Color segColor = vuGradientColor(posRatio, palette);
        bool isPeakMarker = showPeak && (i == peakPos) && (i >= filledFull);

        if (i < filledFull) {
            segs.push_back(text("█") | color(segColor));
        } else if (i == filledFull && partialIdx > 0) {
            segs.push_back(text(kPartialBlocks[partialIdx - 1]) | color(segColor));
        } else if (isPeakMarker) {
            segs.push_back(text("▏") | color(palette.vuPeakMarker) | bold);
        } else {
            segs.push_back(text("░") | color(palette.textDim));
        }
    }

    return hbox(std::move(segs));
}

// LAYOUT DRAW
//
//

Element LayoutCommon::drawLayout(const UiModel& model, const MidiMeta& meta, double elapsedMs, const std::string& currentTimeStr) {
    const std::string& profile = model.activeProfileName();
    const auto palette = model.palette();

    // Render "Terminal too small" when it's smaller than the tiny UI
    if (profile == "too_small") {
        return vbox({
            filler(),
            hbox({
                filler(),
                text(" Terminal too small ") | bold | color(palette.headerBpm) | border,
                filler()
            }),
            hbox({
                filler(),
                text(" Please enlarge window (" + std::to_string(model.termWidth()) + "x" + std::to_string(model.termHeight()) + ") ") | color(palette.textDim),
                filler()
            }),
            filler()
        }) | bgcolor(palette.background);
    }

    Element layoutContent;

    // Compact layout
    if (profile == "compact") {
        layoutContent = vbox({
            drawHeader(model, meta, elapsedMs),
            drawChTable(model) | xflex,
            filler(),
            drawFooter(model, currentTimeStr)
        });
    } else if (profile == "tiny") { // Tiny layout
        auto rows = model.getChRows();
        Elements leftCol, rightCol;

        const int kVuBarWidth = model.settings().vuBarWidth;
        const bool kShowPeak = model.settings().showPeakMarker;
        size_t leftCount = (rows.size() + 1) / 2; // Balance columns for any page size

        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            char labelBuf[16];
            char portChar = 'A' + static_cast<char>(r.channelId / 16);
            std::snprintf(labelBuf, sizeof(labelBuf), "%c%02d", portChar, (r.channelId % 16) + 1);

            float level = model.meterLevel(r.channelId);
            float peak = model.meterPeak(r.channelId);

            Element labelText = text(labelBuf) | bold;
            if (!r.isActive) {
                labelText = labelText | color(palette.channelInactive);
            } else {
                labelText = labelText | color(palette.channelActive);
            }

            Element rowElem = hbox({
                labelText,
                text(" "),
                drawVuBar(level, peak, kVuBarWidth, kShowPeak, palette),
                filler()
            });

            if (i < leftCount) leftCol.push_back(rowElem);
            else rightCol.push_back(rowElem);
        }

        layoutContent = vbox({
            hbox({
                vbox(std::move(leftCol)) | border | flex,
                vbox(std::move(rightCol)) | border | flex
            }) | flex,
            drawFooter(model, currentTimeStr)
        });
    } else { // Responsive full layout
        Elements logActivityRow;
        if (model.settings().showEventLog) {
            logActivityRow.push_back(
                TuiWidgets::drawEventLog(model) |
                (model.settings().showActivityGrid ? size(WIDTH, EQUAL, model.termWidth() / 2) : xflex)
            );
        }
        if (model.settings().showActivityGrid) {
            logActivityRow.push_back(TuiWidgets::drawActivity(model) | flex);
        }

        Element logActivitySection = logActivityRow.empty()
            ? Element(text(""))
            : (hbox(std::move(logActivityRow)) | size(HEIGHT, LESS_THAN, 10));

        layoutContent = vbox({
            drawHeader(model, meta, elapsedMs),
            filler(), // Spacing gap
            drawChTable(model) | xflex,
            filler(), // Spacing gap
            hbox({
                drawSystemFx(model) | flex,
                drawMasterOutput(model) | flex,
                TuiWidgets::drawSystemLoad(model) | flex
            }) | size(HEIGHT, LESS_THAN, 8),
            filler(), // Spacing gap
            logActivitySection,
            filler(), // Spacing gap
            drawFooter(model, currentTimeStr)
        });
    }

    return layoutContent | bgcolor(palette.background) | color(palette.textPrimary);
}

// HEADER DRAW
//
//

Element LayoutCommon::drawHeader(const UiModel& model, const MidiMeta& meta, double elapsedMs) {
    const auto palette = model.palette();
    std::string timeStr = formatTimeMs(elapsedMs) + " / " + formatTimeMs(meta.totalDurationMs);

    char bpmBuf[32];
    std::snprintf(bpmBuf, sizeof(bpmBuf), "%.2f BPM", meta.bpm);

    char timeSigBuf[16];
    std::snprintf(timeSigBuf, sizeof(timeSigBuf), "%d/%d", meta.timeSigNum, meta.timeSigDenom);

    // This requires nerd fonts
    return hbox({
        text(" 󰝚 " + meta.songTitle + " ") | border | color(palette.headerTitle),
        text(" "),
        text(" 󰎇 " + std::string(bpmBuf) + " ") | border | color(palette.headerBpm),
        text(" " + std::string(timeSigBuf) + " ") | border | color(palette.headerTimeSig),
        text(" " + timeStr + " ") | border | color(palette.headerClock),
        text(" "),
        text(" 󰐊 PLAYING ") | border | color(palette.headerStatus) | bold,
        filler(),
        drawPageSelector(model),
    });
}

// CHANNEL TABLE DRAW
//
//

Element LayoutCommon::drawChTable(const UiModel& model) {
    const auto palette = model.palette();
    auto headers = model.getColHeader();
    auto rows = model.getChRows();

    // Minimum content width
    std::vector<int> colWidths(headers.size(), 0);
    for (size_t i = 0; i < headers.size(); ++i) {
        colWidths[i] = static_cast<int>(headers[i].length());
    }

    for (const auto& r : rows) {
        for (size_t i = 0; i < r.cells.size() && i < colWidths.size(); ++i) {
            colWidths[i] = std::max(colWidths[i], static_cast<int>(r.cells[i].text.length()));
        }
    }

    for (size_t i = 0; i < colWidths.size(); ++i) {
        colWidths[i] += 2; // Padding for the text
    }

    // Expand columns across the total border width
    int chWidth = 5;
    int totalMin = chWidth + 1;
    for (int w : colWidths) {
        totalMin += w + 1; // Account for the separator between them
    }

    int termWidth = model.termWidth();
    if (termWidth > totalMin) {
        int extra = termWidth - totalMin - 4; // -4 leaves room for the outer border
        if (extra > 0 && !colWidths.empty()) {
            int numCols = static_cast<int>(colWidths.size());
            int perColExtra = extra / numCols;
            int remainder = extra % numCols;

            // Spreads evenly
            for (int i = 0; i < numCols; ++i) {
                colWidths[i] += perColExtra + (i < remainder ? 1 : 0);
            }
        }
    }

    // Header row
    Elements headerElements;
    headerElements.push_back(text(centerAlignText("CH", chWidth)) | bold | color(palette.tableHeader) | size(WIDTH, EQUAL, chWidth));

    for (size_t i = 0; i < headers.size(); ++i) {
        headerElements.push_back(separator());
        std::string headerText = (i == 0) ? leftAlignText(headers[i], colWidths[i])
                                           : centerAlignText(headers[i], colWidths[i]);
        headerElements.push_back(text(headerText) | bold | color(palette.tableHeader) | size(WIDTH, EQUAL, colWidths[i]));
    }

    Element headerRow = hbox(std::move(headerElements));

    // Body rows
    Elements rowElements;
    rowElements.push_back(headerRow);
    rowElements.push_back(separator());

    for (size_t rowIdx = 0; rowIdx < rows.size(); ++rowIdx) {
        const auto& r = rows[rowIdx];
        Elements cellElements;

        char chLabel[8];
        char portChar = 'A' + static_cast<char>(r.channelId / 16);
        std::snprintf(chLabel, sizeof(chLabel), "%c%02d", portChar, (r.channelId % 16) + 1);

        Element chText = text(centerAlignText(chLabel, chWidth)) | size(WIDTH, EQUAL, chWidth);
        if (r.isActive) {
            chText = chText | color(palette.channelActive);
        } else {
            chText = chText | color(palette.channelInactive);
        }

        cellElements.push_back(chText);

        for (size_t i = 0; i < r.cells.size(); ++i) {
            const auto& cell = r.cells[i];
            int width = (i < colWidths.size()) ? colWidths[i] : cell.width;

            cellElements.push_back(separator());

            std::string textContent = cell.text;
            if (static_cast<int>(textContent.length()) > width) {
                textContent = textContent.substr(0, width);
            }

            std::string aligned = (i == 0) ? leftAlignText(textContent, width)
                                            : centerAlignText(textContent, width);
            Element cellText = text(aligned) | size(WIDTH, EQUAL, width);
            if (!r.isActive) {
                cellText = cellText | color(palette.channelInactive);
            } else {
                cellText = cellText | color(palette.textPrimary);
            }

            cellElements.push_back(cellText);
        }

        rowElements.push_back(hbox(std::move(cellElements)));
    }

    return vbox(std::move(rowElements)) | border;
}

// MULTI-PORT PAGE SELECTOR
//
//

Element LayoutCommon::drawPageSelector(const UiModel& model) {
    const auto palette = model.palette();
    int total = model.totalPages();
    int current = model.currentPage();

    // One button per port (A, B, C...), current one highlighted
    Elements buttons;
    for (int p = 0; p < total; ++p) {
        char labelBuf[32];
        char portChar = 'A' + p;
        std::snprintf(labelBuf, sizeof(labelBuf), " Part %c ", portChar);

        Element btn = text(labelBuf) | border;
        if (p == current) {
            btn = btn | color(palette.channelActive) | bold;
        } else {
            btn = btn | color(palette.textDim);
        }

        buttons.push_back(btn);
    }

    return hbox(std::move(buttons));
}

// SYSTEM FX DRAW
//
//

Element LayoutCommon::drawSystemFx(const UiModel& model) {
    const auto palette = model.palette();
    auto fx = model.getSystemFx();

    size_t maxLabelLen = 0;
    for (const auto& item : fx) {
        maxLabelLen = std::max(maxLabelLen, item.first.length());
    }

    Elements list;
    for (const auto& item : fx) {
        std::string label = item.first + ":";
        label += std::string(maxLabelLen + 2 - label.length(), ' ');

        // Dims values that are set to these values, not case sensitive (hopefully)
        std::string valLower = lower(item.second);
        Color valColor = (valLower == "0" || valLower == "off" || valLower == "bypass" || valLower == "through")
                ? palette.textDim
                : palette.fxValue;

        list.push_back(hbox({
            text(label) | bold | color(palette.fxLabel),
            text(item.second) | color(valColor)
        }));
    }

    if (list.empty()) {
        list.push_back(text("No FX configured") | color(palette.textDim));
    }

    return window(text(" SYSTEM & GLOBAL FX ") | bold, vbox(std::move(list))) | flex;
}

// MASTER OUTPUT DRAW
//
//

Element LayoutCommon::drawMasterOutput(const UiModel& model) {
    const auto palette = model.palette();
    auto master = model.getMasterOut();

    size_t maxLabelLen = 0;
    for (const auto& item : master) {
        maxLabelLen = std::max(maxLabelLen, item.first.length());
    }

    Elements list;
    for (const auto& item : master) {
        std::string label = item.first + ":";
        label += std::string(maxLabelLen + 2 - label.length(), ' ');

        list.push_back(hbox({
            text(label) | bold | color(palette.masterLabel),
            text(item.second) | color(palette.masterValue)
        }));
    }

    if (list.empty()) {
        list.push_back(text("Standard Output") | color(palette.textDim));
    }

    return window(text(" MASTER & OUTPUT ") | bold, vbox(std::move(list))) | flex;
}

// FOOTER DRAW
//
//

Element LayoutCommon::drawFooter(const UiModel& model, const std::string& currentTimeStr) {
    const auto palette = model.palette();
    std::string modeLabel = model.module().name + " Mode";

    return hbox({
        text(" [h/l] Pages [j/k] Queue [x] Stop ") | color(palette.footerText),
        filler(),
        text(modeLabel) | color(palette.footerText),
        filler(),
        text("󰗦 2026 SimTheNep  " + currentTimeStr) | color(palette.footerText)
    });
}
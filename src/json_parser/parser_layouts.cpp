#include "parser.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>

// PARSER_LAYOUTS.CPP
//
// Parses layout definitionss

using json = nlohmann::json;


// Helper to parse views and widgets
static layoutColumn parseColumn(const json& value) {

    layoutColumn column;

    // Simple string field
    // The field ID is also used as the default label
    if (value.is_string()) {

        column.fields.push_back(
            value.get<std::string>()
        );

        column.label =
            column.fields.front();

        return column;
    }

    if (!value.is_object()) {

        throw std::runtime_error(
            "layout column must be a string or object"
        );
    }

    // Single field
    if (value.contains("field")) {

        column.fields.push_back(
            value.at("field").get<std::string>()
        );
    }

    // Multiple grouped fields
    else if (value.contains("fields")) {

        column.fields =
            value.at("fields")
                .get<std::vector<std::string>>();
    }

    else {

        throw std::runtime_error(
            "layout column requires 'field' or 'fields'"
        );
    }

    // Display label
    if (value.contains("label")) {

        column.label =
            value.at("label").get<std::string>();
    }

    // Default label to first field ID
    else if (!column.fields.empty()) {

        column.label =
            column.fields.front();
    }

    // Separator between grouped field values
    if (value.contains("join")) {

        column.join =
            value.at("join").get<std::string>();
    }

    // Requested terminal-cell width
    if (value.contains("width")) {

        column.width =
            value.at("width").get<int>();
    }

    return column;
}


// Parses one view section (channels, system, widgets, etc...)

static layoutSect parseViewSection(
    const json& j
) {

    layoutSect out;

    if (!j.is_array()) {

        throw std::runtime_error(
            "layout view must be an array"
        );
    }

    for (const auto& value : j) {

        out.columns.push_back(
            parseColumn(value)
        );
    }

    return out;
}


static std::unordered_map<std::string, layoutSect>
parseGroups(const json& j) {

    std::unordered_map<std::string, layoutSect> out;

    for (const auto& [key, value] : j.items()) {

        out[key] =
            parseViewSection(value);
    }

    return out;
}


static std::unordered_map<
    std::string,
    std::vector<std::string>
>
parseWidgets(const json& j) {

    std::unordered_map<
        std::string,
        std::vector<std::string>
    > out;

    for (const auto& [key, value] : j.items()) {

        out[key] =
            value.get<std::vector<std::string>>();
    }

    return out;
}


// Parses layout geometry
static layoutGeometry parseGeometry(
    const json& j
) {

    layoutGeometry geometry;

    if (j.contains("min_width")) {

        geometry.minWidth =
            j.at("min_width").get<int>();
    }

    if (j.contains("min_height")) {

        geometry.minHeight =
            j.at("min_height").get<int>();
    }

    if (j.contains("page_size")) {

        geometry.pageSize =
            j.at("page_size").get<int>();
    }

    if (j.contains("channel_columns")) {

        geometry.channelColumns =
            j.at("channel_columns").get<int>();
    }

    return geometry;
}


// Parses one layout profile
static layoutType parseProfile(
    const json& j
) {

    layoutType variant;

    if (j.contains("geometry")) {

        variant.geometry =
            parseGeometry(
                j.at("geometry")
            );
    }

    if (j.contains("views")) {

        variant.views =
            parseGroups(
                j.at("views")
            );
    }

    if (j.contains("widgets")) {

        variant.widgets =
            parseWidgets(
                j.at("widgets")
            );
    }

    return variant;
}


// Parses the display variants
layoutDef parseLayouts(
    const json& j
) {

    layoutDef def;

    // Layout format version
    if (j.contains("version")) {

        def.version =
            j.at("version").get<int>();
    }

    // New layout format

    if (j.contains("profiles")) {

        const auto& profiles =
            j.at("profiles");

        for (const auto& [variantName, variantJson]
             : profiles.items()) {

            def.variants[variantName] =
                parseProfile(
                    variantJson
                );
        }

        return def;
    }

    // Old layout format

    // for (const auto& [variantName, variantJson]
    //      : j.items()) {

    //     if (variantName == "version") {
    //         continue;
    //     }

    //     if (!variantJson.is_object()) {
    //         continue;
    //     }

    //     layoutType variant =
    //         parseProfile(
    //             variantJson
    //         );

    //     def.variants[variantName] =
    //         std::move(variant);
    // }

    return def;
}
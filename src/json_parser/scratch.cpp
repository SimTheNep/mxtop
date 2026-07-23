// Delete once main.cpp has logic implemeted
#include "parser.hpp"
#include <fstream>

int main() {
    std::ifstream f("../../modules/gm/layouts.json");
    nlohmann::json j; f >> j;
    debugPrintLayouts(parseLayouts(j));
}
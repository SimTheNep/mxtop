// Delete once main.cpp exists
#include "parser.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char* argv[]) {
    std::string path = argv[1];
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Couldn't open " << path << " (check the path/working directory)\n";
        return 1;
    }

    nlohmann::json j;
    f >> j;
    debugLayouts(parseLayouts(j));
}
// Delete once main.cpp has this implemented
#include "parser.hpp"
#include <fstream>
#include <cstdio>

int main(int argc, char* argv[]) {
    std::string path = argv[1];
    std::ifstream f(path);
    if (!f) {
        printf("Couldn't open %s (check the path/working directory)\n", path.c_str());
        return 1;
    }

    nlohmann::json j;
    f >> j;
    debugPrintLayouts(parseLayouts(j));
}
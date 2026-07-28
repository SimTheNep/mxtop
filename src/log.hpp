#ifndef LOG_HPP
#define LOG_HPP

#include <fstream>
#include <iostream>
#include <vector>
#include <cstdio>
#include <string>

namespace detail {
    inline void writeToLog(const std::string& msg, bool appendNewline = false) {
        // Clears old log file once per program run
        static bool firstCall = []() {
            std::ofstream cleanFile("debug.log", std::ios::trunc);
            return true;
        }();

        std::ofstream logFile("debug.log", std::ios::app);
        if (logFile.is_open()) {
            logFile << msg;
            if (appendNewline) {
                logFile << '\n';
            }
        }
    }
}

//std::string style
inline void logDbg(const std::string& msg) {
    detail::writeToLog(msg, true);
}

//printf style
template <typename... Args>
inline void logDbg(const char* format, Args... args) {
    int size_s = std::snprintf(nullptr, 0, format, args...);
    if (size_s <= 0) return;

    // Format the string into a buffer
    std::vector<char> buf(static_cast<size_t>(size_s) + 1);
    std::snprintf(buf.data(), buf.size(), format, args...);

    std::string msg(buf.data(), buf.data() + size_s);

    // Write to file
    detail::writeToLog(msg, false);
}

#endif
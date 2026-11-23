/*
 * SPDX-License-Identifier: MIT
 */

#include "subghz_file.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace subghz {
namespace {

// Cuántos tiempos por línea RAW_Data al guardar (el Flipper parte en trozos; 512 es su tope).
constexpr std::size_t kValuesPerLine = 512;

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; };
    std::size_t begin = 0;
    while (begin < value.size() && !not_space(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && !not_space(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

// Separa "Clave: valor" en clave y valor ya recortados. Devuelve false si no hay ':'.
bool split_key_value(const std::string& line, std::string& key, std::string& value) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    key = trim(line.substr(0, colon));
    value = trim(line.substr(colon + 1));
    return true;
}

} // namespace

bool save_sub(std::ostream& out, const Signal& signal, std::string& error) {
    error.clear();
    if (signal.timings.empty()) {
        error = "empty signal";
        return false;
    }

    out << "Filetype: Flipper SubGhz RAW File\n";
    out << "Version: 1\n";
    out << "Frequency: " << signal.frequency_hz << '\n';
    out << "Preset: " << modulation_preset(signal.modulation) << '\n';
    out << "Protocol: RAW\n";

    for (std::size_t i = 0; i < signal.timings.size(); i += kValuesPerLine) {
        out << "RAW_Data:";
        const std::size_t end = std::min(i + kValuesPerLine, signal.timings.size());
        for (std::size_t j = i; j < end; ++j) {
            out << ' ' << signal.timings[j];
        }
        out << '\n';
    }

    if (!out) {
        error = "write error";
        return false;
    }
    return true;
}

bool load_sub(std::istream& in, Signal& signal, std::string& error) {
    error.clear();
    signal = Signal{};

    bool saw_frequency = false;
    std::string line;
    while (std::getline(in, line)) {
        std::string key;
        std::string value;
        if (!split_key_value(line, key, value)) {
            continue;
        }

        if (key == "Frequency") {
            try {
                signal.frequency_hz = static_cast<uint32_t>(std::stoul(value));
                saw_frequency = true;
            } catch (...) {
                error = "invalid Frequency";
                return false;
            }
        } else if (key == "Preset") {
            parse_modulation_preset(value, signal.modulation); // si no se entiende, se queda OOK
        } else if (key == "RAW_Data") {
            std::istringstream values(value);
            long number = 0;
            while (values >> number) {
                signal.timings.push_back(static_cast<int32_t>(number));
            }
        }
    }

    if (!saw_frequency) {
        error = "missing Frequency";
        return false;
    }
    if (signal.timings.empty()) {
        error = "no RAW_Data";
        return false;
    }
    return true;
}

bool save_sub_file(const std::string& path, const Signal& signal, std::string& error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "could not open file for writing";
        return false;
    }
    return save_sub(out, signal, error);
}

bool load_sub_file(const std::string& path, Signal& signal, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "could not open file";
        return false;
    }
    return load_sub(in, signal, error);
}

} // namespace subghz

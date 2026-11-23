/*
 * SPDX-License-Identifier: MIT
 */

#include "name_edit.h"

namespace text {
namespace {

bool allowed(uint32_t key) {
    return (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
           key == ' ' || key == '-' || key == '_' || key == '.' || key == '+';
}

} // namespace

bool edit_name(std::string& name, uint32_t key, std::size_t max_length) {
    if (key == kBackspace) {
        if (!name.empty()) {
            name.pop_back();
        }
        return true;
    }
    if (!allowed(key)) {
        return false;
    }
    if (name.size() < max_length) {
        name.push_back(static_cast<char>(key));
    }
    return true; // aunque esté lleno: la tecla era de texto, no un atajo
}

std::string clean_file_name(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20) {
            continue;
        }
        if (c == ' ' && (out.empty() || out.back() == ' ')) {
            continue;
        }
        out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    while (!out.empty() && out.front() == '.') {
        out.erase(out.begin());
    }
    return out;
}

} // namespace text

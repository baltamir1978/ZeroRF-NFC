/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_storage.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace platform {
namespace {

namespace fs = std::filesystem;

fs::path data_root() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        const fs::path root(xdg);
        if (!root.empty() && root.is_absolute()) {
            return root;
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const fs::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return root / ".local" / "share";
        }
    }
    return fs::current_path();
}

} // namespace

std::string nfc_dir() {
    const fs::path dir = data_root() / "zerorfnfc" / "nfc";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

std::vector<std::string> list_cards() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(nfc_dir(), ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".nfc") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string card_path(const std::string& name) {
    return (fs::path(nfc_dir()) / (name + ".nfc")).string();
}

std::string unique_card_name(const std::string& base) {
    std::error_code ec;
    if (!fs::exists(card_path(base), ec)) {
        return base;
    }
    for (int n = 2; n < 1000; ++n) {
        const std::string candidate = base + "-" + std::to_string(n);
        if (!fs::exists(card_path(candidate), ec)) {
            return candidate;
        }
    }
    return base;
}

bool delete_card(const std::string& name) {
    std::error_code ec;
    return fs::remove(card_path(name), ec);
}

} // namespace platform

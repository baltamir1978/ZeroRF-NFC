/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_file.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

namespace nfc {
namespace {

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (begin < end && space(value[begin])) {
        ++begin;
    }
    while (end > begin && space(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

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

bool save_nfc(std::ostream& out, const Card& card, std::string& error) {
    error.clear();
    if (card.uid.empty()) {
        error = "card has no UID";
        return false;
    }

    out << "Filetype: Flipper NFC device\n";
    out << "Version: 4\n";
    out << "Device type: " << card_type_token(card.type) << '\n';
    out << "UID: " << to_hex(card.uid) << '\n';
    if (card.tech == Tech::IsoA) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02X %02X", (card.atqa >> 8) & 0xFF, card.atqa & 0xFF);
        out << "ATQA: " << buf << '\n';
        std::snprintf(buf, sizeof(buf), "%02X", card.sak);
        out << "SAK: " << buf << '\n';
    }

    if (card.has_memory() && (card.block_size == 16 || card.block_size == 4)) {
        const char* label = card.block_size == 16 ? "Block" : "Page";
        const std::size_t count = card.memory.size() / card.block_size;
        for (std::size_t i = 0; i < count; ++i) {
            std::vector<uint8_t> chunk(card.memory.begin() + i * card.block_size,
                                      card.memory.begin() + (i + 1) * card.block_size);
            out << label << ' ' << i << ": " << to_hex(chunk) << '\n';
        }
    }

    if (!out) {
        error = "write error";
        return false;
    }
    return true;
}

bool load_nfc(std::istream& in, Card& card, std::string& error) {
    error.clear();
    card = Card{};

    std::map<std::size_t, std::vector<uint8_t>> chunks;
    uint16_t chunk_size = 0;
    bool saw_uid = false;

    std::string line;
    while (std::getline(in, line)) {
        std::string key;
        std::string value;
        if (!split_key_value(line, key, value)) {
            continue;
        }

        if (key == "Device type") {
            parse_card_type_token(value, card.type);
            card.tech = tech_for_type(card.type);
        } else if (key == "UID") {
            if (from_hex(value, card.uid) && !card.uid.empty()) {
                saw_uid = true;
            }
        } else if (key == "ATQA") {
            std::vector<uint8_t> bytes;
            if (from_hex(value, bytes) && bytes.size() == 2) {
                card.atqa = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
            }
        } else if (key == "SAK") {
            std::vector<uint8_t> bytes;
            if (from_hex(value, bytes) && bytes.size() == 1) {
                card.sak = bytes[0];
            }
        } else if (key.rfind("Block ", 0) == 0 || key.rfind("Page ", 0) == 0) {
            const std::size_t space = key.find(' ');
            const std::size_t index = static_cast<std::size_t>(std::stoul(key.substr(space + 1)));
            std::vector<uint8_t> bytes;
            if (from_hex(value, bytes) && !bytes.empty()) {
                chunk_size = static_cast<uint16_t>(bytes.size());
                chunks[index] = std::move(bytes);
            }
        }
    }

    if (!saw_uid) {
        error = "missing UID";
        return false;
    }

    if (!chunks.empty() && chunk_size > 0) {
        card.block_size = chunk_size;
        const std::size_t count = chunks.rbegin()->first + 1;
        card.memory.assign(static_cast<std::size_t>(count) * chunk_size, 0);
        for (const auto& [index, bytes] : chunks) {
            std::copy(bytes.begin(), bytes.end(), card.memory.begin() + index * chunk_size);
        }
    }

    return true;
}

bool save_nfc_file(const std::string& path, const Card& card, std::string& error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "could not open file for writing";
        return false;
    }
    return save_nfc(out, card, error);
}

bool load_nfc_file(const std::string& path, Card& card, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "could not open file";
        return false;
    }
    return load_nfc(in, card, error);
}

} // namespace nfc

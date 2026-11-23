/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_keys.h"

#include "nfc_card.h"

#include <cstdio>
#include <istream>
#include <string>

namespace nfc {
namespace {

// Claves de fábrica / muy comunes. Son públicas y de dominio general (vienen en las notas de
// aplicación de NXP y en cualquier diccionario de MIFARE); no son un ataque.
const char* const kDefaultKeyHex[] = {
    "FFFFFFFFFFFF", "000000000000", "A0A1A2A3A4A5", "D3F7D3F7D3F7",
    "B0B1B2B3B4B5", "4D3A99C351DD", "1A982C7E459A", "AABBCCDDEEFF",
    "714C5C886E97", "587EE5F9350F", "A0478CC39091", "533CB6C723F6",
    "8FD0A4F256E9", "0000014B5C31", "B578F38A5C61",
};

std::vector<MifareKey> build_default_keys() {
    std::vector<MifareKey> keys;
    for (const char* hex : kDefaultKeyHex) {
        MifareKey key;
        if (parse_key(hex, key)) {
            keys.push_back(key);
        }
    }
    return keys;
}

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

} // namespace

const std::vector<MifareKey>& default_keys() {
    static const std::vector<MifareKey> keys = build_default_keys();
    return keys;
}

bool parse_key(const std::string& hex, MifareKey& key) {
    std::vector<uint8_t> bytes;
    if (!from_hex(hex, bytes) || bytes.size() != key.size()) {
        return false;
    }
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = bytes[i];
    }
    return true;
}

std::string key_to_hex(const MifareKey& key) {
    std::string out;
    char buf[3];
    for (uint8_t b : key) {
        std::snprintf(buf, sizeof(buf), "%02X", b);
        out += buf;
    }
    return out;
}

std::vector<MifareKey> parse_key_dictionary(std::istream& in) {
    std::vector<MifareKey> keys;
    std::string line;
    while (std::getline(in, line)) {
        const std::string text = trim(line);
        if (text.empty() || text[0] == '#') {
            continue;
        }
        MifareKey key;
        if (parse_key(text, key)) {
            keys.push_back(key);
        }
    }
    return keys;
}

} // namespace nfc

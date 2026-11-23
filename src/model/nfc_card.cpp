/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_card.h"

#include <cctype>
#include <cstdio>

namespace nfc {

const char* tech_name(Tech tech) {
    switch (tech) {
        case Tech::IsoA:
            return "ISO14443-A";
        case Tech::IsoB:
            return "ISO14443-B";
        case Tech::IsoF:
            return "FeliCa";
        case Tech::IsoV:
            return "ISO15693";
    }
    return "?";
}

const char* card_type_name(CardType type) {
    switch (type) {
        case CardType::MifareUltralight:
            return "MIFARE Ultralight / NTAG";
        case CardType::MifareClassicMini:
            return "MIFARE Classic Mini";
        case CardType::MifareClassic1k:
            return "MIFARE Classic 1K";
        case CardType::MifareClassic4k:
            return "MIFARE Classic 4K";
        case CardType::MifarePlus:
            return "MIFARE Plus";
        case CardType::MifareDesfire:
            return "MIFARE DESFire";
        case CardType::IsoDep:
            return "ISO14443-4";
        case CardType::Felica:
            return "FeliCa";
        case CardType::Iso15693:
            return "ISO15693";
        case CardType::Unknown:
        default:
            return "Unknown";
    }
}

namespace {
struct TypeToken {
    CardType type;
    const char* token;
};
const TypeToken kTypeTokens[] = {
    {CardType::MifareUltralight, "mifare_ultralight"},
    {CardType::MifareClassicMini, "mifare_classic_mini"},
    {CardType::MifareClassic1k, "mifare_classic_1k"},
    {CardType::MifareClassic4k, "mifare_classic_4k"},
    {CardType::MifarePlus, "mifare_plus"},
    {CardType::MifareDesfire, "mifare_desfire"},
    {CardType::IsoDep, "isodep"},
    {CardType::Felica, "felica"},
    {CardType::Iso15693, "iso15693"},
    {CardType::Unknown, "unknown"},
};
} // namespace

const char* card_type_token(CardType type) {
    for (const auto& entry : kTypeTokens) {
        if (entry.type == type) {
            return entry.token;
        }
    }
    return "unknown";
}

bool parse_card_type_token(const std::string& token, CardType& type) {
    for (const auto& entry : kTypeTokens) {
        if (token == entry.token) {
            type = entry.type;
            return true;
        }
    }
    return false;
}

Tech tech_for_type(CardType type) {
    switch (type) {
        case CardType::Felica:
            return Tech::IsoF;
        case CardType::Iso15693:
            return Tech::IsoV;
        default:
            return Tech::IsoA;
    }
}

CardType detect_card_type(uint16_t atqa, uint8_t sak) {
    // El bit 0x02 del SAK indica cascada (UID incompleto); no clasifica, se ignora.
    const uint8_t s = sak & ~0x02;

    switch (s) {
        case 0x00:
            return CardType::MifareUltralight; // UL / NTAG: sin cripto
        case 0x08:
            return CardType::MifareClassic1k;
        case 0x09:
            return CardType::MifareClassicMini;
        case 0x18:
            return CardType::MifareClassic4k;
        case 0x10:
        case 0x11:
            return CardType::MifarePlus;
        case 0x20:
            // ISO14443-4. El ATQA 0x0344 es el típico de DESFire.
            return (atqa == 0x0344) ? CardType::MifareDesfire : CardType::IsoDep;
        default:
            return CardType::Unknown;
    }
}

std::string to_hex(const std::vector<uint8_t>& bytes) {
    std::string out;
    char buf[4];
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::snprintf(buf, sizeof(buf), i == 0 ? "%02X" : " %02X", bytes[i]);
        out += buf;
    }
    return out;
}

bool from_hex(const std::string& text, std::vector<uint8_t>& bytes) {
    bytes.clear();
    int high = -1;
    for (char c : text) {
        if (c == ' ' || c == ':' || c == '\t') {
            continue;
        }
        int value;
        if (c >= '0' && c <= '9') {
            value = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            value = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            value = c - 'A' + 10;
        } else {
            return false;
        }
        if (high < 0) {
            high = value;
        } else {
            bytes.push_back(static_cast<uint8_t>((high << 4) | value));
            high = -1;
        }
    }
    return high < 0; // número par de dígitos
}

} // namespace nfc

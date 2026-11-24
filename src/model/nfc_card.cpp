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

std::string card_product(const Card& card) {
    // NTAG / Ultralight: el byte 6 de GET_VERSION da el modelo exacto.
    if (card.type == CardType::MifareUltralight) {
        if (card.version.size() >= 8 && card.version[1] == 0x04) {
            switch (card.version[6]) {
                case 0x0B: return "MIFARE Ultralight EV1 (48 B)";
                case 0x0E: return "MIFARE Ultralight EV1 (128 B)";
                case 0x0F: return "NTAG213 (144 B)";
                case 0x11: return "NTAG215 (504 B)";
                case 0x13: return "NTAG216 (888 B)";
                default: return "NTAG21x / Ultralight EV1";
            }
        }
        // Sin GET_VERSION (Ultralight original, o tarjetas guardadas antes de la 0.3.0, que no lo
        // apuntaban): el tamaño del volcado también dice el modelo.
        if (card.block_size == 4) {
            switch (card.memory.size() / 4) {
                case 16: return "MIFARE Ultralight";
                case 20: return "Ultralight EV1 / NTAG210 (48 B)";
                case 41: return "Ultralight EV1 / NTAG212 (128 B)";
                case 45: return "NTAG213 (144 B)";
                case 135: return "NTAG215 (504 B)";
                case 231: return "NTAG216 (888 B)";
                default: break;
            }
        }
        return "MIFARE Ultralight / NTAG";
    }
    // DESFire: el ATS típico de EV1 es 06 75 77 81 02 80.
    if (card.type == CardType::MifareDesfire) {
        if (card.ats.size() >= 6 && card.ats[1] == 0x75 && card.ats[2] == 0x77) {
            return "MIFARE DESFire EV1";
        }
        return "MIFARE DESFire";
    }
    return card_type_name(card.type);
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

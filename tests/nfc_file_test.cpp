/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_file.h"

#include <cstdio>
#include <sstream>

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

nfc::Card make_ultralight() {
    nfc::Card card;
    card.type = nfc::CardType::MifareUltralight;
    card.tech = nfc::Tech::IsoA;
    card.uid = {0x04, 0xA2, 0x2B, 0x10, 0x55, 0x66, 0x77};
    card.atqa = 0x0044;
    card.sak = 0x00;
    card.block_size = 4;
    for (int i = 0; i < 16 * 4; ++i) { // 16 páginas de 4 bytes
        card.memory.push_back(static_cast<uint8_t>(i));
    }
    return card;
}

void test_round_trip_ultralight() {
    const nfc::Card original = make_ultralight();
    std::ostringstream out;
    std::string error;
    CHECK(nfc::save_nfc(out, original, error));

    std::istringstream in(out.str());
    nfc::Card loaded;
    CHECK(nfc::load_nfc(in, loaded, error));
    CHECK(loaded.type == original.type);
    CHECK(loaded.tech == original.tech);
    CHECK(loaded.uid == original.uid);
    CHECK(loaded.atqa == original.atqa);
    CHECK(loaded.sak == original.sak);
    CHECK(loaded.block_size == original.block_size);
    CHECK(loaded.memory == original.memory);
}

void test_round_trip_classic() {
    nfc::Card card;
    card.type = nfc::CardType::MifareClassic1k;
    card.uid = {0x01, 0x02, 0x03, 0x04};
    card.atqa = 0x0004;
    card.sak = 0x08;
    card.block_size = 16;
    for (int i = 0; i < 16 * 8; ++i) {
        card.memory.push_back(static_cast<uint8_t>(i * 3));
    }

    std::ostringstream out;
    std::string error;
    CHECK(nfc::save_nfc(out, card, error));
    std::istringstream in(out.str());
    nfc::Card loaded;
    CHECK(nfc::load_nfc(in, loaded, error));
    CHECK(loaded.type == nfc::CardType::MifareClassic1k);
    CHECK(loaded.block_size == 16);
    CHECK(loaded.memory == card.memory);
}

void test_rejects_no_uid() {
    std::istringstream in("Filetype: Flipper NFC device\nDevice type: mifare_classic_1k\n");
    nfc::Card card;
    std::string error;
    CHECK(!nfc::load_nfc(in, card, error));
}

} // namespace

int main() {
    test_round_trip_ultralight();
    test_round_trip_classic();
    test_rejects_no_uid();
    if (failures == 0) {
        std::printf("nfc_file_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

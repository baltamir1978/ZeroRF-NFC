/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_card.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

void test_detect() {
    using nfc::CardType;
    CHECK(nfc::detect_card_type(0x0004, 0x08) == CardType::MifareClassic1k);
    CHECK(nfc::detect_card_type(0x0002, 0x18) == CardType::MifareClassic4k);
    CHECK(nfc::detect_card_type(0x0004, 0x09) == CardType::MifareClassicMini);
    CHECK(nfc::detect_card_type(0x0044, 0x00) == CardType::MifareUltralight);
    CHECK(nfc::detect_card_type(0x0344, 0x20) == CardType::MifareDesfire);
    CHECK(nfc::detect_card_type(0x0004, 0x20) == CardType::IsoDep);
    // El bit de cascada (0x02) no debe cambiar la clasificación.
    CHECK(nfc::detect_card_type(0x0004, 0x08 | 0x02) == CardType::MifareClassic1k);
    CHECK(nfc::detect_card_type(0x0000, 0x99) == CardType::Unknown);
}

void test_hex() {
    std::vector<uint8_t> bytes;
    CHECK(nfc::from_hex("04 A2 2B 10", bytes));
    CHECK(bytes.size() == 4);
    CHECK(bytes[0] == 0x04 && bytes[1] == 0xA2 && bytes[3] == 0x10);
    CHECK(nfc::to_hex(bytes) == "04 A2 2B 10");

    CHECK(nfc::from_hex("04a22b", bytes)); // sin espacios
    CHECK(bytes.size() == 3);

    std::vector<uint8_t> dummy;
    CHECK(!nfc::from_hex("0G", dummy));  // dígito no hexadecimal
    CHECK(!nfc::from_hex("ABC", dummy)); // número impar de dígitos
}

} // namespace

int main() {
    test_detect();
    test_hex();
    if (failures == 0) {
        std::printf("nfc_card_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

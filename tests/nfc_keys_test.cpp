/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_keys.h"

#include <algorithm>
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

void test_defaults() {
    const auto& keys = nfc::default_keys();
    CHECK(!keys.empty());

    nfc::MifareKey ff;
    CHECK(nfc::parse_key("FFFFFFFFFFFF", ff));
    CHECK(std::find(keys.begin(), keys.end(), ff) != keys.end());
}

void test_parse_key() {
    nfc::MifareKey key;
    CHECK(nfc::parse_key("A0A1A2A3A4A5", key));
    CHECK(key[0] == 0xA0 && key[5] == 0xA5);
    CHECK(nfc::key_to_hex(key) == "A0A1A2A3A4A5");

    nfc::MifareKey dummy;
    CHECK(!nfc::parse_key("ZZZZZZZZZZZZ", dummy)); // no hex
    CHECK(!nfc::parse_key("FFFF", dummy));         // no son 6 bytes
}

void test_dictionary() {
    std::istringstream in("FFFFFFFFFFFF\n# un comentario\n\nA0A1A2A3A4A5\nbasura\n");
    const auto keys = nfc::parse_key_dictionary(in);
    CHECK(keys.size() == 2); // se saltan comentario, línea vacía y "basura"
}

} // namespace

int main() {
    test_defaults();
    test_parse_key();
    test_dictionary();
    if (failures == 0) {
        std::printf("nfc_keys_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

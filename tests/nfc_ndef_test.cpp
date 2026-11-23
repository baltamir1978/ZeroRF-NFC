/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_ndef.h"

#include <cstdint>
#include <cstdio>
#include <string>
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

void push_string(std::vector<uint8_t>& v, const std::string& s) {
    for (char c : s) {
        v.push_back(static_cast<uint8_t>(c));
    }
}

// Un registro URI: header 0xD1 (MB|ME|SR|TNF=1), type "U", prefijo 0x04 = "https://".
std::vector<uint8_t> uri_record(const std::string& tail) {
    std::vector<uint8_t> v = {0xD1, 0x01, static_cast<uint8_t>(1 + tail.size()), 'U', 0x04};
    push_string(v, tail);
    return v;
}

void test_uri() {
    const std::vector<uint8_t> msg = uri_record("example.com");
    nfc::NdefMessage out;
    CHECK(nfc::parse_ndef_message(msg.data(), msg.size(), out));
    CHECK(out.records.size() == 1);
    CHECK(out.records[0].type == nfc::NdefRecordType::Uri);
    CHECK(out.records[0].content == "https://example.com");
}

void test_text() {
    // header 0xD1, type "T", payload: status 0x02 (lang "en"), "Hola".
    std::vector<uint8_t> msg = {0xD1, 0x01, 0, 'T'};
    std::vector<uint8_t> payload = {0x02, 'e', 'n'};
    push_string(payload, "Hola");
    msg[2] = static_cast<uint8_t>(payload.size());
    msg.insert(msg.end(), payload.begin(), payload.end());

    nfc::NdefMessage out;
    CHECK(nfc::parse_ndef_message(msg.data(), msg.size(), out));
    CHECK(out.records.size() == 1);
    CHECK(out.records[0].type == nfc::NdefRecordType::Text);
    CHECK(out.records[0].language == "en");
    CHECK(out.records[0].content == "Hola");
}

void test_type2_tlv() {
    const std::vector<uint8_t> message = uri_record("example.com");
    std::vector<uint8_t> memory(16, 0x00); // páginas 0-3 (cabecera), se ignoran
    memory.push_back(0x03);                // TLV: mensaje NDEF
    memory.push_back(static_cast<uint8_t>(message.size()));
    memory.insert(memory.end(), message.begin(), message.end());
    memory.push_back(0xFE); // terminador

    nfc::NdefMessage out;
    CHECK(nfc::parse_type2_ndef(memory, out));
    CHECK(out.records.size() == 1);
    CHECK(out.records[0].content == "https://example.com");
}

void test_rejects_truncated() {
    // Cabecera que promete 20 bytes de payload pero no están.
    const std::vector<uint8_t> msg = {0xD1, 0x01, 20, 'U', 0x04, 'a', 'b'};
    nfc::NdefMessage out;
    CHECK(!nfc::parse_ndef_message(msg.data(), msg.size(), out));
}

} // namespace

int main() {
    test_uri();
    test_text();
    test_type2_tlv();
    test_rejects_truncated();
    if (failures == 0) {
        std::printf("nfc_ndef_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

/*
 * SPDX-License-Identifier: MIT
 */

// Pruebas del plan de escritura Type 2: qué páginas se escriben al clonar, y que nunca se tocan
// las de UID/bloqueo/configuración/contraseña. Sin LVGL ni hardware.

#include "nfc_type2.h"

#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

std::vector<uint8_t> version_of(uint8_t type, uint8_t size) {
    return {0x00, 0x04, type, 0x01, 0x00, 0x00, size, 0x03};
}

nfc::Card card_with(uint8_t size_byte, int pages, uint8_t fill_until_page) {
    nfc::Card c;
    c.version = version_of(0x04, size_byte);
    c.block_size = 4;
    c.memory.assign(static_cast<std::size_t>(pages) * 4, 0);
    for (int p = 0; p < pages; ++p) {
        if (p < 4 || p <= fill_until_page) {
            for (int i = 0; i < 4; ++i) c.memory[p * 4 + i] = static_cast<uint8_t>(p + i + 1);
        }
    }
    // Las páginas de configuración y contraseña del final también traen algo (como en la realidad).
    for (int i = (pages - 5) * 4; i < pages * 4; ++i) c.memory[i] = 0xAA;
    return c;
}

void test_layouts() {
    CHECK(nfc::type2_layout(version_of(0x04, 0x11), 0).name == "NTAG215");
    CHECK(nfc::type2_layout(version_of(0x04, 0x11), 0).last_user == 129);
    CHECK(nfc::type2_layout(version_of(0x04, 0x0F), 0).total_pages == 45);
    CHECK(nfc::type2_layout(version_of(0x04, 0x13), 0).user_pages() == 222);
    CHECK(nfc::type2_layout(version_of(0x03, 0x0B), 0).name == "Ultralight EV1 (MF0UL11)");
    CHECK(nfc::type2_layout(version_of(0x03, 0x0B), 0).last_user == 15);
    CHECK(nfc::type2_layout(version_of(0x04, 0x0E), 0).name == "NTAG212");
    CHECK(nfc::type2_layout({}, 16).name == "Ultralight");
    CHECK(nfc::type2_layout({}, 135).name == "NTAG215");
    CHECK(nfc::type2_layout({}, 20).last_user == 15);
    CHECK(!nfc::type2_layout({}, 99).known());
    CHECK(!nfc::type2_layout(version_of(0x04, 0x99), 0).known());
}

void test_same_model() {
    const nfc::Card src = card_with(0x11, 135, 40);
    const auto plan = nfc::plan_type2_write(src, nfc::type2_layout(version_of(0x04, 0x11), 0));
    CHECK(plan.ok());
    CHECK(plan.pages.size() == 126);          // 4..129
    CHECK(plan.pages.front().first == 4);
    CHECK(plan.pages.back().first == 129);    // nunca 130 (bloqueo dinámico) ni 131-134
    CHECK(plan.pages.front().second[0] == 5); // contenido de la página 4
}

void test_smaller_target() {
    // NDEF corta (hasta la página 30) de una NTAG215 en una NTAG213: cabe.
    const nfc::Card short_src = card_with(0x11, 135, 30);
    const auto ok = nfc::plan_type2_write(short_src, nfc::type2_layout(version_of(0x04, 0x0F), 0));
    CHECK(ok.ok());
    CHECK(ok.pages.back().first == 39);
    // Con datos hasta la página 100 no cabe.
    const nfc::Card long_src = card_with(0x11, 135, 100);
    const auto no = nfc::plan_type2_write(long_src, nfc::type2_layout(version_of(0x04, 0x0F), 0));
    CHECK(!no.ok());
    CHECK(no.error == "Data doesn't fit in NTAG213");
}

void test_bigger_target_and_errors() {
    const nfc::Card src = card_with(0x0F, 45, 39);
    const auto plan = nfc::plan_type2_write(src, nfc::type2_layout(version_of(0x04, 0x13), 0));
    CHECK(plan.ok());
    CHECK(plan.pages.back().first == 39); // sólo lo que hay en el origen; no la config del 213
    nfc::Card classic;
    classic.block_size = 16;
    classic.memory.assign(64, 0);
    CHECK(nfc::plan_type2_write(classic, nfc::type2_layout(version_of(0x04, 0x11), 0)).error == "Not an NTAG/Ultralight dump");
    CHECK(nfc::plan_type2_write(src, nfc::Type2Layout{}).error == "Unknown target tag model");
}

// Una NTAG215 guardada sin GET_VERSION (como las de la 0.2): no se deben copiar sus páginas de
// configuración del final, ni dar «no cabe» por ellas.
void test_dump_without_version() {
    nfc::Card src = card_with(0x11, 135, 60);
    src.version.clear();
    const auto plan = nfc::plan_type2_write(src, nfc::type2_layout(version_of(0x04, 0x11), 0));
    CHECK(plan.ok());
    CHECK(plan.pages.back().first == 129);
}

void test_locks() {
    CHECK(!nfc::type2_static_locked({0x44, 0x00, 0x00, 0x00}));
    CHECK(nfc::type2_static_locked({0x44, 0x00, 0x01, 0x00}));
    CHECK(nfc::type2_static_locked({0x44, 0x00, 0x00, 0x80}));
}

} // namespace

int main() {
    test_layouts();
    test_same_model();
    test_smaller_target();
    test_bigger_target_and_errors();
    test_dump_without_version();
    test_locks();
    if (failures == 0) std::printf("nfc_type2_test: OK\n");
    return failures == 0 ? 0 : 1;
}

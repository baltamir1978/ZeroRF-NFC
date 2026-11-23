/*
 * SPDX-License-Identifier: MIT
 */

#include "name_edit.h"

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

void type(std::string& name, const char* keys) {
    for (const char* k = keys; *k; ++k) {
        text::edit_name(name, static_cast<uint32_t>(*k));
    }
}

void test_edit() {
    std::string name;
    type(name, "garaje 2");
    CHECK(name == "garaje 2");
    CHECK(text::edit_name(name, text::kBackspace));
    CHECK(name == "garaje ");
    CHECK(text::edit_name(name, 't')); // la T es texto, no el cambio de tema
    CHECK(name == "garaje t");

    // Teclas que no son texto no se usan (ENTER = 10, flechas 17..20).
    CHECK(!text::edit_name(name, 10));
    CHECK(!text::edit_name(name, 17));
    CHECK(!text::edit_name(name, '/'));
    CHECK(name == "garaje t");

    std::string empty;
    CHECK(text::edit_name(empty, text::kBackspace)); // borrar en vacío no rompe
    CHECK(empty.empty());
}

void test_max_length() {
    std::string name;
    for (int i = 0; i < 40; ++i) {
        CHECK(text::edit_name(name, 'a'));
    }
    CHECK(name.size() == text::kMaxNameLength);
}

void test_clean() {
    CHECK(text::clean_file_name("  oficina   puerta ") == "oficina puerta");
    CHECK(text::clean_file_name("..oculto") == "oculto");
    CHECK(text::clean_file_name("a/b") == "ab");
    CHECK(text::clean_file_name("   ").empty());
    CHECK(text::clean_file_name("ntag-215_v2.1") == "ntag-215_v2.1");
}

} // namespace

int main() {
    test_edit();
    test_max_length();
    test_clean();
    if (failures == 0) {
        std::printf("name_edit: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

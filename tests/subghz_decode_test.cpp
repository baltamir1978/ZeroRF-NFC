/*
 * SPDX-License-Identifier: MIT
 */

// Pruebas del codificador/decodificador Princeton. Sin LVGL ni hardware: corren en ctest.

#include "subghz_decode.h"

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

// Reconoce el código que acaba de codificar, con varios Te y repeticiones.
void test_round_trip() {
    const uint64_t keys[] = {0xABC123ULL, 0x000001ULL, 0xFFFFFFULL, 0x5A5A5AULL};
    const uint32_t tes[] = {250, 350, 450, 1000};
    for (uint64_t key : keys) {
        for (uint32_t te : tes) {
            const subghz::Signal signal = subghz::encode_princeton(key, 24, te, 433920000, 3);
            const subghz::Decoded decoded = subghz::decode(signal);
            CHECK(decoded.ok());
            CHECK(decoded.protocol == subghz::Protocol::Princeton);
            CHECK(decoded.key == key);
            CHECK(decoded.bits == 24);
            CHECK(decoded.frequency_hz == 433920000U);
            // Te se estima del sincronismo: tiene que salir el mismo.
            CHECK(decoded.te_us == te);
        }
    }
}

// Aunque los tiempos lleguen con ruido (±25 %), sigue reconociéndolo: los mandos reales no
// son exactos.
void test_jitter() {
    subghz::Signal signal = subghz::encode_princeton(0xABC123ULL, 24, 400, 433920000, 2);
    // Perturbación determinista ±12 %: el Te se estima del sincronismo, así que lo que
    // importa es el error cruzado entre tiempos (~27 % aquí), que queda dentro de la
    // tolerancia del decodificador.
    for (std::size_t i = 0; i < signal.timings.size(); ++i) {
        const double factor = (i % 2 == 0) ? 1.12 : 0.88;
        const double v = signal.timings[i] * factor;
        signal.timings[i] = static_cast<int32_t>(v);
    }
    const subghz::Decoded decoded = subghz::decode(signal);
    CHECK(decoded.ok());
    CHECK(decoded.key == 0xABC123ULL);
}

// Ruido que no es un código fijo no debe reconocerse como tal.
void test_rejects_noise() {
    subghz::Signal signal;
    signal.frequency_hz = 433920000;
    signal.timings = {120, -90, 2000, -40, 77, -1234, 560, -560, 33, -9000, 10, -10};
    const subghz::Decoded decoded = subghz::decode(signal);
    CHECK(!decoded.ok());
}


// CAME y NICE FLO: mismo formato con distinto tiempo base; 12 y 24 bits.
void test_came_and_nice() {
    struct Case { uint32_t te; subghz::Protocol p; const char* prefix; };
    const Case cases[] = {{320, subghz::Protocol::Came, "CAME-"}, {700, subghz::Protocol::NiceFlo, "NICE-"}};
    for (const Case& c : cases) {
        for (uint8_t bits : {uint8_t(12), uint8_t(24)}) {
            const uint64_t key = bits == 12 ? 0x5A3ULL : 0xC0FFEEULL;
            const subghz::Signal s = subghz::encode_came_like(key, bits, c.te, 433920000, 4);
            const subghz::Decoded d = subghz::decode(s);
            CHECK(d.protocol == c.p);
            CHECK(d.bits == bits);
            CHECK(d.key == key);
            CHECK(d.te_us == c.te);
            CHECK(d.te_long_us == c.te * 2);
            CHECK(subghz::suggested_name(d, s.timings.size()) == std::string(c.prefix) + d.code_hex);
        }
    }
    const subghz::Decoded d = subghz::decode(subghz::encode_came_like(0x5A3, 12, 320, 433920000, 3));
    CHECK(d.code_hex == "5A3");
    CHECK(subghz::describe(d) == "CAME 12 bit  0x5A3  Te 320 us");
}

// Un mando como el del garaje de Bruno: proporción 1:2, pulso + silencio, 77 bits, tramas
// separadas por pausas largas, con una trama cortada al principio de la captura (como pasa al
// empezar a grabar a media pulsación). No es de ninguna marca conocida: sale como código fijo.
void test_generic_fixed_code() {
    std::vector<bool> code;
    uint32_t lfsr = 0xACE1u;
    for (int i = 0; i < 77; ++i) { lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u); code.push_back(lfsr & 1u); }
    const int32_t te = 400, tl = 800;
    subghz::Signal s;
    // Trama cortada (sólo los últimos 10 bits).
    for (int i = 67; i < 77; ++i) { s.timings.push_back(code[i] ? tl : te); s.timings.push_back(code[i] ? -te : -tl); }
    for (int r = 0; r < 5; ++r) {
        s.timings.push_back(-12000);
        for (int i = 0; i < 77; ++i) {
            // Algo de ruido determinista (±10 %), como un oscilador RC.
            const double f = (i % 3 == 0) ? 1.1 : (i % 3 == 1 ? 0.92 : 1.0);
            s.timings.push_back(static_cast<int32_t>((code[i] ? tl : te) * f));
            s.timings.push_back(-static_cast<int32_t>((code[i] ? te : tl) * f));
        }
    }
    const subghz::Decoded d = subghz::decode(s);
    CHECK(d.protocol == subghz::Protocol::FixedCode);
    CHECK(d.bits == 77);
    CHECK(d.code_hex.size() == 20); // 77 bits = 20 dígitos (3 bits de relleno a la izquierda)
    uint64_t low = 0;
    for (int i = 77 - 64; i < 77; ++i) low = (low << 1) | (code[i] ? 1 : 0);
    CHECK(d.key == low);
    CHECK(d.te_us >= 380 && d.te_us <= 420);
    CHECK(d.te_long_us >= 760 && d.te_long_us <= 840);
    const std::string name = subghz::suggested_name(d, s.timings.size());
    CHECK(name.rfind("FIX77-", 0) == 0 && name.size() == 10);
}

// Princeton con un Te raro sigue siendo Princeton; y un nombre RAW para lo irreconocible.
void test_names() {
    const subghz::Signal p = subghz::encode_princeton(0xABC123ULL, 24, 350, 433920000, 3);
    const subghz::Decoded d = subghz::decode(p);
    CHECK(subghz::suggested_name(d, p.timings.size()) == "PT-ABC123");
    CHECK(d.code_hex == "ABC123");
    CHECK(d.te_long_us == 1050);
    CHECK(subghz::suggested_name(subghz::Decoded{}, 42) == "RAW-42");
    CHECK(subghz::describe(subghz::Decoded{}) == "Raw signal (unknown format)");
}

// Tres anchos distintos (no es código de dos anchos): no se inventa bits.
void test_rejects_three_widths() {
    subghz::Signal s;
    for (int r = 0; r < 3; ++r) {
        s.timings.push_back(-15000);
        for (int i = 0; i < 20; ++i) { s.timings.push_back(300 + (i % 3) * 500); s.timings.push_back(-(300 + ((i + 1) % 3) * 500)); }
    }
    CHECK(!subghz::decode(s).ok());
}

// Cabecera de otros anchos antes de los datos, como el mando del garaje de Bruno (2·Te, 2·Te,
// 3·Te, 3·Te, 2·Te, 2·Te, Te, Te y luego bits 1:2 de pulso + silencio). Sin pausas: es una trama
// ya extraída, como las que guarda la app.
void test_header_skipped() {
    const int32_t te = 270;
    subghz::Signal s;
    s.timings = {2 * te, -2 * te, 3 * te, -3 * te, 2 * te, -2 * te, te, -te};
    const uint32_t code = 0xB5A3C1u; // 24 bits
    for (int b = 23; b >= 0; --b) {
        const bool one = (code >> b) & 1u;
        s.timings.push_back(one ? 2 * te : te);
        s.timings.push_back(one ? -te : -2 * te);
    }
    const subghz::Decoded d = subghz::decode(s);
    CHECK(d.protocol == subghz::Protocol::FixedCode);
    CHECK(d.bits == 24);
    CHECK(d.key == code);
    CHECK(d.te_us == 270);
}

// Manchester: medio bit de 400 µs; bit 1 = alto + bajo.
void test_manchester() {
    const int32_t half = 400;
    const uint64_t code = 0xA5C3F00Full; // 32 bits
    std::vector<bool> levels;
    for (int b = 31; b >= 0; --b) {
        const bool one = (code >> b) & 1u;
        levels.push_back(one);
        levels.push_back(!one);
    }
    subghz::Signal s;
    for (int r = 0; r < 3; ++r) {
        s.timings.push_back(-15000);
        std::size_t i = 0;
        while (i < levels.size()) {
            std::size_t j = i;
            while (j < levels.size() && levels[j] == levels[i]) ++j;
            const int32_t d = static_cast<int32_t>(j - i) * half;
            s.timings.push_back(levels[i] ? d : -d);
            i = j;
        }
    }
    const subghz::Decoded d = subghz::decode(s);
    CHECK(d.protocol == subghz::Protocol::Manchester);
    CHECK(d.bits == 32);
    CHECK(d.key == code);
    CHECK(subghz::suggested_name(d, s.timings.size()) == "MAN32-F00F");
}

// Una captura como las del aparato: ruido del receptor antes y después, y el mando (cabecera +
// 95 bits 1:2, como el del garaje) repetido seis veces, la primera cortada y dos con un parpadeo
// de 30 µs dentro de un pulso. Tiene que salir el código del mando, votado por las repeticiones.
void test_extract_from_noisy_capture() {
    const int32_t te = 270;
    std::vector<int32_t> frame = {2 * te, -2 * te, 3 * te, -3 * te, 2 * te, -2 * te, te, -te};
    std::vector<bool> code;
    uint32_t lfsr = 0x1D2Bu;
    for (int i = 0; i < 95; ++i) { lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u); code.push_back(lfsr & 1u); }
    for (bool c : code) { frame.push_back(c ? 2 * te : te); frame.push_back(c ? -te : -2 * te); }

    std::vector<int32_t> capture;
    uint32_t rnd = 12345u;
    auto noise = [&](int n) {
        for (int i = 0; i < n; ++i) {
            rnd = rnd * 1103515245u + 12345u;
            const int32_t d = 20 + static_cast<int32_t>((rnd >> 8) % 2500);
            capture.push_back(i % 2 == 0 ? d : -d);
        }
    };
    noise(301);
    capture.push_back(-12000);
    for (int r = 0; r < 6; ++r) {
        std::vector<int32_t> f = frame;
        if (r == 0) f.erase(f.begin(), f.begin() + 60);       // empezó a grabar a media trama
        if (r == 2 || r == 4) {                                // parpadeo dentro del pulso 20
            const int32_t whole = f[20];
            f[20] = whole / 2;
            f.insert(f.begin() + 21, {-30, whole - whole / 2 - 30});
        }
        capture.insert(capture.end(), f.begin(), f.end());
        capture.push_back(-11000);
    }
    noise(400);

    const subghz::Extracted e = subghz::extract_best_frame(capture);
    CHECK(e.votes >= 5);
    CHECK(e.decoded.protocol == subghz::Protocol::FixedCode);
    CHECK(e.decoded.bits == 95);
    uint64_t low = 0;
    for (int i = 95 - 64; i < 95; ++i) low = (low << 1) | (code[i] ? 1 : 0);
    CHECK(e.decoded.key == low);
    // La trama guardada se decodifica sola igual (es lo que se reemite y lo que lee la lista).
    subghz::Signal saved;
    saved.timings = e.frame;
    CHECK(subghz::decode(saved).code_hex == e.decoded.code_hex);

    // Sólo ruido: no se inventa un mando.
    capture.clear();
    noise(2000);
    CHECK(subghz::extract_best_frame(capture).votes == 0);
}

void test_deglitch() {
    CHECK((subghz::deglitch({250, -30, 260, -540}) == std::vector<int32_t>{540, -540}));
    CHECK((subghz::deglitch({300, 300, -100}) == std::vector<int32_t>{600, -100}));
    CHECK((subghz::deglitch({-40, 500}) == std::vector<int32_t>{-40, 500}));
}

} // namespace

int main() {
    test_round_trip();
    test_jitter();
    test_rejects_noise();
    test_came_and_nice();
    test_generic_fixed_code();
    test_names();
    test_rejects_three_widths();
    test_header_skipped();
    test_manchester();
    test_extract_from_noisy_capture();
    test_deglitch();
    if (failures == 0) {
        std::printf("subghz_decode_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

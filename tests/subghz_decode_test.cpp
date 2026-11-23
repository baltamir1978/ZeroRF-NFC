/*
 * SPDX-License-Identifier: MIT
 */

// Pruebas del codificador/decodificador Princeton. Sin LVGL ni hardware: corren en ctest.

#include "subghz_decode.h"

#include <cstdint>
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

} // namespace

int main() {
    test_round_trip();
    test_jitter();
    test_rejects_noise();
    if (failures == 0) {
        std::printf("subghz_decode_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

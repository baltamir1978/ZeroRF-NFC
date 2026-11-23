/*
 * SPDX-License-Identifier: MIT
 */

#include "subghz_decode.h"

#include <cmath>
#include <cstdlib>

namespace subghz {
namespace {

// Princeton/EV1527: cada bit es un par pulso+silencio en proporción 1:3.
//   bit 0 = Te alto, 3·Te bajo
//   bit 1 = 3·Te alto, Te bajo
// La trama empieza con un preámbulo de sincronismo: Te alto + 31·Te bajo. Luego 24 bits.
constexpr int kPrincetonBits = 24;
constexpr int32_t kSyncRatio = 31; // el silencio del sincronismo dura 31·Te
constexpr uint32_t kTeMinUs = 50;
constexpr uint32_t kTeMaxUs = 3000;
constexpr double kTolerance = 0.40; // ±40 %: los osciladores RC de estos mandos varían mucho

// ¿Está `value` cerca de `reference` dentro de la tolerancia?
bool near(int32_t value, int32_t reference) {
    const double delta = std::abs(static_cast<double>(value) - reference);
    return delta <= reference * kTolerance;
}

// Decodifica 24 pares (alto, bajo) a partir de `start`, con tiempo base `te`. Devuelve si
// todos los bits encajaron; deja el código en `key`.
bool decode_pairs(const std::vector<int32_t>& t, std::size_t start, int32_t te, uint64_t& key) {
    key = 0;
    const std::size_t need = start + static_cast<std::size_t>(kPrincetonBits) * 2;
    if (need > t.size()) {
        return false;
    }
    const int32_t te_long = te * 3;
    for (int b = 0; b < kPrincetonBits; ++b) {
        const int32_t high = t[start + static_cast<std::size_t>(b) * 2];
        const int32_t low_signed = t[start + static_cast<std::size_t>(b) * 2 + 1];
        if (high <= 0 || low_signed >= 0) {
            return false; // tiene que ser pulso (+) seguido de silencio (-)
        }
        const int32_t low = -low_signed;

        int bit;
        if (near(high, te) && near(low, te_long)) {
            bit = 0;
        } else if (near(high, te_long) && near(low, te)) {
            bit = 1;
        } else {
            return false;
        }
        key = (key << 1) | static_cast<uint64_t>(bit);
    }
    return true;
}

} // namespace

const char* protocol_name(Protocol protocol) {
    switch (protocol) {
        case Protocol::Princeton:
            return "Princeton";
        case Protocol::Unknown:
        default:
            return "Unknown";
    }
}

Decoded decode(const Signal& signal) {
    const auto& t = signal.timings;

    // Buscamos el silencio largo del sincronismo; de su duración sale Te (= silencio / 31) y
    // justo después vienen los 24 bits.
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] >= 0) {
            continue; // sólo nos interesan los silencios
        }
        const int32_t gap = -t[i];
        const int32_t te = gap / kSyncRatio;
        if (te < static_cast<int32_t>(kTeMinUs) || te > static_cast<int32_t>(kTeMaxUs)) {
            continue;
        }
        if (!near(gap, te * kSyncRatio)) {
            continue; // el silencio no cuadra como 31·Te exactos
        }

        uint64_t key = 0;
        // Lo normal es sincronismo y luego los bits (EV1527); probamos también por si el
        // fichero empieza a media trama y el sincronismo queda detrás.
        if (decode_pairs(t, i + 1, te, key) ||
            (i >= static_cast<std::size_t>(kPrincetonBits) * 2 &&
             decode_pairs(t, i - static_cast<std::size_t>(kPrincetonBits) * 2, te, key))) {
            Decoded result;
            result.protocol = Protocol::Princeton;
            result.key = key;
            result.bits = kPrincetonBits;
            result.te_us = static_cast<uint32_t>(te);
            result.frequency_hz = signal.frequency_hz;
            return result;
        }
    }

    return Decoded{};
}

Signal encode_princeton(uint64_t key,
                        uint8_t bits,
                        uint32_t te_us,
                        uint32_t frequency_hz,
                        int repeats) {
    Signal signal;
    signal.frequency_hz = frequency_hz;
    signal.modulation = Modulation::Ook;

    const int32_t te = static_cast<int32_t>(te_us);
    const int32_t te_long = te * 3;
    if (repeats < 1) {
        repeats = 1;
    }

    for (int r = 0; r < repeats; ++r) {
        // Sincronismo: pulso corto + silencio largo.
        signal.timings.push_back(te);
        signal.timings.push_back(-(te * kSyncRatio));
        // Bits, del más significativo al menos (orden de emisión).
        for (int b = bits - 1; b >= 0; --b) {
            const bool one = (key >> b) & 1ULL;
            if (one) {
                signal.timings.push_back(te_long);
                signal.timings.push_back(-te);
            } else {
                signal.timings.push_back(te);
                signal.timings.push_back(-te_long);
            }
        }
    }

    return signal;
}

} // namespace subghz

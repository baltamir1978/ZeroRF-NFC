/*
 * SPDX-License-Identifier: MIT
 */

#include "subghz_decode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

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

// --- Código fijo genérico ------------------------------------------------------------------
//
// Casi todos los mandos de código fijo modulan igual: cada bit es un par de elementos (pulso y
// silencio, o silencio y pulso), uno corto y otro largo, y el orden dice si es 0 o 1. Entre
// tramas hay una pausa larga. Así que, sin saber la marca: se parte la captura por las pausas,
// se coge la trama que más se repite, se comprueba que sólo hay dos anchos y se emparejan.

constexpr int32_t kFrameGapUs = 8000;   // una pausa de más de 8 ms separa tramas
constexpr int32_t kGlitchUs = 60;       // por debajo, ruido del receptor
constexpr std::size_t kMinFrameElements = 16;
constexpr int kMinBits = 8;

std::vector<std::vector<int32_t>> split_frames(const std::vector<int32_t>& t) {
    std::vector<std::vector<int32_t>> frames;
    std::vector<int32_t> cur;
    for (int32_t d : t) {
        if (d < 0 && -d >= kFrameGapUs) {
            if (cur.size() >= kMinFrameElements) frames.push_back(cur);
            cur.clear();
        } else if (std::abs(d) >= kGlitchUs) {
            cur.push_back(d);
        }
    }
    if (cur.size() >= kMinFrameElements) frames.push_back(cur);
    return frames;
}

// La trama de la longitud más repetida (con empate, la más larga): las que salen cortadas o con
// ruido al principio y al final de la captura quedan fuera.
const std::vector<int32_t>* modal_frame(const std::vector<std::vector<int32_t>>& frames) {
    std::map<std::size_t, int> count;
    for (const auto& f : frames) ++count[f.size()];
    std::size_t best = 0;
    int best_n = 0;
    for (const auto& [len, n] : count) {
        if (n > best_n || (n == best_n && len > best)) { best = len; best_n = n; }
    }
    for (const auto& f : frames) if (f.size() == best) return &f;
    return nullptr;
}

int32_t median(std::vector<int32_t> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// ¿Sólo hay dos anchos en la trama? Deja la mediana de los cortos y de los largos.
bool two_widths(const std::vector<int32_t>& frame, int32_t& te_short, int32_t& te_long) {
    std::vector<int32_t> abs_values;
    for (int32_t d : frame) abs_values.push_back(std::abs(d));
    const int32_t shortest = *std::min_element(abs_values.begin(), abs_values.end());
    std::vector<int32_t> shorts, longs;
    for (int32_t v : abs_values) (v < shortest * 3 / 2 ? shorts : longs).push_back(v);
    if (shorts.empty() || longs.empty()) return false;
    te_short = median(shorts);
    te_long = median(longs);
    const double ratio = static_cast<double>(te_long) / te_short;
    if (ratio < 1.5 || ratio > 4.5) return false;
    for (int32_t v : abs_values) {
        if (!near(v, te_short) && !near(v, te_long)) return false;
    }
    return true;
}

// Empareja la trama desde `offset` (0 = el par empieza por el primer elemento; 1 = hay un
// elemento de arranque suelto). Cada par tiene que ser corto+largo o largo+corto: 1 si el
// primero es el largo. Sobra como mucho un elemento al final (su pareja era la pausa).
bool pair_bits(const std::vector<int32_t>& frame, std::size_t offset, int32_t te_short, int32_t te_long,
               std::vector<bool>& bits) {
    bits.clear();
    const auto is_long = [&](int32_t d) {
        return std::abs(std::abs(d) - te_long) < std::abs(std::abs(d) - te_short);
    };
    std::size_t i = offset;
    for (; i + 1 < frame.size(); i += 2) {
        const bool a = is_long(frame[i]);
        const bool b = is_long(frame[i + 1]);
        if (a == b) return false;
        bits.push_back(a);
    }
    return static_cast<int>(bits.size()) >= kMinBits;
}

std::string bits_to_hex(const std::vector<bool>& bits) {
    static const char* digits = "0123456789ABCDEF";
    std::string hex;
    const std::size_t pad = (4 - bits.size() % 4) % 4;
    int nibble = 0, filled = static_cast<int>(pad);
    for (bool b : bits) {
        nibble = (nibble << 1) | (b ? 1 : 0);
        if (++filled == 4) { hex += digits[nibble]; nibble = 0; filled = 0; }
    }
    return hex;
}

uint64_t bits_to_key(const std::vector<bool>& bits) {
    uint64_t key = 0;
    const std::size_t from = bits.size() > 64 ? bits.size() - 64 : 0;
    for (std::size_t i = from; i < bits.size(); ++i) key = (key << 1) | (bits[i] ? 1 : 0);
    return key;
}

bool between(double v, double lo, double hi) { return v >= lo && v <= hi; }

} // namespace

const char* protocol_name(Protocol protocol) {
    switch (protocol) {
        case Protocol::Princeton:
            return "Princeton";
        case Protocol::Came:
            return "CAME";
        case Protocol::NiceFlo:
            return "NICE FLO";
        case Protocol::FixedCode:
            return "Fixed code";
        case Protocol::Unknown:
        default:
            return "Unknown";
    }
}

Decoded decode(const Signal& signal) {
    const auto& t = signal.timings;

    // Primero la forma: si la trama tiene claramente otra proporción u otro número de bits,
    // manda eso. El buscador de Princeton de abajo, con su ±40 %, aceptaría un mando 1:2 como si
    // fuera 1:3 y daría 24 bits inventados.
    const Decoded fixed = decode_fixed(signal);
    if (fixed.ok() && fixed.protocol != Protocol::Princeton) {
        return fixed;
    }

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
            result.te_long_us = static_cast<uint32_t>(te * 3);
            result.frequency_hz = signal.frequency_hz;
            char hex[17];
            std::snprintf(hex, sizeof(hex), "%06llX", static_cast<unsigned long long>(key));
            result.code_hex = hex;
            return result;
        }
    }

    return fixed;
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

Decoded decode_fixed(const Signal& signal) {
    const auto frames = split_frames(signal.timings);
    const std::vector<int32_t>* frame = modal_frame(frames);
    if (frame == nullptr) return Decoded{};

    int32_t te_short = 0, te_long = 0;
    if (!two_widths(*frame, te_short, te_long)) return Decoded{};
    const double ratio = static_cast<double>(te_long) / te_short;

    // Los dos emparejamientos posibles. En CAME y NICE el par es silencio + pulso tras un pulso
    // de arranque; en Princeton y casi todos los demás, pulso + silencio desde el principio.
    std::vector<bool> pulse_first, silence_first;
    const std::size_t pulse_offset = (*frame)[0] > 0 ? 0 : 1;
    const bool pf = pair_bits(*frame, pulse_offset, te_short, te_long, pulse_first);
    const bool sf = pair_bits(*frame, pulse_offset + 1, te_short, te_long, silence_first);

    Decoded result;
    result.te_us = static_cast<uint32_t>(te_short);
    result.te_long_us = static_cast<uint32_t>(te_long);
    result.frequency_hz = signal.frequency_hz;

    const std::vector<bool>* bits = nullptr;
    if (sf && between(ratio, 1.6, 2.5) && (silence_first.size() == 12 || silence_first.size() == 24)) {
        if (between(te_short, 250, 420)) { result.protocol = Protocol::Came; bits = &silence_first; }
        else if (between(te_short, 550, 900)) { result.protocol = Protocol::NiceFlo; bits = &silence_first; }
    }
    if (bits == nullptr && pf && between(ratio, 2.4, 3.6) && pulse_first.size() == 24) {
        result.protocol = Protocol::Princeton;
        bits = &pulse_first;
    }
    if (bits == nullptr && (pf || sf)) {
        result.protocol = Protocol::FixedCode;
        bits = pf ? &pulse_first : &silence_first;
    }
    if (bits == nullptr) return Decoded{};

    result.bits = static_cast<uint8_t>(std::min<std::size_t>(bits->size(), 255));
    result.key = bits_to_key(*bits);
    result.code_hex = bits_to_hex(*bits);
    return result;
}

std::string describe(const Decoded& d) {
    if (!d.ok()) return "Raw signal (unknown format)";
    std::string hex = d.code_hex;
    if (hex.size() > 12) hex = hex.substr(0, 11) + "...";
    char buf[128];
    if (d.protocol == Protocol::FixedCode && d.te_us > 0) {
        std::snprintf(buf, sizeof(buf), "%s %u bit  0x%s  %u/%u us", protocol_name(d.protocol),
                      static_cast<unsigned>(d.bits), hex.c_str(), static_cast<unsigned>(d.te_us),
                      static_cast<unsigned>(d.te_long_us));
    } else {
        std::snprintf(buf, sizeof(buf), "%s %u bit  0x%s  Te %u us", protocol_name(d.protocol),
                      static_cast<unsigned>(d.bits), hex.c_str(), static_cast<unsigned>(d.te_us));
    }
    return buf;
}

std::string suggested_name(const Decoded& d, std::size_t timings) {
    char buf[40];
    switch (d.protocol) {
        case Protocol::Princeton:
            std::snprintf(buf, sizeof(buf), "PT-%06llX", static_cast<unsigned long long>(d.key & 0xFFFFFF));
            return buf;
        case Protocol::Came:
            return "CAME-" + d.code_hex;
        case Protocol::NiceFlo:
            return "NICE-" + d.code_hex;
        case Protocol::FixedCode: {
            const std::string tail = d.code_hex.size() > 4 ? d.code_hex.substr(d.code_hex.size() - 4) : d.code_hex;
            std::snprintf(buf, sizeof(buf), "FIX%u-%s", static_cast<unsigned>(d.bits), tail.c_str());
            return buf;
        }
        case Protocol::Unknown:
        default:
            std::snprintf(buf, sizeof(buf), "RAW-%u", static_cast<unsigned>(timings));
            return buf;
    }
}

Signal encode_came_like(uint64_t key, uint8_t bits, uint32_t te_us, uint32_t frequency_hz, int repeats) {
    Signal signal;
    signal.frequency_hz = frequency_hz;
    signal.modulation = Modulation::Ook;
    const int32_t te = static_cast<int32_t>(te_us);
    for (int r = 0; r < std::max(1, repeats); ++r) {
        signal.timings.push_back(-(te * 36)); // pausa entre tramas
        signal.timings.push_back(te);         // pulso de arranque
        for (int b = bits - 1; b >= 0; --b) {
            const bool one = (key >> b) & 1ULL;
            signal.timings.push_back(one ? -(te * 2) : -te);
            signal.timings.push_back(one ? te : te * 2);
        }
    }
    return signal;
}

} // namespace subghz

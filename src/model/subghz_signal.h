/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace subghz {

// Modulación de la portadora. El CC1101 hace las dos; los mandos de garaje de 433 casi
// siempre son OOK (on-off keying, = AM).
enum class Modulation {
    Ook,
    Fsk2,
};

// Nombre del «preset» tal como lo escribe el Flipper en sus ficheros .sub, para que los
// ficheros sean intercambiables con él.
const char* modulation_preset(Modulation modulation);
bool parse_modulation_preset(std::string_view preset, Modulation& modulation);

/**
 * Una señal Sub-GHz capturada en crudo: la ristra de tiempos en microsegundos, con signo
 * (positivo = portadora encendida / pulso; negativo = apagada / silencio), igual que el
 * RAW_Data del Flipper. Más la frecuencia y la modulación con que se capturó.
 */
struct Signal {
    std::string name;
    uint32_t frequency_hz = 433920000; // 433.92 MHz, la banda habitual de los mandos
    Modulation modulation = Modulation::Ook;
    std::vector<int32_t> timings; // µs, + pulso / - silencio

    bool empty() const {
        return timings.empty();
    }
};

} // namespace subghz

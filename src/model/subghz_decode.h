/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subghz_signal.h"

#include <cstdint>

namespace subghz {

// Protocolos de código fijo que sabemos reconocer. De momento Princeton/EV1527, que es el
// de la inmensa mayoría de los mandos de garaje y cancela baratos de 433 MHz. La lista
// crecerá (CAME, NICE FLO…) sin tocar quien la use.
enum class Protocol {
    Unknown,
    Princeton, // también cubre EV1527, el clónico más común
};

const char* protocol_name(Protocol protocol);

/** Resultado de decodificar una señal en crudo a un código fijo. */
struct Decoded {
    Protocol protocol = Protocol::Unknown;
    uint64_t key = 0;        // el código, con los bits en orden de emisión (MSB primero)
    uint8_t bits = 0;        // cuántos bits tiene el código (Princeton: 24)
    uint32_t te_us = 0;      // tiempo base del protocolo (el elemento corto), en µs
    uint32_t frequency_hz = 0;

    bool ok() const {
        return protocol != Protocol::Unknown;
    }
};

/**
 * Intenta reconocer un código fijo en la señal. Si no encaja con ningún protocolo conocido
 * devuelve `Protocol::Unknown` (la señal se puede reemitir en crudo igualmente).
 */
Decoded decode(const Signal& signal);

/**
 * Construye una señal en crudo para un código Princeton/EV1527, lista para reemitir (o para
 * las pruebas). `repeats` es cuántas veces se repite la trama (los mandos repiten varias).
 */
Signal encode_princeton(uint64_t key,
                        uint8_t bits,
                        uint32_t te_us,
                        uint32_t frequency_hz,
                        int repeats);

} // namespace subghz

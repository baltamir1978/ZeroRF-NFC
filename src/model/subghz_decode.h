/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subghz_signal.h"

#include <cstdint>
#include <string>

namespace subghz {

// Protocolos de código fijo que sabemos reconocer: Princeton/EV1527 (la inmensa mayoría de los
// mandos de garaje y cancela baratos de 433 MHz), CAME y NICE FLO (los de código fijo de esas
// marcas, 12 o 24 bits). Lo que tenga forma de código fijo (pulsos de dos anchos, uno corto y
// otro largo, en pares) pero no case con ninguno sale como `FixedCode`: se sabe cuántos bits
// tiene y cuáles son, aunque no de qué marca es.
enum class Protocol {
    Unknown,
    Princeton, // también cubre EV1527, el clónico más común
    Came,
    NiceFlo,
    FixedCode, // código fijo de forma reconocible, marca desconocida
    Manchester, // codificación Manchester (medio bit corto o largo), marca desconocida
};

const char* protocol_name(Protocol protocol);

/** Resultado de decodificar una señal en crudo a un código fijo. */
struct Decoded {
    Protocol protocol = Protocol::Unknown;
    uint64_t key = 0;        // el código, con los bits en orden de emisión (MSB primero)
    uint8_t bits = 0;        // cuántos bits tiene el código (Princeton: 24)
    uint32_t te_us = 0;      // tiempo base del protocolo (el elemento corto), en µs
    uint32_t te_long_us = 0; // el elemento largo, en µs (Princeton: 3·Te; CAME/NICE: 2·Te)
    uint32_t frequency_hz = 0;
    std::string code_hex;    // todos los bits en hexadecimal (MSB primero), también si son > 64

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
 * Analiza la forma de una señal de código fijo sin suponer protocolo (ver `Protocol`). Lo usa
 * `decode` después de probar Princeton; está aparte para las pruebas.
 */
Decoded decode_fixed(const Signal& signal);

/** "Princeton 24 bit · 0x5A3C1F · Te 350 µs" — una línea para enseñar en la pantalla. */
std::string describe(const Decoded& decoded);

/**
 * Nombre de fichero propuesto para una señal recién capturada: "PT-5A3C1F", "CAME-5A3",
 * "NICE-0F1", "FIX77-9C2E" (bits + los últimos 4 dígitos), "MAN97-1A2B" o "RAW-<n>".
 */
std::string suggested_name(const Decoded& decoded, std::size_t timings);

/**
 * Construye una señal en crudo para un código CAME o NICE FLO (los dos son iguales salvo el
 * tiempo base: pausa de 36·Te, pulso de arranque Te y, por bit, silencio + pulso: 1 = largo +
 * corto, 0 = corto + largo). Para las pruebas y, más adelante, para crear mandos a mano.
 */
Signal encode_came_like(uint64_t key, uint8_t bits, uint32_t te_us, uint32_t frequency_hz, int repeats);

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

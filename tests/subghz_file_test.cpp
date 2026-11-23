/*
 * SPDX-License-Identifier: MIT
 */

// Pruebas del formato .sub (lectura/escritura compatible con el Flipper). Sin hardware.

#include "subghz_decode.h"
#include "subghz_file.h"

#include <cstdio>
#include <sstream>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

// Guardar y volver a leer deja la misma señal.
void test_save_load_round_trip() {
    const subghz::Signal original = subghz::encode_princeton(0x5A5A5AULL, 24, 350, 433920000, 3);

    std::ostringstream out;
    std::string error;
    CHECK(subghz::save_sub(out, original, error));
    CHECK(error.empty());

    std::istringstream in(out.str());
    subghz::Signal loaded;
    CHECK(subghz::load_sub(in, loaded, error));
    CHECK(error.empty());

    CHECK(loaded.frequency_hz == original.frequency_hz);
    CHECK(loaded.modulation == original.modulation);
    CHECK(loaded.timings == original.timings);

    // Y lo leído se sigue decodificando igual.
    const subghz::Decoded decoded = subghz::decode(loaded);
    CHECK(decoded.ok());
    CHECK(decoded.key == 0x5A5A5AULL);
}

// Lee un .sub escrito a mano, con la cabecera y varias líneas RAW_Data.
void test_parse_handwritten() {
    const std::string text =
        "Filetype: Flipper SubGhz RAW File\n"
        "Version: 1\n"
        "Frequency: 868350000\n"
        "Preset: FuriHalSubGhzPresetOok650Async\n"
        "Protocol: RAW\n"
        "RAW_Data: 350 -1050 1050 -350\n"
        "RAW_Data: 350 -10850\n";

    std::istringstream in(text);
    subghz::Signal signal;
    std::string error;
    CHECK(subghz::load_sub(in, signal, error));
    CHECK(signal.frequency_hz == 868350000U);
    CHECK(signal.modulation == subghz::Modulation::Ook);
    CHECK(signal.timings.size() == 6);
    CHECK(signal.timings[0] == 350);
    CHECK(signal.timings[1] == -1050);
    CHECK(signal.timings[5] == -10850);
}

// Un fichero sin frecuencia o sin datos es un error, no una señal vacía silenciosa.
void test_rejects_incomplete() {
    std::string error;

    std::istringstream no_freq("Protocol: RAW\nRAW_Data: 350 -350\n");
    subghz::Signal a;
    CHECK(!subghz::load_sub(no_freq, a, error));

    std::istringstream no_data("Frequency: 433920000\nProtocol: RAW\n");
    subghz::Signal b;
    CHECK(!subghz::load_sub(no_data, b, error));
}

} // namespace

int main() {
    test_save_load_round_trip();
    test_parse_handwritten();
    test_rejects_incomplete();
    if (failures == 0) {
        std::printf("subghz_file_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subghz_signal.h"

#include <iosfwd>
#include <string>

namespace subghz {

// Lectura y escritura del formato .sub en crudo del Flipper Zero, para que las señales sean
// intercambiables con él. Cabecera + líneas RAW_Data con los tiempos con signo:
//
//   Filetype: Flipper SubGhz RAW File
//   Version: 1
//   Frequency: 433920000
//   Preset: FuriHalSubGhzPresetOok650Async
//   Protocol: RAW
//   RAW_Data: 350 -10850 1050 -350 ...
//
// Todas las funciones devuelven si han tenido éxito y, si no, dejan el motivo en `error`.

bool save_sub(std::ostream& out, const Signal& signal, std::string& error);
bool load_sub(std::istream& in, Signal& signal, std::string& error);

bool save_sub_file(const std::string& path, const Signal& signal, std::string& error);
bool load_sub_file(const std::string& path, Signal& signal, std::string& error);

} // namespace subghz

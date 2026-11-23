/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace nfc {

// Una clave MIFARE Classic son 6 bytes.
using MifareKey = std::array<uint8_t, 6>;

/**
 * Diccionario de claves «de fábrica» más habituales (la A por defecto, transporte, etc.).
 * Nivel 1 del alcance acordado: leer tarjetas cuyas claves se conocen o son de las típicas,
 * sin ataques de recuperación de claves. Bruno puede añadir las suyas con un fichero.
 */
const std::vector<MifareKey>& default_keys();

// "A0A1A2A3A4A5" (12 hex, con o sin espacios) -> clave. `false` si no son 6 bytes válidos.
bool parse_key(const std::string& hex, MifareKey& key);
std::string key_to_hex(const MifareKey& key);

// Lee un diccionario de un flujo: una clave por línea; se ignoran líneas vacías y las que
// empiezan por '#'. Las líneas inválidas se saltan.
std::vector<MifareKey> parse_key_dictionary(std::istream& in);

} // namespace nfc

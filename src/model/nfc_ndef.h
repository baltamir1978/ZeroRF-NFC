/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfc {

// Qué clase de registro NDEF es. Reconocemos los dos más habituales en tarjetas (URI y
// texto); el resto se marca como Other, guardando el nombre de su tipo.
enum class NdefRecordType {
    Empty,
    Uri,
    Text,
    Other,
};

struct NdefRecord {
    NdefRecordType type = NdefRecordType::Other;
    std::string content;   // la URI completa, o el texto; vacío en Other
    std::string language;  // sólo en Text (p. ej. "en")
    std::string type_name; // el TYPE en claro cuando es Other (p. ej. "application/json")
};

struct NdefMessage {
    std::vector<NdefRecord> records;
};

/** Parsea un mensaje NDEF a partir de sus bytes. `false` si la estructura está corrupta. */
bool parse_ndef_message(const uint8_t* data, std::size_t len, NdefMessage& out);

/**
 * Busca y parsea el NDEF en el volcado de una tarjeta Type 2 (MIFARE Ultralight / NTAG):
 * recorre los TLV desde `data_start` (por defecto la página 4 = byte 16) hasta el TLV 0x03
 * (mensaje NDEF) y lo parsea. `false` si no hay NDEF o está corrupto.
 */
bool parse_type2_ndef(const std::vector<uint8_t>& memory, NdefMessage& out, std::size_t data_start = 16);

} // namespace nfc

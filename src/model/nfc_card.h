/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nfc {

// Familia de la tarjeta (lo que el ST25R3916 sabe leer).
enum class Tech {
    IsoA, // ISO14443-A (NFC-A): MIFARE, NTAG…
    IsoB, // ISO14443-B
    IsoF, // FeliCa (NFC-F)
    IsoV, // ISO15693 (NFC-V)
};

// Tipo concreto, deducido del ATQA/SAK en las ISO-A.
enum class CardType {
    Unknown,
    MifareUltralight, // también NTAG21x (SAK 0x00)
    MifareClassicMini,
    MifareClassic1k,
    MifareClassic4k,
    MifarePlus,
    MifareDesfire, // ISO14443-4 / IsoDep
    IsoDep,        // otra 14443-4 no identificada
    Felica,
    Iso15693,
};

const char* tech_name(Tech tech);
const char* card_type_name(CardType type);

// Token corto y estable para guardar en fichero (p. ej. "mifare_classic_1k"), y su inverso.
const char* card_type_token(CardType type);
bool parse_card_type_token(const std::string& token, CardType& type);
// Tecnología que corresponde a un tipo (ISO-A salvo FeliCa/ISO15693).
Tech tech_for_type(CardType type);

/**
 * Deduce el tipo de una tarjeta ISO-A a partir de su ATQA (2 bytes) y su SAK. Es la tabla
 * estándar (documentada por NXP); no lee la tarjeta, sólo clasifica.
 */
CardType detect_card_type(uint16_t atqa, uint8_t sak);

/** Una tarjeta escaneada: identidad y, si se ha volcado, su memoria. */
struct Card {
    Tech tech = Tech::IsoA;
    CardType type = CardType::Unknown;
    std::vector<uint8_t> uid;
    uint16_t atqa = 0; // ISO-A
    uint8_t sak = 0;   // ISO-A

    // Volcado de memoria (bloques de MIFARE Classic o páginas de UL/NTAG), concatenado.
    std::vector<uint8_t> memory;
    uint16_t block_size = 0; // 16 en Classic, 4 en UL/NTAG; 0 si no hay volcado

    // Para afinar el tipo exacto: respuesta a GET_VERSION (UL/NTAG) y el ATS (ISO14443-4).
    std::vector<uint8_t> version; // 8 bytes de GET_VERSION, si se leyó
    std::vector<uint8_t> ats;     // ATS de una tarjeta ISO-DEP (DESFire, etc.), si se leyó

    bool has_memory() const {
        return block_size > 0 && !memory.empty();
    }
};

// Nombre exacto del producto a partir del tipo + GET_VERSION + ATS (p. ej. "NTAG215",
// "MIFARE DESFire EV1"). Si no hay datos finos, cae en `card_type_name`.
std::string card_product(const Card& card);

// UID / bytes en hexadecimal con espacios ("04 A2 2B 10"). Vacío -> "".
std::string to_hex(const std::vector<uint8_t>& bytes);
// Lee hex con o sin espacios a bytes. Devuelve false si hay un carácter no hexadecimal o
// un número impar de dígitos.
bool from_hex(const std::string& text, std::vector<uint8_t>& bytes);

} // namespace nfc

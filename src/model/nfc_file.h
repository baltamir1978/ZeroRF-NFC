/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nfc_card.h"

#include <iosfwd>
#include <string>

namespace nfc {

// Lectura y escritura de tarjetas en un fichero de texto estilo .nfc del Flipper (cabecera +
// UID/ATQA/SAK + volcado en líneas "Block N:" para MIFARE Classic o "Page N:" para
// Ultralight/NTAG). Hace ida y vuelta con nuestro modelo `Card`; la compatibilidad exacta con
// todas las versiones del Flipper (firmas, contadores…) queda para más adelante.

bool save_nfc(std::ostream& out, const Card& card, std::string& error);
bool load_nfc(std::istream& in, Card& card, std::string& error);

bool save_nfc_file(const std::string& path, const Card& card, std::string& error);
bool load_nfc_file(const std::string& path, Card& card, std::string& error);

} // namespace nfc

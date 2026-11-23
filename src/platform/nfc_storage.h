/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

namespace platform {

// Dónde se guardan las tarjetas NFC (.nfc): $XDG_DATA_HOME (o ~/.local/share)/zerorfnfc/nfc.
std::string nfc_dir();
std::vector<std::string> list_cards();
std::string card_path(const std::string& name);
std::string unique_card_name(const std::string& base);
bool delete_card(const std::string& name);
// Cambia el nombre de una tarjeta guardada. false si el nuevo ya existe o no se puede.
bool rename_card(const std::string& from, const std::string& to);

} // namespace platform

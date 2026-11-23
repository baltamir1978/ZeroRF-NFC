/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

namespace platform {

// Dónde se guardan las señales Sub-GHz grabadas (ficheros .sub). En el aparato y en el
// escritorio: $XDG_DATA_HOME (o ~/.local/share) / zerorfnfc / subghz. Se crea si no existe.
std::string subghz_dir();

// Nombres (sin la extensión .sub) de las señales guardadas, ordenados alfabéticamente.
std::vector<std::string> list_signals();

// Ruta completa del fichero .sub de una señal por su nombre.
std::string signal_path(const std::string& name);

// A partir de `base`, un nombre que no choque con los que ya hay (base, base-2, base-3…).
std::string unique_signal_name(const std::string& base);

// Borra la señal `name`. Devuelve si se borró.
bool delete_signal(const std::string& name);

} // namespace platform

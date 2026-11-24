/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nfc_card.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nfc {

/**
 * Cómo está repartida la memoria de una etiqueta Type 2 (NTAG / MIFARE Ultralight). Las páginas
 * 0-3 son UID, bloqueo estático y CC; luego viene la memoria de usuario; y al final, según el
 * modelo, bloqueo dinámico, configuración, contraseña y PACK. Escribir esas últimas copiando un
 * volcado puede **bloquear la etiqueta para siempre** o ponerle contraseña (la contraseña se lee
 * como ceros), así que al clonar sólo se toca la memoria de usuario.
 */
struct Type2Layout {
    std::string name;    // "NTAG215", "Ultralight EV1 (MF0UL11)", …; vacío si no se sabe
    int total_pages = 0; // páginas que tiene la etiqueta
    int first_user = 4;
    int last_user = 0;   // última página de usuario (incluida); 0 = desconocida

    bool known() const { return last_user >= first_user; }
    int user_pages() const { return known() ? last_user - first_user + 1 : 0; }
};

/**
 * La disposición a partir de GET_VERSION (byte 6 = tamaño). Sin GET_VERSION (la Ultralight
 * original no lo tiene, y las tarjetas guardadas antes de la 0.3.0 no lo apuntaban) se deduce del
 * número de páginas volcadas, que es distinto en cada modelo.
 */
Type2Layout type2_layout(const std::vector<uint8_t>& version, std::size_t dumped_pages);

/** La disposición de una tarjeta guardada (por su GET_VERSION y lo que se volcó). */
Type2Layout type2_layout(const Card& card);

/** ¿El bloqueo estático (bytes 2 y 3 de la página 2) tiene algún bit puesto? */
bool type2_static_locked(const std::array<uint8_t, 4>& page2);

/** Qué páginas escribir para clonar `source` en una etiqueta con disposición `target`. */
struct Type2WritePlan {
    std::vector<std::pair<int, std::array<uint8_t, 4>>> pages;
    std::string error; // si no se puede: por qué (en inglés: sale en la pantalla)

    bool ok() const { return error.empty() && !pages.empty(); }
};

/**
 * Las páginas de usuario de `source` que caben en `target`. Si la etiqueta de destino es más
 * pequeña sólo vale si lo que sobra está a cero (una NDEF corta cabe en una NTAG213 aunque venga
 * de una NTAG215). Nunca incluye páginas 0-3 ni las de después de la memoria de usuario.
 */
Type2WritePlan plan_type2_write(const Card& source, const Type2Layout& target);

} // namespace nfc

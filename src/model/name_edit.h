/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace text {

// Código de la tecla de borrar (LV_KEY_BACKSPACE). El modelo no depende de LVGL; la pantalla
// comprueba con un static_assert que coinciden.
constexpr uint32_t kBackspace = 8;

// Largo máximo de un nombre: lo que cabe en una fila de la lista de 320 px.
constexpr std::size_t kMaxNameLength = 24;

/**
 * Aplica una tecla a un nombre que se está escribiendo con el teclado del Cardputer: añade
 * letras, números, espacio y `- _ . +`, y borra con Backspace. Devuelve true si la tecla se ha
 * usado (para que no llegue a otros atajos, como la T del tema).
 */
bool edit_name(std::string& name, uint32_t key, std::size_t max_length = kMaxNameLength);

/**
 * Deja un nombre listo para usarlo como nombre de fichero: sin espacios al principio ni al
 * final ni repetidos, sin `/`, y sin empezar por `.` (no se vería en la lista). Puede quedar
 * vacío: entonces no hay nombre válido.
 */
std::string clean_file_name(const std::string& name);

} // namespace text

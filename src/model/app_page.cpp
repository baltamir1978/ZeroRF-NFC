/*
 * SPDX-License-Identifier: MIT
 */

// El «modelo» (lógica sin LVGL ni hardware) todavía no tiene nada propio: Sub-GHz y NFC
// irán aquí (decodificadores de protocolo, parseo NDEF…), con sus pruebas en tests/.
// Esta unidad existe para que la librería `..._model` tenga al menos una fuente.

#include "app_page.h"

namespace model {

// Número de herramientas del menú; sirve de comprobación de que el enum está en orden.
int tool_count() {
    return static_cast<int>(AppPage::Nfc);
}

} // namespace model

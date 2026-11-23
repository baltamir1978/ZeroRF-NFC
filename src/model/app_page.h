/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace model {

// ZeroRF-NFC tiene un menú (Home) y una pantalla por herramienta del Cap CC1101+NFC.
// El valor entero es el que viaja por el «subject» de la página.
enum class AppPage {
    Home = 0,
    SubGhz = 1, // mando de garaje 433 MHz (CC1101)
    Nfc = 2,    // leer/escribir/emular tarjetas (ST25R3916)
};

} // namespace model

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

namespace platform {

/**
 * Enciende y apaga el Cap CC1101+NFC y deja listo el SPI del ST25R3916. Es lo que la sesión de
 * `plex` averiguó en el aparato el 23-nov-2026 trazando la app oficial `NFC`:
 *
 * 1. `ext_usb_gpio_fun` se deja en 0 (HAT en modo GPIO/SPI; a 1 el chip deja de verse).
 * 2. GPIO26 (POWER_EN del cap) como salida a 1 **mientras viva el proceso** (se retiene con el
 *    descriptor de línea de `/dev/gpiochip0`; al cerrarlo se suelta). Sin esto el chip contesta
 *    por SPI pero el driver de antena no tiene corriente.
 * 3. `ext_5v_out` = 1.
 * 4. Overlay `spi0-spidev2-gpio22-overlay` → `/dev/spidev0.2` (CS2 = GPIO22, el CS del ST25R3916).
 *
 * Lo que pide root (overlay, LED) se hace con `sudo -n` si la app no es root: el usuario del
 * Cardputer tiene sudo sin contraseña. Así la app sigue corriendo como el usuario y sus ficheros
 * (`~/.local/share/zerorfnfc`) no acaban en `/root`.
 */
class CapPower {
public:
    static constexpr const char* kNfcSpiDevice = "/dev/spidev0.2";

    CapPower() = default;
    ~CapPower();

    CapPower(const CapPower&) = delete;
    CapPower& operator=(const CapPower&) = delete;

    /** Enciende el cap y prepara el SPI del NFC. false + `error()` si algo falla. */
    bool power_on_nfc();
    void power_off();

    const std::string& error() const {
        return error_;
    }

private:
    bool hold_power_en();
    void release_power_en();

    int chip_fd_ = -1;
    int line_fd_ = -1;
    bool on_ = false;
    std::string error_;
};

} // namespace platform

/*
 * Driver del CC1101 (Sub-GHz del Cap CC1101+NFC del Cardputer Zero) sobre spidev de Linux.
 *
 * El mapa de registros (`cc1101_regs.h`) y la secuencia de acceso SPI vienen del firmware del
 * Flipper Zero (GPLv3); aquí se adaptan a spidev + libgpiod. Por eso la app es GPL-3.0-or-later.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "cc1101_regs.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace cc {

/**
 * CC1101 por `/dev/spidev0.1` (CS = CE1 = GPIO7 en el Cap). Comparte el bus SPI0 con el
 * ST25R3916: solo uno activo a la vez (el cap, además, solo admite un chip a la vez). La
 * alimentación del cap (POWER_EN=GPIO26 + ext_5v_out) la gestiona quien use este driver
 * (igual que el NFC, con `platform::CapPower`).
 */
class Cc1101 {
public:
    static constexpr const char* kSpiDevice = "/dev/spidev0.1";

    bool open(const std::string& dev = kSpiDevice, uint32_t speed_hz = 1'000'000);
    void close();
    ~Cc1101();

    // --- acceso de bajo nivel ---
    uint8_t strobe(uint8_t cmd);                 // comando; devuelve el byte de estado
    void writeReg(uint8_t addr, uint8_t value);
    uint8_t readReg(uint8_t addr);
    uint8_t readStatus(uint8_t addr);            // registros de estado (0x30-0x3D), con BURST
    void writeBurst(uint8_t addr, const uint8_t* data, size_t n);
    void readBurst(uint8_t addr, uint8_t* data, size_t n);

    bool reset();                                // SRES + espera a que el chip esté listo
    uint8_t partNumber();                        // 0x00 en el CC1101
    uint8_t version();                           // 0x14 (a veces 0x04/0x17) en el CC1101
    bool present();                              // true si responde como un CC1101

    // Frecuencia en Hz (p. ej. 433920000). Devuelve la frecuencia real ajustada.
    uint32_t setFrequency(uint32_t hz);
    void setPaTable(const uint8_t table[8]);
    void writeConfig(const uint8_t (*regs)[2], size_t n);  // pares {addr, valor}

    void strobeIdle();
    void strobeRx();
    void strobeTx();
    void flushRx();
    void flushTx();
    uint8_t marcState();                         // estado de la máquina (CC1101_STATUS_MARCSTATE)

private:
    void xfer(uint8_t* buf, size_t n);
    int fd_ = -1;
    uint32_t speed_ = 1'000'000;
};

} // namespace cc

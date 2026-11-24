/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nfc_card.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace platform {

/**
 * El lector NFC del Cap (ST25R3916, 13.56 MHz). Escanea, emula y escribe tarjetas, en un hilo
 * aparte, como `Cc1101`. En el simulador (y con `ZERORFNFC_NFC=sim` en el aparato) usa un
 * backend **simulado** que «encuentra» una NTAG con un NDEF dentro, para probar la interfaz.
 * En el aparato usa el driver de `st25/` por `/dev/spidev0.2` (CS2 = GPIO22), con el cap
 * encendido por `CapPower`.
 */
class NfcSt25r3916 {
public:
    enum class State {
        Idle,
        Scanning,
        Found, // hay una tarjeta leída en `scanned()`
        Emulating,
        Writing,
        Written, // escritura acabada y verificada; el resumen va en `message`
        Stopped,
        Failed,
    };

    struct Status {
        State state = State::Idle;
        bool simulated = false;
        std::size_t activity = 0;
        std::string error;
        std::string message; // resultado de una escritura ("Wrote 126 pages to NTAG215")
    };

    NfcSt25r3916();
    ~NfcSt25r3916();

    NfcSt25r3916(const NfcSt25r3916&) = delete;
    NfcSt25r3916& operator=(const NfcSt25r3916&) = delete;

    bool is_simulated() const {
        return simulated_;
    }

    void start_scan();
    void start_emulate(const nfc::Card& card);
    /**
     * Espera una etiqueta NTAG/Ultralight y le escribe la memoria de usuario de `card` (sólo
     * eso: ni UID, ni bloqueos, ni configuración, ni contraseña; ver `nfc::plan_type2_write`).
     * Comprueba antes el modelo y el bloqueo estático de la etiqueta, y relee al acabar.
     */
    void start_write(const nfc::Card& card);
    void stop();

    bool busy() const;
    Status status() const;
    nfc::Card scanned() const;

private:
    void join_worker();
    void run_scan();
    void run_emulate(nfc::Card card);
    void run_scan_device();
    void run_emulate_device(const nfc::Card& card);
    void run_write(nfc::Card card);
    void run_write_device(const nfc::Card& card);
    void set_state(State state, const std::string& error = "");

    const bool simulated_;
    std::thread worker_;
    std::atomic<bool> stop_{false};

    mutable std::mutex mutex_;
    Status status_;
    nfc::Card scanned_;
};

} // namespace platform

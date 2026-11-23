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
 * El lector NFC del Cap (ST25R3916, 13.56 MHz). Escanea una tarjeta y la emula, en un hilo
 * aparte, como `Cc1101`. En el simulador (y con `ZERORFNFC_NFC=sim` en el aparato) usa un
 * backend **simulado** que «encuentra» una NTAG con un NDEF dentro, para probar la interfaz.
 * En el aparato hablará por `/dev/spidev0.1` + libgpiod (todavía no implementado).
 */
class NfcSt25r3916 {
public:
    enum class State {
        Idle,
        Scanning,
        Found, // hay una tarjeta leída en `scanned()`
        Emulating,
        Stopped,
        Failed,
    };

    struct Status {
        State state = State::Idle;
        bool simulated = false;
        std::size_t activity = 0;
        std::string error;
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
    void stop();

    bool busy() const;
    Status status() const;
    nfc::Card scanned() const;

private:
    void join_worker();
    void run_scan();
    void run_emulate(nfc::Card card);
    void set_state(State state, const std::string& error = "");

    const bool simulated_;
    std::thread worker_;
    std::atomic<bool> stop_{false};

    mutable std::mutex mutex_;
    Status status_;
    nfc::Card scanned_;
};

} // namespace platform

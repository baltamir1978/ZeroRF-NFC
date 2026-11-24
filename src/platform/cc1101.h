/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subghz_signal.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace platform {

/**
 * El radio Sub-GHz del Cap (TI CC1101). Graba (RX) y reemite (TX) señales OOK en crudo. El
 * trabajo va en un hilo aparte, como hacía `IrBlaster` con el LED: la pantalla pregunta el
 * estado con `status()` cada 100 ms y nunca toca el hilo.
 *
 * En el simulador (y con `ZERORFNFC_CC1101=sim` en el aparato) usa un backend **simulado**:
 * grabar inventa un código Princeton decodificable y reemitir espera el tiempo que durase la
 * emisión. En el aparato de verdad hablará por `/dev/spidev0.1` + libgpiod (todavía no
 * implementado: el cableado está en la memoria `cardputer-caps-hardware`).
 */
class Cc1101 {
public:
    enum class State {
        Idle,
        Receiving,
        Received,     // hay una señal capturada lista en `captured()`
        Transmitting,
        Done,
        Stopped,
        Failed,
    };

    struct Status {
        State state = State::Idle;
        bool simulated = false;
        std::size_t activity = 0; // cuenta que sube mientras trabaja, para dar vidilla a la UI
        int heard = 0;            // grabando: repeticiones del mismo código oídas (para a las 3)
        std::string error;
    };

    Cc1101();
    ~Cc1101();

    Cc1101(const Cc1101&) = delete;
    Cc1101& operator=(const Cc1101&) = delete;

    bool is_simulated() const {
        return simulated_;
    }

    /** Empieza a escuchar en `frequency_hz`. No bloquea; el hilo graba. */
    void start_receive(uint32_t frequency_hz);
    /** Empieza a reemitir `signal` `repeats` veces. No bloquea. */
    void start_transmit(const subghz::Signal& signal, int repeats);
    /** Para lo que esté haciendo (lo que se interrumpe es la espera). Al grabar, descarta. */
    void stop();
    /** Al grabar: deja de escuchar y se queda con lo capturado hasta ahora (ENTER en la pantalla). */
    void finish_receive();

    bool busy() const;
    Status status() const;
    /** La señal capturada (válida cuando el estado es Received). */
    subghz::Signal captured() const;

private:
    void join_worker();
    void run_receive(uint32_t frequency_hz);
    void run_transmit(subghz::Signal signal, int repeats);
#if !USE_DESKTOP
    void run_receive_device(uint32_t frequency_hz);
    void run_transmit_device(const subghz::Signal& signal, int repeats);
#endif
    void set_state(State state, const std::string& error = "");

    const bool simulated_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> keep_{false};
public:
    static constexpr int kRepeatsNeeded = 3; // grabar para al oír el mismo código estas veces
private: // parar la escucha quedándose con lo grabado

    mutable std::mutex mutex_;
    Status status_;
    subghz::Signal captured_;
};

} // namespace platform

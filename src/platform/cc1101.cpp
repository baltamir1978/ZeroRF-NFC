/*
 * SPDX-License-Identifier: MIT
 */

#include "cc1101.h"

#include "subghz_decode.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>

namespace platform {
namespace {

bool wants_simulation() {
#if USE_DESKTOP
    return true; // en el escritorio no hay radio: siempre simulado
#else
    const char* mode = std::getenv("ZERORFNFC_CC1101");
    return mode != nullptr && std::strcmp(mode, "sim") == 0;
#endif
}

// Espera `total_us` microsegundos a cachos, mirando si han pedido parar. Devuelve false si se
// interrumpió.
bool sleep_interruptible(std::int64_t total_us, std::atomic<bool>& stop) {
    constexpr std::int64_t kStepUs = 20000; // 20 ms
    std::int64_t slept = 0;
    while (slept < total_us) {
        if (stop) {
            return false;
        }
        const std::int64_t step = std::min<std::int64_t>(kStepUs, total_us - slept);
        std::this_thread::sleep_for(std::chrono::microseconds(step));
        slept += step;
    }
    return !stop;
}

} // namespace

Cc1101::Cc1101() : simulated_(wants_simulation()) {
    status_.simulated = simulated_;
}

Cc1101::~Cc1101() {
    stop();
    join_worker();
}

void Cc1101::join_worker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Cc1101::set_state(State state, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = state;
    status_.error = error;
}

void Cc1101::start_receive(uint32_t frequency_hz) {
    stop();
    join_worker();
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = State::Receiving;
        status_.activity = 0;
        status_.error.clear();
        captured_ = subghz::Signal{};
    }
    worker_ = std::thread(&Cc1101::run_receive, this, frequency_hz);
}

void Cc1101::start_transmit(const subghz::Signal& signal, int repeats) {
    stop();
    join_worker();
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = State::Transmitting;
        status_.activity = 0;
        status_.error.clear();
    }
    worker_ = std::thread(&Cc1101::run_transmit, this, signal, repeats);
}

void Cc1101::stop() {
    stop_ = true;
}

bool Cc1101::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.state == State::Receiving || status_.state == State::Transmitting;
}

Cc1101::Status Cc1101::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

subghz::Signal Cc1101::captured() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return captured_;
}

void Cc1101::run_receive(uint32_t frequency_hz) {
    if (!simulated_) {
        // TODO(aparato): programar el CC1101 por SPI (banda, OOK, Rx), leer los flancos de
        // GDO0 (G15) con libgpiod y montar la señal. Cableado en cardputer-caps-hardware.
        set_state(State::Failed, "CC1101 driver not implemented yet");
        return;
    }

    // Backend simulado: hace como que escucha un par de segundos y «captura» un código
    // Princeton al azar, que sí se decodifica (para poder probar guardar y reemitir).
    for (int i = 0; i < 100; ++i) {
        if (stop_) {
            set_state(State::Stopped);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.activity = static_cast<std::size_t>(i);
        }
        // A mitad de la escucha «llega» la señal.
        if (i == 60) {
            std::random_device rd;
            std::mt19937 gen(rd());
            const uint64_t key = (static_cast<uint64_t>(gen()) & 0xFFFFFFULL);
            subghz::Signal signal = subghz::encode_princeton(key, 24, 350, frequency_hz, 5);
            std::lock_guard<std::mutex> lock(mutex_);
            captured_ = std::move(signal);
            status_.state = State::Received;
            return;
        }
    }
    set_state(State::Stopped);
}

void Cc1101::run_transmit(subghz::Signal signal, int repeats) {
    if (!simulated_) {
        set_state(State::Failed, "CC1101 driver not implemented yet");
        return;
    }
    if (repeats < 1) {
        repeats = 1;
    }

    // Duración real de la emisión: la suma de los tiempos por el número de repeticiones.
    std::int64_t frame_us = 0;
    for (int32_t t : signal.timings) {
        frame_us += std::abs(static_cast<std::int64_t>(t));
    }
    const std::int64_t total_us = frame_us * repeats;

    if (sleep_interruptible(total_us, stop_)) {
        set_state(State::Done);
    } else {
        set_state(State::Stopped);
    }
}

} // namespace platform

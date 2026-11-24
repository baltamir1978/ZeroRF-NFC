/*
 * SPDX-License-Identifier: MIT
 */

#include "cc1101.h"

#include "subghz_decode.h"
#include "subghz_file.h"
#include "subghz_storage.h"

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <vector>

#if !USE_DESKTOP
#include "cc1101/cc1101.h"
#include "st25/cap_power.h"

#include <ctime>

#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

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

#if !USE_DESKTOP
// --- Aparato: captura y reemisión reales con el CC1101 (ver Tools/zeronfc, ya validado) ---

constexpr int kRfSw0Line = 14;  // RF_SW0: camino de RF (1 = banda 433, confirmado en RX)
constexpr int kGdo0Line = 15;   // GDO0: datos demodulados (RX) / dato de TX (TX)
constexpr int kPowerEnLine = 26;

int gpio_out_hold(int chip_fd, int line, int value) {
    gpio_v2_line_request req;
    std::memset(&req, 0, sizeof(req));
    req.offsets[0] = line;
    req.num_lines = 1;
    std::strncpy(req.consumer, "zerorfnfc", sizeof(req.consumer) - 1);
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = value ? 1 : 0;
    req.config.attrs[0].mask = 1;
    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0 || req.fd < 0) return -1;
    return req.fd;
}

void gpio_set(int line_fd, int value) {
    gpio_v2_line_values v;
    v.bits = value ? 1 : 0;
    v.mask = 1;
    ioctl(line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

void add_us(timespec& t, long us) {
    t.tv_nsec += us * 1000L;
    while (t.tv_nsec >= 1000000000L) { t.tv_nsec -= 1000000000L; ++t.tv_sec; }
}

void busy_until(const timespec& deadline) {
    timespec now;
    do {
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while (now.tv_sec < deadline.tv_sec ||
             (now.tv_sec == deadline.tv_sec && now.tv_nsec < deadline.tv_nsec));
}

#endif // !USE_DESKTOP

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
    keep_ = false;
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

void Cc1101::finish_receive() {
    keep_ = true;
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

#if !USE_DESKTOP
void Cc1101::run_receive_device(uint32_t frequency_hz) {
    CapPower power;
    if (!power.power_on_rails()) {
        set_state(State::Failed, power.error());
        return;
    }
    int chip = ::open("/dev/gpiochip0", O_RDWR | O_CLOEXEC);
    if (chip < 0) { set_state(State::Failed, "No /dev/gpiochip0"); return; }
    int sw = gpio_out_hold(chip, kRfSw0Line, 1);  // RF_SW0 = banda 433

    cc::Cc1101 radio;
    if (!radio.open() || !radio.present()) {
        if (sw >= 0) ::close(sw);
        ::close(chip);
        set_state(State::Failed, "CC1101 not responding (is the cap on?)");
        return;
    }
    uint32_t f = radio.setFrequency(frequency_hz);
    radio.configureOokRx(f);

    // Pedir GDO0 (GPIO15) como entrada con ambos flancos.
    gpio_v2_line_request req;
    std::memset(&req, 0, sizeof(req));
    req.offsets[0] = kGdo0Line;
    req.num_lines = 1;
    std::strncpy(req.consumer, "zerorfnfc-gdo0", sizeof(req.consumer) - 1);
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING;
    if (ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &req) < 0 || req.fd < 0) {
        radio.close();
        if (sw >= 0) ::close(sw);
        ::close(chip);
        set_state(State::Failed, "Cannot read GDO0 (GPIO15)");
        return;
    }

    // Escuchar hasta que el mismo código llegue 3 veces (el mando repite la trama mientras se
    // pulsa: con eso basta y no hace falta esperar), hasta que paren o, como mucho, 20 s.
    std::vector<int32_t> durs;
    pollfd pfd = {req.fd, POLLIN, 0};
    auto t_end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    auto next_check = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    std::size_t checked_size = 0;
    uint64_t prev_ns = 0;
    int prev_rising = -1;
    while (!stop_ && std::chrono::steady_clock::now() < t_end && durs.size() < 20000) {
        int r = poll(&pfd, 1, 100);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++status_.activity;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_check && durs.size() != checked_size) {
            next_check = now + std::chrono::milliseconds(400);
            checked_size = durs.size();
            if (subghz::extract_best_frame(durs).votes >= 3) break;
        }
        if (r <= 0) continue;
        gpio_v2_line_event ev;
        if (read(req.fd, &ev, sizeof(ev)) != static_cast<ssize_t>(sizeof(ev))) continue;
        bool rising = (ev.id == GPIO_V2_LINE_EVENT_RISING_EDGE);
        if (prev_ns != 0 && prev_rising >= 0) {
            int32_t us = static_cast<int32_t>((ev.timestamp_ns - prev_ns) / 1000);
            durs.push_back(prev_rising ? us : -us);
        }
        prev_ns = ev.timestamp_ns;
        prev_rising = rising ? 1 : 0;
    }
    ::close(req.fd);
    radio.strobeIdle();
    radio.close();
    if (sw >= 0) ::close(sw);
    ::close(chip);

    if (stop_ && !keep_) { set_state(State::Stopped); return; }

    // La captura entera de la última grabación, aparte (no sale en la lista): para estudiar un
    // mando que no se reconoce sin tener que volver a grabarlo.
    {
        subghz::Signal raw;
        raw.frequency_hz = f;
        raw.timings = durs;
        const std::string dir = std::filesystem::path(subghz_dir()).parent_path() / "subghz-raw";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::string err;
        if (!raw.timings.empty()) subghz::save_sub_file(dir + "/last-capture.sub", raw, err);
    }

    const subghz::Extracted best = subghz::extract_best_frame(durs);
    if (best.frame.size() < 24) {
        set_state(State::Stopped);  // no llegó nada reconocible
        return;
    }
    subghz::Signal sig;
    sig.frequency_hz = f;
    sig.modulation = subghz::Modulation::Ook;
    sig.timings = best.frame;
    std::lock_guard<std::mutex> lock(mutex_);
    captured_ = std::move(sig);
    status_.state = State::Received;
}

void Cc1101::run_transmit_device(const subghz::Signal& signal, int repeats) {
    if (signal.timings.empty()) { set_state(State::Failed, "Empty signal"); return; }
    if (repeats < 1) repeats = 1;

    CapPower power;
    if (!power.power_on_rails()) { set_state(State::Failed, power.error()); return; }
    int chip = ::open("/dev/gpiochip0", O_RDWR | O_CLOEXEC);
    if (chip < 0) { set_state(State::Failed, "No /dev/gpiochip0"); return; }
    int sw = gpio_out_hold(chip, kRfSw0Line, 1);
    int data = gpio_out_hold(chip, kGdo0Line, 0);  // GDO0 = dato de TX, empieza sin portadora
    if (data < 0) { if (sw>=0) ::close(sw); ::close(chip); set_state(State::Failed, "Cannot drive GDO0 (GPIO15)"); return; }

    cc::Cc1101 radio;
    if (!radio.open() || !radio.present()) {
        ::close(data); if (sw>=0) ::close(sw); ::close(chip);
        set_state(State::Failed, "CC1101 not responding (is the cap on?)");
        return;
    }
    radio.configureOokTx(radio.setFrequency(signal.frequency_hz ? signal.frequency_hz : 433920000));

    sched_param sp;
    sp.sched_priority = 80;
    sched_setscheduler(0, SCHED_FIFO, &sp);  // tiempos sin saltos (si se puede)

    radio.strobeTx();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    for (int r = 0; r < repeats && !stop_; ++r) {
        for (int32_t d : signal.timings) {
            long us = d < 0 ? -d : d;
            if (us < 30) us = 30;
            gpio_set(data, d > 0 ? 1 : 0);
            add_us(t, us);
            busy_until(t);
        }
        gpio_set(data, 0);
        add_us(t, 15000);  // separación entre repeticiones
        busy_until(t);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++status_.activity;
        }
    }
    gpio_set(data, 0);

    sched_param sp0;
    sp0.sched_priority = 0;
    sched_setscheduler(0, SCHED_OTHER, &sp0);
    radio.strobeIdle();
    radio.close();
    ::close(data);
    if (sw >= 0) ::close(sw);
    ::close(chip);
    set_state(stop_ ? State::Stopped : State::Done);
}
#endif // !USE_DESKTOP

void Cc1101::run_receive(uint32_t frequency_hz) {
    if (!simulated_) {
#if !USE_DESKTOP
        run_receive_device(frequency_hz);
#else
        set_state(State::Failed, "no device backend");
#endif
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
#if !USE_DESKTOP
        run_transmit_device(signal, repeats);
#else
        set_state(State::Failed, "no device backend");
#endif
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

/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_st25r3916.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>

namespace platform {
namespace {

bool wants_simulation() {
#if USE_DESKTOP
    return true;
#else
    const char* mode = std::getenv("ZERORFNFC_NFC");
    return mode != nullptr && std::strcmp(mode, "sim") == 0;
#endif
}

// Una NTAG simulada con un NDEF (URI) dentro, para probar lectura, guardado y NDEF.
nfc::Card make_fake_ntag() {
    nfc::Card card;
    card.type = nfc::CardType::MifareUltralight;
    card.tech = nfc::Tech::IsoA;
    card.atqa = 0x0044;
    card.sak = 0x00;
    card.block_size = 4;

    std::random_device rd;
    std::mt19937 gen(rd());
    // UID de 7 bytes que empieza por 0x04 (NXP), como las NTAG reales.
    card.uid = {0x04,
                static_cast<uint8_t>(gen()),
                static_cast<uint8_t>(gen()),
                static_cast<uint8_t>(gen()),
                static_cast<uint8_t>(gen()),
                static_cast<uint8_t>(gen()),
                static_cast<uint8_t>(gen())};

    // 45 páginas de 4 bytes (como una NTAG213).
    card.memory.assign(45 * 4, 0x00);
    // Capability Container en la página 3 (byte 12): NDEF, 144 bytes de usuario.
    card.memory[12] = 0xE1;
    card.memory[13] = 0x10;
    card.memory[14] = 0x12;
    card.memory[15] = 0x00;

    // Mensaje NDEF: un registro URI "https://m5stack.com" (prefijo 0x04 = "https://").
    const std::string tail = "m5stack.com";
    std::vector<uint8_t> message = {0xD1, 0x01, static_cast<uint8_t>(1 + tail.size()), 'U', 0x04};
    for (char c : tail) {
        message.push_back(static_cast<uint8_t>(c));
    }
    // TLV Type 2 a partir de la página 4 (byte 16): 0x03, longitud, mensaje, terminador 0xFE.
    std::size_t p = 16;
    card.memory[p++] = 0x03;
    card.memory[p++] = static_cast<uint8_t>(message.size());
    for (uint8_t b : message) {
        card.memory[p++] = b;
    }
    card.memory[p++] = 0xFE;
    return card;
}

} // namespace

NfcSt25r3916::NfcSt25r3916() : simulated_(wants_simulation()) {
    status_.simulated = simulated_;
}

NfcSt25r3916::~NfcSt25r3916() {
    stop();
    join_worker();
}

void NfcSt25r3916::join_worker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void NfcSt25r3916::set_state(State state, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = state;
    status_.error = error;
}

void NfcSt25r3916::start_scan() {
    stop();
    join_worker();
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = State::Scanning;
        status_.activity = 0;
        status_.error.clear();
        scanned_ = nfc::Card{};
    }
    worker_ = std::thread(&NfcSt25r3916::run_scan, this);
}

void NfcSt25r3916::start_emulate(const nfc::Card& card) {
    stop();
    join_worker();
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = State::Emulating;
        status_.activity = 0;
        status_.error.clear();
    }
    worker_ = std::thread(&NfcSt25r3916::run_emulate, this, card);
}

void NfcSt25r3916::stop() {
    stop_ = true;
}

bool NfcSt25r3916::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.state == State::Scanning || status_.state == State::Emulating;
}

NfcSt25r3916::Status NfcSt25r3916::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

nfc::Card NfcSt25r3916::scanned() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return scanned_;
}

void NfcSt25r3916::run_scan() {
    if (!simulated_) {
        // TODO(aparato): inicializar el ST25R3916 por SPI (modo 1, CS=G22, IRQ=G23), hacer el
        // anticolisión ISO-A, leer UID/ATQA/SAK y volcar las páginas. Ver cardputer-caps-hardware.
        set_state(State::Failed, "NFC driver not implemented yet");
        return;
    }

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
        if (i == 40) { // «acerca» una tarjeta
            nfc::Card card = make_fake_ntag();
            std::lock_guard<std::mutex> lock(mutex_);
            scanned_ = std::move(card);
            status_.state = State::Found;
            return;
        }
    }
    set_state(State::Stopped);
}

void NfcSt25r3916::run_emulate(nfc::Card /*card*/) {
    if (!simulated_) {
        set_state(State::Failed, "NFC driver not implemented yet");
        return;
    }
    // Emular es «estar presente» hasta que se pida parar.
    while (!stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::lock_guard<std::mutex> lock(mutex_);
        ++status_.activity;
    }
    set_state(State::Stopped);
}

} // namespace platform

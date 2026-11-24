/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_st25r3916.h"

#include "nfc_type2.h"
#include "st25/cap_power.h"
#include "st25/st25r3916.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

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
    // GET_VERSION de una NTAG213, para que el simulador enseñe el producto exacto.
    card.version = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03};

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

// --- Aparato: el driver real (st25/, escrito y probado en cpzero desde plex el 23-nov-2026) ---

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Un intento de selección NFC-A. Ciclar el campo en cada intento resetea la tarjeta: sin eso,
// una tarjeta que se quedó a medias no vuelve a contestar (aprendido en el aparato).
bool select_once(st25::St25r3916& chip, st25::PiccA& picc) {
    chip.fieldOff();
    sleep_ms(15);
    chip.fieldOn();
    sleep_ms(6);
    return chip.activateA(picc);
}

uint16_t atqa_of(const st25::PiccA& picc) {
    return static_cast<uint16_t>((picc.atqa[1] << 8) | picc.atqa[0]);
}

// Páginas de una Type 2 según GET_VERSION (byte 6 = tamaño). 0 = no se sabe.
int type2_pages(st25::St25r3916& chip) {
    uint8_t ver[16];
    int len = 0;
    if (!chip.type2GetVersion(ver, len) || len < 8 || ver[1] != 0x04) {
        return 16; // Ultralight clásica: no tiene GET_VERSION y son 16 páginas
    }
    switch (ver[6]) {
    case 0x0B: return 20;  // Ultralight EV1 (48 B)
    case 0x0E: return 41;  // Ultralight EV1 (128 B)
    case 0x0F: return 45;  // NTAG213
    case 0x11: return 135; // NTAG215
    case 0x13: return 231; // NTAG216
    default: return 0;
    }
}

// Vuelca la memoria de una Type 2 (NTAG/Ultralight) en `card`. La tarjeta tiene que estar
// seleccionada; se vuelve a seleccionar tras GET_VERSION porque, si no lo entiende, se calla.
void dump_type2(st25::St25r3916& chip, st25::PiccA& picc, nfc::Card& card) {
    int pages = type2_pages(chip);
    if (!select_once(chip, picc)) {
        return;
    }
    const int limit = pages > 0 ? pages : 256;
    std::vector<uint8_t> memory;
    uint8_t first[16] = {0};
    for (int page = 0; page < limit; page += 4) {
        uint8_t buf[16];
        if (!chip.type2Read(static_cast<uint8_t>(page), buf)) {
            break;
        }
        if (page == 0) {
            std::memcpy(first, buf, 16);
        } else if (pages == 0 && page >= 16 && std::memcmp(buf, first, 16) == 0) {
            break; // tamaño desconocido: la memoria «da la vuelta» al llegar al final
        }
        const int keep = std::min(4, limit - page);
        memory.insert(memory.end(), buf, buf + keep * 4);
    }
    if (!memory.empty()) {
        card.memory = std::move(memory);
        card.block_size = 4;
    }
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

void NfcSt25r3916::start_write(const nfc::Card& card) {
    stop();
    join_worker();
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = State::Writing;
        status_.activity = 0;
        status_.error.clear();
        status_.message.clear();
    }
    worker_ = std::thread(&NfcSt25r3916::run_write, this, card);
}

void NfcSt25r3916::stop() {
    stop_ = true;
}

bool NfcSt25r3916::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.state == State::Scanning || status_.state == State::Emulating || status_.state == State::Writing;
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
        run_scan_device();
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

void NfcSt25r3916::run_emulate(nfc::Card card) {
    if (!simulated_) {
        run_emulate_device(card);
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

void NfcSt25r3916::run_scan_device() {
    // `power` se declara antes que `chip`: se destruye después, así el SPI se cierra con el cap
    // aún encendido.
    CapPower power;
    if (!power.power_on_nfc()) {
        set_state(State::Failed, power.error());
        return;
    }
    st25::St25r3916 chip;
    if (!chip.open(CapPower::kNfcSpiDevice)) {
        set_state(State::Failed, "Cannot open the NFC SPI");
        return;
    }
    const st25::InitStatus init = chip.init();
    if (!init.ok) {
        set_state(State::Failed, "NFC chip init failed: " + init.error);
        return;
    }

    // Sólo NFC-A (lo único validado con tarjetas en el aparato). B/V/F están en el driver pero
    // escritos sin tarjeta de referencia: se usan desde `Tools/zeronfc` para diagnosticar.
    while (!stop_) {
        st25::PiccA picc;
        if (select_once(chip, picc)) {
            nfc::Card card;
            card.tech = nfc::Tech::IsoA;
            card.atqa = atqa_of(picc);
            card.sak = picc.sak;
            card.uid.assign(picc.uid, picc.uid + picc.uid_len);
            card.type = nfc::detect_card_type(card.atqa, card.sak);
            if ((picc.sak & 0x7F) == 0x00) {
                // NTAG / Ultralight: GET_VERSION da el modelo exacto, y volcamos la memoria.
                uint8_t ver[16];
                int vlen = 0;
                if (chip.type2GetVersion(ver, vlen) && vlen >= 8) {
                    card.version.assign(ver, ver + vlen);
                }
                dump_type2(chip, picc, card);
            } else if (picc.sak & 0x20) {
                // ISO14443-4 (DESFire, etc.): pedir el ATS para afinar el tipo.
                uint8_t ats[32];
                int alen = 0;
                if (chip.rats(ats, alen) && alen > 0) {
                    card.ats.assign(ats, ats + alen);
                }
            }
            chip.fieldOff();
            std::lock_guard<std::mutex> lock(mutex_);
            scanned_ = std::move(card);
            status_.state = State::Found;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++status_.activity;
        }
        sleep_ms(50);
    }
    chip.fieldOff();
    set_state(State::Stopped);
}

void NfcSt25r3916::run_emulate_device(const nfc::Card& card) {
    // El chip responde solo al anticolisión/SELECT; el driver atiende READ con el volcado. Sirve
    // para NTAG/Ultralight; Classic/DESFire necesitan cripto y no se emulan.
    if (card.tech != nfc::Tech::IsoA || (card.uid.size() != 4 && card.uid.size() != 7)) {
        set_state(State::Failed, "Only NFC-A cards with a 4/7-byte UID");
        return;
    }
    if (card.block_size != 4 || card.memory.empty()) {
        set_state(State::Failed, "Only NTAG/Ultralight dumps can be emulated");
        return;
    }
    st25::PiccA picc;
    picc.atqa[0] = static_cast<uint8_t>(card.atqa & 0xFF);
    picc.atqa[1] = static_cast<uint8_t>(card.atqa >> 8);
    picc.sak = card.sak;
    picc.uid_len = static_cast<uint8_t>(card.uid.size());
    std::copy(card.uid.begin(), card.uid.end(), picc.uid);

    CapPower power;
    if (!power.power_on_nfc()) {
        set_state(State::Failed, power.error());
        return;
    }
    st25::St25r3916 chip;
    if (!chip.open(CapPower::kNfcSpiDevice)) {
        set_state(State::Failed, "Cannot open the NFC SPI");
        return;
    }
    st25::St25r3916::EmuStats stats;
    std::string error;
    const bool ok = chip.runEmulationA(picc, card.memory.data(), static_cast<int>(card.memory.size()),
                                       0, stats, error, &stop_);
    if (!ok) {
        set_state(State::Failed, error);
        return;
    }
    set_state(State::Stopped);
}

void NfcSt25r3916::run_write(nfc::Card card) {
    if (!simulated_) {
        run_write_device(card);
        return;
    }
    // Simulado: «aparece» una NTAG215 en blanco al cabo de un momento y se escribe.
    for (int i = 0; i < 40; ++i) {
        if (stop_) {
            set_state(State::Stopped);
            return;
        }
        sleep_ms(25);
        std::lock_guard<std::mutex> lock(mutex_);
        status_.activity = static_cast<std::size_t>(i);
    }
    const nfc::Type2Layout target = nfc::type2_layout({0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03}, 0);
    const nfc::Type2WritePlan plan = nfc::plan_type2_write(card, target);
    if (!plan.ok()) {
        set_state(State::Failed, plan.error);
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = State::Written;
    status_.message = "Wrote " + std::to_string(plan.pages.size()) + " pages to " + target.name;
}

void NfcSt25r3916::run_write_device(const nfc::Card& card) {
    if (card.block_size != 4 || card.memory.empty()) {
        set_state(State::Failed, "Only NTAG/Ultralight cards can be written");
        return;
    }
    CapPower power;
    if (!power.power_on_nfc()) {
        set_state(State::Failed, power.error());
        return;
    }
    st25::St25r3916 chip;
    if (!chip.open(CapPower::kNfcSpiDevice)) {
        set_state(State::Failed, "Cannot open the NFC SPI");
        return;
    }
    const st25::InitStatus init = chip.init();
    if (!init.ok) {
        set_state(State::Failed, "NFC chip init failed: " + init.error);
        return;
    }

    // Esperar la etiqueta de destino.
    st25::PiccA picc;
    while (!stop_ && !select_once(chip, picc)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++status_.activity;
        }
        sleep_ms(50);
    }
    if (stop_) {
        chip.fieldOff();
        set_state(State::Stopped);
        return;
    }
    if ((picc.sak & 0x7F) != 0x00) {
        chip.fieldOff();
        set_state(State::Failed, "Not an NTAG/Ultralight tag");
        return;
    }

    // Qué modelo es (GET_VERSION; si no lo entiende se calla y hay que volver a seleccionarla).
    std::vector<uint8_t> version;
    uint8_t ver[16];
    int vlen = 0;
    if (chip.type2GetVersion(ver, vlen) && vlen >= 8) {
        version.assign(ver, ver + vlen);
    }
    if (!select_once(chip, picc)) {
        chip.fieldOff();
        set_state(State::Failed, "Tag moved away");
        return;
    }
    // Sin GET_VERSION: Ultralight original (16 páginas) si al leer la 16 «da la vuelta» a la 0.
    uint8_t head[16];
    if (!chip.type2Read(0, head)) {
        chip.fieldOff();
        set_state(State::Failed, "Can't read the tag");
        return;
    }
    std::size_t pages_hint = 0;
    if (version.empty()) {
        uint8_t wrap[16];
        if (chip.type2Read(16, wrap) && std::memcmp(wrap, head, 16) == 0) pages_hint = 16;
    }
    const nfc::Type2Layout target = nfc::type2_layout(version, pages_hint);
    if (nfc::type2_static_locked({head[8], head[9], head[10], head[11]})) {
        chip.fieldOff();
        set_state(State::Failed, "Tag is locked (read-only pages)");
        return;
    }
    const nfc::Type2WritePlan plan = nfc::plan_type2_write(card, target);
    if (!plan.ok()) {
        chip.fieldOff();
        set_state(State::Failed, plan.error);
        return;
    }

    int written = 0;
    for (const auto& [page, data] : plan.pages) {
        if (stop_) break;
        if (!chip.type2Write(static_cast<uint8_t>(page), data.data())) {
            chip.fieldOff();
            set_state(State::Failed, "Write failed at page " + std::to_string(page) + " (" + std::to_string(written) +
                                         " written)");
            return;
        }
        ++written;
        std::lock_guard<std::mutex> lock(mutex_);
        status_.activity = static_cast<std::size_t>(written);
    }
    if (stop_) {
        chip.fieldOff();
        set_state(State::Failed, "Stopped after " + std::to_string(written) + " pages: tag left half written");
        return;
    }

    // Releer y comparar (READ da 4 páginas de golpe).
    for (std::size_t i = 0; i < plan.pages.size(); i += 4) {
        uint8_t rd[16];
        const int first = plan.pages[i].first;
        if (!chip.type2Read(static_cast<uint8_t>(first), rd)) {
            chip.fieldOff();
            set_state(State::Failed, "Written, but can't read it back");
            return;
        }
        for (std::size_t k = 0; k < 4 && i + k < plan.pages.size(); ++k) {
            if (std::memcmp(rd + k * 4, plan.pages[i + k].second.data(), 4) != 0) {
                chip.fieldOff();
                set_state(State::Failed, "Verify failed at page " + std::to_string(plan.pages[i + k].first));
                return;
            }
        }
    }
    chip.fieldOff();
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = State::Written;
    status_.message = "Wrote " + std::to_string(written) + " pages to " +
                      (target.name.empty() ? std::string("the tag") : target.name);
}

} // namespace platform

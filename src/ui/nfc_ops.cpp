#include "nfc_ops.h"

#include "../st25r3916.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

const char* kPowerLed = "/sys/class/leds/ext_5v_out/brightness";
const char* kSpiDev = "/dev/spidev0.2";
const char* kOverlay = "spi0-spidev2-gpio22-overlay";
const char* kGpioChip = "/dev/gpiochip0";
const int kPowerEnLine = 26;

int g_chip_fd = -1, g_line_fd = -1;

void poweren_on() {
    if (g_line_fd >= 0) return;
    g_chip_fd = ::open(kGpioChip, O_RDWR | O_CLOEXEC);
    if (g_chip_fd < 0) return;
    gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));
    req.offsets[0] = kPowerEnLine;
    req.num_lines = 1;
    strncpy(req.consumer, "zeronfc", sizeof(req.consumer) - 1);
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = 1;
    req.config.attrs[0].mask = 1;
    if (ioctl(g_chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0 || req.fd < 0) {
        ::close(g_chip_fd); g_chip_fd = -1; return;
    }
    g_line_fd = req.fd;
}
void poweren_off() {
    if (g_line_fd >= 0) { ::close(g_line_fd); g_line_fd = -1; }
    if (g_chip_fd >= 0) { ::close(g_chip_fd); g_chip_fd = -1; }
}
void cap_power(bool on) {
    if (on) poweren_on();
    { std::ofstream f(kPowerLed); if (f) f << (on ? "1" : "0"); }
    if (!on) poweren_off();
}
bool ensure_spidev() {
    { std::ifstream f(kSpiDev); if (f.good()) return true; }
    std::string c = std::string("dtoverlay ") + kOverlay;
    if (system(c.c_str()) != 0) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::ifstream f2(kSpiDev);
    return f2.good();
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Un intento de seleccion NFC-A con ciclado de campo (como el CLI).
bool select_once(st25::St25r3916& nfc, st25::PiccA& p) {
    nfc.fieldOff();
    sleep_ms(15);
    nfc.fieldOn();
    sleep_ms(6);
    return nfc.activateA(p);
}

const char* manufacturer(uint8_t b) {
    switch (b) {
        case 0x04: return "NXP"; case 0x02: return "STMicro"; case 0x05: return "Infineon";
        case 0x07: return "TI"; case 0x16: return "EM Micro"; case 0x21: return "EM Micro";
        default: return "?";
    }
}
const char* classify_a(uint8_t sak, uint16_t atqa) {
    uint8_t s = sak & 0x7F;
    if (s & 0x20) return "ISO14443-4 (DESFire/JCOP)";
    switch (s) {
        case 0x00: return "NTAG / Ultralight (Type 2)";
        case 0x08: return (atqa == 0x0004) ? "MIFARE Classic 1K" : "MIFARE Classic 1K / Plus";
        case 0x09: return "MIFARE Mini";
        case 0x18: return "MIFARE Classic 4K";
        case 0x10: return "MIFARE Plus 2K SL2";
        case 0x11: return "MIFARE Plus 4K SL2";
        default: return "NFC-A (SAK no reconocido)";
    }
}

struct Session {
    st25::St25r3916 nfc;
    bool ok = false;
    std::string err;
    Session() {
        cap_power(true);
        sleep_ms(200);
        if (!ensure_spidev()) { err = "No hay /dev/spidev0.2 (overlay)"; return; }
        if (!nfc.open(kSpiDev)) { err = "No pude abrir el SPI"; return; }
        auto s = nfc.init();
        if (!s.ok) { err = "Init del chip fallo: " + s.error; return; }
        ok = true;
    }
    ~Session() { nfc.close(); cap_power(false); }
};

std::string uid_hex(const st25::PiccA& p) {
    char b[32] = {0};
    for (int i = 0; i < p.uid_len; ++i) sprintf(b + i * 2, "%02X", p.uid[i]);
    return b;
}

}  // namespace

namespace nfcops {

std::string read_card() {
    Session s;
    if (!s.ok) return "Error: " + s.err;
    st25::PiccA p;
    bool got = false;
    for (int i = 0; i < 20 && !got; ++i) got = select_once(s.nfc, p);
    if (!got) return "No se detecta ninguna tarjeta NFC-A.\nAcercala al Cap y reintenta.";
    uint16_t atqa = (p.atqa[1] << 8) | p.atqa[0];
    char head[256];
    snprintf(head, sizeof(head),
             "TARJETA NFC-A\nUID: %s (%d B, %s)\nATQA %04X  SAK %02X\n%s\n",
             uid_hex(p).c_str(), p.uid_len, manufacturer(p.uid[0]), atqa, p.sak,
             classify_a(p.sak, atqa));
    std::string out = head;
    // Si es Type 2, volcar unas paginas
    if ((p.sak & 0x7F) == 0x00) {
        out += "\nPaginas:\n";
        for (int page = 0; page < 12; page += 4) {
            uint8_t buf[16];
            if (!s.nfc.type2Read((uint8_t)page, buf)) break;
            for (int q = 0; q < 4; ++q) {
                char ln[48];
                snprintf(ln, sizeof(ln), " %2d: %02X %02X %02X %02X\n",
                         page + q, buf[q*4], buf[q*4+1], buf[q*4+2], buf[q*4+3]);
                out += ln;
            }
        }
    }
    return out;
}

std::string identify() {
    Session s;
    if (!s.ok) return "Error: " + s.err;
    // NFC-A
    st25::PiccA p;
    bool a = false;
    for (int i = 0; i < 15 && !a; ++i) a = select_once(s.nfc, p);
    if (a) {
        uint16_t atqa = (p.atqa[1] << 8) | p.atqa[0];
        char b[256];
        snprintf(b, sizeof(b), "Tecnologia: NFC-A\n%s\nUID %s\nATQA %04X SAK %02X",
                 classify_a(p.sak, atqa), uid_hex(p).c_str(), atqa, p.sak);
        return b;
    }
    // NFC-B
    uint8_t buf[32]; int bl = 0; bool b = false;
    for (int i = 0; i < 8 && !b; ++i) {
        s.nfc.fieldOff(); sleep_ms(15); s.nfc.configureNfcB(); sleep_ms(6);
        b = s.nfc.nfcbDetect(buf, bl);
    }
    if (b) return "Tecnologia: NFC-B (ISO14443-B)\n(DNI/pasaporte, bancaria, accesos)";
    // NFC-V
    int vl = 0; bool v = false;
    for (int i = 0; i < 8 && !v; ++i) {
        s.nfc.fieldOff(); sleep_ms(15); s.nfc.configureNfcV(); sleep_ms(6);
        v = s.nfc.nfcvInventory(buf, vl);
    }
    if (v) return "Tecnologia: NFC-V (ISO15693 / Type 5)";
    // NFC-F
    int fl = 0; bool f = false;
    for (int i = 0; i < 8 && !f; ++i) {
        s.nfc.fieldOff(); sleep_ms(15); s.nfc.configureNfcF(); sleep_ms(6);
        f = s.nfc.nfcfPoll(buf, fl);
    }
    if (f) return "Tecnologia: NFC-F (FeliCa)";
    return "No responde a NFC estandar (A/B/V/F).\nSi acopla a 13,56 es propietaria\n(iCLASS/LEGIC); si no, es de 125 kHz\n(este Cap no lee 125 kHz).";
}

std::string sub_ghz_note() {
    return "Sub-GHz (CC1101): pendiente.\nPara capturar mandos (p.ej. garaje)\na 433,92 MHz. En desarrollo.";
}

}  // namespace nfcops

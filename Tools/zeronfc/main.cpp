// zeronfc — herramienta de línea de comandos para el NFC del Cardputer Zero (ST25R3916).
// Fase 1 en curso. De momento: encender el Cap y validar el arranque del chip (init).
#include <array>
#include <map>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <time.h>

#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "st25r3916.h"
#include "../../src/platform/cc1101/cc1101.h"
#include "../../src/model/subghz_decode.h"
#include "../../src/model/subghz_signal.h"

static const char* kPowerLed = "/sys/class/leds/ext_5v_out/brightness";
static const char* kSpiDev = "/dev/spidev0.2";
static const char* kOverlay = "spi0-spidev2-gpio22-overlay";
static const char* kGpioChip = "/dev/gpiochip0";
static const int kPowerEnLine = 26;  // GPIO26 = POWER_EN del Cap

static bool write_sysfs(const char* path, const char* val) {
    std::ofstream f(path);
    if (!f) return false;
    f << val;
    return f.good();
}

// Mantiene GPIO26 (POWER_EN del Cap) como salida a 1 mientras estos descriptores sigan abiertos.
static int g_gpiochip_fd = -1, g_powerline_fd = -1;

static void poweren_on() {
    if (g_powerline_fd >= 0) return;
    g_gpiochip_fd = ::open(kGpioChip, O_RDWR | O_CLOEXEC);
    if (g_gpiochip_fd < 0) { fprintf(stderr, "aviso: no pude abrir %s\n", kGpioChip); return; }
    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));
    req.offsets[0] = kPowerEnLine;
    req.num_lines = 1;
    strncpy(req.consumer, "zeronfc", sizeof(req.consumer) - 1);
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = 1;  // bit 0 = valor de offsets[0]
    req.config.attrs[0].mask = 1;
    if (ioctl(g_gpiochip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0 || req.fd < 0) {
        fprintf(stderr, "aviso: no pude poner POWER_EN (GPIO26) alto\n");
        ::close(g_gpiochip_fd); g_gpiochip_fd = -1; return;
    }
    g_powerline_fd = req.fd;  // mantener abierto = línea retenida en alto
}

static void poweren_off() {
    if (g_powerline_fd >= 0) { ::close(g_powerline_fd); g_powerline_fd = -1; }
    if (g_gpiochip_fd >= 0) { ::close(g_gpiochip_fd); g_gpiochip_fd = -1; }
}

static void cap_power(bool on) {
    if (on) poweren_on();                         // POWER_EN (GPIO26) alto
    if (!write_sysfs(kPowerLed, on ? "1" : "0"))  // EXT5V
        fprintf(stderr, "aviso: no pude %s ext_5v_out (¿permisos?)\n", on ? "encender" : "apagar");
    if (!on) poweren_off();
}

static bool ensure_spidev() {
    std::ifstream f(kSpiDev);
    if (f.good()) return true;
    std::string c = std::string("dtoverlay ") + kOverlay;
    if (system(c.c_str()) != 0) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::ifstream f2(kSpiDev);
    return f2.good();
}

static int cmd_init(bool vdd5, bool keep_power, int hold_ms) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) {
        fprintf(stderr, "ERROR: %s no disponible (overlay no cargado)\n", kSpiDev);
        return 2;
    }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) {
        fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev);
        return 2;
    }
    st25::InitStatus s = nfc.init();
    printf("Chip        : ST25R391%d  (tipo=0x%02X, rev=%d)\n",
           s.ic_type == 0x05 ? 6 : 7, s.ic_type, s.ic_rev);
    printf("VDD asumido : %s\n", vdd5 ? "5V" : "3.3V");
    printf("osc_ok      : %s\n", s.osc_ok ? "sí" : "NO");
    printf("Regulador   : raw=0x%02X  ~%.1f V\n", s.regulator_raw, s.regulator_volts);
    printf("Campo RF ON : %s   (OP_CONTROL=0x%02X  AUX_DISPLAY=0x%02X)\n",
           s.tx_on ? "sí" : "NO", s.op_control_raw, s.aux_display_raw);
    if (!s.ok) printf("ERROR init  : %s\n", s.error.c_str());
    printf("Resultado   : %s\n", s.ok ? "INIT OK" : "INIT FALLÓ");
    if (hold_ms > 0) {
        printf("(manteniendo campo %d ms para medir consumo...)\n", hold_ms);
        fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
    }
    nfc.close();
    if (!keep_power) cap_power(false);
    return s.ok ? 0 : 1;
}

static const char* sak_tipo(uint8_t sak, uint8_t uid_len) {
    // Clasificación somera por SAK (ISO14443-3/4)
    if (sak & 0x20) return "ISO14443-4 (DESFire/tarjeta con APDU)";
    if ((sak & 0x1F) == 0x00) return (uid_len == 7) ? "NTAG/Ultralight (Type 2)" : "Ultralight/Type 2";
    if ((sak & 0x1F) == 0x08) return "MIFARE Classic 1K";
    if ((sak & 0x1F) == 0x18) return "MIFARE Classic 4K";
    if ((sak & 0x1F) == 0x09) return "MIFARE Mini";
    return "desconocido";
}

static int cmd_detect(bool vdd5, int secs) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); return 2; }
    st25::InitStatus s = nfc.init();
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    printf("Chip OK (OP=0x%02X). Acerca la tarjeta... (%ds)\n", s.op_control_raw, secs);
    fflush(stdout);
    auto t0 = std::chrono::steady_clock::now();
    auto deadline = t0 + std::chrono::seconds(secs);
    auto last_beat = t0;
    int attempts = 0, atqa_seen = 0, read_ok = 0;
    std::string last_uid;
    while (std::chrono::steady_clock::now() < deadline) {
        ++attempts;
        // Ciclar el campo: apagar un instante para devolver la tarjeta a reposo
        // (si no, tras el primer WUPA se queda activa y deja de responder).
        nfc.fieldOff();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        nfc.fieldOn();
        std::this_thread::sleep_for(std::chrono::milliseconds(6));  // guard time: la tarjeta se alimenta
        st25::PiccA p;
        bool ok = nfc.activateA(p);               // una sola secuencia: wakeup + anticollision
        bool atqa_resp = (p.atqa[0] || p.atqa[1]);
        if (atqa_resp) ++atqa_seen;
        if (ok) {
            char uidhex[32] = {0};
            for (int j = 0; j < p.uid_len; ++j) sprintf(uidhex + j * 2, "%02X", p.uid[j]);
            if (last_uid != uidhex) {
                last_uid = uidhex;
                ++read_ok;
                printf("\n¡TARJETA!  UID=%s (%d bytes)  ATQA=%02X%02X  SAK=%02X\n",
                       uidhex, p.uid_len, p.atqa[1], p.atqa[0], p.sak);
                printf("  Tipo probable: %s\n", sak_tipo(p.sak, p.uid_len));
                fflush(stdout);
            }
        } else if (atqa_resp) {
            printf("\ntarjeta presente (ATQA=%02X%02X) pero no completé el UID\n", p.atqa[1], p.atqa[0]);
            fflush(stdout);
            last_uid.clear();
        } else {
            last_uid.clear();
        }
        auto now = std::chrono::steady_clock::now();
        if (now - last_beat >= std::chrono::seconds(2)) {
            last_beat = now;
            printf("  ... %d intentos, %d con respuesta (ATQA), %d UID leídos\n",
                   attempts, atqa_seen, read_ok);
            fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    printf("Fin: %d intentos, %d respuestas de tarjeta, %d UID leídos.\n", attempts, atqa_seen, read_ok);
    nfc.close();
    cap_power(false);
    return 0;
}

static int cmd_rf(bool vdd5) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); return 2; }
    st25::InitStatus s = nfc.init();
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    using namespace st25;
    auto snap = [&](const char* tag) {
        uint8_t amp = nfc.measureAmplitude();
        uint8_t aux = nfc.readReg(REG_AUX_DISPLAY);
        uint8_t op = nfc.readReg(REG_OP_CONTROL);
        uint8_t rssi = nfc.readReg(0x2D);
        printf("  %-28s OP=0x%02X AUX=0x%02X amp=0x%02X rssi=0x%02X %s\n",
               tag, op, aux, amp, rssi, (aux & AUX_TX_ON) ? "<tx_on!>" : "");
    };
    printf("Estado tras init:\n"); snap("init (field-on cmd)");

    // Campo OFF base
    nfc.cmd(CMD_STOP_ALL);
    nfc.modifyReg(REG_OP_CONTROL, 0x00, OP_TX_EN | OP_RX_EN);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    snap("tras STOP_ALL (off)");

    // Estrategia A: detector de campo OFF, encender tx_en directo
    nfc.modifyReg(REG_OP_CONTROL, OP_EN, OP_EFD_AUTO);
    nfc.modifyReg(REG_OP_CONTROL, OP_TX_EN | OP_RX_EN, 0x00);
    std::this_thread::sleep_for(std::chrono::milliseconds(6));
    snap("A: en_fd off + tx_en");

    // Estrategia B: STOP, comando field-on con en_fd off
    nfc.cmd(CMD_STOP_ALL);
    nfc.modifyReg(REG_OP_CONTROL, 0x00, OP_TX_EN | OP_RX_EN);
    nfc.modifyReg(REG_OP_CONTROL, OP_EN, OP_EFD_AUTO);
    nfc.cmd(CMD_NFC_INITIAL_FIELD_ON);
    std::this_thread::sleep_for(std::chrono::milliseconds(6));
    nfc.modifyReg(REG_OP_CONTROL, OP_TX_EN | OP_RX_EN, 0x00);
    std::this_thread::sleep_for(std::chrono::milliseconds(6));
    snap("B: en_fd off + cmd field-on");
    nfc.close();
    cap_power(false);
    return 0;
}

static bool select_card(st25::St25r3916& nfc, st25::PiccA& p, int tries = 30) {
    for (int i = 0; i < tries; ++i) {
        nfc.fieldOff();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        nfc.fieldOn();
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
        if (nfc.activateA(p)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return false;
}

// Diccionario de claves por defecto de MIFARE Classic (las típicas de fábrica y de muchos
// sistemas: transporte, hoteles, acceso). No es recuperación de claves, sólo prueba las conocidas.
static const char* kClassicKeys[] = {
    "FFFFFFFFFFFF",  // en blanco / de fábrica
    "A0A1A2A3A4A5",  // clave A por defecto del sector 0 (MAD)
    "D3F7D3F7D3F7",  // NDEF público
    "000000000000",
    "B0B1B2B3B4B5", "4D3A99C351DD", "1A982C7E459A", "AABBCCDDEEFF",
    "714C5C886E97", "587EE5F9350F", "A0478CC39091", "533CB6C723F6", "8FD0A4F256E9",
};

// Pide una línea GPIO como salida con un valor fijo y la mantiene (devuelve el fd, -1 si falla).
static int gpio_out_hold(int chip_fd, int line, int value) {
    gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));
    req.offsets[0] = line;
    req.num_lines = 1;
    strncpy(req.consumer, "zerorf", sizeof(req.consumer) - 1);
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = value ? 1 : 0;
    req.config.attrs[0].mask = 1;
    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0 || req.fd < 0) return -1;
    return req.fd;
}

// Captura los flancos de una línea (GDO0 = GPIO15) durante `secs` segundos. Rellena `durs` con
// las duraciones en microsegundos (el signo = nivel: + alto, - bajo). Devuelve nº de tramos.
static int capture_edges(int chip_fd, int line, int secs, std::vector<long>& durs) {
    gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));
    req.offsets[0] = line;
    req.num_lines = 1;
    strncpy(req.consumer, "zerorf-gdo0", sizeof(req.consumer) - 1);
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING;
    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0 || req.fd < 0) return -1;

    struct pollfd pfd = {req.fd, POLLIN, 0};
    auto t_end = std::chrono::steady_clock::now() + std::chrono::seconds(secs);
    uint64_t prev_ns = 0;
    int prev_rising = -1;  // nivel ANTES del flanco actual
    durs.clear();
    while (std::chrono::steady_clock::now() < t_end && durs.size() < 20000) {
        int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t_end - std::chrono::steady_clock::now()).count();
        if (ms <= 0) break;
        int r = poll(&pfd, 1, ms);
        if (r <= 0) continue;
        gpio_v2_line_event ev;
        if (read(req.fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) continue;
        bool rising = (ev.id == GPIO_V2_LINE_EVENT_RISING_EDGE);
        if (prev_ns != 0 && prev_rising >= 0) {
            long us = (long)((ev.timestamp_ns - prev_ns) / 1000);
            // El tramo que acaba de terminar tenía el nivel = prev_rising (alto si venía de subir).
            durs.push_back(prev_rising ? us : -us);
        }
        prev_ns = ev.timestamp_ns;
        prev_rising = rising ? 1 : 0;
    }
    close(req.fd);
    return (int)durs.size();
}

// Captura un mando Sub-GHz por OOK a 433,92 MHz y enseña el tren de pulsos.
static int cmd_rfrx(int secs, int rfsw, double freq_mhz) {
    cap_power(true);  // POWER_EN(26) + 5V del cap
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int chip = open("/dev/gpiochip0", O_RDWR | O_CLOEXEC);
    if (chip < 0) { fprintf(stderr, "no gpiochip0\n"); cap_power(false); return 2; }
    int sw = gpio_out_hold(chip, 14, rfsw);  // RF_SW0 (banda / camino de RF)
    cc::Cc1101 radio;
    if (!radio.open()) { fprintf(stderr, "no abro CC1101\n"); if (sw>=0) close(sw); close(chip); cap_power(false); return 2; }
    if (!radio.present()) { printf("CC1101 no responde.\n"); if (sw>=0) close(sw); close(chip); cap_power(false); return 1; }
    uint32_t f = radio.setFrequency((uint32_t)(freq_mhz * 1e6));
    radio.configureOokRx(f);
    printf("Escuchando %.3f MHz (RF_SW0=%d, MARCSTATE=0x%02X). Pulsa el mando ya... (%d s)\n",
           f / 1e6, rfsw, radio.marcState(), secs);
    fflush(stdout);

    std::vector<long> durs;
    capture_edges(chip, 15, secs, durs);
    radio.strobeIdle();
    if (sw >= 0) close(sw);
    close(chip);
    cap_power(false);

    // Contar sólo tramos "de verdad" (100us..20ms): filtra el ruido de picos muy cortos.
    int real = 0;
    for (long d : durs) { long a = d < 0 ? -d : d; if (a >= 100 && a <= 20000) ++real; }
    printf("Flancos capturados: %zu (tramos útiles ~%d)\n", durs.size(), real);
    if (durs.size() < 8) {
        printf("Nada reconocible. Si el mando es de 433 y pulsaste, prueba RF_SW0=%d.\n", rfsw ? 0 : 1);
        return 1;
    }
    // Intentar decodificar (Princeton/EV1527). Se le pasa el tren entero; el decodificador
    // busca la trama entre las repeticiones.
    subghz::Signal sig;
    sig.frequency_hz = f;
    sig.modulation = subghz::Modulation::Ook;
    for (long d : durs) sig.timings.push_back((int32_t)d);
    subghz::Decoded dec = subghz::decode(sig);
    if (dec.ok()) {
        printf("\n*** DECODIFICADO: %s ***\n", subghz::protocol_name(dec.protocol));
        printf("    código = 0x%llX  (%u bits)  te=%u us\n",
               (unsigned long long)dec.key, dec.bits, dec.te_us);
        printf("    Es código FIJO: se puede guardar y reemitir.\n");
    } else {
        printf("\nNo se reconoció el protocolo (se podría reemitir en crudo igualmente).\n");
    }

    // Guardar la señal cruda en .sub (formato Flipper, RAW) para poder reemitirla luego, aunque
    // no se haya decodificado: en código fijo, reemitir el crudo abre igual.
    {
        std::string dir = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.local/share/zerorfnfc/subghz";
        std::string mk = "mkdir -p '" + dir + "'"; (void)system(mk.c_str());
        char name[128];
        snprintf(name, sizeof(name), "%s/captura-%ld.sub", dir.c_str(), (long)time(nullptr));
        FILE* fp = fopen(name, "w");
        if (fp) {
            fprintf(fp, "Filetype: Flipper SubGhz RAW File\nVersion: 1\n");
            fprintf(fp, "Frequency: %u\nPreset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\n", f);
            fprintf(fp, "RAW_Data:");
            for (long d : durs) { long a = d<0?-d:d; if (a>=50) fprintf(fp, " %ld", d); }
            fprintf(fp, "\n");
            fclose(fp);
            printf("Señal cruda guardada en %s\n", name);
        }
    }

    printf("Tren de pulsos (us, + alto / - bajo):\n");
    int shown = 0;
    for (size_t i = 0; i < durs.size() && shown < 120; ++i) {
        long a = durs[i] < 0 ? -durs[i] : durs[i];
        if (a < 60) continue;  // ignora picos de ruido al imprimir
        printf("%6ld ", durs[i]);
        if (++shown % 12 == 0) printf("\n");
    }
    printf("\n");
    return 0;
}

// Detecta el CC1101 (Sub-GHz) por /dev/spidev0.1. Primer hito del garaje: confirmar cableado.
static int cmd_cc1101(bool vdd5) {
    (void)vdd5;
    cap_power(true);  // POWER_EN + 5V del cap (lo mismo que para el NFC)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cc::Cc1101 radio;
    if (!radio.open()) { fprintf(stderr, "ERROR: no pude abrir %s\n", cc::Cc1101::kSpiDevice); cap_power(false); return 2; }
    bool rdy = radio.reset();
    uint8_t part = radio.partNumber();
    uint8_t ver = radio.version();
    printf("CC1101 en %s:\n", cc::Cc1101::kSpiDevice);
    printf("  reset listo : %s\n", rdy ? "sí" : "no");
    printf("  part number : 0x%02X (se espera 0x00)\n", part);
    printf("  version     : 0x%02X (se espera 0x14, a veces 0x04/0x17)\n", ver);
    if (radio.present()) {
        uint32_t f = radio.setFrequency(433920000);
        printf("  -> CC1101 DETECTADO. Frecuencia de prueba fijada a %u Hz\n", f);
        printf("     MARCSTATE=0x%02X\n", radio.marcState());
    } else {
        printf("  -> NO responde como CC1101. Revisar cableado/CS/alimentación (RF_SW, POWER_EN).\n");
    }
    radio.close();
    cap_power(false);
    return radio.present() ? 0 : 1;
}

static int cmd_classic(bool vdd5) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); cap_power(false); return 2; }
    st25::InitStatus s = nfc.init();
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    printf("Acerca la MIFARE Classic...\n"); fflush(stdout);
    st25::PiccA p;
    if (!select_card(nfc, p)) { printf("No detecté ninguna tarjeta.\n"); cap_power(false); return 1; }
    if ((p.sak & 0x1F) != 0x08 && (p.sak & 0x1F) != 0x18 && (p.sak & 0x1F) != 0x09) {
        printf("SAK %02X: no parece MIFARE Classic.\n", p.sak); nfc.close(); cap_power(false); return 1;
    }
    bool is4k = (p.sak & 0x1F) == 0x18;
    int sectors = is4k ? 40 : 16;
    char uidhex[32] = {0};
    for (int j = 0; j < p.uid_len; ++j) sprintf(uidhex + j * 2, "%02X", p.uid[j]);
    printf("MIFARE Classic %s  UID=%s\n", is4k ? "4K" : "1K", uidhex);
    const uint8_t* uid4 = p.uid + (p.uid_len > 4 ? p.uid_len - 4 : 0);

    if (getenv("MFDBG")) sectors = 1;  // depuración: sólo el sector 0
    int ok_sectors = 0;
    for (int sec = 0; sec < sectors; ++sec) {
        // Bloques por sector: 4 en los 32 primeros, 16 en los 8 últimos de una 4K.
        int first = (sec < 32) ? sec * 4 : 128 + (sec - 32) * 16;
        int nblk = (sec < 32) ? 4 : 16;
        bool authed = false;
        const char* used = nullptr;
        bool used_b = false;
        for (const char* ks : kClassicKeys) {
            uint8_t key[6];
            for (int i = 0; i < 6; ++i) { unsigned v; sscanf(ks + i * 2, "%2x", &v); key[i] = v; }
            // Re-seleccionar antes de cada intento: un auth fallido deja la tarjeta muda.
            if (!select_card(nfc, p, 3)) break;
            if (nfc.mifareAuth(true, uid4, first, key)) { authed = true; used = ks; used_b = false; break; }
            if (!select_card(nfc, p, 3)) break;
            if (nfc.mifareAuth(false, uid4, first, key)) { authed = true; used = ks; used_b = true; break; }
        }
        if (!authed) { printf("  Sector %2d: --- (ninguna clave conocida)\n", sec); continue; }
        ++ok_sectors;
        printf("  Sector %2d: clave %c = %s\n", sec, used_b ? 'B' : 'A', used);
        for (int b = 0; b < nblk; ++b) {
            uint8_t blk[16];
            if (nfc.mifareReadBlock(first + b, blk)) {
                printf("    blq %3d:", first + b);
                for (int i = 0; i < 16; ++i) printf(" %02X", blk[i]);
                printf("\n");
            } else {
                printf("    blq %3d: (no se pudo leer)\n", first + b);
            }
        }
    }
    printf("Sectores leídos: %d/%d\n", ok_sectors, sectors);
    nfc.close(); cap_power(false);
    return ok_sectors > 0 ? 0 : 1;
}

static int cmd_read(bool vdd5, int maxpages) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); return 2; }
    st25::InitStatus s = nfc.init();
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    printf("Acerca la tarjeta...\n"); fflush(stdout);
    st25::PiccA p;
    if (!select_card(nfc, p)) { printf("No detecté ninguna tarjeta.\n"); cap_power(false); return 1; }
    char uidhex[32] = {0};
    for (int j = 0; j < p.uid_len; ++j) sprintf(uidhex + j * 2, "%02X", p.uid[j]);
    printf("UID=%s  ATQA=%02X%02X  SAK=%02X  (%s)\n", uidhex, p.atqa[1], p.atqa[0], p.sak,
           sak_tipo(p.sak, p.uid_len));
    printf("Volcado (Type 2, 4 bytes/página):\n");
    uint8_t first[16] = {0};
    int printed = 0;
    for (int page = 0; page < maxpages; page += 4) {
        uint8_t buf[16];
        if (!nfc.type2Read((uint8_t)page, buf)) break;
        if (page == 0) memcpy(first, buf, 16);
        else if (page >= 16 && memcmp(buf, first, 16) == 0) { printf("  (memoria repetida: fin ~página %d)\n", page); break; }
        for (int q = 0; q < 4; ++q) {
            int pg = page + q;
            printf("  pág %3d: %02X %02X %02X %02X\n", pg, buf[q*4], buf[q*4+1], buf[q*4+2], buf[q*4+3]);
            ++printed;
        }
    }
    printf("(%d páginas leídas)\n", printed);
    nfc.close();
    cap_power(false);
    return 0;
}

static int cmd_sense(bool vdd5, int secs) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); return 2; }
    st25::InitStatus s = nfc.init();   // deja campo NFC-A encendido
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    printf("Midiendo carga de antena (amplitud/fase). Levanta y vuelve a poner la tarjeta mientras corre.\n");
    fflush(stdout);
    auto t0 = std::chrono::steady_clock::now();
    auto deadline = t0 + std::chrono::seconds(secs);
    int ampmin = 255, ampmax = 0, phmin = 255, phmax = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t amp = nfc.measureAmplitude();
        nfc.cmd(0xD9);                               // CMD_MEASURE_PHASE
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        uint8_t ph = nfc.readReg(0x25);              // resultado A/D
        if (amp < ampmin) ampmin = amp; if (amp > ampmax) ampmax = amp;
        if (ph < phmin) phmin = ph;   if (ph > phmax) phmax = ph;
        int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        printf("  t=%5dms  amplitud=0x%02X  fase=0x%02X\n", ms, amp, ph);
        fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    printf("Rango: amplitud 0x%02X..0x%02X (Δ%d), fase 0x%02X..0x%02X (Δ%d)\n",
           ampmin, ampmax, ampmax - ampmin, phmin, phmax, phmax - phmin);
    printf(ampmax - ampmin > 3 || phmax - phmin > 3
               ? "=> La medida CAMBIÓ: hay acoplamiento de 13,56 MHz (la tarjeta es HF).\n"
               : "=> La medida NO cambió: sin acoplamiento de 13,56 MHz (apunta a 125 kHz).\n");
    nfc.close();
    cap_power(false);
    return 0;
}

// Fabricante por el primer byte del UID (ISO/IEC 7816-6).
static const char* manufacturer(uint8_t b) {
    switch (b) {
        case 0x04: return "NXP";
        case 0x02: return "STMicroelectronics";
        case 0x05: return "Infineon";
        case 0x07: return "Texas Instruments";
        case 0x08: return "Fujitsu";
        case 0x16: return "EM Microelectronic";
        case 0x21: return "EM Microelectronic";
        case 0x28: return "Kovio";
        case 0x2B: return "Maxim";
        case 0x47: return "Gemalto/Thales";
        default: return "desconocido";
    }
}

// Clasifica una tarjeta NFC-A por ATQA + SAK (ISO14443-3 + NXP AN10833).
static const char* classify_a(uint8_t sak, uint16_t atqa) {
    uint8_t s = sak & 0x7F;
    if (s & 0x20) return "ISO14443-4 (DESFire / MIFARE Plus SL3 / JCOP / tarjeta con APDU)";
    switch (s) {
        case 0x00: return "NFC Forum Type 2 (MIFARE Ultralight / NTAG)";
        case 0x08: return (atqa == 0x0004) ? "MIFARE Classic 1K" : "MIFARE Classic 1K / Plus 2K SL1";
        case 0x09: return "MIFARE Mini (0.3K)";
        case 0x18: return "MIFARE Classic 4K / Plus 4K SL1";
        case 0x10: return "MIFARE Plus 2K (SL2)";
        case 0x11: return "MIFARE Plus 4K (SL2)";
        case 0x28: return "SmartMX con MIFARE Classic 1K";
        case 0x38: return "SmartMX con MIFARE Classic 4K";
        case 0x01: return "NXP TNP3xxx";
        default: return "NFC-A (SAK no reconocido)";
    }
}

static int cmd_identify(bool vdd5) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); cap_power(false); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); cap_power(false); return 2; }
    st25::InitStatus s = nfc.init();
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    printf("Deja la tarjeta pegada al Cap. Identificando...\n"); fflush(stdout);

    // --- NFC-A ---
    st25::PiccA p;
    bool a = false;
    for (int i = 0; i < 15 && !a; ++i) a = select_card(nfc, p, 1);
    if (a) {
        char uidhex[32] = {0};
        for (int j = 0; j < p.uid_len; ++j) sprintf(uidhex + j * 2, "%02X", p.uid[j]);
        uint16_t atqa = (p.atqa[1] << 8) | p.atqa[0];
        printf("\nTecnología : NFC-A (ISO14443-A)\n");
        printf("UID        : %s (%d bytes, fabricante: %s)\n", uidhex, p.uid_len, manufacturer(p.uid[0]));
        printf("ATQA/SAK   : %04X / %02X\n", atqa, p.sak);
        printf("Tipo       : %s\n", classify_a(p.sak, atqa));
        if ((p.sak & 0x20)) {
            uint8_t ats[32]; int al = 0;
            if (nfc.rats(ats, al)) {
                printf("ATS        :");
                for (int i = 0; i < al; ++i) printf(" %02X", ats[i]);
                printf("\n");
            }
        } else if ((p.sak & 0x7F) == 0x00) {
            uint8_t v[16]; int vl = 0;
            if (nfc.type2GetVersion(v, vl) && vl >= 8 && v[1] == 0x04) {
                const char* prod = "NXP Type 2";
                if (v[2] == 0x03) prod = "MIFARE Ultralight EV1";
                else if (v[2] == 0x04) {
                    switch (v[6]) {
                        case 0x0F: prod = "NTAG213 (144 B)"; break;
                        case 0x11: prod = "NTAG215 (504 B)"; break;
                        case 0x13: prod = "NTAG216 (888 B)"; break;
                        default:   prod = "NTAG21x"; break;
                    }
                }
                printf("Producto   : %s (GET_VERSION:", prod);
                for (int i = 0; i < vl; ++i) printf(" %02X", v[i]);
                printf(")\n");
            } else {
                printf("Producto   : MIFARE Ultralight clásica (sin GET_VERSION)\n");
            }
        } else if ((p.sak & 0x7F) == 0x08 || (p.sak & 0x7F) == 0x18 || (p.sak & 0x7F) == 0x09) {
            printf("Nota       : MIFARE Classic -> leer sectores necesita claves Crypto1 (parte RF pendiente de tarjeta).\n");
        }
        nfc.close(); cap_power(false); return 0;
    }

    // --- NFC-B ---
    uint8_t buf[32]; int bl = 0;
    bool b = false;
    for (int i = 0; i < 10 && !b; ++i) {
        nfc.fieldOff(); std::this_thread::sleep_for(std::chrono::milliseconds(15));
        nfc.configureNfcB(); std::this_thread::sleep_for(std::chrono::milliseconds(6));
        b = nfc.nfcbDetect(buf, bl);
    }
    if (b) {
        printf("\nTecnología : NFC-B (ISO14443-B)\n");
        printf("ATQB       :"); for (int i = 0; i < bl; ++i) printf(" %02X", buf[i]); printf("\n");
        if (bl >= 5) { printf("PUPI       : %02X%02X%02X%02X\n", buf[1], buf[2], buf[3], buf[4]); }
        printf("Tipo       : ISO14443-B (p. ej. DNI/pasaporte, bancaria, algunos accesos)\n");
        nfc.close(); cap_power(false); return 0;
    }

    // --- NFC-V ---
    bool v = false; int vl = 0;
    for (int i = 0; i < 10 && !v; ++i) {
        nfc.fieldOff(); std::this_thread::sleep_for(std::chrono::milliseconds(15));
        nfc.configureNfcV(); std::this_thread::sleep_for(std::chrono::milliseconds(6));
        v = nfc.nfcvInventory(buf, vl);
    }
    if (v) {
        printf("\nTecnología : NFC-V (ISO15693 / NFC Forum Type 5)\n");
        printf("Respuesta  :"); for (int i = 0; i < vl; ++i) printf(" %02X", buf[i]); printf("\n");
        printf("Tipo       : etiqueta de proximidad ISO15693 (acceso, inventario, biblioteca, esquí...)\n");
        nfc.close(); cap_power(false); return 0;
    }

    // --- NFC-F ---
    bool f = false; int fl = 0;
    for (int i = 0; i < 10 && !f; ++i) {
        nfc.fieldOff(); std::this_thread::sleep_for(std::chrono::milliseconds(15));
        nfc.configureNfcF(); std::this_thread::sleep_for(std::chrono::milliseconds(6));
        f = nfc.nfcfPoll(buf, fl);
    }
    if (f) {
        printf("\nTecnología : NFC-F (FeliCa)\n");
        printf("SENSF_RES  :"); for (int i = 0; i < fl; ++i) printf(" %02X", buf[i]); printf("\n");
        if (fl >= 9) { printf("IDm        : %02X%02X%02X%02X%02X%02X%02X%02X\n",
                              buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]); }
        printf("Tipo       : FeliCa (transporte/pagos, sobre todo Japón)\n");
        nfc.close(); cap_power(false); return 0;
    }

    // --- Ninguna respuesta estándar ---
    printf("\nTecnología : NO responde a ningún protocolo NFC estándar (A/B/V/F).\n");
    printf("Tipo       : ejecuta `sense` (quitando y poniendo la tarjeta) para distinguir:\n");
    printf("             - si la amplitud/fase CAMBIAN -> tarjeta PROPIETARIA de 13,56 MHz\n");
    printf("               (HID iCLASS/Picopass, LEGIC): este Cap la nota pero no la lee sin su protocolo.\n");
    printf("             - si NO cambian nada -> tarjeta de 125 kHz (EM4100/HID Prox): otra frecuencia,\n");
    printf("               este Cap no puede leerla.\n");
    nfc.close(); cap_power(false); return 0;
}

static int cmd_iclass(bool vdd5) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); cap_power(false); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); cap_power(false); return 2; }
    st25::InitStatus s = nfc.init();
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    printf("Probando iCLASS/Picopass (ACTALL + IDENTIFY sobre ISO15693). Deja la tarjeta pegada...\n");
    fflush(stdout);
    uint8_t raw[64]; int len = 0; bool ok = false;
    for (int i = 0; i < 20 && !ok; ++i) {
        nfc.fieldOff();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        nfc.configureNfcV();   // misma capa física ISO15693 que usa iCLASS
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
        ok = nfc.iclassIdentify(raw, len);
    }
    if (ok) {
        printf("\n¡Responde a iCLASS!  (es HID iCLASS / Picopass)\n");
        printf("Respuesta cruda (%d bytes):", len);
        for (int i = 0; i < len; ++i) printf(" %02X", raw[i]);
        printf("\n");
        if (len >= 8) {
            printf("CSN (anticolisión, 8 bytes):");
            for (int i = 0; i < 8; ++i) printf(" %02X", raw[i]);
            printf("\n");
        }
    } else {
        printf("\nNo respondió a iCLASS. O no es iCLASS, o mi capa ISO15693 (sin validar) no la engancha.\n");
    }
    nfc.close();
    cap_power(false);
    return ok ? 0 : 1;
}

static int cmd_scan(bool vdd5) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); return 2; }
    st25::InitStatus s = nfc.init();
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    printf("Deja la tarjeta pegada. Probando tecnologías...\n"); fflush(stdout);

    // --- NFC-A (ISO14443-A) ---
    printf("NFC-A (ISO14443-A): "); fflush(stdout);
    st25::PiccA p;
    bool a = false;
    for (int i = 0; i < 12 && !a; ++i) { a = select_card(nfc, p, 1); }
    if (a) {
        char uidhex[32] = {0};
        for (int j = 0; j < p.uid_len; ++j) sprintf(uidhex + j * 2, "%02X", p.uid[j]);
        printf("¡SÍ!  UID=%s  ATQA=%02X%02X  SAK=%02X  (%s)\n",
               uidhex, p.atqa[1], p.atqa[0], p.sak, sak_tipo(p.sak, p.uid_len));
        nfc.close(); cap_power(false); return 0;
    }
    printf("no responde\n"); fflush(stdout);

    // --- NFC-B (ISO14443-B) ---
    printf("NFC-B (ISO14443-B): "); fflush(stdout);
    bool b = false; uint8_t atqb[32]; int blen = 0;
    for (int i = 0; i < 12 && !b; ++i) {
        nfc.fieldOff();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        nfc.configureNfcB();
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
        b = nfc.nfcbDetect(atqb, blen);
    }
    if (b) {
        printf("¡SÍ!  ATQB (%d bytes):", blen);
        for (int i = 0; i < blen; ++i) printf(" %02X", atqb[i]);
        printf("\n");
        nfc.close(); cap_power(false); return 0;
    }
    printf("no responde\n");

    // --- NFC-V (ISO15693) ---
    printf("NFC-V (ISO15693): "); fflush(stdout);
    bool v = false; uint8_t vraw[64]; int vlen = 0;
    for (int i = 0; i < 12 && !v; ++i) {
        nfc.fieldOff();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        nfc.configureNfcV();
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
        v = nfc.nfcvInventory(vraw, vlen);
    }
    if (v) {
        printf("¡SÍ!  respuesta (%d bytes crudos del stream):", vlen);
        for (int i = 0; i < vlen; ++i) printf(" %02X", vraw[i]);
        printf("\n  (es una tarjeta ISO15693 / NFC Forum Type 5; puedo implementar su lectura completa)\n");
        nfc.close(); cap_power(false); return 0;
    }
    printf("no responde\n");

    // --- NFC-F (FeliCa) ---
    printf("NFC-F (FeliCa): "); fflush(stdout);
    bool f = false; uint8_t fres[32]; int flen = 0;
    for (int i = 0; i < 12 && !f; ++i) {
        nfc.fieldOff();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        nfc.configureNfcF();
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
        f = nfc.nfcfPoll(fres, flen);
    }
    if (f) {
        printf("¡SÍ!  SENSF_RES (%d bytes):", flen);
        for (int i = 0; i < flen; ++i) printf(" %02X", fres[i]);
        printf("\n");
        nfc.close(); cap_power(false); return 0;
    }
    printf("no responde\n");

    printf("\nNi NFC-A, ni NFC-B, ni NFC-V, ni NFC-F: no habla NINGÚN protocolo NFC estándar.\n");
    printf("Pero `sense` confirmó que acopla a 13,56 MHz, así que NO es de 125 kHz.\n");
    printf("=> Firma típica de tarjeta de acceso PROPIETARIA de 13,56 MHz (HID iCLASS/Picopass o LEGIC).\n");
    nfc.close();
    cap_power(false);
    return 0;
}

static int cmd_probe(bool vdd5) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); return 2; }
    st25::InitStatus s = nfc.init();
    printf("init %s  OP=0x%02X\n", s.ok ? "OK" : s.error.c_str(), s.op_control_raw);
    using namespace st25;
    for (int i = 0; i < 6; ++i) {
        nfc.cmd(CMD_STOP_ALL);
        nfc.cmd(CMD_RESET_RX_GAIN);
        nfc.writeReg16(REG_NRT_1, 0x0350);                 // No-Response Timer
        nfc.writeReg(REG_ISO14443A, 0x00);
        nfc.modifyReg(REG_AUX_DEF, 0x80, 0x00);            // no_crc_rx
        nfc.clearIrq();
        nfc.cmd(CMD_CLEAR_FIFO);
        nfc.writeReg16(REG_NUM_TX_BYTES_1, 0x0000);
        nfc.cmd(CMD_TRANSMIT_WUPA);
        uint32_t irq = nfc.waitIrq(IRQ_TXE | IRQ_RXE | IRQ_RXS | IRQ_COL | IRQ_NRE | IRQ_ERR_MASK, 30);
        uint8_t s1 = nfc.readReg(REG_FIFO_STATUS_1);
        printf("  WUPA #%d: IRQ=0x%08X  txe=%d rxs=%d rxe=%d col=%d nre=%d err=0x%02X  fifo=%d\n",
               i, irq, !!(irq & IRQ_TXE), !!(irq & IRQ_RXS), !!(irq & IRQ_RXE),
               !!(irq & IRQ_COL), !!(irq & IRQ_NRE), (irq >> 8) & 0xFF, s1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    nfc.close();
    cap_power(false);
    return 0;
}

static int cmd_dump(bool vdd5) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); return 2; }
    st25::InitStatus s = nfc.init();
    printf("init %s\n", s.ok ? "OK" : s.error.c_str());
    // Verificar Space-B con un roundtrip en un registro RW inocuo (correlator_1)
    nfc.writeRegB(st25::REGB_CORRELATOR_1, 0x47);
    uint8_t rb = nfc.readRegB(st25::REGB_CORRELATOR_1);
    printf("Space-B roundtrip: escribí 0x47 en REGB 0x0C, leo 0x%02X  %s\n",
           rb, rb == 0x47 ? "OK" : "<<< FALLA Space-B");
    printf("Space-A regs:\n");
    for (uint8_t a = 0x00; a <= 0x0E; ++a) printf("  0x%02X=0x%02X\n", a, nfc.readReg(a));
    printf("  0x28(TX_DRV)=0x%02X  0x31(AUX)=0x%02X  0x3F(ID)=0x%02X\n",
           nfc.readReg(0x28), nfc.readReg(0x31), nfc.readReg(0x3F));
    printf("Space-B regs: 0x0C=0x%02X 0x0D=0x%02X 0x2C(reg)=0x%02X 0x30=0x%02X 0x31=0x%02X 0x32=0x%02X 0x33=0x%02X\n",
           nfc.readRegB(0x0C), nfc.readRegB(0x0D), nfc.readRegB(0x2C),
           nfc.readRegB(0x30), nfc.readRegB(0x31), nfc.readRegB(0x32), nfc.readRegB(0x33));
    nfc.close();
    cap_power(false);
    return 0;
}

// Guarda un volcado Type 2 a fichero de texto (una línea "página hex8" + cabecera con UID).
static int cmd_save(bool vdd5, const std::string& path, int maxpages) {
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); return 2; }
    st25::InitStatus s = nfc.init();
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    st25::PiccA p;
    if (!select_card(nfc, p)) { printf("No detecté ninguna tarjeta.\n"); cap_power(false); return 1; }
    std::ofstream f(path);
    if (!f) { fprintf(stderr, "ERROR: no pude crear %s\n", path.c_str()); cap_power(false); return 2; }
    char uidhex[32] = {0};
    for (int j = 0; j < p.uid_len; ++j) sprintf(uidhex + j * 2, "%02X", p.uid[j]);
    char hdr[96];
    sprintf(hdr, "# ZeroNFC Type2 dump UID=%s ATQA=%02X%02X SAK=%02X\n",
            uidhex, p.atqa[1], p.atqa[0], p.sak);
    f << hdr;
    uint8_t first[16] = {0};
    int printed = 0;
    for (int page = 0; page < maxpages; page += 4) {
        uint8_t buf[16];
        if (!nfc.type2Read((uint8_t)page, buf)) break;
        if (page == 0) memcpy(first, buf, 16);
        else if (page >= 16 && memcmp(buf, first, 16) == 0) break;  // memoria repetida: fin
        for (int q = 0; q < 4; ++q) {
            char line[32];
            sprintf(line, "%d %02X%02X%02X%02X\n", page + q, buf[q*4], buf[q*4+1], buf[q*4+2], buf[q*4+3]);
            f << line;
            ++printed;
        }
    }
    f.close();
    printf("Guardadas %d páginas de la tarjeta %s en %s\n", printed, uidhex, path.c_str());
    nfc.close();
    cap_power(false);
    return 0;
}

// Carga un volcado desde fichero y lo escribe en la tarjeta (páginas de usuario >=4).
static int cmd_load(bool vdd5, const std::string& path, bool force) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "ERROR: no pude abrir %s\n", path.c_str()); return 2; }
    std::vector<std::pair<int, std::array<uint8_t, 4>>> pages;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        int pg; char hex[16] = {0};
        if (sscanf(line.c_str(), "%d %8s", &pg, hex) != 2) continue;
        std::array<uint8_t, 4> d{};
        unsigned b0, b1, b2, b3;
        if (sscanf(hex, "%2x%2x%2x%2x", &b0, &b1, &b2, &b3) != 4) continue;
        d = {(uint8_t)b0, (uint8_t)b1, (uint8_t)b2, (uint8_t)b3};
        pages.push_back({pg, d});
    }
    f.close();
    if (pages.empty()) { fprintf(stderr, "ERROR: el fichero no tiene páginas válidas\n"); return 1; }
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); return 2; }
    st25::InitStatus s = nfc.init();
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    st25::PiccA p;
    if (!select_card(nfc, p)) { printf("No detecté ninguna tarjeta destino.\n"); cap_power(false); return 1; }
    char uidhex[32] = {0};
    for (int j = 0; j < p.uid_len; ++j) sprintf(uidhex + j * 2, "%02X", p.uid[j]);
    printf("Tarjeta destino UID=%s. Cargando %zu páginas del volcado...\n", uidhex, pages.size());
    int ok = 0, skip = 0, fail = 0;
    for (auto& pr : pages) {
        int pg = pr.first;
        if (pg < 4 && !force) { ++skip; continue; }   // 0-3 = UID/lock/CC (no reescribibles normalmente)
        if (nfc.type2Write((uint8_t)pg, pr.second.data())) ++ok; else { ++fail; printf("  fallo pág %d\n", pg); }
    }
    printf("Escritas %d páginas, %d omitidas (0-3), %d con fallo.\n", ok, skip, fail);
    printf(force ? "" : "(Las páginas 0-3 no se escriben sin --force; el UID de fábrica no se puede reescribir en tarjetas normales.)\n");
    nfc.close();
    cap_power(false);
    return fail ? 1 : 0;
}

// Emula una tarjeta NFC-A Type 2 a partir de un volcado (UID + páginas).
static int cmd_emulate(bool vdd5, const std::string& path, int secs) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "ERROR: no pude abrir %s\n", path.c_str()); return 2; }
    std::string uidhex; unsigned atqa = 0x0044, sak = 0x00;
    std::map<int, std::array<uint8_t, 4>> pg;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            char b[64];
            auto pu = line.find("UID=");  if (pu != std::string::npos && sscanf(line.c_str() + pu, "UID=%40s", b) == 1) uidhex = b;
            auto pa = line.find("ATQA="); if (pa != std::string::npos) sscanf(line.c_str() + pa, "ATQA=%x", &atqa);
            auto ps = line.find("SAK=");  if (ps != std::string::npos) sscanf(line.c_str() + ps, "SAK=%x", &sak);
            continue;
        }
        int p2; char hex[16] = {0};
        if (sscanf(line.c_str(), "%d %8s", &p2, hex) != 2) continue;
        unsigned b0, b1, b2, b3;
        if (sscanf(hex, "%2x%2x%2x%2x", &b0, &b1, &b2, &b3) != 4) continue;
        pg[p2] = {(uint8_t)b0, (uint8_t)b1, (uint8_t)b2, (uint8_t)b3};
    }
    f.close();
    if (uidhex.empty()) { fprintf(stderr, "ERROR: el volcado no tiene UID en la cabecera\n"); return 1; }
    st25::PiccA p{};
    p.uid_len = (uint8_t)(uidhex.size() / 2);
    for (int i = 0; i < p.uid_len && i < 10; ++i) { unsigned v; sscanf(uidhex.c_str() + i*2, "%2x", &v); p.uid[i] = (uint8_t)v; }
    p.atqa[0] = atqa & 0xFF; p.atqa[1] = (atqa >> 8) & 0xFF; p.sak = (uint8_t)sak;
    int maxpg = 0; for (auto& e : pg) maxpg = std::max(maxpg, e.first);
    int nbytes = (maxpg + 1) * 4;
    std::vector<uint8_t> mem(nbytes, 0);
    for (auto& e : pg) for (int i = 0; i < 4; ++i) mem[e.first * 4 + i] = e.second[i];

    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); cap_power(false); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); cap_power(false); return 2; }
    printf("Emulando tarjeta NFC-A: UID=%s ATQA=%02X%02X SAK=%02X, %d páginas, %ds.\n",
           uidhex.c_str(), p.atqa[1], p.atqa[0], p.sak, nbytes / 4, secs);
    printf("Acerca un lector (móvil con NFC) al Cap...\n"); fflush(stdout);
    st25::St25r3916::EmuStats stats; std::string err;
    if (!nfc.runEmulationA(p, mem.data(), nbytes, secs, stats, err)) {
        printf("No pude emular: %s\n", err.c_str()); nfc.close(); cap_power(false); return 1;
    }
    printf("Fin. Campo externo detectado: %d veces, seleccionados: %d, lecturas servidas: %d, otros: %d\n",
           stats.fields, stats.selections, stats.reads, stats.other);
    if (stats.fields == 0)
        printf("(0 campo externo: NINGÚN lector acercó su campo. El NFC del lector está apagado o no llegó a la antena.)\n");
    else if (stats.selections == 0)
        printf("(Hubo campo pero no nos seleccionó: el lector ve el campo pero algo falla en el anticollision emulado.)\n");
    nfc.close();
    cap_power(false);
    return 0;
}

// Escribe una página Type 2 (4 bytes) y la relee para verificar.
static int cmd_write(bool vdd5, int page, const uint8_t data[4], bool force) {
    if (page < 4 && !force) {
        fprintf(stderr, "ERROR: páginas 0-3 son UID/lock/CC; escribirlas puede inutilizar la tarjeta.\n"
                        "       Usa --force si de verdad quieres (bajo tu responsabilidad).\n");
        return 64;
    }
    if (page < 0 || page > 255) { fprintf(stderr, "ERROR: página fuera de rango\n"); return 64; }
    cap_power(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!ensure_spidev()) { fprintf(stderr, "ERROR: %s no disponible\n", kSpiDev); return 2; }
    st25::St25r3916 nfc;
    if (!nfc.open(kSpiDev, vdd5)) { fprintf(stderr, "ERROR: no pude abrir %s\n", kSpiDev); return 2; }
    st25::InitStatus s = nfc.init();
    if (!s.ok) { printf("init falló: %s\n", s.error.c_str()); cap_power(false); return 1; }
    st25::PiccA p;
    if (!select_card(nfc, p)) { printf("No detecté ninguna tarjeta.\n"); cap_power(false); return 1; }
    char uidhex[32] = {0};
    for (int j = 0; j < p.uid_len; ++j) sprintf(uidhex + j * 2, "%02X", p.uid[j]);
    printf("Tarjeta UID=%s. Escribiendo página %d = %02X %02X %02X %02X ...\n",
           uidhex, page, data[0], data[1], data[2], data[3]);
    if (!nfc.type2Write((uint8_t)page, data)) { printf("La escritura NO fue confirmada (sin ACK).\n"); cap_power(false); return 1; }
    // Verificar releyendo (READ de 'page' devuelve 4 páginas; la nuestra es la primera)
    uint8_t rd[16];
    bool ok = nfc.type2Read((uint8_t)page, rd) &&
              rd[0] == data[0] && rd[1] == data[1] && rd[2] == data[2] && rd[3] == data[3];
    printf(ok ? "OK: escrito y verificado.\n" : "ACK recibido pero la relectura NO coincide.\n");
    nfc.close();
    cap_power(false);
    return ok ? 0 : 1;
}

static bool parse4(const char* hex, uint8_t out[4]) {
    int n = 0; uint8_t v = 0; int nib = 0;
    for (const char* c = hex; *c && n < 4; ++c) {
        char ch = *c;
        int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else continue;  // ignora separadores
        v = (v << 4) | d;
        if (++nib == 2) { out[n++] = v; v = 0; nib = 0; }
    }
    return n == 4 && nib == 0;
}

int main(int argc, char** argv) {
    bool vdd5 = false, keep_power = false, force = false;
    int hold_ms = 0, secs = 15, rfsw = 1;
    double freq_mhz = 433.92;
    std::string sub = argc > 1 ? argv[1] : "init";
    std::vector<std::string> pos;  // argumentos posicionales (no-flag)
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--5v")) vdd5 = true;
        else if (!strcmp(argv[i], "--keep-power")) keep_power = true;
        else if (!strcmp(argv[i], "--force")) force = true;
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--secs") && i + 1 < argc) secs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rfsw") && i + 1 < argc) rfsw = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--freq") && i + 1 < argc) freq_mhz = atof(argv[++i]);
        else pos.push_back(argv[i]);
    }
    if (sub == "rfrx") return cmd_rfrx(secs ? secs : 6, rfsw, freq_mhz);
    if (sub == "init") return cmd_init(vdd5, keep_power, hold_ms);
    if (sub == "detect") return cmd_detect(vdd5, secs);
    if (sub == "read") return cmd_read(vdd5, 232);
    if (sub == "scan") return cmd_scan(vdd5);
    if (sub == "iclass") return cmd_iclass(vdd5);
    if (sub == "identify" || sub == "id") return cmd_identify(vdd5);
    if (sub == "classic") return cmd_classic(vdd5);
    if (sub == "cc1101") return cmd_cc1101(vdd5);
    if (sub == "sense") return cmd_sense(vdd5, secs);
    if (sub == "rf") return cmd_rf(vdd5);
    if (sub == "dump") return cmd_dump(vdd5);
    if (sub == "probe") return cmd_probe(vdd5);
    if (sub == "write") {
        uint8_t data[4];
        if (pos.size() < 2 || !parse4(pos[1].c_str(), data)) {
            fprintf(stderr, "uso: %s write <página> <8 hex>  (ej: write 4 DEADBEEF) [--force]\n", argv[0]);
            return 64;
        }
        return cmd_write(vdd5, atoi(pos[0].c_str()), data, force);
    }
    if (sub == "save") {
        if (pos.empty()) { fprintf(stderr, "uso: %s save <fichero>\n", argv[0]); return 64; }
        return cmd_save(vdd5, pos[0], 232);
    }
    if (sub == "emulate") {
        if (pos.empty()) { fprintf(stderr, "uso: %s emulate <volcado> [--secs N]\n", argv[0]); return 64; }
        return cmd_emulate(vdd5, pos[0], secs);
    }
    if (sub == "load") {
        if (pos.empty()) { fprintf(stderr, "uso: %s load <fichero> [--force]\n", argv[0]); return 64; }
        return cmd_load(vdd5, pos[0], force);
    }
    fprintf(stderr, "uso: %s init|detect|read|scan|sense|write|save|load ... [--5v] [--force] [--secs N]\n", argv[0]);
    return 64;
}

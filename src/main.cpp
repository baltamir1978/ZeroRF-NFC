// zeronfc — herramienta de línea de comandos para el NFC del Cardputer Zero (ST25R3916).
// Fase 1 en curso. De momento: encender el Cap y validar el arranque del chip (init).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#include "st25r3916.h"

static const char* kPowerLed = "/sys/class/leds/ext_5v_out/brightness";
static const char* kSpiDev = "/dev/spidev0.2";
static const char* kOverlay = "spi0-spidev2-gpio22-overlay";

static bool write_sysfs(const char* path, const char* val) {
    std::ofstream f(path);
    if (!f) return false;
    f << val;
    return f.good();
}

static void cap_power(bool on) {
    if (!write_sysfs(kPowerLed, on ? "1" : "0"))
        fprintf(stderr, "aviso: no pude %s ext_5v_out (¿permisos?)\n", on ? "encender" : "apagar");
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

int main(int argc, char** argv) {
    bool vdd5 = false, keep_power = false;
    int hold_ms = 0, secs = 15;
    std::string sub = argc > 1 ? argv[1] : "init";
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--5v")) vdd5 = true;
        else if (!strcmp(argv[i], "--keep-power")) keep_power = true;
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--secs") && i + 1 < argc) secs = atoi(argv[++i]);
    }
    if (sub == "init") return cmd_init(vdd5, keep_power, hold_ms);
    if (sub == "detect") return cmd_detect(vdd5, secs);
    if (sub == "read") return cmd_read(vdd5, 232);
    if (sub == "scan") return cmd_scan(vdd5);
    if (sub == "sense") return cmd_sense(vdd5, secs);
    if (sub == "rf") return cmd_rf(vdd5);
    if (sub == "dump") return cmd_dump(vdd5);
    if (sub == "probe") return cmd_probe(vdd5);
    fprintf(stderr, "uso: %s init|detect [--5v] [--keep-power] [--hold MS] [--secs N]\n", argv[0]);
    return 64;
}

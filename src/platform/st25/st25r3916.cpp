#include "st25r3916.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace st25 {

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

bool St25r3916::open(const std::string& dev, bool vdd5, uint32_t speed_hz) {
    vdd5_ = vdd5;
    speed_ = speed_hz;
    fd_ = ::open(dev.c_str(), O_RDWR);
    if (fd_ < 0) return false;
    uint8_t mode = SPI_MODE_1;  // ST25R3916: CPOL=0, CPHA=1
    uint8_t bits = 8;
    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0) return false;
    if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) return false;
    if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_) < 0) return false;
    return true;
}

void St25r3916::close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }
St25r3916::~St25r3916() { close(); }

void St25r3916::xfer(uint8_t* buf, size_t n) {
    spi_ioc_transfer tr{};
    tr.tx_buf = reinterpret_cast<uintptr_t>(buf);
    tr.rx_buf = reinterpret_cast<uintptr_t>(buf);
    tr.len = n;
    tr.speed_hz = speed_;
    tr.bits_per_word = 8;
    ioctl(fd_, SPI_IOC_MESSAGE(1), &tr);
}

uint8_t St25r3916::readReg(uint8_t reg) {
    uint8_t b[2] = {static_cast<uint8_t>(OP_READ_REG | (reg & 0x3F)), 0x00};
    xfer(b, 2);
    return b[1];
}
void St25r3916::writeReg(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {static_cast<uint8_t>(OP_WRITE_REG | (reg & 0x3F)), val};
    xfer(b, 2);
}
void St25r3916::writeReg16(uint8_t reg, uint16_t val) {
    uint8_t b[3] = {static_cast<uint8_t>(OP_WRITE_REG | (reg & 0x3F)),
                    static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val & 0xFF)};
    xfer(b, 3);
}
void St25r3916::writeReg32(uint8_t reg, uint32_t val) {
    uint8_t b[5] = {static_cast<uint8_t>(OP_WRITE_REG | (reg & 0x3F)),
                    static_cast<uint8_t>(val >> 24), static_cast<uint8_t>(val >> 16),
                    static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val & 0xFF)};
    xfer(b, 5);
}
uint8_t St25r3916::readRegB(uint8_t reg) {
    uint8_t b[3] = {CMD_SPACEB_ACCESS, static_cast<uint8_t>(OP_READ_REG | (reg & 0x3F)), 0x00};
    xfer(b, 3);
    return b[2];
}
void St25r3916::writeRegB(uint8_t reg, uint8_t val) {
    uint8_t b[3] = {CMD_SPACEB_ACCESS, static_cast<uint8_t>(OP_WRITE_REG | (reg & 0x3F)), val};
    xfer(b, 3);
}
void St25r3916::modifyReg(uint8_t reg, uint8_t set_bits, uint8_t clr_bits) {
    uint8_t v = readReg(reg);
    v = (v & ~clr_bits) | set_bits;
    writeReg(reg, v);
}
void St25r3916::cmd(uint8_t c) { uint8_t b = c; xfer(&b, 1); }
void St25r3916::cmdData(uint8_t c, const uint8_t* data, size_t n) {
    std::vector<uint8_t> b;
    b.reserve(1 + n);
    b.push_back(c);
    b.insert(b.end(), data, data + n);
    xfer(b.data(), b.size());
}
void St25r3916::fifoLoad(const uint8_t* data, size_t n) {
    std::vector<uint8_t> b;
    b.reserve(1 + n);
    b.push_back(OP_LOAD_FIFO);
    b.insert(b.end(), data, data + n);
    xfer(b.data(), b.size());
}
std::vector<uint8_t> St25r3916::fifoRead(size_t n) {
    std::vector<uint8_t> b(1 + n, 0);
    b[0] = OP_READ_FIFO;
    xfer(b.data(), b.size());
    return std::vector<uint8_t>(b.begin() + 1, b.end());
}
uint8_t St25r3916::fifoCount() {
    // FIFO_STATUS_2 bits[6:5] = fifo_b[9:8]; FIFO_STATUS_1 = fifo_b[7:0]
    uint8_t s1 = readReg(REG_FIFO_STATUS_1);
    uint8_t s2 = readReg(REG_FIFO_STATUS_2);
    return static_cast<uint8_t>(s1 + (((s2 >> 5) & 0x03) ? 0 : 0));  // normalmente <256; s1 basta
}
uint32_t St25r3916::readIrq() {
    uint8_t b[5] = {static_cast<uint8_t>(OP_READ_REG | (REG_MAIN_IRQ & 0x3F)), 0, 0, 0, 0};
    xfer(b, 5);  // 0x1A..0x1D consecutivos (leerlos los limpia)
    return (uint32_t(b[1]) << 24) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 8) | uint32_t(b[4]);
}
void St25r3916::clearIrq() { (void)readIrq(); }

bool St25r3916::enableOsc(std::string& err) {
    modifyReg(REG_OP_CONTROL, OP_EN, 0x00);  // en=1: oscilador+regulador
    for (int i = 0; i < 50; ++i) {           // espera osc_ok (~hasta 50ms)
        if (readReg(REG_AUX_DISPLAY) & AUX_OSC_OK) return true;
        sleep_ms(1);
    }
    err = "oscillator not stable (osc_ok=0)";
    return false;
}

// Arranque común del chip (identidad, set default, IO config, oscilador, reguladores).
// NO configura modo NFC ni enciende campo. Lo usan el lector (init) y el emulador.
bool St25r3916::baseBringUp(InitStatus& st) {
    // 0) Identidad con reintentos
    bool detected = false;
    for (int i = 0; i < 5; ++i) {
        uint8_t id = readReg(REG_IC_IDENTITY);
        st.ic_type = (id >> 3) & 0x1F;
        st.ic_rev = id & 0x07;
        if (st.ic_type == 0x05 && st.ic_rev != 0) { detected = true; break; }
        sleep_ms(10);
    }
    if (!detected) { st.error = "ST25R3916 not detected"; return false; }

    // 1) Reset defensivo
    cmd(CMD_STOP_ALL);
    modifyReg(REG_OP_CONTROL, 0x00, OP_TX_EN | OP_RX_EN);
    sleep_ms(2);

    // 2) Power-on: set default + protección de sobrecalentamiento
    cmd(CMD_SET_DEFAULT);
    const uint8_t protection[2] = {0x04, 0x10};
    cmdData(CMD_TEST_ACCESS, protection, 2);

    // 3) IO config (SPI): igual que la app oficial -> IO_CONF_1=0x07, IO_CONF_2=0x3C
    //    (aat_en + miso pull-down + drive level; SIN sup3v). TX_DRIVER=0xD0.
    uint8_t io2 = IO2_AAT_EN | IO2_MISO_PD1 | IO2_MISO_PD2 | IO2_DRV_LVL | (vdd5_ ? 0 : IO2_SUP3V);
    writeReg16(REG_IO_CONF_1, uint16_t(0x07 << 8) | io2);
    writeReg(REG_TX_DRIVER, 0xD0);

    writeRegB(REGB_RESISTIVE_AM, 0x80);       // non-overlap mínimo
    writeRegB(REGB_RESISTIVE_AM, 0x00);       // non-overlap normal

    writeReg(REG_EFD_ACT_THR, 0x13);
    writeReg(REG_EFD_DEACT_THR, 0x02);
    writeReg(REG_NFCIP1_PT_DEF, 0x50);
    writeReg(REG_PT_MOD, 0x5F);
    writeRegB(REGB_EMD_SUPPRESSION, 0x40);
    writeReg(REG_ANT_TUNE_1, 0x82);
    writeReg(REG_ANT_TUNE_2, 0x82);

    modifyReg(REG_OP_CONTROL, OP_EFD_AUTO, 0x00);  // detector de campo externo auto
    cmd(CMD_CLEAR_FIFO);

    // 4) Reguladores: enmascarar interrupciones salvo error, arrancar oscilador, ajustar
    writeReg32(REG_MASK_MAIN_IRQ, 0xFFFF00FFu);
    clearIrq();
    if (!enableOsc(st.error)) return false;
    writeReg32(REG_MASK_MAIN_IRQ, 0x00000000u);
    cmd(CMD_ADJUST_REGULATORS);
    sleep_ms(5);

    st.osc_ok = (readReg(REG_AUX_DISPLAY) & AUX_OSC_OK) != 0;
    st.regulator_raw = readRegB(REGB_REGULATOR_DISPLAY);
    int rv = (st.regulator_raw >> 4) & 0x0F;
    st.regulator_volts = vdd5_ ? (3.6f + 0.1f * rv) : (2.4f + 0.1f * rv);
    return true;
}

InitStatus St25r3916::init() {
    InitStatus st;
    if (!baseBringUp(st)) return st;

    // 5) Modo lector NFC-A (ISO14443A) + campo ON
    cmd(CMD_STOP_ALL);
    modifyReg(REG_OP_CONTROL, 0x00, OP_WU);           // sin wake-up
    writeReg(REG_MODE_DEF, 0x08);                     // initiator, ISO14443A (como la oficial)
    writeReg(REG_BITRATE_DEF, 0x00);                  // 106/106 kbps
    writeReg(REG_ISO14443A, 0x00);
    writeReg(REG_TIMER_EMV, 0x02);                    // nrt_emv
    modifyReg(REG_AUX_DEF, 0x00, 0x04);               // dis_corr=0
    writeRegB(REGB_OVERSHOOT_1, 0x40);
    writeRegB(REGB_OVERSHOOT_2, 0x03);
    writeRegB(REGB_UNDERSHOOT_1, 0x40);
    writeRegB(REGB_UNDERSHOOT_2, 0x03);
    writeRegB(REGB_CORRELATOR_1, 0x47);
    writeRegB(REGB_CORRELATOR_2, 0x00);
    writeReg(REG_RX_CONF_1, 0x08);                    // z_600k
    writeReg(REG_RX_CONF_2, 0x2D);                    // sqm_dyn|agc_en|agc_m|agc6_3
    writeReg(REG_RX_CONF_3, 0xD8);
    writeReg(REG_RX_CONF_4, 0x22);
    cmd(CMD_RESET_RX_GAIN);
    writeReg32(REG_MASK_MAIN_IRQ, 0x00000000u);

    // Campo ON: flujo COMPLETO como la app oficial (el "simple" no enciende en esta placa):
    //   STOP -> en -> en+en_fd_ca -> guard timer -> C8 -> esperar I_cat -> en+tx_en+rx_en+en_fd_ca
    fieldOn();
    st.aux_display_raw = readReg(REG_AUX_DISPLAY);
    st.op_control_raw = readReg(REG_OP_CONTROL);
    st.tx_on = (st.aux_display_raw & AUX_TX_ON) != 0;

    // Amplitud de RF: prueba de campo sin tarjeta
    st.rf_amplitude = measureAmplitude();

    st.ok = true;
    return st;
}

void St25r3916::fieldOn() {
    // Encendido determinista: oscilador + comando de field-on (conmuta el campo) y
    // habilitar tx/rx. Sin evitación de colisión (en_fd), que puede abortar y dejar el
    // campo apagado en reencendidos. Deja OP_CONTROL=0xC8 (en+rx_en+tx_en).
    cmd(CMD_STOP_ALL);
    writeReg(REG_OP_CONTROL, OP_EN);                 // 0x80: oscilador/regulador
    clearIrq();
    cmd(CMD_NFC_INITIAL_FIELD_ON);                   // conmuta el campo
    sleep_ms(1);
    writeReg(REG_OP_CONTROL, OP_EN | OP_RX_EN | OP_TX_EN);  // 0xC8: campo encendido
    sleep_ms(1);                                     // tiempo de guarda
}

void St25r3916::fieldOff() {
    cmd(CMD_STOP_ALL);
    modifyReg(REG_OP_CONTROL, 0x00, OP_TX_EN | OP_RX_EN);
}

uint8_t St25r3916::measureAmplitude() {
    cmd(CMD_MEASURE_AMPLITUDE);
    sleep_ms(2);
    return readReg(REG_AD_OUTPUT);
}

uint32_t St25r3916::waitIrq(uint32_t mask, int timeout_ms) {
    uint32_t acc = 0;
    for (int i = 0; i <= timeout_ms * 20; ++i) {
        acc |= readIrq();
        if (acc & mask) return acc;
        usleep(50);
    }
    return acc | IRQ_NRE;  // timeout
}

// --- helpers internos de recepción ---
static void setAntcl(St25r3916& d, bool on) { d.writeReg(st25::REG_ISO14443A, on ? 0x01 : 0x00); }
static void setNoCrcRx(St25r3916& d, bool on) { d.modifyReg(st25::REG_AUX_DEF, on ? 0x80 : 0x00, on ? 0x00 : 0x80); }
// Prepara una transmisión: para actividad previa, resetea ganancia RX y arranca el
// No-Response Timer (abre la ventana de recepción; sin esto no llega I_rxe). Como la oficial.
static void prepTx(St25r3916& d) {
    d.cmd(st25::CMD_STOP_ALL);
    d.cmd(st25::CMD_RESET_RX_GAIN);
    d.writeReg16(st25::REG_NRT_1, 0x0350);  // NRT (reg 0x10/0x11)
}

int receiveFrame(St25r3916& d, uint8_t* buf, int maxlen, int timeout_ms, uint8_t& err_reg) {
    // Esperar al FIN de recepción (I_rxe), no al inicio (I_rxs): si no, se lee el FIFO
    // antes de que lleguen todos los bytes y la trama sale truncada.
    uint32_t irq = d.waitIrq(st25::IRQ_RXE | st25::IRQ_COL | st25::IRQ_NRE, timeout_ms);
    err_reg = 0;
    if (irq & st25::IRQ_ERR_MASK) err_reg = (irq >> 8) & 0xFF;
    uint8_t s1 = d.readReg(st25::REG_FIFO_STATUS_1);
    uint8_t s2 = d.readReg(st25::REG_FIFO_STATUS_2);
    int bytes = s1 | ((s2 & 0xC0) << 2);
    if (getenv("NFC_DBGRX")) {
        fprintf(stderr, "    [rx] irq=0x%08X txe=%d rxs=%d rxe=%d col=%d nre=%d err=0x%02X fifo=%d\n",
                irq, !!(irq & st25::IRQ_TXE), !!(irq & st25::IRQ_RXS), !!(irq & st25::IRQ_RXE),
                !!(irq & st25::IRQ_COL), !!(irq & st25::IRQ_NRE), err_reg, bytes);
    }
    if (!(irq & st25::IRQ_RXE)) return -1;  // sin fin de recepción: timeout/no-response/colisión
    uint8_t rxbits = (s2 >> 1) & 0x07;
    int toread = bytes;
    if (bytes == 0 && rxbits > 0) toread = 1;  // respuesta de bits sueltos (p.ej. ACK de 4 bits)
    if (toread <= 0) return 0;
    int n = toread < maxlen ? toread : maxlen;
    auto v = d.fifoRead(n);
    for (int i = 0; i < n; ++i) buf[i] = v[i];
    return n;
}

St25r3916::TxRx St25r3916::transceive(const uint8_t* tx, uint32_t nbits, bool with_crc, int timeout_ms) {
    TxRx r;
    uint32_t nbytes = nbits / 8, extra_bits = nbits % 8;
    prepTx(*this);
    setAntcl(*this, false);
    setNoCrcRx(*this, false);
    clearIrq();
    cmd(CMD_CLEAR_FIFO);
    fifoLoad(tx, nbytes + (extra_bits ? 1 : 0));
    writeReg16(REG_NUM_TX_BYTES_1, uint16_t(((nbytes & 0x1FF) << 3) | (extra_bits & 0x07)));
    cmd(with_crc ? CMD_TRANSMIT_WITH_CRC : CMD_TRANSMIT_WITHOUT_CRC);
    uint8_t buf[32];
    int n = receiveFrame(*this, buf, sizeof(buf), timeout_ms, r.err_reg);
    if (n < 0) return r;
    r.data.assign(buf, buf + n);
    r.ok = true;
    return r;
}

static bool requestWakeup(St25r3916& d, uint8_t cmd_code, uint8_t atqa[2]) {
    prepTx(d);
    setAntcl(d, false);          // settings=0x00 (como la oficial)
    setNoCrcRx(d, true);         // recibir ATQA sin CRC
    d.clearIrq();
    d.cmd(st25::CMD_CLEAR_FIFO);
    d.writeReg16(st25::REG_NUM_TX_BYTES_1, 0x0000);  // frame corto por hardware
    d.cmd(cmd_code);
    uint8_t buf[8], err;
    int n = receiveFrame(d, buf, sizeof(buf), 20, err);
    if (n >= 2) { atqa[0] = buf[0]; atqa[1] = buf[1]; return true; }
    return false;
}

bool St25r3916::reqa(uint8_t atqa[2]) { return requestWakeup(*this, CMD_TRANSMIT_REQA, atqa); }
bool St25r3916::wupa(uint8_t atqa[2]) { return requestWakeup(*this, CMD_TRANSMIT_WUPA, atqa); }

static uint8_t bcc4(const uint8_t* p) { return p[0] ^ p[1] ^ p[2] ^ p[3]; }

// Un nivel de cascada: ANTICOLL (sin colisión, una sola tarjeta) + SELECT -> SAK.
// uid_out recibe 4 bytes (CLn, puede incluir CT=0x88 en el primero); cascade=true si SAK pide otro nivel.
bool St25r3916::anticollisionLevel(uint8_t sel_cmd, uint8_t* uid_out, uint8_t& sak_out, bool& cascade) {
    // ANTICOLL: [SEL, 0x20] sin CRC, recibe 5 bytes (4 UID + BCC)
    prepTx(*this);
    setAntcl(*this, true);
    setNoCrcRx(*this, false);
    clearIrq();
    cmd(CMD_CLEAR_FIFO);
    uint8_t ac[2] = {sel_cmd, 0x20};
    fifoLoad(ac, 2);
    writeReg16(REG_NUM_TX_BYTES_1, uint16_t((2 & 0x1FF) << 3));
    cmd(CMD_TRANSMIT_WITHOUT_CRC);
    uint8_t rbuf[8], err;
    int n = receiveFrame(*this, rbuf, sizeof(rbuf), 20, err);
    if (getenv("NFC_DEBUG")) {
        fprintf(stderr, "  [AC sel=%02X] n=%d err=0x%02X:", sel_cmd, n, err);
        for (int i = 0; i < n && i < 8; ++i) fprintf(stderr, " %02X", rbuf[i]);
        fprintf(stderr, " (bcc esperado=%02X)\n", bcc4(rbuf));
    }
    if (n != 5) return false;
    if (rbuf[4] != bcc4(rbuf)) return false;      // BCC
    for (int i = 0; i < 4; ++i) uid_out[i] = rbuf[i];

    // SELECT: [SEL, 0x70, uid0..3, bcc] con CRC, recibe SAK (1 byte + CRC)
    uint8_t sel[7] = {sel_cmd, 0x70, rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4]};
    TxRx sr = transceive(sel, 7 * 8, true, 20);
    if (getenv("NFC_DEBUG")) {
        fprintf(stderr, "  [SELECT sel=%02X] ok=%d err=0x%02X data:", sel_cmd, sr.ok, sr.err_reg);
        for (auto b : sr.data) fprintf(stderr, " %02X", b);
        fprintf(stderr, "\n");
    }
    if (!sr.ok || sr.data.empty()) return false;
    sak_out = sr.data[0];
    cascade = (sak_out & 0x04) != 0;   // bit cascada: UID incompleto, otro nivel
    return true;
}

bool St25r3916::activateA(PiccA& picc) {
    picc = PiccA{};
    if (!reqa(picc.atqa)) {
        if (!wupa(picc.atqa)) return false;
    }
    const uint8_t sel_cmds[3] = {0x93, 0x95, 0x97};  // CL1, CL2, CL3
    uint8_t uid[10];
    int uid_len = 0;
    for (int lv = 0; lv < 3; ++lv) {
        uint8_t cln[4], sak;
        bool cascade;
        if (!anticollisionLevel(sel_cmds[lv], cln, sak, cascade)) return false;
        if (cln[0] == 0x88) {   // Cascade Tag: 3 bytes de UID en cln[1..3]
            uid[uid_len++] = cln[1];
            uid[uid_len++] = cln[2];
            uid[uid_len++] = cln[3];
        } else {                 // 4 bytes de UID
            for (int i = 0; i < 4; ++i) uid[uid_len++] = cln[i];
        }
        if (!cascade) { picc.sak = sak; break; }  // UID completo
    }
    picc.uid_len = uid_len;
    for (int i = 0; i < uid_len && i < 10; ++i) picc.uid[i] = uid[i];
    return uid_len == 4 || uid_len == 7 || uid_len == 10;
}

void St25r3916::configureNfcB() {
    // Tabla de registros de NFC-B (ISO14443-B) calcada de configure_nfc_b de M5Unit-NFC.
    cmd(CMD_STOP_ALL);
    modifyReg(REG_OP_CONTROL, 0x00, OP_WU);
    writeReg(REG_MODE_DEF, 0x14);     // initiator, ISO14443B, tr_am
    writeReg(REG_BITRATE_DEF, 0x00);  // 106/106
    writeReg(0x06, 0x00);             // ISO14443B settings
    writeReg(REG_NFCIP1_PT_DEF, 0x5D);
    writeReg(0x09, 0x38);             // stream mode definition
    writeReg(REG_AUX_DEF, 0x00);
    writeReg(REG_RX_CONF_1, 0x04);
    writeReg(REG_RX_CONF_2, 0x3D);
    writeReg(REG_RX_CONF_3, 0x00);
    writeReg(REG_RX_CONF_4, 0x00);
    writeReg(0x0F, 0x07);             // mask receive timer
    writeReg(REG_NRT_1, 0x10); writeReg(0x11, 0x0D);  // NRT
    writeReg(REG_TIMER_EMV, 0x23);
    writeReg(0x13, 0x00); writeReg(0x14, 0x58); writeReg(0x15, 0x80);  // GPT1/GPT2/PPON2
    writeReg(0x16, 0x85); writeReg(0x17, 0xA6); writeReg(0x18, 0x0F); writeReg(0x19, 0x7B);  // masks
    writeReg(REG_TX_DRIVER, 0x70);
    writeReg(REG_PT_MOD, 0x5F);
    writeReg(REG_EFD_ACT_THR, 0x13);
    writeReg(REG_EFD_DEACT_THR, 0x02);
    // Space-B
    writeRegB(0x05, 0xC4); writeRegB(0x06, 0x14); writeRegB(0x0B, 0x0C);
    writeRegB(REGB_CORRELATOR_1, 0x1B); writeRegB(REGB_CORRELATOR_2, 0x00);
    writeRegB(0x0F, 0x00); writeRegB(REGB_FIELD_ON_GUARD, 0x00);
    writeRegB(0x28, 0x10); writeRegB(0x29, 0x7C); writeRegB(REGB_RESISTIVE_AM, 0x80);
    writeReg(0x2C, 0xD0);             // regulator voltage control
    fieldOn();
}

bool St25r3916::nfcbDetect(uint8_t* atqb, int& len) {
    len = 0;
    prepTx(*this);
    modifyReg(REG_AUX_DEF, 0x00, 0x80);  // no_crc_rx = 0 (CRC-B esperado)
    clearIrq();
    cmd(CMD_CLEAR_FIFO);
    uint8_t reqb[3] = {0x05, 0x00, 0x00};  // APf, AFI, PARAM
    fifoLoad(reqb, 3);
    writeReg16(REG_NUM_TX_BYTES_1, uint16_t((3 & 0x1FF) << 3));
    cmd(CMD_TRANSMIT_WITH_CRC);
    uint8_t buf[32], err;
    int n = receiveFrame(*this, buf, sizeof(buf), 20, err);
    if (n <= 0) return false;
    len = n;
    for (int i = 0; i < n && i < 32; ++i) atqb[i] = buf[i];
    return true;
}

// CRC-16 ISO15693 (poly 0x1021 reflejado = 0x8408, init 0xFFFF, xorout 0xFFFF)
static uint16_t crc16_15693(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
    }
    return ~crc;
}
// Codifica un byte en 4 símbolos 1-de-4 (LSB primero)
static void encode1of4(std::vector<uint8_t>& out, uint8_t data) {
    static const uint8_t sym[4] = {0x02, 0x08, 0x20, 0x80};  // 00,01,10,11
    for (int i = 0; i < 4; ++i) { out.push_back(sym[data & 0x03]); data >>= 2; }
}

// Decodifica la respuesta ISO15693 del FIFO (modo subcarrier stream): 2 bits de FIFO por bit
// de datos, SOF de 5 bits, patrón 0x01->0, 0x02->1, 0x1D->EOF. Portado de furi_hal_nfc_iso15693.c
// del firmware del Flipper (mismo chip). Devuelve nº de bytes decodificados.
static int decodeIso15693(const uint8_t* fifo, int nbytes, uint8_t* out, int outcap) {
    for (int i = 0; i < outcap; ++i) out[i] = 0;
    int buf_bits = nbytes * 8;
    if (buf_bits <= 10) return 0;
    int bit_pos = 0;
    for (int i = 5 /*SOF 5 bits*/; i < buf_bits - 5; i += 2) {
        int byte_index = i / 8, bit_offset = i % 8;
        uint8_t resp = (fifo[byte_index] >> bit_offset) |
                       (uint8_t)(fifo[byte_index + 1] << (8 - bit_offset));
        if (resp == 0x1D) break;              // EOF
        uint8_t pat = resp & 0x03;
        if (pat == 0x01) { bit_pos++; }       // bit 0
        else if (pat == 0x02) { out[bit_pos / 8] |= 1 << (bit_pos % 8); bit_pos++; }  // bit 1
        else break;
        if (bit_pos / 8 >= outcap) break;
    }
    return bit_pos / 8;
}

void St25r3916::configureNfcV() {
    cmd(CMD_STOP_ALL);
    modifyReg(REG_OP_CONTROL, 0x00, OP_WU);
    writeReg(REG_RX_CONF_1, 0x13);  // lp0|h80|z12k
    writeReg(REG_RX_CONF_2, 0x2D);
    writeReg(REG_RX_CONF_3, 0x00);
    writeReg(REG_RX_CONF_4, 0x00);
    writeReg(REG_TX_DRIVER, 0x70);
    writeReg(REG_IO_CONF_1, 0x07);
    modifyReg(REG_IO_CONF_2, IO2_AAT_EN, 0x00);
    modifyReg(REG_OP_CONTROL, OP_EFD_AUTO, 0x00);
    writeReg(0x09, 0x38);           // stream mode definition
    writeReg(REG_AUX_DEF, 0x02);
    writeRegB(0x05, 0x40); writeRegB(0x06, 0x14); writeRegB(0x0B, 0x0C);
    writeRegB(REGB_CORRELATOR_1, 0x13); writeRegB(REGB_CORRELATOR_2, 0x01);
    writeRegB(0x0F, 0x00); writeRegB(REGB_FIELD_ON_GUARD, 0x00);
    writeRegB(0x28, readRegB(0x28) & ~0x88); writeRegB(0x29, 0x7C);  // AUX_MOD: usar regulador AM (como Flipper)
    writeReg(REG_MODE_DEF, 0x70);   // subcarrier stream mode
    fieldOn();
}

bool St25r3916::nfcvInventory(uint8_t* raw, int& len) {
    len = 0;
    // Petición Inventory: flags=0x26 (alta velocidad, 1 slot, inventory), cmd=0x01, mask-len=0x00
    uint8_t req[3] = {0x26, 0x01, 0x00};
    uint16_t crc = crc16_15693(req, 3);
    std::vector<uint8_t> frame = {req[0], req[1], req[2],
                                  uint8_t(crc & 0xFF), uint8_t((crc >> 8) & 0xFF)};
    std::vector<uint8_t> enc;
    enc.push_back(0x21);  // SOF 1-de-4
    for (uint8_t b : frame) encode1of4(enc, b);
    enc.push_back(0x04);  // EOF

    prepTx(*this);
    clearIrq();
    cmd(CMD_CLEAR_FIFO);
    fifoLoad(enc.data(), enc.size());
    writeReg16(REG_NUM_TX_BYTES_1, uint16_t((enc.size() & 0x1FF) << 3));
    cmd(CMD_TRANSMIT_WITHOUT_CRC);
    uint8_t buf[128], err;
    int n = receiveFrame(*this, buf, sizeof(buf), 20, err);
    if (n <= 0) return false;
    // Decodificar el stream ISO15693 a bytes de datos
    uint8_t dec[64];
    int dn = decodeIso15693(buf, n, dec, sizeof(dec));
    if (dn <= 0) { len = n; for (int i = 0; i < n && i < 64; ++i) raw[i] = buf[i]; return true; }
    len = dn;
    for (int i = 0; i < dn && i < 64; ++i) raw[i] = dec[i];
    return true;
}

bool St25r3916::iclassIdentify(uint8_t* raw, int& len) {
    len = 0;
    // Envía una trama iCLASS sobre la capa ISO15693 (SOF 1-de-4 + bytes + EOF).
    // Tramas iCLASS sobre ISO15693 1-de-4, como la app Picopass del Flipper (nfc_poller_trx):
    // ACTALL {0x0A} y IDENTIFY {0x0C}, ambos SIN CRC. FWT ~7,4 ms (NRT largo).
    auto sendFrame = [&](const uint8_t* d, int n, bool want_rx, uint8_t* rx, int& rxlen) -> bool {
        std::vector<uint8_t> enc;
        enc.push_back(0x21);  // SOF 1-de-4
        for (int i = 0; i < n; ++i) encode1of4(enc, d[i]);
        enc.push_back(0x04);  // EOF
        cmd(CMD_STOP_ALL);
        cmd(CMD_RESET_RX_GAIN);
        writeReg16(REG_NRT_1, 0x0A00);   // No-Response Timer largo (~>7ms) para iCLASS
        clearIrq();
        cmd(CMD_CLEAR_FIFO);
        fifoLoad(enc.data(), enc.size());
        writeReg16(REG_NUM_TX_BYTES_1, uint16_t((enc.size() & 0x1FF) << 3));
        cmd(CMD_TRANSMIT_WITHOUT_CRC);
        if (!want_rx) { waitIrq(IRQ_TXE | IRQ_RXE | IRQ_NRE, 12); return true; }
        uint8_t buf[128], err;
        int m = receiveFrame(*this, buf, sizeof(buf), 40, err);
        if (m <= 0) return false;
        uint8_t dec[64];
        int dn = decodeIso15693(buf, m, dec, sizeof(dec));
        if (dn <= 0) { rxlen = m; for (int i = 0; i < m && i < 64; ++i) rx[i] = buf[i]; return true; }
        rxlen = dn;
        for (int i = 0; i < dn && i < 64; ++i) rx[i] = dec[i];
        return true;
    };
    const uint8_t act_all[1]  = {0x0A};
    const uint8_t identify[1] = {0x0C};
    int rl = 0;
    sendFrame(act_all, 1, false, nullptr, rl);   // despierta (responde sólo SOF)
    return sendFrame(identify, 1, true, raw, len) && len > 0;
}

void St25r3916::configureNfcF() {
    cmd(CMD_STOP_ALL);
    modifyReg(REG_OP_CONTROL, 0x00, OP_WU);
    writeReg(REG_MODE_DEF, 0x1C);     // initiator, FeliCa, tr_am
    writeReg(REG_BITRATE_DEF, 0x11);  // 212/212 kbps
    writeReg(REG_IO_CONF_1, 0x07);
    writeReg(REG_AUX_DEF, 0x00);
    writeReg(REG_RX_CONF_1, 0x13);
    writeReg(REG_RX_CONF_2, 0x3D);
    writeReg(REG_RX_CONF_3, 0x00);
    writeReg(REG_RX_CONF_4, 0x00);
    writeRegB(REGB_CORRELATOR_1, 0x54);
    writeRegB(REGB_CORRELATOR_2, 0x00);
    writeReg32(REG_MASK_MAIN_IRQ, 0x00000000u);
    fieldOn();
}

bool St25r3916::nfcfPoll(uint8_t* res, int& len) {
    len = 0;
    prepTx(*this);
    modifyReg(REG_AUX_DEF, 0x00, 0x80);  // no_crc_rx = 0
    clearIrq();
    cmd(CMD_CLEAR_FIFO);
    uint8_t poll[6] = {0x06, 0x00, 0xFF, 0xFF, 0x00, 0x00};  // LEN, SENSF_REQ, sys=FFFF, rc=0, tsn=0
    fifoLoad(poll, 6);
    writeReg16(REG_NUM_TX_BYTES_1, uint16_t((6 & 0x1FF) << 3));
    cmd(CMD_TRANSMIT_WITH_CRC);
    uint8_t buf[32], err;
    int n = receiveFrame(*this, buf, sizeof(buf), 20, err);
    if (n <= 0) return false;
    len = n;
    for (int i = 0; i < n && i < 32; ++i) res[i] = buf[i];
    return true;
}

void St25r3916::writePtMemoryA(const uint8_t* d, int n) {
    std::vector<uint8_t> b;
    b.reserve(1 + n);
    b.push_back(OP_LOAD_PT_MEMORY_A);
    b.insert(b.end(), d, d + n);
    xfer(b.data(), b.size());
}

bool St25r3916::runEmulationA(const PiccA& p, const uint8_t* mem, int nbytes, int secs,
                              EmuStats& stats, std::string& err, const std::atomic<bool>* stop) {
    if (p.uid_len != 4 && p.uid_len != 7) { err = "only 4- or 7-byte UIDs"; return false; }
    InitStatus st;
    if (!baseBringUp(st)) { err = st.error; return false; }

    // configure_emulation_a: target, NFC-A, detección de bitrate
    cmd(CMD_STOP_ALL);
    writeReg(REG_MODE_DEF, 0xC8);
    writeReg(REG_NFCIP1_PT_DEF, 0x5C);
    writeReg(0x19, 0x02);              // mask passive target interrupt (I_wu_ax)
    writeReg(REG_TIMER_EMV, 0x08);     // mrt_step 512

    // PT memory A: UID(10) + ATQA(2) + SAK x3
    uint8_t wbuf[15] = {0};
    for (int i = 0; i < p.uid_len; ++i) wbuf[i] = p.uid[i];
    wbuf[10] = p.atqa[0];              // ATQA LSB
    wbuf[11] = p.atqa[1];              // ATQA MSB
    wbuf[12] = (p.uid_len == 4) ? (p.sak & ~0x04) : (p.sak | 0x04);  // SAK lv1 (cascada si 7B)
    wbuf[13] = (p.sak & ~0x04);
    wbuf[14] = (p.sak & ~0x04);
    modifyReg(REG_AUX_DEF, (p.uid_len == 7) ? 0x10 : 0x00, 0x30);    // uid_7 / uid_4
    writePtMemoryA(wbuf, 15);

    // start_emulation
    modifyReg(REG_NFCIP1_PT_DEF, 0x0C, 0x01);  // auto-respuesta sólo NFC-A
    modifyReg(REG_TIMER_EMV, 0x00, 0xE0);      // sin trigger GPT
    modifyReg(REG_TIMER_EMV, 0x08, 0x00);      // pasos 512/fc
    writeReg(0x0F, 0x03);                      // MRT ~100us
    modifyReg(REG_ISO14443A, 0x00, 0x80 | 0x40 | 0x20);  // paridad on, NFCIP-1 off
    writeReg(REG_ANT_TUNE_1, 0x00);
    writeReg(REG_ANT_TUNE_2, 0xFF);
    writeRegB(REGB_OVERSHOOT_1, 0x00); writeRegB(REGB_OVERSHOOT_2, 0x00);
    writeRegB(REGB_UNDERSHOOT_1, 0x00); writeRegB(REGB_UNDERSHOOT_2, 0x00);
    // Umbral del detector de campo externo bajo: con el 0x13 de lectura, en modo tarjeta el chip
    // ni veía el campo de un móvil (diagnóstico 23-nov-2026: con 0x02 salta I_eon de sobra).
    writeReg(REG_EFD_ACT_THR, 0x02);
    writeReg(REG_EFD_DEACT_THR, 0x01);
    writeReg32(REG_MASK_MAIN_IRQ, 0x00000000u);
    cmd(CMD_UNMASK_RECEIVE_DATA);
    clearIrq();
    cmd(CMD_GO_TO_SENSE);

    int npages = nbytes / 4;
    auto t0 = std::chrono::steady_clock::now();
    auto deadline = t0 + std::chrono::seconds(secs);
    while ((secs <= 0 || std::chrono::steady_clock::now() < deadline) && !(stop && stop->load())) {
        uint32_t irq = readIrq();
        uint8_t pta = irq & 0xFF;                 // reg interrupción passive target (0x1D)
        if (pta & (IRQ_WU_A | 0x02)) ++stats.selections;  // seleccionados como target
        if (irq & (0x10u << 16)) ++stats.fields;  // I_eon: detectado campo externo (lector cerca)
        if (irq & IRQ_EOF) cmd(CMD_GO_TO_SENSE);   // I_eof: campo caído -> volver a sense
        if (irq & IRQ_RXE) {                      // llegó un comando estando activos
            uint8_t s1 = readReg(REG_FIFO_STATUS_1);
            int n = s1;
            if (n > 0) {
                auto rx = fifoRead(n);
                uint8_t c0 = rx[0];
                if (c0 == 0x30 && rx.size() >= 2 && npages > 0) {        // READ pág
                    uint8_t page = rx[1];
                    uint8_t resp[16];
                    for (int i = 0; i < 16; ++i) resp[i] = mem[((page * 4 + i) % nbytes)];
                    cmd(CMD_CLEAR_FIFO);
                    fifoLoad(resp, 16);
                    writeReg16(REG_NUM_TX_BYTES_1, uint16_t((16 & 0x1FF) << 3));
                    cmd(CMD_TRANSMIT_WITH_CRC);
                    ++stats.reads;
                } else if (c0 == 0x50) {            // HALT
                    cmd(CMD_GO_TO_SLEEP);
                } else {
                    ++stats.other;
                }
            }
            cmd(CMD_UNMASK_RECEIVE_DATA);
        }
        usleep(200);
    }
    cmd(CMD_STOP_ALL);
    return true;
}

bool St25r3916::type2Read(uint8_t page, uint8_t out[16]) {
    uint8_t c[2] = {0x30, page};           // READ
    TxRx r = transceive(c, 2 * 8, true, 20);
    if (!r.ok || r.data.size() < 16) return false;
    for (int i = 0; i < 16; ++i) out[i] = r.data[i];
    return true;
}

bool St25r3916::type2GetVersion(uint8_t* ver, int& len) {
    len = 0;
    uint8_t c[1] = {0x60};
    TxRx r = transceive(c, 1 * 8, true, 20);
    if (!r.ok || r.data.size() < 8) return false;
    len = (int)r.data.size();
    for (int i = 0; i < len && i < 16; ++i) ver[i] = r.data[i];
    return true;
}

bool St25r3916::rats(uint8_t* ats, int& len) {
    len = 0;
    uint8_t c[2] = {0xE0, 0x80};   // RATS: FSDI=8 (256B), CID=0
    TxRx r = transceive(c, 2 * 8, true, 40);
    if (!r.ok || r.data.empty()) return false;
    len = (int)r.data.size();
    for (int i = 0; i < len && i < 32; ++i) ats[i] = r.data[i];
    return true;
}

bool St25r3916::type2Write(uint8_t page, const uint8_t data[4]) {
    // El comando WRITE lleva CRC, pero la respuesta ACK son 4 bits SIN CRC:
    // hay que recibir con no_crc_rx=1 o el chip la descarta por "CRC erróneo".
    uint8_t c[6] = {0xA2, page, data[0], data[1], data[2], data[3]};
    prepTx(*this);
    writeReg16(REG_NRT_1, 0x1000);   // NRT largo: la escritura EEPROM tarda ~5 ms antes del ACK
    setAntcl(*this, false);
    setNoCrcRx(*this, true);
    clearIrq();
    cmd(CMD_CLEAR_FIFO);
    fifoLoad(c, 6);
    writeReg16(REG_NUM_TX_BYTES_1, uint16_t((6 & 0x1FF) << 3));
    cmd(CMD_TRANSMIT_WITH_CRC);
    uint8_t buf[8], err;
    int n = receiveFrame(*this, buf, sizeof(buf), 20, err);
    if (n < 1) return false;
    return (buf[0] & 0x0F) == 0x0A;    // ACK = 0xA (4 bits)
}

// ---------------------------------------------------------------------------
// MIFARE Classic (Crypto1 por software). Portado de la referencia M5Unit-NFC
// (unit_ST25R3916_nfca.cpp, MIT): autenticación de 3 pasos y lectura cifrada.
// El cifrado en sí vive en crypto1.h. El truco es que MIFARE usa paridad propia
// (parte del cifrado), así que se transmite con no_tx_par y el bitstream con la
// paridad metida a mano; y se recibe sin comprobar CRC.
// ---------------------------------------------------------------------------
namespace {

constexpr uint8_t ISO_NO_TX_PAR = 0x80;  // REG_ISO14443A bit7

// CRC_A de ISO14443-A (init 0x6363, polinomio reflejado 0x8408).
uint16_t crc_a(const uint8_t* d, int n) {
    uint16_t crc = 0x6363;
    for (int i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
    }
    return crc;
}

uint64_t key_to64(const uint8_t k[6]) {
    uint64_t v = 0;
    for (int i = 0; i < 6; ++i) v = (v << 8) | k[i];
    return v;
}

uint32_t array_to32(const uint8_t a[4]) {
    return (uint32_t(a[0]) << 24) | (uint32_t(a[1]) << 16) | (uint32_t(a[2]) << 8) | a[3];
}

// Mete los bits de `in` (in_len bytes) seguidos cada uno de su bit de paridad en `out`.
void append_parity(uint8_t* out, int out_len, const uint8_t* in, int in_len, uint32_t parity) {
    for (int i = 0; i < out_len; ++i) out[i] = 0;
    int bitpos = 0;
    for (int i = 0; i < in_len; ++i) {
        uint8_t v = in[i];
        for (int k = 0; k < 8; ++k) {
            if ((v >> k) & 1) out[bitpos >> 3] |= 1u << (bitpos & 7);
            ++bitpos;
        }
        if ((parity >> i) & 1) out[bitpos >> 3] |= 1u << (bitpos & 7);
        ++bitpos;
    }
}

} // namespace

// Envía una trama ya con el CRC_A puesto, cifrada y con la paridad a mano (no_tx_par).
bool St25r3916::mfSendEncrypted(const uint8_t* tx, int tx_len) {
    if (tx_len <= 0 || tx_len > 32) return false;
    uint8_t tmp[34];
    memcpy(tmp, tx, tx_len);
    uint16_t crc = crc_a(tx, tx_len);
    tmp[tx_len] = crc & 0xFF;
    tmp[tx_len + 1] = crc >> 8;
    const int with_crc = tx_len + 2;

    uint8_t enc[34];
    uint32_t parity = mf_crypto_.encrypt(enc, tmp, with_crc);

    const int total_bits = 9 * with_crc;
    const int bs_len = (total_bits + 7) >> 3;
    uint8_t bitstream[40];
    append_parity(bitstream, bs_len, enc, with_crc, parity);

    writeReg(REG_ISO14443A, ISO_NO_TX_PAR);
    setNoCrcRx(*this, true);
    clearIrq();
    cmd(CMD_CLEAR_FIFO);
    fifoLoad(bitstream, bs_len);
    writeReg16(REG_NUM_TX_BYTES_1, uint16_t(((total_bits >> 3) << 3) | (total_bits & 0x07)));
    cmd(CMD_TRANSMIT_WITHOUT_CRC);
    return true;
}

bool St25r3916::mfTransceiveEncrypted(uint8_t* rx, int& rx_len, const uint8_t* tx, int tx_len,
                                      int timeout_ms, bool include_crc, bool decrypt) {
    prepTx(*this);
    if (!mfSendEncrypted(tx, tx_len)) return false;

    uint8_t err = 0;
    uint8_t buf[32];
    int n = receiveFrame(*this, buf, sizeof(buf), timeout_ms, err);
    if (n <= 0) return false;

    if (decrypt) {
        if (n == 1) {                       // ACK/NACK de 4 bits
            uint8_t ret = buf[0] & 0x0F, res = 0;
            for (int b = 0; b < 4; ++b) res |= (mf_crypto_.step_with(0) ^ ((ret >> b) & 1)) << b;
            buf[0] = res;
            if (res != 0x0A) { rx_len = 0; return false; }
        } else {
            for (int i = 0; i < n; ++i) buf[i] ^= mf_crypto_.step8(0);
        }
    }
    if (include_crc) {
        if (n < 2) return false;
        uint16_t crc = crc_a(buf, n - 2);
        if ((crc & 0xFF) != buf[n - 2] || (crc >> 8) != buf[n - 1]) return false;
        n -= 2;
    }
    if (n > rx_len) n = rx_len;
    memcpy(rx, buf, n);
    rx_len = n;
    return true;
}

bool St25r3916::mifareAuth(bool key_a, const uint8_t uid4[4], uint8_t block, const uint8_t key[6]) {
    // 1) AUTH (0x60/0x61 + bloque) con CRC estándar; la tarjeta responde Nt (4 bytes, sin CRC).
    prepTx(*this);
    setAntcl(*this, false);
    writeReg(REG_ISO14443A, 0x00);    // paridad estándar
    setNoCrcRx(*this, true);          // el nonce no lleva CRC
    clearIrq();
    cmd(CMD_CLEAR_FIFO);
    uint8_t frame[2] = {uint8_t(key_a ? 0x60 : 0x61), block};
    fifoLoad(frame, 2);
    writeReg16(REG_NUM_TX_BYTES_1, uint16_t(2 << 3));
    cmd(CMD_TRANSMIT_WITH_CRC);

    uint8_t err = 0, rb[8];
    int n = receiveFrame(*this, rb, sizeof(rb), 20, err);
    const bool dbg = getenv("MFDBG") != nullptr;
    if (dbg) fprintf(stderr, "    [auth blk %u] AUTH1 n=%d err=%02X Nt=%02X%02X%02X%02X\n",
                     block, n, err, rb[0], rb[1], rb[2], rb[3]);
    if (n < 4) return false;
    uint32_t Nt = array_to32(rb);

    usleep(90);  // esperar antes de responder (el protocolo pide >= 86,4 us tras recibir Nt)

    // 2) Responder {Nr}{Ar} cifrado con paridad manual; la tarjeta responde {At} (4 bytes).
    const uint32_t u32 = array_to32(uid4);
    const uint64_t key48 = key_to64(key);
    uint32_t Nr = 0x4F6D3A1Bu;         // nonce del lector: fijo nos vale (el reto es Nt)
    uint32_t Ar = 0, suc3 = 0;

    mf_crypto_.init(key48);
    (void)mf_crypto_.inject(u32, Nt, false);
    mfc::suc_23(mfc::byteswap32(Nt), Ar, suc3);

    uint8_t AB[8];
    uint32_t parity = mf_crypto_.encrypt(AB, Nr, Ar);
    uint8_t bitstream[9];
    append_parity(bitstream, 9, AB, 8, parity);

    cmd(CMD_RESET_RX_GAIN);
    writeReg16(REG_NRT_1, 0x0350);
    writeReg(REG_ISO14443A, ISO_NO_TX_PAR);
    setNoCrcRx(*this, true);
    clearIrq();
    cmd(CMD_CLEAR_FIFO);
    fifoLoad(bitstream, 9);
    writeReg16(REG_NUM_TX_BYTES_1, uint16_t(9 << 3));
    cmd(CMD_TRANSMIT_WITHOUT_CRC);

    uint8_t ba[8];
    n = receiveFrame(*this, ba, sizeof(ba), 20, err);
    if (dbg) fprintf(stderr, "    [auth blk %u] AUTH2 n=%d err=%02X Nt'=%08X suc3=%08X Nr=%08X Ar=%08X\n",
                     block, n, err, Nt, suc3, Nr, Ar);
    if (n < 4) return false;

    uint32_t at = 0;
    for (int i = 0; i < 4; ++i) at |= uint32_t(ba[i] ^ mf_crypto_.step8(0)) << (8 * i);
    if (dbg) fprintf(stderr, "    [auth blk %u] At=%08X %s\n", block, at, at == suc3 ? "OK" : "MISMATCH");
    return at == suc3;    // 3er paso correcto = autenticados
}

bool St25r3916::mifareReadBlock(uint8_t block, uint8_t out[16]) {
    uint8_t cmd_frame[2] = {0x30, block};
    int rx_len = 16;
    return mfTransceiveEncrypted(out, rx_len, cmd_frame, 2, 20, true, true) && rx_len == 16;
}

}  // namespace st25

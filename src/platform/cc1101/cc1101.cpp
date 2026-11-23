/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Driver del CC1101 sobre spidev. Ver cc1101.h.
 */
#include "cc1101.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <thread>

namespace cc {
namespace {
void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
} // namespace

bool Cc1101::open(const std::string& dev, uint32_t speed_hz) {
    speed_ = speed_hz;
    fd_ = ::open(dev.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) return false;
    uint8_t mode = SPI_MODE_0;   // el CC1101 habla SPI modo 0 (CPOL0/CPHA0)
    uint8_t bits = 8;
    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 || ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

void Cc1101::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

Cc1101::~Cc1101() {
    close();
}

void Cc1101::xfer(uint8_t* buf, size_t n) {
    spi_ioc_transfer tr{};
    tr.tx_buf = reinterpret_cast<unsigned long>(buf);
    tr.rx_buf = reinterpret_cast<unsigned long>(buf);
    tr.len = n;
    tr.speed_hz = speed_;
    tr.bits_per_word = 8;
    ioctl(fd_, SPI_IOC_MESSAGE(1), &tr);
}

uint8_t Cc1101::strobe(uint8_t cmd) {
    uint8_t b[1] = {cmd};
    xfer(b, 1);
    return b[0]; // byte de estado
}

void Cc1101::writeReg(uint8_t addr, uint8_t value) {
    uint8_t b[2] = {addr, value};
    xfer(b, 2);
}

uint8_t Cc1101::readReg(uint8_t addr) {
    uint8_t b[2] = {static_cast<uint8_t>(addr | CC1101_READ), 0};
    xfer(b, 2);
    return b[1];
}

uint8_t Cc1101::readStatus(uint8_t addr) {
    // Los registros de estado (0x30-0x3D) necesitan el bit BURST para no confundirse con strobes.
    uint8_t b[2] = {static_cast<uint8_t>(addr | CC1101_READ | CC1101_BURST), 0};
    xfer(b, 2);
    return b[1];
}

void Cc1101::writeBurst(uint8_t addr, const uint8_t* data, size_t n) {
    uint8_t b[66];
    b[0] = static_cast<uint8_t>(addr | CC1101_BURST);
    for (size_t i = 0; i < n && i < sizeof(b) - 1; ++i) b[1 + i] = data[i];
    xfer(b, n + 1);
}

void Cc1101::readBurst(uint8_t addr, uint8_t* data, size_t n) {
    uint8_t b[66];
    b[0] = static_cast<uint8_t>(addr | CC1101_READ | CC1101_BURST);
    for (size_t i = 0; i < n; ++i) b[1 + i] = 0;
    xfer(b, n + 1);
    for (size_t i = 0; i < n; ++i) data[i] = b[1 + i];
}

bool Cc1101::reset() {
    strobe(CC1101_STROBE_SRES);
    // Tras SRES el chip tarda en estar listo; damos margen.
    for (int i = 0; i < 20; ++i) {
        sleep_ms(1);
        if ((strobe(CC1101_STROBE_SNOP) & 0x80) == 0) return true; // CHIP_RDYn = 0
    }
    return false;
}

uint8_t Cc1101::partNumber() {
    return readStatus(CC1101_STATUS_PARTNUM);
}

uint8_t Cc1101::version() {
    return readStatus(CC1101_STATUS_VERSION);
}

bool Cc1101::present() {
    // El CC1101 tiene part number 0x00 y version 0x04/0x14/0x17 según la tirada.
    uint8_t ver = version();
    return partNumber() == 0x00 && ver != 0x00 && ver != 0xFF;
}

uint32_t Cc1101::setFrequency(uint32_t hz) {
    // FREQ = f * 2^16 / fXOSC, con fXOSC = 26 MHz.
    uint64_t word = (static_cast<uint64_t>(hz) * CC1101_FDIV) / CC1101_QUARTZ;
    writeReg(CC1101_FREQ2, (word >> 16) & 0xFF);
    writeReg(CC1101_FREQ1, (word >> 8) & 0xFF);
    writeReg(CC1101_FREQ0, word & 0xFF);
    return static_cast<uint32_t>((word * CC1101_QUARTZ) / CC1101_FDIV);
}

void Cc1101::setPaTable(const uint8_t table[8]) {
    writeBurst(CC1101_PATABLE, table, 8);
}

void Cc1101::writeConfig(const uint8_t (*regs)[2], size_t n) {
    for (size_t i = 0; i < n; ++i) writeReg(regs[i][0], regs[i][1]);
}

namespace {
// Preset OOK/ASK asíncrono de 650 kHz de ancho de banda del firmware del Flipper Zero (GPLv3):
// sin preámbulo/sync, paquete infinito en modo serie asíncrono. GDO0 (IOCFG0) se fija aparte.
const uint8_t kOok650[][2] = {
    {CC1101_FIFOTHR, 0x47},   {CC1101_PKTCTRL0, 0x32},  {CC1101_FSCTRL1, 0x06},
    {CC1101_MDMCFG0, 0x00},   {CC1101_MDMCFG1, 0x00},   {CC1101_MDMCFG2, 0x30},
    {CC1101_MDMCFG3, 0x32},   {CC1101_MDMCFG4, 0x67},   {CC1101_DEVIATN, 0x00},
    {CC1101_MCSM0, 0x18},     {CC1101_FOCCFG, 0x18},    {CC1101_AGCCTRL0, 0x91},
    {CC1101_AGCCTRL1, 0x00},  {CC1101_AGCCTRL2, 0x07},  {CC1101_WORCTRL, 0xFB},
    {CC1101_FREND0, 0x11},    {CC1101_FREND1, 0x56},    {CC1101_FSCAL3, 0xE9},
    {CC1101_FSCAL2, 0x2A},    {CC1101_FSCAL1, 0x00},    {CC1101_FSCAL0, 0x1F},
    {CC1101_TEST2, 0x81},     {CC1101_TEST1, 0x35},     {CC1101_TEST0, 0x09},
};
// Tabla de potencia para OOK: [0]=0 (apagado = el "0" de OOK), [1]=0xC0 (máxima = el "1").
const uint8_t kPaOok[8] = {0x00, 0xC0, 0, 0, 0, 0, 0, 0};
} // namespace

void Cc1101::configureOokRx(uint32_t freq_hz) {
    strobeIdle();
    reset();
    writeConfig(kOok650, sizeof(kOok650) / sizeof(kOok650[0]));
    setFrequency(freq_hz);
    setPaTable(kPaOok);
    writeReg(CC1101_IOCFG0, CC1101IocfgSerialDataOutput);  // 0x0D: GDO0 = datos demodulados
    flushRx();
    strobeRx();
}

void Cc1101::configureOokTx(uint32_t freq_hz) {
    strobeIdle();
    reset();
    writeConfig(kOok650, sizeof(kOok650) / sizeof(kOok650[0]));
    setFrequency(freq_hz);
    setPaTable(kPaOok);
    // GDO0 en alta impedancia: en modo serie asíncrono el CC1101 toma el dato de TX de ese pin,
    // y lo conduce el host (GPIO15 como salida), sin que el chip lo pise. El caller: baja el dato,
    // hace strobeTx() y luego marca los tiempos.
    writeReg(CC1101_IOCFG0, CC1101IocfgHighImpedance);
    flushTx();
    strobeIdle();
}

void Cc1101::strobeIdle() { strobe(CC1101_STROBE_SIDLE); }
void Cc1101::strobeRx() { strobe(CC1101_STROBE_SRX); }
void Cc1101::strobeTx() { strobe(CC1101_STROBE_STX); }
void Cc1101::flushRx() { strobe(CC1101_STROBE_SFRX); }
void Cc1101::flushTx() { strobe(CC1101_STROBE_SFTX); }

uint8_t Cc1101::marcState() {
    return readStatus(CC1101_STATUS_MARCSTATE) & 0x1F;
}

} // namespace cc

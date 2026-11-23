// Driver propio y compacto del ST25R3916 (NFC del Cap CC1101+NFC del Cardputer Zero)
// sobre spidev de Linux. El mapa de registros/comandos viene de
// reference/ST25R3916_definition.hpp (M5Unit-NFC, MIT); la secuencia de init está
// calcada de ese proyecto. Aquí sólo lo que necesita Linux: transporte + bring-up.
#pragma once
#include "crypto1.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace st25 {

// --- Registros Space-A ---
enum : uint8_t {
    REG_IO_CONF_1 = 0x00, REG_IO_CONF_2 = 0x01, REG_OP_CONTROL = 0x02,
    REG_MODE_DEF = 0x03, REG_BITRATE_DEF = 0x04, REG_ISO14443A = 0x05,
    REG_NFCIP1_PT_DEF = 0x08, REG_NRT_1 = 0x10, REG_TIMER_EMV = 0x12,
    REG_AUX_DEF = 0x0A, REG_RX_CONF_1 = 0x0B, REG_RX_CONF_2 = 0x0C,
    REG_RX_CONF_3 = 0x0D, REG_RX_CONF_4 = 0x0E,
    REG_MASK_MAIN_IRQ = 0x16, REG_MAIN_IRQ = 0x1A,
    REG_FIFO_STATUS_1 = 0x1E, REG_FIFO_STATUS_2 = 0x1F, REG_COLLISION = 0x20,
    REG_NUM_TX_BYTES_1 = 0x22, REG_NUM_TX_BYTES_2 = 0x23,
    REG_AD_OUTPUT = 0x25,
    REG_ANT_TUNE_1 = 0x26, REG_ANT_TUNE_2 = 0x27, REG_TX_DRIVER = 0x28,
    REG_PT_MOD = 0x29, REG_EFD_ACT_THR = 0x2A, REG_EFD_DEACT_THR = 0x2B,
    REG_AUX_DISPLAY = 0x31, REG_IC_IDENTITY = 0x3F,
};
// --- Registros Space-B (se acceden con prefijo 0xFB) ---
enum : uint8_t {
    REGB_EMD_SUPPRESSION = 0x05, REGB_CORRELATOR_1 = 0x0C, REGB_CORRELATOR_2 = 0x0D,
    REGB_FIELD_ON_GUARD = 0x15,
    REGB_RESISTIVE_AM = 0x2A, REGB_REGULATOR_DISPLAY = 0x2C,
    REGB_OVERSHOOT_1 = 0x30, REGB_OVERSHOOT_2 = 0x31,
    REGB_UNDERSHOOT_1 = 0x32, REGB_UNDERSHOOT_2 = 0x33,
};
// --- Comandos directos ---
enum : uint8_t {
    CMD_SET_DEFAULT = 0xC1, CMD_STOP_ALL = 0xC2,
    CMD_TRANSMIT_WITH_CRC = 0xC4, CMD_TRANSMIT_WITHOUT_CRC = 0xC5,
    CMD_TRANSMIT_REQA = 0xC6, CMD_TRANSMIT_WUPA = 0xC7,
    CMD_NFC_INITIAL_FIELD_ON = 0xC8, CMD_GO_TO_SENSE = 0xCD, CMD_GO_TO_SLEEP = 0xCE,
    CMD_UNMASK_RECEIVE_DATA = 0xD1,
    CMD_MEASURE_AMPLITUDE = 0xD3, CMD_RESET_RX_GAIN = 0xD5, CMD_ADJUST_REGULATORS = 0xD6,
    CMD_CLEAR_FIFO = 0xDB, CMD_SPACEB_ACCESS = 0xFB, CMD_TEST_ACCESS = 0xFC,
};
enum : uint8_t { OP_LOAD_PT_MEMORY_A = 0xA0 };  // cargar config de passive target NFC-A
// --- Bits ---
enum : uint8_t {
    OP_EN = 0x80, OP_RX_EN = 0x40, OP_TX_EN = 0x08, OP_WU = 0x04,
    OP_EFD_AUTO = 0x03, OP_EFD_CA = 0x01,
    AUX_TX_ON = 0x20, AUX_OSC_OK = 0x10, AUX_RX_ON = 0x08,
    IO2_SUP3V = 0x80, IO2_AAT_EN = 0x20, IO2_MISO_PD1 = 0x08, IO2_MISO_PD2 = 0x10, IO2_DRV_LVL = 0x04,
};
// --- SPI operation modes ---
enum : uint8_t { OP_WRITE_REG = 0x00, OP_READ_REG = 0x40, OP_LOAD_FIFO = 0x80, OP_READ_FIFO = 0x9F };

// IRQ de 32 bits: MSB main(0x1A) | nfc(0x1B) | error(0x1C) | pta(0x1D) LSB
constexpr uint32_t IRQ_TXE  = 0x08u << 24;  // fin de transmisión
constexpr uint32_t IRQ_RXS  = 0x20u << 24;  // inicio de recepción
constexpr uint32_t IRQ_RXE  = 0x10u << 24;  // fin de recepción
constexpr uint32_t IRQ_COL  = 0x04u << 24;  // colisión
constexpr uint32_t IRQ_NRE  = 0x40u << 16;  // no-response timer expiró
constexpr uint32_t IRQ_CAT  = 0x02u << 16;  // guard time de field-on cumplido
constexpr uint32_t IRQ_CAC  = 0x04u << 16;  // colisión en evitación RF (campo externo)
constexpr uint32_t IRQ_EOF  = 0x08u << 16;  // caída del campo externo (en emulación)
constexpr uint32_t IRQ_WU_A = 0x01u;        // (reg pasivo target) seleccionados como target
constexpr uint32_t IRQ_ERR_MASK = 0x0000FF00u;  // cualquier error (crc/par/err1/err2)

struct InitStatus {
    bool ok = false;
    uint8_t ic_type = 0, ic_rev = 0;
    bool osc_ok = false, tx_on = false;
    uint8_t regulator_raw = 0;
    float regulator_volts = 0.f;
    uint8_t op_control_raw = 0, aux_display_raw = 0;
    uint8_t rf_amplitude = 0;  // amplitud de RF medida (prueba de campo sin tarjeta)
    std::string error;
};

// Tarjeta NFC-A detectada
struct PiccA {
    uint8_t atqa[2] = {0, 0};
    uint8_t uid[10] = {0};
    uint8_t uid_len = 0;   // 4, 7 o 10
    uint8_t sak = 0;
};

class St25r3916 {
public:
    // Abre el spidev (p.ej. "/dev/spidev0.2"). vdd5=true si el chip va a 5V.
    bool open(const std::string& dev, bool vdd5 = false, uint32_t speed_hz = 5'000'000);
    void close();
    ~St25r3916();

    // --- transporte ---
    uint8_t  readReg(uint8_t reg);
    void     writeReg(uint8_t reg, uint8_t val);
    void     writeReg16(uint8_t reg, uint16_t val);   // reg y reg+1 (MSB->reg)
    void     writeReg32(uint8_t reg, uint32_t val);   // 4 regs consecutivos (MSB->reg)
    uint8_t  readRegB(uint8_t reg);                   // Space-B
    void     writeRegB(uint8_t reg, uint8_t val);     // Space-B
    void     modifyReg(uint8_t reg, uint8_t set_bits, uint8_t clr_bits);
    void     cmd(uint8_t c);
    void     cmdData(uint8_t c, const uint8_t* data, size_t n);
    void     fifoLoad(const uint8_t* data, size_t n);
    std::vector<uint8_t> fifoRead(size_t n);
    uint8_t  fifoCount();
    uint32_t readIrq();        // lee y limpia las 4 regs de interrupción
    void     clearIrq();

    // Enciende oscilador+reguladores y deja el chip en modo lector NFC-A con campo.
    InitStatus init();

    void     fieldOn();   // enciende el campo RF (flujo con guard timer, como la oficial)
    void     fieldOff();  // apaga el campo RF
    uint8_t  measureAmplitude();                        // amplitud RF (prueba de campo)
    uint32_t waitIrq(uint32_t mask, int timeout_ms);    // acumula IRQ hasta mask o timeout

    // --- NFC-A ---
    struct TxRx { bool ok = false; uint32_t irq = 0; std::vector<uint8_t> data; uint8_t err_reg = 0; };
    // Transmite tx (nbits bits; si nbits%8!=0 es frame corto/partial) y recibe respuesta.
    TxRx transceive(const uint8_t* tx, uint32_t nbits, bool with_crc, int timeout_ms);
    bool reqa(uint8_t atqa[2]);       // REQA -> ATQA (true si hay tarjeta en el campo)
    bool wupa(uint8_t atqa[2]);       // WUPA (despierta también las dormidas)
    // Anticollision + SELECT completo; rellena picc (UID, SAK). true si selecciona.
    bool activateA(PiccA& picc);

    // --- Type 2 (NTAG / Ultralight) ---
    // READ (0x30): devuelve 16 bytes = 4 páginas desde 'page'. La tarjeta debe estar activada.
    bool type2Read(uint8_t page, uint8_t out[16]);
    // WRITE (0xA2): escribe 4 bytes en 'page'. true si la tarjeta responde ACK.
    bool type2Write(uint8_t page, const uint8_t data[4]);
    // GET_VERSION (0x60): 8 bytes de versión (NTAG / Ultralight EV1). La tarjeta debe estar activada.
    bool type2GetVersion(uint8_t* ver, int& len);
    // RATS (0xE0 0x80): pide el ATS a una tarjeta ISO14443-4 (DESFire, etc.). Debe estar activada.
    bool rats(uint8_t* ats, int& len);

    // --- MIFARE Classic (Crypto1 por software sobre el ST25R3916) ---
    // Autenticación de 3 pasos de un sector. key_a=true usa AUTH con clave A (0x60), si no B (0x61).
    // La tarjeta tiene que estar seleccionada (activateA) y `uid4` son sus 4 bytes de UID
    // (los últimos 4 si es cascada). Deja el cifrado activo para leer/escribir ese sector.
    bool mifareAuth(bool key_a, const uint8_t uid4[4], uint8_t block, const uint8_t key[6]);
    // READ (0x30) cifrado de un bloque de 16 bytes. Requiere mifareAuth antes, del mismo sector.
    bool mifareReadBlock(uint8_t block, uint8_t out[16]);

    // --- NFC-B (ISO14443-B) ---
    void configureNfcB();                        // reconfigura el chip para NFC-B y enciende campo
    bool nfcbDetect(uint8_t* atqb, int& len);    // REQB -> ATQB (true si una tarjeta NFC-B responde)

    // --- NFC-V (ISO15693) ---
    void configureNfcV();                        // reconfigura el chip para NFC-V (modo stream) y enciende campo
    bool nfcvInventory(uint8_t* raw, int& len);  // Inventory; true si responde algo (bytes crudos del stream)

    // --- HID iCLASS / Picopass (sobre capa física ISO15693) ---
    // ACTALL + IDENTIFY; devuelve el CSN de anticolisión (raw) si una iCLASS responde.
    // Requiere haber llamado antes a configureNfcV(). true si hay respuesta.
    bool iclassIdentify(uint8_t* raw, int& len);

    // --- NFC-F (FeliCa) ---
    void configureNfcF();                        // reconfigura el chip para NFC-F (212kbps) y enciende campo
    bool nfcfPoll(uint8_t* res, int& len);       // SENSF_REQ; true si una tarjeta FeliCa responde

    // --- Emulación NFC-A (modo target) ---
    struct EmuStats { int selections = 0; int reads = 0; int other = 0; int fields = 0; };
    void writePtMemoryA(const uint8_t* d, int n);   // carga config de passive target (UID/ATQA/SAK)
    // Emula una tarjeta NFC-A Type 2: UID/ATQA/SAK de 'p', memoria de páginas en mem (nbytes).
    // Corre 'secs' segundos sirviendo a un lector. El chip resuelve el anticollision/SELECT solo.
    // Si `stop` no es nulo, se sale en cuanto pase a true (la app lo usa para «parar»; secs<=0 = sin límite).
    bool runEmulationA(const PiccA& p, const uint8_t* mem, int nbytes, int secs, EmuStats& stats, std::string& err,
                       const std::atomic<bool>* stop = nullptr);

    bool vdd5() const { return vdd5_; }
private:
    int fd_ = -1;
    bool vdd5_ = false;
    uint32_t speed_ = 1'000'000;
    void xfer(uint8_t* buf, size_t n);
    bool enableOsc(std::string& err);
    bool baseBringUp(InitStatus& st);
    bool anticollisionLevel(uint8_t sel_cmd, uint8_t* uid_out, uint8_t& sak_out, bool& cascade);

    // MIFARE Classic: envía una trama cifrada con paridad manual, y recibe/descifra la respuesta.
    bool mfSendEncrypted(const uint8_t* tx, int tx_len);
    bool mfTransceiveEncrypted(uint8_t* rx, int& rx_len, const uint8_t* tx, int tx_len,
                               int timeout_ms, bool include_crc, bool decrypt);
    mfc::Crypto1 mf_crypto_{0};  // estado del cifrado del sector autenticado
};

}  // namespace st25

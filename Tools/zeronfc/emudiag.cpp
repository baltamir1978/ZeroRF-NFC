// Diagnóstico v2 del modo tarjeta: carga una NTAG de prueba y mira si el chip responde solo al
// anticolisión de un lector externo. Imprime los eventos del chip. Se compila en el aparato.
// SPDX-License-Identifier: MIT
#include "st25r3916.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>
using namespace st25;
static const char* kSpiDev = "/dev/spidev0.2";
static int g_chip=-1,g_line=-1;
static void pon(){g_chip=open("/dev/gpiochip0",O_RDWR|O_CLOEXEC);if(g_chip<0)return;gpio_v2_line_request r{};r.offsets[0]=26;r.num_lines=1;r.config.flags=GPIO_V2_LINE_FLAG_OUTPUT;r.config.num_attrs=1;r.config.attrs[0].attr.id=GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;r.config.attrs[0].attr.values=1;r.config.attrs[0].mask=1;if(ioctl(g_chip,GPIO_V2_GET_LINE_IOCTL,&r)>=0)g_line=r.fd;}
static void cap(bool on){if(on)pon();std::ofstream f("/sys/class/leds/ext_5v_out/brightness");if(f)f<<(on?"1":"0");if(!on&&g_line>=0){close(g_line);g_line=-1;}if(!on&&g_chip>=0){close(g_chip);g_chip=-1;}}
static bool spidev(){{std::ifstream f(kSpiDev);if(f.good())return true;}if(system("dtoverlay spi0-spidev2-gpio22-overlay")!=0)return false;std::this_thread::sleep_for(std::chrono::milliseconds(300));std::ifstream f(kSpiDev);return f.good();}

int main(int argc,char**argv){
    int secs=(argc>1)?atoi(argv[1]):25;
    uint8_t mode=(argc>2)?(uint8_t)strtol(argv[2],nullptr,16):0x88; // 0x88 directo, 0xC8 detección
    cap(true); std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if(!spidev()){fprintf(stderr,"no spidev\n");cap(false);return 2;}
    St25r3916 nfc;
    if(!nfc.open(kSpiDev)){fprintf(stderr,"no spi\n");cap(false);return 2;}
    InitStatus st=nfc.init();
    if(!st.ok){printf("init: %s\n",st.error.c_str());cap(false);return 1;}
    printf("Chip OK. MODE_DEF=%02X, umbral EFD bajo, cargo NTAG de prueba.\n",mode);

    // --- Config de modo target NFC-A con tarjeta cargada ---
    nfc.fieldOff();
    nfc.cmd(CMD_STOP_ALL);
    nfc.writeReg(REG_MODE_DEF,mode);
    nfc.writeReg(REG_NFCIP1_PT_DEF,0x5C);
    nfc.writeReg(REG_TIMER_EMV,0x08);
    // PT memory A: UID(7) de prueba + ATQA 0044 + SAK 00 (NTAG)
    uint8_t uid[7]={0x04,0xDE,0xAD,0x62,0xD4,0x2A,0x81};
    uint8_t wbuf[15]={0};
    memcpy(wbuf,uid,7);
    wbuf[10]=0x44; wbuf[11]=0x00;            // ATQA
    wbuf[12]=0x04; wbuf[13]=0x00; wbuf[14]=0x00; // SAK (cascada por ser 7B)
    nfc.modifyReg(REG_AUX_DEF,0x10,0x30);    // uid_7
    nfc.cmdData(0xA0,wbuf,15);               // LOAD_PT_MEMORY_A
    // start
    nfc.modifyReg(REG_NFCIP1_PT_DEF,0x0C,0x01);
    nfc.modifyReg(REG_ISO14443A,0x00,0x80|0x40|0x20);
    nfc.writeReg(REG_EFD_ACT_THR,0x02);      // <-- umbral bajo: ver el campo del móvil
    nfc.writeReg(REG_EFD_DEACT_THR,0x01);
    nfc.modifyReg(REG_OP_CONTROL,OP_EN|OP_EFD_AUTO,0x00);
    nfc.writeReg32(REG_MASK_MAIN_IRQ,0x00000000u);
    nfc.cmd(CMD_UNMASK_RECEIVE_DATA);
    nfc.clearIrq();
    nfc.cmd(CMD_GO_TO_SENSE);

    printf("Acerca el móvil. %d s...\n",secs); fflush(stdout);
    bool efd=false,eon=false,nfct=false,wua=false,rxe=false;
    auto t0=std::chrono::steady_clock::now();
    while(std::chrono::steady_clock::now()-t0<std::chrono::seconds(secs)){
        uint32_t irq=nfc.readIrq();
        uint8_t aux=nfc.readReg(REG_AUX_DISPLAY);
        if(aux&0x40)efd=true;
        if(irq&(0x10u<<16))eon=true;
        if(irq&(0x01u<<16))nfct=true;      // I_nfct: bitrate reconocido
        if(irq&0x01u)wua=true;             // I_wu_a: seleccionado
        if(irq&0x02u)wua=true;             // I_wu_ax
        if(irq&(0x10u<<24))rxe=true;       // I_rxe
        if(irq & ~((0x10u<<16)|(0x08u<<16))){ // algo más que el parpadeo eon/eof
            uint8_t fs=nfc.readReg(REG_FIFO_STATUS_1);
            printf("  IRQ=%08X AUX=%02X fifo=%u%s%s%s%s\n",irq,aux,fs,
                (irq&(0x01u<<16))?" I_nfct":"",(irq&0x01u)?" I_wu_a":"",
                (irq&0x02u)?" I_wu_ax":"",(irq&(0x10u<<24))?" I_rxe":"");
            fflush(stdout);
        }
        if(irq&(0x08u<<16))nfc.cmd(CMD_GO_TO_SENSE); // I_eof: volver a sense
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    printf("\nResumen: efd=%s eon=%s nfct(bitrate)=%s wu_a(sel)=%s rxe(cmd)=%s\n",
        efd?"SÍ":"no",eon?"SÍ":"no",nfct?"SÍ":"no",wua?"SÍ":"no",rxe?"SÍ":"no");
    nfc.fieldOff(); nfc.close(); cap(false);
    return 0;
}

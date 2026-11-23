# ZeroRF-NFC

Sub-GHz (CC1101) y NFC (ST25R3916) para el **M5Stack Cardputer Zero** con el Cap CC1101+NFC,
sobre Debian/Linux. Repositorio de la app `RF / NFC` del lanzador.

> Estado (23-nov-2026): en desarrollo. Hay un **núcleo NFC funcionando como herramienta de
> línea de comandos** (`zeronfc`); la interfaz LVGL y el empaquetado `.deb` están pendientes.

## NFC — qué hace ya

Driver propio del **ST25R3916** por SPI (`/dev/spidev0.2`), escrito desde cero. La capa de
registros/arranque se apoya en [M5Unit-NFC](https://github.com/m5stack/M5Unit-NFC) (MIT) como
referencia (ver `reference/`).

- **Lectura NFC-A** (ISO14443-A) fiable: UID (4/7 bytes), ATQA, SAK, tipo.
- **Volcado Type 2** (NTAG / MIFARE Ultralight): todas las páginas.
- **Diagnóstico**: `scan` (prueba NFC-A/B/V/F) y `sense` (mide el acoplamiento a 13,56 MHz para
  distinguir una tarjeta HF de una de 125 kHz).

NFC-B / NFC-V / NFC-F están sólo a nivel de **detección** y **sin validar** con tarjetas de esos
tipos. Las tarjetas **propietarias de 13,56 MHz** (HID iCLASS/Picopass, LEGIC) y las de **125 kHz**
(EM4100/HID Prox) **no** se pueden leer con este hardware/estas rutas.

## Compilar y probar (núcleo CLI)

En el propio Cardputer (Debian arm64), o copiando `src/` al aparato:

```sh
g++ -std=c++17 -O2 -o zeronfc src/main.cpp src/st25r3916.cpp
```

El Cap necesita alimentación antes de usarse. El binario enciende `ext_5v_out` y carga el overlay
SPI, **pero el POWER_EN del Cap (GPIO26) hay que mantenerlo aparte** mientras corre (pendiente
integrarlo en el binario):

```sh
sudo gpioset -c gpiochip0 26=1 &      # mantener en segundo plano
sudo ./zeronfc detect                 # leer UID de una tarjeta NFC-A
sudo ./zeronfc read                   # volcar las páginas (Type 2)
sudo ./zeronfc scan                   # identificar la tecnología
sudo ./zeronfc sense                  # ¿acopla a 13,56 MHz?
```

## Pendiente

Escribir y clonar Type 2, guardar/cargar volcados, MIFARE Classic (Crypto1), emulación de
tarjeta (modo target), integrar el POWER_EN en el binario, y la interfaz LVGL + empaquetado `.deb`.

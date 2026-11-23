# ZeroRF-NFC

Sub-GHz (CC1101) y NFC (ST25R3916) para el **M5Stack Cardputer Zero** con el Cap CC1101+NFC,
sobre Debian/Linux. Repositorio de la app `RF / NFC` del lanzador.

> Estado (23-nov-2026): **app del lanzador funcionando** (`RF / NFC`, interfaz LVGL + `.deb`),
> con menú Leer / Identificar / Sub-GHz. El núcleo NFC también está como herramienta de línea
> de comandos (`src/main.cpp`) para desarrollo.

## La app del lanzador (UI + .deb)

Interfaz LVGL sobre el framebuffer (320×170) con el teclado del Cardputer. Reutiliza el
esqueleto de compilación de las otras apps Zero, pero en versión "lean": sin freetype/png,
toolchain cruzado estándar (sin BSP de M5), así que el `.deb` sólo depende de libc/libstdc++.

```sh
Tools/compilar.sh          # compila en Docker (aarch64) y deja dist/ZeroRF-NFC_*.deb
scp dist/ZeroRF-NFC_*.deb cpzero:/tmp/
ssh cpzero 'sudo apt-get install -y /tmp/ZeroRF-NFC_*.deb'   # queda como "RF / NFC" en el lanzador
```

El `.desktop` la abre con `sudo -n` (el NFC necesita root para SPI/GPIO/overlay); el paquete
instala un `sudoers.d` que lo permite al grupo `sudo` sin contraseña.

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

El binario es autónomo: enciende la alimentación del Cap (POWER_EN en GPIO26 vía ioctl al
chardev, y `ext_5v_out`) y carga el overlay SPI él mismo. Necesita `sudo` (acceso a
`/dev/gpiochip0`, `/dev/spidev0.2` y `/sys/class/leds`).

```sh
sudo ./zeronfc detect                 # leer UID de una tarjeta NFC-A
sudo ./zeronfc read                   # volcar las páginas (Type 2)
sudo ./zeronfc write 4 DEADBEEF       # escribir una página de usuario
sudo ./zeronfc save tarjeta.dump      # guardar el volcado
sudo ./zeronfc load tarjeta.dump      # cargarlo en otra tarjeta (clonar memoria)
sudo ./zeronfc scan                   # identificar la tecnología (A/B/V/F)
sudo ./zeronfc sense                  # ¿acopla a 13,56 MHz?
```

## Pendiente

MIFARE Classic (Crypto1), emulación de tarjeta (modo target), y la interfaz LVGL + empaquetado `.deb`.

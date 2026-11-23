# ZeroRF-NFC

App del **Cap CC1101+NFC** para el **M5Stack Cardputer Zero** (Raspberry Pi CM0,
pantalla de 320×170, Debian 13 arm64). Un menú con dos herramientas:

- **Sub-GHz (433 MHz)**: grabar, guardar y reemitir mandos (el del garaje), con el TI **CC1101**.
- **NFC (13.56 MHz)**: leer, escribir y emular tarjetas, con el **ST25R3916**.

En el lanzador se llama **RF / NFC**. Qué hace y cómo se usa está en `README.md`; aquí va lo que
hace falta para tocarla.

Es la app hermana de la del **Cap LoRa+GPS** (tracks GPX), que va en otro repositorio: los dos
caps comparten ranura y **no se pueden poner a la vez**, por eso hay una app por cap.

No es una app de Android: de `~/CLAUDE.md` valen el estilo (comentarios, documentación y
commits en español explicando el porqué; pocas dependencias; lógica pura con pruebas), que el
repositorio es **público** (ni secretos ni datos personales) y la entrega; lo de Gradle, firma
y GrapheneOS no aplica.

## Compilar y comprobar

`Tools/compilar.sh` usa Docker si lo hay y, si no, las herramientas de la máquina (las del
`Tools/Dockerfile`; en el portátil están instaladas con apt).

```sh
Tools/compilar.sh pruebas   # simulador (SDL2) + ctest; tiene que pasar
Tools/compilar.sh deb       # dist/ZeroRF-NFC_<versión>_rev1_arm64.deb
Tools/compilar.sh simulador # abre el simulador en este escritorio (X11)
```

- La primera vez descarga LVGL 9.5 y el *sysroot* de M5Stack a `.cache/` (no se versiona). Si
  `../ZeroGallery/.cache` o `../ZeroTVBGone/.cache` existe, copiarlo (o enlazarlo) ahorra la
  descarga: es lo mismo.
- GitHub Actions hace lo mismo en cada push (`.github/workflows/build.yml`).
- **Probar sin pantalla**: el simulador corre bajo `Xvfb` (`SDL_RENDER_DRIVER=software`), se le
  mandan teclas con `xdotool key` y se captura con `import -window root`; la pantalla del
  aparato queda en `320x170+540+268` de esa captura. En el portátil, sin Docker:
  `xvfb-run -a --server-args="-screen 0 900x600x24" build/linux-x86-64/Release/zerorfnfc`.
- **OJO**: en el simulador la configuración es `config/zerorfnfc.conf` del repositorio, y
  cambiar de tema la reescribe. Dejarla en `dark_mode=yes` antes de hacer commit.
- **El aparato** es `cpzero` en Tailscale (`ssh cpzero`, usuario `bruno`, `sudo` sin contraseña):
  el `.deb` se copia y se instala con `sudo apt-get install -y`. Suele estar apagado; Bruno avisa
  cuando lo enciende. Lo que haya que mirar en la pantalla lo confirma Bruno: hasta entonces se
  dice («sin probar en el aparato») en el «Estado».

## De dónde viene

- **La base** es `../ZeroTVBGone` (a su vez la plantilla oficial `CardputerZero/Template`, commit
  `477fa6d`): CMake, simulador de escritorio (`src/app/desktop_*`), registro, *bindings*
  reactivos, barra de título, aviso flotante y ayuda se conservan tal cual. Un arreglo en esas
  piezas comunes conviene llevarlo a las tres apps del aparato. Se quitó todo lo de IR/TV-B-Gone
  (`ir_blaster`, `model/tvbgone*`) y el viewmodel pasó de «una pantalla» a «menú + una pantalla
  por herramienta» (`model::AppPage` = Home / SubGhz / Nfc).
- **Sub-GHz**: se reaprovecharán las configuraciones de registro del CC1101 de **Bruce** y los
  decodificadores de protocolo fijo del **Flipper Zero** (Princeton/PT2262, CAME, NICE…). El
  CC1101 es el mismo chip que usan los dos.
- **NFC**: el **driver oficial de Linux del ST25R3916 de ST**
  (`github.com/STMicroelectronics/st25r3916`) + la pila NFC del **Flipper** (mismo chip).

> **Licencias (pendiente de decidir al portar código):** el Flipper es **GPLv3**; reutilizar su
> código obligaría a publicar ZeroRF-NFC como GPLv3 (hoy el `app-builder.json` dice MIT). Bruce y
> el driver de ST tienen sus propias licencias. Antes de copiar código de esos proyectos hay que
> decidir la licencia de ZeroRF-NFC y ajustar `LICENSE` y la ficha. Mientras sea código propio
> (como ahora), es MIT.

## Hardware (Cap CC1101+NFC)

Cableado deducido (docs M5Stack + esquemáticos + repo `osprey74/cardputerzero-apps`); el detalle
está en la memoria de Claude `cardputer-caps-hardware`. Resumen, numeración **BCM** del CM0:

- **SPI0**: SCLK=G11, MOSI=G10, MISO=G9. Comparten bus el CC1101 y el ST25R3916: **solo uno
  activo a la vez**.
- **CC1101**: CS=G7 (CE1), GDO0=G15, RF_SW0=G14, POWER_EN=G26. Bandas 315/433/868/915; OOK/ASK.
- **ST25R3916** (**verificado en el aparato el 23-nov-2026**, `IC_IDENTITY`=0x2A): SPI modo 1 por
  **`/dev/spidev0.2`** (CS2=GPIO22, lo gestiona el kernel), que crea el overlay
  `spi0-spidev2-gpio22-overlay`; IRQ=G23. Encender el cap son tres cosas (sacadas trazando la app
  oficial): `ext_usb_gpio_fun` **en 0**, **GPIO26=1 retenido** mientras se usa (sin él el chip
  habla SPI pero la antena no tiene corriente) y `ext_5v_out`=1. Todo eso lo hace
  `src/platform/st25/cap_power.*`; lo que pide root va con `sudo -n`.
- **Lecciones del chip** (en los comentarios de `st25r3916.cpp`): esperar I_rxe (no I_rxs) antes
  de leer el FIFO; ciclar el campo (15 ms apagado + 6 ms) en cada sondeo; el ACK de escritura
  Type 2 son 4 bits sin CRC; `tx_on` y `MEASURE_AMPLITUDE` no indican si hay campo (sí I_txe).
- **Sin verificar:** selección de banda 433 del CC1101 (al conector solo llega RF_SW0=G14, pero
  el truth table usa RF_SW0 **y** RF_SW1) y si el CC1101 va por CE1 (`spidev0.1`) o necesita
  otro overlay como el NFC.

## Convenciones del aparato que la app respeta

- Se instala como `.deb` con un `.desktop` en `/usr/share/APPLaunch/applications/`; el lanzador
  ejecuta el binario y le cede la pantalla (`/dev/fb0`) y el teclado (evdev).
- `Esc` corto es «atrás» (vuelve al menú) y `Esc` mantenido sale; `Fn`+`H` ayuda, `Fn`+`J` captura.
- Las flechas están en `F`/`Z`/`X`/`C` (con o sin `Fn`): llegan como letras y el viewmodel las
  trata como flechas (F=arriba, X=abajo, Z/C también mueven en el menú de dos entradas).
- **La interfaz está en inglés**, como las demás apps del aparato.

## Versiones y entrega

- La versión está en `project(... VERSION x.y.z)` de `CMakeLists.txt` y en `app-builder.json`:
  se suben a la vez con cada cambio que llegue al aparato, y se anota en «Estado» de aquí y del
  `README.md`.
- El `.deb` se copia a `~/Documentos/ZeroRF-NFC-<versión>.deb`, sustituyendo al anterior.
- Commit en `main` con mensaje en español que empieza por la versión, y push.
- `app-builder.json` tiene la ficha para la tienda (`czdev publish`), pero **no está publicada**:
  la primera publicación pide un vídeo de la app en el aparato. Falta hacer el icono propio
  (`Tools/generar_icono.py` todavía dibuja el de TV-B-Gone).

## Estado

- **0.2.0 (23-nov-2026) — NFC con el driver real; sin probar en el aparato la app integrada.**
  - El 23-nov-2026 la sesión «CPZero» de `plex` trabajó todo el día sobre el aparato, pero **en
    un proyecto nuevo** (`plex:~/CPZero/ZeroRF-NFC`, sin este repo, que no estaba en GitHub): CLI
    + una UI LVGL mínima aparte. Su historial está fusionado aquí (rama `plex`, «ours»: se
    conserva la historia, no su árbol). De allí se trajo lo que valía:
    `src/platform/st25/st25r3916.*` (driver propio, registros de M5Unit-NFC —MIT, en
    `reference/`—), `crypto1.h` (Crypto1 de M5Unit-NFC) y el CLI en `Tools/zeronfc/` (se compila
    en el aparato: `g++ -std=c++17 -O2 -Isrc/platform/st25 Tools/zeronfc/main.cpp src/platform/st25/st25r3916.cpp`).
  - `platform::NfcSt25r3916` ya usa el chip en el aparato: escanear = NFC-A + volcado Type 2
    completo (tamaño por GET_VERSION: UL, UL EV1, NTAG213/215/216); emular = NTAG/Ultralight
    desde un `.nfc` guardado, con «parar» desde la UI (se añadió `stop` a `runEmulationA`).
  - **Validado en el aparato (con el CLI, en plex):** lectura NFC-A, volcado, escritura y clonado
    Type 2. **Escrito pero sin validar:** emulación (el móvil de Bruno no leía ni la NTAG buena),
    NFC-B/V/F y iCLASS (sin tarjetas de referencia), MIFARE Classic por el aire (falta la
    autenticación RF; Bruno no tiene Classic). Una tarjeta de acceso probada no contesta a
    ningún protocolo estándar: propietaria (¿iCLASS/LEGIC?), territorio Proxmark; no insistir.
  - Tarjetas de prueba de Bruno: una NTAG (UID 7 B) y una **NTAG215**. Para validar NFC-V
    harían falta etiquetas ISO15693; UHF (Monza M750) no se puede con este cap.
  - Pendiente: probar la app en cpzero (¿hace falta el `chown` de `/dev/gpiochip0` y
    `/dev/spidev0.2` de `cap_power.cpp` o bruno ya está en los grupos?), escribir tarjetas desde
    la UI, backend real del CC1101.

- **20-nov-2026 (sin versión nueva)**: arreglado en `app.cpp` el núcleo al 100 % que traía la
  plantilla (sin `lv_delay_set_cb`, `lv_delay_ms` esperaba dando vueltas; lo cazó ZeroGPS).
  Ahora usa `clock_gettime`/`usleep` en el aparato. Pruebas en verde; sin instalar.
- **0.1.0 (20-nov-2026) — scaffold + núcleo Sub-GHz, sin probar en el aparato.**
  - Esqueleto traído de ZeroTVBGone, renombrado y reducido: menú con dos tarjetas (Sub-GHz /
    NFC), navegación, tema claro/oscuro, ayuda por página y ESC atrás/salir. Las dos pantallas
    de herramienta son todavía **marcadores de posición** («Coming soon»).
  - **Núcleo de Sub-GHz (software puro, probado en ctest, sin hardware)** en `src/model/`:
    `subghz_signal` (modelo de señal en crudo: tiempos con signo + frecuencia + modulación),
    `subghz_file` (leer/escribir `.sub` **compatible con el Flipper**) y `subghz_decode`
    (codificar/decodificar **Princeton/EV1527**, el protocolo típico de mandos de garaje de
    código fijo). Pruebas en `tests/subghz_*_test.cpp`.
  - **Driver `platform::Cc1101`** (`src/platform/cc1101.*`): hilo de trabajo + estado, como
    `IrBlaster`. En el escritorio (y con `ZERORFNFC_CC1101=sim` en el aparato) es **simulado**:
    grabar inventa un Princeton decodificable, reemitir espera el tiempo real. En el aparato
    es un **stub** que falla con «not implemented yet» (el SPI/GPIO real se hará en `cpzero`).
  - **Pantalla Sub-GHz funcional** (`src/view/screens/subghz_screen.*`): lista de señales
    guardadas + «Record», con grabar → decodificar → guardar `.sub`, reemitir (ENTER) y borrar
    (D). Las señales se guardan en `~/.local/share/zerorfnfc/subghz/`. Para que la herramienta
    reciba teclas y el «atrás», el viewmodel tiene `set_page_handlers(...)` que registra la
    pantalla activa. Probado en el simulador con `xdotool` (graba, decodifica Princeton y
    guarda el `.sub`).
  - **Núcleo de NFC (software puro, probado en ctest, sin hardware)** en `src/model/`:
    `nfc_card` (modelo de tarjeta + `detect_card_type` por ATQA/SAK + hex), `nfc_ndef` (parser
    de mensajes NDEF URI/Text y del TLV Type 2 de UL/NTAG) y `nfc_keys` (diccionario de claves
    por defecto de MIFARE Classic, nivel 1 = claves conocidas, sin recuperación). Pruebas en
    `tests/nfc_*_test.cpp`.
  - **Driver `platform::NfcSt25r3916`** (sim + stub de aparato), **fichero `.nfc`**
    (`src/model/nfc_file.*`, estilo Flipper, round-trip) y **pantalla NFC funcional**
    (`src/view/screens/nfc_screen.*`): escanear → ver tipo/UID/NDEF → guardar → emular → borrar.
    Las tarjetas se guardan en `~/.local/share/zerorfnfc/nfc/`. Probado en el simulador (escanea
    una NTAG simulada, parsea su NDEF `URI: https://m5stack.com` y la guarda).
  - Compila en verde (simulador + ejecutable + 6 pruebas) y se ve bien en el simulador.
  - **Pendiente (necesita el aparato):** los backends reales por SPI+libgpiod del **CC1101**
    (y confirmar en `cpzero` la banda 433 / RF_SW; probar con tu mando) y del **ST25R3916**
    (anticolisión, leer con el diccionario de claves, escribir y emular de verdad). Y: más
    protocolos de código fijo (CAME, NICE FLO); **escribir** tarjetas desde la UI; icono propio;
    decidir licencia al portar código ajeno; repo git + primer commit/publicación.

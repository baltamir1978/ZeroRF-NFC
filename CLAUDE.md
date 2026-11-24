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

- `.cache` es un enlace a `~/.local/share/cardputer-zero-sdk` (LVGL y el *sysroot*, uno por máquina para todas las apps Zero; ver `~/Proyectos/CLAUDE.md`). En un clon nuevo: `mkdir -p ~/.local/share/cardputer-zero-sdk && ln -s ~/.local/share/cardputer-zero-sdk .cache`; si está vacío, la primera compilación lo descarga ahí.
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

> **Licencia: GPL-3.0-or-later** (decidido por Bruno el 23-nov-2026). El lado Sub-GHz porta el
> driver y el mapa de registros del **CC1101 del Flipper Zero (GPLv3)**, así que toda la app es
> GPLv3 (`LICENSE` = GPL-3, `app-builder.json` = `GPL-3.0-or-later`). El lado NFC (ST25R3916,
> Crypto1) viene de M5Unit-NFC (MIT), compatible bajo GPLv3. Los ficheros portados del Flipper
> llevan su cabecera GPLv3 y la atribución.

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
- El `.deb` se copia a `~/Documentos/Software/ZeroRF-NFC-<versión>.deb`, sustituyendo al anterior.
- Commit en `main` con mensaje en español que empieza por la versión, y push.
- `app-builder.json` tiene la ficha para la tienda (`czdev publish`), pero **no está publicada**:
  la primera publicación pide un vídeo de la app en el aparato. El icono ya es propio
  (`Tools/generar_icono.py`: ondas en ámbar sobre violeta).

## Estado

- **0.5.8 (24-nov-2026) â reconoce un mando ya guardado.** La marca no se puede sacar de la
  seÃ±al (KeeLoq lo usan muchos fabricantes y lo que los distingue es la clave de fÃ¡brica, que no
  viaja), pero el **nÃºmero de serie sÃ­** y es constante por mando. `subghz::identity(Decoded)` da una
  clave estable: el serie en KeeLoq (aunque el hop cambie en cada pulsaciÃ³n) y el cÃ³digo en los
  fijos. Al grabar, `SubGhzScreen::match_saved` compara con las demÃ¡s guardadas y, si coincide, dice
  Â«same remote as <nombre>Â»; asÃ­ se bautiza un mando una vez (R) y luego se reconoce. Prueba
  `test_identity` en ctest. Instalada en cpzero.

- **0.5.7 (24-nov-2026) — KeeLoq: la parte cifrada a la vista y aviso más justo.** Instalada en
  cpzero. Reemitir una grabación KeeLoq **puede funcionar**: si el receptor no llegó a oír esa
  pulsación, su contador es válido y la acepta (una vez); y hay clones que repiten siempre la misma
  trama, con receptores que no miran el contador. Así que el aviso al reemitir pasa de «it won't
  open» a «Sent (rolling code: may work only once)», y la lista enseña también el **hop** (bits
  0-31, la parte cifrada; `Decoded::hop`): si dos grabaciones del mismo mando dan el mismo hop, el
  mando repite siempre la trama. **Ni códigos ni números de serie de los mandos de Bruno en este
  repositorio (es público)**: lo concreto de sus pruebas va en la memoria local de Claude.
  - **Confirmado por Bruno en el aparato (24-nov-2026):** grabar para solo en cuanto oye el mismo
    código 3 veces («en cuanto graba bien para»); tres grabaciones seguidas del mismo mando salen
    como KeeLoq con la misma serie y botón y **hop distinto en cada una** (código variable de
    verdad), y una grabación KeeLoq que el receptor no había oído fue aceptada al reemitirla. Lo
    esperable, pues: cada grabación vale como mucho una vez y caduca al usar el mando real.

- **0.5.6 (24-nov-2026) — la grabación de la calle era un KeeLoq; progreso al grabar.** Instalada
  en cpzero. La grabación de Bruno volviendo a casa (`RAW-155.sub`, hecha con la 0.5.4) **no es el
  mando del garaje**: preámbulo de 23 tramos de ~390 µs, pausa de ~3,9 ms (10·Te) y 66 bits PWM de
  390/780 µs = **KeeLoq (HCS de Microchip), código variable**: reemitirlo no abre (cada pulsación
  cambia los 32 bits cifrados; el receptor ignora uno ya usado). Salía RAW porque la cabecera es
  de 24 elementos y sólo se saltaban 8.
  - `decode_fixed` prueba también a empezar justo después de la primera pausa larga (≥ 6 × el tramo
    más corto) de los 60 primeros elementos. `as_keeloq`: 66 bits, Te 250-600 y cabecera ≥ 10
    elementos → `Protocol::KeeLoq`, `rolling`, `serial` (bits 32-59, del menos significativo) y
    `button` (60-63); bit 1 = pulso corto (al revés que el genérico). Esa grabación sale «KeeLoq
    rolling code SN … btn …», nombre
    `KEELOQ-<serie>`. No se descifra nada (ni hace falta para identificarlo).
  - Pantalla: al grabar, «heard 1/3, 2/3…» (`Status::heard`, `Cc1101::kRepeatsNeeded` = 3; lo pidió
    Bruno: «si quieres tres tramas, ponlo en la pantalla»); al reemitir un código variable, «Sent,
    but rolling code: it won't open».

- **0.5.5 (24-nov-2026) — grabar en la calle: escucha corta y trama buena por votación.** Bruno
  grabó el mando volviendo a casa y la 0.5.4 dijo RAW tras escuchar muchísimo. **Sin la captura
  todavía** (el aparato estaba fuera de Tailscale): arreglado lo que se ve en el código.
  - La escucha duraba **siempre 20 s** (no había nada que la cortara) y ENTER la descartaba. Ahora
    `run_receive_device` mira cada 400 ms lo capturado y **para en cuanto el mismo código llega 3
    veces**; ENTER (`Cc1101::finish_receive`) para y se queda con lo grabado; ESC cancela.
  - La trama se elegía por **longitud más repetida** y los tramos < 60 µs se **tiraban**: con ruido
    alrededor, o un parpadeo dentro de un pulso (que rompe el emparejamiento de toda la trama),
    podía salir un trozo de ruido. `subghz::extract_best_frame` (en `model/`, con prueba de una
    captura con ruido, trama cortada y parpadeos): `deglitch` (un tramo corto se suma al anterior y
    dos del mismo signo se juntan), parte por pausas, decodifica cada repetición y **gana el código
    más votado**; si nada se reconoce, la trama más repetida como antes.
  - **El último bit**: el silencio del último bit se funde con la pausa entre tramas y quedaba un
    pulso suelto que se descartaba. Se cuenta por su ancho (no en Princeton: ahí es el pulso de
    sincronismo). **El mando del garaje tiene 96 bits, no 95**: «Fixed code 96 bit …».
  - Princeton admite proporción 2,2-3,8 (antes 2,4-3,6: con ruido en los tiempos se escapaba).
  - Cada grabación deja la **captura entera** en `~/.local/share/zerorfnfc/subghz-raw/last-capture.sub`
    (no sale en la lista): traerla con `scp` para estudiar un mando que no se reconozca.
  - **Sin instalar** (el aparato estaba fuera): instalarla y mirar `last-capture.sub` de la próxima
    grabación; si sigue saliendo RAW, con esa captura se ve por qué.

- **0.5.4 (24-nov-2026) — instalada en cpzero; lo nuevo visto en la pantalla del aparato.**
  Con el aparato encendido (Bruno no podía acercar tarjetas ni el mando):
  - **El mando del garaje («madrid») sale como «Fixed code 95 bit … 268/540 us»** (96 desde la 0.5.5)
    (no 77: lo de 77 era de la extracción de la 0.4). No lo reconocía la 0.5.0: lleva una
    **cabecera de 8 elementos** de otros anchos (2·Te, 2·Te, 3·Te, 3·Te, 2·Te, 2·Te, Te, Te) antes
    de los bits 1:2 de pulso + silencio. `decode_fixed` prueba ahora a saltar 0-8 elementos y vale
    el primer inicio desde el que **la trama entera se decodifica** (no basta con que queden dos
    anchos: con 4 saltados ya quedaban dos anchos y los dos pares de cabecera rompían el PWM).
    Añadido también **Manchester** (tramos de 1 o 2 medios bits; nombre `MAN<n>-xxxx`), por si
    aparece un mando así; con pruebas de las dos cosas.
  - **Menú principal:** en el aparato las fuentes salen más altas que en el simulador y el título
    de cada tarjeta se montaba sobre el subtítulo; tarjetas de 52 px (antes 46).
  - **Tarjetas guardadas sin GET_VERSION** (las de la 0.2): el modelo se deduce de las páginas
    volcadas (16, 20, 41, 45, 135, 231 son de modelos distintos), en `card_product` (la NTAG215
    salía como «MIFARE Ultralight (clásica)», en español) y en `type2_layout` (sin eso, escribir
    una 215 antigua habría dado «no cabe» por sus páginas de configuración).
  - Textos de error que llegaban a la pantalla en español (radio y driver NFC), en inglés.
  - Descripción del paquete: era la de TV-B-Gone («Turns off almost any TV…»), heredada de la
    plantilla; está en `cmake/cm0-package.cmake` como variable de **caché**: tras cambiarla hay
    que borrar `build/*/CMakeCache.txt` o sigue la vieja.
  - **Falta (con Bruno delante):** escribir una NTAG con W (la prueba segura: la NTAG215 sobre sí
    misma, `card-04DEAD62D42A81`, que no cambia nada) y que la reemisión abra el garaje.

- **0.5.0 (24-nov-2026) — mandos reconocidos y escribir tarjetas.** Lo pidió Bruno («sigue
  trabajando lo que puedas»), con el aparato apagado: todo probado en el simulador y en ctest;
  **sin probar en el aparato** (`.deb` en `~/Documentos/Software/ZeroRF-NFC-0.5.0.deb`, sin instalar).
  - **Sub-GHz: qué mando es.** `subghz::decode_fixed` (`src/model/subghz_decode.*`) analiza la
    forma sin suponer marca: parte la captura por las pausas (> 8 ms), coge la trama más repetida,
    exige dos anchos (corto y largo, proporción 1,5-4,5) y prueba los dos emparejamientos (pulso +
    silencio desde el principio, o silencio + pulso tras un pulso de arranque); bit 1 = el primero
    del par es el largo. Con eso reconoce **CAME** (Te ≈ 320 µs, 1:2, 12/24 bits) y **NICE FLO**
    (Te ≈ 700 µs), y lo demás de forma reconocible sale como **«Fixed code N bit»** con su código
    en hex (también > 64 bits: `code_hex`), como el mando del garaje de Bruno (1:2; 95 bits, ver 0.5.4).
    **Arreglado de paso:** el buscador de Princeton aceptaba un mando 1:2 como 1:3 (su ±40 % deja
    pasar 2·Te por 3·Te) y daba 24 bits inventados; ahora manda el análisis de forma si dice otra
    cosa. Al grabar, el nombre sale de `suggested_name` (`PT-…`, `CAME-…`, `NICE-…`, `FIX77-9C2E`,
    `RAW-n`) y la línea de estado dice lo reconocido; con una señal guardada seleccionada, la línea
    de abajo enseña protocolo, bits, código y tiempos (`describe`, leído una vez por señal).
    Reemitir sigue siendo en crudo: lo decodificado es información, no cambia lo que se emite.
  - **NFC: escribir una tarjeta guardada** en una NTAG/Ultralight (tecla **W** en el detalle).
    `src/model/nfc_type2.*`: disposición de cada modelo por el byte 6 de GET_VERSION (Ultralight,
    UL EV1 11/21, NTAG210/212/213/215/216) y `plan_type2_write`, que **sólo escribe memoria de
    usuario** (desde la página 4 hasta la última de usuario): ni UID/CC (0-3), ni bloqueo dinámico,
    configuración, contraseña o PACK del final, que copiados de un volcado pueden **bloquear la
    etiqueta para siempre** o ponerle contraseña (la contraseña se lee como ceros). Si el destino
    es más pequeño sólo vale si lo que no cabe está a cero (una NDEF corta de una 215 cabe en una
    213). `NfcSt25r3916::start_write` (estados `Writing`/`Written`, resumen en `Status::message`)
    espera la etiqueta, lee su GET_VERSION y su bloqueo estático (página 2; con bits puestos no
    escribe), escribe y **relee para verificar**. El CLI `zeronfc load` sigue escribiendo todo lo
    que le den (es la herramienta de diagnóstico).
  - Pruebas: 8 ejecutables de ctest (nuevos: CAME/NICE, código fijo genérico con ruido y trama
    cortada, nombres, rechazo de tres anchos; `nfc_type2_test`: disposiciones, mismo modelo,
    destino menor/mayor, bloqueo).
  - Instalada y vista en el aparato con la 0.5.4 (arriba).

- **0.4.0 (23-nov-2026) — Sub-GHz COMPLETO en la app (captura + reemisión reales) + renombrar.**
  `platform::Cc1101` ya habla con el radio en el aparato (antes era sim/stub): `run_receive_device`
  (preset Ook650, RF_SW0=GPIO14=1, captura los flancos de GDO0=GPIO15 con GPIO v2, extrae una trama
  limpia y la guarda como `Signal`) y `run_transmit_device` (OOK async, GPIO15 como salida con
  espera activa bajo **SCHED_FIFO**, repite la trama). Reusa `cc::Cc1101` (driver) y `CapPower`
  (nuevo `power_on_rails()`: POWER_EN+5V sin el overlay de NFC). La **pantalla Sub-GHz** graba,
  guarda, reemite, borra **y renombra** (tecla **R** sobre una señal guardada → escribir nombre →
  ENTER; `platform::rename_signal`, igual que en NFC), así se pueden guardar varios mandos con
  nombre. El `.deb` da **CAP_SYS_NICE** al binario (postinst + dep `libcap2-bin`) para que el TX
  tenga tiempo real sin correr como root (acceso SPI/GPIO por los grupos spi/gpio de bruno).
  Probado en el simulador (grabar+renombrar) y el driver por CLI en el aparato. **Falta la prueba
  real desde la app en el aparato**: grabar el mando, y que **reemitir abra el garaje** (llevar el
  Cardputer a la puerta; si no abre, el pin RF_SW de TX podría ser el contrario — hoy fijo a 1).

- **CC1101 / Sub-GHz — driver portado y chip detectado en el aparato (23-nov-2026).** Decidido por
  Bruno pasar a **GPL-3.0-or-later** y copiar de Flipper. Portado su `cc1101_regs.h` (mapa de
  registros, GPLv3) y escrito `src/platform/cc1101/cc1101.{h,cpp}` sobre **spidev** (`/dev/spidev0.1`,
  CS=CE1=GPIO7; SPI modo 0): reset, strobes, read/write reg y burst, part/version, setFrequency,
  PA table, RX/TX, FIFO, MARCSTATE. **Confirmado en cpzero** con `zeronfc cc1101`: part=0x00,
  version=0x14, resetea, acepta 433,92 MHz (MARCSTATE idle). Alimentación = la del cap (POWER_EN
  GPIO26 + ext_5v, vía `cap_power`/`CapPower`).
  - **Captura OOK funcionando en el aparato (23-nov-2026).** Preset Ook650 de Flipper en
    `configureOokRx/Tx`, GDO0=IOCFG0 0x0D (datos serie). Comando `zeronfc rfrx [--secs N]
    [--rfsw 0|1] [--freq MHz]`: pone **RF_SW0 (GPIO14)=1** (banda 433 confirmada así), escucha y
    captura los flancos de **GDO0 = GPIO15** con marca de tiempo (GPIO v2 chardev). **Capturado
    el mando del garaje de Bruno** con la **antena de 433 puesta**: tren OOK limpio. Guarda la
    señal cruda en `~/.local/share/zerorfnfc/subghz/captura-*.sub` (formato Flipper RAW).
  - **El mando NO es Princeton/EV1527** (ratio **1:2**, no 1:3; `subghz::decode` no engancha,
    normal). Es **código fijo** igualmente, así que se clona **reemitiendo el crudo** (no hace
    falta decodificar). Hace falta antena para capturar/emitir (sin ella el CC1101 no capta y TX
    sin antena daña el chip).
  - **Reemisión (TX) implementada (23-nov-2026).** Comando `zeronfc rftx [fichero] [--repeats N]
    [--rfsw 0|1] [--freq MHz]` (sin fichero usa el último `.sub`). OOK async: `configureOokTx`
    (GDO0 en alta-Z), GPIO15 como **salida** que marca el 1/0 de la portadora, con espera activa
    (`CLOCK_MONOTONIC`) bajo **SCHED_FIFO** para que los ~400 µs no bailen. `extract_frame()` saca
    **una trama limpia** de la captura larga (parte por los silencios >8 ms y coge la longitud más
    frecuente) y la repite N veces. Probado mecánicamente en el aparato: extrae 155 tramos (~77
    bits) y reemite sin error. **Falta la prueba de verdad: que abra el garaje**, llevando el
    Cardputer (portátil) a la puerta y corriendo `rftx`; si no abre, probar `--rfsw 0` (el camino
    de TX del switch puede ser el contrario al de RX). Luego llevarlo a `platform::Cc1101` (hoy
    sim/stub) y a la pantalla Sub-GHz.

- **0.3.0 (23-nov-2026) — tipo exacto en la pantalla + icono propio.** Al escanear, además del UID
  y el tipo, se lee **GET_VERSION** (NTAG/Ultralight → "NTAG215 (504 B)", etc.) y el **ATS** de las
  ISO14443-4 (DESFire → "MIFARE DESFire EV1", con su línea `ATS:`). Campos nuevos `version` y `ats`
  en `nfc::Card` (se guardan en el `.nfc` como `GetVersion:` y `ATS:`); `card_product()` da el
  nombre fino. Icono nuevo (ondas en ámbar sobre violeta; ya no es el de TV-B-Gone). Probado en el
  simulador; falta confirmar en el aparato con las tarjetas de Bruno.

- **MIFARE Classic (Crypto1) — autenticación portada y probada en el aparato (23-nov-2026).**
  En `st25r3916.cpp`: `mifareAuth(key_a, uid4, block, key)` (3 pasos: AUTH1 con CRC → nonce;
  AUTH2 {Nr}{Ar} cifrado con paridad manual, `usleep(90)` antes; comprueba At==suc3) y
  `mifareReadBlock`. El cifrado es `crypto1.h`; helpers `mfSendEncrypted`/`mfTransceiveEncrypted`.
  Comando de la herramienta: `zeronfc classic` (recorre los sectores con un diccionario de claves;
  `MFDBG=1` traza el handshake y limita al sector 0). **Confirmado en hardware:** AUTH1 recibe el
  nonce de una Classic 1K real (UID 255778B9, SAK 08). **Pero 0 sectores abiertos:** esa tarjeta
  (acceso/transporte) usa **claves propias**, no las de fábrica, y AUTH2 no responde con ninguna
  conocida. **Falta validar el AUTH2 con una MIFARE Classic de claves conocidas** (una en blanco,
  todo `FFFFFFFFFFFF`): hasta entonces no se puede distinguir «clave incorrecta» de un bug fino en
  el cifrado/tiempo. Recuperar claves propias (nested/darkside) es territorio Proxmark, fuera de
  alcance. La UI aún no expone Classic (sólo escanea NFC-A + vuelca Type 2); se añadirá cuando una
  tarjeta abra de verdad. Las tarjetas de acceso de Bruno de 13,56 son casi todas **DESFire EV1**
  (ATS `06 75 77 81 02 80`): sólo UID+ATS, los ficheros van cifrados (AES/DES).
- **Emulación NFC-A — diagnosticada en el aparato (23-nov-2026, con el móvil de Bruno, que ya lee
  tarjetas).** Con `Tools/zeronfc/emudiag.cpp` (dev-tool: pone el modo target y vuelca registros):
  1. **Bug encontrado y corregido:** el umbral del detector de campo externo (`REG_EFD_ACT_THR`)
     se quedaba en `0x13` (valor de lectura) y en modo tarjeta el chip **ni veía el campo** del
     móvil → por eso «no hacía nada». Bajado a `0x02` en `runEmulationA`: ahora `efd_o=1` e
     `I_eon` saltan de sobra.
  2. **Lo que sigue sin funcionar:** el chip ve el campo pero **nunca reconoce el REQA** del móvil
     (ni `I_nfct` = velocidad reconocida, ni `I_wu_a` = seleccionado), ni leyendo activamente. La
     causa: el **receptor está configurado para modo lector** (demodular la subportadora de una
     tarjeta), no para modo tarjeta (recibir la modulación ASK directa del lector a 106 kbps).
     Falta portar la config de recepción en modo escucha (ST rfal `rfalListenStart` o el Flipper,
     GPLv3 → decidir licencia). Es trabajo de driver, no un parche. El móvil es mal instrumento:
     deja de sondear al bloquearse; para probar hay que tenerlo despierto y, mejor, leyendo.
     Nota: el modo directo `0x88` daba `efd=no`; el bueno para escuchar es `0xC8` (detección de
     velocidad). La emulación en la UI queda, pues, **sin funcionar todavía**.
- **0.2.1 (23-nov-2026) — renombrar tarjetas guardadas** (R en el detalle: se escribe el nombre
  desde cero, ENTER guarda, ESC cancela; `src/model/name_edit.*` con pruebas). Arreglado de paso
  que **en el aparato no llegaban casi ninguna letra** (la D de borrar tampoco): se trajo
  `map_evdev_char` de ZeroGPS 0.3.2. La T del tema ahora sólo actúa si la pantalla no usa la
  tecla, para poder escribirla. Probado en el simulador; sin confirmar en el aparato.

- **0.2.0 (23-nov-2026) — NFC con el driver real; escanear y guardar confirmados por Bruno en
  el aparato** (la Ultralight EV1, 20 páginas, y la NTAG215, 135 páginas con su NDEF URI, en
  `~/.local/share/zerorfnfc/nfc/`). Emular sigue sin validar (falta un lector que funcione).
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
  - **Probado en cpzero el 23-nov-2026 (backend, sin interfaz, como usuario `bruno`):** enciende
    el cap, carga el overlay con `sudo -n` e inicializa el chip sin root (bruno está en los
    grupos `gpio` y `spi`: el `chown` de `cap_power.cpp` no se usa). Leyó a la primera la
    Ultralight EV1 de pruebas (UID 7 B, 20 páginas = 80 B), igual que `Tools/zeronfc read`.
    Instalada la 0.2.0 en lugar del paquete que dejó plex (mismo nombre, `zerorf-nfc`).
  - Pendiente: validar emular con un lector, escribir tarjetas desde la UI, backend real del
    CC1101.

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

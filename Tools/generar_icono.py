#!/usr/bin/env python3
"""Dibuja el icono de la app RF / NFC en los tamaños que piden el lanzador (assets/images) y la
tienda (store/icon.png). El motivo son ondas de señal saliendo de un punto (sirve para Sub-GHz y
para NFC/contactless a la vez), en ámbar sobre un fondo violeta oscuro, al estilo de las demás
apps Zero (fondo degradado en cuadrado de esquinas redondas). Necesita Pillow.

    Tools/generar_icono.py
"""

import os

from PIL import Image, ImageDraw

LADO = 1024  # se dibuja grande y se reduce: así los bordes salen suaves


def icono():
    imagen = Image.new("RGBA", (LADO, LADO), (0, 0, 0, 0))

    # Fondo: degradado violeta oscuro recortado en un cuadrado de esquinas redondas.
    fondo = Image.new("RGBA", (LADO, LADO))
    trazo = ImageDraw.Draw(fondo)
    arriba, abajo = (58, 44, 86), (18, 14, 30)
    for y in range(LADO):
        t = y / (LADO - 1)
        trazo.line([(0, y), (LADO, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(arriba, abajo)) + (255,))
    mascara = Image.new("L", (LADO, LADO), 0)
    ImageDraw.Draw(mascara).rounded_rectangle([40, 40, LADO - 40, LADO - 40], radius=200, fill=255)
    imagen.paste(fondo, (0, 0), mascara)

    trazo = ImageDraw.Draw(imagen)
    ambar = (247, 181, 56, 255)
    ambar_claro = (255, 206, 112, 255)

    # Origen de la señal, abajo a la izquierda, dentro del margen del cuadrado.
    ox, oy = 300, 724

    # Punto de origen (el "emisor": antena del CC1101 / chip del NFC).
    trazo.ellipse([ox - 52, oy - 52, ox + 52, oy + 52], fill=ambar_claro)

    # Tres ondas concéntricas abriéndose hacia arriba a la derecha (NE). Cada arco va de
    # ~-80° a ~10° respecto al origen, grosor decreciente con la distancia.
    for r, w in ((230, 64), (400, 58), (570, 52)):
        trazo.arc([ox - r, oy - r, ox + r, oy + r], start=-78, end=8, fill=ambar, width=w)

    return imagen


def main():
    raiz = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    imagen = icono()
    for ruta, lado in (
        ("assets/images/zerorfnfc.png", 100),
        ("assets/images/zerorfnfc_100.png", 100),
        ("assets/images/zerorfnfc_80.png", 80),
        ("store/icon.png", 256),
    ):
        imagen.resize((lado, lado), Image.LANCZOS).save(os.path.join(raiz, ruta))


if __name__ == "__main__":
    main()

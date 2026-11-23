#!/usr/bin/env bash
# Compila la app LVGL de ZeroRF-NFC para el Cardputer Zero (aarch64) y genera el .deb en dist/,
# dentro de Docker (sólo hace falta Docker). Uso: Tools/compilar.sh
set -euo pipefail
cd "$(dirname "$0")/.."
IMAGEN=zerorfnfc-build
if command -v docker >/dev/null 2>&1; then
    docker build -q -t "$IMAGEN" Tools >/dev/null
    docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -w /src "$IMAGEN" bash -c '
        set -e
        cmake -B build/cross -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
          -DCMAKE_BUILD_TYPE=Release \
          -DFETCHCONTENT_BASE_DIR=/src/.cache/deps
        cmake --build build/cross
        ( cd build/cross && cpack )'
    ls -l dist/*.deb
else
    echo "Hace falta Docker." >&2; exit 1
fi

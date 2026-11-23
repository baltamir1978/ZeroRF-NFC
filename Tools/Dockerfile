# Entorno de compilación cruzada de ZeroRF-NFC (aarch64) y empaquetado .deb. Lo usa compilar.sh.
FROM debian:trixie-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential crossbuild-essential-arm64 cmake ninja-build git ca-certificates \
    dpkg-dev fakeroot file \
    && rm -rf /var/lib/apt/lists/*

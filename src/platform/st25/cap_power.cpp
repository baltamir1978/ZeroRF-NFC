/*
 * SPDX-License-Identifier: MIT
 */

#include "cap_power.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace platform {
namespace {

constexpr const char* kGpioChip = "/dev/gpiochip0";
constexpr int kPowerEnLine = 26;
constexpr const char* kLed5v = "/sys/class/leds/ext_5v_out/brightness";
constexpr const char* kOverlay = "spi0-spidev2-gpio22-overlay";

bool exists(const char* path) {
    return ::access(path, F_OK) == 0;
}

// Ejecuta un comando como root: directo si ya lo somos, si no con `sudo -n` (sin preguntar).
bool run_as_root(const std::string& command) {
    const std::string full = (::geteuid() == 0 ? "" : "sudo -n ") + command + " >/dev/null 2>&1";
    return std::system(full.c_str()) == 0;
}

// Escribe en un fichero de /sys; si no hay permiso, a través de `sudo -n tee`.
bool write_sysfs(const char* path, const char* value) {
    int fd = ::open(path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        const bool ok = ::write(fd, value, std::strlen(value)) >= 0;
        ::close(fd);
        return ok;
    }
    return run_as_root(std::string("sh -c 'echo ") + value + " > " + path + "'");
}

// Si el usuario no puede abrir el nodo, se le da permiso hasta el próximo arranque.
// TODO(aparato): comprobar en cpzero si bruno ya está en los grupos gpio/spi y, si es así,
// quitar esto (o sustituirlo por una regla de udev en el paquete).
bool ensure_rw(const char* path) {
    if (::access(path, R_OK | W_OK) == 0) {
        return true;
    }
    run_as_root(std::string("chown ") + std::to_string(::geteuid()) + " " + path);
    return ::access(path, R_OK | W_OK) == 0;
}

} // namespace

CapPower::~CapPower() {
    power_off();
}

bool CapPower::hold_power_en() {
    if (line_fd_ >= 0) {
        return true;
    }
    if (!ensure_rw(kGpioChip)) {
        error_ = "No access to /dev/gpiochip0";
        return false;
    }
    chip_fd_ = ::open(kGpioChip, O_RDWR | O_CLOEXEC);
    if (chip_fd_ < 0) {
        error_ = "Cannot open /dev/gpiochip0";
        return false;
    }
    gpio_v2_line_request req;
    std::memset(&req, 0, sizeof(req));
    req.offsets[0] = kPowerEnLine;
    req.num_lines = 1;
    std::strncpy(req.consumer, "zerorfnfc", sizeof(req.consumer) - 1);
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = 1;
    req.config.attrs[0].mask = 1;
    if (::ioctl(chip_fd_, GPIO_V2_GET_LINE_IOCTL, &req) < 0 || req.fd < 0) {
        ::close(chip_fd_);
        chip_fd_ = -1;
        error_ = "Cannot drive POWER_EN (GPIO26)";
        return false;
    }
    line_fd_ = req.fd;
    return true;
}

void CapPower::release_power_en() {
    if (line_fd_ >= 0) {
        ::close(line_fd_);
        line_fd_ = -1;
    }
    if (chip_fd_ >= 0) {
        ::close(chip_fd_);
        chip_fd_ = -1;
    }
}

bool CapPower::power_on_rails() {
    error_.clear();
    if (!hold_power_en()) {
        return false;
    }
    if (!write_sysfs(kLed5v, "1")) {
        error_ = "Cannot switch on the cap 5V";
        release_power_en();
        return false;
    }
    on_ = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return true;
}

bool CapPower::power_on_nfc() {
    if (!power_on_rails()) {
        return false;
    }
    if (!exists(kNfcSpiDevice)) {
        run_as_root(std::string("dtoverlay ") + kOverlay);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    if (!exists(kNfcSpiDevice)) {
        error_ = "No /dev/spidev0.2 (SPI overlay)";
        return false;
    }
    if (!ensure_rw(kNfcSpiDevice)) {
        error_ = "No access to /dev/spidev0.2";
        return false;
    }
    return true;
}

void CapPower::power_off() {
    if (on_) {
        write_sysfs(kLed5v, "0");
        on_ = false;
    }
    release_power_en();
}

} // namespace platform

// Shim LD_PRELOAD: intercepta ioctl(SPI_IOC_MESSAGE) y vuelca los buffers TX
// para ver qué escribe por SPI la app oficial del Cap NFC. Sólo para depurar.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>

static int (*real_ioctl)(int, unsigned long, ...) = 0;

int ioctl(int fd, unsigned long request, ...) {
    va_list ap; va_start(ap, request);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    if (!real_ioctl) real_ioctl = dlsym(RTLD_NEXT, "ioctl");

    if (_IOC_TYPE(request) == SPI_IOC_MAGIC && _IOC_NR(request) == 0) {
        unsigned n = _IOC_SIZE(request) / sizeof(struct spi_ioc_transfer);
        struct spi_ioc_transfer* tr = (struct spi_ioc_transfer*)arg;
        for (unsigned i = 0; i < n; ++i) {
            const uint8_t* tx = (const uint8_t*)(uintptr_t)tr[i].tx_buf;
            fprintf(stderr, "SPI[%u] len=%u tx:", i, tr[i].len);
            if (tx) for (unsigned j = 0; j < tr[i].len && j < 24; ++j) fprintf(stderr, " %02X", tx[j]);
            else fprintf(stderr, " (null)");
            fprintf(stderr, "\n");
        }
    }
    return real_ioctl(fd, request, arg);
}

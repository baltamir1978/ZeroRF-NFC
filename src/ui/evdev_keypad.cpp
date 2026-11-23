#include "evdev_keypad.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

struct Keypad {
    int fd = -1;
    uint32_t key = 0;
    lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
};

bool g_quit = false;

// Mapeo de códigos evdev a teclas LVGL. Las flechas del Cardputer Zero están en F/Z/X/C (Fn),
// que llegan como letras; aquí las tratamos como navegación para que valgan sin Fn.
uint32_t map_key(uint16_t code) {
    switch (code) {
        case KEY_ESC: return LV_KEY_ESC;
        case KEY_ENTER: case KEY_KPENTER: case KEY_SPACE: return LV_KEY_ENTER;
        case KEY_UP: case KEY_E: return LV_KEY_UP;
        case KEY_DOWN: case KEY_S: case KEY_X: return LV_KEY_DOWN;
        case KEY_LEFT: case KEY_A: case KEY_Z: return LV_KEY_PREV;
        case KEY_RIGHT: case KEY_D: case KEY_C: return LV_KEY_NEXT;
        case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
        default: return 0;
    }
}

bool has_nav_keys(int fd) {
    unsigned long bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return false;
    auto has = [&](int c) {
        const int w = sizeof(unsigned long) * 8;
        return (bits[c / w] & (1UL << (c % w))) != 0;
    };
    return has(KEY_ENTER) && (has(KEY_UP) || has(KEY_S) || has(KEY_ESC));
}

void read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* kp = static_cast<Keypad*>(lv_indev_get_driver_data(indev));
    data->continue_reading = false;
    if (!kp) { data->state = LV_INDEV_STATE_RELEASED; return; }
    input_event ev{};
    while (read(kp->fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type != EV_KEY || ev.value == 2) continue;  // 2 = autorepeat, lo ignoramos
        uint32_t k = map_key(ev.code);
        if (!k) continue;
        if (k == LV_KEY_ESC && ev.value) g_quit = true;  // ESC: salir de la app
        kp->key = k;
        kp->state = ev.value ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = true;
        break;
    }
    data->key = kp->key;
    data->state = kp->state;
}

lv_indev_t* make_indev(int fd, lv_display_t* display, lv_group_t* group) {
    auto* kp = new Keypad{fd, 0, LV_INDEV_STATE_RELEASED};
    lv_indev_t* indev = lv_indev_create();
    if (!indev) { delete kp; close(fd); return nullptr; }
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, read_cb);
    lv_indev_set_driver_data(indev, kp);
    lv_indev_set_display(indev, display);
    lv_indev_set_group(indev, group);
    return indev;
}

}  // namespace

bool evdev_keypad_quit_requested() { return g_quit; }

lv_indev_t* evdev_keypad_create(lv_display_t* display, lv_group_t* group) {
    // Recorre /dev/input/event* y usa el primero que tenga teclas de navegación (el tca8418).
    DIR* dir = opendir("/dev/input");
    if (!dir) return nullptr;
    lv_indev_t* result = nullptr;
    while (auto* e = readdir(dir)) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + e->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (!has_nav_keys(fd)) { close(fd); continue; }
        result = make_indev(fd, display, group);
        break;
    }
    closedir(dir);
    return result;
}

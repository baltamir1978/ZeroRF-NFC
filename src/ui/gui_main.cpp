// Interfaz LVGL de ZeroRF-NFC para el Cardputer Zero (framebuffer 320x170 + teclado tca8418).
// Reutiliza el arranque de LVGL/fbdev y el mapeo de teclas del esqueleto de ZeroPower.
#include "lvgl.h"
#include "src/drivers/display/fb/lv_linux_fbdev.h"

#include "evdev_keypad.h"
#include "nfc_ops.h"

#include <atomic>
#include <ctime>
#include <string>
#include <unistd.h>

namespace {
std::atomic<bool> g_quit{false};
lv_obj_t* g_result = nullptr;
lv_display_t* g_disp = nullptr;

uint32_t tick_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}
void delay_ms(uint32_t ms) { usleep(ms * 1000U); }

void set_result(const std::string& text) {
    if (g_result) lv_label_set_text(g_result, text.c_str());
}

// Ejecuta una operacion NFC mostrando "Trabajando..." antes (la UI se congela unos segundos).
void run_op(const char* working, std::string (*op)()) {
    set_result(working);
    if (g_disp) lv_refr_now(g_disp);
    set_result(op());
}

void on_read(lv_event_t*)     { run_op("Leyendo tarjeta...", nfcops::read_card); }
void on_identify(lv_event_t*) { run_op("Identificando...", nfcops::identify); }
void on_subghz(lv_event_t*)   { set_result(nfcops::sub_ghz_note()); }

lv_obj_t* add_button(lv_obj_t* parent, lv_group_t* group, const char* txt, lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_height(b, 28);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(group, b);
    return b;
}

void build_ui(lv_group_t* group) {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101014), 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 4, 0);
    lv_obj_set_style_pad_row(scr, 4, 0);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "RF / NFC");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8ab4f8), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    // Fila de acciones
    lv_obj_t* row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    add_button(row, group, "Leer", on_read);
    add_button(row, group, "Identificar", on_identify);
    add_button(row, group, "Sub-GHz", on_subghz);

    // Area de resultados (desplazable)
    lv_obj_t* box = lv_obj_create(scr);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x16181d), 0);
    lv_obj_set_style_pad_all(box, 6, 0);
    g_result = lv_label_create(box);
    lv_obj_set_width(g_result, LV_PCT(100));
    lv_label_set_long_mode(g_result, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(g_result, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_result, lv_color_hex(0xe0e0e0), 0);
    lv_label_set_text(g_result,
        "Elige una accion con las flechas y ENTER.\n"
        "Leer: UID y datos de una tarjeta NFC.\n"
        "Identificar: tipo de tarjeta.\n"
        "ESC para salir.");
}

}  // namespace

int main() {
    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_delay_set_cb(delay_ms);

    g_disp = lv_linux_fbdev_create();
    if (!g_disp) { return 1; }
    lv_linux_fbdev_set_file(g_disp, "/dev/fb0");

    lv_group_t* group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_t* kp = evdev_keypad_create(g_disp, group);
    (void)kp;

    build_ui(group);

    while (!g_quit.load() && !evdev_keypad_quit_requested()) {
        uint32_t idle = lv_timer_handler();
        if (idle > 50) idle = 50;
        if (idle < 1) idle = 1;
        lv_delay_ms(idle);
    }
    return 0;
}

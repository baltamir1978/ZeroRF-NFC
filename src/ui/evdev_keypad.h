#pragma once
#include "lvgl.h"
// Crea un indev de teclado LVGL leyendo el teclado del Cardputer (tca8418) por evdev.
// Lo asocia al grupo dado para navegar con flechas/ENTER/ESC. Devuelve el indev o nullptr.
lv_indev_t* evdev_keypad_create(lv_display_t* display, lv_group_t* group);
// true si se ha pulsado ESC (para salir de la app). Se consulta en el bucle principal.
bool evdev_keypad_quit_requested();

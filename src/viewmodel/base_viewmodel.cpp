/*
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

namespace viewmodel {
namespace {

// Se comprueba que los códigos de tecla de LVGL son los que esperamos: si LVGL los
// cambiara, que falle aquí al compilar y no en el Cardputer.
static_assert(LV_KEY_UP == 17 && LV_KEY_DOWN == 18 && LV_KEY_RIGHT == 19 && LV_KEY_LEFT == 20);
static_assert(LV_KEY_ENTER == 10);

const char* title_for_page(model::AppPage page) {
    switch (page) {
        case model::AppPage::SubGhz:
            return "Sub-GHz";
        case model::AppPage::Nfc:
            return "NFC";
        case model::AppPage::Home:
        default:
            return "RF / NFC";
    }
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(title_for_page(model::AppPage::Home)),
      dark_mode_subject_(true),
      current_page_subject_(static_cast<int>(model::AppPage::Home)),
      quit_requested_subject_(false),
      menu_index_subject_(0) {}

BaseViewModel::~BaseViewModel() = default;

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::dark_mode_subject() {
    return dark_mode_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

lv_subject_t* BaseViewModel::menu_index_subject() {
    return menu_index_subject_.native();
}

bool BaseViewModel::is_dark_mode() const {
    return dark_mode_subject_.value();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    dark_mode_subject_.set(enabled);
}

void BaseViewModel::toggle_dark_mode() {
    dark_mode_subject_.toggle();
}

int BaseViewModel::menu_index() const {
    return menu_index_subject_.value();
}

model::AppPage BaseViewModel::current_page() const {
    return static_cast<model::AppPage>(current_page_subject_.value());
}

model::AppPage BaseViewModel::page_for_menu(int index) {
    switch (index) {
        case 0:
            return model::AppPage::SubGhz;
        case 1:
        default:
            return model::AppPage::Nfc;
    }
}

void BaseViewModel::go_to(model::AppPage page) {
    title_subject_.set(title_for_page(page));
    current_page_subject_.set(static_cast<int>(page));
}

void BaseViewModel::move_selection(int delta) {
    int next = menu_index_subject_.value() + delta;
    // Vuelta por los extremos: en un menú de dos, arriba/abajo alternan.
    while (next < 0) {
        next += kMenuCount;
    }
    next %= kMenuCount;
    menu_index_subject_.set(next);
}

void BaseViewModel::set_page_handlers(KeyHandler key_handler, BackHandler back_handler) {
    page_key_handler_ = std::move(key_handler);
    page_back_handler_ = std::move(back_handler);
}

void BaseViewModel::clear_page_handlers() {
    page_key_handler_ = nullptr;
    page_back_handler_ = nullptr;
}

bool BaseViewModel::handle_key(uint32_t key) {
    // En una herramienta las teclas las gestiona antes su pantalla, si se ha registrado: así,
    // mientras se escribe un nombre, la T es una letra y no el cambio de tema.
    if (current_page() != model::AppPage::Home && page_key_handler_ && page_key_handler_(key)) {
        return true;
    }

    // El tema se cambia desde cualquier pantalla.
    if (key == 't' || key == 'T') {
        toggle_dark_mode();
        return true;
    }

    if (current_page() != model::AppPage::Home) {
        return false;
    }

    switch (key) {
        // Las flechas llevan pintadas F/Z/X/C y llegan también como letras (con o sin Fn).
        // En un menú vertical cualquiera de ellas sirve para moverse entre las dos entradas.
        case LV_KEY_UP:
        case LV_KEY_LEFT:
        case 'f':
        case 'F':
        case 'z':
        case 'Z':
            move_selection(-1);
            return true;
        case LV_KEY_DOWN:
        case LV_KEY_RIGHT:
        case 'x':
        case 'X':
        case 'c':
        case 'C':
            move_selection(1);
            return true;
        case LV_KEY_ENTER:
        case '\r':
        case ' ':
            go_to(page_for_menu(menu_index_subject_.value()));
            return true;
        default:
            return false;
    }
}

bool BaseViewModel::back() {
    if (current_page() == model::AppPage::Home) {
        return false;
    }
    // La herramienta puede quedarse (p. ej. parar una grabación) en vez de volver al menú.
    if (page_back_handler_ && page_back_handler_()) {
        return true;
    }
    go_to(model::AppPage::Home);
    return true;
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

} // namespace viewmodel

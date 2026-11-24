/*
 * SPDX-License-Identifier: MIT
 */

#include "subghz_screen.h"

#include "asset_manager.h"
#include "name_edit.h"
#include "subghz_decode.h"
#include "subghz_file.h"
#include "subghz_storage.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>

namespace screen {
namespace {

using State = platform::Cc1101::State;

constexpr int32_t kMargin = 8;
constexpr int32_t kRowHeight = 24;
constexpr int32_t kRowGap = 2;
constexpr uint32_t kPollMs = 100;
constexpr int kReplayRepeats = 2; // reemitir repite la trama, como hacen los mandos

} // namespace

SubGhzScreen::SubGhzScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
    this->view_model().set_page_handlers([this](uint32_t key) { return handle_key(key); },
                                         [this]() { return handle_back(); });
}

SubGhzScreen::~SubGhzScreen() {
    view_model().clear_page_handlers();
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
    }
}

void SubGhzScreen::build_content(lv_obj_t* content) {
    const lv_font_t* row_font = assets().load_standard_font(14, app::StandardFontWeight::Regular);
    const lv_font_t* headline_font = assets().load_standard_font(22);
    const lv_font_t* text_font = assets().load_standard_font(13, app::StandardFontWeight::Regular);
    const lv_font_t* hint_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    const int32_t width = view::kScreenWidth - 2 * kMargin;

    for (int i = 0; i < kVisibleRows; ++i) {
        auto* row = lv_obj_create(content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, width, kRowHeight);
        lv_obj_set_pos(row, kMargin, 4 + i * (kRowHeight + kRowGap));
        lv_obj_set_style_radius(row, 5, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        rows_[i] = row;

        auto* label = lv_label_create(row);
        lv_obj_set_style_text_font(label, row_font ? row_font : &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, width - 20);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
        row_labels_[i] = label;
    }

    headline_ = lv_label_create(content);
    lv_obj_set_style_text_font(headline_, headline_font ? headline_font : &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(headline_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(headline_, width);
    lv_obj_align(headline_, LV_ALIGN_TOP_MID, 0, 30);

    detail_ = lv_label_create(content);
    lv_obj_set_style_text_font(detail_, text_font ? text_font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(detail_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(detail_, width);
    lv_obj_align(detail_, LV_ALIGN_TOP_MID, 0, 66);

    status_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(status_label_, hint_font ? hint_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(status_label_, width);
    lv_obj_align(status_label_, LV_ALIGN_BOTTOM_MID, 0, -20);

    hint_ = lv_label_create(content);
    lv_obj_set_style_text_font(hint_, hint_font ? hint_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint_, width);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -4);

    reload_signals();
    lv_subject_add_observer_obj(view_model().dark_mode_subject(),
                                [](lv_observer_t* o, lv_subject_t*) {
                                    auto* s = static_cast<SubGhzScreen*>(lv_observer_get_user_data(o));
                                    if (s) s->refresh();
                                },
                                content,
                                this);
    poll_timer_ = lv_timer_create(poll_timer_cb, kPollMs, this);
}

int SubGhzScreen::item_count() const {
    return 1 + static_cast<int>(names_.size());
}

void SubGhzScreen::reload_signals() {
    names_ = platform::list_signals();
    info_cache_.clear();
    selection_ = std::clamp(selection_, 0, item_count() - 1);
    ensure_visible();
}

void SubGhzScreen::ensure_visible() {
    if (selection_ < offset_) {
        offset_ = selection_;
    } else if (selection_ >= offset_ + kVisibleRows) {
        offset_ = selection_ - kVisibleRows + 1;
    }
    const int max_offset = std::max(0, item_count() - kVisibleRows);
    offset_ = std::clamp(offset_, 0, max_offset);
}

void SubGhzScreen::start_receive() {
    status_line_.clear();
    mode_ = Mode::Receiving;
    radio_.start_receive(kFrequencyHz);
}

void SubGhzScreen::start_transmit() {
    if (selection_ <= 0 || selection_ - 1 >= static_cast<int>(names_.size())) {
        return;
    }
    const std::string name = names_[selection_ - 1];
    subghz::Signal signal;
    std::string error;
    if (!subghz::load_sub_file(platform::signal_path(name), signal, error)) {
        status_line_ = "Load failed: " + error;
        return;
    }
    tx_name_ = name;
    tx_rolling_ = subghz::decode(signal).rolling;
    status_line_.clear();
    mode_ = Mode::Transmitting;
    radio_.start_transmit(signal, kReplayRepeats);
}

void SubGhzScreen::delete_selected() {
    if (selection_ <= 0 || selection_ - 1 >= static_cast<int>(names_.size())) {
        return;
    }
    const std::string name = names_[selection_ - 1];
    platform::delete_signal(name);
    reload_signals();
    status_line_ = "Deleted " + name;
}

void SubGhzScreen::start_rename() {
    if (selection_ <= 0 || selection_ - 1 >= static_cast<int>(names_.size())) {
        return;
    }
    rename_from_ = names_[selection_ - 1];
    rename_buffer_.clear();  // se empieza en blanco; ENTER sin escribir deja el nombre igual
    status_line_.clear();
    mode_ = Mode::Rename;
}

void SubGhzScreen::finish_rename() {
    mode_ = Mode::List;
    const std::string name = text::clean_file_name(rename_buffer_);
    if (name.empty() || name == rename_from_) {
        return;
    }
    if (!platform::rename_signal(rename_from_, name)) {
        status_line_ = "Name already used";
        return;
    }
    reload_signals();
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) {
            selection_ = static_cast<int>(i) + 1;
            break;
        }
    }
    ensure_visible();
    status_line_ = "Renamed";
}

void SubGhzScreen::on_received() {
    const subghz::Signal signal = radio_.captured();
    const subghz::Decoded decoded = subghz::decode(signal);

    const std::string name = platform::unique_signal_name(subghz::suggested_name(decoded, signal.timings.size()));

    std::string error;
    if (!subghz::save_sub_file(platform::signal_path(name), signal, error)) {
        status_line_ = "Save failed: " + error;
        mode_ = Mode::List;
        return;
    }

    reload_signals();
    // Dejar seleccionada la señal recién guardada.
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) {
            selection_ = static_cast<int>(i) + 1;
            break;
        }
    }
    ensure_visible();
    status_line_ = "Saved: " + subghz::describe(decoded);
    mode_ = Mode::List;
}

bool SubGhzScreen::handle_key(uint32_t key) {
    if (mode_ == Mode::Rename) {
        if (key == LV_KEY_ENTER || key == '\r') {
            finish_rename();
        } else if (!text::edit_name(rename_buffer_, key)) {
            return true;  // en modo texto no se escapan atajos (salvo ESC, que va por «atrás»)
        }
        refresh();
        return true;
    }
    if (mode_ != Mode::List) {
        if (key == LV_KEY_ENTER || key == '\r' || key == ' ') {
            // Grabando: parar y quedarse con lo que haya llegado (ESC es lo que cancela).
            if (mode_ == Mode::Receiving) radio_.finish_receive(); else radio_.stop();
            return true;
        }
        return false;
    }

    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_LEFT:
        case 'f':
        case 'F':
        case 'z':
        case 'Z':
            if (selection_ > 0) {
                --selection_;
                status_line_.clear();
                ensure_visible();
                refresh();
            }
            return true;
        case LV_KEY_DOWN:
        case LV_KEY_RIGHT:
        case 'x':
        case 'X':
        case 'c':
        case 'C':
            if (selection_ < item_count() - 1) {
                ++selection_;
                status_line_.clear();
                ensure_visible();
                refresh();
            }
            return true;
        case LV_KEY_ENTER:
        case '\r':
        case ' ':
            if (selection_ == 0) {
                start_receive();
            } else {
                start_transmit();
            }
            refresh();
            return true;
        case 'd':
        case 'D':
            delete_selected();
            refresh();
            return true;
        case 'r':
        case 'R':
            if (selection_ > 0) {
                start_rename();
                refresh();
            }
            return true;
        default:
            return false;
    }
}

bool SubGhzScreen::handle_back() {
    if (mode_ == Mode::List) {
        return false; // nada que parar: que se vuelva al menú
    }
    if (mode_ == Mode::Rename) {
        mode_ = Mode::List;  // cancelar sin cambiar el nombre
        refresh();
        return true;
    }
    radio_.stop();
    mode_ = Mode::List;
    status_line_ = "Cancelled";
    refresh();
    return true;
}

void SubGhzScreen::poll() {
    const auto status = radio_.status();
    if (mode_ == Mode::Receiving) {
        if (status.state == State::Received) {
            on_received();
        } else if (status.state == State::Stopped || status.state == State::Failed) {
            status_line_ = status.state == State::Failed ? ("No radio: " + status.error) : "Nothing recorded";
            mode_ = Mode::List;
        }
    } else if (mode_ == Mode::Transmitting) {
        if (status.state == State::Done) {
            // Un código variable ya usado lo ignora el receptor: se dice, para no buscar el fallo.
            status_line_ = tx_rolling_ ? "Sent, but rolling code: it won't open" : "Sent " + tx_name_;
            mode_ = Mode::List;
        } else if (status.state == State::Stopped || status.state == State::Failed) {
            status_line_ = status.state == State::Failed ? ("No radio: " + status.error) : "Stopped";
            mode_ = Mode::List;
        }
    }
    refresh();
}

void SubGhzScreen::refresh() {
    if (!hint_) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    const bool list = mode_ == Mode::List;

    if (mode_ == Mode::Rename) {
        for (auto* row : rows_) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(headline_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(detail_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(headline_, (rename_buffer_ + "_").c_str());
        lv_label_set_text(detail_, ("Now: " + rename_from_).c_str());
        lv_obj_set_style_text_color(headline_, colors.primary, 0);
        lv_obj_set_style_text_color(detail_, colors.text_disabled, 0);
        lv_label_set_text(hint_, "ENTER save    DEL erase    ESC cancel");
        lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
        return;
    }

    for (int i = 0; i < kVisibleRows; ++i) {
        const int idx = offset_ + i;
        const bool visible = list && idx < item_count();
        if (!visible) {
            lv_obj_add_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
        const bool selected = idx == selection_;
        lv_label_set_text(row_labels_[i], idx == 0 ? "+  Record new" : names_[idx - 1].c_str());
        lv_obj_set_style_bg_color(rows_[i], selected ? colors.primary : colors.surface, 0);
        lv_obj_set_style_text_color(row_labels_[i], selected ? colors.background : colors.text, 0);
    }

    if (list) {
        lv_obj_add_flag(headline_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        // Lo último que pasó (guardado, enviado…) y, si no, qué es la señal seleccionada.
        const bool info = status_line_.empty() && selection_ > 0;
        lv_label_set_text(status_label_, info ? selected_info().c_str() : status_line_.c_str());
        lv_obj_set_style_text_color(status_label_, info ? colors.text_disabled : colors.success, 0);
        lv_label_set_text(hint_,
                          selection_ == 0 ? "ENTER record      F / X move"
                                          : "ENTER replay   R rename   D delete");
    } else {
        lv_obj_remove_flag(headline_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(detail_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

        const auto status = radio_.status();
        const int dots = static_cast<int>(status.activity % 4);
        char detail[64];
        if (mode_ == Mode::Receiving) {
            lv_label_set_text(headline_, radio_.is_simulated() ? "Listening (sim)" : "Listening");
            if (status.heard > 0) {
                std::snprintf(detail, sizeof(detail), "433.92 MHz   heard %d/%d", std::min(status.heard, platform::Cc1101::kRepeatsNeeded),
                              platform::Cc1101::kRepeatsNeeded);
            } else {
                std::snprintf(detail, sizeof(detail), "433.92 MHz %.*s", dots, "...");
            }
        } else {
            lv_label_set_text(headline_, radio_.is_simulated() ? "Sending (sim)" : "Sending");
            std::snprintf(detail, sizeof(detail), "%s %.*s", tx_name_.c_str(), dots, "...");
        }
        lv_label_set_text(detail_, detail);
        lv_obj_set_style_text_color(headline_, colors.primary, 0);
        lv_obj_set_style_text_color(detail_, colors.text, 0);
        lv_label_set_text(hint_, mode_ == Mode::Receiving ? "Press the remote   ENTER done   ESC cancel"
                                                          : "ENTER / ESC  stop");
    }

    lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
}

const std::string& SubGhzScreen::selected_info() {
    static const std::string empty;
    if (selection_ <= 0 || selection_ - 1 >= static_cast<int>(names_.size())) {
        return empty;
    }
    const std::string& name = names_[selection_ - 1];
    auto it = info_cache_.find(name);
    if (it == info_cache_.end()) {
        subghz::Signal signal;
        std::string error;
        const std::string text = subghz::load_sub_file(platform::signal_path(name), signal, error)
                                     ? subghz::describe(subghz::decode(signal))
                                     : "Can't read: " + error;
        it = info_cache_.emplace(name, text).first;
    }
    return it->second;
}

void SubGhzScreen::poll_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<SubGhzScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->poll();
    }
}

} // namespace screen

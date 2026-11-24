/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_screen.h"

#include "asset_manager.h"
#include "name_edit.h"
#include "nfc_file.h"
#include "nfc_ndef.h"
#include "nfc_storage.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>

namespace screen {
namespace {

using State = platform::NfcSt25r3916::State;

static_assert(LV_KEY_BACKSPACE == text::kBackspace, "el modelo de texto supone Backspace = 8");

constexpr int32_t kMargin = 8;
constexpr int32_t kRowHeight = 24;
constexpr int32_t kRowGap = 2;
constexpr uint32_t kPollMs = 100;

// Resumen del primer registro NDEF de una tarjeta Type 2 (UL/NTAG), si lo hay.
std::string ndef_summary(const nfc::Card& card) {
    if (card.block_size != 4 || card.memory.empty()) {
        return "";
    }
    nfc::NdefMessage message;
    if (!nfc::parse_type2_ndef(card.memory, message) || message.records.empty()) {
        return "";
    }
    const auto& record = message.records[0];
    switch (record.type) {
        case nfc::NdefRecordType::Uri:
            return "URI: " + record.content;
        case nfc::NdefRecordType::Text:
            return "Text: " + record.content;
        case nfc::NdefRecordType::Empty:
            return "NDEF: empty";
        default:
            return "NDEF: " + (record.type_name.empty() ? std::string("record") : record.type_name);
    }
}

std::string hex_no_spaces(const std::vector<uint8_t>& bytes) {
    std::string hex = nfc::to_hex(bytes);
    hex.erase(std::remove(hex.begin(), hex.end(), ' '), hex.end());
    return hex;
}

} // namespace

NfcScreen::NfcScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
    this->view_model().set_page_handlers([this](uint32_t key) { return handle_key(key); },
                                         [this]() { return handle_back(); });
}

NfcScreen::~NfcScreen() {
    view_model().clear_page_handlers();
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
    }
}

void NfcScreen::build_content(lv_obj_t* content) {
    const lv_font_t* row_font = assets().load_standard_font(14, app::StandardFontWeight::Regular);
    const lv_font_t* headline_font = assets().load_standard_font(20);
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
    lv_obj_set_style_text_font(headline_, headline_font ? headline_font : &lv_font_montserrat_20, 0);
    lv_obj_set_width(headline_, width);
    lv_label_set_long_mode(headline_, LV_LABEL_LONG_DOT);
    lv_obj_align(headline_, LV_ALIGN_TOP_MID, 0, 24);

    const struct {
        lv_obj_t** slot;
        int32_t y;
    } lines[] = {{&line1_, 56}, {&line2_, 78}, {&line3_, 100}};
    for (const auto& def : lines) {
        auto* label = lv_label_create(content);
        lv_obj_set_style_text_font(label, text_font ? text_font : &lv_font_montserrat_14, 0);
        lv_obj_set_width(label, width);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, def.y);
        *def.slot = label;
    }

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

    reload_cards();
    lv_subject_add_observer_obj(view_model().dark_mode_subject(),
                                [](lv_observer_t* o, lv_subject_t*) {
                                    auto* s = static_cast<NfcScreen*>(lv_observer_get_user_data(o));
                                    if (s) s->refresh();
                                },
                                content,
                                this);
    poll_timer_ = lv_timer_create(poll_timer_cb, kPollMs, this);
}

int NfcScreen::item_count() const {
    return 1 + static_cast<int>(names_.size());
}

void NfcScreen::reload_cards() {
    names_ = platform::list_cards();
    selection_ = std::clamp(selection_, 0, item_count() - 1);
    ensure_visible();
}

void NfcScreen::ensure_visible() {
    if (selection_ < offset_) {
        offset_ = selection_;
    } else if (selection_ >= offset_ + kVisibleRows) {
        offset_ = selection_ - kVisibleRows + 1;
    }
    offset_ = std::clamp(offset_, 0, std::max(0, item_count() - kVisibleRows));
}

void NfcScreen::start_scan() {
    status_line_.clear();
    mode_ = Mode::Scanning;
    reader_.start_scan();
}

void NfcScreen::open_selected() {
    if (selection_ <= 0 || selection_ - 1 >= static_cast<int>(names_.size())) {
        return;
    }
    const std::string name = names_[selection_ - 1];
    nfc::Card card;
    std::string error;
    if (!nfc::load_nfc_file(platform::card_path(name), card, error)) {
        status_line_ = "Load failed: " + error;
        return;
    }
    current_card_ = std::move(card);
    current_name_ = name;
    status_line_.clear();
    mode_ = Mode::Detail;
}

void NfcScreen::start_emulate() {
    if (current_card_.uid.empty()) {
        return;
    }
    mode_ = Mode::Emulating;
    reader_.start_emulate(current_card_);
}

void NfcScreen::start_write() {
    if (current_card_.block_size != 4 || current_card_.memory.empty()) {
        status_line_ = "Only NTAG/Ultralight cards can be written";
        return;
    }
    status_line_.clear();
    mode_ = Mode::Writing;
    reader_.start_write(current_card_);
}

void NfcScreen::delete_current() {
    if (current_name_.empty()) {
        return;
    }
    platform::delete_card(current_name_);
    const std::string deleted = current_name_;
    current_name_.clear();
    current_card_ = nfc::Card{};
    reload_cards();
    mode_ = Mode::List;
    status_line_ = "Deleted " + deleted;
}

void NfcScreen::start_rename() {
    if (current_name_.empty()) {
        return;
    }
    // Se empieza en blanco: con el nombre automático («card-04DEAD62D42A81») habría que borrar
    // 19 letras antes de escribir. ENTER sin escribir nada deja el nombre como estaba.
    rename_buffer_.clear();
    status_line_.clear();
    mode_ = Mode::Rename;
}

void NfcScreen::finish_rename() {
    mode_ = Mode::Detail;
    const std::string name = text::clean_file_name(rename_buffer_);
    if (name.empty() || name == current_name_) {
        return;
    }
    if (!platform::rename_card(current_name_, name)) {
        status_line_ = "Name already used";
        return;
    }
    current_name_ = name;
    reload_cards();
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) {
            selection_ = static_cast<int>(i) + 1;
            break;
        }
    }
    ensure_visible();
    status_line_ = "Renamed";
}

void NfcScreen::on_found() {
    const nfc::Card card = reader_.scanned();
    const std::string base = "card-" + hex_no_spaces(card.uid);
    const std::string name = platform::unique_card_name(base);

    std::string error;
    if (!nfc::save_nfc_file(platform::card_path(name), card, error)) {
        status_line_ = "Save failed: " + error;
        mode_ = Mode::List;
        return;
    }
    current_card_ = card;
    current_name_ = name;
    reload_cards();
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) {
            selection_ = static_cast<int>(i) + 1;
            break;
        }
    }
    ensure_visible();
    status_line_ = std::string("Saved ") + nfc::card_type_name(card.type);
    mode_ = Mode::Detail;
}

bool NfcScreen::handle_key(uint32_t key) {
    if (mode_ == Mode::Scanning || mode_ == Mode::Emulating || mode_ == Mode::Writing) {
        if (key == LV_KEY_ENTER || key == '\r' || key == ' ') {
            reader_.stop();
            return true;
        }
        return false;
    }

    if (mode_ == Mode::Rename) {
        if (key == LV_KEY_ENTER || key == '\r') {
            finish_rename();
        } else if (!text::edit_name(rename_buffer_, key)) {
            return true; // en modo texto no se escapan atajos (salvo ESC, que va por «atrás»)
        }
        refresh();
        return true;
    }

    if (mode_ == Mode::Detail) {
        status_line_.clear();
        switch (key) {
            case 'r':
            case 'R':
                start_rename();
                refresh();
                return true;
            case LV_KEY_ENTER:
            case '\r':
            case ' ':
                start_emulate();
                refresh();
                return true;
            case 'd':
            case 'D':
                delete_current();
                refresh();
                return true;
            case 'w':
            case 'W':
                start_write();
                refresh();
                return true;
            default:
                return false;
        }
    }

    // Mode::List
    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_LEFT:
        case 'f':
        case 'F':
        case 'z':
        case 'Z':
            if (selection_ > 0) {
                --selection_;
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
                ensure_visible();
                refresh();
            }
            return true;
        case LV_KEY_ENTER:
        case '\r':
        case ' ':
            if (selection_ == 0) {
                start_scan();
            } else {
                open_selected();
            }
            refresh();
            return true;
        case 'd':
        case 'D':
            if (selection_ > 0) {
                current_name_ = names_[selection_ - 1];
                delete_current();
                refresh();
            }
            return true;
        default:
            return false;
    }
}

bool NfcScreen::handle_back() {
    switch (mode_) {
        case Mode::Scanning:
            reader_.stop();
            mode_ = Mode::List;
            status_line_ = "Cancelled";
            refresh();
            return true;
        case Mode::Emulating:
        case Mode::Writing:
            reader_.stop();
            mode_ = Mode::Detail;
            refresh();
            return true;
        case Mode::Rename:
            mode_ = Mode::Detail; // cancelar sin cambiar el nombre
            refresh();
            return true;
        case Mode::Detail:
            mode_ = Mode::List;
            refresh();
            return true;
        case Mode::List:
        default:
            return false;
    }
}

void NfcScreen::poll() {
    const auto status = reader_.status();
    if (mode_ == Mode::Scanning) {
        if (status.state == State::Found) {
            on_found();
        } else if (status.state == State::Stopped || status.state == State::Failed) {
            status_line_ = status.state == State::Failed ? ("No reader: " + status.error) : "Cancelled";
            mode_ = Mode::List;
        }
    } else if (mode_ == Mode::Writing) {
        if (status.state == State::Written) {
            status_line_ = status.message;
            mode_ = Mode::Detail;
        } else if (status.state == State::Failed) {
            status_line_ = "Write failed: " + status.error;
            mode_ = Mode::Detail;
        } else if (status.state == State::Stopped) {
            mode_ = Mode::Detail;
        }
    } else if (mode_ == Mode::Emulating) {
        if (status.state == State::Stopped) {
            mode_ = Mode::Detail;
        } else if (status.state == State::Failed) {
            status_line_ = "No reader: " + status.error;
            mode_ = Mode::Detail;
        }
    }
    refresh();
}

void NfcScreen::refresh() {
    if (!hint_) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    const bool list = mode_ == Mode::List;

    for (int i = 0; i < kVisibleRows; ++i) {
        const int idx = offset_ + i;
        const bool visible = list && idx < item_count();
        if (!visible) {
            lv_obj_add_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
        const bool selected = idx == selection_;
        lv_label_set_text(row_labels_[i], idx == 0 ? "+  Scan a card" : names_[idx - 1].c_str());
        lv_obj_set_style_bg_color(rows_[i], selected ? colors.primary : colors.surface, 0);
        lv_obj_set_style_text_color(row_labels_[i], selected ? colors.background : colors.text, 0);
    }

    const auto show = [](lv_obj_t* obj, bool visible) {
        if (visible) {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    };

    show(status_label_, list);
    show(headline_, !list);

    if (list) {
        show(line1_, false);
        show(line2_, false);
        show(line3_, false);
        lv_label_set_text(status_label_, status_line_.c_str());
        lv_obj_set_style_text_color(status_label_, colors.success, 0);
        lv_label_set_text(hint_, selection_ == 0 ? "ENTER  scan        F / X  move"
                                                 : "ENTER  open     D  delete     F / X  move");
    } else if (mode_ == Mode::Detail) {
        show(line1_, true);
        show(line2_, true);
        show(line3_, true);
        lv_obj_set_style_text_align(headline_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_align(line1_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_align(line2_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_align(line3_, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(headline_, current_name_.c_str());
        lv_label_set_text(line1_, (std::string("Type: ") + nfc::card_product(current_card_)).c_str());
        lv_label_set_text(line2_, (std::string("UID: ") + nfc::to_hex(current_card_.uid)).c_str());
        // Tercera línea: el NDEF si lo hay; si no, el ATS (DESFire); si no, nada.
        const std::string ndef = ndef_summary(current_card_);
        std::string third = ndef;
        if (third.empty() && !current_card_.ats.empty()) {
            third = "ATS: " + nfc::to_hex(current_card_.ats);
        }
        lv_label_set_text(line3_, third.empty() ? "No NDEF" : third.c_str());
        lv_obj_set_style_text_color(headline_, colors.primary, 0);
        lv_obj_set_style_text_color(line1_, colors.text, 0);
        lv_obj_set_style_text_color(line2_, colors.text, 0);
        lv_obj_set_style_text_color(line3_, colors.text, 0);
        // En el detalle no hay sitio para otra línea: el aviso («Saved…», «Renamed») ocupa la
        // de ayuda hasta la siguiente tecla.
        lv_label_set_text(hint_, status_line_.empty() ? "ENTER emulate  W write  R rename  D delete"
                                                      : status_line_.c_str());
        const bool failed = status_line_.rfind("Write failed", 0) == 0 || status_line_.rfind("Only ", 0) == 0;
        lv_obj_set_style_text_color(hint_, status_line_.empty() ? colors.text_disabled
                                           : failed           ? colors.error
                                                              : colors.success, 0);
        return;
    } else if (mode_ == Mode::Rename) {
        show(line1_, true);
        show(line2_, true);
        show(line3_, false);
        lv_obj_set_style_text_align(headline_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_align(line1_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_align(line2_, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(headline_, (rename_buffer_ + "_").c_str());
        lv_label_set_text(line1_, "Type a new name");
        lv_label_set_text(line2_, ("Now: " + current_name_).c_str());
        lv_obj_set_style_text_color(headline_, colors.primary, 0);
        lv_obj_set_style_text_color(line1_, colors.text, 0);
        lv_obj_set_style_text_color(line2_, colors.text_disabled, 0);
        lv_label_set_text(hint_, "ENTER save     DEL erase     ESC cancel");
    } else { // Scanning / Emulating / Writing
        show(line1_, true);
        show(line2_, false);
        show(line3_, false);
        lv_obj_set_style_text_align(headline_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(line1_, LV_TEXT_ALIGN_CENTER, 0);
        if (mode_ == Mode::Scanning) {
            lv_label_set_text(headline_, reader_.is_simulated() ? "Searching (sim)" : "Searching");
            lv_label_set_text(line1_, "Hold a card near the cap");
        } else if (mode_ == Mode::Writing) {
            lv_label_set_text(headline_, reader_.is_simulated() ? "Writing (sim)" : "Writing");
            lv_label_set_text(line1_, "Hold a blank NTAG near the cap");
        } else {
            lv_label_set_text(headline_, reader_.is_simulated() ? "Emulating (sim)" : "Emulating");
            lv_label_set_text(line1_, current_name_.c_str());
        }
        lv_obj_set_style_text_color(headline_, colors.primary, 0);
        lv_obj_set_style_text_color(line1_, colors.text, 0);
        lv_label_set_text(hint_, "ENTER / ESC  stop");
    }

    lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
}

void NfcScreen::poll_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<NfcScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->poll();
    }
}

} // namespace screen

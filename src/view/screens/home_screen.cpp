/*
 * SPDX-License-Identifier: MIT
 */

#include "home_screen.h"

#include "asset_manager.h"
#include "theme.h"

#include <array>
#include <cstddef>

namespace screen {
namespace {

struct Entry {
    const char* name;
    const char* subtitle;
};

// El orden tiene que coincidir con BaseViewModel::page_for_menu.
constexpr std::array<Entry, 2> kEntries = {{
    {"Sub-GHz", "Record & replay 433 MHz remotes"},
    {"NFC", "Read, write & emulate cards"},
}};

constexpr int32_t kMargin = 10;
constexpr int32_t kCardGap = 8;
constexpr int32_t kCardWidth = view::kScreenWidth - 2 * kMargin;
constexpr int32_t kCardHeight = 46;

} // namespace

HomeScreen::HomeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

HomeScreen::~HomeScreen() = default;

void HomeScreen::build_content(lv_obj_t* content) {
    const lv_font_t* name_font = assets().load_standard_font(18);
    const lv_font_t* subtitle_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    const lv_font_t* hint_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);

    for (std::size_t i = 0; i < kEntries.size(); ++i) {
        auto* card = lv_obj_create(content);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, kCardWidth, kCardHeight);
        lv_obj_set_pos(card, kMargin, kMargin + static_cast<int32_t>(i) * (kCardHeight + kCardGap));
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        cards_[i] = card;

        auto* name = lv_label_create(card);
        lv_label_set_text(name, kEntries[i].name);
        lv_obj_set_style_text_font(name, name_font ? name_font : &lv_font_montserrat_18, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 12, 6);
        names_[i] = name;

        auto* subtitle = lv_label_create(card);
        lv_label_set_text(subtitle, kEntries[i].subtitle);
        lv_obj_set_style_text_font(subtitle, subtitle_font ? subtitle_font : &lv_font_montserrat_12, 0);
        lv_obj_align(subtitle, LV_ALIGN_BOTTOM_LEFT, 12, -6);
        subtitles_[i] = subtitle;
    }

    hint_ = lv_label_create(content);
    lv_obj_set_style_text_font(hint_, hint_font ? hint_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint_, view::kScreenWidth - 2 * kMargin);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -4);

    // Al añadir cada observador LVGL lo llama una vez: eso hace el primer pintado.
    lv_subject_add_observer_obj(view_model().menu_index_subject(), refresh_observer, content, this);
    lv_subject_add_observer_obj(view_model().dark_mode_subject(), refresh_observer, content, this);
}

void HomeScreen::refresh() {
    if (!hint_) {
        return; // primer aviso de un observador, con la pantalla a medio montar
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    const int selected = view_model().menu_index();

    for (std::size_t i = 0; i < cards_.size(); ++i) {
        const bool is_selected = static_cast<int>(i) == selected;
        lv_obj_set_style_bg_color(cards_[i], is_selected ? colors.primary : colors.surface, 0);
        lv_obj_set_style_border_color(cards_[i], is_selected ? colors.primary : colors.border, 0);
        // Sobre el color de selección el texto va con el color de fondo, que contrasta.
        lv_obj_set_style_text_color(names_[i], is_selected ? colors.background : colors.text, 0);
        lv_obj_set_style_text_color(
            subtitles_[i], is_selected ? colors.background : colors.text_disabled, 0);
    }

    lv_label_set_text(hint_, "ENTER  open      F / X  move      HOLD ESC  exit");
    lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
}

void HomeScreen::refresh_observer(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<HomeScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

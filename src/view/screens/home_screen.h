/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

/**
 * El menú: dos tarjetas, una por herramienta del Cap CC1101+NFC (Sub-GHz y NFC). La
 * resaltada la lleva el viewmodel (`menu_index`); ENTER entra en ella.
 */
class HomeScreen : public BaseScreen {
public:
    HomeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~HomeScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    void refresh();
    static void refresh_observer(lv_observer_t* observer, lv_subject_t* subject);

    std::array<lv_obj_t*, 2> cards_{};
    std::array<lv_obj_t*, 2> names_{};
    std::array<lv_obj_t*, 2> subtitles_{};
    lv_obj_t* hint_{nullptr};
};

} // namespace screen

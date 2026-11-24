/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "nfc_card.h"
#include "nfc_st25r3916.h"

#include <array>
#include <string>
#include <vector>

namespace screen {

/**
 * Herramienta NFC: escanear una tarjeta, ver su tipo/UID/NDEF, guardarla, emularla y
 * borrarla, con el ST25R3916. En el escritorio el lector es simulado (ver
 * `platform::NfcSt25r3916`); en el aparato hablará con el chip de verdad.
 */
class NfcScreen : public BaseScreen {
public:
    static constexpr int kVisibleRows = 4;

    NfcScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~NfcScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    enum class Mode { List, Scanning, Detail, Emulating, Writing, Rename };

    bool handle_key(uint32_t key);
    bool handle_back();
    void poll();
    void refresh();

    void reload_cards();
    void ensure_visible();
    int item_count() const; // «Scan» + tarjetas guardadas
    void start_scan();
    void open_selected();
    void start_emulate();
    void start_write();
    void delete_current();
    void start_rename();
    void finish_rename();
    void on_found();

    static void poll_timer_cb(lv_timer_t* timer);

    platform::NfcSt25r3916 reader_;
    std::vector<std::string> names_;
    int selection_ = 0; // 0 = «Scan», 1.. = tarjeta guardada
    int offset_ = 0;
    Mode mode_ = Mode::List;
    std::string status_line_;
    nfc::Card current_card_;
    std::string current_name_;
    std::string rename_buffer_;

    std::array<lv_obj_t*, kVisibleRows> rows_{};
    std::array<lv_obj_t*, kVisibleRows> row_labels_{};
    lv_obj_t* headline_{nullptr};
    lv_obj_t* line1_{nullptr};
    lv_obj_t* line2_{nullptr};
    lv_obj_t* line3_{nullptr};
    lv_obj_t* status_label_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_timer_t* poll_timer_{nullptr};
};

} // namespace screen

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "cc1101.h"

#include <array>
#include <string>
#include <vector>

namespace screen {

/**
 * Herramienta Sub-GHz: grabar, guardar, reemitir y borrar mandos de 433 MHz con el CC1101.
 * En el escritorio el CC1101 es simulado (ver `platform::Cc1101`); en el aparato hablará con
 * el radio de verdad. La pantalla sondea el radio cada 100 ms y nunca toca su hilo.
 */
class SubGhzScreen : public BaseScreen {
public:
    static constexpr int kVisibleRows = 4;
    static constexpr uint32_t kFrequencyHz = 433920000; // 433.92 MHz, la banda de los mandos

    SubGhzScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~SubGhzScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    enum class Mode { List, Receiving, Transmitting, Rename };

    bool handle_key(uint32_t key);
    bool handle_back();
    void poll();
    void refresh();

    void reload_signals();
    void ensure_visible();
    int item_count() const; // «Grabar» + señales guardadas
    void start_receive();
    void start_transmit();
    void delete_selected();
    void start_rename();
    void finish_rename();
    void on_received();

    static void poll_timer_cb(lv_timer_t* timer);

    platform::Cc1101 radio_;
    std::vector<std::string> names_;
    int selection_ = 0; // 0 = «Grabar», 1.. = señal guardada
    int offset_ = 0;
    Mode mode_ = Mode::List;
    std::string status_line_;
    std::string tx_name_;
    std::string rename_buffer_;
    std::string rename_from_;

    std::array<lv_obj_t*, kVisibleRows> rows_{};
    std::array<lv_obj_t*, kVisibleRows> row_labels_{};
    lv_obj_t* headline_{nullptr};
    lv_obj_t* detail_{nullptr};
    lv_obj_t* status_label_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_timer_t* poll_timer_{nullptr};
};

} // namespace screen

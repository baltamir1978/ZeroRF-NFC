/*
 * SPDX-License-Identifier: MIT
 */

#include "subghz_signal.h"

namespace subghz {

const char* modulation_preset(Modulation modulation) {
    switch (modulation) {
        case Modulation::Fsk2:
            return "FuriHalSubGhzPreset2FSKDev238Async";
        case Modulation::Ook:
        default:
            return "FuriHalSubGhzPresetOok650Async";
    }
}

bool parse_modulation_preset(std::string_view preset, Modulation& modulation) {
    // Nos basta con distinguir OOK de FSK: el Flipper tiene varios presets de cada uno.
    if (preset.find("Ook") != std::string_view::npos ||
        preset.find("OOK") != std::string_view::npos) {
        modulation = Modulation::Ook;
        return true;
    }
    if (preset.find("FSK") != std::string_view::npos ||
        preset.find("Fsk") != std::string_view::npos) {
        modulation = Modulation::Fsk2;
        return true;
    }
    return false;
}

} // namespace subghz

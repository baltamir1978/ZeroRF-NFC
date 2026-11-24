/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_type2.h"

#include <algorithm>

namespace nfc {

Type2Layout type2_layout(const std::vector<uint8_t>& version, std::size_t dumped_pages) {
    Type2Layout l;
    if (version.size() >= 8) {
        const bool ntag = version[2] == 0x04; // tipo de producto: 0x03 Ultralight, 0x04 NTAG
        // Byte 6 = tamaño de la memoria de usuario (datasheets de NXP). Los NTAG210/212 tienen
        // la misma disposición que las Ultralight EV1 de su tamaño.
        switch (version[6]) {
            case 0x0B: l = {ntag ? "NTAG210" : "Ultralight EV1 (MF0UL11)", 20, 4, 15}; break;
            case 0x0E: l = {ntag ? "NTAG212" : "Ultralight EV1 (MF0UL21)", 41, 4, 35}; break;
            case 0x0F: l = {"NTAG213", 45, 4, 39}; break;
            case 0x11: l = {"NTAG215", 135, 4, 129}; break;
            case 0x13: l = {"NTAG216", 231, 4, 225}; break;
            default: break;
        }
        return l;
    }
    // Sin GET_VERSION (Ultralight original, o tarjetas guardadas antes de la 0.3.0, que no lo
    // apuntaban): el número de páginas volcadas es distinto en cada modelo.
    switch (dumped_pages) {
        case 16: l = {"Ultralight", 16, 4, 15}; break;
        case 20: l = {"Ultralight EV1 / NTAG210", 20, 4, 15}; break;
        case 41: l = {"Ultralight EV1 / NTAG212", 41, 4, 35}; break;
        case 45: l = {"NTAG213", 45, 4, 39}; break;
        case 135: l = {"NTAG215", 135, 4, 129}; break;
        case 231: l = {"NTAG216", 231, 4, 225}; break;
        default: break;
    }
    return l;
}

Type2Layout type2_layout(const Card& card) {
    const std::size_t pages = card.block_size == 4 ? card.memory.size() / 4 : 0;
    return type2_layout(card.version, pages);
}

bool type2_static_locked(const std::array<uint8_t, 4>& page2) {
    return page2[2] != 0 || page2[3] != 0;
}

Type2WritePlan plan_type2_write(const Card& source, const Type2Layout& target) {
    Type2WritePlan plan;
    if (source.block_size != 4 || source.memory.size() < 4 * 5) {
        plan.error = "Not an NTAG/Ultralight dump";
        return plan;
    }
    if (!target.known()) {
        plan.error = "Unknown target tag model";
        return plan;
    }
    const Type2Layout src = type2_layout(source);
    const int dumped = static_cast<int>(source.memory.size() / 4);
    // Sin disposición conocida del origen, todo lo volcado tras la página 3 es «de usuario»
    // salvo que no quepa; con ella, sólo su memoria de usuario.
    const int src_last = std::min(dumped - 1, src.known() ? src.last_user : dumped - 1);

    auto page_bytes = [&](int p) {
        std::array<uint8_t, 4> b{};
        std::copy_n(source.memory.begin() + p * 4, 4, b.begin());
        return b;
    };
    // Lo que no cabe tiene que estar vacío.
    for (int p = target.last_user + 1; p <= src_last; ++p) {
        const auto b = page_bytes(p);
        if (b != std::array<uint8_t, 4>{}) {
            plan.error = "Data doesn't fit in " + (target.name.empty() ? std::string("this tag") : target.name);
            return plan;
        }
    }
    const int last = std::min(src_last, target.last_user);
    for (int p = target.first_user; p <= last; ++p) {
        plan.pages.emplace_back(p, page_bytes(p));
    }
    if (plan.pages.empty()) plan.error = "Nothing to write";
    return plan;
}

} // namespace nfc

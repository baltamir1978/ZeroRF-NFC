// Crypto1 de MIFARE Classic, autocontenido (sin dependencias de M5Utility).
// El cifrado, el LFSR de 48 bits y el filtro no lineal están copiados literalmente de
// M5Unit-NFC (MIT): src/nfc/crypto/mifare_classic_crypto1.hpp y src/m5_utility/lfsr.hpp.
// Aquí sólo se han extraído las piezas necesarias para que compile solo.
#pragma once
#include <bitset>
#include <climits>
#include <cstdint>
#include <type_traits>

namespace mfc {

inline bool parity8(uint8_t u8) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_parity(u8) != 0;
#else
    uint8_t v = u8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return v & 1;
#endif
}
// Bit que hace al byte de paridad impar (1 si el byte tiene un nº par de unos)
inline uint8_t oddParityBit(uint8_t u8) { return parity8(u8) ? 0 : 1; }

// --- Fibonacci LFSR (desplazamiento a la izquierda): salida=MSB, realimentación->LSB ---
template <uint32_t N, uint32_t... Taps>
class FibonacciLFSR_Left {
protected:
    template <uint32_t... Ts>
    static bool taps_xor(const std::bitset<N>& s) {
        bool r{};
        using swallow = int[];
        (void)swallow{0, (r ^= s.test(Ts - 1), 0)...};
        return r;
    }
public:
    using state_type_t = std::bitset<N>;
    explicit FibonacciLFSR_Left(uint64_t seed) noexcept : _state{seed} {}
    inline const state_type_t& state() const noexcept { return _state; }
    bool step() noexcept {
        const bool out = _state.test(N - 1);
        const bool fb = taps_xor<Taps...>(_state);
        _state <<= 1;
        _state.set(0, fb);
        return out;
    }
    state_type_t _state;
};

// --- Fibonacci LFSR (a la derecha): para el PRNG de los nonces (suc_23) ---
template <uint32_t N, uint32_t... Taps>
class FibonacciLFSR_Right {
protected:
    template <uint32_t... Ts>
    static bool taps_xor(const std::bitset<N>& s) {
        bool r{};
        using swallow = int[];
        (void)swallow{0, (r ^= s.test(N - Ts), 0)...};
        return r;
    }
public:
    explicit FibonacciLFSR_Right(uint32_t seed) noexcept : _state{seed} {}
    bool step() noexcept {
        const bool out = _state.test(0);
        const bool fb = taps_xor<Taps...>(_state);
        _state >>= 1;
        _state.set(N - 1, fb);
        return out;
    }
    uint32_t next32() noexcept {
        uint32_t v{};
        for (uint32_t i = 0; i < 32; ++i) v |= (uint32_t(step()) << i);
        return v;
    }
    std::bitset<N> _state;
};

using MLFSR48 = FibonacciLFSR_Left<48, 5, 6, 7, 9, 13, 19, 21, 23, 24, 29, 31, 33, 34, 36, 38, 39, 43, 48>;

class Crypto1 : public MLFSR48 {
public:
    Crypto1() noexcept : MLFSR48(0) {}
    explicit Crypto1(uint64_t key48) noexcept : MLFSR48(0) { init(key48); }

    void init(uint64_t key48) noexcept {
        _state = state_type_t{};
        for (int i = 0; i < 48; ++i) {
            int byte_index = i >> 3, bit_index = i & 0x07;
            int reversed = (byte_index << 3) + (bit_index ^ 7);
            _state[i] = (key48 >> reversed) & 1ULL;
        }
    }
    inline uint32_t inject(uint32_t uid, uint32_t Nt, bool enc = false) noexcept { return step32(uid ^ Nt, enc); }

    bool step_with(bool in, bool enc = false) noexcept {
        bool z = filter();
        (void)step();
        const bool ext = in ^ (enc ? z : 0);
        _state[0] = _state[0] ^ ext;
        return z;
    }
    uint8_t step8(uint8_t in, bool enc = false) noexcept {
        uint8_t v{};
        for (uint_fast8_t i = 0; i < 8; ++i) v |= step_with((in >> i) & 1, enc) << i;
        return v;
    }
    uint32_t step32(uint32_t in, bool enc = false) noexcept {
        uint32_t v{};
        for (uint32_t i = 0; i < 32; ++i) v |= uint32_t(step_with((in >> (i ^ 24)) & 1u, enc)) << (24 ^ i);
        return v;
    }
    // Cifra nonce del lector (Nr) y respuesta (Ar) -> buf[8]; devuelve bits de paridad
    uint8_t encrypt(uint8_t buf[8], uint32_t Nr, uint32_t Ar) noexcept {
        uint8_t parity{};
        for (uint_fast8_t i = 0; i < 4; ++i) {
            const uint8_t v = (Nr >> ((i ^ 0x03) << 3)) & 0xFF;
            buf[i] = step8(v) ^ v;
            parity |= ((filter() ^ oddParityBit(v)) & 1) << i;
        }
        for (uint_fast8_t pos = 4; pos < 8; ++pos) {
            const uint8_t i = pos - 4;
            const uint8_t v = (Ar >> (i << 3)) & 0xFF;
            buf[pos] = step8(0x00) ^ v;
            parity |= ((filter() ^ oddParityBit(v)) & 1) << pos;
        }
        return parity;
    }
    // Cifra un buffer de bytes; devuelve bits de paridad
    uint32_t encrypt(uint8_t* out, const uint8_t* in, uint8_t in_len) {
        uint32_t parity{};
        for (uint_fast8_t i = 0; i < in_len; ++i) {
            out[i] = in[i] ^ step8(0);
            parity |= ((filter() ^ oddParityBit(in[i])) & 1) << i;
        }
        return parity;
    }
    inline bool filter() const noexcept {
        const state_type_t& s = state();
        return fc(fa(s[38], s[36], s[34], s[32]), fb(s[30], s[28], s[26], s[24]),
                  fb(s[22], s[20], s[18], s[16]), fa(s[14], s[12], s[10], s[8]), fb(s[6], s[4], s[2], s[0]));
    }
    static bool fa(bool a, bool b, bool c, bool d) noexcept { return ((a || b) ^ (a && d)) ^ (c && ((a ^ b) || d)); }
    static bool fb(bool a, bool b, bool c, bool d) noexcept { return ((a && b) || c) ^ ((a ^ b) && (c || d)); }
    static bool fc(bool a, bool b, bool c, bool d, bool e) noexcept {
        return (a || ((b || e) && (d ^ e))) ^ ((a ^ (b && d)) && ((c ^ d) || (b && e)));
    }
};

// Sucesores PRNG 2 y 3 del nonce Nt (polinomio x^16+x^14+x^13+x^11+1)
inline void suc_23(uint32_t Nt, uint32_t& suc2, uint32_t& suc3) {
    FibonacciLFSR_Right<32, 16, 14, 13, 11> t(Nt);
    t.next32(); t.next32();
    suc2 = t.next32();
    suc3 = t.next32();
}
inline uint32_t byteswap32(uint32_t v) { return __builtin_bswap32(v); }

}  // namespace mfc

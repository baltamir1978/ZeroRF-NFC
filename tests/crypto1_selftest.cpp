// Autoprueba del Crypto1 portado (software puro, sin aparato ni tarjeta).
// Simula la autenticación mutua de 3 pasos de MIFARE Classic entre "lector" y "tarjeta"
// y comprueba que: la tarjeta recupera el nonce del lector (Nr), su Ar coincide con suc2(Nt),
// y el lector recupera el At de la tarjeta == suc3(Nt). Valida la consistencia del cifrado.
// NOTA: es una prueba de consistencia en software; la validación real necesita una tarjeta.
#include <cstdio>
#include <cstdint>
#include "../src/crypto1.h"

using namespace mfc;

static int fails = 0;
static void check(const char* what, bool ok) {
    printf("  [%s] %s\n", ok ? "OK" : "FALLA", what);
    if (!ok) ++fails;
}

int main() {
    const uint64_t key48 = 0xFFFFFFFFFFFFULL;  // clave por defecto
    const uint32_t uid = 0xDEADBEEF;
    const uint32_t Nt  = 0x01020304;           // nonce de la tarjeta
    const uint32_t Nr  = 0xA5A5F00F;           // nonce del lector

    // Determinismo del flujo de claves
    {
        Crypto1 a(key48), b(key48);
        a.inject(uid, Nt); b.inject(uid, Nt);
        uint8_t ka[8], kb[8];
        a.encrypt(ka, Nr, 0x11223344);
        b.encrypt(kb, Nr, 0x11223344);
        bool same = true;
        for (int i = 0; i < 8; ++i) same &= (ka[i] == kb[i]);
        check("misma clave+inject -> mismo flujo de claves", same);
    }

    // --- Lector ---
    uint32_t Ar, suc3;
    suc_23(byteswap32(Nt), Ar, suc3);      // Ar = suc2(Nt), At esperado = suc3(Nt)
    Crypto1 R(key48);
    R.inject(uid, Nt, false);
    uint8_t AB[8];
    R.encrypt(AB, Nr, Ar);                 // {Nr, Ar} cifrados; estado del lector avanzado

    // --- Tarjeta --- (en lockstep con el lector)
    Crypto1 C(key48);
    C.inject(uid, Nt, false);
    // Recuperar Nr: el lector alimentó Nr (enc=false); la tarjeta lo descifra con enc=true
    uint32_t Nr_rec = 0;
    for (int byte = 0; byte < 4; ++byte) {
        uint8_t e = AB[byte], v = 0;
        for (int i = 0; i < 8; ++i) {
            bool cb = (e >> i) & 1;
            bool z = C.step_with(cb, true);   // alimenta el texto claro recuperado
            bool pb = cb ^ z;
            v |= pb << i;
        }
        // Nr se cifró con los bytes en orden (i^3): byte0 = Nr[31:24] ...
        Nr_rec |= (uint32_t)v << ((byte ^ 0x03) << 3);
    }
    check("la tarjeta recupera el Nr del lector", Nr_rec == Nr);
    // Recuperar Ar: el lector NO lo alimentó (enc=false, in=0); la tarjeta usa step_with(0,false)
    uint32_t Ar_rec = 0;
    for (int byte = 0; byte < 4; ++byte) {
        uint8_t e = AB[4 + byte], v = 0;
        for (int i = 0; i < 8; ++i) {
            bool cb = (e >> i) & 1;
            bool z = C.step_with(0, false);
            v |= (cb ^ z) << i;
        }
        Ar_rec |= (uint32_t)v << (byte << 3);
    }
    check("el Ar recuperado coincide con suc2(Nt)", Ar_rec == Ar);

    // La tarjeta responde At = suc3 cifrado; el lector lo descifra y verifica
    uint8_t BA[4];
    for (int i = 0; i < 4; ++i) BA[i] = C.step8(0) ^ ((suc3 >> (i << 3)) & 0xFF);
    uint32_t At_rec = 0;
    for (int i = 0; i < 4; ++i) At_rec |= (uint32_t)(BA[i] ^ R.step8(0)) << (i << 3);
    check("el lector recupera At == suc3(Nt)", At_rec == suc3);

    printf(fails ? "\nRESULTADO: %d FALLOS\n" : "\nRESULTADO: todo OK\n", fails);
    return fails ? 1 : 0;
}

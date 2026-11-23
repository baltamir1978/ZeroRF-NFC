#pragma once
#include <string>
// Operaciones NFC de alto nivel para la interfaz. Cada una enciende el Cap, hace la operación
// (unos segundos como mucho) y devuelve el resultado como texto para mostrar en pantalla.
namespace nfcops {
std::string read_card();   // detecta una tarjeta NFC-A y vuelca UID/tipo + primeras páginas
std::string identify();    // identifica la tecnología (A/B/V/F) o si es propietaria/125kHz
std::string sub_ghz_note();// nota sobre Sub-GHz (CC1101), pendiente
}  // namespace nfcops

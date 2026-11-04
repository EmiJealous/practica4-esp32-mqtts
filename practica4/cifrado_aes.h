#pragma once
#include <Arduino.h>

// Cifra el texto con AES-128-GCM y regresa en Base64: nonce (12) | cifrado | tag (16).
// Si algo falla regresa "" (y en ese caso NO hay que publicar nada).
String cifrarAesGcm(const uint8_t clave[16], const String &textoPlano);

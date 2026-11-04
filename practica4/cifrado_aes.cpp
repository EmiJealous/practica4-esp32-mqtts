#include "cifrado_aes.h"
#include <mbedtls/gcm.h>
#include <mbedtls/base64.h>
#include <esp_random.h>

static const size_t NONCE_LEN = 12;
static const size_t TAG_LEN = 16;
static const size_t MAX_TEXTO = 64; // de sobra para "100.0"

static const size_t MAX_PAQUETE = NONCE_LEN + MAX_TEXTO + TAG_LEN;
static const size_t MAX_BASE64 = ((MAX_PAQUETE + 2) / 3) * 4 + 1; // +1 por el '\0'

String cifrarAesGcm(const uint8_t clave[16], const String &textoPlano)
{
  size_t largo = textoPlano.length();
  if (largo == 0 || largo > MAX_TEXTO)
    return "";

  // El paquete se arma directo en un solo buffer: [nonce | cifrado | tag]
  uint8_t paquete[MAX_PAQUETE];
  uint8_t *nonce = paquete;
  uint8_t *cifrado = paquete + NONCE_LEN;
  uint8_t *tag = cifrado + largo;

  // Nonce nuevo y aleatorio en cada mensaje (generador por hardware del ESP32),
  // nunca se reutiliza uno con la misma clave
  esp_fill_random(nonce, NONCE_LEN);

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int error = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, clave, 128);
  if (error == 0)
  {
    error = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, largo,
                                      nonce, NONCE_LEN,
                                      nullptr, 0, // sin datos adicionales (AAD)
                                      (const uint8_t *)textoPlano.c_str(), cifrado,
                                      TAG_LEN, tag);
  }
  mbedtls_gcm_free(&gcm);

  if (error != 0)
    return "";

  unsigned char base64[MAX_BASE64];
  size_t largoBase64 = 0;
  if (mbedtls_base64_encode(base64, sizeof(base64), &largoBase64, paquete, NONCE_LEN + largo + TAG_LEN) != 0)
    return "";

  return String((const char *)base64);
}

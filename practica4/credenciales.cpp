#include "credenciales.h"
#include <Preferences.h>

static const char *NVS_NAMESPACE = "practica4";
static const char *CLAVE_LISTO = "listo";

static Preferences nvs;

// Lee una linea completa del Monitor Serial (bloquea hasta que llegue un Enter).
// Si es un secreto, en vez de repetirlo en pantalla solo pone asteriscos.
static String leerLinea(const char *pregunta, bool esSecreto)
{
  Serial.printf("%s: ", pregunta);

  String linea;
  while (true)
  {
    if (!Serial.available())
    {
      delay(10);
      continue;
    }

    char c = Serial.read();
    if (c == '\r')
      continue;
    if (c == '\n' && linea.length() > 0)
      break;
    if (c != '\n')
      linea += c;
  }

  Serial.println(esSecreto ? "********" : linea);
  return linea;
}

// Convierte "00112233...ff" (32 caracteres hex) en los 16 bytes de la clave AES
static bool hexAClave(const String &hex, uint8_t clave[16])
{
  if (hex.length() != 32)
    return false;

  for (char c : hex)
  {
    if (!isxdigit(c))
      return false;
  }

  for (int i = 0; i < 16; i++)
  {
    clave[i] = strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  }
  return true;
}

static void aprovisionarPorSerial()
{
  Serial.println();
  Serial.println("=== MODO APROVISIONAMIENTO ===");
  Serial.println("Este ESP32 no tiene credenciales guardadas.");
  Serial.println("Escribe cada valor y presiona Enter. Esto solo se pide una vez.");

  String ssid = leerLinea("WIFI_SSID", false);
  String wifiPass = leerLinea("WIFI_PASSWORD", true);
  String host = leerLinea("MQTT_HOST (IP del broker)", false);
  String usuario = leerLinea("MQTT_USUARIO", false);
  String mqttPass = leerLinea("MQTT_PASSWORD", true);

  uint8_t clave[16];
  while (!hexAClave(leerLinea("AES_KEY (32 caracteres hex)", true), clave))
  {
    Serial.println("Clave invalida: tienen que ser 32 caracteres hexadecimales.");
  }

  nvs.begin(NVS_NAMESPACE, false);
  nvs.putString("wifi_ssid", ssid);
  nvs.putString("wifi_pass", wifiPass);
  nvs.putString("mqtt_host", host);
  nvs.putString("mqtt_user", usuario);
  nvs.putString("mqtt_pass", mqttPass);
  nvs.putBytes("aes_key", clave, sizeof(clave));
  // "listo" se escribe al final a proposito: si se va la luz a medio guardar,
  // el ESP32 vuelve a pedir todo en vez de arrancar con credenciales a medias
  nvs.putBool(CLAVE_LISTO, true);
  nvs.end();

  Serial.println("Credenciales guardadas en NVS. Reiniciando...");
  delay(500);
  ESP.restart();
}

Credenciales cargarOAprovisionarCredenciales()
{
  nvs.begin(NVS_NAMESPACE, true);
  bool listo = nvs.getBool(CLAVE_LISTO, false);
  nvs.end();

  if (!listo)
    aprovisionarPorSerial();

  Credenciales c;
  nvs.begin(NVS_NAMESPACE, true);
  c.wifiSsid = nvs.getString("wifi_ssid");
  c.wifiPassword = nvs.getString("wifi_pass");
  c.mqttHost = nvs.getString("mqtt_host");
  c.mqttUsuario = nvs.getString("mqtt_user");
  c.mqttPassword = nvs.getString("mqtt_pass");
  nvs.getBytes("aes_key", c.claveAes, sizeof(c.claveAes));
  nvs.end();

  return c;
}

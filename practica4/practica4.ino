/*
 * ============================================================
 * PRACTICA 4 - Internet de las Cosas
 * Asegurar el sistema de la practica 3 (ESP32 + MQTT + LED RGB)
 * ============================================================
 *
 * El circuito y el comportamiento del LED, boton y potenciometro son los mismos
 * de la practica 3. Lo que cambia es la seguridad:
 *   1. El canal va cifrado con TLS (MQTTS, puerto 8883) y el ESP32 valida el
 *      certificado del broker contra nuestra CA (nada de setInsecure()).
 *   2. La telemetria del potenciometro ademas va cifrada con AES-128-GCM.
 *   3. Ya no hay ninguna credencial en el codigo: todo vive en la NVS y se
 *      carga al arrancar (ver credenciales.cpp).
 *
 * Topicos (iguales que en la practica 3):
 *   iot/<equipo>/light/control  -> (sub) comandos que llegan hacia el ESP32
 *   iot/<equipo>/light/status   -> (pub, retain) estado actual de la luz
 *   iot/<equipo>/device/status  -> (pub, retain) ONLINE / OFFLINE (LWT)
 *   iot/<equipo>/button/event   -> (pub) eventos del boton
 *   iot/<equipo>/pot/value      -> (pub) potenciometro, CIFRADO con AES-GCM
 */

#include <WiFi.h>
#include <WiFiClientSecure.h> // cliente TLS (reemplaza al WiFiClient de la practica 3)
#include <PubSubClient.h>
#include <time.h>

#include "credenciales.h"
#include "cifrado_aes.h"
#include "ca_cert.h"

// ============================================================
// CONFIGURACION (nada de esto es secreto)
// ============================================================
const char *MQTT_EQUIPO = "equipo1";
const int MQTT_BROKER_PORT = 8883; // MQTTS

const long ZONA_HORARIA_SEG = -6 * 3600;     // Guadalajara, UTC-6
const time_t HORA_MINIMA_VALIDA = 1700000000; // nov 2023: si la hora es menor, NTP aun no responde

// ============================================================
// PINES (mismo circuito que la practica 3)
// ============================================================
const int RED_PIN = 26;
const int GREEN_PIN = 25;
const int BLUE_PIN = 33;
const int BUTTON_PIN = 14;
const int POT_PIN = 34; // ADC1: los pines ADC2 no funcionan con WiFi activo

// ============================================================
// CLIENTES DE RED
// ============================================================
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

Credenciales creds; // se llenan desde la NVS en setup()
char clientId[24];  // se arma con la MAC, asi cada ESP32 tiene uno distinto sin configurar nada

char topicLightControl[48];
char topicLightStatus[48];
char topicDeviceStatus[48];
char topicButtonEvent[48];
char topicPotValue[48];

// ============================================================
// ESTADO DE LA ILUMINACION
// ============================================================
enum Color
{
  ROJO,
  VERDE,
  AZUL
};
Color colorActual = ROJO;

bool sistemaHabilitado = true;  // true/false -> comandos ON / OFF
bool frecuenciaForzada = false; // true = frecuencia fija por BLINK:x, false = AUTO (usa el pot)
float hzForzadoValor = 1.0;

bool ledEncendido = false;
unsigned long ultimoCambioLED = 0;

String modoAnterior = "ON";
unsigned long ultimoCambioModo = 0;

// ============================================================
// ANTIRREBOTE Y EVENTOS DEL BOTON
// ============================================================
int ultimaLecturaCruda = LOW;
int estadoBotonEstable = LOW;
unsigned long ultimoCambioBoton = 0;
const unsigned long DEBOUNCE_MS = 50;

unsigned long contadorPresiones = 0;
unsigned long ultimaPresionMs = 0;
const unsigned long DOBLE_CLICK_MS = 400;

// ============================================================
// TELEMETRIA DEL POTENCIOMETRO
// ============================================================
unsigned long ultimaLecturaPot = 0;
const unsigned long POT_INTERVALO_MS = 1000;

void construirTopicos()
{
  snprintf(topicLightControl, sizeof(topicLightControl), "iot/%s/light/control", MQTT_EQUIPO);
  snprintf(topicLightStatus, sizeof(topicLightStatus), "iot/%s/light/status", MQTT_EQUIPO);
  snprintf(topicDeviceStatus, sizeof(topicDeviceStatus), "iot/%s/device/status", MQTT_EQUIPO);
  snprintf(topicButtonEvent, sizeof(topicButtonEvent), "iot/%s/button/event", MQTT_EQUIPO);
  snprintf(topicPotValue, sizeof(topicPotValue), "iot/%s/pot/value", MQTT_EQUIPO);
}

// En la practica 3 el Client ID venia en secrets.h y tuvimos que cambiarlo a mano
// para el segundo ESP32. Ahora sale de la MAC: unico y sin configurar nada.
void armarClientId()
{
  uint64_t mac = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "ESP32-%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
}

const char *colorATexto(Color c)
{
  switch (c)
  {
  case ROJO:
    return "RED";
  case VERDE:
    return "GREEN";
  case AZUL:
    return "BLUE";
  }
  return "RED";
}

String obtenerModoActual()
{
  if (!sistemaHabilitado)
    return "OFF";
  if (frecuenciaForzada)
    return "BLINK";
  return "ON"; // encendido, parpadeando segun el potenciometro (control local)
}

void aplicarColor(bool encendido)
{
  digitalWrite(RED_PIN, (encendido && colorActual == ROJO) ? HIGH : LOW);
  digitalWrite(GREEN_PIN, (encendido && colorActual == VERDE) ? HIGH : LOW);
  digitalWrite(BLUE_PIN, (encendido && colorActual == AZUL) ? HIGH : LOW);
}

// Parpadeo no bloqueante: a la frecuencia de BLINK:x o, en AUTO, segun el potenciometro
void manejarIluminacion()
{
  if (!sistemaHabilitado)
  {
    aplicarColor(false);
    ledEncendido = false;
    return;
  }

  int retardoMs;
  if (frecuenciaForzada)
  {
    retardoMs = (int)(1000.0 / hzForzadoValor / 2.0);
  }
  else
  {
    int valorADC = analogRead(POT_PIN);
    retardoMs = map(valorADC, 0, 4095, 1000, 50);
  }

  if (millis() - ultimoCambioLED >= retardoMs)
  {
    ultimoCambioLED = millis();
    ledEncendido = !ledEncendido;
    aplicarColor(ledEncendido);
  }
}

// Publica el estado de la luz en light/status con retain
void publicarEstadoLuz()
{
  String modo = obtenerModoActual();
  int valorADC = analogRead(POT_PIN);
  unsigned long tiempoDesdeCambio = (millis() - ultimoCambioModo) / 1000;

  char buffer[160];
  if (modo == "BLINK")
  {
    snprintf(buffer, sizeof(buffer),
             "{\"mode\":\"%s\",\"color\":\"%s\",\"freq\":%.1f,\"time\":%lu,\"pot\":%d}",
             modo.c_str(), colorATexto(colorActual), hzForzadoValor, tiempoDesdeCambio, valorADC);
  }
  else
  {
    snprintf(buffer, sizeof(buffer),
             "{\"mode\":\"%s\",\"color\":\"%s\",\"time\":%lu,\"pot\":%d}",
             modo.c_str(), colorATexto(colorActual), tiempoDesdeCambio, valorADC);
  }

  mqttClient.publish(topicLightStatus, buffer, true);
  Serial.printf("light/status -> %s\n", buffer);
}

void actualizarModoYPublicarEstado()
{
  String modoNuevo = obtenerModoActual();
  if (modoNuevo != modoAnterior)
  {
    modoAnterior = modoNuevo;
    ultimoCambioModo = millis();
  }
  publicarEstadoLuz();
}

// Comandos que llegan por light/control (igual que en la practica 3)
void alRecibirMensajeMQTT(char *topic, byte *payload, unsigned int length)
{
  String comando;
  for (unsigned int i = 0; i < length; i++)
  {
    comando += (char)payload[i];
  }
  comando.trim();
  comando.toUpperCase();

  if (comando.length() == 0)
    return;

  Serial.printf("Comando MQTT: %s\n", comando.c_str());

  if (comando == "STATUS")
  {
    publicarEstadoLuz();
    return;
  }

  if (comando == "ON")
  {
    sistemaHabilitado = true;
  }
  else if (comando == "OFF")
  {
    sistemaHabilitado = false;
  }
  else if (comando == "AUTO")
  {
    frecuenciaForzada = false;
  }
  else if (comando == "COLOR:RED")
  {
    colorActual = ROJO;
  }
  else if (comando == "COLOR:GREEN")
  {
    colorActual = VERDE;
  }
  else if (comando == "COLOR:BLUE")
  {
    colorActual = AZUL;
  }
  else if (comando.startsWith("BLINK:"))
  {
    float hz = comando.substring(6).toFloat();
    if (hz > 0)
    {
      frecuenciaForzada = true;
      hzForzadoValor = hz;
    }
  }
  else
  {
    Serial.println("Comando no reconocido, se ignora.");
    return;
  }

  actualizarModoYPublicarEstado();
}

// Antirrebote + contador de presiones + doble clic (igual que en la practica 3)
void manejarBoton()
{
  int lecturaCruda = digitalRead(BUTTON_PIN);

  if (lecturaCruda != ultimaLecturaCruda)
  {
    ultimoCambioBoton = millis();
  }

  if ((millis() - ultimoCambioBoton) > DEBOUNCE_MS)
  {
    if (lecturaCruda != estadoBotonEstable)
    {
      estadoBotonEstable = lecturaCruda;

      if (estadoBotonEstable == HIGH)
      {
        contadorPresiones++;

        char buffer[30];
        snprintf(buffer, sizeof(buffer), "BUTTON_PRESSED: %lu", contadorPresiones);
        mqttClient.publish(topicButtonEvent, buffer);

        unsigned long ahora = millis();
        if (ahora - ultimaPresionMs <= DOBLE_CLICK_MS)
        {
          mqttClient.publish(topicButtonEvent, "BUTTON_DOUBLE_CLICK");
        }
        ultimaPresionMs = ahora;
      }
    }
  }
  ultimaLecturaCruda = lecturaCruda;
}

// Potenciometro de 0 a 100% cada segundo. A diferencia de la practica 3, el valor
// ya no sale en claro: va cifrado con AES-128-GCM (ademas del TLS del canal)
void publicarPot()
{
  if (millis() - ultimaLecturaPot < POT_INTERVALO_MS)
    return;
  ultimaLecturaPot = millis();

  int valorADC = analogRead(POT_PIN);
  float porcentaje = (valorADC / 4095.0) * 100.0;

  char texto[10];
  snprintf(texto, sizeof(texto), "%.1f", porcentaje);

  String cifrado = cifrarAesGcm(creds.claveAes, texto);
  if (cifrado.isEmpty())
  {
    Serial.println("!! No se pudo cifrar el potenciometro, no se publica nada");
    return;
  }
  mqttClient.publish(topicPotValue, cifrado.c_str());
}

void conectarWiFi()
{
  Serial.printf("Conectando a WiFi '%s'", creds.wifiSsid.c_str());
  WiFi.begin(creds.wifiSsid.c_str(), creds.wifiPassword.c_str());

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.printf(" conectado. IP: %s\n", WiFi.localIP().toString().c_str());
}

// TLS necesita la hora real para saber si el certificado del broker esta vigente,
// por eso sincronizamos con NTP ANTES de intentar conectarnos al broker
void sincronizarHora()
{
  Serial.print("Sincronizando hora por NTP");
  // time.google.com primero: con pool.ntp.org a veces nos tocaba un servidor lejano que
  // no contestaba y el ESP32 se quedaba esperando. El tercero va por IP por si falla el DNS
  configTime(ZONA_HORARIA_SEG, 0, "time.google.com", "pool.ntp.org", "216.239.35.0");

  while (time(nullptr) < HORA_MINIMA_VALIDA)
  {
    delay(500);
    Serial.print(".");
  }

  time_t ahora = time(nullptr);
  Serial.printf(" listo: %s", ctime(&ahora));
}

// Si la conexion falla por TLS (certificado invalido, CA distinta, IP que no
// viene en el certificado, hora mal...), lo reportamos en vez de quedarnos callados
void reportarErrorTls()
{
  char detalle[128];
  int codigo = espClient.lastError(detalle, sizeof(detalle));
  if (codigo != 0)
  {
    Serial.printf("  !! Error TLS %d: %s\n", codigo, detalle);
  }
}

// Conecta al broker por MQTTS con usuario/password y el LWT de la practica 3
void reconectarMQTT()
{
  while (!mqttClient.connected())
  {
    Serial.printf("Conectando a MQTTS %s:%d...", creds.mqttHost.c_str(), MQTT_BROKER_PORT);

    bool conectado = mqttClient.connect(
        clientId,
        creds.mqttUsuario.c_str(), creds.mqttPassword.c_str(),
        topicDeviceStatus, 0, true, "OFFLINE" // LWT
    );

    if (conectado)
    {
      Serial.println(" conectado (TLS OK, certificado del broker verificado).");
      mqttClient.publish(topicDeviceStatus, "ONLINE", true);
      mqttClient.subscribe(topicLightControl);
    }
    else
    {
      Serial.printf(" fallo (rc=%d), reintentando en 2s...\n", mqttClient.state());
      reportarErrorTls();
      delay(2000);
    }
  }
}

void setup()
{
  Serial.begin(115200);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);

  creds = cargarOAprovisionarCredenciales(); // la primera vez se queda aqui pidiendolas
  armarClientId();
  construirTopicos();

  conectarWiFi();
  sincronizarHora();

  espClient.setCACert(CA_CERT); // solo confiamos en certificados firmados por nuestra CA
  mqttClient.setServer(creds.mqttHost.c_str(), MQTT_BROKER_PORT);
  mqttClient.setCallback(alRecibirMensajeMQTT);
  reconectarMQTT();

  modoAnterior = obtenerModoActual();
  ultimoCambioModo = millis();
  publicarEstadoLuz();

  Serial.printf("Sistema listo. Client ID: %s\n", clientId);
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    conectarWiFi();
  }
  if (!mqttClient.connected())
  {
    reconectarMQTT();
  }
  mqttClient.loop();

  manejarBoton();
  manejarIluminacion();
  publicarPot();
}

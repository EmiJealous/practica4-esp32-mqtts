#pragma once
#include <Arduino.h>

// Todo lo que en la practica 3 estaba escrito en secrets.h ahora vive en la NVS
// del ESP32 y se lee al arrancar. Asi el mismo .bin sirve para cualquier ESP32.
struct Credenciales
{
  String wifiSsid;
  String wifiPassword;
  String mqttHost;
  String mqttUsuario;
  String mqttPassword;
  uint8_t claveAes[16];
};

// Si el ESP32 todavia no tiene credenciales, entra al modo de aprovisionamiento
// por Monitor Serial, las guarda en NVS y se reinicia (no regresa).
// Si ya las tiene, solo las carga: el modo de aprovisionamiento ya no vuelve a salir.
Credenciales cargarOAprovisionarCredenciales();

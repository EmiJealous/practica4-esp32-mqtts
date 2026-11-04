#!/usr/bin/env python3
"""
Aprovisiona un ESP32 recien flasheado: le contesta por el Monitor Serial cada
credencial que pide al arrancar por primera vez.

Uso:   python3 herramientas/aprovisionar.py /dev/cu.usbserial-XXX

Los valores salen de .secrets/credenciales.env (nunca se sube al repo), asi no
quedan en el historial de la terminal ni hay que teclearlos a mano.
"""
import sys
import time
from pathlib import Path

import serial

ARCHIVO_SECRETOS = Path(__file__).resolve().parent.parent / ".secrets" / "credenciales.env"

# (lo que pregunta el ESP32, variable del .env que le contestamos)
CAMPOS = [
    ("WIFI_SSID", "WIFI_SSID"),
    ("WIFI_PASSWORD", "WIFI_PASSWORD"),
    ("MQTT_HOST", "MQTT_HOST"),
    ("MQTT_USUARIO", "MQTT_USER_ESP32"),
    ("MQTT_PASSWORD", "MQTT_PASS_ESP32"),
    ("AES_KEY", "AES_KEY_HEX"),
]


def leer_secretos():
    valores = {}
    for linea in ARCHIVO_SECRETOS.read_text().splitlines():
        linea = linea.strip()
        if linea and not linea.startswith("#") and "=" in linea:
            nombre, valor = linea.split("=", 1)
            valores[nombre] = valor.strip('"')
    return valores


class LectorSerial:
    """Va acumulando lo que dice el ESP32 para poder esperar a que pregunte algo."""

    def __init__(self, puerto):
        self.puerto = puerto
        self.texto = ""
        self.posicion = 0

    def esperar(self, frase, timeout=30):
        limite = time.time() + timeout
        while time.time() < limite:
            self.texto += self.puerto.read(self.puerto.in_waiting or 1).decode(errors="replace")
            encontrado = self.texto.find(frase, self.posicion)
            if encontrado != -1:
                self.posicion = encontrado + len(frase)
                return
        raise TimeoutError(f"El ESP32 nunca dijo '{frase}'. Lo ultimo que llego:\n{self.texto[-400:]}")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    secretos = leer_secretos()

    with serial.Serial(sys.argv[1], 115200, timeout=0.2) as puerto:
        # Reiniciamos el ESP32 para agarrarlo desde el arranque
        puerto.dtr = False
        puerto.rts = True
        time.sleep(0.1)
        puerto.rts = False

        esp32 = LectorSerial(puerto)
        try:
            esp32.esperar("MODO APROVISIONAMIENTO")
        except TimeoutError:
            sys.exit("Este ESP32 no entro al modo de aprovisionamiento: probablemente ya tiene "
                     "credenciales. Para volver a aprovisionarlo hay que borrar su flash.")

        for pregunta, variable in CAMPOS:
            esp32.esperar(pregunta)
            puerto.write((secretos[variable] + "\n").encode())
            print(f"  {pregunta} enviado")

        esp32.esperar("Reiniciando")
        print("Listo: credenciales guardadas en la NVS del ESP32.")


if __name__ == "__main__":
    main()

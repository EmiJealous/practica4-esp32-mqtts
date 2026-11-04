#!/usr/bin/env python3
"""
Prueba de ruptura intencional del payload cifrado.

Agarra un mensaje real de pot/value, le cambia UN solo byte de la parte cifrada
y lo vuelve a publicar. Node-RED tiene que rechazarlo (el tag de GCM ya no cuadra)
en vez de mostrar un valor corrupto.

Uso:   .venv/bin/python herramientas/prueba_ruptura.py
"""
import base64
import subprocess
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
TOPICO = "iot/equipo1/pot/value"
NONCE_LEN = 12


def leer_secretos():
    valores = {}
    for linea in (RAIZ / ".secrets" / "credenciales.env").read_text().splitlines():
        if "=" in linea and not linea.startswith("#"):
            nombre, valor = linea.split("=", 1)
            valores[nombre] = valor.strip('"')
    return valores


def argumentos_mqtt(secretos):
    return ["-h", "localhost", "-p", "8883", "--cafile", str(RAIZ / "certs" / "ca.crt"),
            "-u", secretos["MQTT_USER_PANEL"], "-P", secretos["MQTT_PASS_PANEL"]]


def main():
    secretos = leer_secretos()

    original = subprocess.run(
        ["mosquitto_sub", *argumentos_mqtt(secretos), "-t", TOPICO, "-C", "1", "-W", "10"],
        capture_output=True, text=True, check=True,
    ).stdout.strip()

    paquete = bytearray(base64.b64decode(original))
    posicion = NONCE_LEN  # primer byte del valor cifrado (justo despues del nonce)
    antes = paquete[posicion]
    paquete[posicion] ^= 0x01  # volteamos un solo bit de ese byte
    alterado = base64.b64encode(paquete).decode()

    print(f"Mensaje original : {original}")
    print(f"Mensaje alterado : {alterado}")
    print(f"Byte cambiado    : posicion {posicion}, 0x{antes:02x} -> 0x{paquete[posicion]:02x}")

    subprocess.run(["mosquitto_pub", *argumentos_mqtt(secretos), "-t", TOPICO, "-m", alterado], check=True)
    print("Publicado. En el dashboard debe aparecer RECHAZADO en vez de un valor raro.")


if __name__ == "__main__":
    main()

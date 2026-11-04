#!/usr/bin/env bash
# Genera nuestra CA propia y el certificado del broker Mosquitto firmado por ella.
# Uso: ./generar_certs.sh <ip_del_broker> [mas_ips...]
#
# La CA solo se crea la primera vez: si la regeneramos, el ESP32 (que trae ca.crt
# adentro del firmware) dejaria de confiar en el broker y habria que reflashear.
# El certificado del broker si se puede regenerar las veces que haga falta
# (por ejemplo cuando cambia la IP de la compu).
set -euo pipefail

OPENSSL=/opt/homebrew/opt/openssl@3/bin/openssl
cd "$(dirname "$0")"

if [ $# -eq 0 ]; then
  echo "Uso: $0 <ip_del_broker> [mas_ips...]"
  exit 1
fi

# El ESP32 se conecta por IP, asi que esa IP tiene que venir en el certificado (SAN)
SAN="DNS:localhost,IP:127.0.0.1"
for ip in "$@"; do SAN="$SAN,IP:$ip"; done

if [ ! -f ca.key ]; then
  echo ">> Creando CA..."
  $OPENSSL req -x509 -newkey rsa:2048 -nodes -keyout ca.key -sha256 -days 3650 \
    -subj "/CN=Practica4 IoT CA" -out ca.crt
fi

echo ">> Creando certificado del broker para: $SAN"
$OPENSSL req -newkey rsa:2048 -nodes -keyout server.key -subj "/CN=mosquitto-broker" -out server.csr
$OPENSSL x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -days 825 -sha256 \
  -extfile <(printf "subjectAltName=%s\nextendedKeyUsage=serverAuth\n" "$SAN") -out server.crt
rm -f server.csr

# CA "pirata" que no tiene nada que ver con la nuestra, solo para la prueba de rechazo
if [ ! -f ca_falsa.crt ]; then
  echo ">> Creando CA falsa para pruebas..."
  $OPENSSL req -x509 -newkey rsa:2048 -nodes -keyout ca_falsa.key -sha256 -days 365 \
    -subj "/CN=CA Falsa" -out ca_falsa.crt
fi

echo ">> Listo."

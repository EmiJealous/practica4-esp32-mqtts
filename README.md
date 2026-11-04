# Práctica 4 - IoT: asegurando el ESP32 + MQTT

Universidad de Guadalajara — Ingeniería Informática — Internet de las Cosas

Es la práctica 3 (ESP32 + LED RGB + botón + potenciómetro por MQTT) pero blindada:

1. **TLS (MQTTS, puerto 8883)** con una CA propia; el ESP32 valida el certificado del broker y sincroniza la hora por NTP antes.
2. **AES-128-GCM** en la telemetría del potenciómetro, con descifrado y verificación del tag en Node-RED.
3. **Cero credenciales en el código**: todo vive en la NVS del ESP32 y se carga con un aprovisionamiento por Monitor Serial que solo corre una vez.

> En este repo no hay ninguna credencial: ni contraseñas, ni la clave AES, ni llaves privadas. Cada quien genera las suyas (ver abajo).

## Estructura

```
practica4/              firmware (practica4.ino + credenciales, cifrado_aes, ca_cert)
certs/generar_certs.sh  crea la CA y el certificado del broker (ca.crt es público)
mosquitto/              configuración de Mosquitto (TLS + usuarios)
nodered/                flujo de Node-RED con el descifrado AES-GCM
herramientas/           aprovisionar.py, prueba_ruptura.py, iniciar_nodered.sh
docs/                   marco teórico, arquitectura, explicación paso a paso, guía del código, auditoría OWASP y evidencias
```

## Cómo levantar todo

**1. Credenciales locales** — crea `.secrets/credenciales.env` (nunca se sube):

```bash
WIFI_SSID="TuRed"
WIFI_PASSWORD=...
MQTT_HOST=172.20.10.2          # IP de la compu con Mosquitto
MQTT_USER_ESP32=esp32
MQTT_PASS_ESP32=...            # openssl rand -hex 12
MQTT_USER_PANEL=panel
MQTT_PASS_PANEL=...            # openssl rand -hex 12
AES_KEY_HEX=...                # openssl rand -hex 16
```

**2. Certificados** — con la IP de tu compu:

```bash
certs/generar_certs.sh 172.20.10.2
```

Si regeneras la CA tienes que volver a generar `practica4/ca_cert.h` y reflashear.

**3. Mosquitto**

```bash
ETC=/opt/homebrew/etc/mosquitto
mkdir -p $ETC/certs && cp certs/ca.crt certs/server.crt certs/server.key $ETC/certs/
source .secrets/credenciales.env
mosquitto_passwd -c -b $ETC/passwd "$MQTT_USER_ESP32" "$MQTT_PASS_ESP32"
mosquitto_passwd -b $ETC/passwd "$MQTT_USER_PANEL" "$MQTT_PASS_PANEL"
cp mosquitto/mosquitto.conf $ETC/mosquitto.conf && brew services restart mosquitto
```

**4. Flashear y aprovisionar el ESP32**

```bash
arduino-cli upload -p /dev/cu.usbserial-XXX --fqbn "esp32:esp32:esp32:UploadSpeed=115200,EraseFlash=all" practica4
python3 -m venv .venv && .venv/bin/pip install -r herramientas/requirements.txt
.venv/bin/python herramientas/aprovisionar.py /dev/cu.usbserial-XXX
```

**5. Node-RED** (con `@flowfuse/node-red-dashboard` instalado)

```bash
herramientas/iniciar_nodered.sh
```

Importa `nodered/practica4_flow.json`, ajusta la ruta de `ca.crt` en el nodo TLS, pon el usuario/contraseña `panel` en el nodo del broker y dale Deploy. Dashboard en `http://127.0.0.1:1880/dashboard`.

## Pruebas

```bash
# Rechazo de TLS sin certificado / con CA falsa (ver docs/evidencias/pruebas_tls.txt)
mosquitto_pub -h localhost -p 8883 --cafile certs/ca_falsa.crt -u panel -P ... -t prueba -m x

# Ruptura intencional del payload cifrado
.venv/bin/python herramientas/prueba_ruptura.py
```

Para reaprovisionar un ESP32 hay que borrar su flash (el modo de aprovisionamiento ya no se activa solo).

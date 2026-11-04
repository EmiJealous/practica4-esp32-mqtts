# Práctica 4 - Auditoría OWASP IoT Top 10

## Qué corrige cada medida

| Medida implementada | Vulnerabilidad OWASP que corrige | Por qué |
|---|---|---|
| **1. TLS (MQTTS) con CA propia** + usuario/contraseña en el broker | **I7** Transferencia de datos insegura | Antes todo viajaba en claro por el 1883: cualquiera en la red podía leer o modificar los mensajes. Ahora el canal va cifrado y el ESP32 verifica que el broker sea el nuestro. |
| | **I2** Servicios de red inseguros | Quitamos el puerto 1883 sin cifrar; el único servicio expuesto es el 8883 con TLS. |
| | **I9** Configuración por defecto insegura | En la práctica 3 el broker tenía `allow_anonymous true`. Ahora exige usuario y contraseña. |
| **2. AES-128-GCM en `pot/value`** | **I7** Transferencia de datos insegura | El valor viaja cifrado de extremo a extremo: ni el broker lo puede leer. Y gracias al tag, cualquier alteración se detecta y se rechaza (lo comprobamos con la prueba de ruptura). |
| | **I6** Protección de la privacidad insuficiente (parcial) | La telemetría solo la puede leer quien tiene la clave, aunque pase por un broker de terceros. |
| **3. Credenciales en NVS + aprovisionamiento** | **I1** Contraseñas escritas en el código | El SSID, la contraseña del WiFi, el usuario/contraseña del broker y la clave AES ya no están en el `.ino` ni en el binario (lo verificamos buscándolos dentro del `.bin`). Además las contraseñas del broker son aleatorias, no "admin/1234". |
| | **I9** Configuración por defecto insegura | Un ESP32 recién flasheado no trae ninguna credencial por defecto: si no se aprovisiona, no se conecta a nada. |

## Vulnerabilidades que siguen abiertas

| ID | Qué sigue abierto en nuestro sistema |
|---|---|
| **I10** Falta de protección física | La NVS **no está cifrada** y el ESP32 no tiene *flash encryption* ni *secure boot*. Alguien con el ESP32 en la mano puede leer toda la flash por USB (`esptool read-flash`) y sacar el WiFi, la contraseña del broker y la clave AES. |
| **I4** Falta de actualización segura | No hay OTA ni firmware firmado: se actualiza por cable y el ESP32 acepta cualquier firmware que le flasheen. |
| **I3** Interfaces del ecosistema inseguras | El editor y el dashboard de Node-RED (puerto 1880) **no tienen contraseña** y van por HTTP sin cifrar. Cualquiera en la red puede abrirlos, ver el flujo o mover el LED. |
| **I8** Falta de gestión de dispositivos | Todos los ESP32 comparten el mismo usuario MQTT y la misma clave AES: si se compromete uno, no se le pueden revocar las credenciales solo a él. Tampoco hay un inventario ni monitoreo de dispositivos. |
| **I7** (parcial) Transferencia insegura | Solo `pot/value` va cifrado con AES; los comandos de `light/control` dependen únicamente del TLS. Además Mosquitto **no tiene ACLs**: cualquier usuario autenticado puede publicar en cualquier tópico (por eso la prueba de ruptura pudo inyectar un mensaje en `pot/value`), y no hay protección contra **replay**: un mensaje cifrado válido se podría reenviar más tarde y se aceptaría. |
| **I5** Componentes inseguros u obsoletos (parcial) | `PubSubClient` ya no recibe mantenimiento (última versión de 2020) y Node.js 25 no es versión LTS. |

## Qué haríamos para cerrarlas

- **I10:** activar *flash encryption* + *secure boot* del ESP32 y usar la NVS cifrada.
- **I4:** OTA con firmware firmado.
- **I3:** `adminAuth` en Node-RED y HTTPS.
- **I8 / I7:** un usuario MQTT y una clave por dispositivo, ACLs en Mosquitto (cada quien solo publica en sus tópicos), y meter un contador o timestamp como dato autenticado (AAD) en el AES-GCM para rechazar mensajes repetidos.

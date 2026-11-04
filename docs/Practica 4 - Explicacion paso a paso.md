# Práctica 4 - Explicación paso a paso de la adaptación

Partimos del código de la práctica 3 sin tocar el circuito ni el comportamiento del LED RGB, el botón y el potenciómetro. Todo lo que cambiamos fue de seguridad. Para que el código quedara más ordenado, separamos el sketch en archivos por responsabilidad:

| Archivo | Qué hace |
|---|---|
| `practica4.ino` | Lo mismo que la práctica 3 (LED, botón, comandos) + la conexión segura |
| `credenciales.h/.cpp` | Leer las credenciales de la NVS y el modo de aprovisionamiento |
| `cifrado_aes.h/.cpp` | Cifrar con AES-128-GCM |
| `ca_cert.h` | El certificado público de nuestra CA |

---

## Paso 1: Comunicación cifrada con TLS (MQTTS)

Lo primero fue crear nuestra propia Autoridad Certificadora con OpenSSL. Hicimos un script (`certs/generar_certs.sh`) que genera la CA y luego el certificado del broker firmado por ella. Algo que tuvimos que cuidar es que el ESP32 se conecta al broker **por IP**, así que esa IP (`172.20.10.2`, la de la compu en el hotspot) tiene que venir dentro del certificado, en el campo SAN; si no, la verificación falla aunque la CA sea la correcta.

Después configuramos Mosquitto: quitamos el listener del 1883 (el que usábamos en la práctica 3, sin cifrar y anónimo) y dejamos solo el **8883 con TLS**, apuntando a los certificados. De paso le quitamos el `allow_anonymous true` y creamos dos usuarios con contraseñas aleatorias: `esp32` para el dispositivo y `panel` para Node-RED y las pruebas.

En el firmware cambiamos `WiFiClient` por `WiFiClientSecure` y le cargamos el certificado de la CA con `setCACert()`. Antes de conectarnos al broker agregamos `sincronizarHora()`, que consulta NTP y espera a tener la fecha real, porque sin fecha el ESP32 no puede saber si el certificado está vigente. Si el handshake TLS falla, en vez de quedarnos callados llamamos a `espClient.lastError()` y mostramos el error en el monitor serial. Nunca usamos `setInsecure()`.

Lo probamos desde la terminal con `mosquitto_pub`/`mosquitto_sub`:

- Con `--cafile ca.crt` (la CA correcta) el mensaje llegó sin problema.
- Sin certificado, y con una **CA falsa** que generamos a propósito, la conexión fue rechazada (en el log del broker sale `tlsv1 alert unknown ca` y `openssl s_client` dice `certificate verify failed`).
- De extra: con la CA correcta pero contraseña incorrecta, el broker responde `not authorised`.

**Problema que nos encontramos:** el ESP32 se quedaba esperando NTP para siempre. Hicimos un sketch de diagnóstico y vimos que el DNS y el NTP sí funcionaban; el problema era que la Mac se salía sola del hotspot del iPhone (para regresarse a la red de la escuela) y el hotspot se apagaba. También cambiamos el orden de los servidores NTP (primero `time.google.com`, y uno por IP de respaldo) porque `pool.ntp.org` a veces nos daba un servidor lejano que no contestaba.

## Paso 2: Cifrado del contenido con AES-128-GCM

Sin quitar el TLS, ahora el valor del potenciómetro se cifra antes de publicarlo. En `cifrado_aes.cpp` usamos `mbedtls_gcm` (que en el ESP32 usa el acelerador AES por hardware):

1. Generamos un **nonce de 12 bytes aleatorio** para cada mensaje con `esp_fill_random`, así nunca se repite con la misma clave.
2. Ciframos el texto (por ejemplo `"42.5"`) y obtenemos el **tag de 16 bytes**.
3. Juntamos `nonce | cifrado | tag` en un solo buffer y lo codificamos en **Base64**.

Si por alguna razón el cifrado falla, el ESP32 **no publica nada** en vez de mandar el valor en claro.

En Node-RED agregamos un nodo function, "Descifrar AES-GCM", entre el `mqtt in` de `pot/value` y la gráfica. Separa el Base64 en nonce, cifrado y tag, descifra con el módulo `crypto` y llama a `setAuthTag()`: si el tag no cuadra, `final()` truena y el mensaje se manda a una segunda salida que muestra **RECHAZADO** en el dashboard, en vez de mandar un valor corrupto a la gráfica. La clave no está escrita en el flujo: Node-RED la lee de una variable de entorno (`AES_KEY_HEX`) que carga el script `herramientas/iniciar_nodered.sh`.

Para la **prueba de ruptura** hicimos `herramientas/prueba_ruptura.py`: agarra un mensaje real, le cambia **un solo byte** de la parte cifrada y lo vuelve a publicar. Node-RED lo rechazó por tag inválido, mientras que todos los mensajes legítimos se siguieron mostrando normal.

## Paso 3: Credenciales fuera del código

En la práctica 3 ya habíamos sacado las credenciales a un `secrets.h` que no se subía al repo, pero seguían quedando **compiladas dentro del binario**. Ahora viven en la **NVS** del ESP32:

- `cargarOAprovisionarCredenciales()` revisa si en la NVS existe la bandera `listo`.
- Si no existe, entra al **modo de aprovisionamiento**: por el Monitor Serial pide el SSID, la contraseña del WiFi, la IP del broker, el usuario y contraseña de MQTT y la clave AES (32 caracteres hex, y la valida). Las contraseñas no se repiten en pantalla, solo salen asteriscos.
- Guarda todo con `Preferences` y escribe `listo = true` **al final a propósito**: si se fuera la luz a medio guardar, el ESP32 vuelve a pedir todo en vez de arrancar con credenciales a medias.
- Se reinicia, y a partir de ahí el modo de aprovisionamiento **ya no se activa nunca**. Para reaprovisionarlo hay que borrar la flash completa.

Para no teclear todo a mano hicimos `herramientas/aprovisionar.py`, que lee los valores de `.secrets/credenciales.env` (que tampoco se sube al repo) y se los contesta al ESP32 por serial.

Otra mejora: el Client ID de MQTT ahora se arma con la **MAC** del ESP32, así que cada placa tiene uno distinto sin configurar nada (en la práctica 3 lo tuvimos que cambiar a mano para el segundo ESP32).

Para comprobarlo de forma verificable, compilamos el firmware y buscamos dentro del `.bin` el SSID, la contraseña del WiFi, la del broker y la clave AES (como texto y como bytes): **ninguna aparece**. Lo único "sensible" que trae es el certificado público de la CA, que no es secreto.

**Problema que nos encontramos:** al flashear, el ESP32 no entraba a modo descarga y la Mac recibía basura por el puerto serial. Con un script vimos que el chip se estaba **reiniciando en loop** (`POWERON_RESET` varias veces por segundo): le faltaba energía por el circuito. Al desconectar la protoboard y revisar las conexiones se arregló.

## Paso 4: Auditoría

Clasificamos cada medida con el OWASP IoT Top 10 y listamos lo que sigue abierto (protección física, actualizaciones, Node-RED sin contraseña, falta de ACLs y de protección contra replay). Está completo en [Práctica 4 - Auditoría OWASP](Practica%204%20-%20Auditoria%20OWASP.md).

# Práctica 4 - Marco Teórico

Los conceptos detrás de cada medida de seguridad, y cómo los aplicamos en el proyecto.

---

## TLS y MQTTS

**TLS** (Transport Layer Security) es el protocolo que cifra y autentica una conexión TCP; es lo mismo que pone el candadito en HTTPS. **MQTTS** no es un protocolo nuevo: es MQTT normal pero viajando adentro de un túnel TLS. Por convención usa el puerto **8883** (el MQTT sin cifrar usa el 1883).

Antes de mandar cualquier dato, cliente y servidor hacen el **handshake** (como lo vimos en clase, es donde truenan casi todos los errores de MQTTS):

1. El broker le manda al cliente su **certificado**.
2. El cliente revisa que ese certificado esté firmado por una CA en la que confía, que esté vigente, y que sea para la dirección a la que se está conectando.
3. Si todo cuadra, negocian unas llaves de sesión y desde ahí todo lo que viaja va cifrado.

Si el paso 2 falla, la conexión se corta ahí mismo: eso es justo lo que demostramos con la CA falsa.

**En la práctica:** Mosquitto ya solo escucha en el 8883 con TLS (quitamos el 1883), y el ESP32 se conecta con `WiFiClientSecure` en vez de `WiFiClient`.

## Autoridad Certificadora (CA) y certificados X.509

Un certificado es básicamente "esta llave pública es de este servidor", firmado por alguien de confianza: la **CA**. En internet las CAs son empresas (Let's Encrypt, DigiCert...); en nuestro caso creamos **nuestra propia CA** con OpenSSL, porque el broker vive en una red local y ninguna CA pública firmaría algo para una IP privada.

- `ca.key` → llave **privada** de la CA. Con ella se firman certificados. Nunca sale de la compu.
- `ca.crt` → certificado **público** de la CA. Es lo que el ESP32 trae adentro (`setCACert()`) para verificar al broker. No es un secreto.
- `server.crt` / `server.key` → certificado y llave del broker, firmados por nuestra CA.

Un detalle importante es el **SAN** (Subject Alternative Name): el certificado dice para qué direcciones es válido. Como el ESP32 se conecta por IP, la IP de la compu (`172.20.10.2`) tiene que venir en el SAN; si la compu cambia de IP, hay que regenerar el certificado del broker (para eso está `certs/generar_certs.sh`).

## Por qué NTP antes de TLS

Para validar un certificado hay que saber **qué fecha es** (los certificados tienen fecha de inicio y de vencimiento). El ESP32 no tiene reloj con batería: al prender cree que es 1970, y con esa fecha cualquier certificado se ve "todavía no válido". Por eso primero sincroniza la hora con **NTP** (Network Time Protocol) y hasta después intenta la conexión TLS.

## `setInsecure()` (y por qué no se usa)

`WiFiClientSecure` tiene un método `setInsecure()` que cifra el canal pero **no verifica** el certificado del servidor. Suena a "medio seguro", pero en realidad deja la puerta abierta a un ataque **man-in-the-middle**: cualquiera podría hacerse pasar por el broker y el ESP32 le hablaría feliz. Por eso usamos `setCACert()` y, si la validación falla, el ESP32 reporta el error TLS en el monitor serial en vez de conectarse.

## Autenticación en el broker

Además del TLS, Mosquitto ahora exige **usuario y contraseña** (`allow_anonymous false` + `password_file`). Las contraseñas no se guardan en claro: `mosquitto_passwd` guarda un hash. TLS protege el canal; el usuario/contraseña decide **quién** puede entrar.

## AES-128-GCM (cifrado autenticado)

**AES** es un cifrado simétrico: la misma clave (128 bits = 16 bytes) cifra y descifra. **GCM** (Galois/Counter Mode) es un modo de AES que además de cifrar **autentica**: produce un **tag** de 16 bytes que funciona como "sello". Si alguien cambia aunque sea un bit del mensaje, el tag ya no cuadra y el descifrado falla. A esto se le llama cifrado autenticado (AEAD): da **confidencialidad e integridad** al mismo tiempo.

Las piezas de cada mensaje:

| Pieza | Tamaño | Para qué |
|---|---|---|
| Nonce | 12 bytes | Número aleatorio, distinto en cada mensaje. **Nunca** se debe repetir con la misma clave: si se repite, se puede deducir información del texto y falsificar tags. |
| Cifrado | igual que el texto | El valor del potenciómetro ya cifrado. |
| Tag | 16 bytes | El sello de autenticidad. |

Todo eso se junta y se codifica en **Base64** para que viaje como texto en MQTT.

En el ESP32 usamos `mbedtls_gcm`, que aprovecha el **acelerador AES por hardware** del chip, y el nonce sale del generador de números aleatorios por hardware (`esp_fill_random`).

## ¿Para qué AES si ya hay TLS? (defensa en profundidad)

TLS protege cada **tramo** de la comunicación (ESP32 → broker y broker → Node-RED), pero **el broker sí ve los datos en claro** en medio. Con AES-GCM el valor del potenciómetro va cifrado **de extremo a extremo**: el broker solo ve Base64 ilegible, y solo quien tiene la clave (Node-RED) lo puede leer. Si el broker se viera comprometido, la telemetría seguiría protegida.

## NVS y la librería Preferences

La **NVS** (Non-Volatile Storage) es una partición de la flash del ESP32 que funciona como un diccionario clave-valor: lo que guardas ahí sobrevive a reinicios y a reflashear el firmware (mientras no se borre la flash completa). La librería **Preferences** de Arduino es la forma fácil de usarla (`putString`, `getBytes`, etc.).

## Aprovisionamiento

Es el proceso de cargarle a un dispositivo sus credenciales **después** de fabricarlo/flashearlo, en vez de meterlas al código. Así el mismo firmware sirve para cualquier ESP32 y ningún secreto queda en el repositorio. Lo importante es que el mecanismo se **cierre** al terminar: en nuestro caso, una vez guardadas las credenciales el modo de aprovisionamiento ya no vuelve a aparecer.

## OWASP IoT Top 10

Lista de la fundación OWASP con las 10 vulnerabilidades más comunes en dispositivos IoT (versión 2018):

| ID | Vulnerabilidad |
|---|---|
| I1 | Contraseñas débiles, adivinables o escritas en el código |
| I2 | Servicios de red inseguros |
| I3 | Interfaces del ecosistema inseguras (web, nube, apps) |
| I4 | Falta de un mecanismo de actualización seguro |
| I5 | Uso de componentes inseguros u obsoletos |
| I6 | Protección de la privacidad insuficiente |
| I7 | Transferencia y almacenamiento de datos inseguros |
| I8 | Falta de gestión de dispositivos |
| I9 | Configuración por defecto insegura |
| I10 | Falta de protección física |

La clasificación de nuestras medidas está en [Práctica 4 - Auditoría OWASP](Practica%204%20-%20Auditoria%20OWASP.md).

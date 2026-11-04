# Práctica 4 - Entendiendo el código

Lo explicamos en el mismo orden en que el ESP32 lo ejecuta. El código está partido en 4 archivos, cada uno con una sola responsabilidad:

| Archivo | En una frase |
|---|---|
| `practica4.ino` | El "director": arranca todo y tiene la lógica de la práctica 3 (LED, botón, comandos) |
| `credenciales.cpp` | Saca las contraseñas de la NVS, o las pide la primera vez |
| `cifrado_aes.cpp` | Convierte `"42.5"` en un Base64 cifrado |
| `ca_cert.h` | El certificado público de nuestra CA, para saber si el broker es de confianza |

---

## 1. Lo que pasa al prender el ESP32 (`setup()`)

```cpp
creds = cargarOAprovisionarCredenciales();
armarClientId();
construirTopicos();
conectarWiFi();
sincronizarHora();
espClient.setCACert(CA_CERT);
mqttClient.setServer(creds.mqttHost.c_str(), MQTT_BROKER_PORT);
reconectarMQTT();
```

El orden **importa**:

1. **Credenciales primero**, porque sin ellas no sabe ni a qué WiFi conectarse.
2. **WiFi**, porque NTP y MQTT necesitan red.
3. **NTP antes que MQTT**, porque TLS necesita la fecha real para revisar si el certificado está vigente (el ESP32 al prender cree que es 1970).
4. **`setCACert` antes de conectar**, para que en el handshake ya sepa contra qué CA comparar el certificado del broker.

## 2. Las credenciales (`credenciales.cpp`)

### La estructura

```cpp
struct Credenciales {
  String wifiSsid;
  String wifiPassword;
  String mqttHost;
  String mqttUsuario;
  String mqttPassword;
  uint8_t claveAes[16];
};
```

Es una "cajita" que agrupa todo lo secreto. En vez de tener 6 variables globales sueltas, tenemos una sola (`creds`) y la llenamos de golpe desde la NVS. La clave AES es `uint8_t[16]` (16 bytes crudos) y no un `String`, porque AES trabaja con bytes, no con texto.

### `cargarOAprovisionarCredenciales()`

```cpp
nvs.begin(NVS_NAMESPACE, true);          // true = solo lectura
bool listo = nvs.getBool(CLAVE_LISTO, false);
nvs.end();

if (!listo)
  aprovisionarPorSerial();               // la primera vez: pide todo y reinicia
```

- La NVS se organiza por **namespaces** (como carpetas). El nuestro se llama `"practica4"`.
- `getBool("listo", false)`: si la clave `listo` no existe todavía, regresa `false` (el segundo parámetro es el valor por defecto). Así sabemos si es la primera vez.
- Si ya está `listo`, lee cada credencial con `getString` y la clave AES con `getBytes`.

### `aprovisionarPorSerial()`

Pide cada dato con `leerLinea(...)` y al final:

```cpp
nvs.putString("wifi_ssid", ssid);
...
nvs.putBytes("aes_key", clave, sizeof(clave));
nvs.putBool(CLAVE_LISTO, true);   // ¡al final!
ESP.restart();
```

**¿Por qué `listo` va al último?** Si se va la luz a medio guardar, `listo` todavía no existe y en el siguiente arranque vuelve a pedir todo. Si lo guardáramos primero, podría arrancar "listo" pero con la mitad de las credenciales vacías.

**¿Por qué el aprovisionamiento "deja de estar activo"?** Porque la única forma de llegar a `aprovisionarPorSerial()` es que `listo` sea `false`, y después de la primera vez siempre es `true`. Ni un comando ni un botón lo vuelven a activar; habría que borrar la flash completa.

### `leerLinea()` y `hexAClave()`

- `leerLinea` junta caracteres del serial hasta que llega un Enter (`\n`). Ignora `\r` (Windows manda `\r\n`) y no acepta líneas vacías. Si el dato es un secreto, en pantalla solo imprime `********`.
- `hexAClave` convierte el texto `"a1b2c3..."` (32 caracteres) en 16 bytes. Primero revisa que sean exactamente 32 y que todos sean hexadecimales (`isxdigit`); luego toma de 2 en 2 y los convierte con `strtoul(..., 16)`. Por ejemplo `"a1"` → `161`.

## 3. La conexión segura (`practica4.ino`)

### `WiFiClientSecure` en vez de `WiFiClient`

```cpp
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
```

`PubSubClient` no sabe nada de TLS: solo habla MQTT sobre "un cliente de red". En la práctica 3 le dábamos un `WiFiClient` (TCP normal); ahora le damos un `WiFiClientSecure` (TCP con TLS). Es lo único que hay que cambiar para que **todo** MQTT vaya cifrado. Así funcionan las capas: MQTT encima, TLS en medio, TCP abajo.

### `sincronizarHora()`

```cpp
configTime(ZONA_HORARIA_SEG, 0, "time.google.com", "pool.ntp.org", "216.239.35.0");
while (time(nullptr) < HORA_MINIMA_VALIDA) { delay(500); ... }
```

- `configTime` arranca el cliente NTP con 3 servidores (si uno no contesta, prueba el siguiente). El tercero va por IP por si falla el DNS.
- `time(nullptr)` da los segundos desde 1970. Mientras sea menor a `1700000000` (noviembre 2023), sabemos que NTP todavía no ha contestado, así que esperamos.

### `reconectarMQTT()`

```cpp
mqttClient.connect(clientId,
                   creds.mqttUsuario.c_str(), creds.mqttPassword.c_str(),
                   topicDeviceStatus, 0, true, "OFFLINE");   // LWT
```

Es igual que en la práctica 3 pero con usuario/contraseña. Dentro de `connect()` pasa el **handshake TLS**: si el certificado del broker no está firmado por nuestra CA, si ya venció, o si la IP no viene en el certificado, `connect()` regresa `false`. En ese caso:

```cpp
void reportarErrorTls() {
  char detalle[128];
  int codigo = espClient.lastError(detalle, sizeof(detalle));
  if (codigo != 0)
    Serial.printf("  !! Error TLS %d: %s\n", codigo, detalle);
}
```

Esto cumple lo de "rechazar o reportar explícitamente cualquier fallo de validación".

### `armarClientId()`

```cpp
uint64_t mac = ESP.getEfuseMac();
snprintf(clientId, sizeof(clientId), "ESP32-%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
```

La MAC es de 48 bits y viene grabada de fábrica, así que es única por chip. La partimos en dos (`>> 32` para los bytes altos, el cast a `uint32_t` para los bajos) porque `printf` no imprime bien números de 64 bits en el ESP32. Resultado: algo como `ESP32-EC2D84077000`.

## 4. El cifrado (`cifrado_aes.cpp`)

```cpp
uint8_t paquete[MAX_PAQUETE];
uint8_t *nonce = paquete;                 // bytes 0..11
uint8_t *cifrado = paquete + NONCE_LEN;   // bytes 12..(12+largo-1)
uint8_t *tag = cifrado + largo;           // los últimos 16
```

Truco de clean code: en vez de crear 3 arreglos y luego copiarlos a uno solo, armamos **un solo buffer** y usamos punteros que apuntan a cada parte. Así mbedtls escribe directo en su lugar y el paquete ya queda listo como `nonce | cifrado | tag`.

```cpp
esp_fill_random(nonce, NONCE_LEN);
```

Llena el nonce con números aleatorios del generador por **hardware** del ESP32. Nonce nuevo en cada mensaje: por eso el mismo `0.0` sale distinto cada vez.

```cpp
mbedtls_gcm_init(&gcm);
mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, clave, 128);
mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, largo,
                          nonce, NONCE_LEN,
                          nullptr, 0,              // sin AAD
                          textoPlano, cifrado,
                          TAG_LEN, tag);
mbedtls_gcm_free(&gcm);
```

1. `init` prepara el contexto, `setkey` le pone la clave de 128 bits.
2. `crypt_and_tag` hace todo de una: cifra el texto y calcula el tag.
3. `free` limpia la memoria (borra la clave del contexto). Se llama **siempre**, aunque algo haya fallado.

Al final `mbedtls_base64_encode` lo pasa a texto. Si cualquier paso falla, la función regresa `""` y en `publicarPot()`:

```cpp
if (cifrado.isEmpty()) {
  Serial.println("!! No se pudo cifrar el potenciometro, no se publica nada");
  return;
}
```

Esto se llama **fallar de forma segura** (*fail closed*): si no se puede cifrar, mejor no mandar nada que mandar el valor en claro.

## 5. El descifrado en Node-RED (nodo "Descifrar AES-GCM")

```js
const paquete = Buffer.from(String(msg.payload), "base64");
const nonce   = paquete.subarray(0, 12);
const cifrado = paquete.subarray(12, paquete.length - 16);
const tag     = paquete.subarray(paquete.length - 16);

const descifrador = crypto.createDecipheriv("aes-128-gcm", clave, nonce);
descifrador.setAuthTag(tag);
const texto = Buffer.concat([descifrador.update(cifrado), descifrador.final()]);
```

Es el espejo exacto del ESP32: decodifica el Base64 y corta el paquete en las mismas 3 partes. La clave del tema es `final()`: ahí es donde Node.js compara el tag. Si no coincide (alguien cambió aunque sea un bit) **lanza una excepción**, el `catch` la atrapa y el mensaje se va por la salida 2 ("RECHAZADO") en vez de llegar a la gráfica.

La clave sale de `env.get("AES_KEY_HEX")`, una variable de entorno que pone `iniciar_nodered.sh`. Por eso el flujo se puede subir al repo sin exponer nada.

## 6. Lo que NO cambió

`manejarIluminacion()`, `manejarBoton()`, `alRecibirMensajeMQTT()`, `publicarEstadoLuz()` son idénticas a la práctica 3. La práctica pedía no modificar el comportamiento del LED, el botón y el potenciómetro, y así fue: todo lo nuevo está "alrededor" (cómo se conecta y cómo se protegen los datos), no en la lógica.

## Preguntas que nos pueden hacer

- **¿Por qué el certificado de la CA sí está en el código y no es un problema?** Porque es público: sirve para *verificar* firmas, no para *firmar*. La llave privada (`ca.key`) nunca sale de la compu.
- **¿Qué pasa si alguien copia el `.bin`?** No se lleva nada: las credenciales están en la NVS de cada ESP32, no en el firmware.
- **¿Y si alguien se roba el ESP32?** Ahí sí podría leer la flash con `esptool` y sacar la NVS, porque no está cifrada. Es una de las vulnerabilidades que quedan abiertas (I10).
- **¿Por qué GCM y no AES normal (CBC)?** Porque CBC solo cifra; si alguien cambia bytes, descifras basura sin enterarte. GCM además autentica con el tag, y por eso pudimos detectar la ruptura.

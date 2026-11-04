# Práctica 4 - Arquitectura de seguridad

## Vista general: canal cifrado + payload cifrado

```mermaid
flowchart LR
    subgraph ESP32
        POT[Potenciómetro] --> AES[AES-128-GCM<br/>nonce aleatorio por mensaje]
        NVS[(NVS<br/>WiFi · MQTT · clave AES)]
        CA1[ca.crt pública<br/>setCACert]
    end

    AES -- "MQTTS :8883<br/>TLS + usuario esp32" --> BROKER[(Mosquitto<br/>certificado firmado por nuestra CA<br/>allow_anonymous false)]
    BROKER -- "MQTTS :8883<br/>TLS + usuario panel" --> NR[Node-RED<br/>Descifrar AES-GCM<br/>verifica el tag]
    NR -- "tag OK" --> DASH[Dashboard: gauge y gráfica]
    NR -- "tag inválido" --> ALERTA[Dashboard: RECHAZADO]
```

## Qué protege cada capa

| Capa | Dónde | Protege contra |
|---|---|---|
| TLS (MQTTS) | Cada tramo: ESP32↔broker y broker↔Node-RED | Que alguien en la red lea o modifique el tráfico; que alguien se haga pasar por el broker |
| Usuario/contraseña en Mosquitto | Al conectarse al broker | Que cualquiera se conecte al broker |
| AES-128-GCM | De extremo a extremo: ESP32 → Node-RED (solo `pot/value`) | Que el broker (o quien lo controle) lea el valor; que alguien altere el valor sin que se note |
| NVS + aprovisionamiento | Dentro del ESP32 | Que las credenciales queden en el código fuente, en el repo o en el binario |

Lo que ve cada quien de un mensaje de `pot/value`:

| Quién | Qué ve |
|---|---|
| Alguien escuchando la red | Basura cifrada por TLS |
| El broker | `tZZsxPLLcxFr78fIXllvYE9aQHbsyATVf616rjKdhQ==` (Base64 cifrado con AES) |
| Node-RED (tiene la clave) | `42.5` |

## Flujo de arranque del ESP32

```mermaid
flowchart TD
    A[Arranque] --> B{¿NVS tiene<br/>listo = true?}
    B -- No --> C[Modo aprovisionamiento<br/>por Monitor Serial]
    C --> D[Guarda credenciales en NVS<br/>y al final listo = true]
    D --> E[Reinicia]
    E --> A
    B -- Sí --> F[Carga credenciales de NVS]
    F --> G[Conecta WiFi]
    G --> H[Sincroniza hora por NTP]
    H --> I[Handshake TLS con el broker<br/>valida certificado vs nuestra CA]
    I -- Falla --> J[Reporta error TLS<br/>y reintenta]
    J --> I
    I -- OK --> K[MQTT con usuario/contraseña<br/>publica ONLINE]
```

## Flujo de aprovisionamiento

```mermaid
sequenceDiagram
    participant PC as Compu (aprovisionar.py)
    participant E as ESP32
    participant N as NVS
    E->>N: ¿listo == true?
    N-->>E: no
    E->>PC: === MODO APROVISIONAMIENTO ===
    loop por cada credencial
        E->>PC: WIFI_SSID: / WIFI_PASSWORD: / MQTT_HOST: ...
        PC->>E: valor + Enter
    end
    E->>N: guarda todo
    E->>N: listo = true (al final, a propósito)
    E->>E: reinicia
    Note over E,N: En los siguientes arranques listo == true,<br/>el modo de aprovisionamiento ya no aparece
```

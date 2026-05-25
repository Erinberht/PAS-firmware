# Firmware ESP32 — PAS

Repositorio: **https://github.com/leocarlos10/PAS-firmware**

Firmware Arduino para los dispositivos ESP32 del sistema de seguridad perimetral.

## Estructura

```
Firmware-ESP32/
├── README.md
└── zona2/
    ├── zona2.ino          # Sketch principal (Zona 2)
    ├── secrets.example.h  # Plantilla de credenciales (sí se sube a Git)
    └── secrets.h          # Credenciales reales (NO se sube a Git)
```

## Requisitos

- [Arduino IDE](https://www.arduino.cc/en/software) 2.x o PlatformIO
- Placa: **ESP32 Dev Module**
- Librerías:
  - `PubSubClient` (Nick O'Leary)
  - `ArduinoJson` (Benoit Blanchon)

## Configuración local

1. Abre `Firmware-ESP32/zona2/zona2.ino` en Arduino IDE.
2. Copia `secrets.example.h` → `secrets.h` y completa WiFi/MQTT.
3. Selecciona placa ESP32 y el puerto COM correcto.
4. Compila y sube.

## Topics MQTT (Zona 2)

| Topic | Dirección | Payload ejemplo |
|-------|-----------|-----------------|
| `jardin/zona2/comando` | Backend → ESP32 | `{"accion":"ARMAR"}` |
| `jardin/zona2/estado` | ESP32 → Backend | `ARMADA` / `DESARMADA` |
| `jardin/zona2/eventos` | ESP32 → Backend | `{"codigo_sensor":"Z2-MAG-01",...}` |

Estos topics deben coincidir con `topic_comando`, `topic_estado` y los sensores en la base de datos (`datos_prueba.sql`).

## Pines (Zona 2)

| Pin | Componente |
|-----|------------|
| 27 | Sensor magnético (Reed) |
| 26 | Buzzer |
| 14 | Sensor PIR |

## Subir a GitHub

Repositorio remoto: `https://github.com/leocarlos10/PAS-firmware`

```bash
cd Firmware-ESP32
git push -u origin main
```

## Notas de seguridad

- **Nunca** subas `secrets.h` a GitHub (contiene WiFi y contraseña MQTT).
- Si ya expusiste credenciales en un commit, cámbialas en el broker/WiFi y usa `git filter-repo` o rota las claves.

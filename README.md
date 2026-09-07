# ⚡ CYD Power Monitor

Sistema de monitoreo y automatización energética para respaldo eléctrico (red / generador / inversor), construido sobre una placa **CYD (Cheap Yellow Display) ESP32-2432S028, variante 2USB**.

Monitorea el consumo eléctrico en tiempo real, detecta automáticamente si la fuente activa es la red pública o el generador/inversor, guarda un historial persistente de esos cambios, y administra dispositivos inteligentes Tasmota/OpenBeken con automatización basada en la fuente de energía disponible.

![Firmware version](https://img.shields.io/badge/firmware-v2.1-00d4ff)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)

---

## 📋 Características

### Monitoreo eléctrico
- Lectura en tiempo real de voltaje, corriente, potencia, energía acumulada, frecuencia y factor de potencia vía **PZEM-004T v3.0**
- Detección del estado del **ATS** (Automatic Transfer Switch) — Red Eléctrica vs. Generador — con anti-rebote por hardware
- Historial de transiciones de fuente, **persistente en flash (SPIFFS)** — sobrevive apagones y reinicios
- Gráfica de tendencia diaria (Red vs. Generador) generada en el propio servidor, sin librerías externas
- Exportación del historial a CSV

### Administrador de dispositivos inteligentes
- Compatible con **Tasmota** y **OpenBeken** (mismo protocolo HTTP: `cm?cmnd=...`)
- Lectura de consumo en tiempo real (V/A/W) para dispositivos con medición de energía (ej. Sonoff S40TPB)
- Programación de horarios de encendido/apagado por dispositivo
- **Automatización según la fuente activa del ATS**:
  - Apagar automáticamente al pasar a generador
  - Encender solo en red eléctrica (apagar en generador)
  - Modo nocturno: encender solo de noche *y* con red eléctrica disponible
- **Respeta el botón físico** de cada dispositivo — si detecta un cambio externo (botón del equipo, app del fabricante), la automatización no lo corrige durante una ventana de tiempo configurable
- Indicador de "sin conexión" cuando un dispositivo deja de responder
- Registro de actividad reciente (qué se encendió/apagó, cuándo y por qué: manual, horario o automatización)

### Pantalla física
- Brillo automático según luz ambiental (sensor LDR), con filtrado de ruido
- Interfaz local en la propia pantalla TFT de la CYD

### Dashboard web
- Panel principal con actualización en vivo vía AJAX (sin recargar la página)
- Histórico ATS con filtros de tiempo, estadísticas y línea temporal de eventos
- Actualización de firmware OTA (sin necesidad de cable USB tras la primera carga)
- Sincronización de hora robusta: NTP por internet + respaldo automático con la hora del teléfono (funciona incluso sin internet, que es justo cuando más importa durante un corte de luz)

### Arquitectura
- **Dual-core**: la pantalla y el servidor web corren en un núcleo del ESP32, mientras que la lectura del PZEM y todo el administrador de dispositivos (peticiones HTTP, automatización) corren en tareas FreeRTOS independientes en el otro núcleo — ninguna espera de red congela la interfaz
- Acceso concurrente protegido con mutex

---

## 🔧 Hardware

| Componente | Modelo |
|---|---|
| Placa principal | ESP32-2432S028 "Cheap Yellow Display", variante **2USB** |
| Medidor de energía AC | PZEM-004T v3.0 |
| Transferencia automática | ATS con contacto auxiliar seco (dry contact) |
| Dispositivos controlables | Cualquier smart plug con firmware Tasmota u OpenBeken |

### Pines utilizados

| Función | Pin |
|---|---|
| PZEM RX | GPIO27 |
| PZEM TX | GPIO22 |
| Señal ATS (contacto auxiliar) | GPIO35 *(input-only, requiere resistencia pull-down externa de 10kΩ)* |
| LED RGB de estado | GPIO4 / GPIO16 / GPIO17 |
| Sensor de luz (LDR) | GPIO34 |
| Backlight (brillo de pantalla) | GPIO21 |
| Botón físico (reset WiFi) | GPIO0 (BOOT) |

> El pinout completo de la pantalla (SPI, touch) sigue la configuración oficial de [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) para la variante `CYD2USB`.

---

## 🚀 Instalación

### Opción A — Compilar en la nube (recomendado, no requiere Arduino IDE)

Este repositorio incluye un workflow de **GitHub Actions** que compila el firmware automáticamente:

1. Sube cualquier cambio a la carpeta `CYD_Power_Monitor/`
2. Ve a la pestaña **Actions** de este repositorio
3. Cuando termine la ejecución (✅ verde), descarga el artefacto `firmware-cyd-power-monitor`
4. Descomprime y obtén el archivo `.bin`
5. Súbelo desde la pestaña **Actualización** del dashboard web del dispositivo (OTA, sin cable)

### Opción B — Compilar localmente con Arduino IDE

**Librerías requeridas** (Gestor de Librerías):
- `WiFiManager` (tzapu)
- `ArduinoJson` (bblanchon)
- `TFT_eSPI` (Bodmer)
- `PZEM004Tv30` (mandulaj)

**Configuración de `TFT_eSPI`:** reemplaza el `User_Setup.h` de la librería con el de la variante CYD2USB:
```
https://raw.githubusercontent.com/witnessmenow/ESP32-Cheap-Yellow-Display/main/DisplayConfig/CYD2USB/User_Setup.h
```

**Placa y partición:**
- Board: `ESP32 Dev Module`
- Partition Scheme: **`Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)`**

Abre `CYD_Power_Monitor/CYD_Power_Monitor.ino` (los archivos `device_manager.h` y `device_pages.h` deben estar en la misma carpeta) y compila/sube normalmente.

### Primer arranque

1. Al encender por primera vez, el dispositivo crea un punto de acceso WiFi (`CYD-Monitor-XXXX`) — conéctate y selecciona tu red
2. Una vez conectado, la pantalla muestra la IP asignada
3. Abre esa IP en el navegador para acceder al dashboard

---

## 📁 Estructura del proyecto

```
CYD_Power_Monitor/
├── CYD_Power_Monitor.ino   # Setup, loop, PZEM, ATS, pantalla TFT, páginas web principales
├── device_manager.h        # Lógica de dispositivos: persistencia, control HTTP, automatización, brillo
└── device_pages.h          # Página web /devices y sus endpoints API
```

---

## 🌐 Páginas y endpoints

| Ruta | Descripción |
|---|---|
| `/` | Dashboard principal (PZEM + ATS en vivo) |
| `/history` | Histórico ATS con filtros, gráfica de tendencia y exportación CSV |
| `/history.csv` | Descarga del historial en formato CSV |
| `/devices` | Administrador de dispositivos inteligentes |
| `/ota` | Actualización de firmware |
| `/api/data` | JSON con datos en vivo del PZEM/ATS/sistema |
| `/api/devices` | JSON con el estado de todos los dispositivos |
| `/api/history` | JSON con el historial ATS |

---

## ⚠️ Notas importantes

- El contacto auxiliar del ATS debe ser un **contacto seco (sin voltaje propio)** — verifica esto con un multímetro antes de conectarlo al GPIO35.
- Después de actualizar por OTA, se recomienda un **ciclo de energía completo** del sistema (no solo un reinicio) para evitar que el PZEM quede en un estado de comunicación inconsistente.
- Los umbrales de calibración del sensor LDR (`LDR_BRIGHT_RAW` / `LDR_DARK_RAW` en `device_manager.h`) están calibrados para una unidad específica — es posible que necesiten ajuste según el sensor de cada placa.

---

## 🙏 Créditos

- [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) — configuración de referencia para la placa CYD2USB
- [mandulaj/PZEM-004T-v30](https://github.com/mandulaj/PZEM-004T-v30) — librería del medidor de energía
- [tzapu/WiFiManager](https://github.com/tzapu/WiFiManager)
- [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- [Bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)

---

## 📄 Licencia

Proyecto personal de uso libre. Sin licencia formal definida — úsalo, modifícalo y adáptalo como necesites para tu propio sistema.

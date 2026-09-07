#pragma once
/*
 * device_manager.h
 * Administrador de dispositivos Tasmota / OpenBeken.
 *
 * IMPORTANTE - Protocolo real (investigado, no asumido):
 * OpenBeken (OpenBK7231T/N) implementa un API HTTP COMPATIBLE con Tasmota.
 * Ambos usan exactamente los mismos comandos:
 *   - http://IP/cm?cmnd=Power%20ON | OFF | TOGGLE
 *   - http://IP/cm?cmnd=Status%208  -> JSON con StatusSNS.ENERGY (Power,Voltage,Current)
 * NO existe protocolo eWeLink/DIY aqui - esa suposicion anterior era incorrecta
 * y por eso los dispositivos OpenBeken no respondian. Ambos tipos de
 * dispositivo se controlan con la MISMA funcion HTTP.
 *
 * Monitoreo de consumo en tiempo real:
 * Solo los dispositivos con chip de medicion de energia (BL0937/BL0942/BL0939
 * en OpenBeken, o HLW8012/CSE7766 en Tasmota - ej. Sonoff S40TPB, POW R2)
 * pueden reportar Voltaje/Corriente/Potencia real. Modelos basicos (ej. S26R2)
 * SOLO controlan ON/OFF, no tienen ese sensor. Por eso cada dispositivo se
 * marca explicitamente si tiene medicion o no - no se inventa el dato.
 *
 * Standby de pantalla: eliminado por completo, no aportaba valor.
 */

#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// ============================================
// CONFIGURACION
// ============================================
#define MAX_DEVICES 20
#define DEVICE_HTTP_TIMEOUT 1000
#define METRICS_HTTP_TIMEOUT 1000

enum DeviceType { DEV_TASMOTA = 0, DEV_OPENBEKEN = 1 };

// Automatizacion segun la fuente activa del ATS (red electrica vs generador/inversor)
enum AtsAutoMode {
    ATS_AUTO_NONE = 0,               // sin automatizacion por ATS
    ATS_AUTO_OFF_ON_GENERATOR = 1,   // se apaga automaticamente cuando el ATS pasa a generador
    ATS_AUTO_ONLY_ON_UTILITY = 2,    // enciende en red electrica Y se apaga en generador
    ATS_AUTO_NIGHT_UTILITY = 3       // SOLO enciende de noche Y con red electrica (ambas a la vez)
};

struct SmartDevice {
    String id;
    String name;
    String ip;
    DeviceType type;
    String category;        // "cocina","luz","bomba","exterior","refrigerador","entretenimiento","otro"
    bool dimmable = false;
    int brightness = 100;
    bool state = false;             // ultimo estado conocido (asumido por nuestros propios comandos)
    bool scheduleEnabled = false;
    int onHour = 6, onMin = 0;
    int offHour = 22, offMin = 0;
    AtsAutoMode atsMode = ATS_AUTO_NONE;
    bool hasEnergyMonitoring = false;  // true solo si el hardware tiene chip de medicion real

    // Runtime - metricas en tiempo real (NO se persisten)
    float lastVoltage = 0;
    float lastCurrent = 0;
    float lastPower = 0;
    bool metricsValid = false;
    unsigned long lastMetricsUpdate = 0;

    // Runtime - control de disparo unico por minuto (NO se persiste)
    int lastFiredMinuteOn = -1;
    int lastFiredMinuteOff = -1;
    unsigned long lastAtsCorrectionAttempt = 0; // evita martillar un dispositivo inalcanzable
    int consecutiveFailures = 0; // backoff progresivo si el dispositivo no responde
    int pollFailures = 0; // fallos consecutivos de sondeo de estado, para marcar "sin conexion"
};

// ============================================
// VARIABLES GLOBALES DEL MODULO
// ============================================
Preferences devicePrefs;
SmartDevice devices[MAX_DEVICES];
int deviceCount = 0;
int utcOffsetHours = -5; // ajustable
bool ntpSynced = false;
int metricsPollIndex = 0; // round-robin para no bloquear el loop con varias llamadas HTTP seguidas

// Protege deviceCount/devices[] entre el nucleo principal (pantalla + servidor
// web, Core 1) y la tarea de red que corre en Core 0. Se toma SOLO para leer/
// escribir el arreglo (rapido); las llamadas HTTP en si nunca se hacen con el
// mutex tomado, para no bloquear al otro nucleo mientras se espera la red.
SemaphoreHandle_t devicesMutex = NULL;
void devicesMutexInit() {
    if (devicesMutex == NULL) devicesMutex = xSemaphoreCreateMutex();
}

// Registro de actividad reciente (encendidos/apagados). En RAM unicamente
// (no persiste a flash) - el historial que SI debe sobrevivir reinicios es
// el del ATS, que ya se guarda en SPIFFS por separado.
#define MAX_DEVICE_EVENTS 20
struct DeviceEvent {
    unsigned long timestamp;
    String deviceName;
    bool turnedOn;
    String source;
};
DeviceEvent deviceEvents[MAX_DEVICE_EVENTS];
int deviceEventIndex = 0;
int deviceEventCount = 0;

void logDeviceEvent(const String& name, bool on, const String& source) {
    time_t nowEpoch = time(nullptr);
    deviceEvents[deviceEventIndex].timestamp = (nowEpoch > 100000) ? (unsigned long)nowEpoch : 0;
    deviceEvents[deviceEventIndex].deviceName = name;
    deviceEvents[deviceEventIndex].turnedOn = on;
    deviceEvents[deviceEventIndex].source = source;
    deviceEventIndex = (deviceEventIndex + 1) % MAX_DEVICE_EVENTS;
    if (deviceEventCount < MAX_DEVICE_EVENTS) deviceEventCount++;
}

// Ventana de "modo nocturno" (usada por ATS_AUTO_NIGHT_UTILITY). Soporta
// cruzar medianoche (ej. 18:00 -> 06:00). Configurable, compartida por todos
// los dispositivos que usen ese modo.
int nightStartHour = 18, nightStartMin = 0;
int nightEndHour = 6, nightEndMin = 0;

// ============================================
// UTILIDADES
// ============================================
String deviceTypeToStr(DeviceType t) { return t == DEV_TASMOTA ? "tasmota" : "openbeken"; }
DeviceType deviceTypeFromStr(const String& s) { return s == "openbeken" ? DEV_OPENBEKEN : DEV_TASMOTA; }

String makeDeviceId() {
    static uint32_t counter = 0;
    counter++;
    return "dev_" + String((uint32_t)millis(), HEX) + String(counter, HEX);
}

int findDeviceIndexById(const String& id) {
    for (int i = 0; i < deviceCount; i++) {
        if (devices[i].id == id) return i;
    }
    return -1;
}

// ============================================
// PERSISTENCIA (NVS via Preferences, JSON)
// ============================================
void devicesSave() {
    DynamicJsonDocument doc(6144);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < deviceCount; i++) {
        JsonObject o = arr.createNestedObject();
        o["id"] = devices[i].id;
        o["name"] = devices[i].name;
        o["ip"] = devices[i].ip;
        o["type"] = deviceTypeToStr(devices[i].type);
        o["category"] = devices[i].category;
        o["dimmable"] = devices[i].dimmable;
        o["brightness"] = devices[i].brightness;
        o["state"] = devices[i].state;
        o["scheduleEnabled"] = devices[i].scheduleEnabled;
        o["onHour"] = devices[i].onHour;
        o["onMin"] = devices[i].onMin;
        o["offHour"] = devices[i].offHour;
        o["offMin"] = devices[i].offMin;
        o["atsMode"] = (int)devices[i].atsMode;
        o["hasEnergyMonitoring"] = devices[i].hasEnergyMonitoring;
    }
    String out;
    serializeJson(doc, out);
    devicePrefs.begin("devices", false);
    devicePrefs.putString("list", out);
    devicePrefs.end();
}

void devicesLoad() {
    devicePrefs.begin("devices", true);
    String raw = devicePrefs.getString("list", "[]");
    devicePrefs.end();

    DynamicJsonDocument doc(6144);
    DeserializationError err = deserializeJson(doc, raw);
    deviceCount = 0;
    if (err) {
        Serial.println("devicesLoad: JSON invalido o vacio, iniciando lista vacia");
        return;
    }
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject o : arr) {
        if (deviceCount >= MAX_DEVICES) break;
        SmartDevice d;
        d.id = o["id"].as<String>();
        d.name = o["name"].as<String>();
        d.ip = o["ip"].as<String>();
        d.type = deviceTypeFromStr(o["type"].as<String>());
        d.category = o["category"].as<String>();
        d.dimmable = o["dimmable"] | false;
        d.brightness = o["brightness"] | 100;
        d.state = o["state"] | false;
        d.scheduleEnabled = o["scheduleEnabled"] | false;
        d.onHour = o["onHour"] | 6;
        d.onMin = o["onMin"] | 0;
        d.offHour = o["offHour"] | 22;
        d.offMin = o["offMin"] | 0;
        d.atsMode = (AtsAutoMode)(int)(o["atsMode"] | 0);
        d.hasEnergyMonitoring = o["hasEnergyMonitoring"] | false;
        devices[deviceCount++] = d;
    }
    Serial.printf("devicesLoad: %d dispositivos cargados\n", deviceCount);
}

void utcOffsetSave() {
    devicePrefs.begin("devcfg", false);
    devicePrefs.putInt("utc", utcOffsetHours);
    devicePrefs.putInt("nsh", nightStartHour);
    devicePrefs.putInt("nsm", nightStartMin);
    devicePrefs.putInt("neh", nightEndHour);
    devicePrefs.putInt("nem", nightEndMin);
    devicePrefs.end();
}

void utcOffsetLoad() {
    devicePrefs.begin("devcfg", true);
    utcOffsetHours = devicePrefs.getInt("utc", -5);
    nightStartHour = devicePrefs.getInt("nsh", 18);
    nightStartMin = devicePrefs.getInt("nsm", 0);
    nightEndHour = devicePrefs.getInt("neh", 6);
    nightEndMin = devicePrefs.getInt("nem", 0);
    devicePrefs.end();
}

// ============================================
// CONTROL HTTP - Tasmota Y OpenBeken (protocolo identico confirmado)
// ============================================
bool httpCmndPower(const String& ip, bool on) {
    HTTPClient http;
    String url = "http://" + ip + "/cm?cmnd=Power%20" + (on ? "ON" : "OFF");
    http.begin(url);
    http.setTimeout(DEVICE_HTTP_TIMEOUT);
    int code = http.GET();
    http.end();
    return code == 200;
}

bool httpCmndDimmer(const String& ip, int value) {
    value = constrain(value, 0, 100);
    HTTPClient http;
    String url = "http://" + ip + "/cm?cmnd=Dimmer%20" + String(value);
    http.begin(url);
    http.setTimeout(DEVICE_HTTP_TIMEOUT);
    int code = http.GET();
    http.end();
    return code == 200;
}

// Status 8 = StatusSNS -> incluye bloque ENERGY con Power/Voltage/Current
// (verificado identico en Tasmota y OpenBeken)
// Version "raw" que no toca el struct SmartDevice directamente - se usa desde
// la tarea de red para poder hacer la llamada HTTP SIN tener el mutex tomado,
// y despues escribir el resultado de vuelta por separado (buscando por id,
// no por indice, por si el arreglo cambio mientras se esperaba la red).
bool httpCmndFetchEnergyRaw(const String& ip, float& outV, float& outC, float& outP) {
    HTTPClient http;
    String url = "http://" + ip + "/cm?cmnd=Status%208";
    http.begin(url);
    http.setTimeout(METRICS_HTTP_TIMEOUT);
    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, body)) return false;
    JsonObject energy = doc["StatusSNS"]["ENERGY"];
    if (energy.isNull()) return false;
    outV = energy["Voltage"] | 0.0;
    outC = energy["Current"] | 0.0;
    outP = energy["Power"] | 0.0;
    return true;
}

// Consulta el ESTADO REAL del rele (no lo que nosotros asumimos que quedo).
// cmnd=Power sin argumento devuelve {"POWER":"ON"} o {"POWER":"OFF"} tanto
// en Tasmota como en OpenBeken (mismo protocolo).
bool httpCmndGetPowerState(const String& ip, bool& stateOut) {
    HTTPClient http;
    String url = "http://" + ip + "/cm?cmnd=Power";
    http.begin(url);
    http.setTimeout(1000);
    int code = http.GET();
    if (code != 200) { http.end(); return false; }
    String body = http.getString();
    http.end();
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, body)) return false;
    String p = doc["POWER"].as<String>();
    if (p.length() == 0) return false;
    stateOut = (p == "ON" || p == "on" || p == "1");
    return true;
}

// ============================================
// DISPATCHER
// ============================================
bool deviceSetPower(SmartDevice& d, bool on, const char* source = "Manual") {
    bool ok = httpCmndPower(d.ip, on);
    if (ok) {
        if (d.state != on) logDeviceEvent(d.name, on, source);
        d.state = on;
    }
    return ok;
}

bool deviceSetBrightness(SmartDevice& d, int value) {
    if (!d.dimmable) return false;
    bool ok = httpCmndDimmer(d.ip, value);
    if (ok) {
        d.brightness = constrain(value, 0, 100);
        if (value > 0) d.state = true;
    }
    return ok;
}

// Sondeo round-robin: en cada llamada consulta UN solo dispositivo (avanza
// por todos en turnos) para no bloquear el loop con varias peticiones HTTP
// seguidas. Siempre refresca el estado real ON/OFF; si ademas tiene medicion
// de energia habilitada, tambien consulta voltaje/corriente/potencia.
void devicePollTick() {
    if (devicesMutex == NULL) return;
    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    if (deviceCount == 0) { xSemaphoreGive(devicesMutex); return; }
    metricsPollIndex = (metricsPollIndex + 1) % deviceCount;
    String targetId = devices[metricsPollIndex].id;
    String targetIp = devices[metricsPollIndex].ip;
    bool wantsEnergy = devices[metricsPollIndex].hasEnergyMonitoring;
    xSemaphoreGive(devicesMutex);

    // Llamadas de red SIN el mutex tomado - el otro nucleo (pantalla/web) sigue libre
    bool realState;
    bool gotState = httpCmndGetPowerState(targetIp, realState);

    float v = 0, c = 0, p = 0;
    bool gotEnergy = wantsEnergy ? httpCmndFetchEnergyRaw(targetIp, v, c, p) : false;

    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    int idx = findDeviceIndexById(targetId); // fresco, por si el arreglo cambio mientras se esperaba la red
    if (idx >= 0) {
        if (gotState) {
            devices[idx].state = realState;
            devices[idx].pollFailures = 0;
        } else if (devices[idx].pollFailures < 100) {
            devices[idx].pollFailures++;
        }
        if (wantsEnergy) {
            if (gotEnergy) {
                devices[idx].lastVoltage = v;
                devices[idx].lastCurrent = c;
                devices[idx].lastPower = p;
                devices[idx].metricsValid = true;
                devices[idx].lastMetricsUpdate = millis();
            } else {
                devices[idx].metricsValid = false;
            }
        }
    }
    xSemaphoreGive(devicesMutex);
}

// ============================================
// BRILLO DE PANTALLA (backlight PWM) + AUTO-BRILLO POR LDR
// GPIO21 confirmado oficialmente para la variante CYD2USB en:
// https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display
// (DisplayConfig/CYD2USB/User_Setup.h -> #define TFT_BL 21, activo en HIGH)
// ============================================
#define BACKLIGHT_PIN 21
#define BACKLIGHT_PWM_FREQ 5000
#define BACKLIGHT_PWM_RES 8
#define BACKLIGHT_MIN 25    // nunca completamente apagado, siempre algo legible
#define BACKLIGHT_MAX 255
// Calibrado con lecturas reales del sensor: con luz = 0, oscuridad total = 1888
// (con picos de ruido hasta ~1900). Notese que BRIGHT < DARK a proposito -
// map() interpola bien aunque el rango este invertido asi.
#define LDR_BRIGHT_RAW 0
#define LDR_DARK_RAW 1900
#define LDR_SAMPLES 8 // promedio movil para filtrar los picos de ruido del sensor

bool backlightPwmReady = false;

void backlightBegin() {
    bool ok = ledcAttach(BACKLIGHT_PIN, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_RES);
    backlightPwmReady = ok;
    if (ok) {
        ledcWrite(BACKLIGHT_PIN, BACKLIGHT_MAX);
        Serial.println("backlightBegin: PWM listo en GPIO21");
    } else {
        Serial.println("backlightBegin: fallo al inicializar PWM en GPIO21");
    }
}

void backlightSet(uint8_t value) {
    if (!backlightPwmReady) return;
    ledcWrite(BACKLIGHT_PIN, value);
}

// Se llama periodicamente (no en cada loop, el ADC+PWM no necesitan tanta
// frecuencia). Promedia varias lecturas para filtrar picos de ruido del LDR,
// y ademas suaviza el cambio de brillo para que no "salte" de golpe.
static int s_lastBrightness = -1;
static int s_ldrSamples[LDR_SAMPLES] = {0};
static int s_ldrSampleIdx = 0;
static bool s_ldrBufferFull = false;

void updateAutoBrightness() {
    if (!backlightPwmReady) return;

    s_ldrSamples[s_ldrSampleIdx] = analogRead(LDR_PIN);
    s_ldrSampleIdx = (s_ldrSampleIdx + 1) % LDR_SAMPLES;
    if (s_ldrSampleIdx == 0) s_ldrBufferFull = true;

    int count = s_ldrBufferFull ? LDR_SAMPLES : s_ldrSampleIdx;
    if (count == 0) return;
    long sum = 0;
    for (int i = 0; i < count; i++) sum += s_ldrSamples[i];
    int avgRaw = sum / count;

    int target = map(constrain(avgRaw, LDR_BRIGHT_RAW, LDR_DARK_RAW), LDR_DARK_RAW, LDR_BRIGHT_RAW, BACKLIGHT_MIN, BACKLIGHT_MAX);
    if (s_lastBrightness < 0) s_lastBrightness = target;
    s_lastBrightness += (target - s_lastBrightness) / 4;
    backlightSet((uint8_t)s_lastBrightness);
}


// El NTP por internet puede fallar justo quando mas importa: durante un
// corte de luz, si el router/modem tambien pierde internet. Por eso ademas
// de NTP, el navegador del telefono (que casi siempre tiene hora correcta
// via red celular, sin depender del WiFi local) puede fijar la hora del
// CYD directamente. Ver /api/settime.
void ntpTimeBegin() {
    configTime(utcOffsetHours * 3600, 0, "pool.ntp.org", "time.google.com", "mx.pool.ntp.org");
}

bool getLocalTimeInfo(struct tm& timeinfo) {
    time_t now = time(nullptr);
    if (now < 100000) return false;
    localtime_r(&now, &timeinfo);
    ntpSynced = true;
    return true;
}

// Fija la hora manualmente a partir de un epoch UTC en segundos (enviado
// por el navegador del telefono). configTime() ya establecio el offset de
// zona horaria antes, asi que localtime_r() seguira convirtiendo bien.
void applyManualTime(long epochSeconds) {
    struct timeval tv;
    tv.tv_sec = epochSeconds;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    ntpSynced = true;
    Serial.println("Hora fijada manualmente desde el navegador");
}



// Determina si la hora actual cae dentro de la ventana de "modo nocturno".
// Soporta ventanas que cruzan medianoche (ej. 18:00 -> 06:00).
// Si no hay hora real disponible, devuelve false (no arriesga encender
// algo de noche cuando en realidad no se sabe la hora).
bool isNightTimeNow() {
    struct tm ti;
    if (!getLocalTimeInfo(ti)) return false;
    int curKey = ti.tm_hour * 60 + ti.tm_min;
    int startKey = nightStartHour * 60 + nightStartMin;
    int endKey = nightEndHour * 60 + nightEndMin;
    if (startKey == endKey) return false;
    if (startKey < endKey) {
        return curKey >= startKey && curKey < endKey;
    } else {
        return curKey >= startKey || curKey < endKey; // cruza medianoche
    }
}

// ============================================
// SCHEDULER: horarios por dispositivo + automatizacion segun fuente ATS
// ============================================

// Se evalua en CADA ciclo (no solo cuando el ATS cambia), asi un dispositivo
// recien agregado/editado, o que alguien encendio manualmente por error,
// se autocorrige para calzar con la fuente activa - no solo en el instante
// exacto de la transicion.
void enforceAtsAutomation() {
    if (atsState == ATS_UNKNOWN) return;
    if (devicesMutex == NULL) return;

    String targetId, targetIp, targetName;
    bool desired = false;
    bool found = false;

    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    for (int i = 0; i < deviceCount; i++) {
        SmartDevice& d = devices[i];
        bool desiredLocal;
        bool hasOpinion = false;

        if (d.atsMode == ATS_AUTO_OFF_ON_GENERATOR) {
            if (atsState == ATS_GENERATOR_POWER) { desiredLocal = false; hasOpinion = true; }
        } else if (d.atsMode == ATS_AUTO_ONLY_ON_UTILITY) {
            desiredLocal = (atsState == ATS_UTILITY_POWER);
            hasOpinion = true;
        } else if (d.atsMode == ATS_AUTO_NIGHT_UTILITY) {
            if (isNightTimeNow()) {
                desiredLocal = (atsState == ATS_UTILITY_POWER);
                hasOpinion = true;
            }
        }

        if (hasOpinion && d.state != desiredLocal) {
            // Backoff progresivo: 1.5s, 3s, 6s, 12s, 24s, tope 48s.
            unsigned long cooldown = 1500UL << min(d.consecutiveFailures, 5);
            if (millis() - d.lastAtsCorrectionAttempt < cooldown) continue;
            d.lastAtsCorrectionAttempt = millis();
            targetId = d.id;
            targetIp = d.ip;
            targetName = d.name;
            desired = desiredLocal;
            found = true;
            break; // solo UN dispositivo por llamada
        }
    }
    xSemaphoreGive(devicesMutex);

    if (!found) return;

    Serial.printf("ATS-auto: corrigiendo %s a %s (fuente: %s)\n",
        targetName.c_str(), desired ? "ON" : "OFF", atsGetStateString(atsState).c_str());

    // Llamada HTTP SIN el mutex tomado
    bool ok = httpCmndPower(targetIp, desired);

    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    int idx = findDeviceIndexById(targetId);
    if (idx >= 0) {
        if (ok) {
            if (devices[idx].state != desired) logDeviceEvent(devices[idx].name, desired, "Automatización ATS");
            devices[idx].state = desired;
            devices[idx].consecutiveFailures = 0;
        } else {
            devices[idx].consecutiveFailures++;
            Serial.printf("ATS-auto: %s no respondio, backoff a %lums\n",
                targetName.c_str(), 1500UL << min(devices[idx].consecutiveFailures, 5));
        }
    }
    xSemaphoreGive(devicesMutex);
}

void schedulerCheck() {
    // --- Estado real ON/OFF + metricas (round robin, un dispositivo por ciclo) ---
    if (WiFi.status() == WL_CONNECTED) {
        devicePollTick();
    }

    // NOTA: enforceAtsAutomation() corre por separado en su propio ciclo
    // dentro de deviceManagerTaskFn() (Core 0), sin esperar a este chequeo
    // de 3 segundos - asi reacciona casi al instante al cambio de fuente ATS.

    // --- Horarios individuales por dispositivo (requiere hora real) ---
    if (WiFi.status() != WL_CONNECTED) return;
    struct tm ti;
    if (!getLocalTimeInfo(ti)) return;
    if (devicesMutex == NULL) return;

    int curKey = ti.tm_hour * 60 + ti.tm_min;

    // Se recolectan las acciones necesarias con el mutex tomado (rapido, sin
    // red), y se ejecutan las llamadas HTTP DESPUES de soltarlo.
    struct PendingAction { String id; String ip; String name; bool turnOn; };
    PendingAction pending[MAX_DEVICES];
    int pendingCount = 0;

    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    for (int i = 0; i < deviceCount; i++) {
        SmartDevice& d = devices[i];
        if (!d.scheduleEnabled) continue;

        int onKey = d.onHour * 60 + d.onMin;
        int offKey = d.offHour * 60 + d.offMin;

        if (curKey == onKey && d.lastFiredMinuteOn != curKey) {
            d.lastFiredMinuteOn = curKey;
            pending[pendingCount++] = { d.id, d.ip, d.name, true };
        }
        if (curKey == offKey && d.lastFiredMinuteOff != curKey) {
            d.lastFiredMinuteOff = curKey;
            pending[pendingCount++] = { d.id, d.ip, d.name, false };
        }
    }
    xSemaphoreGive(devicesMutex);

    for (int i = 0; i < pendingCount; i++) {
        Serial.printf("Horario: %s %s\n", pending[i].turnOn ? "encendiendo" : "apagando", pending[i].name.c_str());
        bool ok = httpCmndPower(pending[i].ip, pending[i].turnOn);

        xSemaphoreTake(devicesMutex, portMAX_DELAY);
        int idx = findDeviceIndexById(pending[i].id);
        if (idx >= 0 && ok) {
            if (devices[idx].state != pending[i].turnOn) logDeviceEvent(devices[idx].name, pending[i].turnOn, "Horario");
            devices[idx].state = pending[i].turnOn;
        }
        xSemaphoreGive(devicesMutex);
    }
}

// ============================================
// TAREA EN SEGUNDO PLANO (Core 0)
// Todo lo que implica esperar por red (sondeo de estado/metricas, horarios,
// automatizacion ATS) corre aqui, en un nucleo separado del que maneja la
// pantalla TFT y el servidor web (Core 1). Asi una llamada HTTP lenta o un
// dispositivo desconectado nunca congela la interfaz.
// ============================================
void deviceManagerTaskFn(void* pvParameters) {
    unsigned long lastAtsCheck = 0;
    unsigned long lastPollCheck = 0;

    for (;;) {
        unsigned long now = millis();

        if (now - lastAtsCheck >= 150) {
            lastAtsCheck = now;
            enforceAtsAutomation();
        }

        if (now - lastPollCheck >= SCHEDULER_CHECK_INTERVAL) {
            lastPollCheck = now;
            schedulerCheck();
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // cede CPU, evita acaparar el nucleo y dispara el watchdog
    }
}

void deviceManagerTaskBegin() {
    xTaskCreatePinnedToCore(
        deviceManagerTaskFn,
        "DeviceMgr",
        8192,
        NULL,
        1,
        NULL,
        0 // Core 0 - separado del loop() principal (Core 1)
    );
}

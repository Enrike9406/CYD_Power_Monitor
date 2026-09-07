/*
 * CYD-PZEM-ATS-Monitor v2.1
 * 
 * Proyecto para CYD2USB (ESP32-2432S028) que monitorea un PZEM-004T 
 * y el estado de un ATS (Automatic Transfer Switch).
 * 
 * v2.1 - Professional industrial dashboard redesign
 *   - Modern industrial web dashboard with real-time charts
 *   - Detailed ATS history with statistics and visual timeline
 *   - Professional dark theme with data visualization
 * 
 * v2.0 - Professional industrial dashboard with ATS history
 *   - Industrial-style web dashboard
 *   - ATS history page with time filters
 *   - Circular buffer for history storage
 * 
 * v1.0 - Initial release with:
 *   - PZEM-004T monitoring via Serial2
 *   - ATS status monitoring via GPIO35
 *   - Web dashboard with auto-refresh
 *   - OTA firmware update
 *   - WiFiManager with portal
 *   - Boot button WiFi reset (5s hold)
 *   - CYD2USB gamma fix
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <PZEM004Tv30.h>
#include <SPIFFS.h>

// ============================================
// CONFIGURACION
// ============================================
#define WIFI_MANAGER_TIMEOUT 180
#define HOSTNAME_PREFIX "CYD-Monitor"
#define WEB_SERVER_PORT 80
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#define DISPLAY_ORIENTATION 1
#define DISPLAY_UPDATE_INTERVAL 1500
#define PZEM_RX_PIN 27
#define PZEM_TX_PIN 22
#define PZEM_BAUD_RATE 9600
#define PZEM_READ_INTERVAL 2000
#define ATS_STATUS_PIN 35
#define ATS_DEBOUNCE_DELAY 500
#define ATS_STABLE_COUNT 10
#define LED_RED_PIN 4
#define LED_GREEN_PIN 16
#define LED_BLUE_PIN 17
#define LDR_PIN 34
#define BOOT_BUTTON_PIN 0
#define DEBUG_BAUD_RATE 115200
#define WEB_REFRESH_INTERVAL 5
#define BOOT_RESET_HOLD_TIME 5000
#define FIRMWARE_VERSION "2.1"
#define MAX_HISTORY_ENTRIES 300
#define HISTORY_SAVE_INTERVAL 60000
#define SCHEDULER_CHECK_INTERVAL 3000

// ============================================
// ENUMS Y ESTRUCTURAS
// ============================================
enum ATSState { ATS_UTILITY_POWER, ATS_GENERATOR_POWER, ATS_UNKNOWN };

struct PZEMData {
    float voltage = 0.0;
    float current = 0.0;
    float power = 0.0;
    float energy = 0.0;
    float frequency = 0.0;
    float pf = 0.0;
    bool isValid = false;
    unsigned long lastRead = 0;
};

struct ATSHistoryEntry {
    unsigned long timestamp;  // Unix timestamp
    ATSState state;
    unsigned long duration;   // Duration in seconds
};

// ============================================
// VARIABLES GLOBALES
// ============================================
TFT_eSPI tft;
PZEM004Tv30 pzem(&Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
WebServer server(WEB_SERVER_PORT);
WiFiManager wm;

PZEMData pzemData;
ATSState atsState = ATS_UNKNOWN;
ATSState atsLastState = ATS_UNKNOWN;
unsigned long atsLastStateChange = 0;
unsigned long atsLastDebounce = 0;
int atsStableCounter = 0;

String wifiStatusStr = "WiFi: Connecting...";
String pzemStatusStr = "PZEM: Initializing...";

unsigned long pzemLastRead = 0;
unsigned long displayLastUpdate = 0;
unsigned long wifiReconnectCheck = 0;
unsigned long bootButtonPressStart = 0;
bool bootButtonPressed = false;
bool bootResetActive = false;

bool pzemInitialized = false;
bool displayInitialized = false;

// ATS History storage (circular buffer)
ATSHistoryEntry atsHistory[MAX_HISTORY_ENTRIES];
int historyIndex = 0;
int historyCount = 0;
unsigned long historyLastSave = 0;
unsigned long schedulerLastCheck = 0;
unsigned long atsAutoLastCheck = 0;
unsigned long brightnessLastCheck = 0;

const uint16_t COLOR_BG = 0x0000;
const uint16_t COLOR_WHITE = 0xFFFF;
const uint16_t COLOR_YELLOW = 0xFFE0;
const uint16_t COLOR_GREEN = 0x07E0;
const uint16_t COLOR_RED = 0xF800;
const uint16_t COLOR_BLUE = 0x001F;
const uint16_t COLOR_CYAN = 0x07FF;
const uint16_t COLOR_ORANGE = 0xFD20;
const uint16_t COLOR_DARK_GRAY = 0x7BEF;

const int HEADER_HEIGHT = 25;
const int FOOTER_HEIGHT = 20;
const int COL1_X = 5;
const int COL2_X = 165;
const int ROW_Y_START = 30;
const int ROW_HEIGHT = 22;

// ============================================
// BOOT BUTTON
// ============================================
void checkBootButton() {
    int buttonState = digitalRead(BOOT_BUTTON_PIN);
    
    if (buttonState == LOW && !bootButtonPressed) {
        bootButtonPressed = true;
        bootButtonPressStart = millis();
        bootResetActive = true;
        Serial.println("Boot button pressed - hold 5s to reset WiFi");
    }
    
    if (buttonState == HIGH) {
        bootButtonPressed = false;
        bootResetActive = false;
    }
    
    if (bootResetActive && buttonState == LOW) {
        unsigned long holdTime = millis() - bootButtonPressStart;
        if (holdTime >= BOOT_RESET_HOLD_TIME) {
            Serial.println("WiFi reset triggered!");
            tft.fillScreen(COLOR_BG);
            tft.setTextColor(COLOR_RED);
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("WiFi Reset!");
            tft.setTextColor(COLOR_WHITE);
            tft.setTextSize(1);
            tft.setCursor(20, 90);
            tft.print("Erasing credentials...");
            
            wm.resetSettings();
            WiFi.disconnect(true, true);
            delay(500);
            WiFi.mode(WIFI_OFF);
            delay(500);
            ESP.restart();
        }
        
        if (holdTime > 1000) {
            int progress = (holdTime * 100) / BOOT_RESET_HOLD_TIME;
            tft.fillRect(20, 120, 280, 20, COLOR_DARK_GRAY);
            tft.fillRect(20, 120, (280 * progress) / 100, 20, COLOR_RED);
            tft.setTextColor(COLOR_WHITE);
            tft.setCursor(20, 123);
            tft.printf("Reset: %d%%", progress);
        }
    }
}

// ============================================
// LED
// ============================================
void setLED(bool red, bool green, bool blue) {
    digitalWrite(LED_RED_PIN, red ? LOW : HIGH);
    digitalWrite(LED_GREEN_PIN, green ? LOW : HIGH);
    digitalWrite(LED_BLUE_PIN, blue ? LOW : HIGH);
}

// ============================================
// ATS
// ============================================
void atsBegin() {
    pinMode(ATS_STATUS_PIN, INPUT);
    int reading = digitalRead(ATS_STATUS_PIN);
    atsState = (reading == HIGH) ? ATS_UTILITY_POWER : ATS_GENERATOR_POWER;
    atsLastState = atsState;
    atsLastStateChange = millis();
    Serial.print("ATS initialized. State: ");
    Serial.println(atsGetStateString(atsState));
}

void atsUpdate() {
    unsigned long now = millis();
    if (now - atsLastDebounce < ATS_DEBOUNCE_DELAY) return;

    int readings[3];
    for (int i = 0; i < 3; i++) {
        readings[i] = digitalRead(ATS_STATUS_PIN);
    }
    int highCount = 0;
    for (int i = 0; i < 3; i++) if (readings[i] == HIGH) highCount++;
    ATSState newState = (highCount >= 2) ? ATS_UTILITY_POWER : ATS_GENERATOR_POWER;
    
    if (newState != atsState) {
        atsState = newState;
        atsLastDebounce = now;
        atsStableCounter = 0;
    } else {
        atsStableCounter++;
        if (atsStableCounter >= ATS_STABLE_COUNT && atsState != atsLastState) {
            unsigned long duration = (now - atsLastStateChange) / 1000;
            atsAddHistoryEntry(atsState, duration);
            atsLastState = atsState;
            atsLastStateChange = now;
            Serial.print("ATS changed to: ");
            Serial.println(atsGetStateString(atsState));
        }
    }
}

String atsGetStateString(ATSState state) {
    switch (state) {
        case ATS_UTILITY_POWER: return "SEN";
        case ATS_GENERATOR_POWER: return "INVERSOR";
        default: return "DESCONOCIDO";
    }
}

unsigned long atsGetTimeInState() {
    return (millis() - atsLastStateChange) / 1000;
}

// ============================================
// ATS HISTORY
// ============================================
#define HISTORY_FILE "/ats_history.bin"

// Guarda TODO el buffer de historial de una sola vez. Se llama solo cuando
// hay un cambio de estado real (pocas veces al dia), asi que reescribir el
// archivo completo cada vez es simple y no desgasta la flash.
void historySaveToFlash() {
    File f = SPIFFS.open(HISTORY_FILE, FILE_WRITE);
    if (!f) {
        Serial.println("historySaveToFlash: no se pudo abrir el archivo");
        return;
    }
    f.write((uint8_t*)&historyCount, sizeof(historyCount));
    f.write((uint8_t*)&historyIndex, sizeof(historyIndex));
    f.write((uint8_t*)atsHistory, sizeof(ATSHistoryEntry) * MAX_HISTORY_ENTRIES);
    f.close();
}

void historyLoadFromFlash() {
    if (!SPIFFS.exists(HISTORY_FILE)) {
        Serial.println("historyLoadFromFlash: sin historial previo guardado");
        return;
    }
    File f = SPIFFS.open(HISTORY_FILE, FILE_READ);
    if (!f) return;
    size_t expected = sizeof(historyCount) + sizeof(historyIndex) + sizeof(ATSHistoryEntry) * MAX_HISTORY_ENTRIES;
    if (f.size() != expected) {
        // Archivo de una version anterior con otro tamano de MAX_HISTORY_ENTRIES
        Serial.println("historyLoadFromFlash: archivo de tamano distinto, se ignora");
        f.close();
        return;
    }
    f.read((uint8_t*)&historyCount, sizeof(historyCount));
    f.read((uint8_t*)&historyIndex, sizeof(historyIndex));
    f.read((uint8_t*)atsHistory, sizeof(ATSHistoryEntry) * MAX_HISTORY_ENTRIES);
    f.close();
    Serial.printf("historyLoadFromFlash: %d entradas restauradas\n", historyCount);
}

void atsAddHistoryEntry(ATSState newState, unsigned long duration) {
    // Hora real (epoch UTC) si ya esta sincronizada (NTP o telefono); si no,
    // se guarda 0 y se muestra como "hora no disponible" en vez de inventar
    // una fecha incorrecta con millis().
    time_t nowEpoch = time(nullptr);
    atsHistory[historyIndex].timestamp = (nowEpoch > 100000) ? (unsigned long)nowEpoch : 0;
    atsHistory[historyIndex].state = newState;
    atsHistory[historyIndex].duration = duration;
    historyIndex = (historyIndex + 1) % MAX_HISTORY_ENTRIES;
    if (historyCount < MAX_HISTORY_ENTRIES) historyCount++;
    historySaveToFlash();
}

// Formatea un epoch UTC real como fecha/hora legible. Distinto de
// formatDuration/formatDurationLong, que formatean intervalos de tiempo.
String formatRealTimestamp(unsigned long epoch) {
    if (epoch < 100000) return "Hora no disponible";
    time_t t = (time_t)epoch;
    struct tm ti;
    localtime_r(&t, &ti);
    char buf[24];
    strftime(buf, sizeof(buf), "%d/%m %H:%M", &ti);
    return String(buf);
}

// Grafica de tendencia diaria (Red vs Generador) como SVG generado en el
// servidor - sin librerias externas, coherente con el resto del proyecto.
// Solo cuenta entradas con hora real valida (timestamp >= 100000); las
// entradas antiguas guardadas antes de tener hora sincronizada se omiten.
String buildDailyTrendChart() {
    struct DayBucket { int year, mon, mday; unsigned long utilitySec; unsigned long generatorSec; };
    const int MAX_DAYS = 14;
    DayBucket days[MAX_DAYS];
    int dayCount = 0;

    for (int i = 0; i < historyCount; i++) {
        if (atsHistory[i].timestamp < 100000) continue;
        time_t t = (time_t)atsHistory[i].timestamp;
        struct tm ti;
        localtime_r(&t, &ti);

        int found = -1;
        for (int d = 0; d < dayCount; d++) {
            if (days[d].year == ti.tm_year && days[d].mon == ti.tm_mon && days[d].mday == ti.tm_mday) { found = d; break; }
        }
        if (found < 0) {
            if (dayCount < MAX_DAYS) {
                found = dayCount++;
                days[found] = { ti.tm_year, ti.tm_mon, ti.tm_mday, 0, 0 };
            } else {
                continue;
            }
        }
        if (atsHistory[i].state == ATS_UTILITY_POWER) days[found].utilitySec += atsHistory[i].duration;
        else days[found].generatorSec += atsHistory[i].duration;
    }

    if (dayCount == 0) {
        return "<div style='text-align:center;padding:30px;color:var(--muted)'>Sin datos suficientes con hora real para mostrar tendencia diaria (se sincroniza al abrir /devices desde el telefono)</div>";
    }

    // Orden cronologico ascendente (insertion sort, dayCount <= 14)
    for (int i = 1; i < dayCount; i++) {
        DayBucket key = days[i];
        int j = i - 1;
        while (j >= 0 && (days[j].year > key.year ||
              (days[j].year == key.year && days[j].mon > key.mon) ||
              (days[j].year == key.year && days[j].mon == key.mon && days[j].mday > key.mday))) {
            days[j + 1] = days[j];
            j--;
        }
        days[j + 1] = key;
    }

    int w = 700, h = 220, padLeft = 15, padRight = 15, padBottom = 26, padTop = 10;
    int chartW = w - padLeft - padRight;
    int chartH = h - padTop - padBottom;
    int barGroupW = chartW / dayCount;
    int barW = min(26, barGroupW / 2 - 3);
    if (barW < 4) barW = 4;

    unsigned long maxSec = 3600;
    for (int i = 0; i < dayCount; i++) {
        if (days[i].utilitySec > maxSec) maxSec = days[i].utilitySec;
        if (days[i].generatorSec > maxSec) maxSec = days[i].generatorSec;
    }

    String svg = "<svg viewBox='0 0 " + String(w) + " " + String(h) + "' style='width:100%;height:auto'>";
    svg += "<line x1='" + String(padLeft) + "' y1='" + String(h - padBottom) + "' x2='" + String(w - padRight) + "' y2='" + String(h - padBottom) + "' stroke='#30363d'/>";

    for (int i = 0; i < dayCount; i++) {
        int gx = padLeft + i * barGroupW + barGroupW / 2;
        int uH = (int)((float)days[i].utilitySec / maxSec * chartH);
        int gH = (int)((float)days[i].generatorSec / maxSec * chartH);
        int uX = gx - barW - 2;
        int gX = gx + 2;
        int uY = h - padBottom - uH;
        int gY = h - padBottom - gH;
        svg += "<rect x='" + String(uX) + "' y='" + String(uY) + "' width='" + String(barW) + "' height='" + String(uH) + "' fill='#10b981' rx='2'/>";
        svg += "<rect x='" + String(gX) + "' y='" + String(gY) + "' width='" + String(barW) + "' height='" + String(gH) + "' fill='#f59e0b' rx='2'/>";

        char buf[8];
        snprintf(buf, sizeof(buf), "%02d/%02d", days[i].mday, days[i].mon + 1);
        svg += "<text x='" + String(gx) + "' y='" + String(h - padBottom + 16) + "' font-size='10' fill='#9ca3af' text-anchor='middle'>" + String(buf) + "</text>";
    }
    svg += "</svg>";
    return svg;
}

String atsGetStateName(ATSState state) {
    switch (state) {
        case ATS_UTILITY_POWER: return "UTILITY";
        case ATS_GENERATOR_POWER: return "GENERATOR";
        default: return "UNKNOWN";
    }
}

String atsGetStateColor(ATSState state) {
    switch (state) {
        case ATS_UTILITY_POWER: return "#00ff00";
        case ATS_GENERATOR_POWER: return "#ff8c00";
        default: return "#ff0000";
    }
}

String formatDuration(unsigned long seconds) {
    if (seconds < 60) return String(seconds) + "s";
    if (seconds < 3600) return String(seconds / 60) + "m " + String(seconds % 60) + "s";
    if (seconds < 86400) return String(seconds / 3600) + "h " + String((seconds % 3600) / 60) + "m";
    return String(seconds / 86400) + "d " + String((seconds % 86400) / 3600) + "h";
}

String formatDurationLong(unsigned long seconds) {
    unsigned long days = seconds / 86400;
    unsigned long hours = (seconds % 86400) / 3600;
    unsigned long mins = (seconds % 3600) / 60;
    unsigned long secs = seconds % 60;
    
    String result = "";
    if (days > 0) { result += String(days) + "d "; }
    if (hours > 0 || days > 0) { result += String(hours) + "h "; }
    if (mins > 0 || hours > 0 || days > 0) { result += String(mins) + "m "; }
    result += String(secs) + "s";
    return result;
}

String formatTimestamp(unsigned long epoch) {
    unsigned long days = epoch / 86400;
    unsigned long hours = (epoch % 86400) / 3600;
    unsigned long mins = (epoch % 3600) / 60;
    unsigned long secs = epoch % 60;
    
    char buf[20];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", hours, mins, secs);
    return String(buf);
}

unsigned long atsGetTotalTimeInState(ATSState targetState) {
    unsigned long total = 0;
    for (int i = 0; i < historyCount; i++) {
        if (atsHistory[i].state == targetState) {
            total += atsHistory[i].duration;
        }
    }
    total += (atsState == targetState) ? atsGetTimeInState() : 0;
    return total;
}

float atsGetPercentageInState(ATSState targetState) {
    unsigned long totalTime = 0;
    for (int i = 0; i < historyCount; i++) {
        totalTime += atsHistory[i].duration;
    }
    totalTime += atsGetTimeInState();
    if (totalTime == 0) return 0;
    return (float)atsGetTotalTimeInState(targetState) * 100.0 / totalTime;
}

int atsGetStateChangesCount(ATSState targetState) {
    int count = 0;
    for (int i = 0; i < historyCount; i++) {
        if (atsHistory[i].state == targetState) count++;
    }
    return count;
}

unsigned long atsGetAverageDuration(ATSState targetState) {
    int count = atsGetStateChangesCount(targetState);
    if (count == 0) return 0;
    return atsGetTotalTimeInState(targetState) / count;
}

unsigned long atsGetMaxDuration(ATSState targetState) {
    unsigned long maxDur = 0;
    for (int i = 0; i < historyCount; i++) {
        if (atsHistory[i].state == targetState && atsHistory[i].duration > maxDur) {
            maxDur = atsHistory[i].duration;
        }
    }
    unsigned long current = (atsState == targetState) ? atsGetTimeInState() : 0;
    if (current > maxDur) maxDur = current;
    return maxDur;
}

unsigned long atsGetMinDuration(ATSState targetState) {
    unsigned long minDur = 0xFFFFFFFF;
    bool found = false;
    for (int i = 0; i < historyCount; i++) {
        if (atsHistory[i].state == targetState) {
            found = true;
            if (atsHistory[i].duration < minDur) minDur = atsHistory[i].duration;
        }
    }
    unsigned long current = (atsState == targetState) ? atsGetTimeInState() : 0;
    if (current > 0 && current < minDur) minDur = current;
    if (!found && current == 0) return 0;
    return minDur;
}

// ============================================
// PZEM
// ============================================
bool pzemBegin() {
    Serial2.begin(PZEM_BAUD_RATE, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);
    delay(100);
    float voltage = pzem.voltage();
    if (!isnan(voltage)) {
        pzemInitialized = true;
        Serial.println("PZEM-004T initialized");
        return true;
    }
    Serial.println("PZEM-004T init failed");
    return false;
}

// Lectura "rapida" para el JSON API: devuelve los ultimos valores cacheados en
// pzemData (siempre que sean validos). El PZEM real corre en su propia tarea
// (ver pzemTaskBegin / pzemTaskEntry abajo) para no bloquear ni la pantalla ni
// el servidor web. Esta funcion existe para mantener el resto del codigo que
// la llamaba sin cambios.
bool pzemRead() {
    if (!pzemInitialized) return false;
    if (!pzemData.isValid) return false;
    return true;
}

// Lectura "fresca" usada SOLO desde la tarea dedicada al PZEM. Hace las 6
// transacciones Modbus una tras otra. Cada una tarda ~300ms en baudrate 9600
// (timeout del PZEM004Tv30); en total bloquea 1.5-2s. Por eso vive en su
// propia tarea: mientras tanto, el nucleo principal (pantalla + web) corre
// sin ser interrumpido. Devuelve true si todas las lecturas principales son
// validas (no NaN). Tambien cachea el timestamp en pzemData.lastRead.
static bool pzemReadBlocking() {
    float voltage = pzem.voltage();
    float current = pzem.current();
    float power   = pzem.power();
    float energy  = pzem.energy();
    float frequency = pzem.frequency();
    float pf      = pzem.pf();

    if (!isnan(voltage) && !isnan(current) && !isnan(power)) {
        pzemData.voltage   = voltage;
        pzemData.current   = current;
        pzemData.power     = power;
        pzemData.energy    = energy;
        pzemData.frequency = frequency;
        pzemData.pf        = pf;
        pzemData.isValid   = true;
        pzemData.lastRead  = millis();
        return true;
    }
    pzemData.isValid = false;
    return false;
}

// Tarea FreeRTOS dedicada al PZEM, corre en Core 0 para no molestar al
// nucleo principal (Core 1, donde estan TFT y WebServer). Duerme en bloque
// usando vTaskDelay entre lecturas para no quemar CPU.
static void pzemTaskEntry(void* arg) {
    unsigned long lastRead = 0;
    for (;;) {
        unsigned long now = millis();
        if (pzemInitialized && (now - lastRead) >= PZEM_READ_INTERVAL) {
            lastRead = now;
            bool ok = pzemReadBlocking();
            // Guardamos el estado para que el JSON API y la UI lo lean
            // sin tener que tocar Serial2 ellos mismos.
            if (ok) {
                pzemStatusStr = "PZEM: OK";
            } else {
                pzemStatusStr = "PZEM: ERR";
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static TaskHandle_t pzemTaskHandle = NULL;
void pzemTaskBegin() {
    if (pzemTaskHandle != NULL) return;
    xTaskCreatePinnedToCore(
        pzemTaskEntry,
        "pzem",
        4096,           // stack suficiente para Modbus + lib PZEM
        NULL,
        1,              // prioridad baja: no debe competir con el nucleo principal
        &pzemTaskHandle,
        0               // Core 0
    );
}

String pzemGetStatusString() {
    if (!pzemInitialized) return "PZEM: Not Connected";
    if (!pzemData.isValid) return "PZEM: No Data";
    return "PZEM: OK";
}

// ============================================
// DISPLAY
// ============================================
bool displayBegin() {
    tft.init();
    tft.setRotation(DISPLAY_ORIENTATION);
    
    // Fix gamma issue for CYD2USB (ILI9341_GAMMASET = 0x26)
    tft.writecommand(0x26);
    tft.writedata(2);
    delay(120);
    tft.writecommand(0x26);
    tft.writedata(1);
    
    tft.fillScreen(COLOR_BG);
    tft.setTextFont(2);
    displayInitialized = true;
    Serial.println("Display initialized (CYD2USB gamma fix applied)");
    return true;
}

void displayDrawHeader() {
    tft.fillRect(0, 0, DISPLAY_WIDTH, HEADER_HEIGHT, COLOR_BLUE);
    tft.fillRect(0, HEADER_HEIGHT - 3, DISPLAY_WIDTH, 3, COLOR_CYAN);
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 7);
    tft.print("CYD Power Monitor v");
    tft.print(FIRMWARE_VERSION);
    tft.drawFastHLine(0, HEADER_HEIGHT, DISPLAY_WIDTH, COLOR_CYAN);
}

void displayDrawFooter(const String& wifiStatus, const String& pzemStatus) {
    int footerY = DISPLAY_HEIGHT - FOOTER_HEIGHT;
    tft.fillRect(0, footerY, DISPLAY_WIDTH, FOOTER_HEIGHT, 0x18C3);
    tft.fillRect(0, footerY, DISPLAY_WIDTH, 1, COLOR_CYAN);
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, footerY + 5);
    tft.print(wifiStatus);
    tft.setCursor(165, footerY + 5);
    tft.print(pzemStatus);
}

// Dibuja un campo: borra solo el área del valor y lo reescribe (sin parpadeo)
void displayPrintValue(int x, int y, int clearW, const String& val, uint16_t color) {
    tft.fillRect(x, y, clearW, 14, COLOR_BG);
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(val);
}

void displayDrawPZEMData() {
    int y = ROW_Y_START;
    tft.setTextSize(1);

    // Título — solo se dibuja en el primer frame (no cambia)
    // Zona de datos: borrar solo la columna de valores, no las etiquetas
    // Primera vez o si cambia validez: redibujar etiquetas completas
    static bool labelsDrawn = false;
    static bool wasValid = false;

    if (!labelsDrawn || wasValid != pzemData.isValid) {
        // Borrar zona de datos completa
        tft.fillRect(0, y, DISPLAY_WIDTH, DISPLAY_HEIGHT - FOOTER_HEIGHT - 75 - y, COLOR_BG);

        // Título
        tft.fillRect(2, y - 2, DISPLAY_WIDTH - 4, 14, 0x18C3);
        tft.setTextColor(COLOR_YELLOW);
        tft.setCursor(5, y);
        tft.print("PZEM-004T Electrical Readings");

        if (pzemData.isValid) {
            float ap = (pzemData.voltage > 0 && pzemData.pf > 0) ? pzemData.power / pzemData.pf : 0;
            int dy = y + 16;
            // Etiquetas columna izquierda
            tft.setTextColor(COLOR_CYAN);
            tft.setCursor(COL1_X, dy);          tft.print("Voltage: "); dy += ROW_HEIGHT;
            tft.setCursor(COL1_X, dy);          tft.print("Current: "); dy += ROW_HEIGHT;
            tft.setCursor(COL1_X, dy);          tft.print("Power:   "); dy += ROW_HEIGHT;
            tft.setCursor(COL1_X, dy);          tft.print("Energy:  ");
            // Etiquetas columna derecha
            dy = y + 16;
            tft.setCursor(COL2_X, dy);          tft.print("Freq:    "); dy += ROW_HEIGHT;
            tft.setCursor(COL2_X, dy);          tft.print("PF:      "); dy += ROW_HEIGHT;
            tft.setCursor(COL2_X, dy);          tft.print("Apparent:"); dy += ROW_HEIGHT;
            tft.setCursor(COL2_X, dy);          tft.print("Reactive:");
        }
        labelsDrawn = true;
        wasValid = pzemData.isValid;
    }

    if (!pzemData.isValid) {
        tft.fillRect(COL1_X, y + 16, 150, 14, COLOR_BG);
        tft.setTextColor(COLOR_RED);
        tft.setCursor(COL1_X, y + 16);
        tft.print("No valid data available");
        return;
    }

    float ap = (pzemData.voltage > 0 && pzemData.pf > 0) ? pzemData.power / pzemData.pf : 0;
    float rp = sqrt(max(0.0f, ap * ap - pzemData.power * pzemData.power));

    // Ancho fijo para borrar valores: columna izquierda 75px, derecha 70px
    const int VW1 = 75, VW2 = 72;
    const int VX1 = COL1_X + 54;   // offset tras etiqueta más larga "Voltage: "
    const int VX2 = COL2_X + 54;

    int dy = y + 16;
    // Valores columna izquierda
    displayPrintValue(VX1, dy, VW1, String(pzemData.voltage, 1) + " V",  COLOR_WHITE); dy += ROW_HEIGHT;
    displayPrintValue(VX1, dy, VW1, String(pzemData.current, 2) + " A",  COLOR_WHITE); dy += ROW_HEIGHT;
    displayPrintValue(VX1, dy, VW1, String(pzemData.power,   1) + " W",  COLOR_WHITE); dy += ROW_HEIGHT;
    displayPrintValue(VX1, dy, VW1, String(pzemData.energy,  1) + " Wh", COLOR_WHITE);

    dy = y + 16;
    // Valores columna derecha
    displayPrintValue(VX2, dy, VW2, String(pzemData.frequency, 1) + " Hz", COLOR_WHITE); dy += ROW_HEIGHT;
    displayPrintValue(VX2, dy, VW2, String(pzemData.pf, 2),                COLOR_WHITE); dy += ROW_HEIGHT;
    displayPrintValue(VX2, dy, VW2, String(ap, 1) + " VA",                 COLOR_WHITE); dy += ROW_HEIGHT;
    displayPrintValue(VX2, dy, VW2, String(rp, 1) + " VAR",                COLOR_WHITE);
}

void displayDrawATSStatus() {
    int y = DISPLAY_HEIGHT - FOOTER_HEIGHT - 70;
    int boxWidth = DISPLAY_WIDTH - 10;
    int boxHeight = 55;
    int boxX = 5;

    static ATSState lastAtsState = ATS_UNKNOWN;
    static bool atsBoxDrawn = false;

    uint16_t stateColor;
    String stateText;
    switch (atsState) {
        case ATS_UTILITY_POWER:   stateColor = COLOR_GREEN;  stateText = "UTILITY POWER  "; break;
        case ATS_GENERATOR_POWER: stateColor = COLOR_ORANGE; stateText = "GENERATOR POWER"; break;
        default:                  stateColor = COLOR_RED;    stateText = "UNKNOWN        "; break;
    }

    // Dibujar caja + etiqueta fija solo la primera vez o si cambia el estado
    if (!atsBoxDrawn || lastAtsState != atsState) {
        tft.fillRect(boxX, y, boxWidth, boxHeight, 0x18C3);
        tft.drawRect(boxX, y, boxWidth, boxHeight, COLOR_CYAN);
        tft.setTextColor(COLOR_YELLOW);
        tft.setTextSize(1);
        tft.setCursor(boxX + 5, y + 5);
        tft.print("ATS Transfer Switch");
        // Indicador de color
        tft.fillRect(boxX + 5, y + 18, 12, 12, stateColor);
        tft.setTextColor(stateColor);
        tft.setCursor(boxX + 22, y + 20);
        tft.print(stateText);
        lastAtsState = atsState;
        atsBoxDrawn = true;
    }

    // Timer — borrar solo el área del valor y redibujar
    tft.fillRect(boxX + 5, y + 38, 110, 14, 0x18C3);
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    tft.setCursor(boxX + 5, y + 38);
    tft.print("Time: ");
    tft.print(formatDuration(atsGetTimeInState()));

    // Porcentajes — borrar y redibujar
    float utilPct = atsGetPercentageInState(ATS_UTILITY_POWER);
    float genPct  = atsGetPercentageInState(ATS_GENERATOR_POWER);
    tft.fillRect(boxX + 120, y + 20, 185, 14, 0x18C3);
    if (historyCount > 0 || atsGetTimeInState() > 10) {
        tft.setTextColor(COLOR_GREEN);
        tft.setCursor(boxX + 120, y + 20);
        tft.print("U:"); tft.print(utilPct, 0); tft.print("% ");
        tft.setTextColor(COLOR_ORANGE);
        tft.print("G:"); tft.print(genPct, 0); tft.print("%");
    }
}

void displayUpdate() {
    if (!displayInitialized) return;
    unsigned long now = millis();
    if (now - displayLastUpdate < DISPLAY_UPDATE_INTERVAL) return;
    displayLastUpdate = now;

    // Dibujar elementos estáticos solo una vez al inicio
    static bool staticDrawn = false;
    if (!staticDrawn) {
        tft.fillScreen(COLOR_BG);
        displayDrawHeader();
        staticDrawn = true;
    }

    // Actualizar solo zonas dinámicas (sin borrar toda la pantalla)
    displayDrawPZEMData();
    displayDrawATSStatus();

    // Footer: solo redibujar si las cadenas cambiaron (antes se redibujaba
    // siempre, lo que provocaba flicker visible en la franja inferior)
    static String lastWifi = "";
    static String lastPzem = "";
    if (wifiStatusStr != lastWifi || pzemStatusStr != lastPzem) {
        displayDrawFooter(wifiStatusStr, pzemStatusStr);
        lastWifi = wifiStatusStr;
        lastPzem = pzemStatusStr;
    }

    // LDR — solo se imprime a veces para evitar parpadeo. Leemos un valor
    // suavizado del propio módulo de auto-brillo (que ya promedia varias
    // muestras) para no introducir más ruido en pantalla.
    static unsigned long ldrLastPrint = 0;
    if (now - ldrLastPrint >= 3000) {
        ldrLastPrint = now;
        int ldrValue = analogRead(LDR_PIN);
        tft.fillRect(250, 7, 68, 12, COLOR_BLUE);
        tft.setTextColor(COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(250, 7);
        tft.print("LDR:");
        tft.print(ldrValue);
    }
}

void displayShowMessage(const String& message, int duration) {
    if (!displayInitialized) return;
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(2);
    tft.setTextWrap(true);
    tft.setCursor(10, DISPLAY_HEIGHT / 2 - 10);
    tft.print(message);
    delay(duration);
}

void displayWiFiPortalInfo() {
    if (!displayInitialized) return;
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_YELLOW);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("WiFi Setup");
    tft.drawFastHLine(10, 32, DISPLAY_WIDTH - 20, COLOR_CYAN);
    tft.setTextColor(COLOR_CYAN);
    tft.setTextSize(1);
    tft.setCursor(10, 42);
    tft.print("Connect to WiFi:");
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 58);
    tft.print("CYD-Monitor-");
    tft.print(WiFi.macAddress().substring(15));
    tft.setTextColor(COLOR_CYAN);
    tft.setTextSize(1);
    tft.setCursor(10, 82);
    tft.print("Then open browser:");
    tft.setTextColor(COLOR_GREEN);
    tft.setTextSize(2);
    tft.setCursor(10, 98);
    tft.print("192.168.4.1");
    tft.drawFastHLine(10, 122, DISPLAY_WIDTH - 20, COLOR_CYAN);
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 132);
    tft.print("Configure your home WiFi");
    tft.setCursor(10, 148);
    tft.print("network from the portal");
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(10, 170);
    tft.print("Firmware v");
    tft.print(FIRMWARE_VERSION);
    tft.setCursor(10, 190);
    tft.print("Waiting for connection...");
}

// ============================================
// WEB SERVER - Professional Industrial Dashboard
// ============================================
String getMainPage() {
    String page;
    page.reserve(8000);
    
    float apparentPower = 0;
    float reactivePower = 0;
    if (pzemData.isValid && pzemData.voltage > 0 && pzemData.pf > 0) {
        apparentPower = pzemData.power / pzemData.pf;
        reactivePower = sqrt(apparentPower * apparentPower - pzemData.power * pzemData.power);
    }
    
    float utilPct = atsGetPercentageInState(ATS_UTILITY_POWER);
    float genPct = atsGetPercentageInState(ATS_GENERATOR_POWER);
    
    page = F(R"rawliteral(<!DOCTYPE html>
<html lang='es'>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>CYD Power Monitor v)rawliteral");
    page += FIRMWARE_VERSION;
    page += F(R"rawliteral(</title>
<style>
:root{--bg:#0a0e17;--card:#111827;--border:#1f2937;--text:#e5e7eb;--muted:#9ca3af;--accent:#00d4ff;--accent2:#3b82f6;--green:#10b981;--orange:#f59e0b;--red:#ef4444;--purple:#8b5cf6}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);line-height:1.5}
.topbar{background:linear-gradient(135deg,#0f172a 0%,#1e3a5f 100%);padding:0;border-bottom:2px solid var(--accent);position:sticky;top:0;z-index:100}
.topbar-inner{max-width:1400px;margin:0 auto;padding:12px 20px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px}
.logo{display:flex;align-items:center;gap:12px}
.logo-icon{width:36px;height:36px;background:linear-gradient(135deg,var(--accent),var(--accent2));border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:1.2em;font-weight:bold;color:#000}
.logo-text h1{color:var(--accent);font-size:1.3em;letter-spacing:-0.5px}
.logo-text span{color:var(--muted);font-size:0.75em}
.nav{display:flex;gap:6px;flex-wrap:wrap}
.nav a{color:var(--muted);text-decoration:none;padding:8px 16px;border-radius:6px;font-size:0.85em;font-weight:500;transition:all 0.2s;border:1px solid transparent}
.nav a:hover,.nav a.active{color:var(--accent);background:rgba(0,212,255,0.1);border-color:rgba(0,212,255,0.2)}
.container{max-width:1400px;margin:0 auto;padding:20px}
.grid{display:grid;gap:16px}
.grid-4{grid-template-columns:repeat(auto-fit,minmax(220px,1fr))}
.grid-2{grid-template-columns:repeat(auto-fit,minmax(400px,1fr))}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;overflow:hidden}
.card-header{padding:14px 18px;border-bottom:1px solid var(--border);display:flex;justify-content:space-between;align-items:center}
.card-header h2{font-size:0.95em;color:var(--accent);display:flex;align-items:center;gap:8px;font-weight:600}
.card-header h2::before{content:'';width:3px;height:16px;background:var(--accent);border-radius:2px}
.card-body{padding:18px}
.status-badge{display:inline-flex;align-items:center;gap:6px;padding:4px 12px;border-radius:20px;font-size:0.75em;font-weight:600}
.status-badge::before{content:'';width:6px;height:6px;border-radius:50%;animation:pulse 2s infinite}
.status-online{background:rgba(16,185,129,0.15);color:var(--green)}
.status-online::before{background:var(--green)}
.status-offline{background:rgba(239,68,68,0.15);color:var(--red)}
.status-offline::before{background:var(--red);animation:none}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.4}}
.metric-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px}
.metric{background:linear-gradient(135deg,#0f172a 0%,#1e293b 100%);border:1px solid var(--border);border-radius:10px;padding:16px;text-align:center;transition:transform 0.2s}
.metric:hover{transform:translateY(-2px);border-color:var(--accent)}
.metric .icon{font-size:1.5em;margin-bottom:6px}
.metric .label{color:var(--muted);font-size:0.65em;text-transform:uppercase;letter-spacing:1.5px;margin-bottom:4px}
.metric .value{font-size:1.6em;font-weight:700;color:var(--text);line-height:1.2}
.metric .value.accent{color:var(--accent)}
.metric .value.green{color:var(--green)}
.metric .value.orange{color:var(--orange)}
.metric .value.purple{color:var(--purple)}
.metric .unit{font-size:0.5em;color:var(--muted);margin-left:2px}
.metric .sub{color:var(--muted);font-size:0.7em;margin-top:4px}
.ats-panel{text-align:center;padding:24px}
.ats-state{display:inline-flex;align-items:center;gap:12px;padding:14px 32px;border-radius:50px;font-size:1.3em;font-weight:700;margin-bottom:16px;border:2px solid}
.ats-state.utility{background:rgba(16,185,129,0.1);color:var(--green);border-color:var(--green);box-shadow:0 0 20px rgba(16,185,129,0.2)}
.ats-state.generator{background:rgba(245,158,11,0.1);color:var(--orange);border-color:var(--orange);box-shadow:0 0 20px rgba(245,158,11,0.2)}
.ats-state.unknown{background:rgba(239,68,68,0.1);color:var(--red);border-color:var(--red)}
.ats-state::before{content:'';width:12px;height:12px;border-radius:50%;background:currentColor;animation:blink 1.5s infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0.3}}
.ats-timer{font-size:1.1em;color:var(--muted);margin-bottom:20px;font-variant-numeric:tabular-nums}
.ats-stats{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;max-width:500px;margin:0 auto}
.ats-stat{background:#0f172a;border-radius:8px;padding:12px}
.ats-stat .lbl{color:var(--muted);font-size:0.7em;text-transform:uppercase;margin-bottom:4px}
.ats-stat .val{font-size:1.3em;font-weight:700}
.ats-stat .val.green{color:var(--green)}
.ats-stat .val.orange{color:var(--orange)}
.progress-bar{height:28px;background:#0f172a;border-radius:8px;overflow:hidden;display:flex;margin-top:8px;border:1px solid var(--border)}
.progress-bar .fill{height:100%;display:flex;align-items:center;justify-content:center;font-size:0.75em;font-weight:700;transition:width 0.5s ease}
.progress-bar .utility{background:linear-gradient(90deg,var(--green),#059669);color:#fff}
.progress-bar .generator{background:linear-gradient(90deg,var(--orange),#d97706);color:#fff}
.system-info{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.info-item{display:flex;justify-content:space-between;align-items:center;padding:8px 12px;background:#0f172a;border-radius:6px;font-size:0.85em}
.info-item .lbl{color:var(--muted)}
.info-item .val{color:var(--accent);font-weight:600;font-variant-numeric:tabular-nums}
.footer{text-align:center;padding:24px;color:var(--muted);font-size:0.8em;border-top:1px solid var(--border);margin-top:20px}
@media(max-width:768px){.grid-2{grid-template-columns:1fr}.ats-stats{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class='topbar'>
<div class='topbar-inner'>
<div class='logo'>
<div class='logo-icon'>⚡</div>
<div class='logo-text'>
<h1>CYD Power Monitor</h1>
<span>Sistema de Monitoreo Energético Industrial</span>
</div>
</div>
<div class='nav'>
<a href='/' class='active'>Dashboard</a>
<a href='/history'>Histórico ATS</a>
<a href='/devices'>Dispositivos</a>
<a href='/ota'>Actualización</a>
</div>
</div>
</div>
<div class='container'>
<div class='grid grid-4'>
<div class='card'>
<div class='card-header'>
<h2>🌐 Conectividad</h2>
<span class='status-badge status-online'>EN LÍNEA</span>
</div>
<div class='card-body'>
<div class='metric-grid'>
<div class='metric'>
<div class='icon'>📶</div>
<div class='label'>Señal WiFi</div>
<div class='value accent' id='v_rssi'>)rawliteral");
    page += WiFi.RSSI();
    page += F(R"rawliteral(</div><div style='color:var(--muted);font-size:0.65em'>dBm</div>
</div>
<div class='metric'>
<div class='icon'>🕐</div>
<div class='label'>Tiempo Activo</div>
<div class='value' id='v_uptime'>)rawliteral");
    page += formatDurationLong(millis() / 1000);
    page += F(R"rawliteral(</div>
</div>
</div>
<div class='system-info' style='margin-top:12px'>
<div class='info-item'><span class='lbl'>IP Local</span><span class='val'>)rawliteral");
    page += WiFi.localIP().toString();
    page += F(R"rawliteral(</span></div>
<div class='info-item'><span class='lbl'>Hostname</span><span class='val'>)rawliteral");
    page += WiFi.getHostname();
    page += F(R"rawliteral(</span></div>
<div class='info-item'><span class='lbl'>MAC</span><span class='val'>)rawliteral");
    page += WiFi.macAddress();
    page += F(R"rawliteral(</span></div>
</div>
</div>
</div>
<div class='card'>
<div class='card-header'>
<h2>⚡ PZEM-004T</h2>
<span class='status-badge ')rawliteral");
    page += pzemData.isValid ? F("status-online'>CONECTADO") : F("status-offline'>SIN DATOS");
    page += F(R"rawliteral(</span>
</div>
<div class='card-body'>
<div class='metric-grid'>
<div class='metric'>
<div class='icon'>🔌</div>
<div class='label'>Voltaje</div>
<div class='value accent'><span id='v_volt'>)rawliteral");
    page += String(pzemData.voltage, 1);
    page += F(R"rawliteral(</span><span class='unit'>V</span></div>
</div>
<div class='metric'>
<div class='icon'>⚡</div>
<div class='label'>Corriente</div>
<div class='value accent'><span id='v_curr'>)rawliteral");
    page += String(pzemData.current, 2);
    page += F(R"rawliteral(</span><span class='unit'>A</span></div>
</div>
<div class='metric'>
<div class='icon'>💡</div>
<div class='label'>Potencia Activa</div>
<div class='value green'><span id='v_pow'>)rawliteral");
    page += String(pzemData.power, 1);
    page += F(R"rawliteral(</span><span class='unit'>W</span></div>
</div>
<div class='metric'>
<div class='icon'>📊</div>
<div class='label'>Potencia Aparente</div>
<div class='value purple'><span id='v_app'>)rawliteral");
    page += String(apparentPower, 1);
    page += F(R"rawliteral(</span><span class='unit'>VA</span></div>
</div>
<div class='metric'>
<div class='icon'>🔄</div>
<div class='label'>Potencia Reactiva</div>
<div class='value orange'><span id='v_react'>)rawliteral");
    page += String(reactivePower, 1);
    page += F(R"rawliteral(</span><span class='unit'>VAR</span></div>
</div>
<div class='metric'>
<div class='icon'>📈</div>
<div class='label'>Factor de Potencia</div>
<div class='value'><span id='v_pf'>)rawliteral");
    page += String(pzemData.pf, 2);
    page += F(R"rawliteral(</span></div>
</div>
<div class='metric'>
<div class='icon'>🌊</div>
<div class='label'>Frecuencia</div>
<div class='value accent'><span id='v_freq'>)rawliteral");
    page += String(pzemData.frequency, 1);
    page += F(R"rawliteral(</span><span class='unit'>Hz</span></div>
</div>
<div class='metric'>
<div class='icon'>🔋</div>
<div class='label'>Energía Total</div>
<div class='value green'><span id='v_energy'>)rawliteral");
    page += String(pzemData.energy, 1);
    page += F(R"rawliteral(</span><span class='unit'>Wh</span></div>
<div class='sub'><span id='v_kwh'>)rawliteral");
    page += String(pzemData.energy / 1000.0, 3);
    page += F(R"rawliteral(</span> kWh</div>
</div>
</div>
</div>
</div>
<div class='card'>
<div class='card-header'>
<h2>🔄 ATS Transfer Switch</h2>
<span id='ats_badge' class='status-badge ')rawliteral");
    if (atsState == ATS_UTILITY_POWER) page += F("status-online'>RED ELÉCTRICA");
    else if (atsState == ATS_GENERATOR_POWER) page += F("status-badge' style='background:rgba(245,158,11,0.15);color:var(--orange)'>GENERADOR");
    else page += F("status-offline'>DESCONOCIDO");
    page += F(R"rawliteral(</span>
</div>
<div class='card-body'>
<div class='ats-panel'>
<div id='ats_state' class='ats-state )rawliteral");
    if (atsState == ATS_UTILITY_POWER) page += F("utility'>RED ELÉCTRICA");
    else if (atsState == ATS_GENERATOR_POWER) page += F("generator'>GENERADOR");
    else page += F("unknown'>DESCONOCIDO");
    page += F(R"rawliteral(</div>
<div class='ats-timer'>⏱️ Tiempo en estado actual: <strong id='v_atstime'>)rawliteral");
    page += formatDurationLong(atsGetTimeInState());
    page += F(R"rawliteral(</strong></div>
<div class='ats-stats'>
<div class='ats-stat'>
<div class='lbl'>Tiempo Red</div>
<div class='val green' id='v_uttime'>)rawliteral");
    page += formatDurationLong(atsGetTotalTimeInState(ATS_UTILITY_POWER));
    page += F(R"rawliteral(</div>
</div>
<div class='ats-stat'>
<div class='lbl'>Tiempo Generador</div>
<div class='val orange'>)rawliteral");
    page += formatDurationLong(atsGetTotalTimeInState(ATS_GENERATOR_POWER));
    page += F(R"rawliteral(</div>
</div>
<div class='ats-stat'>
<div class='lbl'>Cambios Totales</div>
<div class='val accent'>)rawliteral");
    page += historyCount;
    page += F(R"rawliteral(</div>
</div>
</div>
<div style='margin-top:16px'>
<div style='display:flex;justify-content:space-between;font-size:0.8em;margin-bottom:4px'>
<span style='color:var(--green)'>Red: <span id='v_upct'>)rawliteral");
    page += String(utilPct, 1);
    page += F(R"rawliteral(%</span></span>
<span style='color:var(--orange)'>Generador: <span id='v_gpct'>)rawliteral");
    page += String(genPct, 1);
    page += F(R"rawliteral(%</span></span>
</div>
<div class='progress-bar'>
<div class='fill utility' id='bar_util' style='width:)rawliteral");
    page += String(utilPct, 1);
    page += F(R"rawliteral(%'>Red</div>
<div class='fill generator' id='bar_gen' style='width:)rawliteral");
    page += String(genPct, 1);
    page += F(R"rawliteral(%'>Gen</div>
</div>
</div>
</div>
</div>
</div>
<div class='card'>
<div class='card-header'>
<h2>📊 Estadísticas del Sistema</h2>
</div>
<div class='card-body'>
<div class='metric-grid'>
<div class='metric'>
<div class='icon'>📋</div>
<div class='label'>Registros ATS</div>
<div class='value accent'>)rawliteral");
    page += historyCount;
    page += F(R"rawliteral(<span class='unit'>/100</span></div>
</div>
<div class='metric'>
<div class='icon'>🔁</div>
<div class='label'>Cambios a Red</div>
<div class='value green'>)rawliteral");
    page += atsGetStateChangesCount(ATS_UTILITY_POWER);
    page += F(R"rawliteral(</div>
</div>
<div class='metric'>
<div class='icon'>🔁</div>
<div class='label'>Cambios a Gen</div>
<div class='value orange'>)rawliteral");
    page += atsGetStateChangesCount(ATS_GENERATOR_POWER);
    page += F(R"rawliteral(</div>
</div>
<div class='metric'>
<div class='icon'>⏱️</div>
<div class='label'>Duración Prom. Red</div>
<div class='value green'>)rawliteral");
    page += formatDurationLong(atsGetAverageDuration(ATS_UTILITY_POWER));
    page += F(R"rawliteral(</div>
</div>
<div class='metric'>
<div class='icon'>⏱️</div>
<div class='label'>Duración Prom. Gen</div>
<div class='value orange'>)rawliteral");
    page += formatDurationLong(atsGetAverageDuration(ATS_GENERATOR_POWER));
    page += F(R"rawliteral(</div>
</div>
<div class='metric'>
<div class='icon'>🔋</div>
<div class='label'>Eficiencia</div>
<div class='value purple'>)rawliteral");
    page += pzemData.pf > 0 ? String(pzemData.pf * 100, 0) : F("0");
    page += F(R"rawliteral(<span class='unit'>%</span></div>
</div>
</div>
</div>
</div>
</div>
<div class='footer'>
<p>CYD Power Monitor v)rawliteral");
    page += FIRMWARE_VERSION;
    page += F(R"rawliteral( | ESP32-2432S028 | Sistema de Monitoreo Energético Industrial</p>
<p style='margin-top:4px;font-size:0.85em' id='lastUpdate'>Conectando...</p>
</div>
</div>
<script>
function fmt(s){s=Math.round(s);var d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),m=Math.floor((s%3600)/60),sc=s%60,r='';if(d>0)r+=d+'d ';if(h>0||d>0)r+=h+'h ';if(m>0||h>0||d>0)r+=m+'m ';return r+sc+'s';}
function set(id,v){var e=document.getElementById(id);if(e)e.textContent=v;}
function refresh(){
  fetch('/api/data').then(r=>r.json()).then(d=>{
    var p=d.pzem,a=d.ats,s=d.system;
    set('v_volt',p.voltage.toFixed(1));set('v_curr',p.current.toFixed(2));
    set('v_pow',p.power.toFixed(1));set('v_energy',p.energy.toFixed(1));
    set('v_kwh',(p.energy/1000).toFixed(3));set('v_freq',p.frequency.toFixed(1));
    set('v_pf',p.pf.toFixed(2));
    var ap=p.pf>0?p.power/p.pf:0,rp=Math.sqrt(Math.max(0,ap*ap-p.power*p.power));
    set('v_app',ap.toFixed(1));set('v_react',rp.toFixed(1));
    set('v_rssi',s.rssi);set('v_uptime',fmt(s.uptime));
    set('v_atstime',fmt(a.timeInState));set('v_changes',a.changes);
    var ub=document.getElementById('bar_util'),gb=document.getElementById('bar_gen');
    if(ub)ub.style.width=a.utilPct.toFixed(1)+'%';
    if(gb)gb.style.width=a.genPct.toFixed(1)+'%';
    set('v_upct',a.utilPct.toFixed(1)+'%');set('v_gpct',a.genPct.toFixed(1)+'%');
    var badge=document.getElementById('ats_badge'),st=document.getElementById('ats_state');
    if(a.state==='UTILITY'){
      if(badge){badge.className='status-badge status-online';badge.textContent='RED ELÉCTRICA';badge.removeAttribute('style');}
      if(st){st.className='ats-state utility';st.textContent='RED ELÉCTRICA';}
    }else if(a.state==='GENERATOR'){
      if(badge){badge.className='status-badge';badge.setAttribute('style','background:rgba(245,158,11,0.15);color:var(--orange)');badge.textContent='GENERADOR';}
      if(st){st.className='ats-state generator';st.textContent='GENERADOR';}
    }else{
      if(badge){badge.className='status-badge status-offline';badge.textContent='DESCONOCIDO';badge.removeAttribute('style');}
      if(st){st.className='ats-state unknown';st.textContent='DESCONOCIDO';}
    }
    set('lastUpdate','Última actualización: '+new Date().toLocaleTimeString());
  }).catch(()=>{});
}
refresh();setInterval(refresh,)rawliteral");
    page += String(WEB_REFRESH_INTERVAL * 1000);
    page += F(R"rawliteral();
</script>
</body>
</html>)rawliteral");
    
    return page;
}

String getHistoryPage(String filter) {
    String page;
    page.reserve(10000);
    
    unsigned long ut = atsGetTotalTimeInState(ATS_UTILITY_POWER);
    unsigned long gt = atsGetTotalTimeInState(ATS_GENERATOR_POWER);
    unsigned long tt = ut + gt;
    float up = tt > 0 ? (float)ut * 100 / tt : 0;
    float gp = tt > 0 ? (float)gt * 100 / tt : 0;
    
    int maxEntries = 50;
    if (filter == "week") maxEntries = 100;
    else if (filter == "month") maxEntries = 100;
    else if (filter == "year") maxEntries = 100;
    
    page = F(R"rawliteral(<!DOCTYPE html>
<html lang='es'>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<meta http-equiv='refresh' content='30'>
<title>Histórico ATS - CYD Power Monitor</title>
<style>
:root{--bg:#0a0e17;--card:#111827;--border:#1f2937;--text:#e5e7eb;--muted:#9ca3af;--accent:#00d4ff;--accent2:#3b82f6;--green:#10b981;--orange:#f59e0b;--red:#ef4444;--purple:#8b5cf6;--yellow:#eab308}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);line-height:1.5}
.topbar{background:linear-gradient(135deg,#0f172a 0%,#1e3a5f 100%);padding:0;border-bottom:2px solid var(--accent);position:sticky;top:0;z-index:100}
.topbar-inner{max-width:1400px;margin:0 auto;padding:12px 20px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px}
.logo{display:flex;align-items:center;gap:12px}
.logo-icon{width:36px;height:36px;background:linear-gradient(135deg,var(--accent),var(--accent2));border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:1.2em;font-weight:bold;color:#000}
.logo-text h1{color:var(--accent);font-size:1.3em;letter-spacing:-0.5px}
.logo-text span{color:var(--muted);font-size:0.75em}
.nav{display:flex;gap:6px;flex-wrap:wrap}
.nav a{color:var(--muted);text-decoration:none;padding:8px 16px;border-radius:6px;font-size:0.85em;font-weight:500;transition:all 0.2s;border:1px solid transparent}
.nav a:hover,.nav a.active{color:var(--accent);background:rgba(0,212,255,0.1);border-color:rgba(0,212,255,0.2)}
.container{max-width:1400px;margin:0 auto;padding:20px}
.grid{display:grid;gap:16px}
.grid-4{grid-template-columns:repeat(auto-fit,minmax(200px,1fr))}
.grid-2{grid-template-columns:repeat(auto-fit,minmax(400px,1fr))}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;overflow:hidden}
.card-header{padding:14px 18px;border-bottom:1px solid var(--border);display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px}
.card-header h2{font-size:0.95em;color:var(--accent);display:flex;align-items:center;gap:8px;font-weight:600}
.card-header h2::before{content:'';width:3px;height:16px;background:var(--accent);border-radius:2px}
.card-body{padding:18px}
.filters{display:flex;gap:8px;flex-wrap:wrap}
.filter-btn{padding:8px 16px;background:#0f172a;border:1px solid var(--border);border-radius:8px;color:var(--muted);text-decoration:none;font-size:0.85em;font-weight:500;transition:all 0.2s}
.filter-btn:hover,.filter-btn.active{background:var(--accent);color:#000;border-color:var(--accent)}
.stat-card{background:linear-gradient(135deg,#0f172a 0%,#1e293b 100%);border:1px solid var(--border);border-radius:10px;padding:18px;text-align:center;transition:transform 0.2s}
.stat-card:hover{transform:translateY(-2px);border-color:var(--accent)}
.stat-card .icon{font-size:1.8em;margin-bottom:8px}
.stat-card .label{color:var(--muted);font-size:0.7em;text-transform:uppercase;letter-spacing:1px;margin-bottom:6px}
.stat-card .value{font-size:1.5em;font-weight:700;margin-bottom:4px}
.stat-card .sub{color:var(--muted);font-size:0.75em}
.stat-card.green .value{color:var(--green)}
.stat-card.orange .value{color:var(--orange)}
.stat-card.purple .value{color:var(--purple)}
.stat-card.accent .value{color:var(--accent)}
.stat-card.yellow .value{color:var(--yellow)}
.chart-container{background:#0f172a;border-radius:10px;padding:16px;margin-bottom:16px;text-align:center}
.chart-title{color:var(--muted);font-size:0.8em;text-transform:uppercase;letter-spacing:1px;margin-bottom:12px}
.donut-wrapper{position:relative;width:120px;height:120px;margin:0 auto}
.donut-chart{width:120px;height:120px;border-radius:50%;background:conic-gradient(var(--green) )rawliteral");
    page += String(up, 1);
    page += F(R"rawliteral(% , var(--orange) 0)}
.donut-hole{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);width:80px;height:80px;background:var(--card);border-radius:50%;display:flex;flex-direction:column;align-items:center;justify-content:center}
.donut-label-pct{font-size:1.1em;font-weight:700;color:var(--accent)}
.donut-label-txt{font-size:0.6em;color:var(--muted)}
.timeline{position:relative;padding-left:24px}
.timeline::before{content:'';position:absolute;left:8px;top:0;bottom:0;width:2px;background:linear-gradient(to bottom,var(--green),var(--orange))}
.timeline-item{position:relative;margin-bottom:16px;padding:14px;background:#0f172a;border-radius:10px;border:1px solid var(--border);transition:all 0.2s}
.timeline-item:hover{border-color:var(--accent);transform:translateX(4px)}
.timeline-item::before{content:'';position:absolute;left:-20px;top:20px;width:10px;height:10px;border-radius:50%;border:2px solid var(--card)}
.timeline-item.utility::before{background:var(--green);box-shadow:0 0 8px var(--green)}
.timeline-item.generator::before{background:var(--orange);box-shadow:0 0 8px var(--orange)}
.timeline-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
.timeline-badge{display:inline-flex;align-items:center;gap:6px;padding:4px 12px;border-radius:20px;font-size:0.8em;font-weight:600}
.timeline-badge.utility{background:rgba(16,185,129,0.15);color:var(--green)}
.timeline-badge.generator{background:rgba(245,158,11,0.15);color:var(--orange)}
.timeline-time{color:var(--muted);font-size:0.8em;font-variant-numeric:tabular-nums}
.timeline-details{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px;margin-top:8px;padding-top:8px;border-top:1px solid var(--border)}
.detail-item{display:flex;justify-content:space-between;font-size:0.8em}
.detail-item .lbl{color:var(--muted)}
.detail-item .val{color:var(--text);font-weight:600}
table{width:100%;border-collapse:separate;border-spacing:0;font-size:0.85em}
th{background:#0f172a;color:var(--muted);padding:12px;text-align:left;font-size:0.75em;text-transform:uppercase;letter-spacing:1px;font-weight:600;border-bottom:2px solid var(--border)}
td{padding:12px;border-bottom:1px solid var(--border)}
tr:hover td{background:rgba(0,212,255,0.03)}
tr:last-child td{border-bottom:none}
.badge{display:inline-flex;align-items:center;gap:4px;padding:4px 10px;border-radius:12px;font-size:0.8em;font-weight:600}
.badge.utility{background:rgba(16,185,129,0.15);color:var(--green)}
.badge.generator{background:rgba(245,158,11,0.15);color:var(--orange)}
.badge.unknown{background:rgba(239,68,68,0.15);color:var(--red)}
.empty-state{text-align:center;padding:40px;color:var(--muted)}
.empty-state .icon{font-size:3em;margin-bottom:12px}
.footer{text-align:center;padding:24px;color:var(--muted);font-size:0.8em;border-top:1px solid var(--border);margin-top:20px}
@media(max-width:768px){.grid-2{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class='topbar'>
<div class='topbar-inner'>
<div class='logo'>
<div class='logo-icon'>📊</div>
<div class='logo-text'>
<h1>Histórico ATS</h1>
<span>Análisis detallado de transferencias</span>
</div>
</div>
<div class='nav'>
<a href='/'>Dashboard</a>
<a href='/history' class='active'>Histórico</a>
<a href='/devices'>Dispositivos</a>
<a href='/ota'>Actualización</a>
</div>
</div>
</div>
<div class='container'>
<div class='grid grid-4'>
<div class='stat-card green'>
<div class='icon'>⚡</div>
<div class='label'>Tiempo Total Red</div>
<div class='value'>)rawliteral");
    page += formatDurationLong(ut);
    page += F(R"rawliteral(</div>
<div class='sub'>)rawliteral");
    page += String(up, 1);
    page += F(R"rawliteral(% del tiempo</div>
</div>
<div class='stat-card orange'>
<div class='icon'>🔌</div>
<div class='label'>Tiempo Total Generador</div>
<div class='value'>)rawliteral");
    page += formatDurationLong(gt);
    page += F(R"rawliteral(</div>
<div class='sub'>)rawliteral");
    page += String(gp, 1);
    page += F(R"rawliteral(% del tiempo</div>
</div>
<div class='stat-card accent'>
<div class='icon'>🔁</div>
<div class='label'>Total de Cambios</div>
<div class='value'>)rawliteral");
    page += historyCount;
    page += F(R"rawliteral(</div>
<div class='sub'>Registros en buffer</div>
</div>
<div class='stat-card purple'>
<div class='icon'>⏱️</div>
<div class='label'>Tiempo Total Monitoreo</div>
<div class='value'>)rawliteral");
    page += formatDurationLong(tt);
    page += F(R"rawliteral(</div>
<div class='sub'>Desde el inicio</div>
</div>
</div>
<div class='grid grid-2'>
<div class='card'>
<div class='card-header'>
<h2>📈 Distribución de Uso</h2>
</div>
<div class='card-body'>
<div class='chart-container'>
<div class='chart-title'>Tiempo en cada fuente de energía</div>
<div class='donut-wrapper'>
<div class='donut-chart'></div>
<div class='donut-hole'>
<div class='donut-label-pct'>)rawliteral");
    page += String(up, 1);
    page += F(R"rawliteral(%</div>
<div class='donut-label-txt'>Red</div>
</div>
</div>
</div>
<div style='display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:16px'>
<div style='text-align:center;padding:12px;background:rgba(16,185,129,0.1);border-radius:8px;border:1px solid rgba(16,185,129,0.2)'>
<div style='color:var(--green);font-size:1.5em;font-weight:700'>)rawliteral");
    page += String(up, 1);
    page += F(R"rawliteral(%</div>
<div style='color:var(--muted);font-size:0.8em;margin-top:4px'>Red Eléctrica</div>
<div style='color:var(--green);font-size:0.9em;margin-top:2px'>)rawliteral");
    page += formatDurationLong(ut);
    page += F(R"rawliteral(</div>
</div>
<div style='text-align:center;padding:12px;background:rgba(245,158,11,0.1);border-radius:8px;border:1px solid rgba(245,158,11,0.2)'>
<div style='color:var(--orange);font-size:1.5em;font-weight:700'>)rawliteral");
    page += String(gp, 1);
    page += F(R"rawliteral(%</div>
<div style='color:var(--muted);font-size:0.8em;margin-top:4px'>Generador</div>
<div style='color:var(--orange);font-size:0.9em;margin-top:2px'>)rawliteral");
    page += formatDurationLong(gt);
    page += F(R"rawliteral(</div>
</div>
</div>
</div>
</div>
<div class='card'>
<div class='card-header'>
<h2>📊 Estadísticas Detalladas</h2>
</div>
<div class='card-body'>
<div style='display:grid;gap:10px'>
<div style='display:flex;justify-content:space-between;align-items:center;padding:12px;background:#0f172a;border-radius:8px'>
<span style='color:var(--muted);font-size:0.85em'>Cambios a Red</span>
<span style='color:var(--green);font-weight:700;font-size:1.1em'>)rawliteral");
    page += atsGetStateChangesCount(ATS_UTILITY_POWER);
    page += F(R"rawliteral(</span>
</div>
<div style='display:flex;justify-content:space-between;align-items:center;padding:12px;background:#0f172a;border-radius:8px'>
<span style='color:var(--muted);font-size:0.85em'>Cambios a Generador</span>
<span style='color:var(--orange);font-weight:700;font-size:1.1em'>)rawliteral");
    page += atsGetStateChangesCount(ATS_GENERATOR_POWER);
    page += F(R"rawliteral(</span>
</div>
<div style='display:flex;justify-content:space-between;align-items:center;padding:12px;background:#0f172a;border-radius:8px'>
<span style='color:var(--muted);font-size:0.85em'>Duración Máx. Red</span>
<span style='color:var(--green);font-weight:700;font-size:1.1em'>)rawliteral");
    page += formatDurationLong(atsGetMaxDuration(ATS_UTILITY_POWER));
    page += F(R"rawliteral(</span>
</div>
<div style='display:flex;justify-content:space-between;align-items:center;padding:12px;background:#0f172a;border-radius:8px'>
<span style='color:var(--muted);font-size:0.85em'>Duración Máx. Gen</span>
<span style='color:var(--orange);font-weight:700;font-size:1.1em'>)rawliteral");
    page += formatDurationLong(atsGetMaxDuration(ATS_GENERATOR_POWER));
    page += F(R"rawliteral(</span>
</div>
<div style='display:flex;justify-content:space-between;align-items:center;padding:12px;background:#0f172a;border-radius:8px'>
<span style='color:var(--muted);font-size:0.85em'>Duración Prom. Red</span>
<span style='color:var(--green);font-weight:700;font-size:1.1em'>)rawliteral");
    page += formatDurationLong(atsGetAverageDuration(ATS_UTILITY_POWER));
    page += F(R"rawliteral(</span>
</div>
<div style='display:flex;justify-content:space-between;align-items:center;padding:12px;background:#0f172a;border-radius:8px'>
<span style='color:var(--muted);font-size:0.85em'>Duración Prom. Gen</span>
<span style='color:var(--orange);font-weight:700;font-size:1.1em'>)rawliteral");
    page += formatDurationLong(atsGetAverageDuration(ATS_GENERATOR_POWER));
    page += F(R"rawliteral(</span>
</div>
<div style='display:flex;justify-content:space-between;align-items:center;padding:12px;background:#0f172a;border-radius:8px'>
<span style='color:var(--muted);font-size:0.85em'>Duración Mín. Red</span>
<span style='color:var(--green);font-weight:700;font-size:1.1em'>)rawliteral");
    page += formatDurationLong(atsGetMinDuration(ATS_UTILITY_POWER));
    page += F(R"rawliteral(</span>
</div>
<div style='display:flex;justify-content:space-between;align-items:center;padding:12px;background:#0f172a;border-radius:8px'>
<span style='color:var(--muted);font-size:0.85em'>Duración Mín. Gen</span>
<span style='color:var(--orange);font-weight:700;font-size:1.1em'>)rawliteral");
    page += formatDurationLong(atsGetMinDuration(ATS_GENERATOR_POWER));
    page += F(R"rawliteral(</span>
</div>
</div>
</div>
</div>
</div>
<div class='card'>
<div class='card-header'>
<h2>📈 Tendencia Diaria (Red vs Generador)</h2>
</div>
<div class='card-body'>)rawliteral");
    page += buildDailyTrendChart();
    page += F(R"rawliteral(
<div style='display:flex;gap:16px;justify-content:center;margin-top:10px;font-size:0.8em'>
<span style='color:var(--green)'>■ Red Eléctrica</span>
<span style='color:var(--orange)'>■ Generador</span>
</div>
</div>
</div>
<div class='card'>
<div class='card-header'>
<h2>🕐 Línea Temporal de Eventos</h2>
<div class='filters'>)rawliteral");
    
    String fNames[] = {"Últimas 24h", "7 Días", "30 Días", "1 Año"};
    String fVals[] = {"day", "week", "month", "year"};
    for (int i = 0; i < 4; i++) {
        page += F("<a href='/history?filter=");
        page += fVals[i];
        page += F("' class='filter-btn");
        if (filter == fVals[i]) page += F(" active");
        page += F("'>");
        page += fNames[i];
        page += F("</a>");
    }
    
    page += F(R"rawliteral(</div>
</div>
<div class='card-body'>
<div class='timeline'>)rawliteral");
    
    if (historyCount == 0) {
        page += F(R"rawliteral(<div class='empty-state'>
<div class='icon'>📭</div>
<h3>Sin registros históricos</h3>
<p>Los eventos de cambio de estado aparecerán aquí automáticamente.</p>
</div>)rawliteral");
    } else {
        int si = historyCount < maxEntries ? 0 : historyCount - maxEntries;
        int dc = min(historyCount, maxEntries);
        for (int i = dc - 1; i >= 0; i--) {
            int idx = (si + i) % MAX_HISTORY_ENTRIES;
            bool isUtility = (atsHistory[idx].state == ATS_UTILITY_POWER);
            
            page += F("<div class='timeline-item ");
            page += isUtility ? F("utility") : F("generator");
            page += F("'>");
            page += F("<div class='timeline-header'>");
            page += F("<span class='timeline-badge ");
            page += isUtility ? F("utility'>⚡ RED ELÉCTRICA") : F("generator'>🔌 GENERADOR");
            page += F("</span>");
            page += F("<span class='timeline-time'>#");
            page += (i + 1);
            page += F(" • ");
            page += formatRealTimestamp(atsHistory[idx].timestamp);
            page += F("</span></div>");
            page += F("<div class='timeline-details'>");
            page += F("<div class='detail-item'><span class='lbl'>Duración</span><span class='val'>");
            page += formatDurationLong(atsHistory[idx].duration);
            page += F("</span></div>");
            page += F("<div class='detail-item'><span class='lbl'>Registrado en</span><span class='val'>");
            page += formatRealTimestamp(atsHistory[idx].timestamp);
            page += F("</span></div>");
            page += F("<div class='detail-item'><span class='lbl'>Fuente</span><span class='val' style='color:");
            page += isUtility ? F("var(--green)'>Red Eléctrica") : F("var(--orange)'>Generador");
            page += F("</span></div>");
            page += F("</div></div>");
        }
    }
    
    page += F(R"rawliteral(</div>
</div>
</div>
<div class='card'>
<div class='card-header' style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px'>
<h2>📋 Registro Completo</h2>
<a href='/history.csv' style='background:var(--accent);color:#000;padding:8px 16px;border-radius:6px;font-weight:bold;font-size:0.85em;text-decoration:none'>⬇️ Descargar CSV</a>
</div>
<div class='card-body' style='overflow-x:auto'>
<table>
<thead>
<tr>
<th>#</th>
<th>Estado</th>
<th>Duración</th>
<th>Fecha / Hora</th>
<th>% del total</th>
</tr>
</thead>
<tbody>)rawliteral");
    
    if (historyCount == 0) {
        page += F("<tr><td colspan='5' style='text-align:center;padding:30px;color:var(--muted)'>No hay registros disponibles</td></tr>");
    } else {
        int si = historyCount < maxEntries ? 0 : historyCount - maxEntries;
        int dc = min(historyCount, maxEntries);
        unsigned long totalDur = 0;
        for (int i = 0; i < historyCount; i++) totalDur += atsHistory[i].duration;
        
        for (int i = 0; i < dc; i++) {
            int idx = (si + i) % MAX_HISTORY_ENTRIES;
            bool isUtility = (atsHistory[idx].state == ATS_UTILITY_POWER);
            float pct = totalDur > 0 ? (float)atsHistory[idx].duration * 100.0 / totalDur : 0;
            
            page += F("<tr><td>");
            page += (i + 1);
            page += F("</td><td><span class='badge ");
            page += isUtility ? F("utility'>⚡ RED") : F("generator'>🔌 GEN");
            page += F("</span></td><td>");
            page += formatDurationLong(atsHistory[idx].duration);
            page += F("</td><td>");
            page += formatRealTimestamp(atsHistory[idx].timestamp);
            page += F("</td><td>");
            page += String(pct, 2);
            page += F("%</td></tr>");
        }
    }
    
    page += F(R"rawliteral(</tbody>
</table>
</div>
</div>
<div class='footer'>
<p>CYD Power Monitor v)rawliteral");
    page += FIRMWARE_VERSION;
    page += F(R"rawliteral( | ESP32-2432S028 | Sistema de Monitoreo Energético Industrial</p>
<p style='margin-top:4px'>Página actualizada automáticamente cada 30 segundos</p>
</div>
</div>
</body>
</html>)rawliteral");
    
    return page;
}

// ============================================
// OTA PAGE  ← PARTE FALTANTE - CONTINUACIÓN
// ============================================
String getOTAPage() {
    String page = F(R"rawliteral(<!DOCTYPE html>
<html lang='es'>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Actualización OTA - CYD Power Monitor</title>
<style>
:root{--bg:#0a0e17;--card:#111827;--border:#1f2937;--text:#e5e7eb;--muted:#9ca3af;--accent:#00d4ff;--accent2:#3b82f6;--green:#10b981;--red:#ef4444;--orange:#f59e0b}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);line-height:1.5}
.topbar{background:linear-gradient(135deg,#0f172a 0%,#1e3a5f 100%);padding:0;border-bottom:2px solid var(--accent)}
.topbar-inner{max-width:1400px;margin:0 auto;padding:12px 20px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px}
.logo{display:flex;align-items:center;gap:12px}
.logo-icon{width:36px;height:36px;background:linear-gradient(135deg,var(--accent),var(--accent2));border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:1.2em;font-weight:bold;color:#000}
.logo-text h1{color:var(--accent);font-size:1.3em}
.nav{display:flex;gap:6px;flex-wrap:wrap}
.nav a{color:var(--muted);text-decoration:none;padding:8px 16px;border-radius:6px;font-size:0.85em;font-weight:500;transition:all 0.2s;border:1px solid transparent}
.nav a:hover,.nav a.active{color:var(--accent);background:rgba(0,212,255,0.1);border-color:rgba(0,212,255,0.2)}
.container{max-width:800px;margin:0 auto;padding:20px}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;overflow:hidden;margin-bottom:16px}
.card-header{padding:14px 18px;border-bottom:1px solid var(--border)}
.card-header h2{font-size:0.95em;color:var(--accent);display:flex;align-items:center;gap:8px;font-weight:600}
.card-header h2::before{content:'';width:3px;height:16px;background:var(--accent);border-radius:2px}
.card-body{padding:18px}
.upload-area{border:2px dashed var(--border);border-radius:12px;padding:40px;text-align:center;transition:all 0.2s;cursor:pointer}
.upload-area:hover,.upload-area.dragover{border-color:var(--accent);background:rgba(0,212,255,0.05)}
.upload-area .icon{font-size:3em;margin-bottom:12px}
.upload-area h3{color:var(--text);margin-bottom:8px}
.upload-area p{color:var(--muted);font-size:0.9em;margin-bottom:16px}
.file-input{display:none}
.btn{background:linear-gradient(135deg,var(--accent),var(--accent2));color:#000;border:none;padding:12px 32px;border-radius:8px;font-weight:600;font-size:1em;cursor:pointer;transition:all 0.2s;display:inline-block}
.btn:hover{opacity:0.85;transform:translateY(-1px)}
.btn:disabled{opacity:0.4;cursor:not-allowed;transform:none}
.btn-danger{background:linear-gradient(135deg,var(--red),#b91c1c);color:#fff}
.btn-secondary{background:#1f2937;color:var(--text);border:1px solid var(--border)}
.btn-secondary:hover{background:#374151}
.progress-wrap{display:none;margin-top:20px}
.progress-bar{height:12px;background:#0f172a;border-radius:6px;overflow:hidden;border:1px solid var(--border);margin-bottom:8px}
.progress-fill{height:100%;background:linear-gradient(90deg,var(--accent),var(--accent2));border-radius:6px;transition:width 0.3s ease;width:0%}
.progress-text{text-align:center;color:var(--muted);font-size:0.85em}
.status-msg{padding:12px 16px;border-radius:8px;margin-top:12px;display:none;font-size:0.9em}
.status-msg.success{background:rgba(16,185,129,0.15);border:1px solid rgba(16,185,129,0.3);color:var(--green)}
.status-msg.error{background:rgba(239,68,68,0.15);border:1px solid rgba(239,68,68,0.3);color:var(--red)}
.status-msg.info{background:rgba(0,212,255,0.15);border:1px solid rgba(0,212,255,0.3);color:var(--accent)}
.info-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px}
.info-row{display:flex;justify-content:space-between;align-items:center;padding:10px 14px;background:#0f172a;border-radius:8px;font-size:0.85em}
.info-row .lbl{color:var(--muted)}
.info-row .val{color:var(--accent);font-weight:600}
.warning-box{background:rgba(245,158,11,0.1);border:1px solid rgba(245,158,11,0.3);border-radius:10px;padding:16px;margin-bottom:16px;display:flex;gap:12px;align-items:flex-start}
.warning-box .wi{font-size:1.5em;flex-shrink:0}
.warning-box .wt{color:var(--orange);font-size:0.9em}
.warning-box .wt strong{display:block;margin-bottom:4px}
.file-info{background:#0f172a;border-radius:8px;padding:12px 16px;margin-top:12px;display:none;font-size:0.85em}
.file-info .fname{color:var(--accent);font-weight:600}
.file-info .fsize{color:var(--muted);margin-top:2px}
footer{text-align:center;padding:24px;color:var(--muted);font-size:0.8em;border-top:1px solid var(--border);margin-top:20px}
</style>
</head>
<body>
<div class='topbar'>
<div class='topbar-inner'>
<div class='logo'>
<div class='logo-icon'>🔧</div>
<div class='logo-text'>
<h1>Actualización OTA</h1>
</div>
</div>
<div class='nav'>
<a href='/'>Dashboard</a>
<a href='/history'>Histórico</a>
<a href='/devices'>Dispositivos</a>
<a href='/ota' class='active'>Actualización</a>
</div>
</div>
</div>
<div class='container'>
<div class='warning-box'>
<div class='wi'>⚠️</div>
<div class='wt'>
<strong>Advertencia de actualización OTA</strong>
No desconecte la alimentación ni cierre esta página durante el proceso de actualización.
El dispositivo se reiniciará automáticamente al finalizar.
</div>
</div>
<div class='card'>
<div class='card-header'>
<h2>📋 Información del Firmware Actual</h2>
</div>
<div class='card-body'>
<div class='info-grid'>
<div class='info-row'><span class='lbl'>Versión</span><span class='val'>v)rawliteral");
    page += FIRMWARE_VERSION;
    page += F(R"rawliteral(</span></div>
<div class='info-row'><span class='lbl'>Chip</span><span class='val'>ESP32</span></div>
<div class='info-row'><span class='lbl'>SDK</span><span class='val'>)rawliteral");
    page += String(ESP.getSdkVersion());
    page += F(R"rawliteral(</span></div>
<div class='info-row'><span class='lbl'>Flash Total</span><span class='val'>)rawliteral");
    page += String(ESP.getFlashChipSize() / 1024) + "KB";
    page += F(R"rawliteral(</span></div>
<div class='info-row'><span class='lbl'>Sketch Size</span><span class='val'>)rawliteral");
    page += String(ESP.getSketchSize() / 1024) + "KB";
    page += F(R"rawliteral(</span></div>
<div class='info-row'><span class='lbl'>Libre para OTA</span><span class='val'>)rawliteral");
    page += String(ESP.getFreeSketchSpace() / 1024) + "KB";
    page += F(R"rawliteral(</span></div>
<div class='info-row'><span class='lbl'>Heap Libre</span><span class='val'>)rawliteral");
    page += String(ESP.getFreeHeap() / 1024) + "KB";
    page += F(R"rawliteral(</span></div>
<div class='info-row'><span class='lbl'>Uptime</span><span class='val'>)rawliteral");
    page += formatDurationLong(millis() / 1000);
    page += F(R"rawliteral(</span></div>
</div>
</div>
</div>
<div class='card'>
<div class='card-header'>
<h2>⬆️ Subir Nuevo Firmware</h2>
</div>
<div class='card-body'>
<div class='upload-area' id='dropZone' onclick='document.getElementById("firmware").click()'>
<div class='icon'>📦</div>
<h3>Seleccionar archivo .bin</h3>
<p>Haz clic aquí o arrastra y suelta el archivo de firmware compilado</p>
<input type='file' id='firmware' class='file-input' accept='.bin'>
<button class='btn' type='button' onclick='event.stopPropagation();document.getElementById("firmware").click()'>
Seleccionar Archivo
</button>
</div>
<div class='file-info' id='fileInfo'>
<div class='fname' id='fileName'>—</div>
<div class='fsize' id='fileSize'>—</div>
</div>
<div class='progress-wrap' id='progressWrap'>
<div class='progress-bar'>
<div class='progress-fill' id='progressFill'></div>
</div>
<div class='progress-text' id='progressText'>Subiendo... 0%</div>
</div>
<div class='status-msg' id='statusMsg'></div>
<div style='margin-top:20px;display:flex;gap:12px;flex-wrap:wrap'>
<button class='btn' id='uploadBtn' onclick='startUpload()' disabled>
⬆️ Iniciar Actualización
</button>
<button class='btn btn-secondary' onclick='window.location="/"'>
← Volver al Dashboard
</button>
</div>
</div>
</div>
<div class='card'>
<div class='card-header'>
<h2>🔄 Reiniciar Dispositivo</h2>
</div>
<div class='card-body'>
<p style='color:var(--muted);margin-bottom:16px;font-size:0.9em'>
Reinicia el ESP32 sin actualizar el firmware. Útil para aplicar cambios de configuración.
</p>
<button class='btn btn-danger' onclick='rebootDevice()'>
🔄 Reiniciar Ahora
</button>
</div>
</div>
</div>
<footer>
<p>CYD Power Monitor v)rawliteral");
    page += FIRMWARE_VERSION;
    page += F(R"rawliteral( | ESP32-2432S028</p>
</footer>
<script>
const firmware = document.getElementById('firmware');
const uploadBtn = document.getElementById('uploadBtn');
const progressWrap = document.getElementById('progressWrap');
const progressFill = document.getElementById('progressFill');
const progressText = document.getElementById('progressText');
const statusMsg = document.getElementById('statusMsg');
const fileInfo = document.getElementById('fileInfo');
const dropZone = document.getElementById('dropZone');

firmware.addEventListener('change', function() {
  if (this.files.length > 0) {
    const f = this.files[0];
    document.getElementById('fileName').textContent = '📄 ' + f.name;
    document.getElementById('fileSize').textContent = 'Tamaño: ' + (f.size / 1024).toFixed(1) + ' KB';
    fileInfo.style.display = 'block';
    uploadBtn.disabled = false;
    showStatus('Archivo listo. Haz clic en "Iniciar Actualización" para continuar.', 'info');
  }
});

dropZone.addEventListener('dragover', function(e) {
  e.preventDefault();
  this.classList.add('dragover');
});
dropZone.addEventListener('dragleave', function() {
  this.classList.remove('dragover');
});
dropZone.addEventListener('drop', function(e) {
  e.preventDefault();
  this.classList.remove('dragover');
  const files = e.dataTransfer.files;
  if (files.length > 0 && files[0].name.endsWith('.bin')) {
    firmware.files = files;
    const f = files[0];
    document.getElementById('fileName').textContent = '📄 ' + f.name;
    document.getElementById('fileSize').textContent = 'Tamaño: ' + (f.size / 1024).toFixed(1) + ' KB';
    fileInfo.style.display = 'block';
    uploadBtn.disabled = false;
    showStatus('Archivo listo. Haz clic en "Iniciar Actualización" para continuar.', 'info');
  } else {
    showStatus('Error: Solo se aceptan archivos .bin de firmware ESP32.', 'error');
  }
});

function showStatus(msg, type) {
  statusMsg.textContent = msg;
  statusMsg.className = 'status-msg ' + type;
  statusMsg.style.display = 'block';
}

function startUpload() {
  const file = firmware.files[0];
  if (!file) { showStatus('Selecciona un archivo primero.', 'error'); return; }

  uploadBtn.disabled = true;
  progressWrap.style.display = 'block';
  showStatus('⏳ Subiendo firmware, por favor espera...', 'info');

  const formData = new FormData();
  formData.append('firmware', file);

  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);

  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      progressFill.style.width = pct + '%';
      progressText.textContent = 'Subiendo... ' + pct + '%';
    }
  };

  xhr.onload = function() {
    if (xhr.status === 200) {
      progressFill.style.width = '100%';
      progressText.textContent = '✅ Completado';
      showStatus('✅ Firmware actualizado correctamente. El dispositivo se reiniciará en 3 segundos...', 'success');
      setTimeout(function() { window.location = '/'; }, 5000);
    } else {
      showStatus('❌ Error al actualizar: ' + xhr.responseText, 'error');
      uploadBtn.disabled = false;
    }
  };

  xhr.onerror = function() {
    showStatus('❌ Error de conexión durante la actualización.', 'error');
    uploadBtn.disabled = false;
  };

  xhr.send(formData);
}

function rebootDevice() {
  if (confirm('¿Confirmas que deseas reiniciar el dispositivo?')) {
    showStatus('🔄 Enviando comando de reinicio...', 'info');
    fetch('/reboot', { method: 'POST' })
      .then(function() {
        showStatus('✅ Reiniciando... Serás redirigido al Dashboard en 8 segundos.', 'success');
        setTimeout(function() { window.location = '/'; }, 8000);
      })
      .catch(function() {
        showStatus('✅ Reiniciando... (sin respuesta esperada)', 'success');
        setTimeout(function() { window.location = '/'; }, 8000);
      });
  }
}
</script>
</body>
</html>)rawliteral");
    return page;
}

// ============================================
// JSON API
// ============================================
String getJsonData() {
    StaticJsonDocument<512> doc;
    
    JsonObject pzem = doc.createNestedObject("pzem");
    pzem["voltage"]   = pzemData.voltage;
    pzem["current"]   = pzemData.current;
    pzem["power"]     = pzemData.power;
    pzem["energy"]    = pzemData.energy;
    pzem["frequency"] = pzemData.frequency;
    pzem["pf"]        = pzemData.pf;
    pzem["valid"]     = pzemData.isValid;
    
    JsonObject ats = doc.createNestedObject("ats");
    ats["state"]       = atsGetStateName(atsState);
    ats["timeInState"] = atsGetTimeInState();
    ats["changes"]     = historyCount;
    ats["utilPct"]     = atsGetPercentageInState(ATS_UTILITY_POWER);
    ats["genPct"]      = atsGetPercentageInState(ATS_GENERATOR_POWER);
    
    JsonObject sys = doc.createNestedObject("system");
    sys["uptime"]    = millis() / 1000;
    sys["freeHeap"]  = ESP.getFreeHeap();
    sys["rssi"]      = WiFi.RSSI();
    sys["ip"]        = WiFi.localIP().toString();
    sys["version"]   = FIRMWARE_VERSION;
    
    String output;
    serializeJson(doc, output);
    return output;
}

// ============================================
// WEB SERVER ROUTES
// ============================================
#include "device_manager.h"
#include "device_pages.h"

void webServerSetup() {
    // Main dashboard
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", getMainPage());
    });
    
    // ATS history page
    server.on("/history", HTTP_GET, []() {
        String filter = server.hasArg("filter") ? server.arg("filter") : "day";
        server.send(200, "text/html", getHistoryPage(filter));
    });
    
    // Exportar historial ATS a CSV
    server.on("/history.csv", HTTP_GET, []() {
        String csv = "numero,estado,duracion_seg,fecha_hora\n";
        for (int i = 0; i < historyCount; i++) {
            int idx = (historyCount < MAX_HISTORY_ENTRIES) ? i : (historyIndex + i) % MAX_HISTORY_ENTRIES;
            csv += String(i + 1) + ",";
            csv += (atsHistory[idx].state == ATS_UTILITY_POWER) ? "RED" : "GENERADOR";
            csv += ",";
            csv += String(atsHistory[idx].duration);
            csv += ",";
            csv += formatRealTimestamp(atsHistory[idx].timestamp);
            csv += "\n";
        }
        server.sendHeader("Content-Disposition", "attachment; filename=historico_ats.csv");
        server.send(200, "text/csv", csv);
    });
    
    // OTA update page
    server.on("/ota", HTTP_GET, []() {
        server.send(200, "text/html", getOTAPage());
    });
    
    // Administrador de dispositivos (Tasmota/OpenBeken) + standby de pantalla
    deviceManagerWebBegin();
    
    // JSON API endpoint
    server.on("/api/data", HTTP_GET, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json", getJsonData());
    });
    
    // ATS history JSON
    server.on("/api/history", HTTP_GET, []() {
        StaticJsonDocument<4096> doc;
        JsonArray arr = doc.createNestedArray("history");
        for (int i = 0; i < historyCount; i++) {
            JsonObject entry = arr.createNestedObject();
            entry["timestamp"] = atsHistory[i].timestamp;
            entry["state"]     = atsGetStateName(atsHistory[i].state);
            entry["duration"]  = atsHistory[i].duration;
        }
        doc["count"]   = historyCount;
        doc["current"] = atsGetStateName(atsState);
        String output;
        serializeJson(doc, output);
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json", output);
    });

    // Reboot endpoint
    server.on("/reboot", HTTP_POST, []() {
        server.send(200, "text/plain", "Rebooting...");
        delay(500);
        ESP.restart();
    });
    
    // OTA firmware upload handler
    server.on("/update", HTTP_POST,
        // Response after upload completes
        []() {
            bool success = !Update.hasError();
            server.sendHeader("Connection", "close");
            server.send(success ? 200 : 500,
                        "text/plain",
                        success ? "OK" : Update.errorString());
            if (success) {
                delay(500);
                ESP.restart();
            }
        },
        // Upload handler (called for each chunk)
        []() {
            HTTPUpload& upload = server.upload();
            
            if (upload.status == UPLOAD_FILE_START) {
                Serial.printf("OTA Start: %s\n", upload.filename.c_str());
                setLED(false, false, true);  // Blue = uploading
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
                // Show progress on TFT
                if (displayInitialized) {
                    unsigned long pct = (upload.totalSize > 0)
                        ? (upload.currentSize * 100 / upload.totalSize) : 0;
                    tft.fillRect(10, 110, 300, 20, 0x18C3);
                    tft.fillRect(10, 110, (300 * pct) / 100, 20, COLOR_CYAN);
                    tft.setTextColor(COLOR_WHITE);
                    tft.setTextSize(1);
                    tft.setCursor(10, 113);
                    tft.printf("OTA: %lu%%  %lu bytes", pct, upload.totalSize);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    Serial.printf("OTA Success: %u bytes\n", upload.totalSize);
                    setLED(false, true, false);  // Green = success
                    if (displayInitialized) {
                        tft.fillScreen(COLOR_BG);
                        tft.setTextColor(COLOR_GREEN);
                        tft.setTextSize(2);
                        tft.setCursor(20, 80);
                        tft.print("OTA exitoso!");
                        tft.setTextColor(COLOR_WHITE);
                        tft.setTextSize(1);
                        tft.setCursor(20, 110);
                        tft.print("Reiniciando...");
                    }
                } else {
                    Update.printError(Serial);
                    setLED(true, false, false);  // Red = error
                }
            }
        }
    );
    
    // 404 handler
    server.onNotFound([]() {
        server.send(404, "text/html",
            "<html><body style='background:#0a0e17;color:#e5e7eb;font-family:sans-serif;"
            "display:flex;align-items:center;justify-content:center;height:100vh;margin:0'>"
            "<div style='text-align:center'><h1 style='color:#00d4ff;font-size:4em'>404</h1>"
            "<p>Página no encontrada</p>"
            "<a href='/' style='color:#00d4ff'>← Volver al Dashboard</a>"
            "</div></body></html>");
    });
    
    server.begin();
    Serial.println("Web server started on port " + String(WEB_SERVER_PORT));
}

// ============================================
// WIFI SETUP
// ============================================
bool wifiSetup() {
    String hostname = String(HOSTNAME_PREFIX) + "-" + WiFi.macAddress().substring(12);
    hostname.replace(":", "");
    WiFi.setHostname(hostname.c_str());
    
    wm.setConfigPortalTimeout(WIFI_MANAGER_TIMEOUT);
    wm.setAPCallback([](WiFiManager* wm) {
        Serial.println("WiFi portal started");
        displayWiFiPortalInfo();
    });
    
    Serial.println("Connecting to WiFi...");
    if (!wm.autoConnect(hostname.c_str())) {
        Serial.println("WiFi connection failed, restarting...");
        delay(3000);
        ESP.restart();
        return false;
    }
    
    Serial.println("WiFi connected!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    wifiStatusStr = "WiFi: " + WiFi.localIP().toString();
    return true;
}

// ============================================
// SETUP
// ============================================
void setup() {
    Serial.begin(DEBUG_BAUD_RATE);
    Serial.println("\n==============================");
    Serial.println("CYD Power Monitor v" + String(FIRMWARE_VERSION));
    Serial.println("==============================");
    
    // GPIO init
    pinMode(LED_RED_PIN,   OUTPUT);
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_BLUE_PIN,  OUTPUT);
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    setLED(true, false, false);  // Red = booting
    
    // Display
    displayBegin();
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_CYAN);
    tft.setTextSize(2);
    tft.setCursor(10, 80);
    tft.print("Iniciando...");
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 110);
    tft.print("CYD Power Monitor v" + String(FIRMWARE_VERSION));
    
    // Almacenamiento persistente (historial ATS)
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS: fallo al montar/formatear");
    } else {
        historyLoadFromFlash();
    }
    
    // ATS
    atsBegin();
    
    // PZEM
    pzemInitialized = pzemBegin();
    pzemStatusStr = pzemGetStatusString();
    // Tarea dedicada en Core 0 para no bloquear la pantalla ni el webserver
    pzemTaskBegin();

    // WiFi
    setLED(false, false, true);  // Blue = connecting
    wifiSetup();
    
    // Administrador de dispositivos
    Serial.println("Loading device manager...");
    devicesLoad();
    utcOffsetLoad();
    backlightBegin();
    devicesMutexInit();
    deviceManagerTaskBegin();
    if (WiFi.status() == WL_CONNECTED) {
        ntpTimeBegin();
    }
    
    // Web server
    webServerSetup();
    
    setLED(false, true, false);  // Green = ready
    
    // Show IP on display briefly
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_GREEN);
    tft.setTextSize(2);
    tft.setCursor(10, 60);
    tft.print("Listo!");
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 90);
    tft.print("IP: " + WiFi.localIP().toString());
    tft.setCursor(10, 110);
    tft.print("Dashboard: http://" + WiFi.localIP().toString());
    delay(2000);
    
    Serial.println("Setup complete. Dashboard: http://" + WiFi.localIP().toString());
}

// ============================================
// LOOP
// ============================================
void loop() {
    unsigned long now = millis();
    
    // Handle web requests
    server.handleClient();
    
    // Check boot button for WiFi reset
    checkBootButton();
    
    // La lectura del PZEM corre en su propia tarea en Core 0 (ver pzemTaskBegin
    // en setup). Aqui solo actualizamos el LED de estado con throttling para
    // no flicker y para no hacer un digitalWrite en cada iteracion del loop.

    // Update ATS state
    atsUpdate();
    
    // Brillo automatico de pantalla segun luz ambiental (LDR)
    if (now - brightnessLastCheck >= 800) {
        brightnessLastCheck = now;
        updateAutoBrightness();
    }
    
    // NOTA: la automatizacion ATS, horarios y sondeo de dispositivos ya NO se
    // llaman aqui - corren en su propia tarea en el nucleo 0 (ver
    // deviceManagerTaskBegin() en setup()), para que nunca bloqueen la
    // pantalla ni el servidor web de este nucleo.
    
    // Update LED based on ATS state (throttled a 500ms para no martillear
    // los pines y para que los cambios de estado sean visibles a ojos)
    static unsigned long ledLastUpdate = 0;
    static int lastLedCode = -1;
    if (now - ledLastUpdate >= 500) {
        ledLastUpdate = now;
        int code = 0; // 0=utility, 1=generator, 2=unknown/error
        if (!pzemData.isValid) {
            code = 2;
        } else {
            switch (atsState) {
                case ATS_UTILITY_POWER:   code = 0; break;
                case ATS_GENERATOR_POWER: code = 1; break;
                default:                  code = 2; break;
            }
        }
        if (code != lastLedCode) {
            lastLedCode = code;
            if (code == 0)      setLED(false, true, false);   // Green
            else if (code == 1) setLED(false, false, true);   // Blue
            else                setLED(true, false, false);   // Red
        }
    }
    
    // WiFi watchdog
    if (now - wifiReconnectCheck >= 30000) {
        wifiReconnectCheck = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi lost, reconnecting...");
            wifiStatusStr = "WiFi: Reconectando...";
            WiFi.reconnect();
        } else {
            wifiStatusStr = WiFi.localIP().toString();
            if (!ntpSynced) ntpTimeBegin();
        }
    }
    
    // Refresh TFT display
    displayUpdate();
    
    // Small yield to keep WiFi stack happy
    yield();
}

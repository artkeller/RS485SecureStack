#include "RS485SecureStack.h"
#include "credantials.h" // Pre-Shared Master Key (MUSS AUF ALLEN DEVICES IDENTISCH SEIN!)
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h> // <-- Wichtig: ST7789 für LilyGo T-Display S3!

// --- RS485 Konfiguration ---
// Für PoC mit Default UART1, da sonst Konflikt mit USB-Serial besteht:
HardwareSerial& rs485Serial = Serial1; 
// ACHTUNG: Wenn Serial auch für USB-Debugausgabe genutzt wird, können Kollisionen auftreten.
// Eine separate UART (Serial1/Serial2) für RS485 ist empfohlen für stabile Kommunikation.

// Monitor-Adresse: sollte eine ungenutzte Adresse sein, die nicht in der Adressliste der aktiven Nodes ist.
// Der Monitor sendet selbst keine aktiven Nachrichten mit dieser Adresse, sondern lauscht passiv.
// Er kann aber ACKs/NACKs senden, die dann von seiner Adresse kommen.
const byte MY_ADDRESS = 254; // Eine hohe, ungenutzte Adresse

RS485SecureStack rs485Stack(rs485Serial, MY_ADDRESS, MASTER_KEY);

// --- TFT Konfiguration für LilyGo T-Display S3 (ST7789 170x320) ---
// Bitte die exakten Pins für dein Board prüfen, dies sind TYPISCHE Werte für T-Display S3
// und können variieren!
#define TFT_CS    -1 // Chip Select (oft intern verdrahtet, -1 bedeutet nicht verwendet)
#define TFT_DC    7  // Data/Command (oft GPIO 7 auf S3)
#define TFT_RST   6  // Reset (oft GPIO 6 auf S3, oder -1 wenn intern verbunden)
#define TFT_MOSI  11 // SPI MOSI (typisch)
#define TFT_SCLK  12 // SPI SCLK (typisch)
#define TFT_MISO  13 // SPI MISO (typisch, aber nicht für Display benötigt)

// Use hardware SPI
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST); // Mit expliziten SPI-Pins

// --- Überwachungsstufen ---
enum MonitorMode {
    MODE_SIMPLE_DASHBOARD,
    MODE_TRAFFIC_ANALYSIS,
    MODE_DEBUG_TRACE
};
MonitorMode currentMonitorMode = MODE_SIMPLE_DASHBOARD; // Start im Dashboard-Modus

// --- Metriken für die Überwachung ---
volatile unsigned long totalPacketsReceived = 0; // Packets per second (reset every second)
volatile unsigned long totalChecksumErrors = 0;
volatile unsigned long totalHmacErrors = 0;
volatile unsigned long packetsPerSecond = 0;
volatile unsigned long bytesPerSecond = 0; // Summe der entschlüsselten Payload-Längen pro Sekunde

unsigned long lastMetricsUpdateTime = 0;
const unsigned long METRICS_UPDATE_INTERVAL_MS = 1000; // Update every second

// Master Heartbeat Monitoring
unsigned long lastMasterHeartbeatRxTime = 0;
const unsigned long MASTER_HEARTBEAT_TIMEOUT_MS = 1000; // Timeout: Master als offline betrachten nach 1 Sekunde ohne Heartbeat
bool masterIsPresent = false; // Flag to indicate if master is currently active

// Master-Kollisionserkennung (falls Monitor unerwarteten Master-Heartbeat empfängt)
bool unexpectedMasterHeartbeat = false;
byte unexpectedMasterAddress = 0;

// Callback-Funktion für empfangene Pakete
void onPacketReceived(byte senderAddr, char msgType, const String& payload);

// Hilfsfunktionen für TFT-Ausgabe
void drawDashboard();
void drawTrafficAnalysis();
void clearTFT();

void setup() {
  Serial.begin(115200); // USB-Serial für Debugging und Debug-Trace
  rs485Serial.begin(9600); // RS485-Serial, startet mit Standard-Baudrate

  Serial.println("Bus Monitor ESP32-C3 (LilyGo T-Display S3) gestartet.");
  
  rs485Stack.begin(9600); // RS485-Stack initialisieren
  rs485Stack.registerReceiveCallback(onPacketReceived);
  rs485Stack.setAckEnabled(true); // Wichtig: Monitor sollte ACKs senden, um Bus nicht zu stören
  rs485Stack.setCurrentKeyId(0); // Start mit KeyID 0, wird vom Scheduler aktualisiert

  // --- TFT Setup für ST7789 ---
  tft.init(170, 320); // Initialisiere mit Breite 170, Höhe 320 (im Hochformat)
  tft.setRotation(1); // Rotation 1 oder 3 für Querformat (320x170 Pixel)
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false); // Kein automatischer Zeilenumbruch
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.print("Monitor Ready...");
  delay(1000);
  clearTFT();

  lastMasterHeartbeatRxTime = millis(); // Assume master is present at startup for initial state
  masterIsPresent = true;
}

void loop() {
  rs485Stack.processIncoming(); // Pakete empfangen und verarbeiten

  // --- Master-Anwesenheit prüfen ---
  if (millis() - lastMasterHeartbeatRxTime > MASTER_HEARTBEAT_TIMEOUT_MS) {
    if (masterIsPresent) {
      Serial.println("!!! WARNING: MASTER HEARTBEAT TIMEOUT! Master is GONE or SILENT !!!");
      masterIsPresent = false;
    }
  } else {
    if (!masterIsPresent) {
      Serial.println("!!! MASTER HEARTBEAT RESTORED! !!!");
      masterIsPresent = true;
    }
  }

  // --- Metriken aktualisieren und Ausgaben steuern ---
  if (millis() - lastMetricsUpdateTime > METRICS_UPDATE_INTERVAL_MS) {
    packetsPerSecond = totalPacketsReceived; // Since we reset totalPacketsReceived every second
    // bytesPerSecond is already summed in onPacketReceived
    totalPacketsReceived = 0; // Reset for next second's calculation
    bytesPerSecond = 0; // Reset for next second's calculation
    lastMetricsUpdateTime = millis();

    // Ausgabe auf TFT und Serial je nach Modus
    switch (currentMonitorMode) {
      case MODE_SIMPLE_DASHBOARD:
        drawDashboard();
        break;
      case MODE_TRAFFIC_ANALYSIS:
        drawTrafficAnalysis();
        break;
      case MODE_DEBUG_TRACE:
        // Debug Trace ist nur seriell, wird direkt in onPacketReceived ausgegeben
        break;
    }
  }

  // --- Moduswechsel (Beispiel: Serielle Eingabe) ---
  // Beispiel: 's' für Simple, 't' für Traffic, 'd' für Debug
  if (Serial.available()) {
      char cmd = Serial.read();
      if (cmd == 's') {
          currentMonitorMode = MODE_SIMPLE_DASHBOARD;
          Serial.println("Monitor Mode: Simple Dashboard");
          clearTFT();
      } else if (cmd == 't') {
          currentMonitorMode = MODE_TRAFFIC_ANALYSIS;
          Serial.println("Monitor Mode: Traffic Analysis");
          clearTFT();
      } else if (cmd == 'd') {
          currentMonitorMode = MODE_DEBUG_TRACE;
          Serial.println("Monitor Mode: Debug Trace");
          clearTFT(); // Clear TFT as debug is Serial only
      }
  }
}

// Callback-Funktion, wird von RS485SecureStack aufgerufen
void onPacketReceived(byte senderAddr, char msgType, const String& payload) {
  // Erhöhen der empfangenen Pakete und Bytes für Metriken
  totalPacketsReceived++;
  bytesPerSecond += payload.length(); // Summiert die Länge der entschlüsselten Payload

  // --- Master-Heartbeat verarbeiten ---
  if (msgType == RS485SecureStack::MSG_TYPE_MASTER_HEARTBEAT) {
      if (senderAddr == 0) { // Offizieller Master
          lastMasterHeartbeatRxTime = millis();
      } else { // Heartbeat von einer unerwarteten Adresse
          if (!unexpectedMasterHeartbeat) { // Log only once until cleared
              Serial.print("!!! Monitor: Detected Master Heartbeat from UNEXPECTED ADDRESS: "); Serial.println(senderAddr);
          }
          unexpectedMasterHeartbeat = true;
          unexpectedMasterAddress = senderAddr;
      }
      rs485Stack.sendAckNack(senderAddr, msgType, true); // ACK, um den Master zu beruhigen (falls er auf ACKs wartet)
      return; // Heartbeat-Pakete werden sonst nicht weiter verarbeitet
  } else {
      unexpectedMasterHeartbeat = false; // Clear flag if no rogue heartbeat for a non-heartbeat packet
  }

  // --- Baudraten-Anpassung durch den Monitor ---
  if (senderAddr == 0) { // Nur vom Scheduler
      if (msgType == RS485SecureStack::MSG_TYPE_BAUD_RATE_SET) {
          long newBaudRate = payload.toLong();
          Serial.print("Monitor: Baud Rate Set Request from Scheduler for: "); Serial.println(newBaudRate);
          rs485Serial.begin(newBaudRate); // HardwareSerial wechseln
          rs485Stack._setBaudRate(newBaudRate); // Stack informieren
          rs485Stack.sendAckNack(senderAddr, msgType, true);
          return;
      } else if (msgType == RS485SecureStack::MSG_TYPE_KEY_UPDATE) {
          uint16_t newKeyId = payload.toInt();
          Serial.print("Monitor: Key update from Scheduler to KeyID: "); Serial.println(newKeyId);
          rs485Stack.setCurrentKeyId(newKeyId);
          rs485Stack.sendAckNack(senderAddr, msgType, true);
          return;
      }
  }

  // --- Spezifische Logik für Debug-Modus (nur Serial) ---
  if (currentMonitorMode == MODE_DEBUG_TRACE) {
    Serial.print("TRACE ["); Serial.print(millis()); Serial.print("ms] ");
    Serial.print("FROM: "); Serial.print(senderAddr);
    Serial.print(" TO: "); Serial.print(rs485Stack._currentPacketTarget);
    Serial.print(" TYPE: '"); Serial.print(msgType); Serial.print("'");
    Serial.print(" KEY_ID: "); Serial.print(rs485Stack._currentSessionKeyId); // Stack's current active key ID
    Serial.print(" IV: ");
    for (int i = 0; i < 16; i++) { // Assuming IV_SIZE is 16
        if (rs485Stack._currentPacketIV[i] < 0x10) Serial.print("0");
        Serial.print(rs485Stack._currentPacketIV[i], HEX);
    }
    Serial.print(" PAYLOAD_LEN: "); Serial.print(payload.length());
    Serial.print(" PAYLOAD: '"); Serial.print(payload); Serial.print("'");
    Serial.print(" HMAC_OK: "); Serial.print(rs485Stack._hmacVerified ? "YES" : "NO");
    Serial.print(" CHKSUM_OK: "); Serial.println(rs485Stack._checksumVerified ? "YES" : "NO");
  }

  // Für die Stufen 1 und 2 werden Metriken gesammelt (bereits oben getan)
  // und die Anzeige in der loop() aktualisiert.
  // Hier nur das ACK/NACK an den Absender, um Bus-Interaktion zu simulieren
  if (msgType != RS485SecureStack::MSG_TYPE_ACK && msgType != RS485SecureStack::MSG_TYPE_NACK) {
      rs485Stack.sendAckNack(senderAddr, msgType, true); // Immer ACK senden, da Monitor passiv ist
  }
}

// --- TFT Hilfsfunktionen ---
void clearTFT() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(0, 0);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
}

void drawDashboard() {
    clearTFT();
    int y_pos = 0; // Aktuelle Y-Position für den Cursor

    tft.setTextSize(2); 
    tft.setCursor(0, y_pos);
    tft.setTextColor(ST77XX_YELLOW);
    tft.println("--- BUS MONITOR ---");
    y_pos += 16; // 2 * 8 (Textgröße * Zeichenhöhe)
    tft.setCursor(0, y_pos);
    tft.println("   (Dashboard)   ");
    y_pos += 20; // Etwas mehr Abstand

    tft.setTextSize(2);
    tft.setCursor(0, y_pos);
    tft.setTextColor(masterIsPresent ? ST77XX_GREEN : ST77XX_RED);
    tft.print("MASTER: "); tft.println(masterIsPresent ? "ONLINE" : "OFFLINE!");
    y_pos += 16;

    tft.setCursor(0, y_pos);
    if (unexpectedMasterHeartbeat) {
        tft.setTextColor(ST77XX_MAGENTA); 
        tft.print("ROGUE: "); tft.println(unexpectedMasterAddress);
    } else {
        tft.setTextColor(ST77XX_GREEN);
        tft.println("ROGUE: NONE");
    }
    y_pos += 16;

    tft.setCursor(0, y_pos);
    tft.setTextColor(ST77XX_CYAN);
    tft.print("BAUD: "); tft.print(rs485Serial.baudRate()); tft.println(" bps");
    y_pos += 16;

    tft.setCursor(0, y_pos);
    tft.setTextColor(ST77XX_WHITE);
    tft.print("KEY ID: "); tft.println(rs485Stack._currentSessionKeyId); 
    y_pos += 16;
    tft.println(""); // Leere Zeile
    y_pos += 8;

    tft.setCursor(0, y_pos);
    tft.print("RX PKTS/s: "); tft.println(packetsPerSecond); 
    y_pos += 16;
    tft.print("RX BYTES/s: "); tft.println(bytesPerSecond); // Zeigt Bytes/Sekunde der Plain-Payload
    y_pos += 16;

    tft.print("HMAC ERR: "); tft.println(totalHmacErrors);
    y_pos += 16;
    tft.print("CHKSM ERR: "); tft.println(totalChecksumErrors);
    y_pos += 16;

    // Fußzeile für Hinweise
    tft.setTextSize(1);
    tft.setCursor(0, tft.height() - 16); // Unten platzieren
    tft.setTextColor(ST77XX_DARKGREY);
    tft.println("Mode: 's' Simple, 't' Traffic, 'd' Debug");
}

void drawTrafficAnalysis() {
    clearTFT();
    int y_pos = 0;

    tft.setTextSize(2);
    tft.setCursor(0, y_pos);
    tft.setTextColor(ST77XX_GREEN);
    tft.println("--- TRAFFIC STATS ---");
    y_pos += 16;
    tft.setCursor(0, y_pos);
    tft.println("  (Analysis Mode)  ");
    y_pos += 20;

    tft.setTextSize(2); // Wichtige Metriken größer
    tft.setCursor(0, y_pos);
    tft.setTextColor(ST77XX_WHITE);
    tft.print("PKTS/s: "); tft.println(packetsPerSecond);
    y_pos += 16;
    tft.print("BYTES/s: "); tft.println(bytesPerSecond);
    y_pos += 16;

    // Hier könnte man detailliertere Statistiken pro Node anzeigen,
    // Dafür müsste man eine Map oder Array von Node-Statistiken pflegen.
    // Beispiel (nur Platzhalter):
    tft.setTextSize(1);
    tft.setCursor(0, y_pos); tft.println("Node Stats (Top 3):"); y_pos += 8;
    tft.setCursor(0, y_pos); tft.println("  ID 1: 5 Pkt, 120B"); y_pos += 8;
    tft.setCursor(0, y_pos); tft.println("  ID 2: 3 Pkt, 80B"); y_pos += 8;
    tft.setCursor(0, y_pos); tft.println("  ID 11: 2 Pkt, 40B"); y_pos += 8;
    
    y_pos += 10; // Extra Abstand

    tft.setTextSize(2);
    tft.setCursor(0, y_pos);
    tft.setTextColor(ST77XX_YELLOW);
    tft.println("NET QLTY: GOOD"); // Platzhalter für tatsächliche Bewertung
    y_pos += 16;
    tft.print("MASTER OPT: OK");  // Platzhalter für tatsächliche Bewertung
    y_pos += 16;

    // Fußzeile
    tft.setTextSize(1);
    tft.setCursor(0, tft.height() - 16);
    tft.setTextColor(ST77XX_DARKGREY);
    tft.println("Mode: 's' Simple, 'd' Debug");
}
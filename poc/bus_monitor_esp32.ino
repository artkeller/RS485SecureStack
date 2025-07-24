#include "RS485SecureStack.h"
#include "credantials.h" // Pre-Shared Master Key (MUSS AUF ALLEN DEVICES IDENTISCH SEIN!)
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h> // <-- Wichtig: ST7789 für LilyGo T-Display S3! 

// --- RS485 Konfiguration ---
HardwareSerial& rs485Serial = Serial1; [cite: 4]
// Monitor-Adresse: sollte eine ungenutzte Adresse sein
const byte MY_ADDRESS = 254; // Eine hohe, ungenutzte Adresse 

RS485SecureStack rs485Stack(rs485Serial, MY_ADDRESS, MASTER_KEY); [cite: 4]

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
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST); [cite: 4]

// --- Überwachungsstufen ---
enum MonitorMode {
    MODE_SIMPLE_DASHBOARD,
    MODE_TRAFFIC_ANALYSIS,
    MODE_DEBUG_TRACE
}; [cite: 4]
MonitorMode currentMonitorMode = MODE_SIMPLE_DASHBOARD; // Start im Dashboard-Modus 

// --- Metriken für die Überwachung ---
volatile unsigned long totalPacketsReceived = 0; // Packets per second (reset every second) 
volatile unsigned long totalChecksumErrors = 0; [cite: 4]
volatile unsigned long totalHmacErrors = 0; [cite: 4]
volatile unsigned long packetsPerSecond = 0; [cite: 4]
volatile unsigned long bytesPerSecond = 0; // Summe der entschlüsselten Payload-Längen pro Sekunde 

unsigned long lastMetricsUpdateTime = 0;
const unsigned long METRICS_UPDATE_INTERVAL_MS = 1000; // Update every second 

// Master Heartbeat Monitoring
unsigned long lastMasterHeartbeatRxTime = 0;
const unsigned long MASTER_HEARTBEAT_TIMEOUT_MS = 1000; // Timeout: Master als offline betrachten nach 1 Sekunde ohne Heartbeat 
bool masterIsPresent = false; // Flag to indicate if master is currently active 

// Master-Kollisionserkennung (falls Monitor unerwarteten Master-Heartbeat empfängt)
bool unexpectedMasterHeartbeat = false; [cite: 4]
byte unexpectedMasterAddress = 0;

// Callback-Funktion für empfangene Pakete
void onPacketReceived(byte senderAddr, char msgType, const String& payload); [cite: 4]
// Hilfsfunktionen für TFT-Ausgabe
void drawDashboard(); [cite: 4]
void drawTrafficAnalysis(); [cite: 4]
void clearTFT(); [cite: 4]

// NEU: Utility-Funktion zur Konvertierung eines Hex-Strings in ein Byte-Array
// Gibt die Anzahl der in den Buffer geschriebenen Bytes zurück, oder 0 bei Fehler.
size_t hexStringToBytes(const String& hexString, byte* buffer, size_t bufferSize) {
    if (hexString.length() % 2 != 0) return 0; // Muss eine gerade Länge haben
    size_t byteLength = hexString.length() / 2;
    if (byteLength > bufferSize) return 0; // Buffer zu klein

    for (size_t i = 0; i < byteLength; i++) {
        char hexPair[3]; // Zwei Zeichen + Null-Terminator
        hexString.substring(i * 2, i * 2 + 2).toCharArray(hexPair, 3);
        buffer[i] = (byte)strtol(hexPair, NULL, 16); // Konvertiere Hex in Byte
    }
    return byteLength;
}

void setup() {
  Serial.begin(115200); // USB-Serial für Debugging und Debug-Trace 
  rs485Serial.begin(9600); // RS485-Serial, startet mit Standard-Baudrate

  Serial.println("Bus Monitor ESP32-C3 (LilyGo T-Display S3) gestartet."); [cite: 4]
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
  tft.setTextColor(ST77XX_WHITE); [cite: 4]
  tft.setTextSize(1); [cite: 4]
  tft.print("Monitor Ready...");
  delay(1000);
  clearTFT();

  lastMasterHeartbeatRxTime = millis(); // Assume master is present at startup for initial state 
  masterIsPresent = true; [cite: 4]
}

void loop() {
  rs485Stack.processIncoming(); // Pakete empfangen und verarbeiten 

  // --- Master-Anwesenheit prüfen ---
  if (millis() - lastMasterHeartbeatRxTime > MASTER_HEARTBEAT_TIMEOUT_MS) {
    if (masterIsPresent) {
      Serial.println("!!! WARNING: MASTER HEARTBEAT TIMEOUT! Master is GONE or SILENT !!!"); [cite: 4]
      masterIsPresent = false;
    }
  } else {
    if (!masterIsPresent) {
      Serial.println("!!! MASTER HEARTBEAT RESTORED! !!!"); [cite: 4]
      masterIsPresent = true;
    }
  }

  // --- Metriken aktualisieren und Ausgaben steuern ---
  if (millis() - lastMetricsUpdateTime > METRICS_UPDATE_INTERVAL_MS) {
    packetsPerSecond = totalPacketsReceived; // Since we reset totalPacketsReceived every second 
    bytesPerSecond = 0; // Summe der entschlüsselten Payload-Längen pro Sekunde
    totalPacketsReceived = 0; // Reset for next second's calculation 
    bytesPerSecond = 0; // Reset for next second's calculation 
    lastMetricsUpdateTime = millis(); [cite: 4]
    // Ausgabe auf TFT und Serial je nach Modus
    switch (currentMonitorMode) {
      case MODE_SIMPLE_DASHBOARD:
        drawDashboard(); [cite: 4]
        break;
      case MODE_TRAFFIC_ANALYSIS:
        drawTrafficAnalysis(); [cite: 4]
        break;
      case MODE_DEBUG_TRACE:
        // Debug Trace ist nur seriell, wird direkt in onPacketReceived ausgegeben 
        break;
    }
  }

  // --- Moduswechsel (Beispiel: Serielle Eingabe) ---
  // Beispiel: 's' für Simple, 't' für Traffic, 'd' für Debug 
  if (Serial.available()) {
      char cmd = Serial.read(); [cite: 4]
      if (cmd == 's') {
          currentMonitorMode = MODE_SIMPLE_DASHBOARD; [cite: 4]
          Serial.println("Monitor Mode: Simple Dashboard"); [cite: 4]
          clearTFT();
      } else if (cmd == 't') {
          currentMonitorMode = MODE_TRAFFIC_ANALYSIS; [cite: 4]
          Serial.println("Monitor Mode: Traffic Analysis"); [cite: 4]
          clearTFT();
      } else if (cmd == 'd') {
          currentMonitorMode = MODE_DEBUG_TRACE; [cite: 4]
          Serial.println("Monitor Mode: Debug Trace"); [cite: 4]
          clearTFT(); // Clear TFT as debug is Serial only 
      }
  }
}

// Callback-Funktion, wird von RS485SecureStack aufgerufen
void onPacketReceived(byte senderAddr, char msgType, const String& payload) {
  // Erhöhen der empfangenen Pakete und Bytes für Metriken
  totalPacketsReceived++; [cite: 4]
  bytesPerSecond += payload.length(); // Summiert die Länge der entschlüsselten Payload 

  // --- Master-Heartbeat verarbeiten ---
  if (msgType == RS485SecureStack::MSG_TYPE_MASTER_HEARTBEAT) {
      if (senderAddr == 0) { // Offizieller Master
          lastMasterHeartbeatRxTime = millis(); [cite: 4]
      } else { // Heartbeat von einer unerwarteten Adresse
          if (!unexpectedMasterHeartbeat) { // Log only once until cleared
              Serial.print("!!! Monitor: Detected Master Heartbeat from UNEXPECTED ADDRESS: "); [cite: 4]
              Serial.println(senderAddr);
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
          Serial.print("Monitor: Baud Rate Set Request from Scheduler for: "); [cite: 4]
          Serial.println(newBaudRate);
          rs485Serial.begin(newBaudRate); // HardwareSerial wechseln 
          rs485Stack._setBaudRate(newBaudRate); // Stack informieren 
          rs485Stack.sendAckNack(senderAddr, msgType, true);
          return;
      }
      // --- NEU: Erweiterte Schlüssel-Update-Verarbeitung für den Monitor ---
      // Erwartetes Format: "KEY_UPDATE:<keyId_dezimal>:<hexKeyString>"
      else if (msgType == RS485SecureStack::MSG_TYPE_KEY_UPDATE) {
          Serial.println("Monitor: Received Key Update message from Scheduler.");
          int firstColon = payload.indexOf(':');
          int secondColon = payload.indexOf(':', firstColon + 1);

          if (firstColon != -1 && secondColon != -1) {
              String prefix = payload.substring(0, firstColon);
              if (prefix == "KEY_UPDATE") {
                  uint16_t newKeyId = payload.substring(firstColon + 1, secondColon).toInt();
                  String hexKeyString = payload.substring(secondColon + 1);

                  byte newKey[AES_KEY_SIZE]; // Annahme: AES_KEY_SIZE ist 16
                  size_t bytesRead = hexStringToBytes(hexKeyString, newKey, AES_KEY_SIZE);

                  if (bytesRead == AES_KEY_SIZE) {
                      rs485Stack.setSessionKey(newKeyId, newKey); // Wichtig: Schlüssel im Pool speichern
                      rs485Stack.setCurrentKeyId(newKeyId);     // Dann den aktuellen Schlüssel setzen
                      Serial.print("Monitor: Updated to Key ID: ");
                      Serial.println(newKeyId);
                      rs485Stack.sendAckNack(senderAddr, msgType, true); // ACK senden
                      return; // Paket verarbeitet
                  } else {
                      Serial.println("Monitor: ERROR parsing hex key string from update message.");
                  }
              } else {
                 Serial.println("Monitor: ERROR: Key Update message has unexpected prefix.");
              }
          } else {
              Serial.println("Monitor: ERROR: Key Update message has unexpected format.");
          }
      }
  }

  // Debug-Trace für alle empfangenen Pakete (außer Heartbeats)
  if (currentMonitorMode == MODE_DEBUG_TRACE) {
    Serial.print("PKT: From ");
    Serial.print(senderAddr);
    Serial.print(", Type: ");
    Serial.print(msgType);
    Serial.print(", Payload_Len: ");
    Serial.print(payload.length());
    Serial.print(", Payload: '");
    Serial.print(payload);
    Serial.print("'");

    // Weitere Details aus dem Stack abrufen (z.B. Fehler-Flags)
    if (rs485Stack.getLastPacketHasChecksumError()) {
        Serial.print(" [Checksum Error]");
        totalChecksumErrors++;
    }
    if (rs485Stack.getLastPacketHasHmacError()) {
        Serial.print(" [HMAC Error]");
        totalHmacErrors++;
    }
    Serial.println();
  }
}

// Hilfsfunktion zur Anzeige des Dashboards auf dem TFT
void drawDashboard() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.println("RS485 Monitor");
  tft.setTextSize(1);
  tft.print("Master: ");
  tft.println(masterIsPresent ? "ONLINE" : "OFFLINE");
  tft.print("Pkts/s: ");
  tft.println(packetsPerSecond);
  tft.print("Bytes/s: ");
  tft.println(bytesPerSecond);
  tft.print("ChkErr: ");
  tft.println(totalChecksumErrors);
  tft.print("HMACErr: ");
  tft.println(totalHmacErrors);

  if (unexpectedMasterHeartbeat) {
      tft.setTextColor(ST77XX_RED);
      tft.print("ROGUE MASTER: ");
      tft.println(unexpectedMasterAddress);
  }
}

// Hilfsfunktion zur Anzeige der Verkehrsanalyse auf dem TFT
void drawTrafficAnalysis() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.println("Traffic Analysis");
  tft.setTextSize(1);
  tft.print("Total Pkts: ");
  tft.println(totalPacketsReceived); // This would show packets since startup if not reset
  tft.print("Current Pk/s: ");
  tft.println(packetsPerSecond);
  tft.print("Current By/s: ");
  tft.println(bytesPerSecond);
  // Hier könnten detailliertere Statistiken pro Adresse etc. angezeigt werden
}

// Hilfsfunktion zum Löschen des TFT-Displays
void clearTFT() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
}

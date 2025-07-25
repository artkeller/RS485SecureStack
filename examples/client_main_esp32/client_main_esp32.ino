#include <Arduino.h>
#include <HardwareSerial.h>
#include <string>
#include <StreamString.h>

// Lokale Bibliotheks-Includes
#include "RS485SecureStack.h"
#include "credentials.h" // Enthält MASTER_KEY

// WICHTIG: Wählen Sie EINE der folgenden Zeilen, je nach Ihrem RS485-Modul:
// Option 1: Für Module MIT einem DE/RE-Pin, der manuell gesteuert werden muss (z.B. einfache MAX485-Module)
// #include "ManualDE_REDirectionControl.h" 

// Option 2: Für Module OHNE externen DE/RE-Pin (mit automatischer Flussrichtung, z.B. bestimmte HW-159/HW-519)
#include "AutomaticDirectionControl.h" 

// ==============================================================================
// GLOBAL KONFIGURATION (Client-spezifisch)
// ==============================================================================
#define MY_ADDRESS 11 // ANPASSEN: Eindeutige Adresse für diesen Client (z.B. 11, 12, etc.)
#define SUBMASTER_ADDRESS 1 // ANPASSEN: Adresse des Submasters, dem dieser Client zugeordnet ist
#define INITIAL_KEY_ID 0 // Startet mit Key ID 0

#define SUBMASTER_POLL_TIMEOUT_MS 10000 // Wenn länger kein Poll vom Submaster, gehe in Wartezustand
#define STATUS_REPORT_INTERVAL_MS 3000 // Alle 3 Sekunden eigenen Status an Submaster melden

// ==============================================================================
// STATE MACHINE FÜR CLIENT
// ==============================================================================
enum ClientState {
    STATE_WAITING_FOR_SUBMASTER, // Wartet auf Poll vom Submaster oder Baudrate-Set
    STATE_ONLINE,                // Mit Submaster verbunden, normaler Betrieb
    STATE_ERROR                  // Allgemeiner Fehlerzustand
};

ClientState currentClientState = STATE_WAITING_FOR_SUBMASTER;

// ==============================================================================
// Globale Variablen für den Client
// ==============================================================================
unsigned long lastSubmasterPollMillis = 0;
unsigned long lastStatusReportMillis = 0;
long currentBaudRate = RS485_INITIAL_BAUD_RATE;
uint8_t currentKeyId = INITIAL_KEY_ID;

// Definition der UART für RS485
HardwareSerial& rs485Serial = Serial1; // Beispiel: UART1 des ESP32

// ==============================================================================
// HIER WÄHLT DER ENTWICKLER SEINE HARDWARE-ABSTRAKTION FÜR DIE FLUSSRICHTUNGSSTEUERUNG
// = = = > WÄHLEN SIE EINE DER FOLGENDEN ZEILEN UND PASSEN SIE DIE PINS AN < = = =
// ==============================================================================

// Option 1: Für Module MIT einem DE/RE-Pin, der manuell gesteuert werden muss
//   Der GPIO-Pin muss an den DE/RE-Pin Ihres RS485-Moduls angeschlossen werden.
// const int RS485_DE_RE_PIN = 3; // ANPASSEN: Beispiel-GPIO für DE/RE Pin des ESP32
// ManualDE_REDirectionControl myDirectionControl(RS485_DE_RE_PIN);

// Option 2: Für Module OHNE externen DE/RE-Pin (mit automatischer Flussrichtung)
//   Für diese Module sind KEINE zusätzlichen GPIOs für DE/RE notwendig.
AutomaticDirectionControl myDirectionControl;

// ==============================================================================
// Globales Objekt für den Stack - ÜBERGABE DES DIRECTIONCONTROL-OBJEKTS
// ==============================================================================
RS485SecureStack rs485Stack(&myDirectionControl);

// ==============================================================================
// Funktionsprototypen
// ==============================================================================
void onPacketReceived(RS485SecureStack::Packet_t packet);
void reportStatusToSubmaster();
void processBaudRateSet(const String& payload);
void processKeyUpdate(const String& payload);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n--- RS485SecureStack Client (Address %d) ---\n", MY_ADDRESS);

    // Seed the random number generator
    randomSeed(analogRead(0));

    // Initialisiere RS485SecureStack
    // Die myDirectionControl.begin() wird nun automatisch in rs485Stack.begin() aufgerufen
    rs485Stack.begin(MY_ADDRESS, MASTER_KEY, INITIAL_KEY_ID, rs485Serial);
    rs485Stack.registerReceiveCallback(onPacketReceived);
    rs485Stack.setDebug(true); // Debug-Ausgaben aktivieren

    lastSubmasterPollMillis = millis();
    lastStatusReportMillis = millis();
    Serial.println("Client: Initialisierung abgeschlossen. Warte auf Submaster.");
}

void loop() {
    rs485Stack.loop(); // Empfängt Pakete

    // Submaster-Präsenz überprüfen
    if (currentClientState == STATE_ONLINE && millis() - lastSubmasterPollMillis > SUBMASTER_POLL_TIMEOUT_MS) {
        Serial.println("ERR: Submaster-Poll Timeout. Gehe in Wartezustand.");
        currentClientState = STATE_WAITING_FOR_SUBMASTER;
    }

    switch (currentClientState) {
        case STATE_WAITING_FOR_SUBMASTER:
            // Warte auf Baudrate-Set oder Poll vom Submaster
            Serial.print("."); // Lebenszeichen
            delay(500);
            break;

        case STATE_ONLINE:
            if (millis() - lastStatusReportMillis > STATUS_REPORT_INTERVAL_MS) {
                reportStatusToSubmaster();
                lastStatusReportMillis = millis();
            }
            // Hier könnten weitere client-spezifische Aufgaben ausgeführt werden
            // z.B. Sensordaten lesen, Aktoren steuern etc.
            break;

        case STATE_ERROR:
            Serial.println("!!!! CLIENT IN ERROR STATE - REBOOT REQUIRED !!!!");
            delay(1000);
            break;
    }
}

// ==============================================================================
// Callback-Funktion für den RS485SecureStack
// ==============================================================================
void onPacketReceived(RS485SecureStack::Packet_t packet) {
    // Wenn Paket nicht für uns ist (oder Broadcast), und keine eigene Adresse, ignorieren
    if (packet.destinationAddress != MY_ADDRESS && packet.destinationAddress != 255) {
        // Serial.printf("DBG: Paket für Adresse %d, nicht für mich (%d). Ignoriere.\n", packet.destinationAddress, MY_ADDRESS);
        return;
    }

    // Prüfe auf HMAC und CRC Fehler
    if (!packet.hmacVerified) {
        Serial.printf("ERR: Paket von %d hatte HMAC Fehler! Payload: '%s'\n", packet.senderAddress, packet.payload.c_str());
        return; // Paket mit HMAC-Fehler ignorieren
    }
    if (!packet.crcVerified) {
        Serial.printf("ERR: Paket von %d hatte CRC Fehler! Payload: '%s'\n", packet.senderAddress, packet.payload.c_str());
        return; // Paket mit CRC-Fehler ignorieren
    }

    // Wenn es ein ACK/NACK für UNS ist, wird es intern vom sendMessage() in RS485SecureStack gehandhabt.
    // Hier kommen nur Pakete an, die der Stack nicht selbst verarbeitet hat.
    if (packet.isAck) {
        Serial.printf("DBG: ACK/NACK von %d, aber nicht für meine wartende Nachricht. Ignoriere im Callback.\n", packet.senderAddress);
        return;
    }

    // Behandlung anderer Nachrichtentypen
    switch (packet.messageType) {
        case MSG_TYPE_MASTER_HEARTBEAT: // Clients ignorieren Master-Heartbeats
            Serial.printf("RCV: Master Heartbeat von %d. Ignoriere.\n", packet.senderAddress);
            break;

        case MSG_TYPE_BAUD_RATE_SET:
            Serial.printf("RCV: Baud Rate Set von %d: '%s'\n", packet.senderAddress, packet.payload.c_str());
            processBaudRateSet(packet.payload);
            // Nach Baudrate-Set, zurück in den Wartezustand für Submaster-Signale
            currentClientState = STATE_WAITING_FOR_SUBMASTER;
            lastSubmasterPollMillis = millis(); // Reset Timeout
            break;

        case MSG_TYPE_KEY_UPDATE:
            Serial.printf("RCV: Key Update von %d: '%s'\n", packet.senderAddress, packet.payload.c_str());
            processKeyUpdate(packet.payload);
            // Nach Key-Update, zurück in den Wartezustand für Submaster-Signale
            currentClientState = STATE_WAITING_FOR_SUBMASTER;
            lastSubmasterPollMillis = millis(); // Reset Timeout
            break;

        case MSG_TYPE_DATA:
            Serial.printf("RCV: DATA von %d: '%s'\n", packet.senderAddress, packet.payload.c_str());
            if (packet.senderAddress == SUBMASTER_ADDRESS && packet.payload.equals("GET_STATUS")) {
                Serial.println("Client: Status-Anfrage vom Submaster erhalten.");
                lastSubmasterPollMillis = millis(); // Submaster ist aktiv
                currentClientState = STATE_ONLINE; // Client ist online und bereit

                // Sende sofort eine Antwort auf die Statusanfrage
                reportStatusToSubmaster();
            } else {
                Serial.println("Client: Unbekannte Daten-Nachricht.");
            }
            break;

        default:
            Serial.printf("RCV: Unerwarteter Nachrichtentyp '%c' von %d.\n", packet.messageType, packet.senderAddress);
            break;
    }
}

// ==============================================================================
// Client-Funktionen
// ==============================================================================
void reportStatusToSubmaster() {
    Serial.println("Client: Melde Status an Submaster.");
    StreamString payload;
    // Beispiel: Sende Temperatur und Luftfeuchtigkeit
    float temperature = random(200, 300) / 10.0; // 20.0 - 30.0
    float humidity = random(400, 600) / 10.0;    // 40.0 - 60.0
    payload.printf("TEMP_HUMID:%.1f,%.1f", temperature, humidity);
    
    // Status an den Submaster senden, kein ACK erforderlich (Submaster poll_Clientt ja Heartbeat)
    if (!rs485Stack.sendMessage(SUBMASTER_ADDRESS, MY_ADDRESS, MSG_TYPE_DATA, payload.c_str(), false)) {
        Serial.println("ERR: Fehler beim Senden des Status an Submaster.");
    }
}

void processBaudRateSet(const String& payload) {
    long newBaudRate = payload.toInt();
    if (newBaudRate > 0 && newBaudRate != currentBaudRate) {
        Serial.printf("Client: Baudrate auf %ld eingestellt.\n", newBaudRate);
        rs485Stack.setBaudRate(newBaudRate);
        currentBaudRate = newBaudRate;
    } else {
        Serial.printf("Client: Baudrate-Set ungültig oder gleiche Baudrate: %s\n", payload.c_str());
    }
}

void processKeyUpdate(const String& payload) {
    // Payload ist JSON, z.B. {"keyID":1,"sessionKey":"base64_encoded_encrypted_key","iv":"hex_iv"}
    
    // JSON-Parsing (einfach gehalten für PoC, in Prod: ArduinoJson verwenden)
    int keyIdIndex = payload.indexOf("\"keyID\":");
    int sessionKeyIndex = payload.indexOf("\"sessionKey\":\"");
    int ivIndex = payload.indexOf("\"iv\":\"");
    
    if (keyIdIndex == -1 || sessionKeyIndex == -1 || ivIndex == -1) {
        Serial.println("ERR: Key Update Payload ungültig (JSON-Fehler).");
        return;
    }

    uint8_t newKeyId = payload.substring(keyIdIndex + 8).toInt();
    
    String encryptedKeyHex = payload.substring(sessionKeyIndex + 14, payload.indexOf("\"", sessionKeyIndex + 14));
    String ivHex = payload.substring(ivIndex + 6, payload.indexOf("\"", ivIndex + 6));

    if (encryptedKeyHex.length() != 64 || ivHex.length() != 32) { // 32 Bytes Key -> 64 Hex-Chars, 16 Bytes IV -> 32 Hex-Chars
        Serial.println("ERR: Key Update Payload: Hex-Länge ungültig.");
        return;
    }

    uint8_t encryptedSessionKey[32];
    uint8_t iv[16];

    // Konvertierung von Hex-String zu Bytes
    for (int i = 0; i < 32; ++i) {
        encryptedSessionKey[i] = (uint8_t)strtol(encryptedKeyHex.substring(i * 2, (i * 2) + 2).c_str(), NULL, 16);
    }
    for (int i = 0; i < 16; ++i) {
        iv[i] = (uint8_t)strtol(ivHex.substring(i * 2, (i * 2) + 2).c_str(), NULL, 16);
    }

    // Entschlüsseln des Session Keys mit dem Master Key
    AES256 aes256;
    uint8_t masterKeyHash[32];
    SHA256 sha256;
    sha256.reset();
    sha256.update(MASTER_KEY, strlen(MASTER_KEY));
    sha256.finalize(masterKeyHash, sizeof(masterKeyHash));

    aes256.setKey(masterKeyHash, aes256.keySize());
    aes256.setIV(iv, aes256.ivSize());
    aes256.decryptCBC(encryptedSessionKey, 32);

    // Neuen Session Key im Stack setzen
    if (rs485Stack.setSessionKey(newKeyId, encryptedSessionKey, sizeof(encryptedSessionKey))) {
        rs485Stack.setCurrentKeyId(newKeyId);
        currentKeyId = newKeyId;
        Serial.printf("Client: Erfolgreich neuen Session Key (ID %d) gesetzt.\n", currentKeyId);
    } else {
        Serial.println("ERR: Fehler beim Setzen des neuen Session Keys.");
    }
}

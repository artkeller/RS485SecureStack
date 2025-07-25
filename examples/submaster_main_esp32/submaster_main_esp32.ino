#include <Arduino.h>
#include <HardwareSerial.h>
#include <map>
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
// GLOBAL KONFIGURATION (Submaster-spezifisch)
// ==============================================================================
#define MY_ADDRESS 1 // ANPASSEN: Eindeutige Adresse für diesen Submaster (z.B. 1 oder 2)
#define MASTER_ADDRESS 0 // Adresse des Haupt-Schedulers (Masters)
#define INITIAL_KEY_ID 0 // Startet mit Key ID 0

#define MASTER_HEARTBEAT_TIMEOUT_MS 10000 // Wenn länger kein Master-Heartbeat, gehe in Wartezustand
#define CLIENT_POLL_INTERVAL_MS 2000 // Alle 2 Sekunden Clients abfragen, wenn Sendeerlaubnis vorhanden
#define SUBMASTER_STATUS_REPORT_INTERVAL_MS 5000 // Alle 5 Sekunden eigenen Status an Master melden

// ==============================================================================
// STATE MACHINE FÜR SUBMASTER
// ==============================================================================
enum SubmasterState {
    STATE_WAITING_FOR_MASTER,     // Wartet auf Master-Heartbeat oder Baudrate-Set
    STATE_WAITING_FOR_PERMISSION, // Master ist da, wartet auf Sendeerlaubnis
    STATE_COMMUNICATING_WITH_CLIENTS, // Hat Sendeerlaubnis, kommuniziert mit Clients
    STATE_ERROR                   // Allgemeiner Fehlerzustand
};

SubmasterState currentSubmasterState = STATE_WAITING_FOR_MASTER;

// ==============================================================================
// Globale Variablen für den Submaster
// ==============================================================================
unsigned long lastMasterHeartbeatMillis = 0;
unsigned long lastClientPollMillis = 0;
unsigned long lastStatusReportMillis = 0;
long currentBaudRate = RS485_INITIAL_BAUD_RATE;
uint8_t currentKeyId = INITIAL_KEY_ID;

// Clients, die dieser Submaster verwaltet (Beispiel)
const uint8_t MANAGED_CLIENTS[] = {11}; // ANPASSEN: Clients, die von diesem Submaster verwaltet werden
const int NUM_MANAGED_CLIENTS = sizeof(MANAGED_CLIENTS) / sizeof(MANAGED_CLIENTS[0]);
int currentClientIndex = 0; // Welcher Client als Nächstes abgefragt wird

// Definition der UART für RS485
HardwareSerial& rs485Serial = Serial1; // Beispiel: UART1 des ESP32

// ==============================================================================
// HIER WÄHLT DER ENTWICKLER SEINE HARDWARE-ABSTRAKTION FÜR DIE FLUSSRICHTUNGSSTEUERUNG
// = = = > WÄHLEN SIE EINE DER FOLGENDEN ZEILEN UND PASSEN SIE DIE PINS AN < = = =
// ==============================================================================

// Option 1: Für Module MIT einem DE/RE-Pin, der manuell gesteuert werden muss
//   Der GPIO-Pin muss an den DE/RE-Pin Ihres RS485-Moduls angeschlossen werden.
const int RS485_DE_RE_PIN = 3; // ANPASSEN: Beispiel-GPIO für DE/RE Pin des ESP32
ManualDE_REDirectionControl myDirectionControl(RS485_DE_RE_PIN);

// Option 2: Für Module OHNE externen DE/RE-Pin (mit automatischer Flussrichtung)
//   Für diese Module sind KEINE zusätzlichen GPIOs für DE/RE notwendig.
// AutomaticDirectionControl myDirectionControl;

// ==============================================================================
// Globales Objekt für den Stack - ÜBERGABE DES DIRECTIONCONTROL-OBJEKTS
// ==============================================================================
RS485SecureStack rs485Stack(&myDirectionControl);

// ==============================================================================
// Funktionsprototypen
// ==============================================================================
void onPacketReceived(RS485SecureStack::Packet_t packet);
void reportStatusToMaster();
void pollClient();
void processBaudRateSet(const String& payload);
void processKeyUpdate(const String& payload);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n--- RS485SecureStack Submaster (Address %d) ---\n", MY_ADDRESS);

    // Seed the random number generator
    randomSeed(analogRead(0));

    // Initialisiere RS485SecureStack
    // Die myDirectionControl.begin() wird nun automatisch in rs485Stack.begin() aufgerufen
    rs485Stack.begin(MY_ADDRESS, MASTER_KEY, INITIAL_KEY_ID, rs485Serial);
    rs485Stack.registerReceiveCallback(onPacketReceived);
    rs485Stack.setDebug(true); // Debug-Ausgaben aktivieren

    lastMasterHeartbeatMillis = millis();
    lastClientPollMillis = millis();
    lastStatusReportMillis = millis();
    Serial.println("Submaster: Initialisierung abgeschlossen. Warte auf Master.");
}

void loop() {
    rs485Stack.loop(); // Empfängt Pakete

    // Master-Präsenz überprüfen
    if (millis() - lastMasterHeartbeatMillis > MASTER_HEARTBEAT_TIMEOUT_MS) {
        if (currentSubmasterState != STATE_WAITING_FOR_MASTER) {
            Serial.println("ERR: Master-Heartbeat Timeout. Gehe in Wartezustand.");
            currentSubmasterState = STATE_WAITING_FOR_MASTER;
        }
    }

    switch (currentSubmasterState) {
        case STATE_WAITING_FOR_MASTER:
            // Warte auf Baudrate-Set oder Master-Heartbeat, sonst passiv
            Serial.print("."); // Lebenszeichen
            delay(500);
            break;

        case STATE_WAITING_FOR_PERMISSION:
            // Master ist da, aber keine Sendeerlaubnis erhalten
            if (millis() - lastStatusReportMillis > SUBMASTER_STATUS_REPORT_INTERVAL_MS) {
                reportStatusToMaster();
                lastStatusReportMillis = millis();
            }
            break;

        case STATE_COMMUNICATING_WITH_CLIENTS:
            if (millis() - lastClientPollMillis > CLIENT_POLL_INTERVAL_MS) {
                pollClient();
                lastClientPollMillis = millis();
            }
            if (millis() - lastStatusReportMillis > SUBMASTER_STATUS_REPORT_INTERVAL_MS) {
                reportStatusToMaster();
                lastStatusReportMillis = millis();
            }
            break;

        case STATE_ERROR:
            Serial.println("!!!! SUBMASTER IN ERROR STATE - REBOOT REQUIRED !!!!");
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
        // Bei HMAC-Fehler antworten wir nicht, da die Quelle unzuverlässig ist.
        return; 
    }
    if (!packet.crcVerified) {
        Serial.printf("ERR: Paket von %d hatte CRC Fehler! Payload: '%s'\n", packet.senderAddress, packet.payload.c_str());
        // Bei CRC-Fehler antworten wir nicht, da das Paket beschädigt ist.
        return;
    }

    // Wenn es ein ACK/NACK für UNS ist, wird es intern vom sendMessage() in RS485SecureStack gehandhabt.
    // Hier kommen nur Pakete an, die der Stack nicht selbst verarbeitet hat.
    if (packet.isAck) {
        Serial.printf("DBG: ACK/NACK von %d, aber nicht für meine wartende Nachricht. Ignoriere im Callback.\n", packet.senderAddress);
        return; 
    }

    // Behandlung anderer Nachrichtentypen
    switch (packet.messageType) {
        case MSG_TYPE_MASTER_HEARTBEAT:
            Serial.println("RCV: Master Heartbeat.");
            lastMasterHeartbeatMillis = millis();
            if (currentSubmasterState == STATE_WAITING_FOR_MASTER) {
                currentSubmasterState = STATE_WAITING_FOR_PERMISSION; // Master ist da
                Serial.println("Submaster: Master gefunden. Warte auf Sendeerlaubnis.");
            }
            break;

        case MSG_TYPE_BAUD_RATE_SET:
            Serial.printf("RCV: Baud Rate Set von Master: '%s'\n", packet.payload.c_str());
            processBaudRateSet(packet.payload);
            // Nach Baudrate-Set, zurück in den Wartezustand für Master-Signale
            currentSubmasterState = STATE_WAITING_FOR_MASTER;
            lastMasterHeartbeatMillis = millis(); // Reset Timeout
            break;

        case MSG_TYPE_KEY_UPDATE:
            Serial.printf("RCV: Key Update von Master: '%s'\n", packet.payload.c_str());
            processKeyUpdate(packet.payload);
            // Nach Key-Update, zurück in den Wartezustand für Master-Signale
            currentSubmasterState = STATE_WAITING_FOR_MASTER;
            lastMasterHeartbeatMillis = millis(); // Reset Timeout
            break;

        case MSG_TYPE_DATA:
            Serial.printf("RCV: DATA von %d: '%s'\n", packet.senderAddress, packet.payload.c_str());
            if (packet.payload.equals("PERMISSION_TO_SEND")) {
                if (packet.senderAddress == MASTER_ADDRESS) {
                    Serial.println("Submaster: Sendeerlaubnis vom Master erhalten!");
                    currentSubmasterState = STATE_COMMUNICATING_WITH_CLIENTS;
                    lastClientPollMillis = millis(); // Starte sofort mit Client-Kommunikation
                } else {
                    Serial.println("ERR: Unerwartete Sendeerlaubnis von Nicht-Master-Adresse.");
                }
            } else if (packet.payload.startsWith("STATUS_OK") || packet.payload.startsWith("TEMP_HUMID:")) {
                // Antwort von einem Client
                Serial.printf("Submaster: Antwort von Client %d: %s\n", packet.senderAddress, packet.payload.c_str());
            } else {
                Serial.println("Submaster: Unbekannte Daten-Nachricht.");
            }
            break;

        default:
            Serial.printf("RCV: Unerwarteter Nachrichtentyp '%c' von %d.\n", packet.messageType, packet.senderAddress);
            break;
    }
}

// ==============================================================================
// Submaster-Funktionen
// ==============================================================================
void reportStatusToMaster() {
    Serial.println("Submaster: Melde Status an Master.");
    StreamString payload;
    payload.printf("SUB_STATUS:Online,State:%d,Baud:%ld,KeyID:%d", currentSubmasterState, currentBaudRate, currentKeyId);
    
    // Status an den Master senden, kein ACK erforderlich (Master poll_Clientt ja Heartbeat)
    if (!rs485Stack.sendMessage(MASTER_ADDRESS, MY_ADDRESS, MSG_TYPE_DATA, payload.c_str(), false)) {
        Serial.println("ERR: Fehler beim Senden des Status an Master.");
    }
}

void pollClient() {
    if (NUM_MANAGED_CLIENTS == 0) return;

    uint8_t targetClient = MANAGED_CLIENTS[currentClientIndex];
    Serial.printf("Submaster: Frage Client %d ab.\n", targetClient);

    // Sende "GET_STATUS" an den Client, erfordert ACK
    if (rs485Stack.sendMessage(targetClient, MY_ADDRESS, MSG_TYPE_DATA, "GET_STATUS", true)) {
        Serial.printf("Submaster: Anfrage an Client %d erfolgreich gesendet und ACK erhalten.\n", targetClient);
    } else {
        Serial.printf("ERR: Fehler oder Timeout bei Anfrage an Client %d.\n", targetClient);
        // Hier könnte man den Client als offline markieren oder einen Fehler zählen
    }

    // Nächsten Client für die nächste Abfrage auswählen
    currentClientIndex = (currentClientIndex + 1) % NUM_MANAGED_CLIENTS;
}

void processBaudRateSet(const String& payload) {
    long newBaudRate = payload.toInt();
    if (newBaudRate > 0 && newBaudRate != currentBaudRate) {
        Serial.printf("Submaster: Baudrate auf %ld eingestellt.\n", newBaudRate);
        rs485Stack.setBaudRate(newBaudRate);
        currentBaudRate = newBaudRate;
    } else {
        Serial.printf("Submaster: Baudrate-Set ungültig oder gleiche Baudrate: %s\n", payload.c_str());
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
        Serial.printf("Submaster: Erfolgreich neuen Session Key (ID %d) gesetzt.\n", currentKeyId);
    } else {
        Serial.println("ERR: Fehler beim Setzen des neuen Session Keys.");
    }
}

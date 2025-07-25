#include <Arduino.h>
#include <HardwareSerial.h>
#include <map> // Für die Verwaltung der verbundenen Nodes
#include <vector> // Für dynamische Arrays
#include <string> // Für String-Manipulationen
#include <StreamString.h> // Für Stream-Operationen

// Lokale Bibliotheks-Includes
#include "RS485SecureStack.h"
#include "credentials.h" // Enthält MASTER_KEY, MY_ADDRESS etc.

// WICHTIG: Wählen Sie EINE der folgenden Zeilen, je nach Ihrem RS485-Modul:
// Option 1: Für Module MIT einem DE/RE-Pin, der manuell gesteuert werden muss (z.B. einfache MAX485-Module)
#include "ManualDE_REDirectionControl.h" 

// Option 2: Für Module OHNE externen DE/RE-Pin (mit automatischer Flussrichtung, z.B. bestimmte HW-159/HW-519)
// #include "AutomaticDirectionControl.h" 


// ==============================================================================
// GLOBAL KONFIGURATION (Scheduler-spezifisch)
// ==============================================================================
#define MY_ADDRESS 0 // Master ist immer Adresse 0
#define CURRENT_KEY_ID 0 // Startet mit Key ID 0

#define MASTER_HEARTBEAT_INTERVAL_MS 5000 // Alle 5 Sekunden einen Heartbeat senden
#define BAUD_RATE_MEASUREMENT_INTERVAL_MS 60000 // Alle 60 Sekunden Baudrate einmessen (nur PoC)
#define REKEYING_INTERVAL_MS 300000 // Alle 5 Minuten Rekeying starten (nur PoC)
#define NODE_TIMEOUT_MS 15000 // Wenn keine Kommunikation von Node in dieser Zeit, als offline markieren

// Liste der zu testenden Baudraten für die Einmessung
const long TEST_BAUD_RATES[] = {115200L, 57600L, 38400L, 19200L, 9600L};
const int NUM_BAUD_RATES = sizeof(TEST_BAUD_RATES) / sizeof(TEST_BAUD_RATES[0]);

// ==============================================================================
// STATE MACHINE FÜR SCHEDULER
// ==============================================================================
enum SchedulerState {
    STATE_INIT_BUS,             // Bus initialisieren, Baudrate einmessen
    STATE_NORMAL_OPERATION,     // Normaler Betrieb, Heartbeats senden, Kommunikation verwalten
    STATE_REKEYING,             // Schlüsselupdate durchführen
    STATE_ROGUEMASTER_DETECTED, // Rogue Master erkannt, sicherer Zustand
    STATE_ERROR                 // Allgemeiner Fehlerzustand
};

SchedulerState currentSchedulerState = STATE_INIT_BUS;

// ==============================================================================
// Globale Variablen für den Scheduler
// ==============================================================================
unsigned long lastHeartbeatMillis = 0;
unsigned long lastBaudRateMeasurementMillis = 0;
unsigned long lastRekeyingMillis = 0;
unsigned long rekeyingStartTime = 0;
int currentBaudRateIndex = 0;
bool baudRateSetAckReceived = false;
uint8_t nextKeyId = 1; // Startet mit Key ID 1 für das erste Rekeying

// Node-Zustandsverwaltung
struct NodeStatus {
    unsigned long lastSeenMillis;
    bool isOnline;
    bool permissionToSend; // Nur für Submaster relevant
    bool awaitingAck;      // Für Baudrate/Key-Update ACKs
};
std::map<uint8_t, NodeStatus> connectedNodes; // Key: Node-Adresse

// Baudrate-Management für Rekeying
long rekeyingBaudRate = 0;
unsigned long rekeyingAckCount = 0; // Anzahl der ACKs für Rekeying

// Zähler für Statistiken
unsigned long packetsSent = 0;
unsigned long packetsReceived = 0;
unsigned long heartbeatCounter = 0;
unsigned long hmacErrors = 0;
unsigned long crcErrors = 0;

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
void manageBaudRateMeasurement();
void manageRekeying();
void sendHeartbeat();
void updateNodeStatus(uint8_t address);
void checkNodeTimeouts();
void sendPermissionToSubmaster(uint8_t submasterAddress);
void generateAndSendNewKey();


void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- RS485SecureStack Scheduler (Master) ---");

    // Seed the random number generator
    randomSeed(analogRead(0));

    // Initialisiere RS485SecureStack
    // Die myDirectionControl.begin() wird nun automatisch in rs485Stack.begin() aufgerufen
    rs485Stack.begin(MY_ADDRESS, MASTER_KEY, CURRENT_KEY_ID, rs485Serial);
    rs485Stack.registerReceiveCallback(onPacketReceived);
    rs485Stack.setDebug(true); // Debug-Ausgaben aktivieren

    // Füge die erwarteten Nodes zur Map hinzu (Beispiel)
    connectedNodes[1] = {0, false, false, false}; // Submaster 1
    connectedNodes[2] = {0, false, false, false}; // Submaster 2
    connectedNodes[11] = {0, false, false, false}; // Client 11 (zugeordnet zu Submaster 1)
    connectedNodes[12] = {0, false, false, false}; // Client 12 (zugeordnet zu Submaster 2)

    lastHeartbeatMillis = millis();
    lastBaudRateMeasurementMillis = millis();
    lastRekeyingMillis = millis();
    Serial.println("Scheduler: Initialisierung abgeschlossen.");
}

void loop() {
    rs485Stack.loop(); // Empfängt Pakete

    switch (currentSchedulerState) {
        case STATE_INIT_BUS:
            manageBaudRateMeasurement();
            break;

        case STATE_NORMAL_OPERATION:
            if (millis() - lastHeartbeatMillis > MASTER_HEARTBEAT_INTERVAL_MS) {
                sendHeartbeat();
                lastHeartbeatMillis = millis();
            }
            if (millis() - lastBaudRateMeasurementMillis > BAUD_RATE_MEASUREMENT_INTERVAL_MS) {
                currentSchedulerState = STATE_INIT_BUS; // Erneut Baudrate einmessen
                Serial.println("Scheduler: Starte erneute Baudraten-Einmessung.");
                lastBaudRateMeasurementMillis = millis();
            }
            if (millis() - lastRekeyingMillis > REKEYING_INTERVAL_MS) {
                currentSchedulerState = STATE_REKEYING;
                Serial.println("Scheduler: Starte Rekeying-Prozess.");
                rekeyingStartTime = millis();
                rekeyingAckCount = 0;
                generateAndSendNewKey();
                lastRekeyingMillis = millis(); // Reset für nächsten Rekeying-Intervall
            }
            checkNodeTimeouts();
            // Beispiel: Erlaube Submaster 1 alle 10 Sekunden zu senden
            // if (millis() % 10000 < 100 && !connectedNodes[1].permissionToSend) {
            //     sendPermissionToSubmaster(1);
            // }
            break;

        case STATE_REKEYING:
            manageRekeying();
            break;

        case STATE_ROGUEMASTER_DETECTED:
            Serial.println("!!!! ROGUE MASTER DETECTED - ENTERING SAFE MODE !!!!");
            // Hier sollten weitere Maßnahmen ergriffen werden, z.B. Alarme auslösen,
            // alle aktiven Operationen einstellen, Bus in den Halted-Zustand versetzen.
            // Der Scheduler wird keine weiteren Nachrichten senden, solange dieser Zustand anhält.
            delay(1000); // Zur Beruhigung der Ausgabe
            break;

        case STATE_ERROR:
            Serial.println("!!!! SCHEDULER IN ERROR STATE - REBOOT REQUIRED !!!!");
            delay(1000); // Zur Beruhigung der Ausgabe
            // In einer echten Anwendung würde hier ein Watchdog-Reset ausgelöst oder
            // eine komplexere Fehlerbehandlung erfolgen.
            break;
    }
}

// ==============================================================================
// Callback-Funktion für den RS485SecureStack
// ==============================================================================
void onPacketReceived(RS485SecureStack::Packet_t packet) {
    packetsReceived++;

    // Prüfe auf HMAC und CRC Fehler
    if (!packet.hmacVerified) {
        hmacErrors++;
        Serial.printf("ERR: Paket von %d hatte HMAC Fehler! Payload: '%s'\n", packet.senderAddress, packet.payload.c_str());
        return; // Paket mit HMAC-Fehler ignorieren
    }
    if (!packet.crcVerified) {
        crcErrors++;
        Serial.printf("ERR: Paket von %d hatte CRC Fehler! Payload: '%s'\n", packet.senderAddress, packet.payload.c_str());
        return; // Paket mit CRC-Fehler ignorieren
    }

    // Aktualisiere den Last-Seen-Status für den Absender
    updateNodeStatus(packet.senderAddress);

    // Spezialbehandlung für ACKs/NACKs
    if (packet.isAck) {
        Serial.printf("RCV ACK/NACK von %d: %s\n", packet.senderAddress, packet.payload.c_str());
        if (packet.payload.startsWith("ACK")) {
            // Je nach Kontext des wartenden ACK:
            if (currentSchedulerState == STATE_INIT_BUS) {
                // Bestätigung für Baudrate-Set
                // Wir zählen ACKs und gehen erst weiter, wenn alle erwarteten Nodes geantwortet haben
                baudRateSetAckReceived = true; // Setzt für den Master, dass er geantwortet hat
                Serial.printf("ACK für Baudrate von Node %d erhalten.\n", packet.senderAddress);
                // Hier müsste man tracken, welche Nodes geantwortet haben
            } else if (currentSchedulerState == STATE_REKEYING && packet.messageType == MSG_TYPE_ACK_NACK) {
                // Bestätigung für Key Update
                rekeyingAckCount++;
                Serial.printf("ACK für Rekeying von Node %d erhalten. Zähler: %lu/%lu\n", packet.senderAddress, rekeyingAckCount, connectedNodes.size());
            }
        } else if (packet.payload.startsWith("NACK")) {
            Serial.printf("NACK von %d erhalten: %s\n", packet.senderAddress, packet.payload.c_str());
            // Fehlerbehandlung für NACK
            if (currentSchedulerState == STATE_REKEYING) {
                Serial.printf("Rekeying fehlgeschlagen für Node %d: %s. Bleibe bei alter Key ID.\n", packet.senderAddress, packet.payload.c_str());
                // Hier müsste man ggf. ein Rollback initiieren oder den Node isolieren
                currentSchedulerState = STATE_NORMAL_OPERATION; // Abbruch des Rekeyings
            }
        }
        return; // ACK/NACK wurde verarbeitet, keine weitere Behandlung
    }

    // Behandlung anderer Nachrichtentypen
    switch (packet.messageType) {
        case MSG_TYPE_MASTER_HEARTBEAT:
            // Wenn ein anderer Master (nicht Adresse 0) einen Heartbeat sendet, ist das ein Rogue Master!
            if (packet.senderAddress != MY_ADDRESS && packet.senderAddress != 255) { // 255 ist Broadcast
                Serial.printf("!!!! Scheduler: ROGUE MASTER DETECTED from address %d !!!!\n", packet.senderAddress);
                currentSchedulerState = STATE_ROGUEMASTER_DETECTED;
            } else {
                 if (packet.senderAddress == MY_ADDRESS) {
                    Serial.printf("DBG: Eigener Heartbeat empfangen (Loopback).\n");
                } else {
                    Serial.printf("DBG: Master-Heartbeat (Broadcast) empfangen von %d.\n", packet.senderAddress);
                }
            }
            break;

        case MSG_TYPE_DATA:
            Serial.printf("RCV DATA von %d: '%s'\n", packet.senderAddress, packet.payload.c_str());
            // Beispiel: Wenn ein Submaster einen Status sendet
            if (connectedNodes.count(packet.senderAddress) && packet.payload.startsWith("SUB_STATUS:")) {
                Serial.printf("Submaster %d Status: %s\n", packet.senderAddress, packet.payload.c_str());
                if (connectedNodes[packet.senderAddress].permissionToSend) {
                    connectedNodes[packet.senderAddress].permissionToSend = false; // Permission verbraucht
                }
            }
            // Beispiel: Client meldet Status
            if (connectedNodes.count(packet.senderAddress) && packet.payload.startsWith("STATUS_OK")) {
                 Serial.printf("Client %d Status: %s\n", packet.senderAddress, packet.payload.c_str());
            }
            break;
        
        case MSG_TYPE_BAUD_RATE_SET: // Scheduler empfängt keine Baudrate Set Nachrichten
        case MSG_TYPE_KEY_UPDATE:    // Scheduler empfängt keine Key Update Nachrichten
            Serial.printf("RCV Unerwarteter Nachrichtentyp '%c' von %d.\n", packet.messageType, packet.senderAddress);
            break;
    }
}

// ==============================================================================
// Baudraten-Einmessung
// ==============================================================================
void manageBaudRateMeasurement() {
    // Sende die aktuelle Test-Baudrate als Broadcast
    if (millis() - lastBaudRateMeasurementMillis > 2000) { // Genug Zeit für Antworten
        if (currentBaudRateIndex < NUM_BAUD_RATES) {
            long testBaud = TEST_BAUD_RATES[currentBaudRateIndex];
            Serial.printf("Scheduler: Teste Baudrate: %ld\n", testBaud);
            rs485Stack.setBaudRate(testBaud); // Setze eigene Baudrate

            StreamString payload;
            payload.printf("%ld", testBaud);

            // Sende die Baudrate als Broadcast. Erfordert ACK.
            if (rs485Stack.sendMessage(255, MY_ADDRESS, MSG_TYPE_BAUD_RATE_SET, payload.c_str(), true)) {
                // Erfolg bedeutet, dass das Senden und das Warten auf ACKs erfolgreich waren.
                // ACHTUNG: sendMessage(..., true) wartet nur auf EIN ACK. Für alle Nodes müsste man eigene Logik implementieren.
                // Für dieses PoC gehen wir davon aus, dass ein ACK für Broadcasts reicht oder wir prüfen die individuellen Nodes später.
                Serial.printf("Scheduler: Baudrate %ld gesendet und ACK erhalten (mind. einer).\n", testBaud);
                currentSchedulerState = STATE_NORMAL_OPERATION;
                lastBaudRateMeasurementMillis = millis(); // Setze Zeit für nächste Einmessung
                Serial.printf("Scheduler: Erfolgreich auf Baudrate %ld eingestellt. Normaler Betrieb.\n", testBaud);
                return;
            } else {
                Serial.printf("Scheduler: Baudrate %ld konnte nicht gesetzt werden (kein ACK).\n", testBaud);
                currentBaudRateIndex++; // Versuche nächste Baudrate
            }
        } else {
            Serial.println("Scheduler: Alle Baudraten getestet, keine stabile Rate gefunden. Bleibe bei initialer Baudrate.");
            currentBaudRateIndex = 0; // Reset für den nächsten Zyklus
            currentSchedulerState = STATE_ERROR; // Kann keine Kommunikation aufbauen
            // Optional: Hard-Reset oder Default-Baudrate erzwingen
        }
        lastBaudRateMeasurementMillis = millis(); // Wartezeit für nächsten Versuch
    }
}

// ==============================================================================
// Rekeying Management
// ==============================================================================
void generateAndSendNewKey() {
    // Generiere eine neue Key ID (muss von 0-255 reichen, 0 ist Master Key)
    nextKeyId++;
    if (nextKeyId == 0) nextKeyId = 1; // 0 ist reserviert für Master Key oder initialen Schlüssel

    // Generiere einen neuen, zufälligen Session Key (32 Bytes für SHA256)
    uint8_t newSessionKey[32];
    for (int i = 0; i < 32; ++i) {
        newSessionKey[i] = random(256);
    }
    
    // Setze den neuen Schlüssel im Scheduler selbst
    rs485Stack.setSessionKey(nextKeyId, newSessionKey, sizeof(newSessionKey));
    rs485Stack.setCurrentKeyId(nextKeyId); // Ab sofort diesen Schlüssel verwenden

    // Bereite den Payload vor: JSON mit keyID und dem verschlüsselten SessionKey
    // Der SessionKey muss mit dem MasterKey verschlüsselt werden, damit nur autorisierte Nodes ihn lesen können
    AES256 aes256;
    uint8_t encryptedSessionKey[32];
    uint8_t iv[16];
    
    // IV für die Verschlüsselung des Session Keys
    for (int i = 0; i < 16; ++i) iv[i] = random(256); // Zufälliges IV

    // Master Key hashen (für AES-Key)
    uint8_t masterKeyHash[32];
    SHA256 sha256;
    sha256.reset();
    sha256.update(MASTER_KEY, strlen(MASTER_KEY));
    sha256.finalize(masterKeyHash, sizeof(masterKeyHash));

    aes256.setKey(masterKeyHash, aes256.keySize());
    aes256.setIV(iv, aes256.ivSize());
    memcpy(encryptedSessionKey, newSessionKey, 32); // Kopiere den Session Key in den Puffer
    aes256.encryptCBC(encryptedSessionKey, 32); // Verschlüssele den Session Key

    // Base64-Kodierung für den verschlüsselten Session Key
    // Achtung: Crypto.h bietet keine Base64-Kodierung. Hier müsste eine manuelle Implementierung oder eine separate Bibliothek her.
    // Für dieses PoC simulieren wir die Kodierung einfach
    char base64EncryptedKey[65]; // 32 Bytes werden zu 44 Base64-Chars + Nullterminierung
    // Beispielhaft (nicht real):
    for(int i=0; i<32; ++i) sprintf(&base64EncryptedKey[i*2], "%02X", encryptedSessionKey[i]);
    base64EncryptedKey[64] = '\0'; // Nullterminierung

    StreamString payload;
    payload.printf("{\"keyID\":%d,\"sessionKey\":\"%s\",\"iv\":\"", nextKeyId, base64EncryptedKey);
    for(int i=0; i<16; ++i) payload.printf("%02X", iv[i]);
    payload.printf("\"}");

    Serial.printf("Scheduler: Sende neuen Key (ID %d) an alle Nodes...\n", nextKeyId);
    // Sende den Key-Update als Broadcast. Erfordert ACK.
    // Jeder Node muss mit ACK antworten
    rs485Stack.sendMessage(255, MY_ADDRESS, MSG_TYPE_KEY_UPDATE, payload.c_str(), true);
    // ACHTUNG: Die sendMessage Funktion wartet nur auf das ERSTE ACK.
    // Für einen robusten Rekeying-Prozess müsste hier eine Logik mit individuellen ACKs
    // von jeder erwarteten Node implementiert werden, um sicherzustellen, dass alle Nodes den Key erhalten haben.
    // Für dieses PoC ist dies eine Vereinfachung.
}

void manageRekeying() {
    // Im PoC wartet der Scheduler nicht auf alle ACKs, sondern geht davon aus,
    // dass das erste ACK genügt, um das Rekeying als initiiert zu betrachten.
    // Eine robustere Implementierung würde hier eine Schleife haben, die auf alle
    // erwarteten ACKs wartet oder nach einer gewissen Zeit einen Timeout hat.
    
    // Nach dem Senden des Keys wird auf die ACKs gewartet. Wenn die Zeit abgelaufen ist
    // oder genügend ACKs gesammelt wurden, wird in den Normalbetrieb zurückgekehrt.
    if (millis() - rekeyingStartTime > 5000) { // 5 Sekunden für alle ACKs
        if (rekeyingAckCount >= connectedNodes.size()) { // Alle haben geantwortet (Idealfall)
            Serial.println("Scheduler: Rekeying erfolgreich abgeschlossen. Alle Nodes haben geantwortet.");
            currentSchedulerState = STATE_NORMAL_OPERATION;
        } else {
            Serial.printf("Scheduler: Rekeying abgeschlossen, aber nicht alle Nodes (%lu/%lu) haben geantwortet.\n", rekeyingAckCount, connectedNodes.size());
            Serial.println("Scheduler: Kehre zum Normalbetrieb zurück. Überprüfe Nodes manuell.");
            currentSchedulerState = STATE_NORMAL_OPERATION;
            // Hier könnte man versuchen, die fehlenden Nodes erneut zu rekeyen oder sie als offline zu markieren
        }
    }
}


// ==============================================================================
// Hilfsfunktionen
// ==============================================================================
void sendHeartbeat() {
    if (currentSchedulerState == STATE_ROGUEMASTER_DETECTED) {
        Serial.println("Scheduler: KEIN Heartbeat gesendet - Rogue Master erkannt!");
        return;
    }
    Serial.println("Scheduler: Sende Master Heartbeat.");
    // Heartbeat als Broadcast senden, kein ACK erforderlich
    // Die sendMessage-Methode des Stacks kümmert sich jetzt intern um die Flussrichtung
    if (rs485Stack.sendMessage(255, MY_ADDRESS, MSG_TYPE_MASTER_HEARTBEAT, "H", false)) {
        packetsSent++;
        // Serial.println("Master Heartbeat gesendet.");
    } else {
        Serial.println("Fehler beim Senden des Master Heartbeats.");
    }
}

void updateNodeStatus(uint8_t address) {
    if (connectedNodes.count(address)) {
        connectedNodes[address].lastSeenMillis = millis();
        connectedNodes[address].isOnline = true;
    }
}

void checkNodeTimeouts() {
    for (auto const& [address, status] : connectedNodes) {
        if (status.isOnline && millis() - status.lastSeenMillis > NODE_TIMEOUT_MS) {
            connectedNodes[address].isOnline = false;
            Serial.printf("Node %d ist offline gegangen (Timeout).\n", address);
        }
    }
}

void sendPermissionToSubmaster(uint8_t submasterAddress) {
    if (currentSchedulerState == STATE_ROGUEMASTER_DETECTED) {
        Serial.println("Scheduler: KEINE Sendeerlaubnis vergeben - Rogue Master erkannt!");
        return;
    }
    if (connectedNodes.count(submasterAddress) && connectedNodes[submasterAddress].isOnline) {
        Serial.printf("Scheduler: Sende Sendeerlaubnis an Submaster %d.\n", submasterAddress);
        // Sendeerlaubnis als reguläre Daten-Nachricht, erfordert ACK
        if (rs485Stack.sendMessage(submasterAddress, MY_ADDRESS, MSG_TYPE_DATA, "PERMISSION_TO_SEND", true)) {
            connectedNodes[submasterAddress].permissionToSend = true;
            packetsSent++;
        } else {
            Serial.printf("Fehler beim Senden der Sendeerlaubnis an Submaster %d.\n", submasterAddress);
        }
    } else {
        Serial.printf("Submaster %d nicht gefunden oder offline.\n", submasterAddress);
    }
}

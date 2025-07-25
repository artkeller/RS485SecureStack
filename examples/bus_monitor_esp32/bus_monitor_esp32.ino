#include <Arduino.h>
#include <HardwareSerial.h>
#include <string>

// Lokale Bibliotheks-Includes
#include "RS485SecureStack.h"
#include "credentials.h" // Enthält MASTER_KEY

// WICHTIG: Wählen Sie EINE der folgenden Zeilen, je nach Ihrem RS485-Modul:
// Option 1: Für Module MIT einem DE/RE-Pin, der manuell gesteuert werden muss (z.B. einfache MAX485-Module)
// #include "ManualDE_REDirectionControl.h" 

// Option 2: Für Module OHNE externen DE/RE-Pin (mit automatischer Flussrichtung, z.B. bestimmte HW-159/HW-519)
#include "AutomaticDirectionControl.h" 

// ==============================================================================
// GLOBAL KONFIGURATION (Monitor-spezifisch)
// ==============================================================================
#define MY_ADDRESS 254 // Eine Adresse, die nicht mit anderen Nodes kollidiert
#define INITIAL_KEY_ID 0 // Startet mit Key ID 0

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

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n--- RS485SecureStack Bus Monitor (Address %d) ---\n", MY_ADDRESS);

    // Initialisiere RS485SecureStack
    // Der Monitor hört nur zu, sendet aber nichts aktiv, muss aber trotzdem die Baudrate kennen.
    // Die myDirectionControl.begin() wird nun automatisch in rs485Stack.begin() aufgerufen
    rs485Stack.begin(MY_ADDRESS, MASTER_KEY, INITIAL_KEY_ID, rs485Serial);
    rs485Stack.registerReceiveCallback(onPacketReceived);
    rs485Stack.setDebug(true); // Debug-Ausgaben aktivieren

    Serial.println("Monitor: Initialisierung abgeschlossen. Warte auf Bus-Verkehr...");
}

void loop() {
    rs485Stack.loop(); // Empfängt Pakete und verarbeitet sie über den Callback
    // Der Monitor hat keine eigene Logik, außer zu lauschen.
}

// ==============================================================================
// Callback-Funktion für den RS485SecureStack
// ==============================================================================
void onPacketReceived(RS485SecureStack::Packet_t packet) {
    // Dieser Callback wird für jedes empfangene und (wenn möglich) entschlüsselte/authentifizierte Paket aufgerufen.
    // Der Monitor loggt einfach alles, was er sieht.

    Serial.println("\n--- Paket Empfangen ---");
    Serial.printf("  Nachrichtentyp: '%c'\n", packet.messageType);
    Serial.printf("  Zieladresse:     %d\n", packet.destinationAddress);
    Serial.printf("  Absenderadresse: %d\n", packet.senderAddress);
    Serial.printf("  Key ID:          %d\n", packet.keyId);
    Serial.printf("  Payload Länge:   %d\n", packet.payload.length());
    Serial.printf("  Payload (klar):  '%s'\n", packet.payload.c_str());
    Serial.printf("  HMAC geprüft:    %s\n", packet.hmacVerified ? "OK" : "FEHLER!");
    Serial.printf("  CRC geprüft:     %s\n", packet.crcVerified ? "OK" : "FEHLER!");
    Serial.printf("  Ist ACK/NACK:    %s\n", packet.isAck ? "Ja" : "Nein");
    Serial.println("-----------------------\n");

    // Wenn der Monitor die Baudrate ändern soll, wenn der Master dies tut:
    if (packet.messageType == MSG_TYPE_BAUD_RATE_SET && packet.senderAddress == 0) { // Master ist Adresse 0
        long newBaudRate = packet.payload.toInt();
        if (newBaudRate > 0) {
            Serial.printf("Monitor: Baudrate-Set vom Master empfangen. Passe eigene Baudrate auf %ld an.\n", newBaudRate);
            rs485Stack.setBaudRate(newBaudRate);
        }
    }
    // Wenn der Monitor den Schlüssel synchronisieren soll, wenn der Master dies tut:
    else if (packet.messageType == MSG_TYPE_KEY_UPDATE && packet.senderAddress == 0) {
        // Der Monitor muss den Master Key haben, um den neuen Session Key zu entschlüsseln.
        Serial.println("Monitor: Key-Update vom Master empfangen.");
        
        // Der hier implementierte `processKeyUpdate` ist nur ein Platzhalter.
        // In einer echten Anwendung müsste der Monitor die gleiche Logik wie Submaster/Client implementieren,
        // um den Schlüssel zu extrahieren, zu entschlüsseln und im eigenen Stack zu setzen,
        // um weiterhin Pakete lesen zu können.
        // Siehe die Implementierung in `submaster_main_esp32.ino` oder `client_main_esp32.ino`.

        // Beispiel einer vereinfachten Key-Extraktion/Entschlüsselung für den Monitor (nicht produktionsreif):
        // (Diese Logik müsste aus den anderen Sketches kopiert und hierher verlagert werden,
        // oder eine gemeinsame Hilfsfunktion erstellt werden.)
        
        // Payload ist JSON, z.B. {"keyID":1,"sessionKey":"base64_encoded_encrypted_key","iv":"hex_iv"}
        int keyIdIndex = packet.payload.indexOf("\"keyID\":");
        int sessionKeyIndex = packet.payload.indexOf("\"sessionKey\":\"");
        int ivIndex = packet.payload.indexOf("\"iv\":\"");
        
        if (keyIdIndex != -1 && sessionKeyIndex != -1 && ivIndex != -1) {
            uint8_t newKeyId = packet.payload.substring(keyIdIndex + 8).toInt();
            String encryptedKeyHex = packet.payload.substring(sessionKeyIndex + 14, packet.payload.indexOf("\"", sessionKeyIndex + 14));
            String ivHex = packet.payload.substring(ivIndex + 6, packet.payload.indexOf("\"", ivIndex + 6));

            if (encryptedKeyHex.length() == 64 && ivHex.length() == 32) {
                uint8_t encryptedSessionKey[32];
                uint8_t iv[16];

                for (int i = 0; i < 32; ++i) encryptedSessionKey[i] = (uint8_t)strtol(encryptedKeyHex.substring(i * 2, (i * 2) + 2).c_str(), NULL, 16);
                for (int i = 0; i < 16; ++i) iv[i] = (uint8_t)strtol(ivHex.substring(i * 2, (i * 2) + 2).c_str(), NULL, 16);

                AES256 aes256;
                uint8_t masterKeyHash[32];
                SHA256 sha256;
                sha256.reset();
                sha256.update(MASTER_KEY, strlen(MASTER_KEY));
                sha256.finalize(masterKeyHash, sizeof(masterKeyHash));

                aes256.setKey(masterKeyHash, aes256.keySize());
                aes256.setIV(iv, aes256.ivSize());
                aes256.decryptCBC(encryptedSessionKey, 32);

                if (rs485Stack.setSessionKey(newKeyId, encryptedSessionKey, sizeof(encryptedSessionKey))) {
                    rs485Stack.setCurrentKeyId(newKeyId);
                    Serial.printf("Monitor: Erfolgreich neuen Session Key (ID %d) gesetzt, um weiter mithören zu können.\n", newKeyId);
                } else {
                    Serial.println("ERR: Monitor konnte neuen Session Key nicht setzen.");
                }
            } else {
                Serial.println("ERR: Key Update Payload: Hex-Länge ungültig für Monitor.");
            }
        } else {
            Serial.println("ERR: Key Update Payload ungültig (JSON-Fehler) für Monitor.");
        }
    }
}

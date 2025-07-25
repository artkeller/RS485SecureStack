#ifndef RS485_SECURE_STACK_H
#define RS485_SECURE_STACK_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <SHA256.h> // Für HMAC-SHA256
#include <Crypto.h> // Für AES
#include <AES.h>    // Für AES

// Neu hinzugefügt für die Flussrichtungssteuerung
#include "RS485DirectionControl.h" 

// ==============================================================================
// KONFIGURATION
// ==============================================================================

// Maximale Paketgröße im Bytes (Header + IV + Payload + HMAC + CRC)
// Payload max 200 Bytes
#define MAX_PACKET_SIZE 256 

// Standard-Baudrate bei Start und für die Einmessung
#define RS485_INITIAL_BAUD_RATE 9600L

// Timeout für serielle Lesevorgänge in Millisekunden
#define SERIAL_TIMEOUT_MS 10 

// Max. Verzögerung nach TX-Enable und vor RX-Enable in Mikrosekunden
// WICHTIG: Diese Werte können je nach RS485-Transceiver variieren und müssen 
// ggf. an Ihre spezifische Hardware angepasst werden.
// Typische Werte liegen zwischen 100 und 200 Mikrosekunden.
#define RS485_TX_ENABLE_DELAY_US  150 // Verzögerung nach DE/RE HIGH, bevor Daten gesendet werden
#define RS485_TX_DISABLE_DELAY_US 150 // Verzögerung nach letztem Byte, bevor DE/RE LOW gesetzt wird


// ==============================================================================
// Ende KONFIGURATION
// ==============================================================================

// Enum für die Protokoll-Byte-Indizes im Header
enum RS485HeaderIndex {
    START_BYTE_0_INDEX = 0, // 0xDE
    START_BYTE_1_INDEX,     // 0xAD
    PROTOCOL_VERSION_INDEX, // 0x01
    TOTAL_LENGTH_INDEX,     // Länge des gesamten Pakets inkl. Header und CRC
    MESSAGE_TYPE_INDEX,     // Z.B. 'D' für Data, 'H' für Heartbeat, 'K' für Key
    DEST_ADDRESS_INDEX,     // Zieladresse
    SENDER_ADDRESS_INDEX,   // Absenderadresse
    KEY_ID_INDEX,           // ID des verwendeten Schlüssels
    // Ab hier beginnt der variabel lange Teil (IV und Payload), Länge wird in TOTAL_LENGTH_INDEX angegeben
    // (Der eigentliche Payload beginnt nach dem IV)
};

// Konstanten für feste Werte im Protokoll
const uint8_t RS485_START_BYTE_0 = 0xDE;
const uint8_t RS485_START_BYTE_1 = 0xAD;
const uint8_t RS485_PROTOCOL_VERSION = 0x01;
const uint8_t RS485_IV_LENGTH = 16;   // AES Blockgröße
const uint8_t RS485_HMAC_LENGTH = 32; // SHA256 Output

// Anwendungsdefinierte Message Types (Beispiele aus RS485SecureCom App)
// Können in der Anwendung neu definiert werden oder als Basis dienen
#define MSG_TYPE_MASTER_HEARTBEAT 'H'
#define MSG_TYPE_BAUD_RATE_SET    'B'
#define MSG_TYPE_KEY_UPDATE       'K'
#define MSG_TYPE_DATA             'D'
#define MSG_TYPE_ACK_NACK         'A' // Wird automatisch vom Stack gehandhabt bei requiresAck=true

class RS485SecureStack {
public:
    // Definition der Paketstruktur für den Callback
    // Die Payload ist hier bereits entschlüsselt und der HMAC geprüft.
    struct Packet_t {
        uint8_t totalLength;
        char messageType;
        uint8_t destinationAddress;
        uint8_t senderAddress;
        uint8_t keyId;
        String payload;
        bool requiresAck; // Ob diese Nachricht ein ACK erwartet
        bool isAck;       // Ob diese Nachricht selbst ein ACK/NACK ist
        // Ergänzung für Debugging/Monitoring:
        bool hmacVerified; // True, wenn HMAC korrekt war
        bool crcVerified;  // True, wenn CRC korrekt war
    };

    // Callback-Funktionstyp
    typedef void (*PacketReceivedCallback)(Packet_t packet);

    // NEU: Konstruktor, der ein RS485DirectionControl Objekt akzeptiert
    // Der Stack übernimmt die Verwaltung der Flussrichtung
    RS485SecureStack(RS485DirectionControl* directionControl = nullptr);

    // Initialisiert den Stack
    void begin(uint8_t myAddress, const char* masterKey, uint8_t initialKeyId, HardwareSerial& serial);

    // Hauptloop-Funktion zum Empfangen von Paketen
    void loop();

    // Registriert eine Callback-Funktion, die bei jedem empfangenen und validierten Paket aufgerufen wird
    void registerReceiveCallback(PacketReceivedCallback callback);

    // Sendet eine Nachricht. Gibt true zurück bei Erfolg (oder wenn kein ACK erforderlich ist), false bei Fehler.
    // Achtung: Bei requiresAck=true wartet diese Funktion auf ein ACK/NACK.
    bool sendMessage(uint8_t destinationAddress, uint8_t senderAddress, char messageType, const String& payload, bool requiresAck);

    // Setzt einen neuen Session Key für eine bestimmte Key ID
    bool setSessionKey(uint8_t keyId, const uint8_t* keyData, size_t keyLen);

    // Wechselt zur Verwendung eines neuen Schlüssels für ausgehende Nachrichten
    void setCurrentKeyId(uint8_t keyId);

    // Gibt die aktuell verwendete Key ID zurück
    uint8_t getCurrentKeyId() const { return _currentKeyId; }

    // Setzt die Baudrate der seriellen Schnittstelle
    void setBaudRate(long baudRate);

    // Gibt die aktuelle Baudrate zurück
    long getBaudRate() const { return _serial->baudRate(); }

    // Debugging: Setzt den Debug-Modus
    void setDebug(bool debug) { _debug = debug; }

private:
    HardwareSerial* _serial;
    uint8_t _myAddress;
    uint8_t _masterKey[32];      // SHA256-Hash des Master-Schlüssels
    uint8_t _sessionKeys[256][32]; // 256 mögliche Session Keys (Key ID 0-255)
    uint8_t _currentKeyId;       // Aktuell verwendete Key ID

    PacketReceivedCallback _packetReceivedCallback = nullptr;

    // Byte-Stuffing Puffer
    uint8_t _stuffedPacketBuffer[MAX_PACKET_SIZE * 2]; // Worst case 2x Größe für Stuffing
    uint8_t _unstuffedPacketBuffer[MAX_PACKET_SIZE];

    uint8_t _receiveBuffer[MAX_PACKET_SIZE * 2]; // Puffer für eingehende gestuffte Bytes
    size_t _receiveBufferPos = 0;

    // Neu: Zeiger auf das DirectionControl-Objekt
    RS485DirectionControl* _directionControl; 

    bool _debug = false; // Debug-Ausgaben aktivieren/deaktivieren

    // Hilfsfunktionen
    void _resetReceiveBuffer();
    bool _isStartByte(uint8_t byte);
    bool _extractPacket();
    uint16_t _calculateCRC16(const uint8_t* data, size_t length);
    void _generateIV(uint8_t* iv);
    void _encryptAES(uint8_t* data, size_t len, const uint8_t* key, const uint8_t* iv);
    void _decryptAES(uint8_t* data, size_t len, const uint8_t* key, const uint8_t* iv);
    void _calculateHMAC(const uint8_t* key, const uint8_t* data, size_t dataLen, uint8_t* hmacResult);
    size_t _byteStuff(const uint8_t* source, size_t sourceLen, uint8_t* destination);
    size_t _byteUnstuff(const uint8_t* source, size_t sourceLen, uint8_t* destination);
    bool _sendAck(uint8_t destinationAddress, uint8_t senderAddress, uint8_t keyId);
    bool _sendNack(uint8_t destinationAddress, uint8_t senderAddress, uint8_t keyId, const char* reason);
    bool _waitForAck(long timeoutMs); // Wartet auf ein ACK/NACK
};

#endif // RS485_SECURE_STACK_H

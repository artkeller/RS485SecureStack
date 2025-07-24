#ifndef RS485_SECURE_STACK_H
#define RS485_SECURE_STACK_H

#include <Arduino.h>
#include <HardwareSerial.h>
// Include spezifische Crypto Bibliotheken für ESP32 Hardware-Beschleunigung
#include <Crypto.h>
#include <AES.h>    // Für AES128
#include <SHA256.h> // Für SHA256 Hashes
#include <HMAC.h>   // Für HMAC-SHA256

// --- Forward-Deklaration der KeyRotationManager Klasse ---
// Wird benötigt, da RS485SecureStack einen Zeiger auf diese Klasse als Member hat,
// aber KeyRotationManager auch RS485SecureStack als Zeiger haben kann.
class KeyRotationManager; 

// --- Define RS485 Protocol Constants ---
// Diese Konstanten definieren das physikalische Rahmenprotokoll
#define START_BYTE 0xAA      // Startkennzeichen eines Pakets
#define END_BYTE 0x55        // Endkennzeichen eines Pakets
#define ESCAPE_BYTE 0xBB     // Escape-Zeichen für Byte-Stuffing
#define ESCAPE_XOR_MASK 0x20 // XOR-Maske für escaped Bytes (z.B. 0xAA ^ 0x20)

// Message Types
// Ein-Byte-Charakter-Token für effizienten Header
#define MSG_TYPE_DATA_TOKEN 'D' // Generische Daten-Nachricht
#define MSG_TYPE_ACK_TOKEN 'A'  // Bestätigung (Acknowledgment)
#define MSG_TYPE_NACK_TOKEN 'N' // Negative Bestätigung (Negative Acknowledgment)
#define MSG_TYPE_MASTER_HEARTBEAT_TOKEN 'H' // Master's Heartbeat zur Anwesenheitserkennung
#define MSG_TYPE_BAUD_RATE_SET_TOKEN 'B'    // Master setzt neue Baudrate
#define MSG_TYPE_KEY_UPDATE_TOKEN 'K'       // Master verteilt neuen Session Key

// --- Security Parameters (feste Größen für AES-128 und HMAC-SHA256) ---
#define AES_KEY_SIZE 16         // 128-bit AES Schlüssel (in Bytes)
#define HMAC_KEY_SIZE 32        // 256-bit HMAC Schlüssel (Master Key, in Bytes)
#define HMAC_TAG_SIZE 32        // SHA256 Output-Größe für HMAC (in Bytes)
#define AES_BLOCK_SIZE 16       // AES Blockgröße (in Bytes)
#define IV_SIZE AES_BLOCK_SIZE  // Initialisierungsvektor (IV) Größe für AES-CBC (in Bytes)

// --- Protokoll- und Puffergrößen Berechnung ---
// Header-Größe: Source (1) + Target (1) + MsgType (1) + KeyID (2) + IV (16) = 21 Bytes
#define HEADER_SIZE (1 + 1 + 1 + 2 + IV_SIZE) // 21 Bytes

// Definierte maximale unverschlüsselte Nutzlast (String etc.) die vom Nutzer gesendet werden kann.
// Dies ist die *maximale Länge des Strings/Byte-Arrays*, das du verschlüsseln möchtest.
#define MAX_RAW_PAYLOAD_SIZE 200 // Beispiel: 200 Bytes maximale Nutzlast

// Maximale Größe der verschlüsselten Nutzlast nach PKCS7-Padding.
// Muss ein Vielfaches der AES_BLOCK_SIZE sein.
// Formel: ((RAW_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE
#define MAX_PADDED_ENCRYPTED_PAYLOAD_SIZE \
    ((MAX_RAW_PAYLOAD_SIZE + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE * AES_BLOCK_SIZE)

// Maximale Größe des *Logischen Pakets* (vor Byte-Stuffing)
// = Header + MAX_PADDED_ENCRYPTED_PAYLOAD_SIZE + HMAC_TAG_SIZE
#define MAX_LOGICAL_PACKET_SIZE (HEADER_SIZE + MAX_PADDED_ENCRYPTED_PAYLOAD_SIZE + HMAC_TAG_SIZE)

// Maximale Größe des *Physikalischen Pakets* nach Byte-Stuffing im Worst-Case.
// Im Worst-Case kann jedes Byte im logischen Paket escaped werden (Verdopplung).
// Plus die 2 zusätzlichen Rahmen-Bytes (START_BYTE, END_BYTE).
#define MAX_PHYSICAL_PACKET_SIZE (MAX_LOGICAL_PACKET_SIZE * 2 + 2) // * 2 für Stuffing, + 2 für Start/End Bytes

// Maximale Größe der Puffer für eingehende/ausgehende gestuffte Daten
#define RECEIVE_BUFFER_SIZE MAX_PHYSICAL_PACKET_SIZE
#define SEND_BUFFER_SIZE MAX_PHYSICAL_PACKET_SIZE

// Maximale Anzahl von Session Keys, die im Pool gespeichert werden können.
// Ermöglicht Rekeying und das Speichern mehrerer Schlüssel.
#define MAX_SESSION_KEYS 5 

// --- Callback function types ---
// Typdefinition für die Callback-Funktion, die aufgerufen wird, wenn ein
// gültiges, entschlüsseltes Paket empfangen wurde.
typedef void (*ReceiveCallback)(byte senderAddr, char msgType, const String& payload);

class RS485SecureStack {
public:
    // Konstruktor: Nimmt eine Referenz auf die HardwareSerial, die lokale Adresse
    // und den Master Authentication Key entgegen.
    // Der masterKey ist ein Array der Größe HMAC_KEY_SIZE (32 Bytes).
    RS485SecureStack(HardwareSerial& serial, byte localAddress, const byte masterKey[HMAC_KEY_SIZE]);

    // Initialisiert die RS485 Hardware Serial und interne Puffer.
    void begin(long baudRate);

    // Setzt den GPIO-Pin für die DE/RE-Steuerung des RS485-Transceivers.
    // Dieser Pin muss im Anwendungs-Sketch konfiguriert und verwaltet werden.
    void setDeRePin(int pin);

    // Verarbeitet eingehende Bytes vom RS485 Bus. Muss häufig in loop() aufgerufen werden.
    void processIncoming();

    // Sendet eine verschlüsselte und authentifizierte Nachricht an eine Zieladresse.
    // targetAddress: Zielknoten-Adresse (0xFF für Broadcast).
    // msgType: Typ der Nachricht (z.B. MSG_TYPE_DATA).
    // payload: Die zu sendende Nutzlast als String.
    // Gibt true bei Erfolg zurück, false bei Fehler (z.B. Puffer voll).
    bool sendMessage(byte targetAddress, char msgType, const String& payload);

    // Registriert eine Callback-Funktion, die aufgerufen wird,
    // wenn eine gültige, entschlüsselte Nachricht empfangen wurde.
    void registerReceiveCallback(ReceiveCallback callback);

    // Sendet ein ACK (Acknowledgment) oder NACK (Negative Acknowledgment)
    // als Antwort auf eine empfangene Nachricht.
    // targetAddress: An den ursprünglichen Absender.
    // originalMsgType: Der Typ der Nachricht, auf die geantwortet wird (für Kontext).
    // success: true für ACK, false für NACK.
    bool sendAckNack(byte targetAddress, char originalMsgType, bool success);
    
    // Ermöglicht das dynamische Ändern der Baudrate der RS485-Schnittstelle.
    void _setBaudRate(long newBaudRate); 

    // Ermöglicht die Steuerung, ob dieser Knoten ACKs/NACKs sendet.
    void setAckEnabled(bool enabled);

    // Setzt den aktuell zu verwendenden Session Key aus dem internen Pool.
    // keyId: Die ID des zu aktivierenden Schlüssels.
    void setCurrentKeyId(uint16_t keyId);

    // Speichert einen neuen Session Key im internen Pool unter der gegebenen ID.
    // Dies wird hauptsächlich vom Master verwendet, um neue Schlüssel zu verteilen,
    // oder von Clients, um neue Schlüssel vom Master zu empfangen und zu speichern.
    void setSessionKey(uint16_t keyId, const byte sessionKey[AES_KEY_SIZE]);

    // NEU: Setzt den KeyRotationManager für diese Instanz
    void setKeyRotationManager(KeyRotationManager* manager);

    // --- Klassenmember für Nachrichtentypen (saubere C++-Konstanten) ---
    // Diese sind public zugänglich (z.B. RS485SecureStack::MSG_TYPE_DATA)
    // und leiten ihre Werte von den oben definierten #define TOKENs ab.
    static const char MSG_TYPE_DATA = MSG_TYPE_DATA_TOKEN;
    static const char MSG_TYPE_ACK = MSG_TYPE_ACK_TOKEN;
    static const char MSG_TYPE_NACK = MSG_TYPE_NACK_TOKEN;
    static const char MSG_TYPE_MASTER_HEARTBEAT = MSG_TYPE_MASTER_HEARTBEAT_TOKEN;
    static const char MSG_TYPE_BAUD_RATE_SET = MSG_TYPE_BAUD_RATE_SET_TOKEN;
    static const char MSG_TYPE_KEY_UPDATE = MSG_TYPE_KEY_UPDATE_TOKEN;

    // --- Public zugängliche Statusvariablen (für Debugging/Monitoring) ---
    uint16_t _currentSessionKeyId; // Aktuell verwendete Session Key ID
    byte _currentPacketSource;     // Quelladresse des zuletzt verarbeiteten Pakets
    byte _currentPacketTarget;     // Zieladresse des zuletzt verarbeiteten Pakets
    char _currentPacketMsgType;    // Nachrichtentyp des zuletzt verarbeiteten Pakets
    byte _currentPacketIV[IV_SIZE]; // IV des zuletzt verarbeiteten Pakets (für Debugging)
    bool _hmacVerified;            // Flag: War der HMAC des letzten Pakets gültig?
    bool _checksumVerified;        // Flag: War eine eventuelle Checksumme gültig? (derzeit immer true)

private:
    HardwareSerial& _rs485Serial; // Referenz auf die Hardware-Serial Instanz (z.B. Serial1)
    byte _localAddress;           // Die eindeutige Adresse dieses Knotens auf dem Bus
    byte _masterKey[HMAC_KEY_SIZE]; // Der Pre-Shared Master Authentication Key (MAK) für HMAC

    // Krypto-Objekte als direkte Member, Nutzung der ESP32 Hardware-Beschleunigung
    AES128 _aes;     // AES128 Objekt für Ver- und Entschlüsselung
    SHA256 _sha256;  // SHA256 Objekt, primär für Key Derivation und intern von HMAC genutzt
    HMAC<SHA256> _hmac; // HMAC Objekt für Message Authentication Code Berechnung/Verifikation

    // Session Key Management
    // Pool für Session Keys. Key-ID 0 wird für den initialen, vom MAK abgeleiteten Schlüssel genutzt.
    byte _sessionKeyPool[MAX_SESSION_KEYS][AES_KEY_SIZE]; 
    byte _currentSessionKey[AES_KEY_SIZE]; // Der aktuell aktive AES-Schlüssel

    ReceiveCallback _receiveCallback; // Zeiger auf die registrierte Callback-Funktion

    // RS485 Transceiver Control Pin
    int _deRePin; // GPIO Pin für DE/RE (Data Enable / Receive Enable)

    // Empfangs-Puffer und Zustandsmaschinen-Variablen für die Paketverarbeitung
    byte _receiveBuffer[RECEIVE_BUFFER_SIZE]; // Puffer für eingehende, gestuffte Bytes
    size_t _receiveBufferIdx;                 // Aktueller Index im Empfangspuffer

#ifndef RS485_SECURE_STACK_H
#define RS485_SECURE_STACK_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Crypto.h> // Common Crypto API for ESP32
#include <AES.h>    // AES implementation (often integrated with Crypto for ESP32)
#include <SHA256.h> // SHA256 for HMAC (often integrated with Crypto for ESP32)
#include <HMAC.h>   // HMAC implementation

// --- Define RS485 Protocol Constants ---
#define START_BYTE 0xAA      // Preamble start byte
#define END_BYTE 0x55        // Preamble end byte
#define ESCAPE_BYTE 0xBB     // Escape byte for byte stuffing

// Message Types
// Single characters to keep header small
#define MSG_TYPE_DATA_TOKEN 'D' // Generic data message
#define MSG_TYPE_ACK_TOKEN 'A'  // Acknowledgment
#define MSG_TYPE_NACK_TOKEN 'N' // Negative Acknowledgment
#define MSG_TYPE_MASTER_HEARTBEAT_TOKEN 'H' // Master's heartbeat message
#define MSG_TYPE_BAUD_RATE_SET_TOKEN 'B'    // Master sets new baud rate
#define MSG_TYPE_KEY_UPDATE_TOKEN 'K'       // Master pushes a new session key

// --- Security Parameters ---
#define AES_KEY_SIZE 16 // 128-bit AES key
#define HMAC_KEY_SIZE 32 // 256-bit HMAC key (Master Key)
#define HMAC_TAG_SIZE 32 // SHA256 output size
#define AES_BLOCK_SIZE 16 // AES block size
#define IV_SIZE AES_BLOCK_SIZE // IV size for AES-CBC

//#define MAX_PAYLOAD_SIZE 128 // Max bytes for encrypted payload (plus padding)
#define MAX_PAYLOAD_SIZE (MAX_PACKET_SIZE - HEADER_SIZE - HMAC_TAG_SIZE - AES_BLOCK_SIZE)
#define MAX_PACKET_SIZE (MAX_PAYLOAD_SIZE * 2 + 32) // Max raw packet size (worst case byte stuffing + header/HMAC)

// --- Callback function types ---
// The callback function will receive sender address, message type, and the DECRYPTED payload.
typedef void (*ReceiveCallback)(byte senderAddr, char msgType, const String& payload);

class RS485SecureStack {
public:
    // Constructor: Takes HardwareSerial reference, local address, and master key
    RS485SecureStack(HardwareSerial& serial, byte localAddress, const byte masterKey[HMAC_KEY_SIZE]);

    // Initializes the RS485 hardware serial and internal buffers
    void begin(long baudRate);

    // Processes incoming bytes from the RS485 bus. Call frequently in loop().
    void processIncoming();

    // Sends a message to a specific target address
    // Returns true on success, false otherwise (e.g., buffer full)
    bool sendMessage(byte targetAddress, char msgType, const String& payload);

    // Registers a callback function to be called when a valid, decrypted message is received
    void registerReceiveCallback(ReceiveCallback callback);

    // Sends an ACK or NACK in response to a received message
    bool sendAckNack(byte targetAddress, char originalMsgType, bool success);

    // --- Internal/Utility functions exposed for debugging/monitoring ---
    void _setBaudRate(long newBaudRate); // Allows changing baud rate dynamically
    uint16_t _currentSessionKeyId; // Publicly accessible to monitor current key ID
    byte _currentPacketSource; // Source address of the last processed packet
    byte _currentPacketTarget; // Target address of the last processed packet
    char _currentPacketMsgType; // Message type of the last processed packet
    byte _currentPacketIV[IV_SIZE]; // IV of the last processed packet (for debugging)
    
    // For monitor, to check HMAC/Checksum status
    bool _hmacVerified;
    bool _checksumVerified;
    
    // Enable/Disable ACK/NACK sending for this node
    void setAckEnabled(bool enabled);

    // Set the current session key ID and key (Master only or after Key_Update)
    void setCurrentKeyId(uint16_t keyId);
    void setSessionKey(uint16_t keyId, const byte sessionKey[AES_KEY_SIZE]);


    static const char MSG_TYPE_DATA = MSG_TYPE_DATA_TOKEN;
    static const char MSG_TYPE_ACK = MSG_TYPE_ACK_TOKEN;
    static const char MSG_TYPE_NACK = MSG_TYPE_NACK_TOKEN;
    static const char MSG_TYPE_MASTER_HEARTBEAT = MSG_TYPE_MASTER_HEARTBEAT_TOKEN;
    static const char MSG_TYPE_BAUD_RATE_SET = MSG_TYPE_BAUD_RATE_SET_TOKEN;
    static const char MSG_TYPE_KEY_UPDATE = MSG_TYPE_KEY_UPDATE_TOKEN;

private:
    HardwareSerial& _rs485Serial;
    byte _localAddress;
    byte _masterKey[HMAC_KEY_SIZE]; // The pre-shared master key (for HMAC)

    AES _aes;
    SHA256 _sha256;
    HMAC _hmac;

    // Current session key for AES encryption/decryption
    byte _currentSessionKey[AES_KEY_SIZE];
    
    ReceiveCallback _receiveCallback;

    // Receive buffer and state machine variables
    byte _receiveBuffer[MAX_PACKET_SIZE];
    size_t _receiveBufferIdx;
    bool _receivingPacket;
    bool _escapeNextByte;
    
    // Packet parsing state variables
    byte _currentPacketRaw[MAX_PACKET_SIZE]; // Raw received packet
    size_t _currentPacketRawLen;

    // --- Internal helper functions ---
    void sendRaw(const byte* data, size_t len);
    
    // Encapsulates data into a secure packet
    // Returns true if packet was sent, false otherwise
    bool buildAndSendPacket(byte targetAddress, char msgType, const byte* payload, size_t payloadLen, uint16_t keyId, const byte iv[IV_SIZE]);
    
    // Packet parsing and validation
    void processReceivedPacket(const byte* packet, size_t len);
    
    // Encryption/Decryption
    size_t encryptPayload(const byte* plain, size_t plainLen, byte* encrypted, const byte key[AES_KEY_SIZE], byte iv[IV_SIZE]);
    size_t decryptPayload(const byte* encrypted, size_t encryptedLen, byte* decrypted, const byte key[AES_KEY_SIZE], const byte iv[IV_SIZE]);

    // Byte stuffing/unstuffing
    size_t stuffBytes(const byte* input, size_t inputLen, byte* output);
    size_t unstuffBytes(const byte* input, size_t inputLen, byte* output);

    bool _ackEnabled; // Flag to control if this node sends ACKs/NACKs
    
    // Internal session key storage (for master to manage, for clients to react)
    // For a real system, these would be managed more robustly and securely.
    // Here, just a simple example for dynamic rekeying
    byte _sessionKeyPool[2][AES_KEY_SIZE]; // Store current and next key, for example
};

#endif // RS485_SECURE_STACK_H

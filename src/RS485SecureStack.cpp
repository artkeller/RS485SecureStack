#if 0
#include "RS485SecureStack.h"
#include "Crypto.h" // Ensure Crypto.h is included for AES/HMAC context
#endif

#include "RS485SecureStack.h"
#include <HardwareSerial.h>
#include <Crypto.h> // Für SHA256
#include <AES.h>
#include <HMAC.h>
#include <SHA256.h> // Explizit für die Key-Ableitung

// Define MAX_PAYLOAD_SIZE if not already defined (e.g., in .h)
// #define MAX_PAYLOAD_SIZE (MAX_PACKET_SIZE - HEADER_SIZE - HMAC_TAG_SIZE - AES_BLOCK_SIZE)

// Initialize the ESP32 hardware crypto engines
AES128 aes128; // Using AES128 for 128-bit key
SHA256 sha256_hasher;
HMAC<SHA256> hmac_engine;

RS485SecureStack::RS485SecureStack(HardwareSerial& serial, byte localAddress, const byte masterKey[HMAC_KEY_SIZE])
    : _rs485Serial(serial),
      _localAddress(localAddress),
      _receiveBufferIdx(0),
      _receivingPacket(false),
      _escapeNextByte(false),
      _receiveCallback(nullptr),
      _ackEnabled(true),
      _currentSessionKeyId(0),
      _hmacVerified(false),
      _checksumVerified(false),
      _currentPacketSource(0),
      _currentPacketTarget(0),
      _currentPacketMsgType(0)
#if 0
{
    memcpy(_masterKey, masterKey, HMAC_KEY_SIZE);
    
    // Initialize session key pool with default (could be derived from MASTER_KEY or a hardcoded first key)
    // For PoC, let's just use a hardcoded initial session key
    const byte initialSessionKey[AES_KEY_SIZE] = {
        0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
        0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32
    };
    memcpy(_sessionKeyPool[0], initialSessionKey, AES_KEY_SIZE);
    memcpy(_currentSessionKey, initialSessionKey, AES_KEY_SIZE); // Set initial active key
}
#endif

{
    // Initialisiere den Master Key
    memcpy(_masterKey, masterKey, HMAC_KEY_SIZE);

    // Initialisiere Krypto-Objekte
    _aes = new AESCrypto;
    _hmac = new Hmac;
    _sha256 = new SHA256; // Benötigt für die Ableitung des initialen Session Keys

    // Initialisiere den Session Key Pool
    for (int i = 0; i < MAX_SESSION_KEYS; ++i) {
        memset(_sessionKeyPool[i], 0, AES_KEY_SIZE); // Alle Schlüssel zunächst mit Nullen initialisieren
    }

    // *** Wichtige Änderung hier: Ableiten des initialen Session Keys aus dem Master Key ***
    // Nutze SHA256 als einfache KDF (Key Derivation Function)
    byte derivedKey[HMAC_KEY_SIZE]; // SHA256 erzeugt 32 Bytes
    _sha256->reset();
    _sha256->update(_masterKey, HMAC_KEY_SIZE); // Hash über den Master Key
    _sha256->finalize(derivedKey, HMAC_KEY_SIZE);

    // Setze den ersten Session Key (Key ID 0) auf die ersten 16 Bytes des abgeleiteten Schlüssels
    memcpy(_sessionKeyPool[0], derivedKey, AES_KEY_SIZE);
    
    // Setze den aktuellen Session Key auf den soeben abgeleiteten
    // _aes->setKey(_sessionKeyPool[0], _aes->keySize()); // Dies geschieht in setCurrentKeyId(0) oder wenn ein Packet ankommt

    // Registriere den initialen Schlüssel im Pool und setze ihn als aktiv
    setSessionKey(0, _sessionKeyPool[0]); // Session Key 0 ist jetzt der abgeleitete Schlüssel
    setCurrentKeyId(0); // Setze den abgeleiteten Schlüssel als aktuellen aktiven Schlüssel
}

void RS485SecureStack::begin(long baudRate) {
    _rs485Serial.begin(baudRate);
    // On ESP32, you might need to configure RX/TX pins if not using default UART0
    // e.g., _rs485Serial.setPins(RX_PIN, TX_PIN);
    // Also, if you have a DE/RE pin for the RS485 transceiver, you'd manage it here:
    // pinMode(DE_RE_PIN, OUTPUT);
    // digitalWrite(DE_RE_PIN, LOW); // Set to receive mode
    Serial.print("RS485SecureStack started on ");
    Serial.print(_rs485Serial.baudRate());
    Serial.print(" bps, Address: ");
    Serial.println(_localAddress);
}

void RS485SecureStack::_setBaudRate(long newBaudRate) {
    _rs485Serial.updateBaudRate(newBaudRate);
    Serial.print("Baud rate updated to: ");
    Serial.println(_rs485Serial.baudRate());
}

void RS485SecureStack::processIncoming() {
    while (_rs485Serial.available()) {
        byte inByte = _rs485Serial.read();

        if (!_receivingPacket) {
            // Looking for START_BYTE
            if (inByte == START_BYTE) {
                _receivingPacket = true;
                _receiveBufferIdx = 0;
                _escapeNextByte = false;
            }
            continue;
        }

        // Handle escape byte
        if (inByte == ESCAPE_BYTE) {
            _escapeNextByte = true;
            continue;
        }

        // Handle END_BYTE
        if (inByte == END_BYTE) {
            if (!_escapeNextByte) { // If not escaped, end of packet
                _receivingPacket = false;
                if (_receiveBufferIdx > 0) {
                    processReceivedPacket(_receiveBuffer, _receiveBufferIdx);
                }
                _receiveBufferIdx = 0; // Reset for next packet
                continue;
            }
            // If escaped, treat as normal byte
        }

        // Store byte in buffer
        if (_receiveBufferIdx < MAX_PACKET_SIZE) {
            if (_escapeNextByte) {
                // If it was an escaped byte, just store the original byte value
                _receiveBuffer[_receiveBufferIdx++] = inByte;
                _escapeNextByte = false;
            } else {
                // Store normal byte
                _receiveBuffer[_receiveBufferIdx++] = inByte;
            }
        } else {
            // Buffer overflow - discard packet
            Serial.println("Receive buffer overflow, discarding packet.");
            _receivingPacket = false;
            _receiveBufferIdx = 0;
        }
    }
}

void RS485SecureStack::sendRaw(const byte* data, size_t len) {
    // If DE/RE pin is used, set to transmit mode
    // digitalWrite(DE_RE_PIN, HIGH);
    _rs485Serial.write(data, len);
    _rs485Serial.flush(); // Ensure all bytes are sent
    // Delay to ensure last byte is sent before switching to receive
    // delayMicroseconds(len * 10 * 1000000 / _rs485Serial.baudRate()); // 10 bits per byte
    // Then set back to receive mode
    // digitalWrite(DE_RE_PIN, LOW);
}

bool RS485SecureStack::sendMessage(byte targetAddress, char msgType, const String& payload) {
    // Current payload size limits are based on AES block size for padding
    size_t plainLen = payload.length();
    
    // Generate a random IV for CBC
    byte iv[IV_SIZE];
    for (int i = 0; i < IV_SIZE; i++) {
        iv[i] = random(256); // Use a proper random source in production
    }

    // Encrypt payload
    byte encryptedPayload[MAX_PAYLOAD_SIZE]; // Size after encryption
    size_t encryptedLen = encryptPayload((byte*)payload.c_str(), plainLen, encryptedPayload, _currentSessionKey, iv);

    if (encryptedLen == 0) {
        Serial.println("Encryption failed or payload too large.");
        return false;
    }
    
    return buildAndSendPacket(targetAddress, msgType, encryptedPayload, encryptedLen, _currentSessionKeyId, iv);
}

bool RS485SecureStack::sendAckNack(byte targetAddress, char originalMsgType, bool success) {
    if (!_ackEnabled) return false;

    // ACK/NACK payload indicates original message type and result
    String ackNackPayload = String(originalMsgType) + (success ? "1" : "0");
    
    // Generate a random IV for CBC (even for small payloads)
    byte iv[IV_SIZE];
    for (int i = 0; i < IV_SIZE; i++) {
        iv[i] = random(256); // Use a proper random source in production
    }
    
    byte encryptedPayload[AES_BLOCK_SIZE]; // Smallest possible encrypted size is AES_BLOCK_SIZE
    size_t encryptedLen = encryptPayload((byte*)ackNackPayload.c_str(), ackNackPayload.length(), encryptedPayload, _currentSessionKey, iv);

    if (encryptedLen == 0) {
        Serial.println("ACK/NACK encryption failed.");
        return false;
    }

    return buildAndSendPacket(targetAddress, (success ? MSG_TYPE_ACK : MSG_TYPE_NACK), encryptedPayload, encryptedLen, _currentSessionKeyId, iv);
}


bool RS485SecureStack::buildAndSendPacket(byte targetAddress, char msgType, const byte* encryptedPayload, size_t encryptedLen, uint16_t keyId, const byte iv[IV_SIZE]) {
    // Packet structure:
    // Header (1 byte source, 1 byte target, 1 byte msgType, 2 bytes keyId, IV_SIZE bytes IV)
    // Encrypted Payload (variable length, padded to AES_BLOCK_SIZE)
    // HMAC (HMAC_TAG_SIZE bytes)
    // Total size before stuffing
    size_t headerLen = 1 + 1 + 1 + 2 + IV_SIZE; // Source, Target, MsgType, KeyId (2 bytes), IV
    size_t dataLen = headerLen + encryptedLen;
    size_t totalPacketLen = dataLen + HMAC_TAG_SIZE;

    if (totalPacketLen > MAX_PACKET_SIZE) {
        Serial.println("Packet too large to send.");
        return false;
    }

    byte tempPacket[totalPacketLen];
    size_t currentIdx = 0;

    // 1. Build Header
    tempPacket[currentIdx++] = _localAddress;
    tempPacket[currentIdx++] = targetAddress;
    tempPacket[currentIdx++] = (byte)msgType;
    tempPacket[currentIdx++] = (byte)(keyId >> 8);   // Key ID MSB
    tempPacket[currentIdx++] = (byte)(keyId & 0xFF); // Key ID LSB
    memcpy(&tempPacket[currentIdx], iv, IV_SIZE);
    currentIdx += IV_SIZE;

    // 2. Add Encrypted Payload
    memcpy(&tempPacket[currentIdx], encryptedPayload, encryptedLen);
    currentIdx += encryptedLen;

    // 3. Calculate HMAC over (Header + Encrypted Payload) using Master Key
    hmac_engine.setKey(_masterKey, HMAC_KEY_SIZE);
    hmac_engine.update(tempPacket, dataLen);
    byte hmacTag[HMAC_TAG_SIZE];
    hmac_engine. finalize(hmacTag, HMAC_TAG_SIZE);

    // 4. Add HMAC Tag
    memcpy(&tempPacket[currentIdx], hmacTag, HMAC_TAG_SIZE);
    currentIdx += HMAC_TAG_SIZE; // currentIdx is now totalPacketLen

    // 5. Byte Stuffing
    byte stuffedPacket[totalPacketLen * 2 + 2]; // Worst case: all bytes escaped + start/end
    stuffedPacket[0] = START_BYTE;
    size_t stuffedLen = stuffBytes(tempPacket, totalPacketLen, &stuffedPacket[1]);
    stuffedPacket[1 + stuffedLen] = END_BYTE;

    sendRaw(stuffedPacket, stuffedLen + 2); // Send with start and end bytes
    return true;
}


void RS485SecureStack::processReceivedPacket(const byte* rawPacket, size_t rawLen) {
    _currentPacketRawLen = rawLen;
    memcpy(_currentPacketRaw, rawPacket, rawLen); // Store raw for debug if needed

    // Clear previous verification flags
    _hmacVerified = false;
    _checksumVerified = true; // Assume true if we got here (CRC not explicit in this simplified model yet)

    if (rawLen < (1 + 1 + 1 + 2 + IV_SIZE + HMAC_TAG_SIZE)) { // Minimum packet size
        Serial.println("Packet too short.");
        return;
    }

    size_t currentIdx = 0;

    // Extract Header
    _currentPacketSource = rawPacket[currentIdx++];
    _currentPacketTarget = rawPacket[currentIdx++];
    _currentPacketMsgType = (char)rawPacket[currentIdx++];
    uint16_t receivedKeyId = (uint16_t)rawPacket[currentIdx] << 8 | rawPacket[currentIdx + 1];
    currentIdx += 2;
    memcpy(_currentPacketIV, &rawPacket[currentIdx], IV_SIZE);
    currentIdx += IV_SIZE;

    size_t dataLen = rawLen - HMAC_TAG_SIZE; // Header + Encrypted Payload
    size_t encryptedPayloadLen = dataLen - (1 + 1 + 1 + 2 + IV_SIZE);

    // 1. Verify HMAC
    byte receivedHmac[HMAC_TAG_SIZE];
    memcpy(receivedHmac, &rawPacket[dataLen], HMAC_TAG_SIZE);

    hmac_engine.setKey(_masterKey, HMAC_KEY_SIZE);
    hmac_engine.update(rawPacket, dataLen); // HMAC over header + encrypted payload
    byte calculatedHmac[HMAC_TAG_SIZE];
    hmac_engine.finalize(calculatedHmac, HMAC_TAG_SIZE);

    if (memcmp(receivedHmac, calculatedHmac, HMAC_TAG_SIZE) != 0) {
        Serial.println("HMAC Mismatch - Packet integrity compromised or wrong key.");
        _hmacVerified = false;
        return;
    }
    _hmacVerified = true;

    // 2. Check Target Address
    if (_currentPacketTarget != _localAddress && _currentPacketTarget != 0xFF) { // 0xFF for broadcast
        // Not for me and not a broadcast. Discard after HMAC check.
        // Serial.println("Packet not for this address.");
        return;
    }

    // 3. Check Key ID
    if (receivedKeyId != _currentSessionKeyId) {
        // If not the current key, could be an old or future key.
        // For a simple PoC, we only process with the current key.
        // In a real system, might buffer and try previous/next keys.
        Serial.print("Warning: Received packet with unexpected Key ID (expected ");
        Serial.print(_currentSessionKeyId);
        Serial.print(", got ");
        Serial.print(receivedKeyId);
        Serial.println("). Discarding payload.");
        // Still call callback with empty payload or error status if needed
        if (_receiveCallback) {
             _receiveCallback(_currentPacketSource, _currentPacketMsgType, "KEY_MISMATCH");
        }
        return;
    }
    
    // 4. Decrypt Payload
    byte decryptedPayload[MAX_PAYLOAD_SIZE];
    size_t decryptedLen = decryptPayload(&rawPacket[currentIdx], encryptedPayloadLen, decryptedPayload, _currentSessionKey, _currentPacketIV);

    if (decryptedLen == 0) {
        Serial.println("Decryption failed or invalid padding.");
        return;
    }

    // Null-terminate the decrypted payload for String conversion
    decryptedPayload[decryptedLen] = '\0';
    String payloadStr = (char*)decryptedPayload;

    // 5. Call Callback
    if (_receiveCallback) {
        _receiveCallback(_currentPacketSource, _currentPacketMsgType, payloadStr);
    }
}

void RS485SecureStack::registerReceiveCallback(ReceiveCallback callback) {
    _receiveCallback = callback;
}

size_t RS485SecureStack::encryptPayload(const byte* plain, size_t plainLen, byte* encrypted, const byte key[AES_KEY_SIZE], byte iv[IV_SIZE]) {
    // AES in CBC mode requires padding to block size
    size_t paddedLen = plainLen;
    if (paddedLen % AES_BLOCK_SIZE != 0) {
        paddedLen = (plainLen / AES_BLOCK_SIZE + 1) * AES_BLOCK_SIZE;
    }
    
    // PKCS7 padding
    byte paddedPlain[paddedLen];
    memcpy(paddedPlain, plain, plainLen);
    byte paddingByte = paddedLen - plainLen;
    for (size_t i = plainLen; i < paddedLen; i++) {
        paddedPlain[i] = paddingByte;
    }

    aes128.setKey(key, AES_KEY_SIZE);
    aes128.setIV(iv, IV_SIZE);
    aes128.encrypt(encrypted, paddedPlain, paddedLen);

    return paddedLen;
}

size_t RS485SecureStack::decryptPayload(const byte* encrypted, size_t encryptedLen, byte* decrypted, const byte key[AES_KEY_SIZE], const byte iv[IV_SIZE]) {
    if (encryptedLen == 0 || encryptedLen % AES_BLOCK_SIZE != 0) {
        return 0; // Invalid length for AES
    }

    aes128.setKey(key, AES_KEY_SIZE);
    aes128.setIV(iv, IV_SIZE);
    aes128.decrypt(decrypted, encrypted, encryptedLen);

    // PKCS7 unpadding
    if (encryptedLen == 0) return 0; // Prevent accessing decrypted[0] for empty payload
    byte paddingByte = decrypted[encryptedLen - 1];
    if (paddingByte > AES_BLOCK_SIZE || paddingByte == 0) {
        return 0; // Invalid padding
    }
    // Check if all padding bytes are correct
    for (size_t i = 0; i < paddingByte; i++) {
        if (decrypted[encryptedLen - 1 - i] != paddingByte) {
            return 0; // Invalid padding
        }
    }
    return encryptedLen - paddingByte;
}


size_t RS485SecureStack::stuffBytes(const byte* input, size_t inputLen, byte* output) {
    size_t outputIdx = 0;
    for (size_t i = 0; i < inputLen; i++) {
        if (input[i] == START_BYTE || input[i] == END_BYTE || input[i] == ESCAPE_BYTE) {
            output[outputIdx++] = ESCAPE_BYTE;
            output[outputIdx++] = input[i]; // Store the original byte
        } else {
            output[outputIdx++] = input[i];
        }
    }
    return outputIdx;
}

size_t RS485SecureStack::unstuffBytes(const byte* input, size_t inputLen, byte* output) {
    size_t outputIdx = 0;
    bool escaped = false;
    for (size_t i = 0; i < inputLen; i++) {
        if (input[i] == ESCAPE_BYTE && !escaped) {
            escaped = true;
        } else {
            output[outputIdx++] = input[i];
            escaped = false;
        }
    }
    return outputIdx;
}

void RS485SecureStack::setAckEnabled(bool enabled) {
    _ackEnabled = enabled;
}

void RS485SecureStack::setCurrentKeyId(uint16_t keyId) {
    _currentSessionKeyId = keyId;
    // For PoC, only 2 keys are in the pool.
    // In a real system, you'd retrieve the actual key based on keyId.
    if (keyId == 0) {
         memcpy(_currentSessionKey, _sessionKeyPool[0], AES_KEY_SIZE);
    } else if (keyId == 1) { // Example for second key in pool
         memcpy(_currentSessionKey, _sessionKeyPool[1], AES_KEY_SIZE);
    } else {
        Serial.println("Warning: Attempted to set unknown key ID.");
        // Fallback to default key or error state
    }
}

void RS485SecureStack::setSessionKey(uint16_t keyId, const byte sessionKey[AES_KEY_SIZE]) {
    // Allows the master to push new keys into the pool or for nodes to store new keys from master
    if (keyId < 2) { // Limited pool size for PoC
        memcpy(_sessionKeyPool[keyId], sessionKey, AES_KEY_SIZE);
        Serial.print("Session Key for ID ");
        Serial.print(keyId);
        Serial.println(" updated.");
    } else {
        Serial.println("Warning: Session key pool too small for this ID.");
    }
}

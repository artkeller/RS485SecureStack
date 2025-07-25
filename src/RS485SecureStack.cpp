#include "RS485SecureStack.h"

// Konstante für das Escape-Byte im Byte-Stuffing
const uint8_t RS485_ESCAPE_BYTE = 0x7D; // Beispielwert, kann angepasst werden

// CRC16 Tabelle (CCITT, Xmodem, Kermit, etc. können variieren)
// Hier: CRC-16-IBM (oder CRC-16-CCITT mit initial 0x0000, polynomial 0x8005)
static const uint16_t crc16_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F81, 0xEF40, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B81, 0xAB40, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5001, 0x90C0, 0x9180, 0x5141, 0x9300, 0x53C1, 0x5281, 0x9240,
    0x9600, 0x56C1, 0x5781, 0x9740, 0x5501, 0x95C0, 0x9481, 0x5440,
    0x9C00, 0x5CC1, 0x5D81, 0x9D40, 0x5F01, 0x9FC0, 0x9E80, 0x5E41,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};


// NEU: Konstruktor, der den DirectionControl-Zeiger speichert
RS485SecureStack::RS485SecureStack(RS485DirectionControl* directionControl) 
    : _directionControl(directionControl), _serial(nullptr), _myAddress(0), _currentKeyId(0) {
    // Initialisiere Master Key und Session Keys mit Nullen
    memset(_masterKey, 0, sizeof(_masterKey));
    for (int i = 0; i < 256; ++i) {
        memset(_sessionKeys[i], 0, sizeof(_sessionKeys[i]));
    }
}

// Initialisiert den Stack
void RS485SecureStack::begin(uint8_t myAddress, const char* masterKey, uint8_t initialKeyId, HardwareSerial& serial) {
    _myAddress = myAddress;
    _serial = &serial;
    _serial->begin(RS485_INITIAL_BAUD_RATE); // Startet mit einer bekannten Baudrate
    _serial->setTimeout(SERIAL_TIMEOUT_MS);

    // Initialisiere den Master Key (SHA256 Hash des übergebenen Schlüssels)
    SHA256 sha256;
    sha256.reset();
    sha256.update(masterKey, strlen(masterKey));
    sha256.finalize(_masterKey, sizeof(_masterKey));

    // Initialisiere Session Key 0 mit dem Master Key
    setSessionKey(0, _masterKey, sizeof(_masterKey));
    _currentKeyId = initialKeyId; // Setzt die initial zu verwendende Key ID

    // Wenn ein DirectionControl-Objekt übergeben wurde, initialisiere es
    if (_directionControl != nullptr) {
        _directionControl->begin();
    }

    _resetReceiveBuffer();
}

// Hauptloop-Funktion zum Empfangen von Paketen
void RS485SecureStack::loop() {
    while (_serial->available()) {
        uint8_t incomingByte = _serial->read();

        if (_receiveBufferPos == 0) { // Suchen nach Startbytes
            if (incomingByte == RS485_START_BYTE_0) {
                _receiveBuffer[_receiveBufferPos++] = incomingByte;
            } else {
                // Falsches Startbyte, verwerfen
                if (_debug) Serial.printf("DBG: Falsches Startbyte 0x%02X\n", incomingByte);
            }
        } else if (_receiveBufferPos == 1) {
            if (incomingByte == RS485_START_BYTE_1) {
                _receiveBuffer[_receiveBufferPos++] = incomingByte;
            } else {
                // Falsches zweites Startbyte, Puffer zurücksetzen
                if (_debug) Serial.printf("DBG: Falsches zweites Startbyte 0x%02X\n", incomingByte);
                _resetReceiveBuffer();
            }
        } else {
            // Normale Daten oder Escape-Sequenz
            if (incomingByte == RS485_START_BYTE_0 || incomingByte == RS485_START_BYTE_1) {
                // Unerwartetes Startbyte, Puffer zurücksetzen und neu beginnen
                if (_debug) Serial.println("DBG: Unerwartetes Startbyte im Paket, Puffer reset.");
                _resetReceiveBuffer();
                if (incomingByte == RS485_START_BYTE_0) { // Neuen Start erfassen
                     _receiveBuffer[_receiveBufferPos++] = incomingByte;
                }
                continue; // Nächstes Byte lesen
            }
            
            _receiveBuffer[_receiveBufferPos++] = incomingByte;

            // Paketlänge überprüfen (Mindestlänge Header + IV + HMAC + CRC)
            if (_receiveBufferPos >= TOTAL_LENGTH_INDEX + 1) { // totalLength Byte wurde empfangen
                uint8_t totalLength = _receiveBuffer[TOTAL_LENGTH_INDEX];
                
                // Überprüfen, ob die deklarierte Länge im akzeptablen Bereich liegt
                // Header (8) + IV (16) + HMAC (32) + CRC (2) = 58 Bytes Mindestlänge für leeres Payload
                // Maximale Länge = MAX_PACKET_SIZE
                if (totalLength < (8 + RS485_IV_LENGTH + RS485_HMAC_LENGTH + 2) || totalLength > MAX_PACKET_SIZE) {
                    if (_debug) Serial.printf("DBG: Ungültige Paketlänge: %d (Pos: %d). Resetting buffer.\n", totalLength, _receiveBufferPos);
                    _resetReceiveBuffer();
                    continue; // Beginne neu mit der Suche nach Startbytes
                }

                // Wenn genug Bytes für das gesamte Paket empfangen wurden (inkl. Stuffing!)
                // Achtung: totalLength ist die LÄNGE des UNgestufften Pakets.
                // Das gestuffte Paket kann bis zu doppelt so lang sein.
                // Wir müssen hier auf die LÄNGE des GESTUFFTEN Pakets warten.
                // Ein einfaches Prüfen auf totalLength reicht hier nicht!
                // Wir nehmen an, dass der Byte-Stuffing-Prozess das Paket maximal verdoppelt.
                // Daher ist MAX_PACKET_SIZE * 2 unser maximaler Puffer.
                // Wir können das Paket nur komplett verarbeiten, wenn _receiveBufferPos >= totalLength ist.
                // Das Unstuffing passiert danach.
                if (_receiveBufferPos >= totalLength + 2) { // +2 für Startbytes
                    // Versuche das Paket zu extrahieren und zu verarbeiten
                    if (_extractPacket()) {
                        // Paket wurde erfolgreich verarbeitet, Puffer zurücksetzen
                        _resetReceiveBuffer();
                    } else {
                        // Paketfehler (CRC/HMAC), Puffer zurücksetzen
                        _resetReceiveBuffer();
                    }
                }
            }
            // Maximalen Pufferüberlauf verhindern
            if (_receiveBufferPos >= (MAX_PACKET_SIZE * 2) -1) {
                if (_debug) Serial.println("DBG: Receive buffer overflow. Resetting.");
                _resetReceiveBuffer();
            }
        }
    }
}

// Registriert eine Callback-Funktion
void RS485SecureStack::registerReceiveCallback(PacketReceivedCallback callback) {
    _packetReceivedCallback = callback;
}

// Sendet eine Nachricht
bool RS458SecureStack::sendMessage(uint8_t destinationAddress, uint8_t senderAddress, char messageType, const String& payload, bool requiresAck) {
    // Überprüfen, ob Payload zu lang ist
    if (payload.length() > MAX_PACKET_SIZE - (8 + RS485_IV_LENGTH + RS485_HMAC_LENGTH + 2)) {
        if (_debug) Serial.println("ERR: Payload zu lang.");
        return false;
    }

    // Puffer für verschlüsselten Payload (Payload + Padding)
    // AES verschlüsselt in 16-Byte-Blöcken
    size_t paddedPayloadLen = payload.length();
    if (paddedPayloadLen % RS485_IV_LENGTH != 0) {
        paddedPayloadLen = ((paddedPayloadLen / RS485_IV_LENGTH) + 1) * RS485_IV_LENGTH;
    }
    uint8_t encryptedPayloadBuffer[paddedPayloadLen];
    memset(encryptedPayloadBuffer, 0, paddedPayloadLen);
    memcpy(encryptedPayloadBuffer, payload.c_str(), payload.length());

    // IV generieren
    uint8_t iv[RS485_IV_LENGTH];
    _generateIV(iv);

    // Payload verschlüsseln
    _encryptAES(encryptedPayloadBuffer, paddedPayloadLen, _sessionKeys[_currentKeyId], iv);

    // Gesamtpaket zusammenbauen (unverschlüsselte Teile + IV + verschlüsselter Payload + HMAC + CRC)
    // Header (8 Bytes) + IV (16 Bytes) + Encrypted Payload (padded) + HMAC (32 Bytes) + CRC (2 Bytes)
    size_t packetBodyLength = RS485_IV_LENGTH + paddedPayloadLen + RS485_HMAC_LENGTH + 2;
    uint8_t rawPacket[8 + packetBodyLength]; // Header + Body

    // Header füllen
    rawPacket[START_BYTE_0_INDEX] = RS485_START_BYTE_0;
    rawPacket[START_BYTE_1_INDEX] = RS485_START_BYTE_1;
    rawPacket[PROTOCOL_VERSION_INDEX] = RS485_PROTOCOL_VERSION;
    // Gesamtlänge (ab Startbyte 0xDE)
    rawPacket[TOTAL_LENGTH_INDEX] = (uint8_t)(8 + packetBodyLength); // Gesamtlänge des *un-stuffed* Pakets
    rawPacket[MESSAGE_TYPE_INDEX] = messageType;
    rawPacket[DEST_ADDRESS_INDEX] = destinationAddress;
    rawPacket[SENDER_ADDRESS_INDEX] = senderAddress;
    rawPacket[KEY_ID_INDEX] = _currentKeyId;

    // IV hinzufügen
    memcpy(&rawPacket[8], iv, RS485_IV_LENGTH);

    // Verschlüsselten Payload hinzufügen
    memcpy(&rawPacket[8 + RS485_IV_LENGTH], encryptedPayloadBuffer, paddedPayloadLen);

    // HMAC berechnen und hinzufügen
    uint8_t hmacResult[RS485_HMAC_LENGTH];
    // HMAC über Header, IV und verschlüsseltem Payload
    _calculateHMAC(_sessionKeys[_currentKeyId], rawPacket, 8 + RS485_IV_LENGTH + paddedPayloadLen, hmacResult);
    memcpy(&rawPacket[8 + RS485_IV_LENGTH + paddedPayloadLen], hmacResult, RS485_HMAC_LENGTH);

    // CRC16 berechnen und hinzufügen (über alles von Startbyte 0 bis HMAC-Ende)
    uint16_t crc = _calculateCRC16(rawPacket, 8 + RS485_IV_LENGTH + paddedPayloadLen + RS485_HMAC_LENGTH);
    rawPacket[8 + RS485_IV_LENGTH + paddedPayloadLen + RS485_HMAC_LENGTH] = (uint8_t)(crc & 0xFF);
    rawPacket[8 + RS485_IV_LENGTH + paddedPayloadLen + RS485_HMAC_LENGTH + 1] = (uint8_t)((crc >> 8) & 0xFF);

    // Byte-Stuffing anwenden
    size_t stuffedLength = _byteStuff(rawPacket, 8 + packetBodyLength, _stuffedPacketBuffer);

    // NEU: Setze den Transceiver in den Sende-Modus
    if (_directionControl != nullptr) {
        _directionControl->setTransmitMode();
        delayMicroseconds(RS485_TX_ENABLE_DELAY_US); 
    }

    // Sende das gestuffte Paket
    _serial->write(_stuffedPacketBuffer, stuffedLength);
    _serial->flush(); // Warte, bis alle Bytes gesendet wurden

    // NEU: Setze den Transceiver sofort nach dem Senden zurück in den Empfangs-Modus
    if (_directionControl != nullptr) {
        delayMicroseconds(RS485_TX_DISABLE_DELAY_US); 
        _directionControl->setReceiveMode();
    }
    
    // Wenn ACK erforderlich, warten und prüfen
    if (requiresAck) {
        return _waitForAck(500); // 500ms Timeout für ACK
    }

    return true;
}

// Setzt einen neuen Session Key
bool RS485SecureStack::setSessionKey(uint8_t keyId, const uint8_t* keyData, size_t keyLen) {
    if (keyLen != 32) { // Session Keys müssen 32 Bytes für SHA256 HMAC sein
        if (_debug) Serial.println("ERR: Session Key muss 32 Bytes lang sein.");
        return false;
    }
    memcpy(_sessionKeys[keyId], keyData, keyLen);
    return true;
}

// Wechselt zur Verwendung eines neuen Schlüssels für ausgehende Nachrichten
void RS485SecureStack::setCurrentKeyId(uint8_t keyId) {
    if (keyId >= 256) {
        if (_debug) Serial.println("ERR: Ungültige Key ID.");
        return;
    }
    _currentKeyId = keyId;
    if (_debug) Serial.printf("DBG: Aktuelle Key ID auf %d gesetzt.\n", _currentKeyId);
}

// Setzt die Baudrate der seriellen Schnittstelle
void RS485SecureStack::setBaudRate(long baudRate) {
    if (_serial) {
        _serial->end();
        _serial->begin(baudRate);
        _serial->setTimeout(SERIAL_TIMEOUT_MS);
        if (_debug) Serial.printf("DBG: Baudrate auf %ld gesetzt.\n", baudRate);
    }
}

// Private Hilfsfunktionen

void RS485SecureStack::_resetReceiveBuffer() {
    _receiveBufferPos = 0;
    memset(_receiveBuffer, 0, sizeof(_receiveBuffer)); // Optional: Puffer leeren
}

// Prüft, ob ein Byte ein Startbyte ist (DE oder AD)
bool RS485SecureStack::_isStartByte(uint8_t byte) {
    return (byte == RS485_START_BYTE_0 || byte == RS485_START_BYTE_1);
}

// Extrahiert und verarbeitet ein Paket aus dem Empfangspuffer
bool RS485SecureStack::_extractPacket() {
    // Unstuffing des empfangenen Puffers
    size_t unstuffedLength = _byteUnstuff(_receiveBuffer, _receiveBufferPos, _unstuffedPacketBuffer);

    // Mindestlänge prüfen: Header (8) + IV (16) + HMAC (32) + CRC (2) = 58 Bytes
    if (unstuffedLength < (8 + RS485_IV_LENGTH + RS485_HMAC_LENGTH + 2)) {
        if (_debug) Serial.printf("ERR: Unstuffed Paket zu kurz (%d Bytes).\n", unstuffedLength);
        return false;
    }

    // Header-Prüfung
    if (_unstuffedPacketBuffer[START_BYTE_0_INDEX] != RS485_START_BYTE_0 ||
        _unstuffedPacketBuffer[START_BYTE_1_INDEX] != RS485_START_BYTE_1 ||
        _unstuffedPacketBuffer[PROTOCOL_VERSION_INDEX] != RS485_PROTOCOL_VERSION) {
        if (_debug) Serial.println("ERR: Ungültiger Header im unstuffed Paket.");
        return false;
    }

    uint8_t totalLength = _unstuffedPacketBuffer[TOTAL_LENGTH_INDEX];
    if (totalLength != unstuffedLength) {
        if (_debug) Serial.printf("ERR: Deklarierte Länge (%d) stimmt nicht mit unstuffed Länge (%d) überein.\n", totalLength, unstuffedLength);
        return false;
    }

    // CRC16 prüfen (CRC befindet sich am Ende des unstuffed Pakets)
    uint16_t receivedCrc = (_unstuffedPacketBuffer[unstuffedLength - 2] | (_unstuffedPacketBuffer[unstuffedLength - 1] << 8));
    uint16_t calculatedCrc = _calculateCRC16(_unstuffedPacketBuffer, unstuffedLength - 2); // CRC über alles außer den letzten 2 Bytes (CRC selbst)

    bool crcVerified = (receivedCrc == calculatedCrc);
    if (!crcVerified) {
        if (_debug) Serial.printf("ERR: CRC16 Fehler. Empfangen: 0x%04X, Berechnet: 0x%04X\n", receivedCrc, calculatedCrc);
        return false; // CRC-Fehler, Paket verwerfen
    }

    uint8_t keyId = _unstuffedPacketBuffer[KEY_ID_INDEX];
    if (keyId >= 256) {
        if (_debug) Serial.printf("ERR: Ungültige Key ID (%d) im Header.\n", keyId);
        return false; // Ungültige Key ID
    }

    uint8_t receivedHmac[RS485_HMAC_LENGTH];
    memcpy(receivedHmac, &_unstuffedPacketBuffer[totalLength - RS485_HMAC_LENGTH - 2], RS485_HMAC_LENGTH); // -2 für CRC

    uint8_t calculatedHmac[RS485_HMAC_LENGTH];
    // HMAC über alles bis zum Beginn des HMAC-Feldes
    _calculateHMAC(_sessionKeys[keyId], _unstuffedPacketBuffer, totalLength - RS485_HMAC_LENGTH - 2, calculatedHmac);

    bool hmacVerified = true;
    for (size_t i = 0; i < RS485_HMAC_LENGTH; ++i) {
        if (receivedHmac[i] != calculatedHmac[i]) {
            hmacVerified = false;
            break;
        }
    }

    if (!hmacVerified) {
        if (_debug) Serial.println("ERR: HMAC-Fehler. Paket nicht authentifiziert.");
        // Dennoch könnte das Paket ein ACK/NACK sein, das selbst HMAC-gesichert ist.
        // Wenn es mein ACK ist und der HMAC nicht stimmt, ist etwas faul.
        // Für den Callback geben wir hmacVerified = false mit.
        // Wir verwerfen das Paket nicht komplett hier, sondern lassen den Callback entscheiden.
    }

    // IV und verschlüsselte Payload extrahieren
    uint8_t iv[RS485_IV_LENGTH];
    memcpy(iv, &_unstuffedPacketBuffer[8], RS485_IV_LENGTH);

    size_t encryptedPayloadStart = 8 + RS485_IV_LENGTH;
    size_t encryptedPayloadLen = totalLength - encryptedPayloadStart - RS485_HMAC_LENGTH - 2; // -2 für CRC

    uint8_t decryptedPayloadBuffer[encryptedPayloadLen];
    memcpy(decryptedPayloadBuffer, &_unstuffedPacketBuffer[encryptedPayloadStart], encryptedPayloadLen);

    // Payload entschlüsseln (nur wenn HMAC_OK ist, sonst wäre Entschlüsselung nutzlos und potenziell gefährlich)
    if (hmacVerified) {
        _decryptAES(decryptedPayloadBuffer, encryptedPayloadLen, _sessionKeys[keyId], iv);
    } else {
        // Wenn HMAC nicht verifiziert, Payload mit Nullen füllen, um keine sensiblen Daten preiszugeben.
        // Oder den Callback das leere Payload verarbeiten lassen.
        memset(decryptedPayloadBuffer, 0, encryptedPayloadLen);
        if (_debug) Serial.println("DBG: Payload nicht entschlüsselt wegen fehlendem HMAC.");
    }
    
    // Packet_t Struktur füllen
    Packet_t receivedPacket;
    receivedPacket.totalLength = totalLength;
    receivedPacket.messageType = (char)_unstuffedPacketBuffer[MESSAGE_TYPE_INDEX];
    receivedPacket.destinationAddress = _unstuffedPacketBuffer[DEST_ADDRESS_INDEX];
    receivedPacket.senderAddress = _unstuffedPacketBuffer[SENDER_ADDRESS_INDEX];
    receivedPacket.keyId = keyId;
    receivedPacket.payload = String((char*)decryptedPayloadBuffer); // Konvertierung von uint8_t* zu String
    receivedPacket.requiresAck = false; // Wird nicht aus Paket gelesen, muss vom Sender bekannt sein
    receivedPacket.isAck = (receivedPacket.messageType == MSG_TYPE_ACK_NACK);
    receivedPacket.hmacVerified = hmacVerified;
    receivedPacket.crcVerified = crcVerified;

    // Nur Pakete, die für uns sind oder Broadcasts, verarbeiten
    // Und wir dürfen keine ACK/NACKs von uns selbst verarbeiten
    if ((receivedPacket.destinationAddress == _myAddress || receivedPacket.destinationAddress == 255) && 
        !(receivedPacket.isAck && receivedPacket.senderAddress == _myAddress)) {
        
        if (_debug) {
            Serial.printf("RCV: Type='%c', Dest=%d, Sender=%d, KeyID=%d, Len=%d, Payload='%s'\n",
                          receivedPacket.messageType, receivedPacket.destinationAddress,
                          receivedPacket.senderAddress, receivedPacket.keyId,
                          receivedPacket.payload.length(), receivedPacket.payload.c_str());
            Serial.printf("HMAC_OK: %s, CRC_OK: %s\n", receivedPacket.hmacVerified ? "YES" : "NO", receivedPacket.crcVerified ? "YES" : "NO");
        }

        if (_packetReceivedCallback) {
            _packetReceivedCallback(receivedPacket);
        }

        // Automatisch ACK senden, wenn erforderlich und gültig
        // Und es ist KEINE ACK/NACK Nachricht
        if (!receivedPacket.isAck && receivedPacket.requiresAck && receivedPacket.destinationAddress == _myAddress && hmacVerified && crcVerified) {
             _sendAck(receivedPacket.senderAddress, _myAddress, receivedPacket.keyId);
        }
    } else {
        if (_debug) {
            Serial.printf("DBG: Paket für andere Adresse (%d), Sender=%d, oder ist eigenes ACK. Verworfen.\n", 
                          receivedPacket.destinationAddress, receivedPacket.senderAddress);
        }
    }

    return true; // Paket wurde (versucht zu) verarbeitet, Puffer kann zurückgesetzt werden
}

// Berechnet CRC16 über gegebene Daten
uint16_t RS485SecureStack::_calculateCRC16(const uint8_t* data, size_t length) {
    uint16_t crc = 0x0000; // Initialwert
    for (size_t i = 0; i < length; ++i) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

// Generiert einen zufälligen Initialisierungsvektor (IV)
void RS485SecureStack::_generateIV(uint8_t* iv) {
    // Arduino random() ist nicht kryptographisch sicher, aber für PoC ausreichend.
    // In Produktion: Hardware Random Number Generator (TRNG) des ESP32 verwenden!
    for (size_t i = 0; i < RS485_IV_LENGTH; ++i) {
        iv[i] = random(256);
    }
}

// Verschlüsselt Daten mit AES-256 im CBC-Modus
void RS458SecureStack::_encryptAES(uint8_t* data, size_t len, const uint8_t* key, const uint8_t* iv) {
    AES256 aes256;
    aes256.setKey(key, aes256.keySize());
    aes256.setIV(iv, aes256.ivSize());
    aes256.encryptCBC(data, len);
}

// Entschlüsselt Daten mit AES-256 im CBC-Modus
void RS458SecureStack::_decryptAES(uint8_t* data, size_t len, const uint8_t* key, const uint8_t* iv) {
    AES256 aes256;
    aes256.setKey(key, aes256.keySize());
    aes256.setIV(iv, aes256.ivSize());
    aes256.decryptCBC(data, len);
}

// Berechnet HMAC-SHA256
void RS485SecureStack::_calculateHMAC(const uint8_t* key, const uint8_t* data, size_t dataLen, uint8_t* hmacResult) {
    uint8_t opad[64];
    uint8_t ipad[64];
    uint8_t tempKey[64]; // Für den Fall, dass der Schlüssel länger als 64 Bytes ist

    // Key-Padding oder Hashing, falls der Schlüssel länger als der Blocksize ist
    if (RS485_HMAC_LENGTH > 64) { // HMAC_LENGTH ist 32, SHA256 Blocksize ist 64. Also wird dieser Teil nicht erreicht.
        SHA256 sha256_key_hash;
        sha256_key_hash.reset();
        sha256_key_hash.update(key, RS485_HMAC_LENGTH);
        sha256_key_hash.finalize(tempKey, 32);
        memset(tempKey + 32, 0, 32); // Pad mit Nullen
    } else {
        memcpy(tempKey, key, RS485_HMAC_LENGTH);
        memset(tempKey + RS485_HMAC_LENGTH, 0, 64 - RS485_HMAC_LENGTH); // Pad mit Nullen
    }

    // opad und ipad XORen
    for (int i = 0; i < 64; ++i) {
        ipad[i] = tempKey[i] ^ 0x36;
        opad[i] = tempKey[i] ^ 0x5C;
    }

    // Inner hash
    SHA256 sha256_inner;
    sha256_inner.reset();
    sha256_inner.update(ipad, 64);
    sha256_inner.update(data, dataLen);
    uint8_t innerHash[32];
    sha256_inner.finalize(innerHash, 32);

    // Outer hash
    SHA256 sha256_outer;
    sha256_outer.reset();
    sha256_outer.update(opad, 64);
    sha256_outer.update(innerHash, 32);
    sha256_outer.finalize(hmacResult, 32);
}

// Wendet Byte-Stuffing an (ersetzt Start- und Escape-Bytes)
size_t RS485SecureStack::_byteStuff(const uint8_t* source, size_t sourceLen, uint8_t* destination) {
    size_t destLen = 0;
    for (size_t i = 0; i < sourceLen; ++i) {
        if (source[i] == RS485_START_BYTE_0 || source[i] == RS485_START_BYTE_1 || source[i] == RS485_ESCAPE_BYTE) {
            destination[destLen++] = RS485_ESCAPE_BYTE;
            destination[destLen++] = source[i] ^ 0x20; // XOR mit 0x20 zum Escaping
        } else {
            destination[destLen++] = source[i];
        }
    }
    return destLen;
}

// Entfernt Byte-Stuffing
size_t RS485SecureStack::_byteUnstuff(const uint8_t* source, size_t sourceLen, uint8_t* destination) {
    size_t destLen = 0;
    for (size_t i = 0; i < sourceLen; ++i) {
        if (source[i] == RS485_ESCAPE_BYTE) {
            if (i + 1 < sourceLen) {
                destination[destLen++] = source[++i] ^ 0x20;
            } else {
                if (_debug) Serial.println("ERR: Unvollständige Escape-Sequenz.");
                return 0; // Fehler: Unvollständige Escape-Sequenz
            }
        } else {
            destination[destLen++] = source[i];
        }
    }
    return destLen;
}

// Sendet eine ACK-Nachricht
bool RS485SecureStack::_sendAck(uint8_t destinationAddress, uint8_t senderAddress, uint8_t keyId) {
    if (_debug) Serial.printf("DBG: Sende ACK an %d\n", destinationAddress);
    return sendMessage(destinationAddress, senderAddress, MSG_TYPE_ACK_NACK, "ACK", false); // ACK selbst erfordert kein ACK
}

// Sendet eine NACK-Nachricht
bool RS485SecureStack::_sendNack(uint8_t destinationAddress, uint8_t senderAddress, uint8_t keyId, const char* reason) {
    if (_debug) Serial.printf("DBG: Sende NACK an %d, Grund: %s\n", destinationAddress, reason);
    String payload = "NACK:";
    payload += reason;
    return sendMessage(destinationAddress, senderAddress, MSG_TYPE_ACK_NACK, payload, false); // NACK selbst erfordert kein ACK
}

// Wartet auf ein ACK/NACK
bool RS485SecureStack::_waitForAck(long timeoutMs) {
    unsigned long startTime = millis();
    _resetReceiveBuffer(); // Puffer für ACK leeren

    while (millis() - startTime < timeoutMs) {
        if (_serial->available()) {
            uint8_t incomingByte = _serial->read();

            if (_receiveBufferPos == 0) { // Suchen nach Startbytes
                if (incomingByte == RS485_START_BYTE_0) {
                    _receiveBuffer[_receiveBufferPos++] = incomingByte;
                }
            } else if (_receiveBufferPos == 1) {
                if (incomingByte == RS485_START_BYTE_1) {
                    _receiveBuffer[_receiveBufferPos++] = incomingByte;
                } else {
                    _resetReceiveBuffer();
                }
            } else {
                if (incomingByte == RS485_START_BYTE_0 || incomingByte == RS485_START_BYTE_1) {
                    _resetReceiveBuffer();
                    if (incomingByte == RS485_START_BYTE_0) { 
                         _receiveBuffer[_receiveBufferPos++] = incomingByte;
                    }
                    continue; 
                }
                _receiveBuffer[_receiveBufferPos++] = incomingByte;

                if (_receiveBufferPos >= TOTAL_LENGTH_INDEX + 1) {
                    uint8_t totalLength = _receiveBuffer[TOTAL_LENGTH_INDEX];
                    if (totalLength < (8 + RS485_IV_LENGTH + RS485_HMAC_LENGTH + 2) || totalLength > MAX_PACKET_SIZE) {
                        _resetReceiveBuffer();
                        continue;
                    }

                    if (_receiveBufferPos >= totalLength + 2) { // +2 für Startbytes
                        // Versuche das Paket zu extrahieren und zu verarbeiten
                        size_t unstuffedLength = _byteUnstuff(_receiveBuffer, _receiveBufferPos, _unstuffedPacketBuffer);

                        if (unstuffedLength < (8 + RS485_IV_LENGTH + RS485_HMAC_LENGTH + 2)) {
                            _resetReceiveBuffer();
                            continue;
                        }

                        if (_unstuffedPacketBuffer[START_BYTE_0_INDEX] != RS485_START_BYTE_0 ||
                            _unstuffedPacketBuffer[START_BYTE_1_INDEX] != RS485_START_BYTE_1 ||
                            _unstuffedPacketBuffer[PROTOCOL_VERSION_INDEX] != RS485_PROTOCOL_VERSION) {
                            _resetReceiveBuffer();
                            continue;
                        }
                        
                        if (_unstuffedPacketBuffer[TOTAL_LENGTH_INDEX] != unstuffedLength) {
                            _resetReceiveBuffer();
                            continue;
                        }

                        uint16_t receivedCrc = (_unstuffedPacketBuffer[unstuffedLength - 2] | (_unstuffedPacketBuffer[unstuffedLength - 1] << 8));
                        uint16_t calculatedCrc = _calculateCRC16(_unstuffedPacketBuffer, unstuffedLength - 2);

                        if (receivedCrc != calculatedCrc) {
                            if (_debug) Serial.println("ERR: ACK/NACK CRC16 Fehler.");
                            _resetReceiveBuffer();
                            continue;
                        }

                        uint8_t keyId = _unstuffedPacketBuffer[KEY_ID_INDEX];
                        if (keyId >= 256) {
                            if (_debug) Serial.printf("ERR: Ungültige Key ID (%d) im ACK/NACK Header.\n", keyId);
                            _resetReceiveBuffer();
                            continue;
                        }

                        uint8_t receivedHmac[RS485_HMAC_LENGTH];
                        memcpy(receivedHmac, &_unstuffedPacketBuffer[unstuffedLength - RS485_HMAC_LENGTH - 2], RS485_HMAC_LENGTH);

                        uint8_t calculatedHmac[RS485_HMAC_LENGTH];
                        _calculateHMAC(_sessionKeys[keyId], _unstuffedPacketBuffer, unstuffedLength - RS485_HMAC_LENGTH - 2, calculatedHmac);

                        bool hmacVerified = true;
                        for (size_t i = 0; i < RS485_HMAC_LENGTH; ++i) {
                            if (receivedHmac[i] != calculatedHmac[i]) {
                                hmacVerified = false;
                                break;
                            }
                        }

                        if (!hmacVerified) {
                            if (_debug) Serial.println("ERR: ACK/NACK HMAC-Fehler. Nicht authentifiziert.");
                            _resetReceiveBuffer();
                            continue;
                        }

                        // Entschlüsseln (auch wenn es ein ACK ist, könnte es eine Payload haben, z.B. NACK Reason)
                        uint8_t iv[RS485_IV_LENGTH];
                        memcpy(iv, &_unstuffedPacketBuffer[8], RS485_IV_LENGTH);
                        size_t encryptedPayloadStart = 8 + RS485_IV_LENGTH;
                        size_t encryptedPayloadLen = unstuffedLength - encryptedPayloadStart - RS485_HMAC_LENGTH - 2;

                        uint8_t decryptedPayloadBuffer[encryptedPayloadLen];
                        memcpy(decryptedPayloadBuffer, &_unstuffedPacketBuffer[encryptedPayloadStart], encryptedPayloadLen);
                        _decryptAES(decryptedPayloadBuffer, encryptedPayloadLen, _sessionKeys[keyId], iv);
                        
                        // Überprüfen, ob es ein ACK/NACK für UNS ist und vom erwarteten Sender kommt
                        if ((char)_unstuffedPacketBuffer[MESSAGE_TYPE_INDEX] == MSG_TYPE_ACK_NACK &&
                            _unstuffedPacketBuffer[DEST_ADDRESS_INDEX] == _myAddress && // Es ist für mich
                            _unstuffedPacketBuffer[SENDER_ADDRESS_INDEX] == destinationAddress) // Und vom Empfänger der Originalnachricht
                        {
                            String ackPayload = String((char*)decryptedPayloadBuffer);
                            _resetReceiveBuffer();
                            if (ackPayload.startsWith("ACK")) {
                                if (_debug) Serial.println("DBG: ACK empfangen.");
                                return true;
                            } else if (ackPayload.startsWith("NACK")) {
                                if (_debug) Serial.printf("DBG: NACK empfangen: %s\n", ackPayload.c_str());
                                return false; // NACK bedeutet Fehler
                            }
                        } else {
                            if (_debug) Serial.println("DBG: Empfangenes Paket ist kein passendes ACK/NACK.");
                            // Dies ist kein ACK/NACK für uns, verarbeiten es als normales Paket in der Haupt-Loop.
                            // Temporär zurück in den Puffer legen und Haupt-Loop verarbeiten lassen
                            _resetReceiveBuffer(); // Puffer leeren
                            // Dieses Verhalten ist kompliziert, wenn man im _waitForAck_ ist.
                            // Die einfachste Lösung ist, es zu verwerfen und davon auszugehen, dass _waitForAck_ nur auf das spezielle ACK/NACK achtet.
                            // Andere Pakete werden von der Haupt-Loop abgeholt.
                        }
                    }
                }
            }
             if (_receiveBufferPos >= (MAX_PACKET_SIZE * 2) -1) { // Pufferüberlauf verhindern
                if (_debug) Serial.println("DBG: ACK/NACK receive buffer overflow. Resetting.");
                _resetReceiveBuffer();
            }
        }
    }
    if (_debug) Serial.println("DBG: ACK/NACK Timeout.");
    return false; // Timeout
}

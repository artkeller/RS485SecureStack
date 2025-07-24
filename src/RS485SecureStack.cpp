#include "RS485SecureStack.h"
#include <HardwareSerial.h> // Wird für HardwareSerial::begin() etc. benötigt
#include <Crypto.h>         // Basis Crypto API für ESP32
#include <AES.h>            // Spezifische AES Implementierung für ESP32
#include <HMAC.h>           // Spezifische HMAC Implementierung für ESP32
#include <SHA256.h>         // Spezifische SHA256 Implementierung für ESP32 (auch für Key Derivation)

// Hinweis: Globale Instanzen von Crypto-Objekten wie im alten Code sind entfernt,
// da sie jetzt als Member der Klasse RS485SecureStack deklariert sind.
// Beispiel: `AES128 _aes;` ist bereits in der Klasse definiert.

// Konstruktor-Implementierung
// Initialisierungsliste initialisiert alle Member-Variablen direkt
// und synchronisiert Namen und Typen mit RS485SecureStack.h
RS485SecureStack::RS485SecureStack(HardwareSerial& serial, byte localAddress, const byte masterKey[HMAC_KEY_SIZE])
    : _rs485Serial(serial),          // Initialisiert die HardwareSerial Referenz
      _localAddress(localAddress),   // Initialisiert die lokale Adresse
      _receiveBufferIdx(0),          // Index für den Empfangspuffer
      _receivingPacket(false),       // Flag, ob gerade ein Paket empfangen wird
      _escapeNextByte(false),        // Flag, ob das nächste Byte escaped ist
      _receiveCallback(nullptr),     // Callback-Funktion ist initial nullptr
      _ackEnabled(true),             // ACKs standardmäßig aktiviert
      _currentSessionKeyId(0),       // Aktuelle Session Key ID (initial 0)
      _hmacVerified(false),          // HMAC initial nicht verifiziert
      _checksumVerified(false),      // Checksumme initial nicht verifiziert (falls implementiert)
      _currentPacketSource(0),       // Source der letzten Nachricht
      _currentPacketTarget(0),       // Target der letzten Nachricht
      _currentPacketMsgType(0),      // Typ der letzten Nachricht
      _deRePin(-1)                   // DE/RE Pin initial auf -1 (nicht gesetzt)
{
    // Kopiere den Pre-Shared Master Authentication Key in das private Member-Array
    memcpy(_masterKey, masterKey, HMAC_KEY_SIZE);

    // Initialisiere den Session Key Pool mit Nullen
    for (int i = 0; i < MAX_SESSION_KEYS; ++i) {
        memset(_sessionKeyPool[i], 0, AES_KEY_SIZE);
    }

    // *** WICHTIGSTE ÄNDERUNG: Ableiten des initialen Session Keys (Key ID 0) aus dem Master Key ***
    // Nutzt SHA256 als einfache, aber kryptographisch sichere Key Derivation Function (KDF).
    // Der Master Key wird gehasht, um einen deterministischen Session Key zu erzeugen.
    byte derivedKey[HMAC_KEY_SIZE]; // SHA256 erzeugt 32 Bytes
    _sha256.reset(); // Initialisiert das SHA256 Objekt
    _sha256.update(_masterKey, HMAC_KEY_SIZE); // Hash über den gesamten Master Key
    _sha256.finalize(derivedKey, HMAC_KEY_SIZE); // Speichert den 32-Byte Hash

    // Setze den ersten Session Key (Key ID 0) im Pool auf die ersten 16 Bytes des abgeleiteten Schlüssels.
    // AES-128 benötigt nur 16 Bytes.
    memcpy(_sessionKeyPool[0], derivedKey, AES_KEY_SIZE);
    
    // Setze diesen abgeleiteten Schlüssel als den aktuell aktiven Session Key.
    // Dies geschieht über setSessionKey (speichert den Schlüssel) und setCurrentKeyId (aktiviert ihn).
    setSessionKey(0, _sessionKeyPool[0]); // Session Key 0 ist jetzt der abgeleitete Schlüssel
    setCurrentKeyId(0); // Setze den abgeleiteten Schlüssel als aktuellen aktiven Schlüssel
}

// Initialisiert die HardwareSerial und optional den DE/RE Pin
void RS485SecureStack::begin(long baudRate) {
    _rs485Serial.begin(baudRate);
    // Optional: Konfiguration von RX/TX Pins bei Nicht-Standard-UARTs (z.B. Serial1/Serial2)
    // _rs485Serial.setPins(RX_PIN, TX_PIN); 

    if (_deRePin != -1) {
        pinMode(_deRePin, OUTPUT);
        digitalWrite(_deRePin, LOW); // Standardmäßig auf Empfangsmodus setzen
    }

    Serial.print("RS485SecureStack gestartet auf Baudrate ");
    Serial.print(_rs485Serial.baudRate());
    Serial.print(", lokale Adresse: ");
    Serial.println(_localAddress);
}

// Setzt den DE/RE Pin des RS485 Transceivers
void RS485SecureStack::setDeRePin(int pin) {
    _deRePin = pin;
    if (_deRePin != -1) {
        pinMode(_deRePin, OUTPUT);
        digitalWrite(_deRePin, LOW); // Initial auf Empfang
    }
}

// Interne Funktion zum Ändern der Baudrate
void RS485SecureStack::_setBaudRate(long newBaudRate) {
    _rs485Serial.updateBaudRate(newBaudRate);
    Serial.print("Baudrate aktualisiert auf: ");
    Serial.println(_rs485Serial.baudRate());
}

// Verarbeitet eingehende Bytes von der seriellen Schnittstelle
void RS485SecureStack::processIncoming() {
    while (_rs485Serial.available()) {
        byte inByte = _rs485Serial.read();

        // Wenn noch kein Paket empfangen wird, suche nach START_BYTE
        if (!_receivingPacket) {
            if (inByte == START_BYTE) {
                _receivingPacket = true;    // Paketempfang beginnt
                _receiveBufferIdx = 0;      // Pufferindex zurücksetzen
                _escapeNextByte = false;    // Escape-Status zurücksetzen
            }
            continue; // Warte auf START_BYTE oder verarbeite nächstes Byte
        }

        // Byte-Stuffing Logik
        if (inByte == ESCAPE_BYTE && !_escapeNextByte) {
            _escapeNextByte = true; // Nächstes Byte ist escaped
            continue;
        }

        // END_BYTE signalisiert Paketende, es sei denn, es ist escaped
        if (inByte == END_BYTE && !_escapeNextByte) {
            _receivingPacket = false; // Paketempfang beendet
            if (_receiveBufferIdx > 0) {
                // Verarbeite das ungestuffte Paket im _receiveBuffer
                size_t unstuffedLen = _unstuffBytes(_receiveBuffer, _receiveBufferIdx, _currentPacketRaw);
                _processReceivedPacket(_currentPacketRaw, unstuffedLen);
            }
            _receiveBufferIdx = 0; // Puffer für nächstes Paket zurücksetzen
            continue;
        }

        // Speichere das empfangene Byte im Puffer
        if (_receiveBufferIdx < RECEIVE_BUFFER_SIZE) {
            if (_escapeNextByte) {
                // Wenn das aktuelle Byte escaped war, un-XORen und speichern
                _receiveBuffer[_receiveBufferIdx++] = inByte ^ ESCAPE_XOR_MASK;
                _escapeNextByte = false; // Escape-Status zurücksetzen
            } else {
                // Normales Byte speichern
                _receiveBuffer[_receiveBufferIdx++] = inByte;
            }
        } else {
            // Pufferüberlauf - Paket verwerfen und State zurücksetzen
            Serial.println("Fehler: Empfangspufferüberlauf, Paket verworfen.");
            _receivingPacket = false;
            _receiveBufferIdx = 0;
            _escapeNextByte = false;
        }
    }
}

// Sendet Rohdaten über die RS485-Schnittstelle
void RS485SecureStack::_sendRaw(const byte* data, size_t len) {
    if (_deRePin != -1) {
        digitalWrite(_deRePin, HIGH); // Auf Sende-Modus schalten
        delayMicroseconds(20); // Kurze Verzögerung für Transceiver-Umschaltung
    }
    _rs485Serial.write(data, len);
    _rs485Serial.flush(); // Sicherstellen, dass alle Bytes gesendet wurden

    // Kurze Pause, um das letzte Byte zu senden, bevor auf Empfang umgeschaltet wird.
    // Dies hängt von der Baudrate ab. 10 Bits pro Byte.
    // delayMicroseconds((10UL * len * 1000000UL) / _rs485Serial.baudRate());
    delay(1); // Eine kleine feste Verzögerung ist oft ausreichend und einfacher

    if (_deRePin != -1) {
        digitalWrite(_deRePin, LOW); // Auf Empfangs-Modus zurückschalten
    }
}

// Sendet eine Nachricht (öffentliche Methode)
bool RS485SecureStack::sendMessage(byte targetAddress, char msgType, const String& payload) {
    size_t plainLen = payload.length();
    
    // Prüfen, ob die Klartext-Nutzlast die maximale Größe überschreitet
    if (plainLen > MAX_RAW_PAYLOAD_SIZE) {
        Serial.println("Fehler: Nutzlast zu groß für MAX_RAW_PAYLOAD_SIZE.");
        return false;
    }

    // Generiere einen zufälligen IV für AES-CBC.
    // HINWEIS: random(256) ist ein Pseudozufallsgenerator.
    // FÜR PRODUKTIONSEINSATZ MUSS HIER ein kryptographisch sicherer Zufallszahlengenerator (CSPRNG)
    // wie `esp_random()` auf ESP32 verwendet werden!
    byte iv[IV_SIZE];
    for (int i = 0; i < IV_SIZE; i++) {
        iv[i] = random(256); 
    }

    // Puffer für die verschlüsselte Nutzlast
    byte encryptedPayload[MAX_PADDED_ENCRYPTED_PAYLOAD_SIZE]; 
    size_t encryptedLen = _encryptPayload((byte*)payload.c_str(), plainLen, encryptedPayload, _currentSessionKey, iv);

    if (encryptedLen == 0) {
        Serial.println("Fehler: Verschlüsselung der Nutzlast fehlgeschlagen (z.B. falsche Größe nach Padding).");
        return false;
    }
    
    // Baue das Paket und sende es
    return _buildAndSendPacket(targetAddress, msgType, encryptedPayload, encryptedLen, _currentSessionKeyId, iv);
}

// Sendet ein ACK oder NACK
bool RS485SecureStack::sendAckNack(byte targetAddress, char originalMsgType, bool success) {
    if (!_ackEnabled) return false; // Wenn ACKs/NACKs deaktiviert sind, nichts tun

    // Die Payload für ACK/NACK enthält den Original-Nachrichtentyp und das Ergebnis ('1'/'0')
    String ackNackPayload = String(originalMsgType) + (success ? "1" : "0");
    
    // Generiere einen IV, auch für kleine Payloads
    byte iv[IV_SIZE];
    for (int i = 0; i < IV_SIZE; i++) {
        iv[i] = random(256); // FÜR PRODUKTION: CSPRNG nutzen!
    }
    
    // Puffer für verschlüsselte ACK/NACK Payload (mindestens eine AES-Blockgröße)
    byte encryptedPayload[AES_BLOCK_SIZE]; 
    size_t encryptedLen = _encryptPayload((byte*)ackNackPayload.c_str(), ackNackPayload.length(), encryptedPayload, _currentSessionKey, iv);

    if (encryptedLen == 0) {
        Serial.println("Fehler: ACK/NACK-Verschlüsselung fehlgeschlagen.");
        return false;
    }

    // Sende das ACK/NACK Paket
    return _buildAndSendPacket(targetAddress, (success ? MSG_TYPE_ACK : MSG_TYPE_NACK), encryptedPayload, encryptedLen, _currentSessionKeyId, iv);
}

// Interne Methode zum Bauen und Senden eines Pakets
bool RS485SecureStack::_buildAndSendPacket(byte targetAddress, char msgType, const byte* encryptedPayload, size_t encryptedLen, uint16_t keyId, const byte iv[IV_SIZE]) {
    // Überprüfung der Größe des verschlüsselten Payloads
    if (encryptedLen % AES_BLOCK_SIZE != 0 || encryptedLen > MAX_PADDED_ENCRYPTED_PAYLOAD_SIZE) {
        Serial.println("Fehler: Ungültige verschlüsselte Payload-Länge.");
        return false;
    }

    // Berechne die Größe des logischen Pakets (Header + Encrypted Payload + HMAC)
    size_t logicalPacketLen = HEADER_SIZE + encryptedLen + HMAC_TAG_SIZE;

    // Prüfe, ob das logische Paket nicht zu groß ist
    if (logicalPacketLen > MAX_LOGICAL_PACKET_SIZE) {
        Serial.println("Fehler: Logisches Paket zu groß.");
        return false;
    }

    // Temporärer Puffer für das logische Paket vor dem Stuffing
    byte tempLogicalPacket[MAX_LOGICAL_PACKET_SIZE];
    size_t currentIdx = 0;

    // 1. Header erstellen
    tempLogicalPacket[currentIdx++] = _localAddress;
    tempLogicalPacket[currentIdx++] = targetAddress;
    tempLogicalPacket[currentIdx++] = (byte)msgType;
    tempLogicalPacket[currentIdx++] = (byte)(keyId >> 8);   // Key ID MSB
    tempLogicalPacket[currentIdx++] = (byte)(keyId & 0xFF); // Key ID LSB
    memcpy(&tempLogicalPacket[currentIdx], iv, IV_SIZE);
    currentIdx += IV_SIZE;

    // 2. Verschlüsselte Nutzlast hinzufügen
    memcpy(&tempLogicalPacket[currentIdx], encryptedPayload, encryptedLen);
    currentIdx += encryptedLen;

    // 3. HMAC über (Header + Encrypted Payload) mit dem Master Key berechnen
    // Dies stellt die Authentizität und Integrität des Pakets sicher.
    _hmac.setKey(_masterKey, HMAC_KEY_SIZE); // Setze den Master Key für HMAC
    _hmac.update(tempLogicalPacket, currentIdx); // HMAC über Header und verschlüsselte Payload
    byte hmacTag[HMAC_TAG_SIZE];
    _hmac.finalize(hmacTag, HMAC_TAG_SIZE); // Berechne den HMAC Tag

    // 4. HMAC Tag anfügen
    memcpy(&tempLogicalPacket[currentIdx], hmacTag, HMAC_TAG_SIZE);
    currentIdx += HMAC_TAG_SIZE; // currentIdx ist nun gleich logicalPacketLen

    // 5. Byte-Stuffing des gesamten logischen Pakets
    // Puffer für das gestuffte Paket, Worst-Case Größe
    byte stuffedPacket[MAX_PHYSICAL_PACKET_SIZE]; 
    stuffedPacket[0] = START_BYTE; // Füge das Start-Byte hinzu

    // Führe das Stuffing durch und speichere im Puffer, beginnend nach dem START_BYTE
    size_t stuffedLen = _stuffBytes(tempLogicalPacket, logicalPacketLen, &stuffedPacket[1]);
    
    // Füge das End-Byte nach den gestufften Daten hinzu
    stuffedPacket[1 + stuffedLen] = END_BYTE;

    // 6. Sende das vollständig gerahmte und gestuffte Paket
    _sendRaw(stuffedPacket, stuffedLen + 2); // Länge ist gestuffte Daten + START_BYTE + END_BYTE
    return true;
}

// Verarbeitet ein vollständig empfangenes, ungestufftes Logisches Paket
void RS485SecureStack::_processReceivedPacket(const byte* rawPacket, size_t rawLen) {
    // Speichere das rohe Paket und dessen Länge für Debugging/Monitoring
    _currentPacketRawLen = rawLen;
    memcpy(_currentPacketRaw, rawPacket, rawLen); 

    // Flags für die Verifikation zurücksetzen
    _hmacVerified = false;
    _checksumVerified = true; // Placeholder: Wenn keine separate Checksumme verwendet wird, ist dies immer true

    // Minimale Paketgröße prüfen: Header + HMAC_TAG_SIZE
    if (rawLen < (HEADER_SIZE + HMAC_TAG_SIZE)) { 
        Serial.println("Fehler: Paket zu kurz (Minimum Header + HMAC nicht erreicht).");
        return;
    }

    size_t currentIdx = 0;

    // Header-Informationen extrahieren
    _currentPacketSource = rawPacket[currentIdx++];
    _currentPacketTarget = rawPacket[currentIdx++];
    _currentPacketMsgType = (char)rawPacket[currentIdx++];
    uint16_t receivedKeyId = (uint16_t)rawPacket[currentIdx] << 8 | rawPacket[currentIdx + 1];
    currentIdx += 2;
    memcpy(_currentPacketIV, &rawPacket[currentIdx], IV_SIZE);
    currentIdx += IV_SIZE;

    // Länge der verschlüsselten Nutzlast ermitteln
    // rawLen (gesamtes logisches Paket) - HMAC_TAG_SIZE - HEADER_SIZE = encryptedPayloadLen
    size_t encryptedPayloadLen = rawLen - HMAC_TAG_SIZE - HEADER_SIZE;

    // 1. HMAC Verifikation
    // Der empfangene HMAC-Tag befindet sich am Ende des Pakets
    byte receivedHmac[HMAC_TAG_SIZE];
    memcpy(receivedHmac, &rawPacket[rawLen - HMAC_TAG_SIZE], HMAC_TAG_SIZE);

    // Berechne den HMAC über Header und verschlüsselte Payload (ohne den empfangenen HMAC selbst)
    _hmac.setKey(_masterKey, HMAC_KEY_SIZE); // Setze den Master Key für die Berechnung
    _hmac.update(rawPacket, rawLen - HMAC_TAG_SIZE); // HMAC über den Teil, der gesichert ist
    byte calculatedHmac[HMAC_TAG_SIZE];
    _hmac.finalize(calculatedHmac, HMAC_TAG_SIZE); // Berechne den Tag

    // Vergleiche den berechneten HMAC mit dem empfangenen HMAC
    if (memcmp(receivedHmac, calculatedHmac, HMAC_TAG_SIZE) != 0) {
        Serial.println("Fehler: HMAC Mismatch - Paketintegrität kompromittiert oder falscher Master Key.");
        _hmacVerified = false;
        // Optional: NACK senden, wenn ACKs aktiviert sind und die Adresse für uns war
        if (_ackEnabled && (_currentPacketTarget == _localAddress || _currentPacketTarget == 0xFF)) {
            sendAckNack(_currentPacketSource, _currentPacketMsgType, false);
        }
        return;
    }
    _hmacVerified = true; // HMAC ist gültig

    // 2. Zieladressen-Prüfung
    if (_currentPacketTarget != _localAddress && _currentPacketTarget != 0xFF) { 
        // Paket ist weder für diesen Knoten noch ein Broadcast. Verwerfen nach HMAC-Check.
        // Serial.println("Paket nicht für diese Adresse bestimmt.");
        return; 
    }

    // 3. Key ID Prüfung (Abgleich des Session Key)
    if (receivedKeyId != _currentSessionKeyId) {
        Serial.print("Warnung: Paket mit unerwarteter Key ID empfangen (erwartet ");
        Serial.print(_currentSessionKeyId);
        Serial.print(", erhalten ");
        Serial.print(receivedKeyId);
        Serial.println("). Nutzlast wird nicht entschlüsselt.");
        // Wenn ein Callback registriert ist, kann er über den Schlüssel-Mismatch informiert werden
        if (_receiveCallback) {
             _receiveCallback(_currentPacketSource, _currentPacketMsgType, "KEY_MISMATCH");
        }
        // Optional: NACK senden, da wir die Payload nicht verarbeiten können
        if (_ackEnabled) {
            sendAckNack(_currentPacketSource, _currentPacketMsgType, false);
        }
        return;
    }
    
    // 4. Nutzlast entschlüsseln
    // Puffer für die entschlüsselte Nutzlast
    byte decryptedPayload[MAX_RAW_PAYLOAD_SIZE + AES_BLOCK_SIZE]; // Max. mögliche Größe (inkl. Padding vor dem Entfernen)
    size_t decryptedLen = _decryptPayload(&rawPacket[currentIdx], encryptedPayloadLen, decryptedPayload, _currentSessionKey, _currentPacketIV);

    if (decryptedLen == 0) {
        Serial.println("Fehler: Entschlüsselung fehlgeschlagen oder ungültiges Padding.");
        // Optional: NACK senden
        if (_ackEnabled) {
            sendAckNack(_currentPacketSource, _currentPacketMsgType, false);
        }
        return;
    }

    // Null-Terminierung für Umwandlung in String.
    // Sicherstellen, dass der Puffer groß genug ist (+1 für Null-Terminator).
    if (decryptedLen < (MAX_RAW_PAYLOAD_SIZE + AES_BLOCK_SIZE)) {
        decryptedPayload[decryptedLen] = '\0';
    } else {
        // Sollte nicht passieren, wenn MAX_RAW_PAYLOAD_SIZE korrekt dimensioniert ist.
        Serial.println("Warnung: Entschlüsselte Nutzlast ist größer als erwartet.");
        return; // Oder Fehlerbehandlung
    }
    String payloadStr = (char*)decryptedPayload;

    // 5. Callback aufrufen (wenn registriert)
    if (_receiveCallback) {
        _receiveCallback(_currentPacketSource, _currentPacketMsgType, payloadStr);
    }
    
    // 6. ACK senden (falls aktiviert und alles erfolgreich war)
    if (_ackEnabled) {
        sendAckNack(_currentPacketSource, _currentPacketMsgType, true);
    }
}

// Registriert die Callback-Funktion
void RS485SecureStack::registerReceiveCallback(ReceiveCallback callback) {
    _receiveCallback = callback;
}

// Verschlüsselt die Klartext-Nutzlast
size_t RS485SecureStack::_encryptPayload(const byte* plain, size_t plainLen, byte* encrypted, const byte key[AES_KEY_SIZE], byte iv[IV_SIZE]) {
    // PKCS7 Padding: Bestimme die Länge nach Padding, die ein Vielfaches der AES_BLOCK_SIZE ist
    size_t paddedLen = plainLen;
    if (paddedLen % AES_BLOCK_SIZE != 0) {
        paddedLen = (plainLen / AES_BLOCK_SIZE + 1) * AES_BLOCK_SIZE;
    }
    
    // Prüfe auf Pufferüberlauf für die gepaddete Payload
    if (paddedLen > MAX_PADDED_ENCRYPTED_PAYLOAD_SIZE) {
        Serial.println("Fehler: Puffer für gepaddete Nutzlast ist zu klein.");
        return 0; // Rückgabe 0 bei Fehler
    }

    byte paddedPlain[paddedLen];
    memcpy(paddedPlain, plain, plainLen); // Kopiere Klartext

    // Fülle den Rest des Puffers mit Padding-Bytes
    byte paddingByte = paddedLen - plainLen;
    for (size_t i = plainLen; i < paddedLen; i++) {
        paddedPlain[i] = paddingByte;
    }

    // Setze AES-Schlüssel und IV und verschlüssele
    _aes.setKey(key, AES_KEY_SIZE);
    _aes.setIV(iv, IV_SIZE);
    _aes.encrypt(encrypted, paddedPlain, paddedLen);

    return paddedLen; // Gibt die Länge der verschlüsselten Daten zurück
}

// Entschlüsselt die verschlüsselte Nutzlast
size_t RS485SecureStack::_decryptPayload(const byte* encrypted, size_t encryptedLen, byte* decrypted, const byte key[AES_KEY_SIZE], const byte iv[IV_SIZE]) {
    // Prüfe auf gültige Länge für AES-Entschlüsselung
    if (encryptedLen == 0 || encryptedLen % AES_BLOCK_SIZE != 0) {
        Serial.println("Fehler: Entschlüsselte Länge ist 0 oder kein Vielfaches der Blockgröße.");
        return 0;
    }

    // Setze AES-Schlüssel und IV und entschlüssele
    _aes.setKey(key, AES_KEY_SIZE);
    _aes.setIV(iv, IV_SIZE);
    _aes.decrypt(decrypted, encrypted, encryptedLen);

    // PKCS7 Unpadding
    byte paddingByte = decrypted[encryptedLen - 1]; // Letztes Byte enthält die Anzahl der Padding-Bytes
    
    // Prüfe auf gültigen Padding-Wert
    if (paddingByte == 0 || paddingByte > AES_BLOCK_SIZE) {
        Serial.print("Fehler: Ungültiger Padding-Wert: ");
        Serial.println(paddingByte);
        return 0;
    }
    
    // Prüfe, ob alle Padding-Bytes korrekt sind
    for (size_t i = 0; i < paddingByte; i++) {
        if (decrypted[encryptedLen - 1 - i] != paddingByte) {
            Serial.println("Fehler: Ungültiges Padding-Muster.");
            return 0; // Ungültiges Padding-Muster
        }
    }
    
    return encryptedLen - paddingByte; // Gibt die tatsächliche Länge der Nutzlast zurück
}

// Implementiert Byte-Stuffing nach dem DLE-Verfahren mit XOR-Maske
size_t RS485SecureStack::_stuffBytes(const byte* input, size_t inputLen, byte* output) {
    size_t outputIdx = 0;
    for (size_t i = 0; i < inputLen; i++) {
        // Wenn das Byte ein Steuerzeichen (START, END, ESCAPE) ist,
        // füge das ESCAPE_BYTE und das XOR-Maskierte Originalbyte hinzu.
        if (input[i] == START_BYTE || input[i] == END_BYTE || input[i] == ESCAPE_BYTE) {
            output[outputIdx++] = ESCAPE_BYTE;
            output[outputIdx++] = input[i] ^ ESCAPE_XOR_MASK;
        } else {
            // Normale Bytes werden direkt kopiert.
            output[outputIdx++] = input[i];
        }
        // Prüfe auf Pufferüberlauf im Output-Puffer
        if (outputIdx >= SEND_BUFFER_SIZE) {
            Serial.println("Fehler: Stuffing Output Buffer Overflow!");
            return 0; // Indiziere Fehler
        }
    }
    return outputIdx;
}

// Implementiert Byte-Unstuffing nach dem DLE-Verfahren mit XOR-Maske
size_t RS485SecureStack::_unstuffBytes(const byte* input, size_t inputLen, byte* output) {
    size_t outputIdx = 0;
    bool escaped = false; // Flag, ob das letzte Byte ein ESCAPE_BYTE war

    for (size_t i = 0; i < inputLen; i++) {
        if (input[i] == ESCAPE_BYTE && !escaped) {
            // ESCAPE_BYTE gefunden, und es war nicht selbst escaped.
            // Das nächste Byte ist das eigentliche Datenbyte, das un-XORt werden muss.
            escaped = true;
        } else {
            if (escaped) {
                // Wenn das vorherige Byte ein ESCAPE_BYTE war,
                // un-XOR das aktuelle Byte, um den Originalwert zu erhalten.
                output[outputIdx++] = input[i] ^ ESCAPE_XOR_MASK;
                escaped = false; // Escape-Status zurücksetzen
            } else {
                // Normales Byte (nicht escaped).
                output[outputIdx++] = input[i];
            }
        }
        // Prüfe auf Pufferüberlauf im Output-Puffer
        if (outputIdx >= MAX_LOGICAL_PACKET_SIZE) {
            Serial.println("Fehler: Unstuffing Output Buffer Overflow!");
            return 0; // Indiziere Fehler
        }
    }
    return outputIdx;
}

// Setzt das Flag, ob ACKs/NACKs gesendet werden sollen
void RS485SecureStack::setAckEnabled(bool enabled) {
    _ackEnabled = enabled;
}

// Setzt den aktuell aktiven Session Key basierend auf der Key ID
void RS485SecureStack::setCurrentKeyId(uint16_t keyId) {
    _currentSessionKeyId = keyId;
    if (keyId < MAX_SESSION_KEYS) {
         // Kopiere den gewählten Schlüssel aus dem Pool in den _currentSessionKey Puffer
         memcpy(_currentSessionKey, _sessionKeyPool[keyId], AES_KEY_SIZE);
         Serial.print("Aktiver Session Key auf ID ");
         Serial.print(keyId);
         Serial.println(" gesetzt.");
    } else {
        Serial.print("Warnung: Versuch, unbekannte Key ID (");
        Serial.print(keyId);
        Serial.println(") zu setzen. Aktiver Schlüssel bleibt unverändert.");
        // Optional: Auf einen Standard-Fallback-Schlüssel setzen oder Fehler signalisieren
    }
}

// Speichert einen Session Key im Pool unter einer bestimmten ID
void RS485SecureStack::setSessionKey(uint16_t keyId, const byte sessionKey[AES_KEY_SIZE]) {
    if (keyId < MAX_SESSION_KEYS) {
        memcpy(_sessionKeyPool[keyId], sessionKey, AES_KEY_SIZE);
        Serial.print("Session Key für ID ");
        Serial.print(keyId);
        Serial.println(" im Pool aktualisiert.");
    } else {
        Serial.println("Warnung: Session Key Pool zu klein für diese ID oder ID ungültig.");
    }
}

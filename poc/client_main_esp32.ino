#include "RS485SecureStack.h"
#include "credantials.h" // Pre-Shared Master Key (MUSS AUF ALLEN DEVICES IDENTISCH SEIN!)

// --- RS485 Konfiguration ---
HardwareSerial& rs485Serial = Serial1;
const int RS485_DE_RE_PIN = 3; // Beispiel GPIO für DE/RE Pin, anpassen!
const byte MY_ADDRESS = 11; // Diesen Wert für jeden Client anpassen (z.B. 11 oder 12) 

RS485SecureStack rs485Stack(rs485Serial, MY_ADDRESS, MASTER_KEY); [cite: 3]

// --- Master Heartbeat Monitoring ---
unsigned long lastMasterHeartbeatRxTime = 0;
const unsigned long MASTER_HEARTBEAT_TIMEOUT_MS = 1000; // Wenn Master länger als 1 Sekunde still ist, als offline betrachten 
bool masterIsPresent = false; [cite: 3]

// --- Client State ---
enum ClientState {
    CLIENT_STATE_WAITING_FOR_MASTER,
    CLIENT_STATE_IDLE
};
ClientState currentClientState = CLIENT_STATE_WAITING_FOR_MASTER; [cite: 3]

// --- Rekeying ---
uint16_t currentSessionKeyId = 0; [cite: 3]
// Note: Clients receive new keys from the Master and update their stack.
// They don't generate keys. 

// --- Callbacks ---
void onPacketReceived(byte senderAddr, char msgType, const String& payload);
void manageDERE(bool transmit); // Function to control DE/RE pin

// NEU: Utility-Funktion zur Konvertierung eines Hex-Strings in ein Byte-Array
// Gibt die Anzahl der in den Buffer geschriebenen Bytes zurück, oder 0 bei Fehler.
size_t hexStringToBytes(const String& hexString, byte* buffer, size_t bufferSize) {
    if (hexString.length() % 2 != 0) return 0; // Muss eine gerade Länge haben
    size_t byteLength = hexString.length() / 2;
    if (byteLength > bufferSize) return 0; // Buffer zu klein

    for (size_t i = 0; i < byteLength; i++) {
        char hexPair[3]; // Zwei Zeichen + Null-Terminator
        hexString.substring(i * 2, i * 2 + 2).toCharArray(hexPair, 3);
        buffer[i] = (byte)strtol(hexPair, NULL, 16); // Konvertiere Hex in Byte
    }
    return byteLength;
}

void setup() {
  Serial.begin(115200); // USB-Serial für Debugging
  pinMode(RS485_DE_RE_PIN, OUTPUT);
  manageDERE(false); // Start in Receive Mode
  rs485Stack.begin(9600); // RS485-Stack initialisieren (startet mit Basis-Baudrate) 

  Serial.print("Client ESP32-C3 gestartet, Adresse: ");
  Serial.println(MY_ADDRESS);
  rs485Stack.registerReceiveCallback(onPacketReceived);
  rs485Stack.setAckEnabled(true); // Client muss ACKs senden 

  lastMasterHeartbeatRxTime = millis(); // Assume master is present at startup for initial state 
}

void loop() {
  rs485Stack.processIncoming(); // Pakete empfangen und verarbeiten 
  manageDERE(false); // Ensure we are in receive mode by default

  // --- Master-Anwesenheit prüfen ---
  if (millis() - lastMasterHeartbeatRxTime > MASTER_HEARTBEAT_TIMEOUT_MS) {
    if (masterIsPresent) {
      Serial.println("!!! WARNING: MASTER HEARTBEAT TIMEOUT! Master is GONE or SILENT !!!"); [cite: 3]
      masterIsPresent = false;
      currentClientState = CLIENT_STATE_WAITING_FOR_MASTER; // Go to a safe state
    }
  } else {
    if (!masterIsPresent) {
      Serial.println("!!! MASTER HEARTBEAT RESTORED! !!!"); [cite: 3]
      masterIsPresent = true;
      if (currentClientState == CLIENT_STATE_WAITING_FOR_MASTER) {
        currentClientState = CLIENT_STATE_IDLE; // Resume normal flow
      }
    }
  }

  if (!masterIsPresent) {
      // If master is not present, remain in a passive state. Do not send data. 
      return;
  }

  // Clients sind meist passiv, d.h., sie warten auf Anfragen und antworten dann. 
  // Hier keine aktive Sendelogik, außer als Reaktion auf empfangene Pakete.
  // Serial.println("Client is idle, waiting for commands.");
  delay(100); // Reduce busy waiting
}

// Callback-Funktion, wird von RS485SecureStack aufgerufen
void onPacketReceived(byte senderAddr, char msgType, const String& payload) {
  // --- Master Heartbeat verarbeiten ---
  if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_MASTER_HEARTBEAT) {
      lastMasterHeartbeatRxTime = millis(); [cite: 3]
      rs485Stack.sendAckNack(senderAddr, msgType, true); // ACK, um den Master zu beruhigen
      return; // Heartbeat-Pakete werden sonst nicht weiter verarbeitet
  }

  // --- Baudraten-Anpassung vom Master verarbeiten ---
  if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_BAUD_RATE_SET) {
      long newBaudRate = payload.toLong();
      Serial.print("Client: Baud Rate Set Request from Master for: ");
      Serial.println(newBaudRate);
      rs485Serial.begin(newBaudRate); // HardwareSerial wechseln
      rs485Stack._setBaudRate(newBaudRate); // Stack informieren
      rs485Stack.sendAckNack(senderAddr, msgType, true);
      return;
  }

  // --- NEU: Schlüssel-Update vom Master verarbeiten ---
  // Erwartetes Format: "KEY_UPDATE:<keyId_dezimal>:<hexKeyString>"
  if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_KEY_UPDATE) {
      Serial.println("Client: Received Key Update message from Master.");
      int firstColon = payload.indexOf(':');
      int secondColon = payload.indexOf(':', firstColon + 1);

      if (firstColon != -1 && secondColon != -1) {
          String prefix = payload.substring(0, firstColon);
          if (prefix == "KEY_UPDATE") {
              uint16_t newKeyId = payload.substring(firstColon + 1, secondColon).toInt();
              String hexKeyString = payload.substring(secondColon + 1);

              byte newKey[AES_KEY_SIZE]; // Annahme: AES_KEY_SIZE ist 16
              size_t bytesRead = hexStringToBytes(hexKeyString, newKey, AES_KEY_SIZE);

              if (bytesRead == AES_KEY_SIZE) {
                  rs485Stack.setSessionKey(newKeyId, newKey); // Schlüssel im Pool speichern
                  rs485Stack.setCurrentKeyId(newKeyId);     // Aktuellen Schlüssel setzen
                  Serial.print("Client: New session key set for ID ");
                  Serial.println(newKeyId);
                  currentSessionKeyId = newKeyId; // Aktualisiere die lokale Variable
                  rs485Stack.sendAckNack(senderAddr, msgType, true); // ACK senden
                  return; // Paket verarbeitet
              } else {
                  Serial.println("Client: ERROR parsing hex key string from update message.");
              }
          } else {
             Serial.println("Client: ERROR: Key Update message has unexpected prefix.");
          }
      } else {
          Serial.println("Client: ERROR: Key Update message has unexpected format.");
      }
  }

  // --- Beispiel für eine Daten-Nachricht vom Submaster (oder Master) ---
  if (msgType == RS485SecureStack::MSG_TYPE_DATA) {
      Serial.print("Client: Received DATA from ");
      Serial.print(senderAddr);
      Serial.print(": '");
      Serial.print(payload);
      Serial.println("'");
      // Optional: Eine Antwort senden
      // manageDERE(true);
      // rs485Stack.sendMessage(senderAddr, RS485SecureStack::MSG_TYPE_ACK, "DATA_RECEIVED");
      // manageDERE(false);
  }

  // Debug-Ausgabe für alle empfangenen Pakete
  Serial.print("Received from ");
  Serial.print(senderAddr);
  Serial.print(", Type: ");
  Serial.print(msgType);
  Serial.print(", Payload: '");
  Serial.print(payload);
  Serial.println("'");
}

// Funktion zum Steuern des DE/RE Pins
void manageDERE(bool transmit) {
  if (transmit) {
    digitalWrite(RS485_DE_RE_PIN, HIGH); // Senden aktivieren
  } else {
    digitalWrite(RS485_DE_RE_PIN, LOW); // Empfangen aktivieren
  }
}

#include "RS485SecureStack.h"
#include "credantials.h" // Pre-Shared Master Key (MUSS AUF ALLEN DEVICES IDENTISCH SEIN!)

// --- RS485 Konfiguration ---
HardwareSerial& rs485Serial = Serial1;
const int RS485_DE_RE_PIN = 3; // Beispiel GPIO für DE/RE Pin, anpassen!
const byte MY_ADDRESS = 1; // Diesen Wert für jeden Submaster anpassen (z.B. 1 oder 2) 

RS485SecureStack rs485Stack(rs485Serial, MY_ADDRESS, MASTER_KEY); [cite: 2]

// --- Master Heartbeat Monitoring ---
unsigned long lastMasterHeartbeatRxTime = 0;
const unsigned long MASTER_HEARTBEAT_TIMEOUT_MS = 1000; // Wenn Master länger als 1 Sekunde still ist, als offline betrachten 
bool masterIsPresent = false; [cite: 2]

// --- Submaster State ---
enum SubmasterState {
    SUBMASTER_STATE_WAITING_FOR_MASTER,
    SUBMASTER_STATE_WAITING_FOR_PERMISSION,
    SUBMASTER_STATE_SENDING_DATA,
    SUBMASTER_STATE_IDLE
};
SubmasterState currentSubmasterState = SUBMASTER_STATE_WAITING_FOR_MASTER; [cite: 2]

// --- Communication with Clients ---
const byte MY_CLIENT_ADDRESS = 11; // Beispiel: Submaster 1 kontrolliert Client 11 

unsigned long lastSendAttemptTime = 0;
const unsigned long SEND_INTERVAL_MS = 2000; // Try sending data every 2 seconds when permitted 

// --- Rekeying ---
uint16_t currentSessionKeyId = 0; [cite: 2]
// Note: Submasters receive new keys from the Master and update their stack.
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

  Serial.print("Submaster ESP32-C3 gestartet, Adresse: ");
  Serial.println(MY_ADDRESS);
  rs485Stack.registerReceiveCallback(onPacketReceived);
  rs485Stack.setAckEnabled(true); // Submaster muss ACKs senden und empfangen 

  lastMasterHeartbeatRxTime = millis(); // Assume master is present at startup for initial state 
}

void loop() {
  rs485Stack.processIncoming(); // Pakete empfangen und verarbeiten 
  manageDERE(false); // Ensure we are in receive mode by default

  // --- Master-Anwesenheit prüfen ---
  if (millis() - lastMasterHeartbeatRxTime > MASTER_HEARTBEAT_TIMEOUT_MS) {
    if (masterIsPresent) {
      Serial.println("!!! WARNING: MASTER HEARTBEAT TIMEOUT! Master is GONE or SILENT !!!"); [cite: 2]
      masterIsPresent = false;
      currentSubmasterState = SUBMASTER_STATE_WAITING_FOR_MASTER; // Go to a safe state
    }
  } else {
    if (!masterIsPresent) {
      Serial.println("!!! MASTER HEARTBEAT RESTORED! !!!"); [cite: 2]
      masterIsPresent = true;
      if (currentSubmasterState == SUBMASTER_STATE_WAITING_FOR_MASTER) {
        currentSubmasterState = SUBMASTER_STATE_WAITING_FOR_PERMISSION; // Resume normal flow
      }
    }
  }

  if (!masterIsPresent) {
      // If master is not present, remain in a passive state. Do not send data. 
      return;
  }

  // --- Submaster State Machine ---
  switch (currentSubmasterState) {
    case SUBMASTER_STATE_WAITING_FOR_MASTER:
      // Handled by masterIsPresent check above
      Serial.println("Waiting for Master to come online..."); [cite: 2]
      delay(100); // Don't spam serial
      break;

    case SUBMASTER_STATE_WAITING_FOR_PERMISSION:
      // Waiting for the Master to send "PERMISSION_TO_SEND" 
      Serial.println("Waiting for send permission from Master..."); [cite: 2]
      delay(100);
      break;

    case SUBMASTER_STATE_SENDING_DATA:
      if (millis() - lastSendAttemptTime > SEND_INTERVAL_MS) {
        manageDERE(true); // Set to transmit
        String data = "Hello from Submaster " + String(MY_ADDRESS) + " to Client " + String(MY_CLIENT_ADDRESS); [cite: 2]
        if (rs485Stack.sendMessage(MY_CLIENT_ADDRESS, RS485SecureStack::MSG_TYPE_DATA, data)) {
          Serial.print("Sent data to Client ");
          Serial.println(MY_CLIENT_ADDRESS);
          lastSendAttemptTime = millis();
          currentSubmasterState = SUBMASTER_STATE_IDLE; // Sent one message, now idle
          // Inform Master that sending is done (optional, but good for token passing)
          manageDERE(true);
          rs485Stack.sendMessage(0, RS485SecureStack::MSG_TYPE_DATA, "DONE_SENDING");
          manageDERE(false);
        } else {
          Serial.println("Submaster: Failed to send data.");
        }
      }
      break;

    case SUBMASTER_STATE_IDLE:
      // Submaster sent data and is now idle, waiting for next permission.
      // Serial.println("Submaster is idle.");
      delay(50); // Small delay to avoid busy-waiting
      break;
  }
}

// Callback-Funktion, wird von RS485SecureStack aufgerufen
void onPacketReceived(byte senderAddr, char msgType, const String& payload) {
  // --- Master Heartbeat verarbeiten ---
  if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_MASTER_HEARTBEAT) {
      lastMasterHeartbeatRxTime = millis(); [cite: 2]
      rs485Stack.sendAckNack(senderAddr, msgType, true); // ACK, um den Master zu beruhigen
      return; // Heartbeat-Pakete werden sonst nicht weiter verarbeitet
  }

  // --- Baudraten-Anpassung vom Master verarbeiten ---
  if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_BAUD_RATE_SET) {
      long newBaudRate = payload.toLong();
      Serial.print("Submaster: Baud Rate Set Request from Master for: ");
      Serial.println(newBaudRate);
      rs485Serial.begin(newBaudRate); // HardwareSerial wechseln
      rs485Stack._setBaudRate(newBaudRate); // Stack informieren
      rs485Stack.sendAckNack(senderAddr, msgType, true);
      return;
  }

  // --- NEU: Schlüssel-Update vom Master verarbeiten ---
  // Erwartetes Format: "KEY_UPDATE:<keyId_dezimal>:<hexKeyString>"
  if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_KEY_UPDATE) {
      Serial.println("Submaster: Received Key Update message from Master.");
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
                  Serial.print("Submaster: New session key set for ID ");
                  Serial.println(newKeyId);
                  currentSessionKeyId = newKeyId; // Aktualisiere die lokale Variable
                  rs485Stack.sendAckNack(senderAddr, msgType, true); // ACK senden
                  return; // Paket verarbeitet
              } else {
                  Serial.println("Submaster: ERROR parsing hex key string from update message.");
              }
          } else {
             Serial.println("Submaster: ERROR: Key Update message has unexpected prefix.");
          }
      } else {
          Serial.println("Submaster: ERROR: Key Update message has unexpected format.");
      }
  }

  // --- Sendeerlaubnis vom Master ---
  if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_PERMISSION_TO_SEND) {
      Serial.println("Submaster: Received PERMISSION_TO_SEND from Master.");
      currentSubmasterState = SUBMASTER_STATE_SENDING_DATA;
      lastSendAttemptTime = millis() - SEND_INTERVAL_MS; // Erlaubt sofortiges Senden
      rs485Stack.sendAckNack(senderAddr, msgType, true); // ACK senden
      return;
  }

  // --- Antwort vom Client verarbeiten ---
  if (msgType == RS485SecureStack::MSG_TYPE_ACK && payload == "DATA_RECEIVED") {
      Serial.print("Submaster: Received ACK (DATA_RECEIVED) from Client ");
      Serial.println(senderAddr);
      currentSubmasterState = SUBMASTER_STATE_IDLE; // Zurück in den Idle-Zustand
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

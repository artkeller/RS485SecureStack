#include "RS485SecureStack.h"
#include "credantials.h" // Pre-Shared Master Key (MUSS AUF ALLEN DEVICES IDENTISCH SEIN!)

// --- RS485 Konfiguration ---
// Für PoC mit Default UART1, da sonst Konflikt mit USB-Serial besteht:
HardwareSerial& rs485Serial = Serial1; // Siehe Scheduler-Kommentare zu UART-Nutzung
const int RS485_DE_RE_PIN = 3; // Beispiel GPIO für DE/RE Pin, anpassen!

const byte MY_ADDRESS = 11; // Diesen Wert für jeden Client anpassen (z.B. 11 oder 12)

RS485SecureStack rs485Stack(rs485Serial, MY_ADDRESS, MASTER_KEY);

// --- Master Heartbeat Monitoring ---
unsigned long lastMasterHeartbeatRxTime = 0;
const unsigned long MASTER_HEARTBEAT_TIMEOUT_MS = 1000; // Wenn Master länger als 1 Sekunde still ist, als offline betrachten
bool masterIsPresent = false;

// --- Client State ---
enum ClientState {
    CLIENT_STATE_WAITING_FOR_MASTER,
    CLIENT_STATE_IDLE
};
ClientState currentClientState = CLIENT_STATE_WAITING_FOR_MASTER;

// --- Rekeying ---
uint16_t currentSessionKeyId = 0;
// Note: Clients receive new keys from the Master and update their stack.
// They don't generate keys.

// --- Callbacks ---
void onPacketReceived(byte senderAddr, char msgType, const String& payload);
void manageDERE(bool transmit); // Function to control DE/RE pin

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
      Serial.println("!!! WARNING: MASTER HEARTBEAT TIMEOUT! Master is GONE or SILENT !!!");
      masterIsPresent = false;
      currentClientState = CLIENT_STATE_WAITING_FOR_MASTER; // Go to a safe state
    }
  } else {
    if (!masterIsPresent) {
      Serial.println("!!! MASTER HEARTBEAT RESTORED! !!!");
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
        lastMasterHeartbeatRxTime = millis();
        if (!masterIsPresent) {
            masterIsPresent = true;
            Serial.println("Master heartbeat received. Master is online.");
            if (currentClientState == CLIENT_STATE_WAITING_FOR_MASTER) {
                currentClientState = CLIENT_STATE_IDLE;
            }
        }
        rs485Stack.sendAckNack(senderAddr, msgType, true); // ACK, um den Master zu beruhigen
        return; // Heartbeat-Pakete werden sonst nicht weiter verarbeitet
    }

    // --- Baudraten-Anpassung ---
    if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_BAUD_RATE_SET) {
        long newBaudRate = payload.toLong();
        Serial.print("Baud Rate Set Request from Master for: "); Serial.println(newBaudRate);
        rs485Serial.begin(newBaudRate); // HardwareSerial wechseln
        rs485Stack._setBaudRate(newBaudRate); // Stack informieren
        rs485Stack.sendAckNack(senderAddr, msgType, true);
        return;
    }

    // --- Key Update ---
    if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_KEY_UPDATE) {
        uint16_t newKeyId = payload.toInt();
        Serial.print("Key update from Master to KeyID: "); Serial.println(newKeyId);
        rs485Stack.setCurrentKeyId(newKeyId);
        currentSessionKeyId = newKeyId; // Update local tracker
        rs485Stack.sendAckNack(senderAddr, msgType, true);
        return;
    }

    // --- Empfangene Daten (von Master oder Submastern) ---
    if (msgType == RS485SecureStack::MSG_TYPE_DATA) {
        Serial.print("DATA RECEIVED from "); Serial.print(senderAddr);
        Serial.print(", Payload: '"); Serial.print(payload); Serial.println("'");
        // Example: If a client receives a specific query, it might respond
        if (payload == "GET_STATUS") {
            manageDERE(true);
            rs485Stack.sendMessage(senderAddr, RS485SecureStack::MSG_TYPE_DATA, "STATUS_OK: Client " + String(MY_ADDRESS));
            manageDERE(false);
            Serial.println("Sent status response.");
        }
    }

    // Sende ACK/NACK an Absender für Nicht-Heartbeat-Pakete und Nicht-Baudrate/Key-Set
    // ACKs/NACKs for master heartbeats, baudrate sets, key updates are handled above.
    // For other generic data, send ACK.
    if (msgType != RS485SecureStack::MSG_TYPE_ACK && msgType != RS485SecureStack::MSG_TYPE_NACK) {
        rs485Stack.sendAckNack(senderAddr, msgType, true);
    }
}

// Helper function to control the DE/RE pin
void manageDERE(bool transmit) {
  if (transmit) {
    digitalWrite(RS485_DE_RE_PIN, HIGH);
  } else {
    digitalWrite(RS485_DE_RE_PIN, LOW);
  }
}
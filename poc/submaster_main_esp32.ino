#include "RS485SecureStack.h"
#include "credantials.h" // Pre-Shared Master Key (MUSS AUF ALLEN DEVICES IDENTISCH SEIN!)

// --- RS485 Konfiguration ---
// Für PoC mit Default UART1, da sonst Konflikt mit USB-Serial besteht:
HardwareSerial& rs485Serial = Serial1; // Siehe Scheduler-Kommentare zu UART-Nutzung
const int RS485_DE_RE_PIN = 3; // Beispiel GPIO für DE/RE Pin, anpassen!

const byte MY_ADDRESS = 1; // Diesen Wert für jeden Submaster anpassen (z.B. 1 oder 2)

RS485SecureStack rs485Stack(rs485Serial, MY_ADDRESS, MASTER_KEY);

// --- Master Heartbeat Monitoring ---
unsigned long lastMasterHeartbeatRxTime = 0;
const unsigned long MASTER_HEARTBEAT_TIMEOUT_MS = 1000; // Wenn Master länger als 1 Sekunde still ist, als offline betrachten
bool masterIsPresent = false;

// --- Submaster State ---
enum SubmasterState {
    SUBMASTER_STATE_WAITING_FOR_MASTER,
    SUBMASTER_STATE_WAITING_FOR_PERMISSION,
    SUBMASTER_STATE_SENDING_DATA,
    SUBMASTER_STATE_IDLE
};
SubmasterState currentSubmasterState = SUBMASTER_STATE_WAITING_FOR_MASTER;

// --- Communication with Clients ---
const byte MY_CLIENT_ADDRESS = 11; // Beispiel: Submaster 1 kontrolliert Client 11

unsigned long lastSendAttemptTime = 0;
const unsigned long SEND_INTERVAL_MS = 2000; // Try sending data every 2 seconds when permitted

// --- Rekeying ---
uint16_t currentSessionKeyId = 0;
// Note: Submasters receive new keys from the Master and update their stack.
// They don't generate keys.

// --- Callbacks ---
void onPacketReceived(byte senderAddr, char msgType, const String& payload);
void manageDERE(bool transmit); // Function to control DE/RE pin

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
      Serial.println("!!! WARNING: MASTER HEARTBEAT TIMEOUT! Master is GONE or SILENT !!!");
      masterIsPresent = false;
      currentSubmasterState = SUBMASTER_STATE_WAITING_FOR_MASTER; // Go to a safe state
    }
  } else {
    if (!masterIsPresent) {
      Serial.println("!!! MASTER HEARTBEAT RESTORED! !!!");
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
      Serial.println("Waiting for Master to come online...");
      delay(100); // Don't spam serial
      break;

    case SUBMASTER_STATE_WAITING_FOR_PERMISSION:
      // Waiting for the Master to send "PERMISSION_TO_SEND"
      Serial.println("Waiting for send permission from Master...");
      delay(100);
      break;

    case SUBMASTER_STATE_SENDING_DATA:
      if (millis() - lastSendAttemptTime > SEND_INTERVAL_MS) {
        manageDERE(true); // Set to transmit
        String data = "Hello from Submaster " + String(MY_ADDRESS) + " to Client " + String(MY_CLIENT_ADDRESS);
        if (rs485Stack.sendMessage(MY_CLIENT_ADDRESS, RS485SecureStack::MSG_TYPE_DATA, data)) {
          Serial.print("Sent data to Client "); Serial.println(MY_CLIENT_ADDRESS);
          lastSendAttemptTime = millis();
          currentSubmasterState = SUBMASTER_STATE_IDLE; // Sent one message, now idle
          // Inform Master that sending is done (optional, but good for token passing)
          manageDERE(true);
          rs485Stack.sendMessage(0, RS485SecureStack::MSG_TYPE_DATA, "DONE_SENDING");
          manageDERE(false);
        } else {
          Serial.println("Failed to send data (buffer full or other error).");
        }
        manageDERE(false); // Back to receive
      }
      break;

    case SUBMASTER_STATE_IDLE:
      // Submaster is idle, waiting for next permission or internal task
      // Serial.println("Submaster is idle.");
      delay(10); // Don't spam serial
      break;
  }
}

// Callback-Funktion, wird von RS485SecureStack aufgerufen
void onPacketReceived(byte senderAddr, char msgType, const String& payload) {
    // --- Master Heartbeat verarbeiten ---
    if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_MASTER_HEARTBEAT) {
        lastMasterHeartbeatRxTime = millis();
        if (!masterIsPresent) {
            masterIsPresent = true;
            Serial.println("Master heartbeat received. Master is online.");
            if (currentSubmasterState == SUBMASTER_STATE_WAITING_FOR_MASTER) {
                currentSubmasterState = SUBMASTER_STATE_WAITING_FOR_PERMISSION;
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

    // --- Send Permission from Master ---
    if (senderAddr == 0 && msgType == RS485SecureStack::MSG_TYPE_DATA && payload == "PERMISSION_TO_SEND") {
        if (currentSubmasterState == SUBMASTER_STATE_WAITING_FOR_PERMISSION || currentSubmasterState == SUBMASTER_STATE_IDLE) {
            Serial.println("Received PERMISSION_TO_SEND from Master. Ready to send data.");
            currentSubmasterState = SUBMASTER_STATE_SENDING_DATA;
            lastSendAttemptTime = millis(); // Reset timer for sending
        }
        rs485Stack.sendAckNack(senderAddr, msgType, true);
        return;
    }

    // --- Empfangene Daten (von Clients oder anderen Submastern) ---
    if (msgType == RS458SecureStack::MSG_TYPE_DATA) {
        Serial.print("DATA RECEIVED from "); Serial.print(senderAddr);
        Serial.print(", Payload: '"); Serial.print(payload); Serial.println("'");
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
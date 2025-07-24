#include "RS485SecureStack.h"
#include "credantials.h" // Pre-Shared Master Key (MUSS AUF ALLEN DEVICES IDENTISCH SEIN!)
#include "KeyRotationManager.h" // NEU: Key Rotation Manager
#include <map> // For tracking client states
#include <vector> // For managing nodes

// --- RS485 Konfiguration ---
HardwareSerial& rs485Serial = Serial1;
const int RS485_DE_RE_PIN = 3; // Beispiel GPIO für DE/RE Pin, anpassen!

const byte MY_ADDRESS = 0; // Master/Scheduler Adresse

RS485SecureStack rs485Stack(rs485Serial, MY_ADDRESS, MASTER_KEY);
KeyRotationManager keyManager; // NEU: Instanz des KeyRotationManagers

// --- Master Management State ---
enum MasterState {
    MASTER_STATE_INITIALIZING,
    MASTER_STATE_BAUD_RATE_TESTING,
    MASTER_STATE_OPERATIONAL,
    MASTER_STATE_ROUGE_MASTER_DETECTED // Safety state
};
MasterState currentMasterState = MASTER_STATE_INITIALIZING; [cite: 1]

// --- Node Management ---
struct NodeInfo {
    byte address;
    unsigned long lastContactTime;
    unsigned int ackCount;
    unsigned int nackCount;
    bool isSubmaster;
}; [cite: 1]

std::map<byte, NodeInfo> connectedNodes;
std::vector<byte> submasterQueue; // Queue for token passing
int currentSubmasterIdx = 0;

// --- Baud Rate Management ---
const long BAUD_RATES[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
const int NUM_BAUD_RATES = sizeof(BAUD_RATES) / sizeof(BAUD_RATES[0]); [cite: 1]
int currentBaudRateIdx = 0;
long activeBaudRate = 0;
unsigned long baudRateTestStartTime = 0;
const unsigned long BAUD_RATE_TEST_DURATION_MS = 2000; // Test each baud rate for 2 seconds 
const int MIN_TEST_ACKS = 5; // Minimum ACKs expected during a test
long bestBaudRate = 9600;

// --- Master Heartbeat ---
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL_MS = 200; // Send heartbeat every 200ms 
const unsigned long ROUGE_MASTER_TIMEOUT_MS = 500; // If rogue master heartbeats are too frequent, declare collision 
unsigned long lastRogueHeartbeatTime = 0; // Time of last detected rogue heartbeat
byte rogueMasterAddress = 0;

// --- Rekeying Interval ---
// Das Rekeying-Intervall wird nun vom KeyRotationManager verwendet.
const unsigned long REKEY_INTERVAL_MS = 60 * 60 * 1000; // Rekey every hour 

// --- Callbacks ---
void onPacketReceived(byte senderAddr, char msgType, const String& payload);
void manageDERE(bool transmit); // Function to control DE/RE pin

// NEU: Utility-Funktion zur Konvertierung eines Byte-Arrays in einen Hex-String
String bytesToHexString(const byte* bytes, size_t len) {
    String hexString = "";
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] < 0x10) {
            hexString += "0";
        }
        hexString += String(bytes[i], HEX);
    }
    return hexString;
}

// NEU: Callback-Funktion für die Schlüsselgenerierung und -verteilung
// Diese Funktion wird vom KeyRotationManager aufgerufen, wenn ein neuer Schlüssel benötigt wird.
void handleKeyGenerationAndDistribution(uint16_t newKeyId, const byte newKey[AES_KEY_SIZE]) {
    Serial.print("Master: Initiating key rotation. New Key ID: ");
    Serial.println(newKeyId);

    // 1. Neuen Schlüssel im eigenen Stack speichern und als aktuellen Schlüssel setzen
    rs485Stack.setSessionKey(newKeyId, newKey);
    rs485Stack.setCurrentKeyId(newKeyId);
    Serial.println("Master: New key set and activated locally.");

    // 2. Neuen Schlüssel an alle bekannten Clients und Submaster verteilen
    // Das Payload-Format ist "KEY_UPDATE:<keyId_dezimal>:<hexKeyString>"
    String hexKeyString = bytesToHexString(newKey, AES_KEY_SIZE);
    String payload = "KEY_UPDATE:" + String(newKeyId) + ":" + hexKeyString;

    // Sicherstellen, dass der DE/RE Pin im Sendemodus ist
    manageDERE(true);
    delay(10); // Kurze Pause nach DE/RE Umschaltung für Bus-Stabilität

    for (auto const& [addr, node] : connectedNodes) {
        if (addr != MY_ADDRESS) { // Nicht an sich selbst senden
            Serial.print("Master: Sending key update to node ");
            Serial.print(addr);
            Serial.print(" with payload: ");
            Serial.println(payload);
            rs485Stack.sendMessage(addr, RS485SecureStack::MSG_TYPE_KEY_UPDATE, payload);
            delay(20); // Kleine Verzögerung zwischen den Nachrichten, um Bus-Kollisionen zu vermeiden
        }
    }
    manageDERE(false); // Zurück in den Empfangsmodus

    Serial.println("Master: Key update messages broadcasted.");
}

void setup() {
  Serial.begin(115200); // USB-Serial für Debugging
  pinMode(RS485_DE_RE_PIN, OUTPUT);
  manageDERE(false); // Start in Receive Mode
  rs485Stack.begin(9600); // RS485-Stack initialisieren mit Basis-Baudrate

  Serial.println("Scheduler (Master) ESP32-C3 gestartet.");
  
  rs485Stack.registerReceiveCallback(onPacketReceived);
  rs485Stack.setAckEnabled(true); // Master muss ACKs empfangen können

  // --- NEU: KeyRotationManager initialisieren ---
  // Der Manager generiert und verteilt nun den initialen Schlüssel sowie alle weiteren.
  keyManager.begin(handleKeyGenerationAndDistribution, &rs485Stack, REKEY_INTERVAL_MS);
  rs485Stack.setKeyRotationManager(&keyManager);

  // Die manuelle Initialisierung der Session Keys entfällt hier,
  // da der KeyRotationManager den initialen Schlüssel erzeugt und verteilt.
  // const byte initialKey0[16] = { ... };
  // const byte initialKey1[16] = { ... };
  // rs485Stack.setSessionKey(0, initialKey0);
  // rs485Stack.setSessionKey(1, initialKey1);
  // rs485Stack.setCurrentKeyId(0); 

  // Definiere die bekannten Nodes (für PoC fest verdrahtet)
  connectedNodes[1] = {1, 0, 0, 0, true}; // Submaster 1 
  connectedNodes[2] = {2, 0, 0, 0, true}; // Submaster 2 
  submasterQueue.push_back(1);
  submasterQueue.push_back(2);
  // Clients (reagieren auf Anfragen)
  connectedNodes[11] = {11, 0, 0, 0, false}; // Client 11 
  connectedNodes[12] = {12, 0, 0, 0, false}; // Client 12 

  // Beginne mit Baudraten-Einmessung
  currentMasterState = MASTER_STATE_BAUD_RATE_TESTING; [cite: 1]
  currentBaudRateIdx = 0;
  Serial.print("Starting Baud Rate Test at: ");
  Serial.println(BAUD_RATES[currentBaudRateIdx]);
  rs485Stack._setBaudRate(BAUD_RATES[currentBaudRateIdx]);
  baudRateTestStartTime = millis();
}

void loop() {
  rs485Stack.processIncoming(); // Pakete empfangen und verarbeiten
  manageDERE(false); // Ensure we are in receive mode by default

  keyManager.update(); // NEU: Regelmäßiges Update für den Schlüsselmanager
                       // Dies prüft, ob eine Schlüsselrotation notwendig ist.

  switch (currentMasterState) {
    case MASTER_STATE_INITIALIZING:
      // Should transition to BAUD_RATE_TESTING in setup
      break;
    case MASTER_STATE_BAUD_RATE_TESTING:
      if (millis() - baudRateTestStartTime > BAUD_RATE_TEST_DURATION_MS) {
        // Evaluate current baud rate
        Serial.print("Finished test for ");
        Serial.print(BAUD_RATES[currentBaudRateIdx]); Serial.println(" bps.");
        int totalAcks = 0;
        for (auto const& [addr, node] : connectedNodes) {
          totalAcks += node.ackCount;
        }

        Serial.print("Total ACKs received for this test: "); Serial.println(totalAcks);
        if (totalAcks >= MIN_TEST_ACKS) { // We got enough ACKs to consider this baud rate stable
          bestBaudRate = BAUD_RATES[currentBaudRateIdx];
          Serial.print("Found stable baud rate: "); Serial.println(bestBaudRate);
          currentMasterState = MASTER_STATE_OPERATIONAL;
          activeBaudRate = bestBaudRate;
          // rs485Stack.setCurrentKeyId(currentSessionKeyId); // Dies wird nun vom KeyRotationManager gehandhabt
          // Inform all nodes about the final baud rate
          for (auto const& [addr, node] : connectedNodes) {
            manageDERE(true); // Set to transmit for sending
            rs485Stack.sendMessage(addr, RS485SecureStack::MSG_TYPE_BAUD_RATE_SET, String(bestBaudRate));
            delay(10); // Small delay to allow bus to settle
            manageDERE(false); // Back to receive
          }
          Serial.println("Transitioning to OPERATIONAL state.");
          // lastRekeyTime = millis(); // Nicht mehr benötigt, wird vom KeyRotationManager verwaltet
          lastHeartbeatTime = millis(); // Start heartbeat timer
          break; // Exit baud rate testing
        }
        // Reset ACK counts for next test
        for (auto& pair : connectedNodes) {
          pair.second.ackCount = 0;
          pair.second.nackCount = 0;
        }
        // Try next baud rate
        currentBaudRateIdx++;
        if (currentBaudRateIdx < NUM_BAUD_RATES) {
          Serial.print("Trying next baud rate: ");
          Serial.println(BAUD_RATES[currentBaudRateIdx]);
          rs485Stack._setBaudRate(BAUD_RATES[currentBaudRateIdx]);
          baudRateTestStartTime = millis();
          // Send some dummy packets to provoke ACKs
          for (auto const& [addr, node] : connectedNodes) {
            if (node.isSubmaster) { // Only test Submasters, clients might not respond directly
              manageDERE(true);
              rs485Stack.sendMessage(addr, RS485SecureStack::MSG_TYPE_DATA, "TEST_BAUD");
              delay(10); // Small delay
              manageDERE(false);
            }
          }
        } else {
          Serial.println("No stable baud rate found. Sticking with best found or default.");
          activeBaudRate = bestBaudRate; // Default to the best one found so far (could be 9600)
          currentMasterState = MASTER_STATE_OPERATIONAL; // Force transition to operational
          // lastRekeyTime = millis(); // Nicht mehr benötigt
          lastHeartbeatTime = millis();
        }
      }
      break;

    case MASTER_STATE_OPERATIONAL:
      // Master Heartbeat senden
      if (millis() - lastHeartbeatTime > HEARTBEAT_INTERVAL_MS) {
        manageDERE(true);
        rs485Stack.sendMessage(RS485SecureStack::BROADCAST_ADDRESS, RS485SecureStack::MSG_TYPE_MASTER_HEARTBEAT, "");
        manageDERE(false);
        lastHeartbeatTime = millis();
      }

      // Token Passing (Submaster-Management)
      if (!submasterQueue.empty() && millis() - connectedNodes[submasterQueue[currentSubmasterIdx]].lastContactTime > 500) { // Give token to next submaster after a delay or upon confirmation
        byte nextSubmaster = submasterQueue[currentSubmasterIdx];
        manageDERE(true);
        rs485Stack.sendMessage(nextSubmaster, RS485SecureStack::MSG_TYPE_PERMISSION_TO_SEND, "");
        manageDERE(false);
        Serial.print("Master: Giving permission to send to Submaster: ");
        Serial.println(nextSubmaster);
        connectedNodes[nextSubmaster].lastContactTime = millis(); // Update last contact to prevent immediate re-permission
        currentSubmasterIdx = (currentSubmasterIdx + 1) % submasterQueue.size();
      }

      // Rogue Master Detection
      if (rogueMasterAddress != 0 && millis() - lastRogueHeartbeatTime < ROUGE_MASTER_TIMEOUT_MS) {
          Serial.print("!!! MASTER: Rogue Master detected at address: ");
          Serial.println(rogueMasterAddress);
          currentMasterState = MASTER_STATE_ROUGE_MASTER_DETECTED;
      }
      break;

    case MASTER_STATE_ROUGE_MASTER_DETECTED:
      Serial.println("!!! WARNING: ROGUE MASTER DETECTED! HALTING TRANSMISSIONS. !!!");
      delay(100); // Wait in safe state
      break;
  }
}

// Callback-Funktion, wird von RS485SecureStack aufgerufen
void onPacketReceived(byte senderAddr, char msgType, const String& payload) {
  // --- Baudraten-Antworten verarbeiten (für Baudraten-Einmessung) ---
  if (currentMasterState == MASTER_STATE_BAUD_RATE_TESTING && msgType == RS485SecureStack::MSG_TYPE_ACK) {
    if (connectedNodes.count(senderAddr)) {
      connectedNodes[senderAddr].ackCount++;
      Serial.print("ACK from "); Serial.println(senderAddr);
    }
  }

  // --- Heartbeat-Antworten verarbeiten (von Submastern) ---
  if (msgType == RS485SecureStack::MSG_TYPE_HEARTBEAT) {
      if (connectedNodes.count(senderAddr)) {
          connectedNodes[senderAddr].lastContactTime = millis();
          // Serial.print("Heartbeat from: "); Serial.println(senderAddr);
      }
  }

  // --- Rogue Master Detection (wenn Master einen fremden Heartbeat empfängt) ---
  if (msgType == RS485SecureStack::MSG_TYPE_MASTER_HEARTBEAT) {
      if (senderAddr != MY_ADDRESS) { // Nicht mein eigener Heartbeat
          if (rogueMasterAddress == 0 || rogueMasterAddress == senderAddr) {
              rogueMasterAddress = senderAddr;
              lastRogueHeartbeatTime = millis();
              Serial.print("Master: Heard Master Heartbeat from unexpected address: ");
              Serial.println(senderAddr);
          }
      }
  }
  
  // --- Bestätigung nach Senden vom Submaster ---
  if (msgType == RS485SecureStack::MSG_TYPE_DATA && payload == "DONE_SENDING") {
      Serial.print("Master: Received DONE_SENDING from Submaster ");
      Serial.println(senderAddr);
      // Hier könnte man den Token weitergeben oder den Submaster-Index aktualisieren
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
    digitalWrite(RS458_DE_RE_PIN, LOW); // Empfangen aktivieren
  }
}

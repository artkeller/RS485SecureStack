#include "RS485SecureStack.h"
#include "credantials.h" // Pre-Shared Master Key (MUSS AUF ALLEN DEVICES IDENTISCH SEIN!)
#include <map> // For tracking client states
#include <vector> // For managing nodes

// --- RS485 Konfiguration ---
// WICHTIG: Für ESP32-C3 Devboards ist Serial (UART0) oft für USB-CDC verwendet.
// Wenn RS485 an dedizierten Pins angeschlossen wird, bitte Serial1 oder Serial2 verwenden!
// Beispiel für Serial1 an GPIO9 (RX) und GPIO10 (TX) - Prüfe Pinout deines C3!
// HardwareSerial& rs485Serial = Serial1; 
// const int RS485_RX_PIN = 9;
// const int RS485_TX_PIN = 10;
// const int RS485_DE_RE_PIN = 8; // Beispiel DE/RE Pin

// Für PoC mit Default UART1, da sonst Konflikt mit USB-Serial besteht:
HardwareSerial& rs485Serial = Serial1;
// ACHTUNG: Wenn Serial auch für USB-Debugausgabe genutzt wird, können Kollisionen auftreten.
// Eine separate UART (Serial1/Serial2) für RS485 ist empfohlen für stabile Kommunikation.

const int RS485_DE_RE_PIN = 3; // Beispiel GPIO für DE/RE Pin, anpassen!

const byte MY_ADDRESS = 0; // Master/Scheduler Adresse

RS485SecureStack rs485Stack(rs485Serial, MY_ADDRESS, MASTER_KEY);

// --- Master Management State ---
enum MasterState {
    MASTER_STATE_INITIALIZING,
    MASTER_STATE_BAUD_RATE_TESTING,
    MASTER_STATE_OPERATIONAL,
    MASTER_STATE_ROUGE_MASTER_DETECTED // Safety state
};
MasterState currentMasterState = MASTER_STATE_INITIALIZING;

// --- Node Management ---
struct NodeInfo {
    byte address;
    unsigned long lastContactTime;
    unsigned int ackCount;
    unsigned int nackCount;
    bool isSubmaster;
};

std::map<byte, NodeInfo> connectedNodes;
std::vector<byte> submasterQueue; // Queue for token passing
int currentSubmasterIdx = 0;

// --- Baud Rate Management ---
const long BAUD_RATES[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
const int NUM_BAUD_RATES = sizeof(BAUD_RATES) / sizeof(BAUD_RATES[0]);
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

// --- Rekeying ---
unsigned long lastRekeyTime = 0;
const unsigned long REKEY_INTERVAL_MS = 60 * 60 * 1000; // Rekey every hour (for production, more frequent or event-driven)
uint16_t currentSessionKeyId = 0;
byte sessionKeyPool[2][16]; // Store two keys: current and next

// --- Callbacks ---
void onPacketReceived(byte senderAddr, char msgType, const String& payload);
void manageDERE(bool transmit); // Function to control DE/RE pin

void setup() {
  Serial.begin(115200); // USB-Serial für Debugging
  pinMode(RS485_DE_RE_PIN, OUTPUT);
  manageDERE(false); // Start in Receive Mode
  rs485Stack.begin(9600); // RS485-Stack initialisieren mit Basis-Baudrate

  Serial.println("Scheduler (Master) ESP32-C3 gestartet.");
  
  rs485Stack.registerReceiveCallback(onPacketReceived);
  rs485Stack.setAckEnabled(true); // Master muss ACKs empfangen können

  // Initialisiere die Session Keys
  // Key ID 0
  const byte initialKey0[16] = {
      0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
      0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32
  };
  // Key ID 1 (could be a truly random key for production)
  const byte initialKey1[16] = {
      0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22,
      0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00
  };
  rs485Stack.setSessionKey(0, initialKey0);
  rs485Stack.setSessionKey(1, initialKey1);
  rs485Stack.setCurrentKeyId(0); // Start with Key ID 0

  // Definiere die bekannten Nodes (für PoC fest verdrahtet)
  // Submaster (erhalten Token zum Senden)
  connectedNodes[1] = {1, 0, 0, 0, true};
  connectedNodes[2] = {2, 0, 0, 0, true};
  submasterQueue.push_back(1);
  submasterQueue.push_back(2);
  // Clients (reagieren auf Anfragen)
  connectedNodes[11] = {11, 0, 0, 0, false};
  connectedNodes[12] = {12, 0, 0, 0, false};

  // Beginne mit Baudraten-Einmessung
  currentMasterState = MASTER_STATE_BAUD_RATE_TESTING;
  currentBaudRateIdx = 0;
  Serial.print("Starting Baud Rate Test at: ");
  Serial.println(BAUD_RATES[currentBaudRateIdx]);
  rs485Stack._setBaudRate(BAUD_RATES[currentBaudRateIdx]);
  baudRateTestStartTime = millis();
}

void loop() {
  rs485Stack.processIncoming(); // Pakete empfangen und verarbeiten
  manageDERE(false); // Ensure we are in receive mode by default

  switch (currentMasterState) {
    case MASTER_STATE_INITIALIZING:
      // Should transition to BAUD_RATE_TESTING in setup
      break;

    case MASTER_STATE_BAUD_RATE_TESTING:
      if (millis() - baudRateTestStartTime > BAUD_RATE_TEST_DURATION_MS) {
        // Evaluate current baud rate
        Serial.print("Finished test for "); Serial.print(BAUD_RATES[currentBaudRateIdx]); Serial.println(" bps.");
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
          rs485Stack.setCurrentKeyId(currentSessionKeyId); // Ensure current key is active
          // Inform all nodes about the final baud rate
          for (auto const& [addr, node] : connectedNodes) {
            manageDERE(true); // Set to transmit for sending
            rs485Stack.sendMessage(addr, RS485SecureStack::MSG_TYPE_BAUD_RATE_SET, String(bestBaudRate));
            delay(10); // Small delay to allow bus to settle
            manageDERE(false); // Back to receive
          }
          Serial.println("Transitioning to OPERATIONAL state.");
          lastRekeyTime = millis(); // Start rekey timer
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
          currentMasterState = MASTER_STATE_OPERATIONAL;
          rs485Stack._setBaudRate(bestBaudRate);
          // Inform all nodes about the final baud rate
          for (auto const& [addr, node] : connectedNodes) {
            manageDERE(true);
            rs485Stack.sendMessage(addr, RS485SecureStack::MSG_TYPE_BAUD_RATE_SET, String(bestBaudRate));
            delay(10);
            manageDERE(false);
          }
          Serial.println("Transitioning to OPERATIONAL state after test.");
          lastRekeyTime = millis();
          lastHeartbeatTime = millis();
        }
      }
      break;

    case MASTER_STATE_OPERATIONAL:
      // --- Master Heartbeat ---
      if (millis() - lastHeartbeatTime > HEARTBEAT_INTERVAL_MS) {
        manageDERE(true); // Set to transmit
        rs485Stack.sendMessage(0xFF, RS485SecureStack::MSG_TYPE_MASTER_HEARTBEAT, ""); // Broadcast Heartbeat
        delay(1); // Small delay for bus
        manageDERE(false); // Back to receive
        lastHeartbeatTime = millis();
      }

      // --- Rekeying ---
      if (millis() - lastRekeyTime > REKEY_INTERVAL_MS) {
        Serial.println("Initiating Rekeying process...");
        currentSessionKeyId = (currentSessionKeyId + 1) % 2; // Toggle between 0 and 1
        
        // In a real system, you'd generate a new random key here
        // For PoC, use the other pre-defined key
        // generateRandomKey(sessionKeyPool[currentSessionKeyId]);
        
        rs485Stack.setCurrentKeyId(currentSessionKeyId); // Master switches to new key
        
        // Inform all nodes about the new key
        for (auto const& [addr, node] : connectedNodes) {
          manageDERE(true);
          rs485Stack.sendMessage(addr, RS485SecureStack::MSG_TYPE_KEY_UPDATE, String(currentSessionKeyId));
          delay(10); // Small delay between sends
          manageDERE(false);
        }
        Serial.print("New session Key ID: "); Serial.println(currentSessionKeyId);
        lastRekeyTime = millis();
      }

      // --- Token Passing (Example: Cycle through Submasters to grant send permission) ---
      // This is a simplified token passing. More robust would involve timeouts, retries, etc.
      if (!submasterQueue.empty() && millis() - connectedNodes[submasterQueue[currentSubmasterIdx]].lastContactTime > 1000) {
        manageDERE(true);
        byte targetSubmaster = submasterQueue[currentSubmasterIdx];
        Serial.print("Granting send permission to Submaster: "); Serial.println(targetSubmaster);
        // MSG_TYPE_DATA with a specific payload indicating "PERMISSION_TO_SEND"
        // Or create a new MSG_TYPE_PERMISSION_TO_SEND
        rs485Stack.sendMessage(targetSubmaster, RS485SecureStack::MSG_TYPE_DATA, "PERMISSION_TO_SEND");
        delay(10);
        manageDERE(false);
        // Move to next submaster after some time or ACK from current
        currentSubmasterIdx = (currentSubmasterIdx + 1) % submasterQueue.size();
      }
      break;

    case MASTER_STATE_ROUGE_MASTER_DETECTED:
      // Master is in a safety state. It stops sending its own heartbeats or permissions.
      // It only monitors the bus for the rogue master to disappear.
      Serial.print("!!! DANGER MODE: ROGUE MASTER DETECTED at address ");
      Serial.print(rogueMasterAddress);
      Serial.println(" !!! All operations halted.");
      // Potentially flash an LED, trigger an alarm, etc.
      if (millis() - lastRogueHeartbeatTime > ROUGE_MASTER_TIMEOUT_MS * 2) {
          // Rogue master has gone silent
          Serial.println("Rogue master gone silent. Resuming normal operations.");
          currentMasterState = MASTER_STATE_OPERATIONAL;
          rogueMasterAddress = 0;
          lastHeartbeatTime = millis(); // Reset heartbeat timer to start sending again
      }
      break;
  }
}

// Callback-Funktion, wird von RS485SecureStack aufgerufen
void onPacketReceived(byte senderAddr, char msgType, const String& payload) {
    // Update last contact time for any node that sends a packet (valid or not)
    if (connectedNodes.count(senderAddr)) {
        connectedNodes[senderAddr].lastContactTime = millis();
    }
    
    // --- Master Heartbeat von anderen Adressen prüfen (Rogue Master Detection) ---
    if (msgType == RS485SecureStack::MSG_TYPE_MASTER_HEARTBEAT && senderAddr != MY_ADDRESS) {
        Serial.print("!!! WARNING: Detected MASTER HEARTBEAT from UNEXPECTED ADDRESS: ");
        Serial.println(senderAddr);
        rogueMasterAddress = senderAddr;
        lastRogueHeartbeatTime = millis();
        currentMasterState = MASTER_STATE_ROUGE_MASTER_DETECTED; // Enter safety mode
        return; // Don't process further if a rogue master heartbeat
    }

    // --- ACK/NACK Verarbeitung ---
    if (msgType == RS485SecureStack::MSG_TYPE_ACK) {
        // payload could contain original msgType + result, e.g., "D1" for Data ACK success
        // For baud rate testing, just count total ACKs
        if (connectedNodes.count(senderAddr)) {
            connectedNodes[senderAddr].ackCount++;
        }
        Serial.print("ACK from "); Serial.print(senderAddr); Serial.print(" (Payload: "); Serial.print(payload); Serial.println(")");
        return; // No further processing for ACK messages
    } else if (msgType == RS485SecureStack::MSG_TYPE_NACK) {
        if (connectedNodes.count(senderAddr)) {
            connectedNodes[senderAddr].nackCount++;
        }
        Serial.print("NACK from "); Serial.print(senderAddr); Serial.print(" (Payload: "); Serial.print(payload); Serial.println(")");
        return; // No further processing for NACK messages
    }

    // --- Standard Daten-Pakete (nur zum Loggen im Master) ---
    if (msgType == RS485SecureStack::MSG_TYPE_DATA) {
        Serial.print("DATA FROM: "); Serial.print(senderAddr);
        Serial.print(" TYPE: '"); Serial.print(msgType);
        Serial.print("' PAYLOAD: '"); Serial.print(payload); Serial.println("'");

        // Example: Master confirms submaster finished sending
        if (payload == "DONE_SENDING" && connectedNodes.count(senderAddr) && connectedNodes[senderAddr].isSubmaster) {
             Serial.print("Submaster "); Serial.print(senderAddr); Serial.println(" finished its turn.");
             // Potentially give permission to next submaster here or based on next cycle
        }
    }
    // Master sends ACK for any valid packet it receives (excluding heartbeats and ACKs/NACKs which are handled above)
    rs485Stack.sendAckNack(senderAddr, msgType, true); 
}

// Helper function to control the DE/RE pin
void manageDERE(bool transmit) {
  if (transmit) {
    digitalWrite(RS485_DE_RE_PIN, HIGH);
  } else {
    digitalWrite(RS485_DE_RE_PIN, LOW);
  }
}

// Function to generate a new random session key (for production)
void generateRandomKey(byte key[16]) {
    for (int i = 0; i < 16; i++) {
        key[i] = random(256); // Use a cryptographically secure random number generator in production!
    }
}
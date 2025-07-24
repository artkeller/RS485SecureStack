#include "KeyRotationManager.h"
#include "RS485SecureStack.h" // Muss hier inkludiert werden, da wir die Klasse nutzen
#ifdef ESP32
#include <esp_system.h> // Für esp_random() auf ESP32
#endif

// Default-Werte für die Policies
#define DEFAULT_ROTATION_INTERVAL_MS (60 * 60 * 1000UL) // 1 Stunde
#define DEFAULT_MESSAGE_COUNT_THRESHOLD 1000UL        // 1000 Nachrichten

KeyRotationManager::KeyRotationManager()
    : _secureStack(nullptr),
      _rotationIntervalMs(DEFAULT_ROTATION_INTERVAL_MS),
      _messageCountThreshold(DEFAULT_MESSAGE_COUNT_THRESHOLD),
      _lastRotationTime(0),
      _messagesSentSinceLastRotation(0),
      _currentManagedKeyId(0), // Startet mit Key ID 0
      _keyGenCallback(nullptr)
{
    // Konstruktor initialisiert nur Member. begin() muss aufgerufen werden, um Referenzen zu setzen.
}

void KeyRotationManager::begin(KeyGenerationAndDistributionCallback keyGenCallback, RS485SecureStack* secureStackInstance) {
    _secureStack = secureStackInstance;
    _keyGenCallback = keyGenCallback;
    _lastRotationTime = millis(); // Setze die Startzeit
    _messagesSentSinceLastRotation = 0;
    
    // Stelle sicher, dass der KeyRotationManager die gleiche initiale KeyID verwendet
    // wie der RS485SecureStack. Standardmäßig ist das KeyID 0.
    if (_secureStack) {
        _currentManagedKeyId = _secureStack->_currentSessionKeyId; 
    } else {
        Serial.println("Warnung: KeyRotationManager::begin - secureStack ist nullptr!");
    }

    Serial.println("KeyRotationManager gestartet.");
    Serial.print("Initial Rotation Interval: "); Serial.print(_rotationIntervalMs); Serial.println(" ms");
    Serial.print("Initial Message Count Threshold: "); Serial.println(_messageCountThreshold);
}

void KeyRotationManager::update() {
    // Wenn kein secureStack oder Callback gesetzt ist, kann der Manager nichts tun.
    if (!_secureStack || !_keyGenCallback) {
        // Serial.println("KeyRotationManager nicht vollständig initialisiert.");
        return;
    }

    // Prüfe zeitbasierte Rotation
    if (_rotationIntervalMs > 0 && (millis() - _lastRotationTime >= _rotationIntervalMs)) {
        Serial.println("KeyRotationManager: Zeitbasiertes Intervall erreicht. Schlüsselrotation triggern.");
        _triggerKeyRotation();
        return; // Nur einen Trigger pro Update-Zyklus
    }

    // Prüfe Nachrichtenanzahl-basierte Rotation
    if (_messageCountThreshold > 0 && _messagesSentSinceLastRotation >= _messageCountThreshold) {
        Serial.println("KeyRotationManager: Nachrichtenanzahl-Schwelle erreicht. Schlüsselrotation triggern.");
        _triggerKeyRotation();
        return; // Nur einen Trigger pro Update-Zyklus
    }
}

void KeyRotationManager::notifyMessageSent() {
    _messagesSentSinceLastRotation++;
    // Wird im update() gecheckt, wenn Threshold > 0
}

void KeyRotationManager::setRotationInterval(unsigned long intervalMs) {
    _rotationIntervalMs = intervalMs;
    Serial.print("KeyRotationManager: Rotation Interval auf "); Serial.print(_rotationIntervalMs); Serial.println(" ms gesetzt.");
}

void KeyRotationManager::setMessageCountThreshold(unsigned long count) {
    _messageCountThreshold = count;
    Serial.print("KeyRotationManager: Message Count Threshold auf "); Serial.print(_messageCountThreshold); Serial.println(" gesetzt.");
}

unsigned long KeyRotationManager::getTimeSinceLastRotation() {
    return millis() - _lastRotationTime;
}

unsigned long KeyRotationManager::getMessagesSinceLastRotation() {
    return _messagesSentSinceLastRotation;
}

uint16_t KeyRotationManager::getCurrentKeyId() {
    return _currentManagedKeyId;
}

void KeyRotationManager::_triggerKeyRotation() {
    // Ermittle die nächste Key ID im Ringpuffer
    // MAX_SESSION_KEYS kommt aus RS485SecureStack.h, muss aber hier verfügbar sein.
    // Daher direkt hier wieder definiert oder über Getter. Für PoC direct define.
    // Best Practice wäre es, MAX_SESSION_KEYS in einer gemeinsamen Datei (z.B. config.h) zu haben
    // oder über eine Methode des RS485SecureStack zu bekommen.
    uint16_t newKeyId = (_currentManagedKeyId + 1) % 5; // Annahme: MAX_SESSION_KEYS = 5

    // Generiere einen neuen, zufälligen Schlüssel (PoC-Implementierung)
    byte newKey[AES_KEY_SIZE];
    _generateRandomKey(newKey); // Füllt newKey mit zufälligen Bytes

    Serial.print("KeyRotationManager: Trigger Rotation. Neuer KeyId wird ");
    Serial.print(newKeyId);
    Serial.println(".");

    // Rufe den Callback im Master-Sketch auf, um den neuen Schlüssel zu verteilen
    if (_keyGenCallback) {
        _keyGenCallback(newKeyId, newKey);
        // Nach dem Callback ist der neue Schlüssel im RS485SecureStack gespeichert
        // und kann verwendet werden.
        _currentManagedKeyId = newKeyId; // Aktualisiere die vom Manager verwaltete Key ID
        _lastRotationTime = millis();     // Setze den Timer zurück
        _messagesSentSinceLastRotation = 0; // Setze den Nachrichten-Zähler zurück
    } else {
        Serial.println("Fehler: KeyGenerationAndDistributionCallback ist nicht registriert!");
        Serial.println("Schlüsselrotation konnte nicht durchgeführt werden.");
    }
}

// Hilfsfunktion: Generiert einen zufälligen AES-Schlüssel (NUR FÜR PoC!)
// In Produktion MUSS hier esp_random() oder eine andere kryptographisch sichere Methode verwendet werden.
void KeyRotationManager::_generateRandomKey(byte key[AES_KEY_SIZE]) {
    for (int i = 0; i < AES_KEY_SIZE; i++) {
        #ifdef ESP32
        key[i] = esp_random() & 0xFF; // Nutze den ESP32 Hardware-Zufallsgenerator
        #else
        key[i] = random(256); // Pseudo-Zufall für andere Plattformen
        #endif
    }
    Serial.println("KeyRotationManager: Zufälliger Schlüssel generiert (PoC-Modus).");
}

#ifndef KEY_ROTATION_MANAGER_H
#define KEY_ROTATION_MANAGER_H

#include <Arduino.h>

// Forward-Deklaration, um Zirkel-Inklusion zu vermeiden
class RS485SecureStack; 

// Makro für die AES Schlüsselgröße, da wir sie auch hier brauchen
#define AES_KEY_SIZE 16 // 128-bit AES key

// Callback-Typ für die Schlüssel-Generierung und -Verteilung
// Wird vom KeyRotationManager aufgerufen, wenn ein neuer Schlüssel benötigt wird.
// Der Master-Sketch muss diese Funktion implementieren, um den neuen Schlüssel zu liefern
// und die Verteilung über das Netzwerk zu initiieren.
typedef void (*KeyGenerationAndDistributionCallback)(uint16_t newKeyId, const byte newKey[AES_KEY_SIZE]);

class KeyRotationManager {
public:
    // Standard-Konstruktor mit Default-Werten
    KeyRotationManager();

    // Konstruktor zur Initialisierung mit benutzerdefinierten Werten
    KeyRotationManager(unsigned long rotationIntervalMs, unsigned long messageCountThreshold,
                       KeyGenerationAndDistributionCallback keyGenCallback, RS485SecureStack* secureStackInstance);

    // Initialisiert den Manager und setzt die Referenz auf die RS485SecureStack Instanz.
    // Dies muss im Setup des Master-Knotens aufgerufen werden.
    void begin(KeyGenerationAndDistributionCallback keyGenCallback, RS485SecureStack* secureStackInstance);

    // Diese Methode muss regelmäßig im Loop des Master-Sketches aufgerufen werden.
    // Sie prüft die Rotationsbedingungen und triggert ggf. einen Schlüsselwechsel.
    void update();

    // Methode, die nach jeder erfolgreichen Nachrichtenübertragung aufgerufen wird.
    void notifyMessageSent();

    // Setter für die Rotations-Policys
    void setRotationInterval(unsigned long intervalMs);     // Zeitbasiert in Millisekunden
    void setMessageCountThreshold(unsigned long count); // Nachrichtenanzahl-basiert

    // Getter für die aktuellen Metriken
    unsigned long getTimeSinceLastRotation();
    unsigned long getMessagesSinceLastRotation();
    uint16_t getCurrentKeyId();

private:
    RS485SecureStack* _secureStack; // Zeiger auf die RS485SecureStack-Instanz

    // Rotations-Policys
    unsigned long _rotationIntervalMs;   // Zeitintervall für Schlüsselrotation (Standard: 1 Stunde)
    unsigned long _messageCountThreshold; // Anzahl der Nachrichten für Schlüsselrotation (Standard: 1000 Nachrichten)

    // Interne Zustandsvariablen
    unsigned long _lastRotationTime;     // Millis() der letzten Rotation
    unsigned long _messagesSentSinceLastRotation; // Zähler für gesendete Nachrichten
    uint16_t _currentManagedKeyId;       // Die Key ID, die dieser Manager als aktuell betrachtet

    KeyGenerationAndDistributionCallback _keyGenCallback; // Callback für Schlüsselgenerierung/Verteilung

    // Interne Methode zum Triggern der Rotation
    void _triggerKeyRotation();

    // Hilfsfunktion zur Generierung eines neuen, zufälligen Schlüssels für den PoC
    void _generateRandomKey(byte key[AES_KEY_SIZE]);
};

#endif // KEY_ROTATION_MANAGER_H

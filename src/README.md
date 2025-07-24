# RS485SecureStack Library: Technisch detaillierte Dokumentation (src/)

(Alphaversion - aktuell nur auf deutsch)

---

## 💡 Einleitung und Zielsetzung

Die `RS485SecureStack` Bibliothek ist das Herzstück des gleichnamigen Projekts und bietet einen robusten sowie sicheren Kommunikationsstack über die RS485-Schnittstelle. Sie wurde entwickelt, um typische Herausforderungen in dezentralen Automatisierungsumgebungen, insbesondere im Kontext von **Industrie 4.0** und dem **Internet of Things (IoT)**, zu adressieren.

**Zielgruppe:** Diese Dokumentation richtet sich an technisch versierte Entwickler, die ein tiefes Verständnis der **Implementierungsdetails**, der **kryptographischen Operationen** und der **Safety-Mechanismen** innerhalb der Bibliothek benötigen, auch wenn sie keine umfassende Expertise in Kryptographie mitbringen.

**Kernprobleme, die adressiert werden:**
* **Vertraulichkeit:** Schutz sensibler Nutzdaten vor unbefugtem Auslesen.
* **Integrität:** Sicherstellung, dass übertragene Daten nicht manipuliert wurden.
* **Authentizität:** Verifizierung der Identität des Senders, um Spoofing zu verhindern.
* **Bus-Verfügbarkeit & -Stabilität:** Management von Bus-Ressourcen, Kollisionserkennung und Fehlerhandling.

---

## ⚙️ Architektur der Bibliothek (`src/` Verzeichnis)

Das `src/` Verzeichnis beherbergt die Bibliothek `RS485SecureStack` selbst. Diese ist plattformunabhängig im Hinblick auf die Logik, nutzt jedoch die **Hardware-Beschleunigung des ESP32** für kryptographische Operationen (AES, SHA256) über die bereitgestellten Arduino-Bibliotheken `Crypto`, `AES`, `SHA256` und `HMAC`.

### `RS485SecureStack.h` (Definitionen und Schnittstellen)

Diese Header-Datei deklariert die Klasse `RS485SecureStack` und definiert alle benötigten Konstanten, Datenstrukturen und Callback-Typen.

#### Wichtige Konstanten:

Die Konstanten definieren das Protokoll und die Sicherheitsparameter:

* **Protokollrahmen:**
    * `#define START_BYTE 0x02` (STX)
    * `#define END_BYTE 0x03` (ETX)
    * `#define ESCAPE_BYTE 0x10` (DLE)
    * `#define ESCAPE_XOR_MASK 0x20` (XOR-Maske für Escape-Sequenzen)
* **Nachrichtentypen (`MSG_TYPE_...`):**
    * `#define MSG_TYPE_DATA 'D'` (Generische Daten-Nachricht)
    * `#define MSG_TYPE_ACK 'A'` (Bestätigung des Empfangs)
    * `#define MSG_TYPE_NACK 'N'` (Negative Bestätigung, Fehler beim Empfang/Verarbeitung)
    * `#define MSG_TYPE_MASTER_HEARTBEAT 'H'` (Zur Master-Anwesenheitserkennung)
    * `#define MSG_TYPE_BAUD_RATE_SET 'B'` (Anweisung zur Baudratenänderung)
    * `#define MSG_TYPE_KEY_UPDATE 'K'` (Anweisung zum Session-Key-Wechsel)
    * **Hinweis:** Diese sind zusätzlich als `static const char` Member der Klasse definiert (z.B. `RS485SecureStack::MSG_TYPE_DATA`), was eine typsichere Verwendung und Kapselung innerhalb des Klassen-Namespaces ermöglicht, während die `#define`s die globale Verfügbarkeit für das Protokoll sicherstellen.
* **Sicherheitsparameter:**
    * `AES_KEY_SIZE = 16` (Bytes, entspricht AES-128)
    * `HMAC_KEY_SIZE = 32` (Bytes, SHA256 Hash-Größe für HMAC)
    * `HMAC_TAG_SIZE = 32` (Bytes, SHA256 Hash-Größe für HMAC-Output)
    * `AES_BLOCK_SIZE = 16` (Bytes)
    * `IV_SIZE = 16` (Bytes, Initialisierungsvektor für AES-CBC)
    * `MAX_PACKET_SIZE = 2048` (Maximale Größe des ungepackten Datenpakets inkl. Header und HMAC)
    * `MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - HEADER_SIZE - HMAC_TAG_SIZE - AES_BLOCK_SIZE` (Berechnete maximale Nutzlast)
* **Interne Zustände (`ReceiveState` Enum):** Definiert die Zustände der Empfangs-State-Machine: `WAITING_FOR_START_BYTE`, `RECEIVING_PAYLOAD`, `ESCAPING_BYTE`.

#### Klassenstruktur (`RS485SecureStack`):

* **Konstruktor:**
    * `RS485SecureStack(HardwareSerial& serial, byte localAddr, const byte* masterKey)`: Initialisiert den Stack mit der zu verwendenden HardwareSerial-Instanz, der lokalen Knotenadresse und dem **Pre-Shared Master Authentication Key (MAK)**.
* **Öffentliche Methoden:**
    * `begin(long baudRate)`: Startet die HardwareSerial-Kommunikation.
    * `registerReceiveCallback(ReceiveCallback callback)`: Registriert eine Callback-Funktion, die aufgerufen wird, wenn ein gültiges, entschlüsseltes Paket empfangen wurde.
    * `processIncoming()`: Die Kernfunktion, die kontinuierlich in der `loop()` aufgerufen werden muss, um eingehende Bytes zu verarbeiten.
    * `sendMessage(byte targetAddr, char msgType, const String& payload)`: Hauptmethode zum Senden von Daten.
    * `sendAckNack(byte targetAddr, char msgType, bool success)`: Sendet eine Bestätigung (ACK/NACK) an den Absender.
    * `setSessionKey(uint16_t keyId, const byte* key)`: Legt einen neuen Session Key für eine bestimmte ID im internen Key-Pool ab.
    * `setCurrentKeyId(uint16_t keyId)`: Wählt den aktuell zu verwendenden Session Key aus dem Pool.
    * `setBaudRate(long newBaudRate)`: Ändert die Hardware-Baudrate dynamisch.
    * `setDeRePin(int pin)`: Setzt den DE/RE-Pin für den RS485-Transceiver.
    * `setAckEnabled(bool enabled)`: Aktiviert/deaktiviert das automatische Senden von ACKs/NACKs.
* **Private Member:** Enthalten die State-Variablen für die Empfangs-State-Machine, Puffer für eingehende Daten, die HardwareSerial-Instanz, Adressen, Schlüssel-Pools und Zeiger auf die Crypto-Objekte (AES, HMAC, SHA256).

### `RS485SecureStack.cpp` (Implementierung)

Diese Datei enthält die konkrete Implementierung aller in `RS485SecureStack.h` deklarierten Methoden.

#### 1. Crypto-Subsystem (Kern der Sicherheit)

Die `RS485SecureStack` nutzt die **Hardware-Beschleunigung des ESP32** für die kryptographischen Operationen.

* **`_aes` (AES-Objekt):** Für die symmetrische Ver- und Entschlüsselung der Nutzlast.
* **`_hmac` (HMAC-Objekt):** Für die Berechnung und Verifikation von Message Authentication Codes.
* **`_sha256` (SHA256-Objekt):** Wird intern vom HMAC-Objekt genutzt.

#### 2. Paketstruktur (Wire Format)

Ein über den Bus gesendetes Rohpaket folgt diesem Aufbau:

`[START_BYTE] [STUFFED_DATA_BLOCK] [END_BYTE]`

Der `STUFFED_DATA_BLOCK` ist das Ergebnis des Byte-Stuffings des eigentlichen **Logischen Pakets**.

Das **Logische Paket** (vor dem Byte-Stuffing) hat die folgende Struktur:

| Feld                  | Größe (Bytes) | Beschreibung                                                                   |
| :-------------------- | :------------ | :----------------------------------------------------------------------------- |
| `Source Address`      | 1             | Adresse des sendenden Knotens.                                                 |
| `Target Address`      | 1             | Adresse des Zielknotens (oder `0xFF` für Broadcast).                           |
| `Message Type`        | 1             | Typ der Nachricht (z.B. `'D'`, `'A'`, `'K'`).                                  |
| `Key ID`              | 2             | ID des verwendeten Session Keys (für Rekeying).                                |
| `IV`                  | 16            | Initialisierungsvektor für die AES-CBC-Verschlüsselung der `Encrypted Payload`.|
| `Encrypted Payload`   | `N * 16`      | Die mit AES-128-CBC verschlüsselte Nutzlast, PKCS7-gepadded.                  |
| `HMAC-SHA256 Tag`     | 32            | Der Message Authentication Code über den gesamten Header und die `Encrypted Payload`, berechnet mit dem **Master Authentication Key (MAK)**. |

---

## 🔒 Detaillierter Crypto & Safety Workflow

Dieser Abschnitt erklärt die einzelnen Schritte, wie Nachrichten gesendet und empfangen werden, mit Fokus auf die kryptographischen und sicherheitsrelevanten Operationen.

### 1. Initialisierung und Schlüsselmanagement

* **Master Authentication Key (MAK):** Dieser `const byte MASTER_KEY[32]` ist ein **Pre-Shared Secret**. Er muss auf *allen* Geräten identisch sein und wird ausschließlich zur Berechnung und Verifikation des **HMAC** verwendet. Seine Vertraulichkeit ist entscheidend für die Authentizität und Integrität der Bus-Kommunikation. Er wird niemals über den Bus gesendet.
* **Session Keys (`_sessionKeyPool`):** Dies sind die Schlüssel, die für die **AES-128-Verschlüsselung und -Entschlüsselung** der *Nutzdaten* verwendet werden.
    * Die Bibliothek verwaltet einen Pool von bis zu `MAX_SESSION_KEYS` (aktuell 5) verschiedenen Session Keys.
    * Jeder Session Key wird durch eine `uint16_t keyId` identifiziert.
    * Der Master kann über `setSessionKey(keyId, newKey)` neue Schlüssel im Pool hinterlegen und dann alle Clients mittels einer `MSG_TYPE_KEY_UPDATE` Nachricht auffordern, zu einer neuen `keyId` zu wechseln (`setCurrentKeyId(newKeyId)`).
    * Dieser Mechanismus ermöglicht **dynamisches Rekeying**, was die Angriffsfläche verringert, da Angreifer einen Session Key nur für eine begrenzte Zeit nutzen können.

### 2. Senden einer Nachricht (`sendMessage` -> `buildAndSendPacket`)

1.  **`sendMessage(byte targetAddr, char msgType, const String& payload)`:**
    * Wird vom Anwender aufgerufen. Es wird sichergestellt, dass die Nutzlast nicht die `MAX_PAYLOAD_SIZE` überschreitet.
    * **IV-Generierung:** Ein **neuer, zufälliger 16-Byte IV** (Initialisierungsvektor) wird generiert. `_generateIV()` füllt den `_iv` Puffer mit pseudozufälligen Bytes. **In einer Produktionsumgebung ist hier ein kryptographisch sicherer Zufallszahlengenerator (CSPRNG) erforderlich!**
    * **PKCS7-Padding:** Die `payload` wird vor der Verschlüsselung mittels **PKCS7-Padding** auf ein Vielfaches der AES-Blockgröße (16 Bytes) aufgefüllt. Die Padding-Bytes geben die Anzahl der hinzugefügten Bytes an.
        * Beispiel: `[D A T A] [PAD] [PAD] ... [PAD]`
    * **AES-128-CBC-Verschlüsselung (`_aes.encrypt()`):**
        * Die gepaddete Payload wird mit dem **aktuell aktiven Session Key** (aus `_sessionKeyPool[_currentSessionKeyId]`) und dem generierten IV verschlüsselt.
        * Der **Cipher Block Chaining (CBC)**-Modus wird verwendet. Dieser Modus erfordert einen eindeutigen IV für jede Nachricht, um Muster in der Verschlüsselung zu vermeiden und die Sicherheit zu erhöhen.

2.  **`buildAndSendPacket(byte sourceAddr, byte targetAddr, char msgType, uint16_t keyId, const byte* iv, const byte* encryptedPayload, size_t encryptedPayloadLen)`:**
    * Diese interne Methode formatiert das Paket und wendet die Integritätsschutzmaßnahmen an.
    * **Paketkonstruktion im `_sendBuffer`:**
        * Die `Source Address`, `Target Address`, `Message Type`, `Key ID` und der `IV` werden an den Anfang des `_sendBuffer` geschrieben.
        * Die `encryptedPayload` wird direkt dahinter kopiert.
        * Dies bildet den Datenblock, über den der HMAC berechnet wird.
    * **HMAC-SHA256 Berechnung:**
        * `_hmac.setKey(MASTER_KEY, HMAC_KEY_SIZE);` Der **Pre-Shared Master Authentication Key (MAK)** wird als Schlüssel für den HMAC-Algorithmus gesetzt.
        * `_hmac.update(dataBlock, dataBlockLen);` Der HMAC wird über den gesamten Header und die verschlüsselte Nutzlast berechnet. **Dies ist entscheidend für die Integrität und Authentizität!** Wenn auch nur ein Bit in Header oder verschlüsselter Payload manipuliert wird, ändert sich der HMAC.
        * `_hmac.finalize(hmacTag, HMAC_TAG_SIZE);` Der 32-Byte HMAC-Tag wird erzeugt.
    * **HMAC-Tag anhängen:** Der berechnete `hmacTag` wird an das Ende des `_sendBuffer` gehängt.
    * **Byte-Stuffing (`stuffBytes`):**
        * Das gesamte Logische Paket (`_sendBuffer` Inhalt: Header + Encrypted Payload + HMAC) wird durchlaufen.
        * Trifft ein Byte mit dem Wert `START_BYTE`, `END_BYTE` oder `ESCAPE_BYTE` auf, wird ihm ein `ESCAPE_BYTE` vorangestellt und das ursprüngliche Byte mit einer `ESCAPE_XOR_MASK` (0x20) XOR-verknüpft. Beispiel: `0x02` wird zu `0x10 0x22`. Dies verhindert, dass Nutzdaten fälschlicherweise als Protokoll-Steuerbytes interpretiert werden.
    * **Rahmung und Senden:** Das gestuffte Paket wird zwischen ein `START_BYTE` und ein `END_BYTE` gesetzt und dann byteweise über `_serial.write()` gesendet. Der DE/RE-Pin wird auf Sende-Modus geschaltet (`digitalWrite(RS485_DE_RE_PIN, HIGH)`), nach dem Senden kurz gewartet (`delay(1)`) und dann auf Empfangs-Modus zurückgeschaltet (`digitalWrite(RS485_DE_RE_PIN, LOW)`).

### 3. Empfangen und Verarbeiten einer Nachricht (`processIncoming` -> `processReceivedPacket`)

1.  **`processIncoming()`:**
    * Diese Funktion wird kontinuierlich in der `loop()` aufgerufen.
    * Sie implementiert eine **State-Machine** (`_receiveState`) um den eingehenden Byte-Stream zu interpretieren:
        * `WAITING_FOR_START_BYTE`: Wartet auf das `START_BYTE`.
        * `RECEIVING_PAYLOAD`: Sammelt Bytes im `_receiveBuffer` bis ein `END_BYTE` oder ein `ESCAPE_BYTE` auftritt.
        * `ESCAPING_BYTE`: Wenn ein `ESCAPE_BYTE` erkannt wird, wird das *nächste* Byte mit der `ESCAPE_XOR_MASK` XOR-verknüpft, um den ursprünglichen Wert wiederherzustellen, und dann gepuffert. Das `ESCAPE_BYTE` selbst wird verworfen.
    * Wenn ein vollständiges Paket (zwischen `START_BYTE` und `END_BYTE`) empfangen wurde, wird `processReceivedPacket()` aufgerufen.

2.  **`processReceivedPacket()`:**
    * **Unstuffing:** Das empfangene, gestuffte Paket im `_receiveBuffer` wird entstufft (`unstuffBytes()`), um das ursprüngliche **Logische Paket** wiederherzustellen.
    * **Längenprüfung:** Überprüft, ob das entstuffte Paket die Mindestlänge (Header + HMAC-Tag) aufweist.
    * **HMAC-Verifikation:**
        * Der empfangene 32-Byte `HMAC-Tag` wird vom Ende des `_receiveBuffer` abgetrennt.
        * `_hmac.setKey(MASTER_KEY, HMAC_KEY_SIZE);` Der **Pre-Shared Master Authentication Key (MAK)** wird erneut geladen.
        * `_hmac.update(headerAndEncryptedPayload, len);` Ein **neuer HMAC** wird über den *gesamten empfangenen Header und die empfangene verschlüsselte Payload* berechnet.
        * `_hmac.finalize(calculatedHmac, HMAC_TAG_SIZE);` Der berechnete Tag wird erstellt.
        * **Vergleich:** `memcmp(calculatedHmac, receivedHmac, HMAC_TAG_SIZE)` wird verwendet, um den berechneten HMAC mit dem empfangenen zu vergleichen.
        * **Entscheidung:** **Stimmen die HMACs nicht überein, wird das Paket kommentarlos verworfen.** (`_hmacVerified = false;`) Dies schützt vor:
            * **Datenmanipulation:** Jede Änderung am Paketinhalt führt zu einem anderen HMAC.
            * **Spoofing:** Nur Knoten, die den korrekten MAK kennen, können gültige HMACs generieren.
    * **Zieladress-Filterung:** Wenn der `Target Address` im Header nicht der `_localAddr` des Knotens entspricht und nicht die Broadcast-Adresse (`0xFF`) ist, wird das Paket ignoriert.
    * **Key ID Prüfung:** Die im Paket übermittelte `Key ID` wird extrahiert. Wenn sie nicht der aktuell aktiven `_currentSessionKeyId` des Knotens entspricht, wird die Entschlüsselung nicht durchgeführt (dies ist ein Hinweis auf einen Schlüsselwechsel).
    * **AES-128-CBC-Entschlüsselung (`_aes.decrypt()`):**
        * Die `Encrypted Payload` und der `IV` werden aus dem Paket extrahiert.
        * Die Entschlüsselung erfolgt mit dem **aktuell aktiven Session Key** (der durch die `Key ID` im Paket identifiziert wird) und dem empfangenen IV.
    * **PKCS7-Unpadding:** Nach der Entschlüsselung werden die Padding-Bytes gemäß PKCS7 entfernt, um die Original-Nutzlast wiederherzustellen.
    * **Callback:** Bei erfolgreicher HMAC-Verifikation und Entschlüsselung wird die Original-Nutzlast (`String payload`), die `Source Address` und der `Message Type` über die registrierte `ReceiveCallback`-Funktion an die Anwendungslogik übergeben.
    * **ACK/NACK (optional):** Wenn `_ackEnabled` auf `true` gesetzt ist, sendet der Empfänger ein `MSG_TYPE_ACK` (`'A'`) bei Erfolg oder ein `MSG_TYPE_NACK` (`'N'`) bei HMAC-Fehlern oder Entschlüsselungsproblemen zurück an den Sender. Dies bietet ein grundlegendes Feedback auf Anwendungsebene.

### 4. Safety-Funktionen (Bus-Management)

Die `RS485SecureStack` Bibliothek bietet die **Bausteine** für robuste Safety-Funktionen. Die vollständige Implementierung der Bus-Management-Logik liegt in den Anwendungs-Sketches (insbesondere im Master/Scheduler).

* **Master-Heartbeat (`MSG_TYPE_MASTER_HEARTBEAT`):**
    * **Bibliotheks-Rolle:** Die Bibliothek kann diesen Nachrichtentyp senden und empfangen. Die `processIncoming()`-Methode ruft den Callback auch für diese Typen auf.
    * **Anwendungs-Rolle (Scheduler):** Der Scheduler-Sketch sollte periodisch Heartbeats senden.
    * **Anwendungs-Rolle (Clients/Submaster):** Diese Knoten überwachen den Heartbeat-Empfang. Bleibt der Heartbeat des Masters aus, kann der Knoten einen Timeout auslösen (`MASTER_HEARTBEAT_TIMEOUT_MS`) und in einen sicheren Zustand übergehen (z.B. keine Sendeanfragen mehr initiieren), um Chaos auf dem Bus zu vermeiden, falls der Master ausgefallen ist.
* **Multi-Master-Erkennung (`ROUGE_MASTER_DETECTED`):**
    * **Bibliotheks-Rolle:** Empfängt `MSG_TYPE_MASTER_HEARTBEAT`-Nachrichten von beliebigen Absendern.
    * **Anwendungs-Rolle (Scheduler):** Der primäre Scheduler-Sketch sollte auf Heartbeats von *anderen* Adressen (ungleich seiner eigenen) achten. Wird ein solcher "fremder" Heartbeat empfangen, bedeutet dies, dass ein anderer unautorisierter Master auf dem Bus aktiv ist. Der Scheduler muss dann in einen **Failsafe-Zustand** übergehen (z.B. alle Sendeaktivitäten einstellen), um Kollisionen, Bus-Stau und inkonsistente Zustände zu verhindern.
* **Dynamische Baudraten-Anpassung (`MSG_TYPE_BAUD_RATE_SET`):**
    * **Bibliotheks-Rolle:** Die Bibliothek bietet die Methode `setBaudRate(long newBaudRate)`, um die zugrunde liegende `HardwareSerial` neu zu initialisieren. Sie empfängt auch den Nachrichtentyp `'B'`.
    * **Anwendungs-Rolle (Scheduler):** Der Scheduler kann beim Start oder im Fehlerfall eine **Baudraten-Einmessung** durchführen, indem er verschiedene Baudraten testet und die optimale Rate an alle verbundenen Nodes sendet.
    * **Anwendungs-Rolle (Clients/Submaster/Monitor):** Empfangen sie eine `'B'`-Nachricht vom Master, rufen sie `setBaudRate()` auf, um ihre Baudrate anzupassen und somit synchron zum Master zu bleiben.

---

## 🚀 Erste Schritte

### Installation

1.  **Arduino IDE & ESP32 Board-Definitionen:** Stelle sicher, dass du eine aktuelle Arduino IDE und die ESP32-Board-Definitionen installiert hast.
2.  **Zusätzliche Bibliotheken:** Die `RS485SecureStack` benötigt folgende Bibliotheken, die über den Arduino Bibliotheksverwalter installiert werden müssen (oder manuell, falls bevorzugt):
    * `Crypto` (von Espressif)
    * `AES` (von Espressif, für AES-128)
    * `SHA256` (von Espressif, für HMAC-SHA256)
    * `HMAC` (von Espressif, für HMAC-Implementierung)
    * *(Optional: `ArduinoJson` für die Strukturierung komplexer JSON-Payloards, nicht direkt von dieser Bibliothek benötigt, aber oft nützlich für Industrie 4.0)*
3.  **`RS485SecureStack` Bibliothek hinzufügen:**
    * Lade das gesamte Repository als ZIP-Datei herunter.
    * In der Arduino IDE: `Skizze > Bibliothek einbinden > .ZIP-Bibliothek hinzufügen...` und wähle die heruntergeladene ZIP-Datei des Repositorys aus. Dies installiert die Bibliothek korrekt unter `Dokumente/Arduino/libraries/RS485SecureStack`.
4.  **`credentials.h` Konfiguration:**
    * **Erstelle eine neue Datei** mit dem Namen `credentials.h` im Hauptverzeichnis deines *Arduino-Sketches* (nicht im Bibliotheksordner!).
    * Füge den folgenden Code ein und **ändere den `MASTER_KEY` zu einem eigenen, zufälligen 32-Byte-Schlüssel!** Dieser Schlüssel ist das Herzstück deiner Sicherheit.
        ```cpp
        // credentials.h
        #ifndef CREDENTIALS_H
        #define CREDENTIALS_H

        // ACHTUNG: DIESEN SCHLÜSSEL UNBEDINGT ÄNDERN UND GEHEIM HALTEN!
        // Ein 32-Byte (256-Bit) Schlüssel für HMAC-SHA256.
        const byte MASTER_KEY[32] = {
            0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23, 0x45, 0x67, // Beispielschlüssel - BITTE ÄNDERN!
            0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98,
            0x76, 0x54, 0x32, 0x10, 0x12, 0x34, 0x56, 0x78,
            0x9A, 0xBC, 0xDE, 0xF0, 0xA1, 0xB2, 0xC3, 0xD4
        };

        #endif // CREDENTIALS_H
        ```
    * Stelle sicher, dass in deinen Sketches, die diesen Schlüssel benötigen, die Zeile `#include "credentials.h"` (ohne //) am Anfang steht.
5.  **Hardware-Anschluss:** Verbinde deine ESP32-Boards über geeignete RS485-Transceiver-Module (z.B. MAX485, SP3485) mit den Hardware-Serial-Ports des ESP32. Konfiguriere die RX/TX-Pins sowie den DE/RE-Steuerpin im Sketch (`setDeRePin()`).

### Beispiel-Nutzung (Kürzlich gezeigtes Beispiel-Sketch, zur Orientierung)

```cpp
// In deinem Haupt-Sketch-Datei (.ino)
#include <Arduino.h>
#include "RS485SecureStack.h" // Die Bibliothek inkludieren
#include "credentials.h"     // Deine selbst erstellte credentials.h

// ... Rest des Beispiel-Codes wie im vorherigen Abschnitt "Usage" beschrieben ...

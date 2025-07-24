# SECURITY.md - Sicherheitskonzepte und -implementierung des RS485SecureStack

Dieses Dokument beschreibt die implementierten Sicherheitsmerkmale des `RS485SecureStack` und gibt wichtige Hinweise zum sicheren Einsatz und zur weiteren Entwicklung.

## 💡 Zielsetzung der Sicherheit

Der `RS485SecureStack` wurde mit einer "Security-by-Design"-Philosophie entwickelt, um die kritischen Anforderungen an **Vertraulichkeit, Integrität und Authentizität** in RS485-Kommunikationssystemen zu erfüllen.

## 🛡️ Implementierte Sicherheitsmerkmale im Detail

Der Stack integriert mehrere kryptographische Primitive, um die genannten Sicherheitsziele zu erreichen. Die Implementierung nutzt, wo immer möglich, die dedizierten Hardware-Kryptographie-Beschleuniger der ESP32-MCUs für maximale Performance und Robustheit.

### 1. Authentifizierung (HMAC-SHA256)

* **Zweck:** Stellt sicher, dass eine empfangene Nachricht authentisch ist (d.h. sie wurde vom erwarteten Absender gesendet und wurde auf dem Transportweg nicht manipuliert). Dies schützt vor Spoofing und Tampering.
* **Algorithmus:** Verwendet **Hash-based Message Authentication Code (HMAC) mit SHA256** als zugrundeliegender Hash-Funktion.
* **Implementierung:** Die Bibliothek `HMAC<SHA256>` wird genutzt, um einen 32-Byte-HMAC-Tag zu generieren. Dieser Tag wird über den gesamten Header, den Initialization Vector (IV) und den verschlüsselten Payload des Pakets berechnet.
* **Schlüssel:** Für die HMAC-Berechnung wird der **`MASTER_KEY`** verwendet. Dieser Schlüssel muss auf *allen* Geräten (Master, Submaster, Clients, Monitor) identisch sein und ist in der Datei `credantials.h` hinterlegt. Er wird einmalig im Konstruktor des `RS485SecureStack` Objekts initialisiert (`HMAC<SHA256> hmac_engine;` und `hmac_engine.setKey(masterKey, HMAC_KEY_SIZE);`).
* **Verifizierung:** Jedes empfangene Paket wird nach dem Unstuffing und der Header-Validierung neu gehasht. Der berechnete HMAC wird dann mit dem im Paket enthaltenen HMAC-Tag verglichen. Stimmen sie überein, gilt das Paket als authentisch. Bei Abweichungen wird das Paket verworfen.

### 2. Verschlüsselung (AES128-CBC)

* **Zweck:** Schützt die Vertraulichkeit der Nutzdaten (Payload), sodass nur autorisierte Parteien, die im Besitz des korrekten Sitzungsschlüssels sind, den Inhalt lesen können. Dies verhindert Abhören.
* **Algorithmus:** Verwendet **Advanced Encryption Standard (AES) im Cipher Block Chaining (CBC) Modus mit einer 128-Bit-Schlüssellänge**.
* **Implementierung:** Die Bibliothek `AES128 aes128;` wird genutzt, um den Payload zu verschlüsseln und zu entschlüsseln. Der CBC-Modus erfordert einen **Initialization Vector (IV)**, der für jedes Paket einzigartig sein muss, um Muster in der Verschlüsselung zu vermeiden und Replay-Angriffe zu erschweren.
* **IV-Generierung:** Der 16-Byte IV wird für jedes gesendete Paket neu mittels des **Hardware True Random Number Generators (TRNG)** des ESP32 generiert (via `esp_random()`). Der IV wird unverschlüsselt als Teil des Pakets mitgesendet, da er für die Entschlüsselung benötigt wird und keine Vertraulichkeit erfordert, aber seine Einzigartigkeit kritisch ist.
* **Schlüssel:** Für die AES-Verschlüsselung werden **dynamische Sitzungsschlüssel** (`Session Keys`) verwendet, die vom Master verwaltet und rotiert werden können (siehe Schlüsselmanagement und Schlüsselrotation).

### 3. Zufallszahlen (Hardware TRNG)

* **Bedeutung:** Für kryptographische Operationen, insbesondere die Generierung von IVs, Nonces oder zukünftig für Schlüsselableitung, ist ein **kryptographisch sicherer Zufallszahlengenerator (True Random Number Generator, TRNG)** unerlässlich. Die Qualität der Zufallszahlen hat direkten Einfluss auf die Sicherheit der Verschlüsselung und Authentifizierung.
* **Implementierung:** Der ESP32 verfügt über einen integrierten Hardware-TRNG, der über Funktionen wie `esp_random()` im ESP-IDF oder im Arduino-Core für ESP32 zugänglich ist. Dieser wird vom `RS485SecureStack` verwendet, um qualitativ hochwertige, unvorhersehbare Zufallszahlen für die IV-Generierung zu produzieren.

### 4. Byte Stuffing & Framing

* **Zweck:** Gewährleistet die Integrität des Kommunikationsrahmens, indem verhindert wird, dass die speziellen Kontrollbytes (`START_BYTE`, `END_BYTE`, `ESCAPE_BYTE`) fälschlicherweise als solche interpretiert werden, wenn sie im verschlüsselten Payload vorkommen.
* **Implementierung:** Ein Escape-Mechanismus wird angewendet:
    * **Stuffing (beim Senden):** Alle Vorkommen von `START_BYTE`, `END_BYTE` oder `ESCAPE_BYTE` innerhalb des Payload-Datenstroms werden durch `ESCAPE_BYTE` gefolgt von einer modifizierten Version des Originalbytes ersetzt (z.B. `ESCAPE_BYTE XOR 0x20`).
    * **Unstuffing (beim Empfangen):** Der umgekehrte Prozess wird angewendet, um die ursprünglichen Bytes wiederherzustellen, bevor die Krypto-Validierung erfolgt.
* **Funktionen:** `stuffBytes(const byte* input, size_t inputLen, byte* output)` und `unstuffBytes(const byte* input, size_t inputLen, byte* output)` sind dafür verantwortlich.

## 5. Schlüsselmanagement und Schlüsselrotation (Für Entwickler)

Der `RS485SecureStack` implementiert ein System zur Verwaltung von Sitzungsschlüsseln, das vom Master gesteuert werden kann. Dies ermöglicht eine dynamische Aktualisierung der Schlüssel, ohne das gesamte System neu flashen zu müssen, und erhöht die Sicherheit, da kompromittierte Schlüssel ausgetauscht werden können.

### 5.1 Der Master-Schlüssel (`MASTER_KEY`)

* **Typ:** Dies ist ein **Pre-Shared Key (PSK)**, der auf **allen Geräten** im Netzwerk (Master, Submaster, Clients, Monitor) **identisch** sein muss.
* **Speicherort:** Er wird in der Datei `credantials.h` definiert. **Diese Datei sollte niemals im öffentlichen Repository veröffentlicht oder unverschlüsselt übertragen werden!**
* **Funktion:** Der `MASTER_KEY` wird primär für die **HMAC-Authentifizierung** von Paketen verwendet. Dies gewährleistet, dass alle Geräte nur authentische Nachrichten von autorisierten Teilnehmern akzeptieren. Er ist nicht direkt an der AES-Verschlüsselung der Daten beteiligt, sondern dient als die unveränderliche Basis für die Vertrauenskette im Netzwerk.
* **Sicherheit:** Da der `MASTER_KEY` statisch und auf allen Geräten vorhanden ist, ist seine Kompromittierung ein kritisches Ereignis, das die gesamte Sicherheit des Netzwerks untergraben würde. Er sollte daher niemals über unsichere Kanäle übertragen werden und muss auf den Geräten sicher abgelegt werden.

### 5.2 Sitzungsschlüssel (`Session Keys`)

* **Typ:** Dies sind die **dynamischen Schlüssel**, die für die **AES128-Verschlüsselung** des eigentlichen Payloads (Nutzdaten) verwendet werden.
* **Funktion:** Die Verwendung von Sitzungsschlüsseln, die regelmäßig rotiert werden können, erhöht die Forward Secrecy und begrenzt den Schaden, falls ein einzelner Sitzungsschlüssel kompromittiert wird. Selbst wenn ein Angreifer einen Sitzungsschlüssel erlangt, kann er damit nur Nachrichten entschlüsseln, die mit diesem spezifischen Schlüssel verschlüsselt wurden. Frühere oder spätere Kommunikation, die mit anderen Schlüsseln verschlüsselt wurde, bleibt sicher.
* **Verwaltung:** Sitzungsschlüssel werden vom Master generiert und verwaltet. Der Master ist dafür verantwortlich, neue Schlüssel sicher an die Clients zu verteilen.

### 5.3 Schlüssel-Pool (`_sessionKeyPool`)

* Die `RS485SecureStack`-Klasse hält intern ein kleines Array von Sitzungsschlüsseln (`_sessionKeyPool`). Dies ermöglicht es Geräten, mehrere Schlüssel gleichzeitig zu speichern und bei Bedarf zwischen ihnen zu wechseln.
* In der aktuellen Proof-of-Concept (PoC)-Implementierung sind **zwei Beispielschlüssel hartkodiert**, und die Umschaltung erfolgt über eine `keyId` (0 oder 1).
* **Für eine reale Anwendung:** Der `_sessionKeyPool` müsste robuster verwaltet werden. Dies könnte die Nutzung von sicheren Speichern (z.B. Flash-Speicher mit Verschlüsselung, TPM/Secure Element, oder dedizierten Key-Management-Systemen) umfassen. Schlüssel sollten nicht hartkodiert sein.

### 5.4 Schlüssel-Update durch den Master

* Der Master kann einen neuen Sitzungsschlüssel an alle oder bestimmte Geräte im Netzwerk senden.
* **`void setSessionKey(uint16_t keyId, const byte sessionKey[AES_KEY_SIZE])`:** Diese öffentliche Funktion in der `RS485SecureStack`-Klasse ermöglicht es, einen neuen Sitzungsschlüssel für eine bestimmte `keyId` in den internen Pool zu speichern. Diese Funktion wird primär vom Master aufgerufen, um einen neuen Schlüssel für sich selbst zu generieren/festzulegen, oder von Clients/Submastern, wenn sie einen neuen Schlüssel vom Master erhalten.
* **Nachrichtentyp `MSG_TYPE_KEY_UPDATE_TOKEN` ('K'):** Der Master verwendet diesen spezifischen Nachrichtentyp, um ein Update eines Sitzungsschlüssels an die Clients und Submaster zu signalisieren. Der Payload dieser Nachricht würde den `keyId` und den verschlüsselten neuen Sitzungsschlüssel enthalten.
    * **Kritischer Punkt:** Das Senden eines *neuen* Sitzungsschlüssels muss selbst über einen **sicheren Kanal** erfolgen, idealerweise verschlüsselt mit dem bereits bekannten, *aktuell gültigen* Sitzungsschlüssel. Die PoC-Implementierung des Schlüssel-Updates ist vereinfacht und müsste für den Produktionseinsatz gehärtet werden, um sicherzustellen, dass die Übertragung des neuen Schlüssels nicht abgefangen oder manipuliert werden kann. Ein komplexerer Schlüsselableitungs- oder Schlüsselaustausch-Algorithmus (z.B. Diffie-Hellman) könnte hier erforderlich sein, wobei der `MASTER_KEY` die Authentizität dieses Austauschs sichert.

### 5.5 Schlüssel-Rotation/Wechsel

* **`void setCurrentKeyId(uint16_t keyId)`:** Diese öffentliche Funktion ermöglicht es einem Gerät, zum Sitzungsschlüssel zu wechseln, der der angegebenen `keyId` im `_sessionKeyPool` zugeordnet ist. Wenn der `keyId` nicht bekannt ist (z.B. der Master hat ihn noch nicht gepusht), gibt die aktuelle Implementierung eine Warnung aus oder fällt auf einen Standard-Schlüssel zurück.
* Der Master kann eine Anweisung an die Clients/Submaster senden, zu einem bestimmten `keyId` zu wechseln. Dies kann Teil eines regelmäßigen Rotationsschemas sein oder ausgelöst werden, wenn ein Schlüssel als kompromittiert gilt.

### 5.6 Implikationen für Clients/Submaster

* Clients und Submaster müssen auf Nachrichten vom Master mit dem Typ `MSG_TYPE_KEY_UPDATE_TOKEN` achten.
* Beim Empfang einer solchen Nachricht extrahieren sie den neuen `keyId` und den neuen Sitzungsschlüssel aus dem Payload (nach der Entschlüsselung mit dem aktuell gültigen Schlüssel).
* Sie rufen dann `rs485Stack.setSessionKey(newKeyId, newKey)` auf, um den neuen Schlüssel in ihren Pool zu speichern.
* Anschließend rufen sie `rs485Stack.setCurrentKeyId(newKeyId)` auf, um diesen neuen Schlüssel für alle zukünftigen Verschlüsselungs- und Entschlüsselungsvorgänge zu aktivieren.
* Die Clients müssen in der Lage sein, nahtlos zwischen alten und neuen Schlüsseln zu wechseln, um die Kommunikation aufrechtzuerhalten, während der Master das Netzwerk aktualisiert.

## ⚠️ Disclaimer und wichtige Sicherheitshinweise

**Wichtiger Hinweis:** Diese Version des `RS485SecureStack` ist **explizit für Proof-of-Concepts (PoCs)** und Evaluierungen in kontrollierten Umgebungen gedacht. Sie dient der Demonstration der Machbarkeit und der Sicherheitskonzepte.

**Diese Software ist NICHT für den Produktionseinsatz geeignet, ohne erhebliche Erweiterungen und Härtungen.** Für den Einsatz in einer Produktionsumgebung sind die folgenden Aspekte von entscheidender Bedeutung:

1.  **Sicheres Provisioning des Master-Schlüssels (`MASTER_KEY`):** Derzeit ist der `MASTER_KEY` im Quellcode (`credantials.h`) hinterlegt. Für den Produktionseinsatz muss ein sicheres Verfahren implementiert werden, um diesen Schlüssel in jedes Gerät zu integrieren, ohne dass er kompromittiert oder ausgelesen werden kann. Dies könnte die Nutzung von:
    * **ESP32-eigenen Secure-Boot- und Flash-Verschlüsselungsfunktionen:** Um den Schlüssel im verschlüsselten Flash abzulegen und das Booten nicht signierter Firmware zu verhindern.
    * **Dedizierten Hardware-Security-Modulen (HSM) oder Trusted Platform Modules (TPM):** Um Schlüssel sicher zu speichern und kryptographische Operationen auszulagern.
    * **Over-the-Air (OTA) Provisioning mit robustem Key-Exchange:** Ein sicherer initialer Schlüsselaustausch.
2.  **Härtung des Schlüssel-Update-Prozesses:** Der in der PoC gezeigte Schlüssel-Update-Mechanismus (`MSG_TYPE_KEY_UPDATE_TOKEN`) ist eine Vereinfachung. Eine produktionsreife Implementierung muss sicherstellen, dass die Verteilung neuer Sitzungsschlüssel selbst gegen Abhören, Replay-Angriffe und Manipulationen vollständig geschützt ist. Dies könnte komplexere Key-Exchange-Protokolle erfordern.
3.  **Fehlerbehandlung und Robustheit:** Umfassende Fehlerbehandlung und Edge-Case-Abdeckung sind für den Produktionseinsatz unerlässlich, um das System gegen Angriffe oder unerwartetes Verhalten zu schützen.
4.  **Hardware-Schutz:** Neben der Software-Sicherheit muss auch die physikalische Sicherheit der Geräte berücksichtigt werden, um Manipulationen oder den Ausbau von Chips zum Auslesen von Schlüsseln zu verhindern.
5.  **Regelmäßige Sicherheitsaudits:** Der Code sollte von Sicherheitsexperten überprüft werden, um potenzielle Schwachstellen zu identifizieren.

Die Autoren übernehmen keine Haftung für Schäden oder Verluste, die durch die Verwendung dieser Software entstehen. Die Nutzung erfolgt auf eigenes Risiko.

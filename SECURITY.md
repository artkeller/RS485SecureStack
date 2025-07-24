# SECURITY.md - Sicherheitskonzepte und -implementierung des RS485SecureStack

Dieses Dokument beschreibt die implementierten Sicherheitsmerkmale des `RS485SecureStack` und gibt wichtige Hinweise zum sicheren Einsatz und zur weiteren Entwicklung. Es ist essenziell für Entwickler, die den Stack verstehen, modifizieren oder in Produktionssystemen einsetzen möchten.

## 💡 Zielsetzung der Sicherheit

Der `RS485SecureStack` wurde mit einer "Security-by-Design"-Philosophie entwickelt, um die kritischen Anforderungen an **Vertraulichkeit (Confidentiality), Integrität (Integrity) und Authentizität (Authenticity)** in RS485-Kommunikationssystemen zu erfüllen.

* **Vertraulichkeit:** Sicherstellung, dass nur autorisierte Parteien den Inhalt einer Nachricht lesen können.
* **Integrität:** Gewährleistung, dass eine Nachricht auf dem Transportweg nicht manipuliert oder verändert wurde.
* **Authentizität:** Bestätigung der Identität des Absenders einer Nachricht, um Spoofing zu verhindern.

## 🛡️ Implementierte Sicherheitsmerkmale im Detail

Der Stack integriert mehrere kryptographische Primitive, um die genannten Sicherheitsziele zu erreichen. Die Implementierung nutzt, wo immer möglich, die dedizierten Hardware-Kryptographie-Beschleuniger der ESP32-MCUs für maximale Performance und Robustheit.

### 1. Authentifizierung (HMAC-SHA256)

* **Zweck:** Stellt sicher, dass eine empfangene Nachricht authentisch ist (d.h. sie wurde vom erwarteten Absender gesendet und wurde auf dem Transportweg nicht manipuliert). Dies schützt effektiv vor Spoofing-Angriffen (Nachrichten von Unbefugten) und Tampering (Veränderung von Nachrichten).
* **Algorithmus:** Verwendet **Hash-based Message Authentication Code (HMAC)** mit **SHA256** als zugrundeliegender Hash-Funktion. HMAC ist ein kryptographischer Algorithmus, der einen geheimen Schlüssel zusammen mit einer Hash-Funktion verwendet, um einen Nachrichtenauthentifizierungscode zu erzeugen.
* **Implementierung:** Die Bibliothek `HMAC<SHA256>` aus der `Crypto.h` Suite wird genutzt, um einen 32-Byte-HMAC-Tag zu generieren. Dieser Tag wird über den **gesamten Header, den Initialization Vector (IV) und den verschlüsselten Payload** des Pakets berechnet. Dies gewährleistet, dass jede Änderung an diesen Daten detektiert wird.
* **Schlüssel:** Für die HMAC-Berechnung wird der **`MASTER_KEY`** verwendet. Dieser Schlüssel muss auf *allen* Geräten (Master, Submaster, Clients, Monitor) im Netzwerk **identisch** sein. Er wird in der nicht-öffentlichen Datei `credantials.h` definiert. Der `MASTER_KEY` wird einmalig im Konstruktor des `RS485SecureStack` Objekts initialisiert (`HMAC<SHA256> hmac_engine;` und `hmac_engine.setKey(masterKey, HMAC_KEY_SIZE);`).
* **Verifizierung:** Jedes empfangene Paket wird nach dem Unstuffing und der Header-Validierung (aber vor der Entschlüsselung) neu gehasht. Der auf dem Empfänger berechnete HMAC wird dann mit dem im Paket enthaltenen HMAC-Tag verglichen. **Stimmen sie überein, gilt das Paket als authentisch.** Bei Abweichungen wird das Paket als manipuliert oder unauthentisch betrachtet und sofort verworfen, ohne den Payload zu entschlüsseln.

### 2. Verschlüsselung (AES128-CBC)

* **Zweck:** Schützt die Vertraulichkeit der Nutzdaten (Payload), sodass nur autorisierte Parteien, die im Besitz des korrekten Sitzungsschlüssels sind, den Inhalt lesen können. Dies verhindert Abhören.
* **Algorithmus:** Verwendet **Advanced Encryption Standard (AES)** im **Cipher Block Chaining (CBC) Modus** mit einer **128-Bit-Schlüssellänge**. AES-128 ist ein weit verbreiteter und als sicher geltender Blockchiffre-Algorithmus. CBC ist ein Betriebsmodus, der die Abhängigkeit jedes verschlüsselten Blocks vom vorherigen Block sicherstellt und so die Sicherheit erhöht.
* **Implementierung:** Die Bibliothek `AES128 aes128;` aus der `Crypto.h` Suite wird genutzt, um den Payload zu verschlüsseln und zu entschlüsseln. Der CBC-Modus erfordert einen **Initialization Vector (IV)**.
* **Initialization Vector (IV):** Ein 16-Byte-Vektor, der für jedes Paket **einzigartig** sein muss. Er wird zusammen mit dem Schlüssel zur Verschlüsselung des ersten Blocks verwendet und beeinflusst jeden nachfolgenden Block. Dies ist entscheidend, um Muster in der Verschlüsselung gleicher Klartextblöcke zu vermeiden (was bei ECB-Modus auftreten würde) und Replay-Angriffe zu erschweren. Der IV wird unverschlüsselt als Teil des Pakets mitgesendet, da er für die Entschlüsselung zwingend benötigt wird und keine Vertraulichkeit erfordert, aber seine Einzigartigkeit kritisch ist.
* **IV-Generierung:** Der IV wird für jedes gesendete Paket neu und **zufällig** mittels des **Hardware True Random Number Generators (TRNG)** des ESP32 generiert (via `esp_random()`). Eine hochqualitative, unvorhersehbare IV-Generierung ist ein fundamentaler Aspekt der kryptographischen Sicherheit.
* **Schlüssel:** Für die AES-Verschlüsselung werden **dynamische Sitzungsschlüssel** (`Session Keys`) verwendet, die vom Master verwaltet und rotiert werden können (siehe Schlüsselmanagement und Schlüsselrotation).

### 3. Zufallszahlen (Hardware TRNG)

* **Bedeutung:** Für kryptographische Operationen, insbesondere die Generierung von IVs, Nonces (Zahlen, die nur einmal verwendet werden) oder zukünftig für Schlüsselableitung, ist ein **kryptographisch sicherer Zufallszahlengenerator (True Random Number Generator, TRNG)** unerlässlich. Die Qualität der Zufallszahlen hat direkten Einfluss auf die Sicherheit der Verschlüsselung (Unvorhersehbarkeit der IVs) und die Robustheit des gesamten Sicherheitssystems. Ein PRNG (Pseudo Random Number Generator), der nicht von einer echten Entropiequelle gespeist wird, wäre eine kritische Schwachstelle.
* **Implementierung:** Der ESP32 verfügt über einen integrierten **Hardware-TRNG**, der über Funktionen wie `esp_random()` (im Arduino-Core für ESP32) oder `esp_fill_random()` (im ESP-IDF) zugänglich ist. Dieser Hardware-TRNG nutzt physikalische Zufallsquellen (z.B. thermisches Rauschen) und wird vom `RS485SecureStack` verwendet, um qualitativ hochwertige, unvorhersehbare Zufallszahlen für die IV-Generierung zu produzieren.

### 4. Byte Stuffing & Framing

* **Zweck:** Gewährleistet die Integrität des Kommunikationsrahmens, indem verhindert wird, dass die speziellen Kontrollbytes (`START_BYTE`, `END_BYTE`, `ESCAPE_BYTE`) fälschlicherweise als solche interpretiert werden, wenn sie im verschlüsselten Payload vorkommen. Dies ist ein Schutzmechanismus auf Protokollebene, der vor Framing-Fehlern schützt, die durch zufällig auftretende Kontrollbytes in den Nutzdaten entstehen könnten.
* **Implementierung:** Ein Escape-Mechanismus wird angewendet:
    * **Stuffing (beim Senden):** Alle Vorkommen von `START_BYTE` (0xAA), `END_BYTE` (0x55) oder `ESCAPE_BYTE` (0xBB) innerhalb des Payloads (einschließlich IV, verschlüsselter Daten und HMAC-Tag) werden durch das `ESCAPE_BYTE` gefolgt von einer modifizierten Version des Originalbytes ersetzt (z.B. `OriginalByte XOR 0x20`). Dies vergrößert das Paket geringfügig, ist aber notwendig für die Protokollintegrität.
    * **Unstuffing (beim Empfangen):** Der umgekehrte Prozess wird angewendet, um die ursprünglichen Bytes wiederherzustellen, bevor die Header-Validierung, HMAC-Verifizierung und Entschlüsselung erfolgen.
* **Funktionen:** `stuffBytes(const byte* input, size_t inputLen, byte* output)` und `unstuffBytes(const byte* input, size_t inputLen, byte* output)` sind dafür verantwortlich.

## 5. Schlüsselmanagement und Schlüsselrotation (Für Entwickler)

Der `RS485SecureStack` implementiert ein System zur Verwaltung von Sitzungsschlüsseln, das vom Master gesteuert werden kann. Dies ermöglicht eine dynamische Aktualisierung der Schlüssel, ohne das gesamte System neu flashen zu müssen, und erhöht die Sicherheit, da kompromittierte Schlüssel ausgetauscht werden können.

### 5.1 Der Master-Schlüssel (`MASTER_KEY`)

* **Typ:** Dies ist ein **Pre-Shared Key (PSK)**, der **statisch** und **identisch** auf *allen* Geräten im Netzwerk (Master, Submaster, Clients, Monitor) vorhanden sein muss.
* **Speicherort:** Er wird in der nicht-öffentlichen Datei `credantials.h` definiert. **Diese Datei sollte niemals im öffentlichen Repository veröffentlicht, in Klartext über unsichere Kanäle übertragen oder in einer Weise gespeichert werden, die ein einfaches Auslesen durch Unbefugte ermöglicht!**
* **Funktion:** Der `MASTER_KEY` wird primär und ausschließlich für die **HMAC-Authentifizierung** von Paketen verwendet. Dies ist seine wichtigste Funktion, da er die grundlegende Vertrauensbasis im Netzwerk bildet und sicherstellt, dass nur authentische Nachrichten von autorisierten Teilnehmern akzeptiert werden. Er ist **nicht direkt an der AES-Verschlüsselung der Nutzdaten** beteiligt, sondern dient als die unveränderliche Basis für die Vertrauenskette und Integritätsprüfung im gesamten Netzwerk.
* **Sicherheit:** Da der `MASTER_KEY` statisch und auf allen Geräten vorhanden ist, ist seine Kompromittierung ein **kritisches Ereignis**, das die gesamte Sicherheit des Netzwerks untergraben würde, da dann jeder beliebige Angreifer authentische Nachrichten fälschen könnte. Sein Schutz hat daher höchste Priorität.

### 5.2 Sitzungsschlüssel (`Session Keys`)

* **Typ:** Dies sind die **dynamischen Schlüssel**, die für die **AES128-Verschlüsselung** des eigentlichen Payloads (Nutzdaten) verwendet werden. Sie sind 16 Bytes (128 Bit) lang.
* **Funktion:** Die Verwendung von Sitzungsschlüsseln, die regelmäßig rotiert werden können, erhöht die **Forward Secrecy** und begrenzt den Schaden, falls ein einzelner Sitzungsschlüssel kompromittiert wird. Selbst wenn ein Angreifer einen Sitzungsschlüssel erlangt, kann er damit nur Nachrichten entschlüsseln, die mit diesem spezifischen Schlüssel verschlüsselt wurden. Frühere oder spätere Kommunikation, die mit anderen, bereits rotierten oder noch nicht verteilten Schlüsseln verschlüsselt wurde, bleibt sicher.
* **Verwaltung:** Sitzungsschlüssel werden **vom Master generiert und verwaltet**. Der Master ist dafür verantwortlich, neue Schlüssel sicher an die Clients zu verteilen.

### 5.3 Schlüssel-Pool (`_sessionKeyPool`)

* Die `RS485SecureStack`-Klasse hält intern ein kleines Array von Sitzungsschlüsseln (`_sessionKeyPool`), in der aktuellen Implementierung für zwei Schlüssel (Index 0 und 1). Dies ermöglicht es Geräten, mehrere Schlüssel gleichzeitig zu speichern und bei Bedarf zwischen ihnen zu wechseln (z.B. während einer Übergangsphase bei der Schlüsselrotation).
* In der aktuellen Proof-of-Concept (PoC)-Implementierung sind **zwei Beispielschlüssel hartkodiert** innerhalb der `RS485SecureStack.cpp` im `_sessionKeyPool` initialisiert. Die Umschaltung erfolgt über eine `keyId` (0 oder 1).
* **Für eine reale Anwendung:** Der `_sessionKeyPool` müsste robuster verwaltet werden. Dies könnte die Nutzung von **sicheren Speichern** (z.B. verschlüsselter Flash-Speicher auf dem ESP32, dedizierte Hardware-Sicherheitsmodule (HSM) oder Trusted Platform Modules (TPM) für die Schlüsselableitung und -speicherung) umfassen. Schlüssel sollten **niemals hartkodiert** sein in einer Produktionsfirmware. Der `MASTER_KEY` könnte zur Ableitung neuer Sitzungsschlüssel verwendet werden, anstatt sie direkt zu übertragen, um die Sicherheit weiter zu erhöhen.

### 5.4 Schlüssel-Update durch den Master

* Der Master kann einen neuen Sitzungsschlüssel an alle oder bestimmte Geräte im Netzwerk senden.
* **`void setSessionKey(uint16_t keyId, const byte sessionKey[AES_KEY_SIZE])`:** Diese öffentliche Funktion in der `RS485SecureStack`-Klasse ermöglicht es, einen neuen Sitzungsschlüssel für eine bestimmte `keyId` in den internen `_sessionKeyPool` zu speichern. Diese Funktion wird primär vom Master aufgerufen, um einen neuen Schlüssel für sich selbst zu generieren/festzulegen, oder von Clients/Submastern, wenn sie einen neuen Schlüssel vom Master erhalten haben.
* **Nachrichtentyp `MSG_TYPE_KEY_UPDATE_TOKEN` ('K'):** Der Master verwendet diesen spezifischen Nachrichtentyp, um ein Update eines Sitzungsschlüssels an die Clients und Submaster zu signalisieren. Der Payload dieser Nachricht würde den `keyId` des neuen Schlüssels und den neuen Sitzungsschlüssel selbst enthalten.
    * **Kritischer Punkt zur Sicherheit des Updates:** Das Senden eines *neuen* Sitzungsschlüssels muss selbst über einen **sicheren Kanal** erfolgen, idealerweise verschlüsselt mit dem **aktuell gültigen** Sitzungsschlüssel. Die PoC-Implementierung des Schlüssel-Updates ist vereinfacht und dient der Demonstration. Für den Produktionseinsatz müsste sie **erheblich gehärtet** werden, um sicherzustellen, dass die Übertragung des neuen Schlüssels nicht abgefangen oder manipuliert werden kann. Ein komplexerer Schlüsselableitungs- oder Schlüsselaustausch-Algorithmus (z.B. ein vereinfachtes Diffie-Hellman, das den `MASTER_KEY` zur Authentifizierung des Austauschs nutzt) wäre hierfür eine robustere Lösung als die direkte Übertragung des neuen Schlüssels.

### 5.5 Schlüssel-Rotation/Wechsel

* **`void setCurrentKeyId(uint16_t keyId)`:** Diese öffentliche Funktion erlaubt es einem Gerät, zum Sitzungsschlüssel zu wechseln, der der angegebenen `keyId` im `_sessionKeyPool` zugeordnet ist. Wenn der angegebene `keyId` nicht bekannt ist (z.B. der Master hat ihn noch nicht gepusht oder er wurde noch nicht gespeichert), gibt die aktuelle Implementierung eine Warnung aus oder fällt auf einen Standard-Schlüssel zurück (dieses Verhalten muss in der Produktion präziser definiert werden).
* Der Master kann eine explizite Anweisung an die Clients/Submaster senden, zu einem bestimmten `keyId` zu wechseln. Dies kann Teil eines regelmäßigen, zeitbasierten Rotationsschemas sein (z.B. wöchentlicher Schlüsselwechsel) oder ausgelöst werden, wenn ein Schlüssel als potenziell kompromittiert gilt.

### 5.6 Implikationen für Clients/Submaster bei Schlüsselrotation

* Clients und Submaster müssen kontinuierlich auf Nachrichten vom Master mit dem Typ `MSG_TYPE_KEY_UPDATE_TOKEN` achten.
* Beim Empfang einer solchen Nachricht extrahieren sie den neuen `keyId` und den neuen Sitzungsschlüssel aus dem Payload (nach der Entschlüsselung mit dem aktuell gültigen Schlüssel).
* Sie rufen dann `rs485Stack.setSessionKey(newKeyId, newKey)` auf, um den neuen Schlüssel in ihren internen `_sessionKeyPool` zu speichern.
* Anschließend rufen sie `rs485Stack.setCurrentKeyId(newKeyId)` auf, um diesen neuen Schlüssel für alle zukünftigen Verschlüsselungs- und Entschlüsselungsvorgänge zu aktivieren.
* Die Clients müssen in der Lage sein, **nahtlos und ohne Kommunikationsunterbrechung** zwischen alten und neuen Schlüsseln zu wechseln. Dies erfordert oft, dass sie für eine kurze Übergangszeit sowohl mit dem alten als auch mit dem neuen Schlüssel entschlüsseln können, während der Master das Netzwerk schrittweise auf den neuen Schlüssel umstellt.

## ⚠️ Disclaimer und wichtige Sicherheitshinweise

**Wichtiger Hinweis:** Diese Version des `RS485SecureStack` ist **explizit für Proof-of-Concepts (PoCs), Forschungs- und Evaluierungszwecke** in kontrollierten Umgebungen gedacht. Sie dient der Demonstration der Machbarkeit und der implementierten Sicherheitskonzepte.

**Diese Software ist NICHT für den Produktionseinsatz geeignet, ohne erhebliche Erweiterungen, Audits und Härtungen.** Für den Einsatz in einer kritischen Produktionsumgebung sind die folgenden Aspekte von entscheidender Bedeutung und müssen vor einem Einsatz umgesetzt werden:

1.  **Sicheres Provisioning des Master-Schlüssels (`MASTER_KEY`):**
    * Derzeit ist der `MASTER_KEY` im Quellcode (`credantials.h`) hinterlegt. Für den Produktionseinsatz muss ein **sicheres und robustes Verfahren** implementiert werden, um diesen Schlüssel in jedes Gerät zu integrieren, ohne dass er kompromittiert oder ausgelesen werden kann.
    * Mögliche Lösungen umfassen: Nutzung der **ESP32-eigenen Secure-Boot- und Flash-Verschlüsselungsfunktionen**, um den Schlüssel im verschlüsselten Flash abzulegen und das Booten nicht signierter Firmware zu verhindern. Alternativ könnten dedizierte **Hardware-Security-Module (HSM) oder Trusted Platform Modules (TPM)** eingesetzt werden, um Schlüssel sicher zu speichern und kryptographische Operationen auszulagern. Ein sicheres Initial-Provisioning (z.B. über JTAG-Schutz oder spezielle On-Chip-Mechanismen) ist unerlässlich.

2.  **Härtung des Schlüssel-Update-Prozesses:**
    * Der in der PoC gezeigte Schlüssel-Update-Mechanismus (`MSG_TYPE_KEY_UPDATE_TOKEN`) ist eine Vereinfachung. Eine produktionsreife Implementierung muss sicherstellen, dass die **Verteilung neuer Sitzungsschlüssel selbst gegen Abhören, Replay-Angriffe und Manipulationen vollständig geschützt** ist.
    * Dies könnte komplexere Key-Exchange-Protokolle (z.B. ein Authenticated Key Exchange auf Basis von Diffie-Hellman, gesichert durch den `MASTER_KEY` für die Authentifizierung der Teilnehmer) erfordern. Ziel ist es, dass der neue Schlüssel niemals in Klartext über den Bus gesendet wird und seine Herkunft zweifelsfrei verifiziert werden kann.

3.  **Umfassende Fehlerbehandlung und Robustheit:**
    * Umfassende Fehlerbehandlung für alle möglichen Szenarien (z.B. fehlerhafte Pakete, Kommunikationsausfälle, unerwartete Daten) und Edge-Case-Abdeckung sind für den Produktionseinsatz unerlässlich, um das System gegen Angriffe oder unerwartetes Verhalten abzusichern und Denial-of-Service-Angriffe zu verhindern.

4.  **Physikalische Sicherheit der Hardware:**
    * Neben der Software-Sicherheit muss auch die **physikalische Sicherheit** der Geräte berücksichtigt werden. Dazu gehört der Schutz vor Manipulationen oder dem Ausbau von Chips zum Auslesen von Schlüsseln oder Firmware-Inhalten.

5.  **Regelmäßige Sicherheitsaudits:**
    * Der Code sollte von **unabhängigen Sicherheitsexperten** (mit Fokus auf Embedded- und Kryptographie-S
  

# RS485SecureStack: Robuster & Sicherer RS485 Kommunikationsstack

(Aplhaversion - aktuell nur auf deutsch)

## 💡 Warum dieses Projekt?

In vielen industriellen, Automatisierungs- und kritischen Steuerungsumgebungen ist die RS485-Kommunikation aufgrund ihrer Robustheit über weite Strecken und in störungsreichen Umgebungen immer noch Standard. Herkömmliche RS485-Protokolle bieten jedoch oft keine oder nur unzureichende Mechanismen für **Datensicherheit, Integrität** und **zentrales Bus-Management**. Bestehende Standards sind häufig entweder zu komplex für kleinere bis mittlere Systemarchitekturen oder sie vernachlässigen kritische Aspekte der modernen IT-Sicherheit.

Wir sind uns bewusst, dass wir mit dieser Lösung **keinem etablierten Standard Konkurrenz machen** oder vorgeben, alles besser zu wissen. Stattdessen haben wir aus der Praxis heraus festgestellt, dass die verfügbaren Lösungen für unsere spezifischen Anforderungen im industriellen Kontext – nämlich die Anbindung einer **kleinen bis mittleren Anzahl von Clients** in Umgebungen mit potenziellen **Multi-Master-Anforderungen** – oft nicht optimal passen. Mit **RS485SecureStack** präsentieren wir eine **"Security-by-Design"-Lösung mit integrierter "Safety-DNA"**, die genau diese Lücke schließt.

Dieses Projekt kombiniert die physikalische Robustheit von RS485 mit modernen Sicherheitsmerkmalen wie **AES-128-Verschlüsselung** und **HMAC-Authentifizierung**. Darüber hinaus implementiert es einen intelligenten **Scheduler (Master)**, der die Bus-Kommunikation nicht nur orchestriert, sondern auch **automatisch die optimale Baudrate einmisst**, die **Bus-Qualität überwacht** und proaktiv auf das **Vorhandensein unerlaubter Master reagiert**, um die Systemintegrität und -verfügbarkeit zu gewährleisten.

Dieses Projekt bietet eine komplette, praxiserprobte und bescheidene Lösung für zuverlässige, sichere und intelligent verwaltete RS485-Netzwerke, insbesondere für ESP32-basierte Systeme.

-----

## ✨ Features auf einen Blick

  * **Sichere Kommunikation:**
      * **AES-128-Verschlüsselung (CBC-Modus):** Schutz der Vertraulichkeit von Nutzdaten.
      * **HMAC-SHA256 Authentifizierung:** Gewährleistung der Datenintegrität und Authentizität jeder Nachricht. Verhindert Manipulation und Spoofing.
      * **Dynamisches Rekeying:** Der Scheduler (Master) kann in regelmäßigen Abständen neue Session Keys an alle Teilnehmer verteilen, um die Langzeit-Sicherheit zu erhöhen.
  * **Intelligentes Bus-Management (durch den Scheduler/Master):**
      * **Automatisierte Baudraten-Einmessung:** Der Master testet und ermittelt die höchste stabile Baudrate für alle Bus-Teilnehmer und passt diese dynamisch an die Umgebungsbedingungen an.
      * **Fehlerüberwachung:** Kontinuierliche Überwachung der Kommunikations-Fehlerraten (HMAC-Fehler, fehlende ACKs) durch den Master.
      * **Master-Heartbeat:** Der Master sendet einen regelmäßigen "Herzschlag", um seine Präsenz zu signalisieren.
      * **Multi-Master-Erkennung & Kollisionsvermeidung:** Der Master erkennt das Auftreten eines unerlaubten/zweiten Masters auf dem Bus und geht in einen sicheren Fehlerzustand, um Schäden zu verhindern.
      * **Zentrale Zugriffskontrolle:** Der Master vergibt tokenbasierte Sendeerlaubnis an Submaster, um Kollisionen zu vermeiden und den Datenfluss zu steuern.
  * **Flexible Node-Typen:**
      * **Scheduler (Master):** Die zentrale Steuerungseinheit des Busses.
      * **Submaster:** Intelligente Knoten, die vom Master Sendeerlaubnis erhalten und Clients steuern können.
      * **Clients:** Passive Knoten, die auf Anfragen reagieren.
      * **Bus-Monitor:** Ein passiver Lauscher, der den gesamten Verkehr entschlüsseln und analysieren kann, um Debugging und Systemüberwachung zu erleichtern.
  * **Einfache Integration:** Arduino-Bibliothek für ESP32-Plattformen.
  * **Hohe Zuverlässigkeit:** Konzipiert für den Einsatz in rauen Umgebungen.

-----

## 🔒 Sicherheitsanalyse & Bewertung der gebotenen Security und Safety

RS485SecureStack wurde mit einem starken Fokus auf die Abwehr gängiger Bedrohungen in seriellen Bussystemen entwickelt. Die Implementierung berücksichtigt sowohl **Security** (Schutz vor böswilligen Angriffen) als auch **Safety** (Schutz vor unbeabsichtigten Fehlern und deren Folgen).

### Architekturentscheidungen zur Sicherheit

Für dieses Projekt wurde bewusst eine **Shared-Key-Lösung** für den Master Authentication Key (MAK) gewählt, anstatt komplexere, zertifikatsbasierte Ansätze oder Hardware Secure Elements (wie z.B. ATECC608A). Die Gründe dafür sind:

  * **Komplexitätsreduktion für den Anwendungsfall:** Für die Anbindung einer kleinen bis mittleren Anzahl von Clients im industriellen Kontext bieten Zertifikate oder Secure Elements einen Overhead, der oft nicht im Verhältnis zum Sicherheitsgewinn für diese spezifische Nische steht. Ihre Implementierung erfordert einen erheblichen Aufwand in Bezug auf Public Key Infrastructure (PKI), Zertifikatsmanagement, Secure Provisioning und die Integration spezialisierter Hardware.
  * **Proof-of-Concept & Machbarkeit:** Dieses Projekt dient als **Proof-of-Concept (PoC)**. Eine Shared-Key-Lösung ermöglicht es, die Kernkonzepte der sicheren und intelligenten Bus-Kommunikation schnell zu demonstrieren und zu validieren, ohne sich in der Komplexität eines vollständigen PKI-Systems zu verlieren.
  * **Ressourcenbeschränkung:** Mikrocontroller wie der ESP32-C3 haben zwar Kryptobeschleuniger, aber die Verwaltung einer komplexen PKI oder die Interaktion mit Secure Elements kann zusätzliche Hard- und Software-Ressourcen binden, die für andere Aufgaben benötigt werden oder die Kosten des Gesamtsystems erhöhen.

### Bedrohungsmodell und Abwehrmechanismen

Wir betrachten folgende potenzielle Angriffs- und Fehlerszenarien:

| Bedrohung / Fehlerszenario         | Beschreibung                                                              | Security/Safety | Abwehrmeermechanismus in RS485SecureStack                                                                                                                                                                     | Bewertung                                                                                                                                                                                                         |
| :--------------------------------- | :------------------------------------------------------------------------ | :-------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **1. Unautorisiertes Mithören (Eavesdropping)** | Ein Angreifer greift physisch auf den Bus zu und liest den Kommunikationsverkehr mit. | Security        | **AES-128 (CBC-Modus) Verschlüsselung** der Payloads. Selbst wenn Pakete abgefangen werden, sind die Nutzdaten unlesbar, solange der Angreifer den geheimen Master Key nicht besitzt.                       | **Hoch:** Effektiver Schutz der Vertraulichkeit. AES-128 ist ein starker Standard. CBC erfordert korrekte IV-Nutzung, was im Stack implementiert ist.                                                                  |
| **2. Nachrichten-Manipulation** | Ein Angreifer ändert Nachrichteninhalte während der Übertragung.          | Security        | **HMAC-SHA256 Authentifizierung** für jedes Paket. Jede Änderung am Paket (Header, Payload) führt zu einem ungültigen HMAC, wodurch das Paket vom Empfänger verworfen wird.                                    | **Hoch:** Gewährleistet Datenintegrität. SHA256 ist kryptographisch stark. Angreifer können keine gültigen manipulierten Nachrichten erzeugen ohne den geheimen Master Key.                                             |
| **3. Nachrichten-Einschleusung (Spoofing)** | Ein Angreifer injiziert eigene, gefälschte Nachrichten in den Bus, gibt sich als legitimer Teilnehmer aus oder sendet ungültige Befehle. | Security        | **HMAC-SHA256 Authentifizierung.** Nur Nachrichten, deren HMAC mit dem geheimen Master Key korrekt berechnet wurde, werden akzeptiert. Ein Angreifer ohne Kenntnis des Master Keys kann keine gültigen HMACs erzeugen. | **Hoch:** Verhindert effektiv das Einschleusen von Nachrichten. Dies schützt vor Replay-Angriffen nur bedingt (falls ältere HMACs mit altem Key noch gültig wären), daher das Rekeying.                                  |
| **4. Denial of Service (DoS) durch Bus-Spamming / Kollisionen** | Ein Angreifer oder ein fehlerhafter Node sendet kontinuierlich Nachrichten und blockiert den Bus für legitime Kommunikation. | Security/Safety | **Zentrale Zugriffskontrolle (Token-basiert durch Master):** Nur Nodes mit Sendeerlaubnis dürfen senden. \<br\>**Master-Heartbeat & Multi-Master-Erkennung:** Der legitime Master überwacht den Bus aktiv auf unautorisierte Master-Aktivität. Bei Erkennung wird der Master in einen sicheren Stopp-Zustand versetzt. | **Moderat bis Hoch:** Die Token-basierte Methode ist effektiv gegen unabsichtliches Spamming. Gegen gezielte, böswillige Überflutung durch einen Angreifer, der die Physik manipuliert, ist es schwieriger, aber die Erkennung schützt den Master. Die Sicherheit hängt davon ab, wie schnell und entschieden der Master reagiert. |
| **5. Ausfall des Master** | Der Master-Knoten fällt aus und das System verliert seine zentrale Steuerung. | Safety          | **Master-Heartbeat-Überwachung durch alle Nodes:** Nodes erkennen das Fehlen des Master-Heartbeats und gehen in einen definierten, passiven Zustand (stellen eigene aktive Kommunikation ein). | **Hoch:** Verhindert unkontrolliertes Verhalten der Nodes nach Master-Ausfall. Eine automatische Master-Wahl (Leader Election) ist nicht implementiert, dies ist ein bewusster Design-Entscheid für Einfachheit und Determinismus. |
| **6. Fehlkonfiguration der Baudrate** | Ein Node ist mit einer falschen Baudrate konfiguriert oder die optimale Baudrate ändert sich durch Umweltfaktoren. | Safety          | **Automatisierte Baudraten-Einmessung durch den Master:** Der Master testet dynamisch die beste Baudrate und teilt sie den Nodes mit. \<br\>**Baudraten-Anpassung durch Nodes:** Nodes können ihre Baudrate auf Anweisung des Masters ändern. | **Hoch:** Erhöht die Systemrobustheit und -verfügbarkeit in variablen Umgebungen erheblich.                                                                                                                                 |
| **7. Veraltete / Kompromittierte Session Keys** | Ein Session Key ist über einen längeren Zeitraum aktiv und könnte durch Analyse des verschlüsselten Verkehrs entschlüsselt worden sein. | Security        | **Dynamisches Rekeying:** Der Master sendet in regelmäßigen Abständen neue Session Keys an alle Nodes. | **Hoch:** Begrenzt die Lebensdauer eines Schlüssels und damit das Zeitfenster für Angriffe. Dies ist ein wichtiger Schritt über statische Schlüssel hinaus.                                                                  |
| **8. Firmware-Manipulation / Physischer Zugriff & Master Key Schutz** | Ein Angreifer erhält physischen Zugriff auf einen Node und kann dessen Firmware ändern oder den Master Key auslesen. | Security        | **Nicht direkt vom Stack abgedeckt.** Da es sich um eine Shared-Key-Lösung handelt, ist der `MASTER_KEY` derzeit im Quellcode hinterlegt. Dies erfordert physische Sicherheitsmaßnahmen (z.B. Gehäuse, Manipulationserkennung), Secure Boot / Encrypted Flash auf dem ESP32 und sicheres Key-Provisioning. | **Gering (in aktueller PoC-Version):** **Diese Version ist NICHT für den Produktionseinsatz geeignet.** Der Schutz des Master Authentication Key (MAK) ist in dieser Phase nicht vollumfänglich implementiert. **Sobald ein sicheres Provisioning-Verfahren für den MAK vorliegt (z.B. über eFuse, NVS mit Verschlüsselung, oder einen externen Secure Element Chip), können Feldtests und ein Produktions-Rollout in Betracht gezogen werden.** |

### Bewertung der gebotenen Sicherheit

RS485SecureStack bietet eine **robuste Basis für sichere und fehlertolerante RS485-Kommunikation**. Durch die Kombination von Verschlüsselung, Authentifizierung und intelligentem Bus-Management werden die häufigsten Angriffs- und Fehlerszenarien auf dieser Protokollebene effektiv abgedeckt.

  * **Security (Vertraulichkeit, Integrität, Authentizität):** Die Verwendung von AES-128 und HMAC-SHA256 mit einem Pre-Shared Key, unterstützt durch dynamisches Rekeying, bietet ein **hohes Sicherheitsniveau** gegen Mithören, Manipulation und Fälschung von Nachrichten, **solange der Master Key geheim bleibt und geschützt ist**.
  * **Safety (Zuverlässigkeit, Verfügbarkeit, Schadensvermeidung):** Die Mechanismen zur Baudraten-Anpassung, Fehlerüberwachung und insbesondere die **Multi-Master-Erkennung und der sichere Stopp-Zustand** des Masters sind entscheidende Beiträge zur Betriebssicherheit. Sie minimieren das Risiko von Bus-Kollisionen und unkontrollierten Systemzuständen.

**Wichtiger Hinweis:** Diese Version des RS485SecureStack ist **explizit für Proof-of-Concepts (PoCs)** und Evaluierungen in kontrollierten Umgebungen gedacht. Für den Produktionseinsatz ist das **sichere Provisioning und der Schutz des Master Authentication Key (MAK)** von entscheidender Bedeutung. Sobald entsprechende Verfahren dafür implementiert sind (z.B. durch Nutzung der ESP32-eigenen Secure-Boot- und Flash-Verschlüsselungsfunktionen oder dedizierte Hardware-Security-Module), kann das System für Feldtests und einen späteren Produktions-Rollout in Betracht gezogen werden.

-----

## ⚙️ Architektur & Komponenten

Das Projekt besteht aus einer Kernbibliothek (`RS485SecureStack`) und verschiedenen Arduino-Sketches, die die verschiedenen Node-Typen repräsentieren.

### RS485SecureStack Bibliothek

Dies ist die C++-Bibliothek im `src/` Ordner, die die grundlegenden Funktionen für die gesicherte RS485-Kommunikation bereitstellt:

  * Initialisierung der Hardware-Serial für RS485.
  * Implementierung des Sende- und Empfangsmechanismus mit Preamble, Header, Payload und Trailer.
  * AES-128-Ver-/Entschlüsselung der Payloads.
  * HMAC-Generierung und -Verifikation für jedes Paket.
  * ACK/NACK-Handshake-Mechanismus.
  * Interne Verwaltung der Baudrate und Session Keys.

### Node-Typen (Arduino Sketches)

Im `poc/` Ordner findest du die Implementierungen der verschiedenen Nodes:

#### 1\. Scheduler (Master)

`scheduler_main_esp32.ino`

  * **Rolle:** Der zentrale Dirigent des RS485-Busses.
  * **Aufgaben:**
      * Verwaltet das gesamte Netzwerk.
      * Vergibt "Sendeerlaubnis" (Token) an Submaster.
      * Führt dynamisches Rekeying durch (verteilt neue Session Keys).
      * Implementiert die automatische Baudraten-Einmessung und optimiert die Bus-Baudrate für alle Teilnehmer.
      * Überwacht die Fehlerraten der Kommunikation.
      * Sendet seinen Master-Heartbeat.
      * **Kritische Safety:** Erkennt andere Master auf dem Bus und stoppt alle Operationen, um Bus-Kollisionen und inkonsistente Zustände zu verhindern.

#### 2\. Submaster

`submaster_main_esp32.ino`

  * **Rolle:** Intelligenter Knoten, der unter der Kontrolle des Schedulers steht. Kann selbst Anfragen an Clients senden.
  * **Aufgaben:**
      * Empfängt Sendeerlaubnis vom Scheduler.
      * Kommuniziert mit Clients in seinem Segment.
      * Reagiert auf Baudraten-Set-Anweisungen des Schedulers.
      * Überwacht den Master-Heartbeat und geht in einen passiven Zustand, wenn der Master ausfällt.
      * Aktualisiert seine Session Keys nach Anweisung des Schedulers.

#### 3\. Client

`client_main_esp32.ino`

  * **Rolle:** Passiver Endknoten, der auf Anfragen von Submastern oder dem Scheduler reagiert.
  * **Aufgaben:**
      * Antwortet auf spezifische Anfragen mit Daten oder Aktionen.
      * Reagiert auf Baudraten-Set-Anweisungen des Schedulers.
      * Überwacht den Master-Heartbeat und geht in einen passiven Zustand, wenn der Master ausfällt.
      * Aktualisiert seine Session Keys nach Anweisung des Schedulers.

#### 4\. Bus-Monitor

`bus_monitor_esp32.ino`

  * **Rolle:** Ein passiver Beobachter des Bus-Verkehrs.
  * **Aufgaben:**
      * **Muss den MASTER\_KEY kennen**, um alle Payloads entschlüsseln zu können.
      * Lauscht dem gesamten Bus-Verkehr.
      * Versteht alle Protokollnachrichten (Heartbeat, Baudrate-Set, Key-Update).
      * Zeigt Bus-Status (Master-Präsenz, Baudrate, Key ID, Fehlerzähler) im "Dashboard-Modus" auf einem TFT-Display und seriell an.
      * Analysiert Traffic-Statistiken (Pakete/Sekunde, Bytes/Sekunde) im "Traffic-Modus" auf TFT und seriell.
      * Bietet einen detaillierten "Debug-Modus", der alle empfangenen Pakete mit entschlüsseltem Payload und Metadaten seriell ausgibt.
      * Passt seine Baudrate automatisch an die des Busses an.

-----

## 🧪 Testkonfiguration und Anwendungsfall (Reale Welt)

Um die Funktionalität, Sicherheit und Robustheit des RS485SecureStack zu demonstrieren und zu testen, verwenden wir die folgende reale Hardware-Konfiguration. Dieses Szenario simuliert eine typische Automatisierungsumgebung mit einer zentralen Steuerung, verteilten Sub-Systemen und Endgeräten, ergänzt durch ein Überwachungsmodul.

### Systemübersicht (Graphic)

```
+----------------+                   +----------------+
|    Scheduler   | Master (Address 0)|                |
|  ESP32-C3 Dev. |<----------------->|   RS485 Bus    |
|     (UART0)    |                   |  (Twisted Pair)|
+----------------+                   +----------------+
        |                                       |
        |                                       |
        V                                       V
+----------------+                       +----------------+
|    Submaster 1 | (Address 1)           |    Submaster 2 | (Address 2)
|  ESP32-C3 Dev. |<--------------------->|  ESP32-C3 Dev. |
|     (UART0)    |                       |     (UART0)    |
+----------------+                       +----------------+
        |                                       |
        |                                       |
        V                                       V
+----------------+                       +----------------+
|    Client 11   | (Address 11)          |    Client 12   | (Address 12)
|  ESP32-C3 Dev. |<--------------------->|  ESP32-C3 Dev. |
|     (UART0)    |                       |     (UART0)    |
+----------------+                       +----------------+
                                                |
                                                V
                                       +----------------+
                                       |   Bus-Monitor  | (Address 254)
                                       | LilyGo T-Disp. S3|
                                       |  (TFT + UART0) |
                                       +----------------+
```

*(Hinweis: Die Pfeile auf dem RS485 Bus symbolisieren bidirektionale Kommunikation. Jeder Node ist über einen RS485 Transceiver mit dem Bus verbunden.)*

### Bill of Materials (BOM)

| Menge | Bauteil                 | Beschreibung                                                                                              | Bezugsquelle/Typische Bezeichnung                               |
| :---- | :---------------------- | :-------------------------------------------------------------------------------------------------------- | :-------------------------------------------------------------- |
| **5** | **ESP32-C3 Development Board** | Mikrocontroller für Master, Submaster und Clients. Geringer Stromverbrauch, Hardware-Krypto.               | ESP32-C3-DevKitM-1, NodeMCU-32C3, ESP32-C3 Supermini           |
| **1** | **LilyGo T-Display S3** | ESP32-S3 Entwicklungsboard mit integriertem ST7789 170x320 TFT-Display für den Bus-Monitor.              | LilyGo T-Display-S3 (mit 170x320 ST7789 TFT)                  |
| **6** | **RS485 Transceiver Modul (HW-159)** | Modul mit MAX485-Chip zur Umwandlung von TTL-Signalen in RS485-Signale und umgekehrt. Kompatibel mit 3.3V Logik. | HW-159 MAX485 Modul (oft als "MAX485 TTL zu RS485 Konverter Modul" gelistet) |
| **\~10m**| **Twisted-Pair Kabel** | Für die RS485-Busleitung (A und B Leitungen). Abgeschirmtes Kabel (z.B. CAT5/6) empfohlen für geringere Störungen. | Cat5e/Cat6 Netzwerkkabel                                     |
| **6** | **Micro-USB Kabel** | Zur Stromversorgung und Programmierung der ESP32 Boards.                                                 | Standard Micro-USB Kabel                                      |
| **6** | **Breadboard / Steckplatine** | Zum einfachen Aufbau der Schaltung (ESP32 + RS485 Modul).                                                | Beliebiges Standard-Breadboard                                |
| **\~50** | **Jumper Wires (m-f, m-m)** | Für die Verbindungen auf dem Breadboard und zwischen Modulen.                                             | Verschiedene Längen und Typen (Male-Female, Male-Male)      |
| **2** | **120 Ohm Abschlusswiderstand** | Optional, aber empfohlen für lange Busleitungen oder hohe Baudraten zur Impedanzanpassung. An beiden Enden des Busses anbringen. | 1/4W Widerstand                                               |

### Verdrahtungshinweise (Allgemein)

Für jeden ESP32 (C3 und S3):

  * **ESP32 TX** an **MAX485 DI**
  * **ESP32 RX** an **MAX485 RO**
  * **ESP32 GPIO (DE/RE)** an **MAX485 DE & RE** (oft gebrückt) - dieser Pin muss im Sketch entsprechend gesetzt werden (HIGH für Senden, LOW für Empfangen). In unserem `RS485SecureStack` wurde das noch nicht direkt im Stack implementiert. Für Tests kann man ihn fest auf LOW lassen und nur lauschen oder manuell für den Master steuern. **Für eine funktionierende Sendefähigkeit des Master/Submaster/Clients muss dieser Pin jedoch vom Sketch gesteuert werden\!**
  * **MAX485 VCC** an **ESP32 3.3V**
  * **MAX485 GND** an **ESP32 GND**
  * **MAX485 A** an **RS485 Bus A**
  * **MAX485 B** an **RS485 Bus B**

Alle "A"-Pins der MAX485 Module werden miteinander verbunden, ebenso alle "B"-Pins.
Der RS485-Bus sollte als eine durchgehende Linie (Daisy-Chain) und nicht als Stern-Topologie verdrahtet werden.

### Test-Use Cases

1.  **Systemstart & Baudraten-Einmessung:**

      * Alle Nodes starten auf einer Standard-Baudrate (z.B. 9600 bps).
      * Der Scheduler führt die automatische Einmessung durch, testet verschiedene Baudraten und setzt die optimale Rate für alle Nodes.
      * **Monitor:** Beobachtet den gesamten Einmessprozess, zeigt die Baudratenwechsel und die resultierende optimale Rate an.

2.  **Regelmäßiger Betrieb:**

      * Der Scheduler sendet kontinuierlich seinen Heartbeat.
      * Der Scheduler vergibt Sendeerlaubnis an Submaster, die daraufhin mit Clients kommunizieren.
      * Clients antworten auf Anfragen.
      * **Monitor (Dashboard/Traffic-Modus):** Zeigt die Master-Präsenz, aktuelle Baudrate, Key ID, Pakete/Sekunde, Bytes/Sekunde und Fehlerraten an.

3.  **Rekeying-Prozess:**

      * Nach einer definierten Zeit initiiert der Scheduler ein Rekeying und verteilt eine neue Key ID und den entsprechenden Session Key an alle Nodes.
      * **Monitor:** Zeigt den Wechsel der Key ID an und verifiziert, dass die Kommunikation mit dem neuen Schlüssel erfolgreich entschlüsselt wird.

4.  **Fehlerfall: Baudrate verschlechtert sich (Simulation):**

      * Während des Betriebs wird die Qualität der Busleitung absichtlich verschlechtert (z.B. durch Entfernen des Abschlusswiderstands, oder durch starke externe Störquelle).
      * Der Master registriert erhöhte Fehlerraten (HMAC-Fehler, fehlende ACKs).
      * Idealerweise initiiert der Master eine erneute Baudraten-Einmessung und reduziert die Baudrate, um die Kommunikation wiederherzustellen.
      * **Monitor:** Zeigt die erhöhten Fehlerraten und den Baudraten-Abstieg an.

5.  **Safety-Test: Anderer Master betritt den Bus:**

      * Ein zweiter ESP32-C3, ebenfalls als "Scheduler" geflasht (mit anderer Adresse als 0, aber dem gleichen Master Key), wird an den Bus angeschlossen.
      * Dieser "Rogue Master" beginnt ebenfalls, Heartbeats zu senden.
      * Der legitime Scheduler (Adresse 0) sollte den Heartbeat des Rogue Masters erkennen.
      * **Legitimer Scheduler:** Geht in den "DANGER MODE" und stoppt alle aktiven Sendeoperationen. Serielle Ausgabe und/oder LED-Anzeige signalisiert den Fehler.
      * **Submaster/Clients:** Überwachen weiterhin den Heartbeat des *offiziellen* Schedulers (Adresse 0). Falls dieser aufhört, aktiv zu sein, gehen sie in ihren passiven Zustand. Falls der Rogue Master ebenfalls auf Adresse 0 eingestellt wäre (was in der echten Welt zu Adresskonflikten führt), würden die Nodes den Heartbeat empfangen, aber der legitime Master würde immer noch den Konflikt bemerken.
      * **Monitor (Dashboard/Debug-Modus):** Zeigt deutlich die Erkennung des unerwarteten Master-Heartbeats und dessen Absenderadresse an.

6.  **Sicherheits-Test: Angreifer versucht zu Spoofen/Manipulieren:**

      * Ein externer ESP32 mit einer eigenen, *falschen* Implementierung versucht, Pakete in den Bus einzuschleusen, die nicht mit dem korrekten HMAC signiert sind.
      * **Alle Nodes:** Empfangen die Pakete, aber die HMAC-Prüfung schlägt fehl, und die Pakete werden verworfen.
      * **Monitor (Debug-Modus):** Zeigt die empfangenen Pakete an, aber mit dem Hinweis "HMAC\_OK: NO", was die erfolgreiche Abwehr der Manipulation demonstriert.

Dieses umfassende Setup ermöglicht eine realitätsnahe Validierung der robusten und sicheren Kommunikationsfähigkeiten des RS485SecureStack-Projekts.

-----

Ja, das ist eine ausgezeichnete Idee! Standard-GitHub-Sektionen wie Installation, Usage, Lizenz und Disclaimer sind essentiell für ein vollständiges und professionelles Repository. Sie erleichtern anderen die Nutzung, das Verständnis und die rechtliche Einordnung des Projekts.

Ich werde diese Abschnitte in die `README.md` einbauen und dabei sicherstellen, dass sie die bereits besprochenen Punkte (PoC-Status, Shared-Key-Lösung, keine Produktionsfreigabe, deutsche Sprache) widerspiegeln und die MIT-Lizenz korrekt angeben.

Hier sind die zusätzlichen Sektionen, die in die `README.md` integriert werden würden:

---

## 🚀 Erste Schritte

### Installation

1.  **Voraussetzungen:**
    * **Arduino IDE:** Lade die Arduino IDE von der offiziellen Website herunter und installiere sie.
    * **ESP32 Board-Definitionen:** Füge die ESP32 Board-Definitionen zur Arduino IDE hinzu. Gehe zu `Datei > Voreinstellungen`, füge `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` in "Zusätzliche Boardverwalter-URLs" ein. Gehe dann zu `Werkzeuge > Board > Boardverwalter...` und suche nach "esp32" und installiere die "esp32 by Espressif Systems" Boards.
    * **Bibliotheken:**
        * **`RS485SecureStack`:** Lade dieses Repository als ZIP herunter (`Code > Download ZIP`) und füge es über `Skizze > Bibliothek einbinden > .ZIP-Bibliothek hinzufügen...` in die Arduino IDE ein.
        * **`Adafruit GFX Library`:** Installiere diese über den Arduino Bibliotheksverwalter (`Skizze > Bibliothek einbinden > Bibliotheken verwalten...`). Suche nach "Adafruit GFX" und installiere sie.
        * **`Adafruit ST7789 Library`:** Installiere diese ebenfalls über den Arduino Bibliotheksverwalter. Suche nach "Adafruit ST7789" und installiere sie.
        * **`ArduinoJson`:** Für komplexere Payload-Verarbeitung könnte diese nützlich sein, ist aber nicht direkt im Kern-Stack enthalten. Bei Bedarf installieren.
2.  **Hardware-Anschluss:**
    * Verbinde deine ESP32-Boards (ESP32-C3 und LilyGo T-Display S3) über die RS485 Transceiver (HW-159) wie im Abschnitt "Verdrahtungshinweise" beschrieben.
    * Stelle sicher, dass die RS485-Leitung korrekt terminiert ist (120 Ohm Widerstände an den Enden des Busses, falls verwendet).
3.  **Anpassung der Sketches:**
    * Öffne die gewünschten Node-Sketches (z.B. `poc/scheduler_main_esp32.ino`, `poc/bus_monitor_esp32.ino`) in der Arduino IDE.
    * **RS485-Pins:** Passe die GPIO-Pins für RX/TX und den DE/RE-Pin des RS485-Transceivers (falls verwendet) in den jeweiligen Sketches an deine Hardware an. Die Standard-UART0 wird oft für den Serial Monitor verwendet; für dedizierte RS485-Kommunikation ist es ratsam, eine andere HardwareSerial (z.B. `Serial1` oder `Serial2`) und entsprechende GPIOs zu verwenden.
    * **`MASTER_KEY`:** Stelle sicher, dass der `MASTER_KEY` in allen Sketches, die am selben Bus kommunizieren sollen, **identisch** ist.
    * **TFT-Pins (für Bus-Monitor):** Überprüfe und passe die TFT-Pins im `bus_monitor_esp32.ino` Sketch an die spezifische Pinbelegung deines LilyGo T-Display S3 an.

### Usage

1.  **Flashen der Firmware:**
    * Wähle das korrekte Board und den COM-Port in der Arduino IDE aus (`Werkzeuge > Board`, `Werkzeuge > Port`).
    * Lade die entsprechenden Sketches auf jedes deiner ESP32-Boards.
        * Ein Board als **Scheduler**.
        * Zwei Boards als **Submaster**.
        * Zwei Boards als **Client**.
        * Ein LilyGo T-Display S3 als **Bus-Monitor**.
2.  **Inbetriebnahme:**
    * Starte zuerst den **Scheduler (Master)**. Er wird beginnen, den Bus zu initialisieren und die Baudrate einzumessen.
    * Schalte dann die **Submaster** und **Clients** ein. Sie sollten die Baudraten-Anweisungen des Masters empfangen und sich anpassen.
    * Schalte den **Bus-Monitor** ein. Er sollte automatisch die Baudrate des Busses erkennen und beginnen, den Verkehr anzuzeigen.
3.  **Interaktion mit dem Bus-Monitor:**
    * Der Bus-Monitor (LilyGo T-Display S3) zeigt standardmäßig das Dashboard an.
    * Über den seriellen Monitor des Bus-Monitors kannst du den Modus wechseln:
        * `s`: Wechselt zum Simple Dashboard-Modus (TFT).
        * `t`: Wechselt zum Traffic Analysis-Modus (TFT).
        * `d`: Wechselt zum Debug Trace-Modus (nur serieller Monitor).
    * Beobachte die Ausgaben auf dem TFT-Display und/oder im seriellen Monitor, um den Bus-Verkehr, Fehler und den Status der Nodes zu überwachen.

### Lizenz

Dieses Projekt ist unter der **MIT-Lizenz** lizenziert. Details finden Sie in der `LICENSE`-Datei im Root-Verzeichnis dieses Repositories.

### Disclaimer

**Wichtiger Hinweis:** Diese Version des RS485SecureStack ist **explizit für Proof-of-Concepts (PoCs)** und Evaluierungen in kontrollierten Umgebungen gedacht. Sie dient der Demonstration der Machbarkeit und der Sicherheitskonzepte.

**Diese Software ist NICHT für den Produktionseinsatz geeignet.** Für den Einsatz in einer Produktionsumgebung ist das **sichere Provisioning und der Schutz des Master Authentication Key (MAK)** von entscheidender Bedeutung. Derzeit ist der MAK im Quellcode hinterlegt. Sobald entsprechende Verfahren dafür implementiert sind (z.B. durch Nutzung der ESP32-eigenen Secure-Boot- und Flash-Verschlüsselungsfunktionen oder dedizierte Hardware-Security-Module), kann das System für Feldtests und einen späteren Produktions-Rollout in Betracht gezogen werden.

Die Autoren übernehmen keine Haftung für Schäden oder Verluste, die durch die Verwendung dieser Software entstehen. Die Nutzung erfolgt auf eigenes Risiko.

---

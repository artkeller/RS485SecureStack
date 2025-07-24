# RS485SecureStack: Robuster & Sicherer RS485 Kommunikationsstack

(Alphaversion - aktuell nur auf deutsch)

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

Wichtige Informationen zu den implementierten Sicherheitsmechanismen, dem Bedrohungsmodell und Sicherheitshinweisen für den Einsatz dieses Stacks finden Sie in der [SECURITY.md](SECURITY.md) Datei im Root-Verzeichnis dieses Repositories. Dort werden auch die Architekturentscheidungen zur Sicherheit und die detaillierte Funktionsweise des Schlüsselmanagements und der Schlüsselrotation erläutert.

-----

## 🚀 Unterstützte MCUs

Die Wahl des richtigen Mikrocontrollers ist entscheidend für die Leistungsfähigkeit und die Sicherheitsmerkmale des Stacks, da dieser dedizierte Hardware-Kryptographie-Beschleuniger nutzt.

Die folgende Tabelle gibt eine Übersicht über die Eignung verschiedener MCU-Familien für dieses Projekt, bewertet auf einer Skala von 0 bis 1 (wobei 1 die beste Bewertung darstellt):

| MCU-Familie                  | Wirtschaftlichkeit (0-1) | Eignung für sichere RS485 (0-1)¹ | Arduino Ecosystem Unterstützung (0-1) | Anmerkungen                                                                                                                                              |
| :--------------------------- | :----------------------- | :------------------------------- | :------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **ESP32 (Classic, S, C)** | **1.0** | **1.0** | **1.0** | **Top-Empfehlung.** Beste Kombination aus Kosten, Leistung und einfacher Weiterentwicklung Ihrer bestehenden Codebasis.                                    |
| STMicroelectronics STM32     | 0.7                      | 1.0                              | 0.9                                    | Sehr leistungsfähig, exzellente Krypto-Hardware, aber höherer Portierungsaufwand für Ihren Code.                                                         |
| Microchip SAM                | 0.7                      | 0.95                             | 0.7                                    | Gute Krypto-Hardware, speziell SAM D51. Portierungsaufwand nötig.                                                                                       |
| NXP i.MX RT (z.B. Teensy)    | 0.5                      | 1.0                              | 0.8                                    | Sehr hohe Performance und Krypto-Hardware. Boards (Teensy) sind teurer, Portierung notwendig.                                                          |
| Raspberry Pi RP23xx          | 0.7                      | 0.9                              | 0.7                                    | Neuere Serie mit Hardware-Krypto. Gutes Potenzial, aber noch jung im Vergleich zu etablierten MCUs. Portierung nötig.                                      |
| Nordic Semiconductor nRF52/53 | 0.65                     | 0.75                             | 0.7                                    | Hauptfokus auf Wireless (BLE). Krypto-Hardware (AES/SHA) vorhanden, aber primär für drahtlose Protokolle optimiert; HMAC könnte mehr Software-Anteil haben. |
| Raspberry Pi RP2040          | 0.6                      | 0.3                              | 0.9                                    | **Nicht empfohlen** für dieses Projekt. Keine Hardware-Krypto-Beschleunigung (AES/SHA/HMAC), was Performance und Sicherheit stark beeinträchtigt.          |

---
¹ **Eignung für sichere RS485** bezieht sich auf die Verfügbarkeit von Hardware-Beschleunigern für AES, SHA und HMAC sowie geeigneten UART-Schnittstellen.

**Begründung für die Wirtschaftlichkeit:** Die "Wirtschaftlichkeit" berücksichtigt nicht nur den reinen Chip-Preis, sondern auch die **Entwicklungskosten**. Da Sie bereits eine funktionierende ESP32-Codebasis haben, ist die Weiterführung auf dieser Plattform die mit Abstand kostengünstigste Option in Bezug auf Zeit und Aufwand. Eine Umstellung auf eine andere MCU-Familie würde erhebliche Umschreibungs- und Testkosten verursachen, die potenzielle Hardware-Einsparungen (falls vorhanden) bei weitem übersteigen würden.

## 📦 Kernkomponenten & Bibliotheken

Der `RS485SecureStack` baut auf Standard-Arduino-Bibliotheken und spezialisierten Krypto-Bibliotheken auf:

* `RS485SecureStack.h` / `RS485SecureStack.cpp`: Die Hauptimplementierung des Kommunikationsstacks.
* `HardwareSerial.h`: Für die RS485-Kommunikation über eine der Hardware-UART-Schnittstellen des MCUs (z.B. `Serial1`, `Serial2`).
* `Crypto.h`, `AES.h`, `HMAC.h`, `SHA256.h`: Diese Bibliotheken stellen die Schnittstellen zu den Hardware-Kryptographie-Engines des ESP32 bereit. Sie sind entscheidend für die Leistung und Sicherheit von AES128-Verschlüsselung, SHA256-Hashing und HMAC-Generierung.
* `credantials.h`: Eine separate Datei (aus Sicherheitsgründen nicht Teil des Repositories), die den `MASTER_KEY` enthält. Dieser Schlüssel muss auf *allen* Geräten im Netzwerk identisch sein.
* `Adafruit_GFX.h`, `Adafruit_ST7789.h` (für `bus_monitor_esp32.ino`): Werden für die Ansteuerung des TFT-Displays auf dem LilyGo T-Display S3 verwendet.

## 🛠️ Hardware-Setup

Für den Betrieb des `RS485SecureStack` benötigen Sie:

* **ESP32-basierte Entwicklungsboards:** Beliebige Boards der ESP32-, ESP32-S- oder ESP32-C-Serie sind geeignet. Beispiele: ESP32-DevKitC, ESP32-C3-DevKitM-1, LilyGo T-Display S3.
* **RS485 Transceiver Modul:** Ein Konverter von TTL (UART) zu RS485-Signalen, z.B. Module mit dem MAX485-Chip.
* **DE/RE Pin:** Ein GPIO-Pin des ESP32, der den `DE` (Driver Enable) und `RE` (Receiver Enable) Pins des RS485-Transceivers steuert. Dieser Pin muss beim Senden auf HIGH und beim Empfangen auf LOW gesetzt werden. (Beispiel: `const int RS485_DE_RE_PIN = 3;`).
* **UART-Schnittstelle:** Eine der Hardware-UARTs des ESP32 (z.B. `Serial1`, `Serial2`). Die `Serial` (UART0) kann auch verwendet werden, aber Vorsicht bei Konflikten mit der USB-Serial-Ausgabe für Debugging. Es wird dringend empfohlen, eine separate UART für RS485 zu verwenden.
    * Beispiel: `HardwareSerial& rs485Serial = Serial1;`
* **Für den Bus-Monitor:** Zusätzlich ein TFT-Display, z.B. das ST7789, wie es auf dem LilyGo T-Display S3 zu finden ist.

## 🧪 Testkonfiguration und Anwendungsfall (Reale Welt)

Um die Funktionalität, Sicherheit und Robustheit des RS485SecureStack zu demonstrieren und zu testen, verwenden wir die folgende reale Hardware-Konfiguration. Dieses Szenario simuliert eine typische Automatisierungsumgebung mit einer zentralen Steuerung, verteilten Sub-Systemen und Endgeräten, ergänzt durch ein Überwachungsmodul.

### Systemübersicht (Graphic)

```doc

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
| **~10m**| **Twisted-Pair Kabel** | Für die RS485-Busleitung (A und B Leitungen). Abgeschirmtes Kabel (z.B. CAT5/6) empfohlen für geringere Störungen. | Cat5e/Cat6 Netzwerkkabel                                     |
| **6** | **Micro-USB Kabel** | Zur Stromversorgung und Programmierung der ESP32 Boards.                                                 | Standard Micro-USB Kabel                                      |
| **6** | **Breadboard / Steckplatine** | Zum einfachen Aufbau der Schaltung (ESP32 + RS485 Modul).                                                | Beliebiges Standard-Breadboard                                |
| **~50** | **Jumper Wires (m-f, m-m)** | Für die Verbindungen auf dem Breadboard und zwischen Modulen.                                             | Verschiedene Längen und Typen (Male-Female, Male-Male)      |
| **2** | **120 Ohm Abschlusswiderstand** | Optional, aber empfohlen für lange Busleitungen oder hohe Baudraten zur Impedanzanpassung. An beiden Enden des Busses anbringen. | 1/4W Widerstand                                               |

### Verdrahtungshinweise (Allgemein)

Für jeden ESP32 (C3 und S3):

* **ESP32 TX** an **MAX485 DI**
* **ESP32 RX** an **MAX485 RO**
* **ESP32 GPIO (DE/RE)** an **MAX485 DE & RE** (oft gebrückt) - dieser Pin muss im Sketch entsprechend gesetzt werden (HIGH für Senden, LOW für Empfangen). In unserem `RS485SecureStack` wurde das noch nicht direkt im Stack implementiert. Für Tests kann man ihn fest auf LOW lassen und nur lauschen oder manuell für den Master steuern. **Für eine funktionierende Sendefähigkeit des Master/Submaster/Clients muss dieser Pin jedoch vom Sketch gesteuert werden!**
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
    * **Monitor (Debug-Modus):** Zeigt die empfangenen Pakete an, aber mit dem Hinweis "HMAC_OK: NO", was die erfolgreiche Abwehr der Manipulation demonstriert.

Dieses umfassende Setup ermöglicht eine realitätsnahe Validierung der robusten und sicheren Kommunikationsfähigkeiten des RS485SecureStack-Projekts.

-----

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

Dieses Projekt ist unter der **MIT-Lizenz** lizenziert. Details finden Sie in der [LICENSE.md](LICENSE.md)-Datei im Root-Verzeichnis dieses Repositories.

### Disclaimer

**Wichtiger Hinweis:** Diese Version des RS485SecureStack ist **explizit für Proof-of-Concepts (PoCs)** und Evaluierungen in kontrollierten Umgebungen gedacht. Sie dient der Demonstration der Machbarkeit und der Sicherheitskonzepte.

**Diese Software ist NICHT für den Produktionseinsatz geeignet.** Für den Einsatz in einer Produktionsumgebung ist das **sichere Provisioning und der Schutz des Master Authentication Key (MAK)** von entscheidender Bedeutung. Derzeit ist der MAK im Quellcode hinterlegt. Sobald entsprechende Verfahren dafür implementiert sind (z.B. durch Nutzung der ESP32-eigenen Secure-Boot- und Flash-Verschlüsselungsfunktionen oder dedizierte Hardware-Security-Module), kann das System für Feldtests und einen späteren Produktions-Rollout in Betracht gezogen werden.

Die Autoren übernehmen keine Haftung für Schäden oder Verluste, die durch die Verwendung dieser Software entstehen. Die Nutzung erfolgt auf eigenes Risiko.

---

## 👨‍💻 Autor & Copyright

* **Autor:** Thomas Walloschke
* **Kontakt:** artkeller@gmx.de
* **Copyright:** © 2025 Thomas Walloschke. Alle Rechte vorbehalten.

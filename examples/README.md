# RS485SecureCom: Eine umfassende RS485-Kommunikationsanwendung

Dieses Verzeichnis enthält die **`RS485SecureCom`**-Applikation, ein umfangreiches Anwendungsbeispiel, das die Leistungsfähigkeit und die Sicherheitsfunktionen des `RS485SecureStack` in einer simulierten industriellen Umgebung demonstriert. Es handelt sich um ein Proof-of-Concept (PoC), das verschiedene Node-Typen implementiert, die über einen sicheren RS485-Bus kommunizieren.

## 🎯 Ziel der Applikation

Die `RS485SecureCom`-Applikation dient mehreren Zielen:

* **Demonstration:** Zeigt die praktische Anwendung des `RS485SecureStack` für sichere und robuste RS485-Kommunikation.
* **Best Practices:** Veranschaulicht, wie Master-, Submaster-, Client- und Monitoring-Rollen implementiert und verwaltet werden können.
* **Testbed:** Bietet eine funktionierende Umgebung zum Testen der Baudraten-Einmessung, des dynamischen Rekeyings, der Multi-Master-Erkennung und der allgemeinen Datenintegrität unter realitätsnahen Bedingungen.
* **Entwicklungsreferenz:** Dient als Ausgangspunkt und Referenz für die Entwicklung eigener RS485-basierter Anwendungen mit dem `RS485SecureStack`.

## 🏛️ Systemübersicht

Die `RS485SecureCom`-Applikation besteht aus mehreren spezialisierten Nodes, die über einen einzigen RS485-Bus miteinander verbunden sind. Jede Node-Rolle wird durch einen separaten Arduino-Sketch (für ESP32) implementiert.

### Topologie

        +----------------+                       +----------------+
        |    Scheduler   | Master (Address 0)    |                |
        |  ESP32-C3 Dev. |<----------    ------->|   RS485 Bus    |
        |     (UART0)    |                       |  (Twisted Pair)|
        +----------------+                       +----------------+
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


*(Hinweis: Die Pfeile auf dem RS485 Bus symbolisieren bidirektionale Kommunikation. Jeder Node ist über einen RS485 Transceiver mit dem Bus verbunden.)*

## ⚙️ Komponenten und deren Rollen 

Die `RS485SecureCom`-Applikation besteht aus den folgenden Kern-Sketches, die jeweils eine spezifische Rolle im RS485-Netzwerk einnehmen:

### 1. `scheduler_main_esp32.ino` (Master-Node)

* **Adresse:** `0` (Standardadresse für den Master)
* **Rolle:** Der Scheduler ist der zentrale Orchestrator des RS485-Busses. Er ist verantwortlich für die Bus-Initialisierung, das Management der Baudrate, die Zuteilung von Sendeerlaubnissen an Submaster und die Überwachung der Bus-Integrität.
* **Schlüsselfunktionen:**
    * **Automatisierte Baudraten-Einmessung:** Testet beim Start verschiedene Baudraten, um die höchste stabile Rate für das gesamte Netzwerk zu finden und setzt diese.
    * **Master-Heartbeat:** Sendet regelmäßig (`MSG_TYPE_MASTER_HEARTBEAT`, `'H'`) einen Heartbeat, um seine Präsenz zu signalisieren.
    * **Dynamisches Rekeying:** Initiiert den Prozess zur Verteilung neuer Session Keys (`MSG_TYPE_KEY_UPDATE`, `'K'`) an alle Teilnehmer zur Erhöhung der Langzeit-Sicherheit.
    * **Zugriffskontrolle:** Kann Sendeerlaubnis (`MSG_TYPE_DATA`, Payload "PERMISSION_TO_SEND") an Submaster vergeben, um Kollisionen in einem Multi-Master-Szenario zu vermeiden.
    * **Fehler- und Rogue-Master-Erkennung:** Überwacht auf Kommunikationsfehler (HMAC-Fehler, fehlende ACKs) und erkennt das Auftreten eines unerwarteten Masters auf dem Bus. Im Falle eines Rogue Masters geht er in einen sicheren Zustand.
* **Wichtige Code-Details:**
    * Verwendet eine `std::map` (`connectedNodes`) zur Verwaltung des Zustands der verbundenen Clients und Submaster.
    * Die `onPacketReceived` Callback-Funktion ist kritisch für die Verarbeitung von ACKs/NACKs, Master-Heartbeats (für Kollisionserkennung), Baudraten-Set-Bestätigungen und Key-Update-Anfragen.
    * Die `manageDERE` Funktion steuert den DE/RE-Pin des RS485-Transceivers für das Senden.

### 2. `submaster_main_esp32.ino` (Submaster-Node)

* **Adressen:** `1`, `2` (Beispieladressen; für jeden Submaster eindeutig)
* **Rolle:** Ein Submaster ist ein intelligenter Knoten, der vom Scheduler (Master) Sendeerlaubnis erhalten muss, um dann mit seinen zugeordneten Clients zu kommunizieren. Er fungiert als Gateway für eine Gruppe von Clients.
* **Schlüsselfunktionen:**
    * **Master-Präsenzüberwachung:** Überwacht den Heartbeat des Masters; geht in den `WAITING_FOR_MASTER`-Zustand, wenn der Master offline ist.
    * **Permission-to-Send-Empfang:** Wartet auf eine explizite Sendeerlaubnis vom Master, bevor er selbst Nachrichten auf den Bus sendet.
    * **Client-Kommunikation:** Fragt regelmäßig Daten von seinen zugewiesenen Clients ab (`MSG_TYPE_DATA`, Payload "GET_STATUS") oder sendet Befehle.
    * **Baudraten- und Key-Update-Verarbeitung:** Passt seine Baudrate und Session Key ID an, wenn er eine entsprechende Nachricht vom Master erhält.
* **Wichtige Code-Details:**
    * Implementiert einen State Machine (`SubmasterState`) zur Verwaltung des Kommunikationsflusses (Warten auf Master, Warten auf Erlaubnis, Senden von Daten, Idle).
    * Die `onPacketReceived` Funktion verarbeitet Heartbeats, Baudraten- und Key-Updates vom Master sowie Antworten von Clients.
    * Verwendet `rs485Stack.sendMessage()` zum Senden von Daten an Clients und Status an den Master.

### 3. `client_main_esp32.ino` (Client-Node)

* **Adressen:** `11`, `12` (Beispieladressen; für jeden Client eindeutig)
* **Rolle:** Ein Client ist eine passive Node, die auf Anfragen vom Master oder einem Submaster reagiert. Er sendet keine Nachrichten unaufgefordert, sondern antwortet nur auf spezifische Abfragen.
* **Schlüsselfunktionen:**
    * **Master-Präsenzüberwachung:** Ähnlich wie der Submaster, um den Status des Masters zu verfolgen.
    * **Anfragebehandlung:** Empfängt Datenanfragen (`MSG_TYPE_DATA`, z.B. Payload "GET_STATUS") und antwortet mit entsprechenden Daten (`MSG_TYPE_DATA`, z.B. "STATUS_OK").
    * **Baudraten- und Key-Update-Verarbeitung:** Passt seine Baudrate und Session Key ID an, wenn er eine entsprechende Nachricht vom Master erhält.
* **Wichtige Code-Details:**
    * Einfachere State Machine (`ClientState`) als der Submaster (Warten auf Master, Idle).
    * Die `onPacketReceived` Funktion ist hauptsächlich auf das Parsen von Datenanfragen und das Senden von Antworten ausgelegt.

### 4. `bus_monitor_esp32.ino` (Bus-Monitor-Node)

* **Adresse:** `254` (Eine hohe, ungenutzte Adresse; der Monitor sendet selbst keine aktiven Nachrichten)
* **Rolle:** Der Bus-Monitor ist ein passiver Lauscher auf dem RS485-Bus. Er entschlüsselt und analysiert den gesamten Verkehr, um Debugging und Systemüberwachung zu ermöglichen. Er interagiert nicht aktiv mit dem Bus-Management, kann aber ACKs/NACKs senden, wenn er gültige Pakete empfängt und seine Adresse als Ziel angegeben ist.
* **Schlüsselfunktionen:**
    * **Passive Überwachung:** Lauscht auf alle Pakete auf dem Bus.
    * **Entschlüsselung & Validierung:** Nutzt den `MASTER_KEY` und die Session Keys, um Pakete zu entschlüsseln und deren HMAC zu überprüfen.
    * **Echtzeit-Anzeige:** Verwendet ein TFT-Display (z.B. LilyGo T-Display S3) zur Darstellung wichtiger Bus-Metriken.
    * **Rogue-Master-Erkennung:** Zeigt explizit an, wenn ein unerwarteter Master-Heartbeat von einer anderen als der Master-Adresse 0 erkannt wird.
    * **Fehler-Reporting:** Zeigt Checksum- und HMAC-Fehler an.
* **Wichtige Code-Details:**
    * Nutzt die `RS485SecureStack::registerReceiveCallback` Funktion, um alle empfangenen Pakete zu verarbeiten.
    * Führt Metriken wie `packetsPerSecond`, `bytesPerSecond`, `totalChecksumErrors`, `totalHmacErrors` usw.
    * Implementiert mehrere Anzeigemodi (`MODE_SIMPLE_DASHBOARD`, `MODE_TRAFFIC_ANALYSIS`, `MODE_DEBUG_TRACE`) für das TFT-Display, die über serielle Eingaben gewechselt werden können.
    * LVGL-Integration: Nutzt die LVGL-Bibliothek für eine moderne und interaktive Benutzeroberfläche auf dem TFT-Display, anstelle von direkten Textausgaben.

## 🛠️ Bill of Materials (BOM) für die Applikation

Für den Aufbau der vollständigen `RS485SecureCom`-Applikation benötigen Sie folgende Komponenten:

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

## 🔌 Verdrahtungshinweise (Allgemein)

Für jeden ESP32 (C3 und S3) mit einem MAX485 Transceiver Modul:

* **ESP32 TX** an **MAX485 DI**
* **ESP32 RX** an **MAX485 RO**
* **ESP32 GPIO (DE/RE)** an **MAX485 DE & RE** (oft gebrückt) - **Dieser Pin muss im Sketch von `manageDERE(true)` für Senden und `manageDERE(false)` für Empfangen gesteuert werden!**
* **MAX485 VCC** an **ESP32 3.3V**
* **MAX485 GND** an **ESP32 GND**
* **MAX485 A** an **RS485 Bus A**
* **MAX485 B** an **RS485 Bus B**

Alle "A"-Pins der MAX485 Module werden miteinander verbunden, ebenso alle "B"-Pins.
Der RS485-Bus sollte als eine durchgehende Linie (Daisy-Chain) und nicht als Stern-Topologie verdrahtet werden.

## 🚀 Erste Schritte

### Installation der Bibliotheken

Stellen Sie sicher, dass die folgenden Bibliotheken in Ihrer Arduino IDE installiert sind:

* **`RS485SecureStack`:** (Dieses Repository) Fügen Sie es über `Skizze > Bibliothek einbinden > .ZIP-Bibliothek hinzufügen...` ein, nachdem Sie das Repository heruntergeladen haben.
* **`Adafruit GFX Library`:** Über den Arduino Bibliotheksverwalter (`Skizze > Bibliothek einbinden > Bibliotheken verwalten...`).
* **`Adafruit ST7789 Library`:** Über den Arduino Bibliotheksverwalter.
* **`LVGL`:** Über den Arduino Bibliotheksverwalter. (Für den Bus-Monitor mit LVGL-UI).
* **`TFT_eSPI`:** Über den Arduino Bibliotheksverwalter. (Für den Bus-Monitor mit LVGL-UI).
    * **WICHTIG für TFT_eSPI:** Nach der Installation müssen Sie die Konfigurationsdatei für Ihr Display anpassen. Gehen Sie zu `libraries/TFT_eSPI/User_Setup_Select.h` im Arduino Sketchbook-Ordner. Aktivieren Sie die Zeile, die zu Ihrem LilyGo T-Display S3 passt (z.B. `#include <User_Setups/Setup25_TTGO_T_Display.h>`) und kommentieren Sie andere aus. Prüfen Sie die Pin-Definitionen in der gewählten `User_Setup_*.h`-Datei.

### Anpassung der Sketches

Für jeden Sketch, den Sie verwenden möchten:

1.  **Öffnen Sie den Sketch:** Navigieren Sie zu diesem `examples/` Verzeichnis und öffnen Sie den gewünschten `*.ino`-Sketch in der Arduino IDE.
2.  **`credantials.h`:** Stellen Sie sicher, dass der `MASTER_KEY` in `credantials.h` (im Projekt-Root) für alle Geräte, die am selben Bus kommunizieren sollen, **identisch** ist.
3.  **UART und DE/RE Pin:**
    * Die Beispiel-Sketches verwenden standardmäßig `Serial1` für RS485-Kommunikation. Passen Sie `HardwareSerial& rs485Serial = Serial1;` und die GPIO-Pins für RX/TX sowie den `RS485_DE_RE_PIN` (`const int RS485_DE_RE_PIN = 3;`) an die tatsächlichen Anschlüsse Ihrer Hardware an.
    * **Hinweis zu `Serial` (UART0):** `Serial` wird oft für die USB-Debugausgabe verwendet. Eine separate UART (wie `Serial1` oder `Serial2`) ist für stabile RS485-Kommunikation dringend empfohlen, um Konflikte zu vermeiden.
4.  **Eindeutige Adressen:** Stellen Sie sicher, dass `MY_ADDRESS` in jedem Sketch für jeden physischen Node **eindeutig** ist (z.B. Scheduler=0, Submaster1=1, Submaster2=2, Client1=11, Client2=12, Monitor=254).
5.  **TFT-Pins (nur für `bus_monitor_esp32.ino`):** Überprüfen Sie die `#define`s für `TFT_CS`, `TFT_DC`, `TFT_RST`, `TFT_MOSI`, `TFT_SCLK`, `TFT_MISO` in `bus_monitor_esp32.ino` und passen Sie diese an die spezifische Pinbelegung Ihres LilyGo T-Display S3 an.

### Flashen und Inbetriebnahme

1.  **Flashen:**
    * Wählen Sie in der Arduino IDE das korrekte Board und den COM-Port für das jeweilige ESP32-Board aus (`Werkzeuge > Board`, `Werkzeuge > Port`).
    * Laden Sie die vorbereiteten Sketches auf jedes Board entsprechend seiner Rolle (`scheduler_main_esp32.ino`, `submaster_main_esp32.ino`, `client_main_esp32.ino`, `bus_monitor_esp32.ino`).
2.  **Inbetriebnahme-Reihenfolge:**
    * Starten Sie zuerst den **Scheduler (Master)**. Er beginnt mit der Bus-Initialisierung und Baudraten-Einmessung.
    * Schalten Sie anschließend die **Submaster** und **Clients** ein. Sie sollten die Baudraten-Anweisungen des Masters empfangen und sich anpassen.
    * Zum Schluss schalten Sie den **Bus-Monitor** ein. Er sollte automatisch die Baudrate des Busses erkennen und den Verkehr anzeigen.

## 🏃 Betriebsszenarien der `RS485SecureCom` Applikation

Die Applikation ist darauf ausgelegt, verschiedene Betriebsszenarien zu demonstrieren, die auch die implementierten Sicherheits- und Safety-Funktionen des `RS485SecureStack` hervorheben:

1.  **Systemstart & Baudraten-Einmessung**
    * Alle Nodes starten auf einer vordefinierten Standard-Baudrate (z.B. 9600 bps).
    * Der **Scheduler** (`scheduler_main_esp32.ino`) initiiert und führt eine **automatisierte Baudraten-Einmessung** durch. Er testet verschiedene Baudraten und sendet die optimale Rate an alle Bus-Teilnehmer (`MSG_TYPE_BAUD_RATE_SET`, `'B'`).
    * Alle anderen Nodes passen ihre UART-Baudrate entsprechend an.
    * Der **Bus-Monitor** (`bus_monitor_esp32.ino`) visualisiert den Einmessprozess und die resultierende stabile Baudrate auf seinem TFT-Display.

2.  **Regelmäßiger Betrieb & Kommunikation**
    * Der **Scheduler** sendet kontinuierlich seinen Heartbeat (`MSG_TYPE_MASTER_HEARTBEAT`, `'H'`), um seine Präsenz und den aktiven Status zu signalisieren.
    * Der **Scheduler** kann Sendeerlaubnis (`MSG_TYPE_DATA`, Payload "PERMISSION_TO_SEND") an die **Submaster** vergeben, die dann ihrerseits mit ihren zugeordneten **Clients** kommunizieren können (z.B. Sensordaten abfragen, Befehle senden).
    * Die **Clients** (`client_main_esp32.ino`) reagieren auf Anfragen der Submaster oder des Schedulers (`MSG_TYPE_DATA`).
    * Der **Bus-Monitor** zeigt im Dashboard- oder Traffic-Modus die Master-Präsenz, die aktuelle Baudrate, die verwendete Key ID für die Verschlüsselung, die Paket- und Byte-Raten pro Sekunde sowie die aktuellen Fehlerraten an.

3.  **Dynamisches Rekeying**
    * Nach einer vordefinierten Zeit oder bei Bedarf (triggerbar über serielle Eingabe am Scheduler) initiiert der **Scheduler** einen Rekeying-Prozess.
    * Er generiert eine neue Session Key ID und den entsprechenden neuen Session Key, den er sicher (`MSG_TYPE_KEY_UPDATE`, `'K'`) an alle teilnehmenden Nodes verteilt.
    * Alle Nodes wechseln zur neuen Key ID und verwenden den neuen Schlüssel für die nachfolgende Kommunikation.
    * Der **Bus-Monitor** zeigt den Wechsel der Key ID an und verifiziert, dass die Kommunikation mit dem neuen Schlüssel erfolgreich entschlüsselt wird, was die Effektivität des dynamischen Rekeyings demonstriert.

4.  **Fehlerfall: Baudrate verschlechtert sich**
    * **Manuelle Simulation:** Während des Betriebs kann die Qualität der Busleitung absichtlich verschlechtert werden (z.B. durch Entfernen eines Abschlusswiderstands oder durch Einführung externer Störquellen am Test-Setup).
    * Der **Scheduler** registriert erhöhte Kommunikations-Fehlerraten (z.B. fehlende ACKs von Clients, HMAC-Fehler).
    * Im Falle einer signifikanten Verschlechterung kann der **Scheduler** eine erneute Baudraten-Einmessung initiieren und die Baudrate reduzieren, um die Kommunikationsstabilität wiederherzustellen.
    * Der **Bus-Monitor** visualisiert die erhöhten Fehlerraten und einen eventuellen Baudraten-Abstieg, was die Robustheit und Anpassungsfähigkeit des Systems unterstreicht.

5.  **Safety-Test: Erkennung eines unerlaubten Masters**
    * **Simulationsschritt:** Ein zweiter ESP32, der ebenfalls mit dem `scheduler_main_esp32.ino` Sketch geflasht wurde (aber mit einer **anderen Adresse als 0**, z.B. Adresse `5`), wird nachträglich an den Bus angeschlossen. Dieser simuliert einen "Rogue Master".
    * Dieser "Rogue Master" beginnt ebenfalls, Heartbeats zu senden.
    * Der **legitime Scheduler** (Adresse 0) erkennt den Heartbeat des Rogue Masters.
    * Der **legitime Scheduler** geht in einen "DANGER MODE" und stoppt alle aktiven Sendeoperationen, um Kollisionen zu verhindern und die Kontrolle des Busses zu sichern. Eine serielle Ausgabe und/oder eine LED-Anzeige signalisiert diesen kritischen Fehlerzustand.
    * Der **Bus-Monitor** zeigt auf seinem Display deutlich die Erkennung des unerwarteten Master-Heartbeats und dessen Absenderadresse an, was die Schutzfunktion des Systems unterstreicht.

6.  **Sicherheits-Test: Abwehr von Spoofing und Manipulation**
    * **Simulationsschritt:** Ein externer Angreifer-Node versucht, Pakete in den Bus einzuschleusen, die entweder keinen oder einen falschen HMAC aufweisen.
    * Alle **Nodes** (Scheduler, Submaster, Clients, Bus-Monitor) empfangen diese Pakete.
    * Die `RS485SecureStack`-Bibliothek führt jedoch eine strenge HMAC-Prüfung durch. Da der HMAC ungültig ist, werden die Pakete sofort verworfen, ohne dass deren Inhalt verarbeitet wird.
    * Der **Bus-Monitor** (im Debug-Modus) kann die empfangenen Pakete protokollieren, aber mit dem expliziten Hinweis "HMAC_OK: NO", was die erfolgreiche Abwehr der Manipulation demonstriert und die Datenintegrität des Systems gewährleistet.

Diese Szenarien verdeutlichen die umfassenden Fähigkeiten der `RS485SecureCom`-Applikation, ein sicheres, zuverlässiges und intelligent verwaltetes RS485-Netzwerk zu betreiben.

## ⚠️ Disclaimer für Anwendungsbeispiele

Diese Beispiele sind **ausschließlich für Proof-of-Concepts (PoCs)**, Evaluierungen und Entwicklungszwecke in kontrollierten Umgebungen gedacht. Sie dienen der Demonstration der Machbarkeit und der Sicherheitskonzepte des `RS485SecureStack`.

**Diese Software ist NICHT für den Produktionseinsatz geeignet.** Für den Einsatz in einer Produktionsumgebung ist das **sichere Provisioning und der Schutz des Master Authentication Key (MAK)** von entscheidender Bedeutung. Derzeit ist der MAK im Quellcode hinterlegt. Implementierungen für Secure-Boot, Flash-Verschlüsselung und Hardware-Security-Module, die für einen produktiven Einsatz notwendig wären, sind in diesen Beispielen nicht enthalten.

Die Autoren übernehmen keine Haftung für Schäden oder Verluste, die durch die Verwendung dieser Software entstehen. Die Nutzung erfolgt auf eigenes Risiko.

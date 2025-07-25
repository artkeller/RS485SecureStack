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
    * Die Flussrichtungssteuerung (DE/RE-Pin) wird nun vollständig von der `RS485SecureStack`-Bibliothek übernommen, nachdem die entsprechende `RS485DirectionControl`-Implementierung beim Initialisieren des Stacks konfiguriert wurde (siehe "Verdrahtungshinweise").

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
    * Verwendet `rs485Stack.sendMessage()` zum Senden von Daten an Clients und Status an den Master. Die Flussrichtungssteuerung erfolgt automatisch durch die Bibliothek.

### 3. `client_main_esp32.ino` (Client-Node)

* **Adressen:** `11`, `12` (Beispieladressen; für jeden Client eindeutig)
* **Rolle:** Ein Client ist eine passive Node, die auf Anfragen vom Master oder einem Submaster reagiert. Er sendet keine Nachrichten unaufgefordert, sondern antwortet nur auf spezifische Abfragen.
* **Schlüsselfunktionen:**
    * **Master-Präsenzüberwachung:** Ähnlich wie der Submaster, um den Status des Masters zu verfolgen.
    * **Anfragebehandlung:** Empfängt Datenanfragen (`MSG_TYPE_DATA`, z.B. Payload "GET_STATUS") und antwortet mit entsprechenden Daten (`MSG_TYPE_DATA`, z.B. "STATUS_OK").
    * **Baudraten- und Key-Update-Verarbeitung:** Passt seine Baudrate und Session Key ID an, wenn er eine entsprechende Nachricht vom Master erhält.
* **Wichtige Code-Details:**
    * Einfachere State Machine (`ClientState`) als der Submaster (Warten auf Master, Idle).
    * Die `onPacketReceived` Funktion ist hauptsächlich auf das Parsen von Datenanfragen und das Senden von Antworten ausgelegt. Die Flussrichtungssteuerung erfolgt automatisch durch die Bibliothek.

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

## 📦 Anwendungs-Protokoll der `RS485SecureCom` Applikation

Die `RS485SecureCom`-Applikation baut auf dem grundlegenden Datagramm-Format des `RS485SecureStack` auf. Details zum Aufbau des Datagramms auf Byte-Ebene (Header, IV, Payload, HMAC, Byte-Stuffing etc.) finden Sie in der [zentralen `README.md`](../README.md) im Root-Verzeichnis dieses Projekts unter dem Abschnitt "RS485SecureStack: Protokoll-Spezifikation (Datagramm-Format)".

Dieses Kapitel beschreibt, wie die `RS485SecureCom`-Applikation die `MessageType` und den `Encrypted Payload` des Datagramms nutzt, um die spezifischen Kommunikationsbedürfnisse des Netzwerks zu erfüllen.

### `MessageType` (1 Byte im Datagramm-Header)

Der `MessageType` ist ein einzelnes Zeichen (`char`), das den Typ der Nachricht auf Anwendungsebene definiert. Dies ermöglicht den Nodes, Pakete je nach ihrem Inhalt und Zweck unterschiedlich zu verarbeiten. Die `RS485SecureCom`-Applikation definiert die folgenden Nachrichtentypen:

| `MessageType` (char) | Beschreibung                      | Sender (primär)        | Empfänger (primär)   | Zuverlässigkeit (ACK/NACK) | Nutzungszweck in `RS485SecureCom`                 |
| :------------------- | :-------------------------------- | :--------------------- | :------------------- | :------------------------- | :------------------------------------------------ |
| `'H'`                | **Heartbeat** | Master (Scheduler)     | Alle (Broadcast)     | Optional                   | Master-Präsenzanzeige, Rogue-Master-Erkennung     |
| `'B'`                | **Baud Rate Set** | Master (Scheduler)     | Alle (Broadcast)     | Erforderlich               | Dynamische Anpassung der Bus-Geschwindigkeit      |
| `'K'`                | **Key Update** | Master (Scheduler)     | Alle (Broadcast)     | Erforderlich               | Verteilung neuer Session Keys (Rekeying)          |
| `'D'`                | **Data/Command** | Master, Submaster, Client | Master, Submaster, Client | Optional/Erforderlich      | Nutzdaten, Steuerbefehle, Statusabfragen, Sendeerlaubnis |
| `'A'`                | **ACK/NACK (Acknowledgement)** | Alle (Unicast)         | Sender der Originalnachricht | Nicht zutreffend           | Bestätigung oder Ablehnung eines empfangenen Pakets |

### `Encrypted Payload` (Inhalt und Format)

Der Inhalt des `Encrypted Payload` hängt stark vom `MessageType` ab. Die Payloads sind in der Regel einfache Zeichenketten (Strings) oder bei Bedarf JSON-formatierte Strings für komplexere Daten.

#### Details pro `MessageType`:

1.  **`MSG_TYPE_MASTER_HEARTBEAT` (`'H'`):**
    * **Payload-Inhalt:** Typischerweise leer oder enthält eine kurze Statusinformation des Masters (z.B. Uptime, aktuelle Baudrate).
    * **Beispiel:** `"OK"` oder `""`
    * **Verantwortlichkeit:** Der Scheduler sendet dies periodisch, um seine Lebensfähigkeit zu demonstrieren. Andere Nodes prüfen auf diesen Heartbeat, um die Master-Präsenz zu bestätigen. Der Bus-Monitor zeigt dessen Empfang und die Absenderadresse an.

2.  **`MSG_TYPE_BAUD_RATE_SET` (`'B'`):**
    * **Payload-Inhalt:** Die neue Baudrate als ASCII-String (z.B. `"115200"`, `"57600"`).
    * **Beispiel:** `"115200"`
    * **Verantwortlichkeit:** Nur der Scheduler sendet diesen Typ. Alle empfangenden Nodes müssen ihre UART-Baudrate auf den angegebenen Wert umstellen und eine ACK-Nachricht zurücksenden.

3.  **`MSG_TYPE_KEY_UPDATE` (`'K'`):**
    * **Payload-Inhalt:** Eine JSON-Struktur, die die neue `keyID` und den verschlüsselten `sessionKey` enthält.
        ```json
        {"keyID":123,"sessionKey":"base64_encoded_encrypted_key"}
        ```
        Der `sessionKey` ist hierbei vom Master mit dem `MASTER_KEY` verschlüsselt, bevor er in den Payload gepackt wird.
    * **Verantwortlichkeit:** Nur der Scheduler sendet dies. Empfangende Nodes entschlüsseln den Session Key mit ihrem `MASTER_KEY`, speichern ihn unter der neuen `keyID` und verwenden ihn fortan für die Kommunikation. Eine ACK-Nachricht ist erforderlich.
    * **WICHTIGER HINWEIS:** Die Sicherheit des Session Keys hängt maßgeblich vom Schutz des `MASTER_KEY` ab. Sollte der `MASTER_KEY` kompromittiert werden, könnte ein Angreifer Session Keys entschlüsseln und sich unbemerkt in das Netzwerk einschleichen oder manipulierte Nachrichten senden. In Produktionsumgebungen ist daher ein **sicheres Provisioning und der Schutz des `MASTER_KEY` unerlässlich** (z.B. durch Secure Element Hardware oder sichere Schlüsselinjektionsverfahren), was über den Rahmen dieses PoC hinausgeht.

4.  **`MSG_TYPE_DATA` (`'D'`):**
    * **Payload-Inhalt:** Variabel, je nach Anwendungsfall. Kann einfache Statusanfragen, Befehle oder übertragene Sensordaten sein.
    * **Beispiele:**
        * **Permission-to-Send (vom Master an Submaster):** `"PERMISSION_TO_SEND"`
        * **Statusabfrage (vom Submaster an Client):** `"GET_STATUS"`
        * **Statusantwort (vom Client an Submaster/Master):** `"STATUS_OK:25C,70%"`, oder JSON-formatiert `{"temp":25.5,"hum":70.2}`
        * **Befehl (vom Submaster an Client):** `"SET_LED:ON"`
    * **Verantwortlichkeit:** Dieser Typ wird von allen Nodes für die allgemeine Datenkommunikation verwendet. Der Master kann Sendeerlaubnis vergeben, Submaster können Clients befragen/steuern, und Clients antworten.

5.  **`MSG_TYPE_ACK_NACK` (`'A'`):**
    * **Payload-Inhalt:** Typischerweise leer für ACK, oder ein Fehlercode/eine kurze Beschreibung für NACK (z.B. `"NACK:BAD_CRC"`, `"NACK:UNKNOWN_CMD"`).
    * **Beispiel:** `""` (für ACK), `"ERROR_PROCESSING_PAYLOAD"` (für NACK)
    * **Verantwortlichkeit:** Wird als Antwort auf Nachrichten gesendet, die eine Bestätigung erfordern (z.B. `BaudRateSet`, `KeyUpdate`). Sie dient der Bestätigung des Empfangs und der korrekten Verarbeitung.

### Kommunikationsflüsse und Verantwortlichkeiten

Die Kombination aus `MessageType` und Payload definiert die komplexen Interaktionen innerhalb der `RS485SecureCom`-Applikation:

* **Master-Initiierte Abläufe:**
    * **Baudraten-Management:** Master sendet `'B'` mit neuer Rate. Alle antworten mit `'A'`.
    * **Key-Management:** Master sendet `'K'` mit neuem Schlüssel. Alle antworten mit `'A'`.
    * **Sendeerlaubnis:** Master sendet `'D'` (Payload "PERMISSION_TO_SEND") an einen spezifischen Submaster. Der Submaster erhält daraufhin das Senderecht.
* **Submaster-Initiierte Abläufe:**
    * Nach Erhalt der Sendeerlaubnis: Submaster sendet `'D'` (Payload "GET_STATUS" oder Befehl) an seine Clients.
* **Client-Reaktionen:**
    * Client empfängt `'D'` von Master/Submaster und antwortet mit `'D'` (Payload "STATUS_OK" oder Ergebnis des Befehls).
* **Fehlerbehandlung:** Wenn ein Paket nicht entschlüsselt oder der HMAC nicht verifiziert werden kann, wird es stillschweigend verworfen (Bibliotheksverhalten). Wenn ein Paket zwar korrekt entschlüsselt, aber der Inhalt auf Anwendungsebene nicht verarbeitet werden kann, kann eine NACK-Antwort gesendet werden.

Diese anwendungsspezifische Protokollbeschreibung ist entscheidend für Entwickler, die eigene Anwendungen mit dem `RS485SecureStack` erstellen oder die Funktionsweise der `RS485SecureCom`-Beispiele detailliert verstehen möchten.

---

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

Die `RS485SecureCom`-Applikation nutzt den **Halbduplex-Betrieb** des RS485-Busses. Dies bedeutet, dass ein Transceiver zu einem Zeitpunkt entweder senden *oder* empfangen kann, aber nicht beides gleichzeitig.

Die Flussrichtungssteuerung (d.h., das Umschalten des RS485-Moduls zwischen Sende- und Empfangsmodus) wird nun vollständig von der `RS485SecureStack`-Bibliothek über eine dedizierte Hardware-Abstraktionsschicht gehandhabt. Der App-Entwickler muss sich um die Timing-Details nicht mehr kümmern; es ist eine **"Set-and-Forget"-Lösung**.

Je nach Variante Ihres RS485-Transceiver-Moduls (z.B. HW-159 oder ähnliche MAX485-basierte Module) gibt es zwei gängige Methoden, wie die Flussrichtung gesteuert wird. Die Bibliothek unterstützt beide durch eine einfache Konfiguration im Sketch:

1.  **Manuelle Steuerung mittels eines DE/RE-Pins (häufig bei einfachen Modulen):**
    * Viele RS485-Module, die auf dem MAX485-Chip basieren, führen dessen DE (Driver Enable) und RE (Receiver Enable) Pins als einen einzigen Pin (oft gebrückt) nach außen.
    * Diese Module erfordern die Ansteuerung dieses Pins durch einen GPIO des Mikrocontrollers.
    * **Verdrahtung:** Der DE/RE-Pin des RS485-Moduls wird mit einem beliebigen freien GPIO-Pin des ESP32 verbunden (z.B. GPIO 3).

2.  **Automatische Flussrichtungssteuerung (bei Modulen mit integrierter Logik):**
    * Einige RS485-Module (wie das im vorliegenden PoC verwendete HW-159-Modul) verfügen über **zusätzliche Hardware-Logik auf der Platine**, die die DE/RE-Pins des MAX485-Chips intern steuert.
    * Diese Module erkennen selbstständig anhand der Aktivität auf der TX-Leitung des Mikrocontrollers, wann gesendet werden soll.
    * **Verdrahtung:** Für diese Module ist KEIN separater GPIO-Pin vom Mikrocontroller für die DE/RE-Steuerung erforderlich. Das Modul wird dann typischerweise nur über GND, VCC, RX (vom Modul zum MCU) und TX (vom MCU zum Modul) mit dem Mikrocontroller verbunden, zusätzlich zu den RS485-Bus-Leitungen A und B.

**Die grundlegenden Verbindungen (unabhängig von der Flussrichtungssteuerung) sind immer wie folgt herzustellen:**

* **ESP32 TX** an **MAX485 DI** (Driver Input)
* **ESP32 RX** an **MAX485 RO** (Receiver Output)
* **[Nur für Module mit manuellem DE/RE-Pin] ESP32 GPIO (DE/RE)** an **MAX485 DE & RE** (Driver Enable & Receiver Enable, oft gebrückt auf dem Modul)
* **MAX485 VCC** an **ESP32 3.3V** (oder 5V, je nach Modul-Spezifikation und ESP32-Kompatibilität)
* **MAX485 GND** an **ESP32 GND**
* **MAX485 A** an **RS485 Bus A**
* **MAX485 B** an **RS485 Bus B**

Alle "A"-Pins der MAX485 Module werden miteinander verbunden, ebenso alle "B"-Pins.
Die empfohlene Topologie für den RS485-Bus ist eine **durchgehende Linie (Daisy-Chain)**, bei der die Nodes sequenziell an das Hauptkabel angeschlossen sind. Eine Stern-Topologie sollte vermieden werden, um Signalreflexionen und damit verbundene Kommunikationsprobleme zu minimieren.

Für detailliertere Informationen zur RS485-Verkabelung, Buseigenschaften und empfohlenen Topologien, konsultieren Sie bitte die folgenden Ressourcen:

* [Renesas, White Paper: Schnittstellen für Industrie-PCs vereinfachen](https://www.renesas.com/en/document/whp/schnittstellen-f-r-industrie-pcs-vereinfachen#:~:text=RS%2D485%20unterst%C3%BCtzt%20Leitungsl%C3%A4ngen%20bis,in%20Bild%204%20dargestellt%20ist.)
* [Renesas, Application Note, RS-485 Design Guide](https://www.renesas.com/en/document/apn/rs-485-design-guide-application-note#:~:text=Suggested%20Network%20Topology,-RS%2D485%20is&text=RS%2D485%20supports%20several%20topologies,each%20with%20a%20unique%20address.)

---

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
3.  **UART und Flussrichtungssteuerung ("Set-and-Forget"):**
    * Die Beispiel-Sketches verwenden standardmäßig `Serial1` für RS485-Kommunikation. Passen Sie `HardwareSerial& rs485Serial = Serial1;` und die GPIO-Pins für RX/TX an die tatsächlichen Anschlüsse Ihrer Hardware an (normalerweise werden diese für die UART in der `begin()`-Methode der `HardwareSerial` festgelegt oder sind fest verdrahtet).
    * **Die Flussrichtungssteuerung ist nun eine "Set-and-Forget"-Konfiguration:**
        * Fügen Sie am Anfang des Sketches die entsprechende Header-Datei für Ihre Hardware-Variante ein:
            * Für Module **mit** einem manuell zu steuernden DE/RE-Pin: `#include "ManualDE_REDirectionControl.h"`
            * Für Module **mit** automatischer Flussrichtungssteuerung: `#include "AutomaticDirectionControl.h"`
        * Erstellen Sie eine globale Instanz Ihrer `RS485DirectionControl`-Klasse **vor** der Instanziierung des `RS485SecureStack`-Objekts.
            * **Beispiel für manuellen DE/RE-Pin:**
                ```cpp
                const int RS485_DE_RE_PIN = 3; // GPIO für DE/RE Pin, ANPASSEN!
                ManualDE_REDirectionControl myDirectionControl(RS485_DE_RE_PIN);
                ```
            * **Beispiel für automatische Flussrichtungssteuerung:**
                ```cpp
                AutomaticDirectionControl myDirectionControl;
                ```
        * Übergeben Sie diese Instanz im Konstruktor des `RS485SecureStack`-Objekts:
            ```cpp
            RS485SecureStack rs485Stack(&myDirectionControl);
            ```
    * **Hinweis zu `Serial` (UART0):** `Serial` wird oft für die USB-Debugausgabe verwendet. Eine separate UART (wie `Serial1` oder `Serial2`) ist für stabile RS485-Kommunikation dringend empfohlen, um Konflikte zu vermeiden.
4.  **Eindeutige Adressen:** Stellen Sie sicher, dass `MY_ADDRESS` in jedem Sketch für jeden physischen Node **eindeutig** ist (z.B. Scheduler=0, Submaster1=1, Submaster2=2, Client1=11, Client2=12, Monitor=254).
5.  **TFT-Pins (nur für `bus_monitor_esp32.ino`):** Überprüfen Sie die `#define`s für `TFT_CS`, `TFT_DC`, `TFT_RST`, `TFT_MOSI`, `TFT_SCLK`, `TFT_MISO` in `bus_monitor_esp32.ino` und passen Sie diese an die spezifische Pinbelegung Ihres LilyGo T-Display S3 an.

### Flashen und Inbetriebnahme

1.  **Flashen:**
    * Wählen Sie in der Arduino IDE das korrekte Board und den COM-Port für das jeweilige ESP32-Board aus (`Werkzeuge > Board`, `Werkzeuge > Port`).
    * Laden Sie die vorbereiteten Sketches auf jedes Board entsprechend seiner Rolle (`scheduler_main_esp32.ino`, `submaster_main_esp32.ino`, `client_main_esp32.ino`, `bus_monitor_esp32.ino`).
2.  **Inbetriebnahme:**
    * Starten Sie zuerst den **Scheduler (Master)**. Er wird beginnen, den Bus zu initialisieren und die Baudrate einzumessen.
    * Schalten Sie dann die **Submaster** und **Clients** ein. Sie sollten die Baudraten-Anweisungen des Masters empfangen und sich anpassen.
    * Zum Schluss schalten Sie den **Bus-Monitor** ein. Er sollte automatisch die Baudrate des Busses erkennen und beginnen, den Verkehr anzuzeigen.

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
    * Alle Nodes wechseln zur neuen Key ID und verwenden den neuen Schlüssel fortan für die Kommunikation.
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

**Diese Software hat den Reifegrad Alpha und ist (NOCH) NICHT für den Produktionseinsatz geeignet.** Für den Einsatz in einer Produktionsumgebung ist das **sichere Provisioning und der Schutz des Master Authentication Key (MAK)** von entscheidender Bedeutung. Derzeit ist der MAK im Quellcode hinterlegt. Implementierungen für Secure-Boot, Flash-Verschlüsselung und Hardware-Security-Module, die für einen produktiven Einsatz notwendig wären, sind in diesen Beispielen nicht enthalten.

Die Autoren übernehmen keine Haftung für Schäden oder Verluste, die durch die Verwendung dieser Software entstehen. Die Nutzung erfolgt auf eigenes Risiko.

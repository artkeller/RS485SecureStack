// File: bus_monitor_esp32.ino
// App:  RS485SecureCom
// Ver:  0.5.1 (Alpha)
// Date: 20250725
// Auth: Thomas Walloschke, artkeller@gmx.de
// Lic:  MIT
// (c):  2025 Thoams Walloschke

#include "RS485SecureStack.h"
#include "credantials.h" // Pre-Shared Master Key (MUSS AUF ALLEN DEVICES IDENTISCH SEIN!)

// --- LVGL & TFT_eSPI Integration ---
#include <lvgl.h>
#include <TFT_eSPI.h> // Hardware-spezifischer Treiber für das Display

// --- RS485 Konfiguration ---
HardwareSerial& rs485Serial = Serial1;
// Monitor-Adresse: sollte eine ungenutzte Adresse sein
const byte MY_ADDRESS = 254;

RS485SecureStack rs485Stack(rs485Serial, MY_ADDRESS, MASTER_KEY);

// --- LVGL Display und Touch Buffer ---
// Puffer für LVGL Rendering (Anzahl der Zeilen, die auf einmal gerendert werden)
#define LV_HOR_RES_MAX  320 // Breite im Querformat
#define LV_VER_RES_MAX  170 // Höhe im Querformat

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10]; // Ein Puffer für 10 Zeilen

TFT_eSPI tft = TFT_eSPI(LV_HOR_RES_MAX, LV_VER_RES_MAX); // Initialisiere TFT_eSPI

// LVGL Display flushing callback (MUSS so definiert sein)
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.pushImage(area->x1, area->y1, w, h, (uint16_t *)color_p);
    lv_disp_flush_ready(disp);
}

// --- Überwachungsstufen (bleibt gleich, aber jetzt für LVGL-Ansichten) ---
enum MonitorMode {
    MODE_SIMPLE_DASHBOARD,
    MODE_TRAFFIC_ANALYSIS,
    MODE_DEBUG_TRACE
};
MonitorMode currentMonitorMode = MODE_SIMPLE_DASHBOARD; // Start im Dashboard-Modus

// --- Metriken für die Überwachung (bleibt gleich) ---
volatile unsigned long totalPacketsReceived = 0;
volatile unsigned long totalChecksumErrors = 0;
volatile unsigned long totalHmacErrors = 0;
volatile unsigned long packetsPerSecond = 0;
volatile unsigned long bytesPerSecond = 0; // Summe der entschlüsselten Payload-Längen pro Sekunde

unsigned long lastMetricsUpdateTime = 0;
const unsigned long METRICS_UPDATE_INTERVAL_MS = 1000; // Update every second

// Master Heartbeat Monitoring (bleibt gleich)
unsigned long lastMasterHeartbeatRxTime = 0;
const unsigned long MASTER_HEARTBEAT_TIMEOUT_MS = 1000;
bool masterIsPresent = false;

// Master-Kollisionserkennung (bleibt gleich)
bool unexpectedMasterHeartbeat = false;
byte unexpectedMasterAddress = 0;

// Callback-Funktion für empfangene Pakete (bleibt gleich)
void onPacketReceived(byte senderAddr, char msgType, const String& payload);

// --- LVGL-Objekte für die UI (NEU) ---
lv_obj_t * mainScreen;
lv_obj_t * dashboardScreen;
lv_obj_t * trafficScreen;
lv_obj_t * debugScreen;

// Dashboard Labels
lv_obj_t * lbl_master_status;
lv_obj_t * lbl_baudrate;
lv_obj_t * lbl_packets_per_second;
lv_obj_t * lbl_bytes_per_second;
lv_obj_t * lbl_checksum_errors;
lv_obj_t * lbl_hmac_errors;
lv_obj_t * lbl_rogue_master;
lv_obj_t * lbl_key_id; // Für die aktuelle Key ID

// Traffic Analysis Labels/Objects (Platzhalter)
lv_obj_t * lbl_traffic_total_packets;
lv_obj_t * lbl_traffic_current_pkps;
lv_obj_t * lbl_traffic_current_byps;

// Debug Trace Text Area (Platzhalter)
lv_obj_t * ta_debug_log;


// LVGL UI Initialisierung (NEU)
void create_lvgl_ui();
void update_dashboard_ui();
void update_traffic_ui();
void set_monitor_mode(MonitorMode mode);

// NEU: Utility-Funktion zur Konvertierung eines Hex-Strings in ein Byte-Array (bleibt gleich)
size_t hexStringToBytes(const String& hexString, byte* buffer, size_t bufferSize) {
    if (hexString.length() % 2 != 0) return 0;
    size_t byteLength = hexString.length() / 2;
    if (byteLength > bufferSize) return 0;

    for (size_t i = 0; i < byteLength; i++) {
        char hexPair[3];
        hexString.substring(i * 2, i * 2 + 2).toCharArray(hexPair, 3);
        buffer[i] = (byte)strtol(hexPair, NULL, 16);
    }
    return byteLength;
}

void setup() {
  Serial.begin(115200);
  rs485Serial.begin(9600);

  Serial.println("Bus Monitor ESP32-C3 (LilyGo T-Display S3) gestartet.");
  rs485Stack.begin(9600);
  rs485Stack.registerReceiveCallback(onPacketReceived);
  rs485Stack.setAckEnabled(true);
  rs485Stack.setCurrentKeyId(0);

  // --- TFT Setup für ST7789 mit TFT_eSPI ---
  tft.init(); // Initialisiere TFT_eSPI
  tft.setRotation(1); // Rotation 1 oder 3 für Querformat (320x170 Pixel)

  // --- LVGL Initialisierung ---
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, LV_HOR_RES_MAX * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LV_HOR_RES_MAX;
  disp_drv.ver_res = LV_VER_RES_MAX;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Initialisiere die LVGL UI
  create_lvgl_ui();
  set_monitor_mode(MODE_SIMPLE_DASHBOARD); // Startet mit dem Dashboard

  lastMasterHeartbeatRxTime = millis();
  masterIsPresent = true;
}

void loop() {
  lv_timer_handler(); // WICHTIG: LVGL Timer Handler muss regelmäßig aufgerufen werden
  delay(5); // Kurze Pause, um andere Tasks auszuführen

  rs485Stack.processIncoming();

  // --- Master-Anwesenheit prüfen ---
  if (millis() - lastMasterHeartbeatRxTime > MASTER_HEARTBEAT_TIMEOUT_MS) {
    if (masterIsPresent) {
      Serial.println("!!! WARNING: MASTER HEARTBEAT TIMEOUT! Master is GONE or SILENT !!!");
      masterIsPresent = false;
      // Aktualisiere LVGL-Label sofort
      lv_label_set_text_fmt(lbl_master_status, "Master: #ff0000 OFFLINE#");
    }
  } else {
    if (!masterIsPresent) {
      Serial.println("!!! MASTER HEARTBEAT RESTORED! !!!");
      masterIsPresent = true;
      // Aktualisiere LVGL-Label sofort
      lv_label_set_text_fmt(lbl_master_status, "Master: #00ff00 ONLINE#");
    }
  }

  // --- Metriken aktualisieren und Ausgaben steuern ---
  if (millis() - lastMetricsUpdateTime > METRICS_UPDATE_INTERVAL_MS) {
    packetsPerSecond = totalPacketsReceived;
    bytesPerSecond = bytesPerSecond; // Bleibt gleich, da es pro Sekunde summiert wird
    totalPacketsReceived = 0; // Reset
    bytesPerSecond = 0; // Reset
    lastMetricsUpdateTime = millis();

    // Ausgabe auf TFT (LVGL) je nach Modus
    switch (currentMonitorMode) {
      case MODE_SIMPLE_DASHBOARD:
        update_dashboard_ui();
        break;
      case MODE_TRAFFIC_ANALYSIS:
        update_traffic_ui();
        break;
      case MODE_DEBUG_TRACE:
        // Debug Trace ist nur seriell, aber wir könnten die Anzeige auf dem TFT aktualisieren, falls gewollt
        break;
    }
  }

  // --- Moduswechsel (Beispiel: Serielle Eingabe) ---
  if (Serial.available()) {
      char cmd = Serial.read();
      if (cmd == 's') {
          set_monitor_mode(MODE_SIMPLE_DASHBOARD);
          Serial.println("Monitor Mode: Simple Dashboard");
      } else if (cmd == 't') {
          set_monitor_mode(MODE_TRAFFIC_ANALYSIS);
          Serial.println("Monitor Mode: Traffic Analysis");
      } else if (cmd == 'd') {
          set_monitor_mode(MODE_DEBUG_TRACE);
          Serial.println("Monitor Mode: Debug Trace");
      }
  }
}

// Callback-Funktion, wird von RS485SecureStack aufgerufen (bleibt weitestgehend gleich)
void onPacketReceived(byte senderAddr, char msgType, const String& payload) {
  totalPacketsReceived++;
  bytesPerSecond += payload.length();

  if (msgType == RS485SecureStack::MSG_TYPE_MASTER_HEARTBEAT) {
      if (senderAddr == 0) {
          lastMasterHeartbeatRxTime = millis();
      } else {
          if (!unexpectedMasterHeartbeat) {
              Serial.print("!!! Monitor: Detected Master Heartbeat from UNEXPECTED ADDRESS: ");
              Serial.println(senderAddr);
              // Update LVGL Label sofort
              lv_label_set_text_fmt(lbl_rogue_master, "ROGUE MASTER: #ff0000 %d#", senderAddr);
          }
          unexpectedMasterHeartbeat = true;
          unexpectedMasterAddress = senderAddr;
      }
      rs485Stack.sendAckNack(senderAddr, msgType, true);
      return;
  } else {
      // Wenn ein nicht-Heartbeat-Paket empfangen wird, und vorher ein Rogue Master erkannt wurde,
      // sollte der Rogue Master Zustand nur zurückgesetzt werden, wenn es sich *nicht* um ein Heartbeat-Paket handelt.
      // Andernfalls würde er sofort verschwinden, wenn ein reguläres Paket reinkommt.
      // Hier müssen wir aufpassen, dass dies den Status nicht zu schnell löscht.
      // Eine bessere Logik wäre, den Rogue Master für eine gewisse Zeit anzuzeigen, nachdem er gesehen wurde.
      // Fürs Erste lassen wir es so, wie es war.
      // unexpectedMasterHeartbeat = false; // Original
  }

  if (senderAddr == 0) {
      if (msgType == RS485SecureStack::MSG_TYPE_BAUD_RATE_SET) {
          long newBaudRate = payload.toLong();
          Serial.print("Monitor: Baud Rate Set Request from Scheduler for: ");
          Serial.println(newBaudRate);
          rs485Serial.begin(newBaudRate);
          rs485Stack._setBaudRate(newBaudRate);
          rs485Stack.sendAckNack(senderAddr, msgType, true);
          // Update LVGL-Label sofort
          lv_label_set_text_fmt(lbl_baudrate, "Baudrate: %ld bps", newBaudRate);
          return;
      }
      else if (msgType == RS485SecureStack::MSG_TYPE_KEY_UPDATE) {
          Serial.println("Monitor: Received Key Update message from Scheduler.");
          int firstColon = payload.indexOf(':');
          int secondColon = payload.indexOf(':', firstColon + 1);
          if (firstColon != -1 && secondColon != -1) {
              String prefix = payload.substring(0, firstColon);
              if (prefix == "KEY_UPDATE") {
                  uint16_t newKeyId = payload.substring(firstColon + 1, secondColon).toInt();
                  String hexKeyString = payload.substring(secondColon + 1);

                  byte newKey[AES_KEY_SIZE];
                  size_t bytesRead = hexStringToBytes(hexKeyString, newKey, AES_KEY_SIZE);
                  if (bytesRead == AES_KEY_SIZE) {
                      rs485Stack.setSessionKey(newKeyId, newKey);
                      rs485Stack.setCurrentKeyId(newKeyId);
                      Serial.print("Monitor: Updated to Key ID: ");
                      Serial.println(newKeyId);
                      rs485Stack.sendAckNack(senderAddr, msgType, true);
                      // Update LVGL-Label sofort
                      lv_label_set_text_fmt(lbl_key_id, "Key ID: #00ffff %d#", newKeyId);
                      return;
                  } else {
                      Serial.println("Monitor: ERROR parsing hex key string from update message.");
                  }
              } else {
                 Serial.println("Monitor: ERROR: Key Update message has unexpected prefix.");
              }
          } else {
              Serial.println("Monitor: ERROR: Key Update message has unexpected format.");
          }
      }
  }

  if (currentMonitorMode == MODE_DEBUG_TRACE) {
    String debug_msg = "";
    debug_msg += "PKT: From ";
    debug_msg += senderAddr;
    debug_msg += ", Type: ";
    debug_msg += msgType;
    debug_msg += ", Payload_Len: ";
    debug_msg += payload.length();
    debug_msg += ", Payload: '";
    debug_msg += payload;
    debug_msg += "'";
    if (rs485Stack.getLastPacketHasChecksumError()) {
        debug_msg += " [Checksum Error]";
        totalChecksumErrors++; // Zählt nur hier, nicht überall
    }
    if (rs485Stack.getLastPacketHasHmacError()) {
        debug_msg += " [HMAC Error]";
        totalHmacErrors++; // Zählt nur hier, nicht überall
    }
    Serial.println(debug_msg);
    // Hier könnten wir das debug_msg auch an ein LVGL Text-Area-Objekt anhängen
    // lv_textarea_add_text(ta_debug_log, debug_msg.c_str());
    // lv_textarea_add_char(ta_debug_log, '\n');
  }
}

// --- NEUE LVGL UI Funktionen ---

// Funktion zum Erstellen aller LVGL-Bildschirme und Widgets
void create_lvgl_ui() {
    mainScreen = lv_obj_create(NULL); // Basis-Screen

    // --- Dashboard Screen ---
    dashboardScreen = lv_obj_create(mainScreen);
    lv_obj_set_size(dashboardScreen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(dashboardScreen, lv_color_hex(0x000000), LV_PART_MAIN); // Schwarz

    lv_obj_t * title_dash = lv_label_create(dashboardScreen);
    lv_label_set_text(title_dash, "RS485 Monitor Dashboard");
    lv_obj_set_style_text_color(title_dash, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_dash, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(title_dash, LV_ALIGN_TOP_MID, 0, 5);

    lbl_master_status = lv_label_create(dashboardScreen);
    lv_obj_set_style_text_color(lbl_master_status, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_master_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_recolor(lbl_master_status, true); // Ermöglicht Farbcodes im String
    lv_obj_align_to(lbl_master_status, title_dash, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_label_set_text(lbl_master_status, "Master: UNKNOWN");

    lbl_baudrate = lv_label_create(dashboardScreen);
    lv_obj_set_style_text_color(lbl_baudrate, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_baudrate, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(lbl_baudrate, lbl_master_status, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_label_set_text_fmt(lbl_baudrate, "Baudrate: %ld bps", rs485Stack.getBaudRate());

    lbl_key_id = lv_label_create(dashboardScreen);
    lv_obj_set_style_text_color(lbl_key_id, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_key_id, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_recolor(lbl_key_id, true);
    lv_obj_align_to(lbl_key_id, lbl_baudrate, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_label_set_text_fmt(lbl_key_id, "Key ID: #00ffff %d#", rs485Stack.getCurrentKeyId());

    lbl_packets_per_second = lv_label_create(dashboardScreen);
    lv_obj_set_style_text_color(lbl_packets_per_second, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_packets_per_second, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(lbl_packets_per_second, lbl_key_id, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    lbl_bytes_per_second = lv_label_create(dashboardScreen);
    lv_obj_set_style_text_color(lbl_bytes_per_second, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_bytes_per_second, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(lbl_bytes_per_second, lbl_packets_per_second, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lbl_checksum_errors = lv_label_create(dashboardScreen);
    lv_obj_set_style_text_color(lbl_checksum_errors, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_checksum_errors, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(lbl_checksum_errors, lbl_bytes_per_second, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lbl_hmac_errors = lv_label_create(dashboardScreen);
    lv_obj_set_style_text_color(lbl_hmac_errors, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_hmac_errors, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(lbl_hmac_errors, lbl_checksum_errors, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lbl_rogue_master = lv_label_create(dashboardScreen);
    lv_obj_set_style_text_color(lbl_rogue_master, lv_color_hex(0xFF0000), LV_PART_MAIN); // Rot für Warnung
    lv_obj_set_style_text_font(lbl_rogue_master, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_recolor(lbl_rogue_master, true);
    lv_obj_align_to(lbl_rogue_master, lbl_hmac_errors, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_label_set_text(lbl_rogue_master, ""); // Zuerst leer

    // --- Traffic Analysis Screen (Platzhalter) ---
    trafficScreen = lv_obj_create(mainScreen);
    lv_obj_set_size(trafficScreen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(trafficScreen, lv_color_hex(0x000000), LV_PART_MAIN); // Schwarz
    lv_obj_add_flag(trafficScreen, LV_OBJ_FLAG_HIDDEN); // Zuerst versteckt

    lv_obj_t * title_traffic = lv_label_create(trafficScreen);
    lv_label_set_text(title_traffic, "Traffic Analysis");
    lv_obj_set_style_text_color(title_traffic, lv_color_hex(0x00FFFF), LV_PART_MAIN); // Cyan
    lv_obj_set_style_text_font(title_traffic, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(title_traffic, LV_ALIGN_TOP_MID, 0, 5);

    lbl_traffic_total_packets = lv_label_create(trafficScreen);
    lv_obj_set_style_text_color(lbl_traffic_total_packets, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_traffic_total_packets, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(lbl_traffic_total_packets, title_traffic, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    lbl_traffic_current_pkps = lv_label_create(trafficScreen);
    lv_obj_set_style_text_color(lbl_traffic_current_pkps, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_traffic_current_pkps, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(lbl_traffic_current_pkps, lbl_traffic_total_packets, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lbl_traffic_current_byps = lv_label_create(trafficScreen);
    lv_obj_set_style_text_color(lbl_traffic_current_byps, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_traffic_current_byps, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(lbl_traffic_current_byps, lbl_traffic_current_pkps, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    // --- Debug Trace Screen (Platzhalter) ---
    debugScreen = lv_obj_create(mainScreen);
    lv_obj_set_size(debugScreen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(debugScreen, lv_color_hex(0x000000), LV_PART_MAIN); // Schwarz
    lv_obj_add_flag(debugScreen, LV_OBJ_FLAG_HIDDEN); // Zuerst versteckt

    lv_obj_t * title_debug = lv_label_create(debugScreen);
    lv_label_set_text(title_debug, "Debug Trace (Serial Only)");
    lv_obj_set_style_text_color(title_debug, lv_color_hex(0xFFFF00), LV_PART_MAIN); // Gelb
    lv_obj_set_style_text_font(title_debug, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(title_debug, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t * debug_info_label = lv_label_create(debugScreen);
    lv_label_set_text(debug_info_label, "Please use Serial Monitor for full debug output.");
    lv_obj_set_style_text_color(debug_info_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(debug_info_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(debug_info_label, title_debug, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    // Initialisiere mainScreen als aktiven Bildschirm
    lv_disp_load_scr(mainScreen);
}

// Funktion zum Aktualisieren des Dashboards mit aktuellen Metriken
void update_dashboard_ui() {
    lv_label_set_text_fmt(lbl_master_status, "Master: #%s %s#", masterIsPresent ? "00ff00" : "ff0000", masterIsPresent ? "ONLINE" : "OFFLINE");
    lv_label_set_text_fmt(lbl_baudrate, "Baudrate: %ld bps", rs485Stack.getBaudRate());
    lv_label_set_text_fmt(lbl_packets_per_second, "Pkts/s: %lu", packetsPerSecond);
    lv_label_set_text_fmt(lbl_bytes_per_second, "Bytes/s: %lu", bytesPerSecond);
    lv_label_set_text_fmt(lbl_checksum_errors, "ChkErr: #%s %lu#", totalChecksumErrors > 0 ? "ffff00" : "ffffff", totalChecksumErrors); // Gelb bei Fehlern
    lv_label_set_text_fmt(lbl_hmac_errors, "HMACErr: #%s %lu#", totalHmacErrors > 0 ? "ff0000" : "ffffff", totalHmacErrors); // Rot bei Fehlern
    lv_label_set_text_fmt(lbl_key_id, "Key ID: #00ffff %d#", rs485Stack.getCurrentKeyId());

    if (unexpectedMasterHeartbeat) {
        lv_label_set_text_fmt(lbl_rogue_master, "ROGUE MASTER: #ff0000 %d#", unexpectedMasterAddress);
    } else {
        lv_label_set_text(lbl_rogue_master, ""); // Leeren, wenn kein Rogue Master
    }
}

// Funktion zum Aktualisieren der Traffic Analysis UI
void update_traffic_ui() {
    lv_label_set_text_fmt(lbl_traffic_total_packets, "Total Pkts: %lu", totalPacketsReceived);
    lv_label_set_text_fmt(lbl_traffic_current_pkps, "Current Pk/s: %lu", packetsPerSecond);
    lv_label_set_text_fmt(lbl_traffic_current_byps, "Current By/s: %lu", bytesPerSecond);
    // Hier könnten später Charts oder detailliertere Listen eingebunden werden
}

// Funktion zum Umschalten zwischen den LVGL Bildschirmen
void set_monitor_mode(MonitorMode mode) {
    currentMonitorMode = mode;
    lv_obj_add_flag(dashboardScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(trafficScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(debugScreen, LV_OBJ_FLAG_HIDDEN); // Diesen Screen können wir auch als Info-Screen nutzen

    switch (currentMonitorMode) {
        case MODE_SIMPLE_DASHBOARD:
            lv_obj_clear_flag(dashboardScreen, LV_OBJ_FLAG_HIDDEN);
            lv_disp_load_scr(dashboardScreen);
            update_dashboard_ui(); // Sofort aktualisieren
            break;
        case MODE_TRAFFIC_ANALYSIS:
            lv_obj_clear_flag(trafficScreen, LV_OBJ_FLAG_HIDDEN);
            lv_disp_load_scr(trafficScreen);
            update_traffic_ui(); // Sofort aktualisieren
            break;
        case MODE_DEBUG_TRACE:
            lv_obj_clear_flag(debugScreen, LV_OBJ_FLAG_HIDDEN);
            lv_disp_load_scr(debugScreen);
            // Keine automatische Aktualisierung hier, da der Fokus auf Serial liegt
            break;
    }
}

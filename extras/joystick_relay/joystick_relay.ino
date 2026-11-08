// ESP-01S Joystick Relay — receives serial from Pro Mini, sends UDP to ESP32
// Wiring:
//   ESP-01S RX ← Pro Mini TX(D1)
//   ESP-01S VCC → 3.3V
//   ESP-01S GND → GND
//   ESP-01S CH_PD/EN → 3.3V (pull high)
//   ESP-01S GPIO0 → float (HIGH at boot for normal run)
//
// WiFi credentials stored in EEPROM, or hardcoded for now.
// The ESP-01S joins the same WiFi as ESP32 and sends UDP to ESP32's IP.
// ESP32 IP is discovered via UDP broadcast on port 8888.

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

// WiFi config — will be set via serial command or hardcode
// Format: send "WIFI:ssid:pass\n" via serial to configure
// Or "TARGET:ip\n" to set ESP32 IP

#define UDP_PORT 8888
#define EEPROM_SIZE 256

WiFiUDP udp;
String targetIP = "";
bool connected = false;

struct Config {
  char ssid[64];
  char pass[64];
  char target[32];
  char magic[4]; // "JOY" + null
};

Config cfg;

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cfg);
  if (cfg.magic[0] != 'J' || cfg.magic[1] != 'O' || cfg.magic[2] != 'Y') {
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.ssid, "");          // 首次使用经串口 WIFI:ssid:pass 配置
    strcpy(cfg.pass, "");
    cfg.target[0] = 0;
    strcpy(cfg.magic, "JOY");
  }
}

void saveConfig() {
  strcpy(cfg.magic, "JOY");
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid, cfg.pass);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }
  connected = (WiFi.status() == WL_CONNECTED);
  if (connected) {
    udp.begin(UDP_PORT);
  }
}

void setup() {
  Serial.begin(115200);
  loadConfig();
  connectWiFi();
}

void loop() {
  // Check for config commands via serial
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("WIFI:")) {
      // Format: WIFI:ssid:pass
      int sep = line.indexOf(':', 5);
      if (sep > 5) {
        String s = line.substring(5, sep);
        String p = line.substring(sep + 1);
        s.toCharArray(cfg.ssid, 64);
        p.toCharArray(cfg.pass, 64);
        saveConfig();
        ESP.restart();
      }
    } else if (line.startsWith("TARGET:")) {
      // Format: TARGET:192.168.x.x
      String t = line.substring(7);
      t.toCharArray(cfg.target, 32);
      targetIP = t;
      saveConfig();
    } else if (connected && targetIP.length() > 0) {
      // Normal joystick data — forward via UDP
      udp.beginPacket(targetIP.c_str(), UDP_PORT);
      udp.print(line);
      udp.endPacket();
    }
  }

  // Also listen for broadcast from ESP32 announcing its IP
  if (connected) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buf[64];
      int len = udp.read(buf, 63);
      buf[len] = 0;
      if (strncmp(buf, "IMGSHOW:", 8) == 0) {
        targetIP = String(buf + 8);
        targetIP.toCharArray(cfg.target, 32);
        saveConfig();
      }
    }
  }

  // Reconnect if needed
  if (!connected && millis() % 10000 < 50) {
    connectWiFi();
  }
}

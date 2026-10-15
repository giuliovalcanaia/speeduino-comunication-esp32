#include "transport.h"
#include "config.h"

#if USE_WIFI
  #include <WiFi.h>

  static WiFiServer server(TS_TCP_PORT);
  static WiFiClient wclient;

  void transportInit() {
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    server.begin();
  }

  bool transportConnected() {
    if (!wclient || !wclient.connected()) {
      WiFiClient c = server.available();
      if (c) { wclient.stop(); wclient = c; }
    }
    return wclient && wclient.connected();
  }

  Stream& transportStream() { return wclient; }

#else
  #include "BluetoothSerial.h"

  static BluetoothSerial serialBT;

  void transportInit() {
    serialBT.begin(BT_NAME);
    serialBT.setPin(BT_PIN, 4);
  }

  bool transportConnected() { return serialBT.hasClient(); }

  Stream& transportStream() { return serialBT; }
#endif

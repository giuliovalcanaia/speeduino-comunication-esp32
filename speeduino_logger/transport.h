#pragma once

// Abstracao do transporte cliente <-> ESP32.
//   USE_WIFI = 1: cria AP "Speeduino" e ouve TCP na porta 2000 (TunerStudio)
//   USE_WIFI = 0: Bluetooth SPP com nome "Speeduino-ESP32" e PIN 1234

#include <Arduino.h>

// Inicializa o transporte (AP+server ou Bluetooth).
void transportInit();

// true se um cliente esta conectado. No modo Wi-Fi tambem aceita novas
// conexoes atraves de server.available().
bool transportConnected();

// Stream do cliente conectado (WiFiClient ou BluetoothSerial).
Stream& transportStream();

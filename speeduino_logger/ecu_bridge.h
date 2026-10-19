#pragma once

// Comunicacao com a ECU Speeduino (Serial2 do ESP32).
//
// O ESP32 fica no conector HC-06 da Speeduino, que e ligado a Serial0 (D0/D1)
// do Mega. O protocolo e o mesmo do TunerStudio/ECU Manager:
//   [tamanho 2B BE][payload][CRC32 4B]
//
// Modos:
//   - Cliente TCP conectado: bridge transparente + sniffer passivo das
//     respostas do comando 'r' (live data).
//   - Sem cliente: polling autonomo com 'r' 0x30 a POLL_HZ.
// Os frames de tempo real sao enfileirados para a task de SD.

#include <Arduino.h>

// Configura o buffer RX e inicia Serial2.
void ecuInit();

// Corpo da task de comunicacao (bridge / polling).
// Para ser usado com xTaskCreatePinnedToCore(..., 0).
void ecuTaskFn(void* parameter);

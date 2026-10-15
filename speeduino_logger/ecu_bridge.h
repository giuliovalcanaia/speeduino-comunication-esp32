#pragma once

// Comunicacao com a ECU Speeduino (Serial2):
//   - Sem cliente conectado: polling autonomo com o comando 'n' a POLL_HZ Hz
//   - Com cliente conectado: ponte transparente + sniffer das respostas de
//     dados em tempo real (comandos 'n' e 'r canId 0x30 off=0')
// Os frames de tempo real sao enfileirados para a task de SD.

#include <Arduino.h>

// Configura o buffer RX e inicia Serial2.
void ecuInit();

// Corpo da task de comunicacao (polling / bridge + sniffer).
// Para ser usado com xTaskCreatePinnedToCore(..., 0).
void ecuTaskFn(void* parameter);

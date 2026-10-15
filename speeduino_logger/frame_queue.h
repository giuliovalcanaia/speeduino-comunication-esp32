#pragma once

// Fila FreeRTOS de frames brutos recebidos da ECU.
// Produtor: task de comunicacao (ecu_bridge). Consumidor: task de SD.
// Desacopla a comunicacao com a ECU dos picos de latencia do cartao SD.

#include <Arduino.h>
#include "config.h"

struct LogFrame {
  uint32_t ms;                // millis() quando o frame chegou
  uint8_t  src;               // 0 = poll, 1 = sniff
  uint8_t  len;               // tamanho util do frame
  uint8_t  data[MAX_FRAME];   // dados brutos
};

// Cria a fila. Deve ser chamada antes de criar as tasks. false = falha fatal.
bool frameQueueInit();

// Coloca um frame bruto da ECU na fila. Se a fila estiver cheia, espera
// (back-pressure): o polling atrasa naturalmente quando o SD nao acompanha,
// preservando todos os frames em vez de descartar.
void enqueueFrame(uint8_t src, const uint8_t* p, uint8_t n);

// Espera ate `timeoutMs` por um frame. true = frame recebido.
bool dequeueFrame(LogFrame& frame, uint32_t timeoutMs);

// millis() do ultimo frame enfileirado (para o LED de status)
uint32_t frameQueueLastMs();

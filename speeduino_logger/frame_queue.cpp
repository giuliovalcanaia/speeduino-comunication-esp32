#include "frame_queue.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static QueueHandle_t queue = NULL;
static volatile uint32_t lastFrameMs = 0;

bool frameQueueInit() {
  queue = xQueueCreate(FRAME_QUEUE_SIZE, sizeof(LogFrame));
  return queue != NULL;
}

void enqueueFrame(uint8_t src, const uint8_t* p, uint8_t n) {
  if (n == 0 || n > MAX_FRAME) return;

  LogFrame frame;
  frame.ms  = millis();
  frame.src = src;
  frame.len = n;
  memcpy(frame.data, p, n);

  // Timeout curto: se a task de SD nao acompanhou, descarta o frame para
  // nunca bloquear ou atrasar a ponte ECU <-> cliente.
  if (xQueueSend(queue, &frame, pdMS_TO_TICKS(10)) == pdTRUE) {
    lastFrameMs = frame.ms;
  }
}

bool dequeueFrame(LogFrame& frame, uint32_t timeoutMs) {
  return xQueueReceive(queue, &frame, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

uint32_t frameQueueLastMs() { return lastFrameMs; }

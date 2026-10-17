#include "led_status.h"
#include "config.h"
#include "log_store.h"
#include "rtc_time.h"
#include "frame_queue.h"

void ledInit() { pinMode(LED_PIN, OUTPUT); }

void ledStep() {
  enum LedState { LED_PAUSE, LED_ON, LED_OFF };

  static uint32_t lastMs     = 0;
  static uint8_t  code       = 0;
  static uint8_t  blinkCount = 0;
  static LedState state      = LED_PAUSE;

  // Determina o codigo atual pela prioridade: SD -> RTC -> logando -> aguardando.
  uint8_t newCode;
  if (!logStoreIsOk()) {
    newCode = 1;                         // 1 piscada: erro no SD
  } else if (!rtcAvailable()) {
    newCode = 2;                         // 2 piscadas: RTC falhou
  } else if (millis() - frameQueueLastMs() < 1000) {
    newCode = 3;                         // 3 piscadas: logando normalmente
  } else {
    newCode = 4;                         // 4 piscadas: aguardando conexao/polling
  }

  // Se o codigo mudou, reinicia o padrao.
  if (newCode != code) {
    code       = newCode;
    blinkCount = 0;
    state      = LED_PAUSE;
    lastMs     = millis();
    digitalWrite(LED_PIN, LOW);
    return;
  }

  uint32_t now = millis();

  switch (state) {
    case LED_PAUSE:
      if (now - lastMs >= LED_CODE_PAUSE_MS) {
        state  = LED_ON;
        lastMs = now;
        digitalWrite(LED_PIN, HIGH);
      }
      break;

    case LED_ON:
      if (now - lastMs >= LED_BLINK_ON_MS) {
        state  = LED_OFF;
        lastMs = now;
        digitalWrite(LED_PIN, LOW);
      }
      break;

    case LED_OFF:
      if (now - lastMs >= LED_BLINK_OFF_MS) {
        blinkCount++;
        if (blinkCount >= code) {
          // Codigo completo: volta para pausa longa.
          blinkCount = 0;
          state      = LED_PAUSE;
          lastMs     = now;
        } else {
          // Proxima piscada do mesmo codigo.
          state  = LED_ON;
          lastMs = now;
          digitalWrite(LED_PIN, HIGH);
        }
      }
      break;
  }
}

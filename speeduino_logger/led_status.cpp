#include "led_status.h"
#include "config.h"
#include "log_store.h"
#include "rtc_time.h"
#include "frame_queue.h"

void ledInit() { pinMode(LED_PIN, OUTPUT); }

void ledStep() {
  static uint32_t t = 0;
  static bool s = false;
  uint32_t period;
  if (!logStoreIsOk()) {
    period = 100;                       // SD com erro: pisca rapido
  } else if (!rtcAvailable()) {
    period = 200;                       // RTC com erro: pisca bem rapido
  } else if (millis() - frameQueueLastMs() < 1000) {
    period = 500;                       // logando normalmente
  } else {
    period = 1500;                      // aguardando
  }
  if (millis() - t >= period) { t = millis(); s = !s; digitalWrite(LED_PIN, s); }
}

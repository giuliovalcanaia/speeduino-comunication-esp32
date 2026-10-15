#include "rtc_time.h"
#include "config.h"
#include <Wire.h>

static RTC_DS3231 rtc;
static bool ok = false;

bool rtcInit() {
  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);
  ok = rtc.begin();
  if (!ok) {
    Serial.println("RTC indisponivel (gravando em /NO_RTC)");
  } else if (rtc.lostPower()) {
    Serial.println("RTC perdeu a alimentacao. Ajuste a hora com set_rtc_time.ino");
  } else {
    char iso[24];
    formatDateTime(rtc.now(), iso, sizeof(iso));
    Serial.printf("RTC OK: %s\n", iso);
  }
  return ok;
}

bool rtcAvailable() { return ok; }

DateTime rtcNow() { return rtc.now(); }

void formatDateTime(const DateTime& dt, char* out, size_t len) {
  snprintf(out, len, "%04d-%02d-%02dT%02d:%02d:%02d",
           (int)dt.year(), (int)dt.month(), (int)dt.day(),
           (int)dt.hour(), (int)dt.minute(), (int)dt.second());
}

void dateFolderName(const DateTime& dt, char* out, size_t len) {
  snprintf(out, len, "/%04d-%02d-%02d",
           (int)dt.year(), (int)dt.month(), (int)dt.day());
}

void fileNameFromDateTime(const DateTime& dt, char* out, size_t len) {
  // Nome de arquivo no padrao ISO 8601 (dois-pontos substituidos por hifens)
  snprintf(out, len, "%04d-%02d-%02dT%02d-%02d-%02d.csv",
           (int)dt.year(), (int)dt.month(), (int)dt.day(),
           (int)dt.hour(), (int)dt.minute(), (int)dt.second());
}

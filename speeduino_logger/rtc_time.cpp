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

bool rtcSet(const DateTime& dt) {
  if (!ok) {
    Serial.println("rtcSet: RTC nao disponivel");
    return false;
  }
  rtc.adjust(dt);
  char iso[24];
  formatDateTime(dt, iso, sizeof(iso));
  Serial.printf("RTC ajustado: %s\n", iso);
  return true;
}

bool rtcSetFromISOString(const char* iso) {
  // Formato esperado: YYYY-MM-DDTHH:MM:SS
  if (iso == nullptr) return false;
  size_t len = strlen(iso);
  if (len < 19) return false;

  int year   = (iso[0]  - '0') * 1000 + (iso[1]  - '0') * 100 +
               (iso[2]  - '0') * 10   + (iso[3]  - '0');
  int month  = (iso[5]  - '0') * 10   + (iso[6]  - '0');
  int day    = (iso[8]  - '0') * 10   + (iso[9]  - '0');
  int hour   = (iso[11] - '0') * 10   + (iso[12] - '0');
  int minute = (iso[14] - '0') * 10   + (iso[15] - '0');
  int second = (iso[17] - '0') * 10   + (iso[18] - '0');

  if (iso[4]  != '-' || iso[7]  != '-' || iso[10] != 'T' ||
      iso[13] != ':' || iso[16] != ':') {
    return false;
  }
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) return false;
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;

  return rtcSet(DateTime(year, month, day, hour, minute, second));
}

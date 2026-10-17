#include "log_store.h"
#include "config.h"
#include "rtc_time.h"
#include "channels.h"
#include <SPI.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static File     logFile;
static bool     sdOk = false;
static uint32_t lastFlush = 0;
static uint32_t lastSdTry = 0;

static DateTime logStartDateTime;
static char     currentFolder[16]   = "";
static char     currentFileName[40] = "";

static SemaphoreHandle_t sdMutex = NULL;

void logStoreLock()   { xSemaphoreTake(sdMutex, portMAX_DELAY); }
void logStoreUnlock() { xSemaphoreGive(sdMutex); }

bool logStoreIsOk() { return sdOk; }

String logStoreCurrentPath() {
  char folder[16] = "";
  char file[40]   = "";
  logStoreLock();
  strncpy(folder, currentFolder, sizeof(folder) - 1);
  folder[sizeof(folder) - 1] = '\0';
  strncpy(file, currentFileName, sizeof(file) - 1);
  file[sizeof(file) - 1] = '\0';
  logStoreUnlock();
  if (folder[0] == '\0' || file[0] == '\0') return "";
  return String(folder) + "/" + String(file);
}

static uint64_t sdFreeBytes() {
  return SD.totalBytes() - SD.usedBytes();
}

static uint32_t sdFreeMB() {
  return (uint32_t)(sdFreeBytes() / (1024ULL * 1024ULL));
}

// Pastas de log: YYYY-MM-DD (10 chars) ou NO_RTC (fallback sem RTC)
static bool isLogFolderName(const char* name) {
  if (name[0] == '/') name++;
  if (strcmp(name, "NO_RTC") == 0) return true;

  size_t len = strlen(name);
  if (len != 10) return false;
  for (size_t i = 0; i < 10; i++) {
    if (i == 4 || i == 7) {
      if (name[i] != '-') return false;
    } else {
      if (name[i] < '0' || name[i] > '9') return false;
    }
  }
  return true;
}

static void removeFolderIfEmpty(const char* path) {
  File folder = SD.open(path);
  if (!folder || !folder.isDirectory()) return;
  File entry = folder.openNextFile();
  bool empty = (entry == false);
  if (entry) entry.close();
  folder.close();
  if (empty) {
    SD.rmdir(path);
  }
}

// Encontra o arquivo .csv mais antigo. Nomes ISO sao cronologicamente
// ordenaveis em ordem lexicografica; os datados sao removidos antes dos
// de /NO_RTC por causa da ordenacao ASCII ('/' < '2' < 'N').
static bool findOldestLog(char* outPath, size_t len) {
  File root = SD.open("/");
  if (!root) return false;

  bool found = false;
  char oldest[64] = "";

  File folder = root.openNextFile();
  while (folder) {
    if (folder.isDirectory()) {
      const char* folderName = folder.name();
      if (isLogFolderName(folderName)) {
        File f = folder.openNextFile();
        while (f) {
          if (!f.isDirectory()) {
            const char* n = f.name();
            size_t nlen = strlen(n);
            if (nlen > 4 && strcmp(n + nlen - 4, ".csv") == 0) {
              const char* fn = folderName;
              if (fn[0] == '/') fn++;
              char fullPath[64];
              snprintf(fullPath, sizeof(fullPath), "/%s/%s", fn, n);
              if (!found || strcmp(fullPath, oldest) < 0) {
                found = true;
                strncpy(oldest, fullPath, sizeof(oldest) - 1);
                oldest[sizeof(oldest) - 1] = '\0';
              }
            }
          }
          f = folder.openNextFile();
        }
      }
    }
    folder.close();
    folder = root.openNextFile();
  }
  root.close();

  if (found) {
    strncpy(outPath, oldest, len - 1);
    outPath[len - 1] = '\0';
  }
  return found;
}

// Remove logs antigos ate liberar SD_FREE_MARGIN_MB.
static bool ensureSpaceAvailable() {
  while (sdFreeMB() < SD_FREE_MARGIN_MB) {
    char oldest[64];
    if (!findOldestLog(oldest, sizeof(oldest))) {
      Serial.println("SD cheio e nenhum log antigo para remover");
      return false;
    }

    // Extrai o nome da pasta para verificar se ficara vazia depois.
    char folder[16] = "";
    const char* slash = strchr(oldest + 1, '/');
    if (slash) {
      size_t flen = slash - oldest;
      if (flen < sizeof(folder)) {
        strncpy(folder, oldest, flen);
        folder[flen] = '\0';
      }
    }

    Serial.printf("Removendo log antigo: %s\n", oldest);
    if (!SD.remove(oldest)) {
      Serial.printf("Falha ao remover %s\n", oldest);
      return false;
    }

    if (folder[0]) {
      removeFolderIfEmpty(folder);
    }
  }
  return true;
}

static bool openLog() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) return false;

  if (!ensureSpaceAvailable()) {
    Serial.println("SD sem espaco disponivel");
    return false;
  }

  DateTime now;
  char folder[16];
  char fileName[40];
  char fullPath[64];

  if (rtcAvailable()) {
    now = rtcNow();
    dateFolderName(now, folder, sizeof(folder));
    fileNameFromDateTime(now, fileName, sizeof(fileName));
  } else {
    // Fallback: pasta NO_RTC, nome baseado no tempo desde o boot
    strcpy(folder, "/NO_RTC");
    snprintf(fileName, sizeof(fileName), "BOOT_%010lu.csv", (unsigned long)millis());
  }

  if (!SD.exists(folder)) {
    if (!SD.mkdir(folder)) return false;
  }

  snprintf(fullPath, sizeof(fullPath), "%s/%s", folder, fileName);

  // FILE_WRITE abre em modo append. Se abrirmos o mesmo nome duas vezes no
  // mesmo segundo (ex.: retry rapido apos falha), o cabecalho seria
  // duplicado no meio do arquivo. Evitamos isso sufixando _2, _3...
  if (SD.exists(fullPath)) {
    size_t baseLen = strlen(fileName) - 4;   // sem ".csv"
    for (int i = 2; ; i++) {
      fileName[baseLen] = '\0';              // trunca na posicao do sufixo
      snprintf(fileName + baseLen, sizeof(fileName) - baseLen, "_%d.csv", i);
      snprintf(fullPath, sizeof(fullPath), "%s/%s", folder, fileName);
      if (!SD.exists(fullPath) || i >= 99) break;
    }
  }

  logFile = SD.open(fullPath, FILE_WRITE);
  if (!logFile) return false;

  logFile.print("datetime,time_s,src");
  for (size_t i = 0; i < NCH; i++) { logFile.print(','); logFile.print(CH[i].name); }
  logFile.print('\n');
  logFile.flush();

  if (rtcAvailable()) {
    logStartDateTime = now;
  }

  strncpy(currentFolder, folder, sizeof(currentFolder) - 1);
  currentFolder[sizeof(currentFolder) - 1] = '\0';
  strncpy(currentFileName, fileName, sizeof(currentFileName) - 1);
  currentFileName[sizeof(currentFileName) - 1] = '\0';

  Serial.printf("Log: %s\n", fullPath);
  return true;
}

bool logStoreInit() {
  sdMutex = xSemaphoreCreateMutex();
  if (sdMutex == NULL) {
    Serial.println("FATAL: nao foi possivel criar o mutex do SD");
    while (true) { delay(1000); }
  }

  sdOk = openLog();
  if (!sdOk) Serial.println("SD indisponivel (vai tentar de novo)");
  return sdOk;
}

void logStoreEnsureOpen() {
  if (sdOk) return;
  if (millis() - lastSdTry <= SD_REOPEN_MS) return;
  lastSdTry = millis();
  logStoreLock();
  sdOk = openLog();
  logStoreUnlock();
}

bool logStoreRotate() {
  logStoreLock();
  if (sdOk && logFile) {
    logFile.flush();
    logFile.close();
  }
  sdOk = openLog();
  logStoreUnlock();
  if (sdOk) {
    Serial.println("Log rotacionado via comando web");
  } else {
    Serial.println("Falha ao rotacionar log via comando web");
  }
  return sdOk;
}

void logStoreWriteFrame(const LogFrame& frame) {
  logStoreLock();

  if (!sdOk) {
    logStoreUnlock();
    return;
  }

  uint32_t t = frame.ms;

  DateTime now;
  bool haveRtcNow = false;
  if (rtcAvailable()) {
    now = rtcNow();
    haveRtcNow = true;
  }

  char line[720];
  size_t pos;

  if (haveRtcNow) {
    char dt[24];
    formatDateTime(now, dt, sizeof(dt));
    pos = snprintf(line, sizeof(line), "%s,%lu.%03lu,%s",
                   dt,
                   (unsigned long)(t / 1000), (unsigned long)(t % 1000),
                   frame.src == 0 ? "poll" : "sniff");
  } else {
    // Fallback: datetime fica vazio, time_s continua funcionando
    pos = snprintf(line, sizeof(line), ",%lu.%03lu,%s",
                   (unsigned long)(t / 1000), (unsigned long)(t % 1000),
                   frame.src == 0 ? "poll" : "sniff");
  }

  for (size_t i = 0; i < NCH && pos < sizeof(line) - 24; i++) {
    line[pos++] = ',';
    pos += chanFormatValue(CH[i], frame.data, frame.len, line + pos, sizeof(line) - pos);
  }
  line[pos++] = '\n';

  if (logFile.write((const uint8_t*)line, pos) != pos) {
    sdOk = false;               // cartao removido / erro: tenta reabrir depois
    logFile.close();
    logStoreUnlock();
    return;
  }
  if (t - lastFlush >= FLUSH_MS) { logFile.flush(); lastFlush = t; }

  // Verifica se precisa rotacionar o arquivo (tamanho ou mudanca de dia)
  bool rotate = (logFile.size() >= MAX_LOG_SIZE_BYTES);
  if (haveRtcNow) {
    if (now.year() != logStartDateTime.year() ||
        now.month() != logStartDateTime.month() ||
        now.day() != logStartDateTime.day()) {
      rotate = true;
    }
  }

  if (rotate) {
    logFile.flush();
    logFile.close();
    sdOk = openLog();
  }

  logStoreUnlock();
}

/*
  Speeduino logger com ESP32 (no lugar do HC-06)
  Baseado em: github.com/dpfeletronica/ESP32-no-lugar-do-HC-06

  MODOS
  -----
  1) Cliente conectado (celular / TunerStudio via Bluetooth SPP ou Wi-Fi TCP):
       - o ESP32 vira uma ponte transparente ECU <-> cliente
       - ao mesmo tempo "escuta" a conversa e grava no SD as respostas de dados em tempo real
  2) Nenhum cliente conectado:
       - o ESP32 pergunta sozinho à ECU (comando 'n') a POLL_HZ Hz e grava no SD

  SERVIDOR WEB DE LOGS (modo Wi-Fi)
  ---------------------------------
  Com USE_WIFI = 1, o ESP32 tambem disponibiliza uma pagina em http://192.168.4.1/
  para listar e baixar os arquivos CSV do cartao SD diretamente pelo navegador.

  ARQUITETURA FREERTOS
  --------------------
  A comunicacao com a ECU e a gravacao no SD rodam em tasks separadas,
  comunicando por uma fila (queue) de frames brutos. Isso evita que picos de
  latencia do cartao SD atrasem o polling da ECU.
  - ecuTask (Core 0): polling/bridge + sniffer. Envia frames para a fila.
  - sdTask  (Core 1): consome a fila, formata CSV e grava no SD.
  - loop()  (Core 1): LED e servidor web.

  TIMESTAMP E ORGANIZACAO DOS LOGS
  --------------------------------
  - Os logs usam a hora real de um RTC DS3231 via I2C.
  - Cada dia gera uma pasta: /YYYY-MM-DD/
  - Dentro da pasta, o arquivo eh nomeado com o momento de inicio da gravacao:
      YYYY-MM-DDTHH-MM-SS.csv
  - Cada arquivo tem no maximo MAX_LOG_SIZE_BYTES (10 MB). Ao atingir o limite,
    um novo arquivo eh criado.
  - Antes de criar um novo log, se o espaco livre no SD for menor que
    SD_FREE_MARGIN_MB (100 MB), o log mais antigo eh removido automaticamente.
  - Se o RTC falhar, o logger continua gravando na pasta fallback /NO_RTC/.

  ARQUIVO DE LOG - abre direto no MegaLogViewer / Excel
  -----------------------------------------------------
  Uma linha de cabecalho com os nomes dos canais e uma linha por amostra:
     datetime,time_s,src,secl,status1,...
     datetime = data/hora real do RTC (ISO 8601)
     time_s   = segundos desde que o ESP32 foi ligado
     src      = poll  -> veio do polling do ESP32
              = sniff -> veio da escuta da conversa cliente<->ECU

  Os offsets seguem a lista de dados em tempo real da wiki da Speeduino; confirme
  as escalas (principalmente PW, TPS e AFR) com o speeduino.ini da SUA versao e
  ajuste a tabela CH[] abaixo.

  LIGACAO (ESP32 classico, NAO serve S2/S3/C3 por causa do Bluetooth classico)
  ----------------------------------------------------------------------------
  Speeduino TX (5V) --[divisor 5V->3V3]--> GPIO16 (RX2)
  Speeduino RX      <------------------   GPIO17 (TX2)    (3V3 basta)
  GND <-> GND ; 5V da placa Speeduino (pino VCC do conector do HC-06) -> VIN do ESP32

  DS3231 RTC:
    VCC -> 3V3
    GND -> GND
    SDA -> GPIO21
    SCL -> GPIO22

  Modulo micro SD (3V3, sem conversor de nivel):
    3V3, GND, CS=GPIO5, MOSI=GPIO23, CLK=GPIO18, MISO=GPIO19

  COMPILACAO: placa "ESP32 Dev Module", Partition Scheme "Huge APP" (o BluetoothSerial eh grande).
*/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#define USE_WIFI 1   // 0 = Bluetooth SPP, 1 = Wi-Fi AP + servidor TCP (porta 2000)

#if USE_WIFI
  #include <WiFi.h>
  #include <WebServer.h>
  WiFiServer server(2000);
  WiFiClient wclient;
  WebServer webServer(80);
#else
  #include "BluetoothSerial.h"
  BluetoothSerial SerialBT;
#endif

// ----------------------------- CONFIGURACAO ------------------------------
static const uint32_t ECU_BAUD        = 115200;
static const int      ECU_RX_PIN      = 16;
static const int      ECU_TX_PIN      = 17;

static const int      SD_CS_PIN       = 5;
static const int      SD_SCK_PIN      = 18;
static const int      SD_MISO_PIN     = 19;
static const int      SD_MOSI_PIN     = 23;
static const uint32_t SD_SPI_HZ       = 10000000;

static const int      RTC_SDA_PIN     = 21;
static const int      RTC_SCL_PIN     = 22;

static const uint32_t POLL_HZ         = 30;                 // taxa do modo autonomo
static const uint32_t POLL_PERIOD_MS  = 1000 / POLL_HZ;
static const uint32_t FLUSH_MS        = 1000;               // flush no SD (corte de energia ao desligar o carro)

static const uint32_t MAX_LOG_SIZE_BYTES = 10UL * 1024 * 1024;  // 10 MB por arquivo
static const uint32_t SD_FREE_MARGIN_MB  = 100;                 // mantem 100 MB livres no SD

// Bytes que a ECU antepoe a resposta do comando 'r' (0x30):
//   0 = porta principal (protocolo do TunerStudio, sem cabecalho)
//   2 = porta secundaria (Serial3): responde 'r' + 0x30 antes dos dados
// VERIFICAR no seu hardware com SNIFF_DEBUG = 1.
static const uint8_t  R_REPLY_HEADER  = 0;

#define SNIFF_DEBUG 0   // 1 = imprime no USB o que o sniffer classificou

static const int      LED_PIN         = 2;
static const size_t   MAX_FRAME       = 250;

// --------------------------- FREERTOS / FILA -----------------------------
// Frame bruto recebido da ECU, colocado na fila pela task de comunicacao
// e consumido pela task de escrita no SD.
struct LogFrame {
  uint32_t ms;                // millis() quando o frame chegou
  uint8_t  src;               // 0 = poll, 1 = sniff
  uint8_t  len;               // tamanho util do frame
  uint8_t  data[MAX_FRAME];   // dados brutos
};

static const uint16_t FRAME_QUEUE_SIZE = 128;   // ~32 KB de RAM

static QueueHandle_t  frameQueue = NULL;
static SemaphoreHandle_t sdMutex = NULL;
static TaskHandle_t   ecuTaskHandle = NULL;
static TaskHandle_t   sdTaskHandle  = NULL;
// -------------------------------------------------------------------------

// ------------------------- CANAIS GRAVADOS NO CSV ------------------------
enum Typ : uint8_t { U8, I8, U16, I16 };
struct Chan {
  const char* name;
  uint8_t     off;     // posicao no pacote de tempo real
  Typ         typ;
  float       scale;
  int16_t     add;
  uint8_t     dec;     // casas decimais no CSV
};

static const Chan CH[] = {
  {"secl",         0, U8,  1,     0,   0},
  {"status1",      1, U8,  1,     0,   0},
  {"engine",       2, U8,  1,     0,   0},
  {"dwell_ms",     3, U8,  0.1f,  0,   1},
  {"MAP_kPa",      4, U16, 1,     0,   0},
  {"IAT_C",        6, U8,  1,   -40,   0},
  {"CLT_C",        7, U8,  1,   -40,   0},
  {"batt_V",       9, U8,  0.1f,  0,   1},
  {"O2_AFR",      10, U8,  0.1f,  0,   1},
  {"egoCorr_pct", 11, U8,  1,     0,   0},
  {"RPM",         14, U16, 1,     0,   0},
  {"gammaE_pct",  17, U8,  1,     0,   0},
  {"VE1_pct",     18, U8,  1,     0,   0},
  {"AFR_target",  19, U8,  0.1f,  0,   1},
  {"PW1_ms",      20, U16, 0.001f,0,   3},   // confirmar escala no .ini
  {"advance_deg", 23, I8,  1,     0,   0},
  {"TPS_pct",     24, U8,  0.5f,  0,   1},   // confirmar escala no .ini
  {"loops_per_s", 25, U16, 1,     0,   0},
  {"spark_bits",  31, U8,  1,     0,   0},
  {"ethanol_pct", 34, U8,  1,     0,   0},
  {"baro_kPa",    40, U8,  1,     0,   0},
  {"tpsADC",      73, U8,  1,     0,   0},
  {"errors",      74, U8,  1,     0,   0},
  {"vss",        100, U16, 1,     0,   0},
  {"gear",       102, U8,  1,     0,   0},
  {"fuelPress",  103, U8,  1,     0,   0},
  {"oilPress",   104, U8,  1,     0,   0},
};
static const size_t NCH = sizeof(CH) / sizeof(CH[0]);
// -------------------------------------------------------------------------

HardwareSerial& ECU = Serial2;

#if USE_WIFI
static Stream& link() { return wclient; }
static bool linkConnected() {
  if (!wclient || !wclient.connected()) {
    WiFiClient c = server.available();
    if (c) { wclient.stop(); wclient = c; }
  }
  return wclient && wclient.connected();
}
#else
static Stream& link() { return SerialBT; }
static bool linkConnected() { return SerialBT.hasClient(); }
#endif

// ------------------------------- RTC -------------------------------------
RTC_DS3231 rtc;
static bool rtcOk = false;

static void formatDateTime(const DateTime& dt, char* out, size_t len) {
  snprintf(out, len, "%04d-%02d-%02dT%02d:%02d:%02d",
           (int)dt.year(), (int)dt.month(), (int)dt.day(),
           (int)dt.hour(), (int)dt.minute(), (int)dt.second());
}

static void dateFolderName(const DateTime& dt, char* out, size_t len) {
  snprintf(out, len, "/%04d-%02d-%02d",
           (int)dt.year(), (int)dt.month(), (int)dt.day());
}

static void fileNameFromDateTime(const DateTime& dt, char* out, size_t len) {
  // Nome de arquivo no padrao ISO 8601 (dois-pontos substituidos por hifens)
  snprintf(out, len, "%04d-%02d-%02dT%02d-%02d-%02d.csv",
           (int)dt.year(), (int)dt.month(), (int)dt.day(),
           (int)dt.hour(), (int)dt.minute(), (int)dt.second());
}
// -------------------------------------------------------------------------

// ------------------------------- SD / LOG --------------------------------
static File     logFile;
static bool     sdOk = false;
static uint32_t lastFlush = 0;
static volatile uint32_t lastFrameMs = 0;
static uint32_t lastSdTry = 0;

static DateTime logStartDateTime;
static char     currentFolder[16]  = "";
static char     currentFileName[32] = "";

static uint64_t sdFreeBytes() {
  return SD.totalBytes() - SD.usedBytes();
}

static uint32_t sdFreeMB() {
  return (uint32_t)(sdFreeBytes() / (1024ULL * 1024ULL));
}

static bool isDateFolderName(const char* name) {
  // Formato esperado: YYYY-MM-DD (10 caracteres), com ou sem barra inicial
  if (name[0] == '/') name++;
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

// Encontra o arquivo de log .csv mais antigo dentro das pastas diarias.
// Retorna true se encontrou e preenche outPath com o caminho completo.
static bool findOldestLog(char* outPath, size_t len) {
  File root = SD.open("/");
  if (!root) return false;

  bool found = false;
  char oldest[64] = "";

  File folder = root.openNextFile();
  while (folder) {
    if (folder.isDirectory()) {
      const char* folderName = folder.name();
      if (isDateFolderName(folderName)) {
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
  char fileName[32];
  char fullPath[48];

  if (rtcOk) {
    now = rtc.now();
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

  logFile = SD.open(fullPath, FILE_WRITE);
  if (!logFile) return false;

  logFile.print("datetime,time_s,src");
  for (size_t i = 0; i < NCH; i++) { logFile.print(','); logFile.print(CH[i].name); }
  logFile.print('\n');
  logFile.flush();

  if (rtcOk) {
    logStartDateTime = now;
  }

  strncpy(currentFolder, folder, sizeof(currentFolder) - 1);
  currentFolder[sizeof(currentFolder) - 1] = '\0';
  strncpy(currentFileName, fileName, sizeof(currentFileName) - 1);
  currentFileName[sizeof(currentFileName) - 1] = '\0';

  Serial.printf("Log: %s\n", fullPath);
  return true;
}

// Coloca um frame bruto da ECU na fila para a task de SD gravar.
// Chamada pela task de comunicacao (ecuTask).
static void enqueueFrame(uint8_t src, const uint8_t* p, uint8_t n) {
  if (n == 0 || n > MAX_FRAME) return;

  LogFrame frame;
  frame.ms  = millis();
  frame.src = src;
  frame.len = n;
  memcpy(frame.data, p, n);

  // Envia para a fila. Se a fila estiver cheia, espera (portMAX_DELAY).
  // Isso faz o polling naturalmente atrasar quando o SD nao acompanha,
  // preservando todos os frames em vez de descartar.
  xQueueSend(frameQueue, &frame, portMAX_DELAY);

  lastFrameMs = frame.ms;
}

// Converte um frame em linha de CSV e grava no SD.
// Chamada pela task de escrita no SD (sdTask).
static void writeFrameToSd(const LogFrame& frame) {
  if (xSemaphoreTake(sdMutex, portMAX_DELAY) != pdTRUE) return;

  if (!sdOk) {
    xSemaphoreGive(sdMutex);
    return;
  }

  uint32_t t = frame.ms;

  DateTime now;
  bool haveRtcNow = false;
  if (rtcOk) {
    now = rtc.now();
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
    const Chan& c = CH[i];
    uint8_t size = (c.typ == U16 || c.typ == I16) ? 2 : 1;
    if (c.off + size > frame.len) { line[pos++] = ','; continue; }      // pacote curto: campo vazio

    int32_t raw;
    switch (c.typ) {
      case U8:  raw = frame.data[c.off]; break;
      case I8:  raw = (int8_t)frame.data[c.off]; break;
      case U16: raw = (uint16_t)(frame.data[c.off] | (frame.data[c.off + 1] << 8)); break;
      default:  raw = (int16_t)(frame.data[c.off] | (frame.data[c.off + 1] << 8)); break;
    }
    float v = raw * c.scale + c.add;
    pos += snprintf(line + pos, sizeof(line) - pos, ",%.*f", c.dec, v);
  }
  line[pos++] = '\n';

  if (logFile.write((const uint8_t*)line, pos) != pos) {
    sdOk = false;               // cartao removido / erro: tenta reabrir depois
    logFile.close();
    xSemaphoreGive(sdMutex);
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

  xSemaphoreGive(sdMutex);
}
// -------------------------------------------------------------------------

// --------------------------- SERVIDOR WEB DE LOGS ------------------------
// Disponivel apenas no modo Wi-Fi. Permite listar e baixar os arquivos CSV
// do cartao SD diretamente pelo navegador em http://192.168.4.1/
#if USE_WIFI

// Retorna o caminho do arquivo de log atualmente aberto, protegendo o acesso
// as variaveis compartilhadas com a task de SD.
static String currentLogPath() {
  char folder[16] = "";
  char file[32]   = "";
  if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
    strncpy(folder, currentFolder, sizeof(folder) - 1);
    folder[sizeof(folder) - 1] = '\0';
    strncpy(file, currentFileName, sizeof(file) - 1);
    file[sizeof(file) - 1] = '\0';
    xSemaphoreGive(sdMutex);
  }
  if (folder[0] == '\0' || file[0] == '\0') return "";
  return String(folder) + "/" + String(file);
}

static String htmlEscape(const String& s) {
  String r;
  r.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': r += "&amp;";  break;
      case '<': r += "&lt;";   break;
      case '>': r += "&gt;";   break;
      case '"': r += "&quot;"; break;
      default:  r += c;        break;
    }
  }
  return r;
}

static String urlEncode(const String& s) {
  String r;
  r.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
      r += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      r += buf;
    }
  }
  return r;
}

static String urlDecode(const String& s) {
  String r;
  r.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '%' && i + 2 < s.length()) {
      char hex[3] = {s[i + 1], s[i + 2], '\0'};
      r += (char)strtol(hex, NULL, 16);
      i += 2;
    } else if (c == '+') {
      r += ' ';
    } else {
      r += c;
    }
  }
  return r;
}

static bool isPathSafe(const String& path) {
  if (!path.startsWith("/")) return false;
  if (path.indexOf("..") >= 0) return false;
  return true;
}

static String fileNameFromPath(const String& path) {
  int idx = path.lastIndexOf('/');
  if (idx < 0) return path;
  return path.substring(idx + 1);
}

static void sendDirectoryListing() {
  String dir = urlDecode(webServer.arg("dir"));
  if (dir.length() == 0) dir = "/";
  if (!isPathSafe(dir)) {
    webServer.send(403, "text/plain", "Caminho invalido");
    return;
  }

  String html =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>Speeduino Logs</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#f5f5f5;}"
    "h1{color:#333;}"
    "table{width:100%;max-width:900px;background:#fff;border-collapse:collapse;box-shadow:0 2px 4px rgba(0,0,0,0.1);}"
    "th,td{padding:12px;text-align:left;border-bottom:1px solid #ddd;}"
    "th{background:#0066cc;color:#fff;}"
    "a{color:#0066cc;text-decoration:none;}"
    "a:hover{text-decoration:underline;}"
    ".up{font-weight:bold;}"
    ".current{color:#999;font-style:italic;}"
    "</style></head><body>"
    "<h1>Logs do Speeduino</h1>"
    "<p>Pasta: <strong>" + htmlEscape(dir) + "</strong></p>"
    "<table><tr><th>Nome</th><th>Tamanho</th><th>Acao</th></tr>";

  if (dir != "/") {
    String parent = dir.substring(0, dir.lastIndexOf('/'));
    if (parent.length() == 0) parent = "/";
    html += "<tr><td class=\"up\" colspan=\"3\"><a href=\"/?dir=" + urlEncode(parent) + "\">[..]</a></td></tr>";
  }

  File folder = SD.open(dir.c_str());
  if (!folder || !folder.isDirectory()) {
    webServer.send(404, "text/plain", "Pasta nao encontrada");
    return;
  }

  String currentPath = currentLogPath();
  File entry = folder.openNextFile();
  while (entry) {
    String entryName = entry.name();
    // entry.name() pode retornar caminho absoluto; isola o nome relativo
    String displayName = fileNameFromPath(entryName);
    String fullPath;
    if (dir == "/") fullPath = "/" + displayName;
    else            fullPath = dir + "/" + displayName;

    html += "<tr>";
    if (entry.isDirectory()) {
      html += "<td>" + htmlEscape(displayName) + "/</td>";
      html += "<td>-</td>";
      html += "<td><a href=\"/?dir=" + urlEncode(fullPath) + "\">Abrir</a></td>";
    } else {
      html += "<td>" + htmlEscape(displayName) + "</td>";
      html += "<td>" + String(entry.size()) + " bytes</td>";
      if (fullPath == currentPath) {
        html += "<td class=\"current\">gravando agora</td>";
      } else {
        html += "<td><a href=\"/download?file=" + urlEncode(fullPath) + "\">Download</a></td>";
      }
    }
    html += "</tr>";
    entry.close();
    entry = folder.openNextFile();
  }
  folder.close();

  html += "</table></body></html>";
  webServer.send(200, "text/html; charset=utf-8", html);
}

static void handleDownload() {
  String file = urlDecode(webServer.arg("file"));
  if (file.length() == 0) {
    webServer.send(400, "text/plain", "Parametro 'file' ausente");
    return;
  }
  if (!isPathSafe(file)) {
    webServer.send(403, "text/plain", "Caminho invalido");
    return;
  }

  // Nao permite baixar o arquivo que ainda esta aberto para escrita.
  String currentPath = currentLogPath();
  if (file == currentPath) {
    webServer.send(409, "text/plain", "Arquivo ainda sendo gravado. Aguarde a rotacao.");
    return;
  }

  File f = SD.open(file.c_str(), FILE_READ);
  if (!f) {
    webServer.send(404, "text/plain", "Arquivo nao encontrado");
    return;
  }

  String name = fileNameFromPath(file);
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  webServer.streamFile(f, "text/csv");
  f.close();
}

#endif
// -------------------------------------------------------------------------

// ---------------------------- MODO 2: POLLING ----------------------------
static uint8_t frame[MAX_FRAME + 4];

static bool readExact(uint8_t* b, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t t0 = millis();
  while (got < n) {
    int a = ECU.available();
    if (a > 0) {
      got += ECU.readBytes(b + got, min((size_t)a, n - got));
      t0 = millis();
    } else if (millis() - t0 > timeoutMs) {
      return false;
    }
  }
  return true;
}

// Envia 'n' e le: 'n' | tipo(0x32) | tamanho | dados[tamanho]
// O tamanho vem no cabecalho, entao funciona mesmo que a versao do firmware mude o pacote.
static bool pollOnce() {
  while (ECU.available()) ECU.read();
  ECU.write('n');
  uint8_t hdr[3];
  if (!readExact(hdr, 3, 25) || hdr[0] != 'n' || hdr[1] != 0x32) return false;
  uint8_t len = hdr[2];
  if (len == 0 || len > MAX_FRAME) return false;
  if (!readExact(frame, len, 25)) return false;
  enqueueFrame(0, frame, len);
  return true;
}

static void pollStep() {
  static uint32_t next = 0;
  uint32_t now = millis();
  if ((int32_t)(now - next) < 0) { delay(1); return; }
  next += POLL_PERIOD_MS;
  if ((int32_t)(now - next) > 0) next = now + POLL_PERIOD_MS;   // atrasou demais: ressincroniza
  pollOnce();
}

// ------------------------ MODO 1: PONTE + SNIFFER ------------------------
struct Pending {
  bool     active = false;
  char     kind = 0;        // 'n' ou 'r'
  uint16_t len = 0;         // so para 'r'
  uint16_t expect = 0;      // bytes totais esperados da resposta
  uint16_t got = 0;
  uint32_t t = 0;
};
static Pending pend;
static uint8_t rbuf[MAX_FRAME + 8];

static uint8_t  cbuf[16];
static uint8_t  clen = 0;
static uint32_t lastClientByteMs = 0;

// Classifica o que o cliente mandou. So interessam:
//   'n'                                  -> dados em tempo real (formato novo, auto-descritivo)
//   'r' canId 0x30 offLo offHi lenLo lenHi -> o que o TunerStudio usa (so offset 0)
static void onClientBytes(const uint8_t* p, size_t n) {
  uint32_t now = millis();
  if (now - lastClientByteMs > 5) clen = 0;      // lacuna entre comandos
  lastClientByteMs = now;

  for (size_t i = 0; i < n; i++) {
    if (clen < sizeof(cbuf)) cbuf[clen++] = p[i];

    if (cbuf[0] == 'n' && clen == 1) {
      pend = Pending(); pend.active = true; pend.kind = 'n'; pend.t = now;
      clen = 0;
    } else if (cbuf[0] == 'r') {
      if (clen >= 7) {
        uint16_t off = cbuf[3] | (cbuf[4] << 8);
        uint16_t len = cbuf[5] | (cbuf[6] << 8);
        pend = Pending();
        if (cbuf[2] == 0x30 && off == 0 && len > 0 && len <= MAX_FRAME) {
          pend.active = true; pend.kind = 'r'; pend.len = len;
          pend.expect = R_REPLY_HEADER + len; pend.t = now;
        }
        clen = 0;
      }
    } else {
      pend.active = false;       // qualquer outro comando: nao eh dado em tempo real
      clen = 0;
    }
  }
}

// Recebe os bytes que a ECU mandou ao cliente e remonta o quadro pendente.
static void onEcuBytes(const uint8_t* p, size_t n) {
  if (!pend.active) return;
  for (size_t i = 0; i < n && pend.active; i++) {
    if (pend.got < sizeof(rbuf)) rbuf[pend.got] = p[i];
    pend.got++;

    if (pend.kind == 'n' && pend.got == 3) {
      if (rbuf[0] != 'n' || rbuf[1] != 0x32 || rbuf[2] == 0) { pend.active = false; break; }
      pend.expect = 3 + rbuf[2];
    }
    if (pend.expect && pend.got == pend.expect) {
      if (pend.kind == 'n') {
        enqueueFrame(1, rbuf + 3, rbuf[2]);
#if SNIFF_DEBUG
        Serial.printf("sniff n len=%u\n", rbuf[2]);
#endif
      } else {
        enqueueFrame(1, rbuf + R_REPLY_HEADER, pend.len);
#if SNIFF_DEBUG
        Serial.printf("sniff r len=%u\n", pend.len);
#endif
      }
      pend.active = false;
    }
  }
}

static void bridgeStep() {
  static uint8_t buf[512];
  bool idle = true;

  int a = link().available();
  if (a > 0) {
    size_t n = link().readBytes(buf, min((size_t)a, sizeof(buf)));
    ECU.write(buf, n);                // a ponte nunca depende do sniffer
    onClientBytes(buf, n);
    idle = false;
  }
  a = ECU.available();
  if (a > 0) {
    size_t n = ECU.readBytes(buf, min((size_t)a, sizeof(buf)));
    link().write(buf, n);
    onEcuBytes(buf, n);
    idle = false;
  }
  if (pend.active && millis() - pend.t > 150) pend.active = false;   // resposta nao veio
  if (idle) delay(1);
}

// ------------------------------ TRANSICOES -------------------------------
static void drainEcu(uint32_t ms) {
  uint32_t t = millis();
  while (millis() - t < ms) { while (ECU.available()) ECU.read(); }
}

static void onModeChange(bool connected) {
  pend = Pending();
  clen = 0;
  drainEcu(50);                       // descarta resposta que estava em voo
  Serial.println(connected ? "Modo: ponte + sniffer" : "Modo: polling autonomo");
}

static void ledStep() {
  static uint32_t t = 0;
  static bool s = false;
  uint32_t period;
  if (!sdOk) {
    period = 100;                       // SD com erro: pisca rapido
  } else if (!rtcOk) {
    period = 200;                       // RTC com erro: pisca bem rapido
  } else if (millis() - lastFrameMs < 1000) {
    period = 500;                       // logando normalmente
  } else {
    period = 1500;                      // aguardando
  }
  if (millis() - t >= period) { t = millis(); s = !s; digitalWrite(LED_PIN, s); }
}

// ---------------------------- TAREFAS FREERTOS ---------------------------
// Task de comunicacao com a ECU (polling / bridge + sniffer).
// Roda no Core 0 para nao ser afetada pela escrita no SD.
static void ecuTask(void* parameter) {
  (void)parameter;
  static bool wasConnected = false;

  for (;;) {
    bool connected = linkConnected();
    if (connected != wasConnected) {
      wasConnected = connected;
      onModeChange(connected);
    }

    if (connected) {
      bridgeStep();
    } else {
      pollStep();
    }

    // Cede tempo para o sistema, mantendo a task responsiva.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// Task de escrita no SD. Consome frames da fila e grava no cartao.
// Roda no Core 1 junto com o loop()/web server, mas em task separada.
static void sdTask(void* parameter) {
  (void)parameter;

  for (;;) {
    // Se o SD falhou, tenta reabrir periodicamente.
    if (!sdOk && millis() - lastSdTry > 5000) {
      lastSdTry = millis();
      if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        sdOk = openLog();
        xSemaphoreGive(sdMutex);
      }
    }

    LogFrame frame;
    if (xQueueReceive(frameQueue, &frame, pdMS_TO_TICKS(100)) == pdTRUE) {
      writeFrameToSd(frame);
    }
  }
}
// -------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);
  rtcOk = rtc.begin();
  if (!rtcOk) {
    Serial.println("RTC indisponivel (gravando em /NO_RTC)");
  } else if (rtc.lostPower()) {
    Serial.println("RTC perdeu a alimentacao. Ajuste a hora com set_rtc_time.ino");
  } else {
    char iso[24];
    formatDateTime(rtc.now(), iso, sizeof(iso));
    Serial.printf("RTC OK: %s\n", iso);
  }

  ECU.setRxBufferSize(4096);          // aguenta uma escrita lenta no SD sem perder bytes
  ECU.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);

  sdOk = openLog();
  if (!sdOk) Serial.println("SD indisponivel (vai tentar de novo)");

#if USE_WIFI
  WiFi.softAP("Speeduino", "qK95xvFC");
  server.begin();
  webServer.on("/", HTTP_GET, sendDirectoryListing);
  webServer.on("/download", HTTP_GET, handleDownload);
  webServer.begin();
  Serial.println("Servidor web de logs em http://192.168.4.1/");
#else
  SerialBT.begin("Speeduino-ESP32");
  SerialBT.setPin("1234", 4);
#endif

  // FreeRTOS: fila, mutex e tasks
  frameQueue = xQueueCreate(FRAME_QUEUE_SIZE, sizeof(LogFrame));
  if (frameQueue == NULL) {
    Serial.println("FATAL: nao foi possivel criar a fila de frames");
    while (true) { delay(1000); }
  }

  sdMutex = xSemaphoreCreateMutex();
  if (sdMutex == NULL) {
    Serial.println("FATAL: nao foi possivel criar o mutex do SD");
    while (true) { delay(1000); }
  }

  xTaskCreatePinnedToCore(ecuTask, "ecuTask", 4096, NULL, 2, &ecuTaskHandle, 0);
  xTaskCreatePinnedToCore(sdTask,  "sdTask",  4096, NULL, 1, &sdTaskHandle,  1);

  Serial.println("Tasks FreeRTOS iniciadas.");
}

void loop() {
  ledStep();
#if USE_WIFI
  webServer.handleClient();
#endif
}

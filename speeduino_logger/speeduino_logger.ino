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

  ARQUIVO DE LOG (/log_NNNN.csv) - abre direto no MegaLogViewer / Excel
  ---------------------------------------------------------------------
  Uma linha de cabeçalho com os nomes dos canais e uma linha por amostra:
     time_s,src,secl,status1,...
     src = poll  -> veio do polling do ESP32
     src = sniff -> veio da escuta da conversa cliente<->ECU
  time_s = segundos desde que o ESP32 foi ligado.
  Os offsets seguem a lista de dados em tempo real da wiki da Speeduino; confirme as escalas
  (principalmente PW, TPS e AFR) com o speeduino.ini da SUA versão e ajuste a tabela CH[] abaixo.

  LIGAÇÃO (ESP32 clássico, NÃO serve S2/S3/C3 por causa do Bluetooth clássico)
  ---------------------------------------------------------------------------
  Speeduino TX (5V) --[divisor 5V->3V3]--> GPIO16 (RX2)
  Speeduino RX      <------------------   GPIO17 (TX2)    (3V3 basta)
  GND <-> GND ; 5V da placa Speeduino (pino VCC do conector do HC-06) -> VIN do ESP32
  Módulo micro SD (3V3, sem conversor de nível): 3V3, GND, CS=GPIO5, MOSI=GPIO23, CLK=GPIO18, MISO=GPIO19

  COMPILAÇÃO: placa "ESP32 Dev Module", Partition Scheme "Huge APP" (o BluetoothSerial é grande).
*/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#define USE_WIFI 1   // 0 = Bluetooth SPP, 1 = Wi-Fi AP + servidor TCP (porta 2000)

#if USE_WIFI
  #include <WiFi.h>
  WiFiServer server(2000);
  WiFiClient wclient;
#else
  #include "BluetoothSerial.h"
  BluetoothSerial SerialBT;
#endif

// ----------------------------- CONFIGURAÇÃO -----------------------------
static const uint32_t ECU_BAUD        = 115200;
static const int      ECU_RX_PIN      = 16;
static const int      ECU_TX_PIN      = 17;

static const int      SD_CS_PIN       = 5;
static const int      SD_SCK_PIN      = 18;
static const int      SD_MISO_PIN     = 19;
static const int      SD_MOSI_PIN     = 23;
static const uint32_t SD_SPI_HZ       = 10000000;

static const uint32_t POLL_HZ         = 30;                 // taxa do modo autônomo
static const uint32_t POLL_PERIOD_MS  = 1000 / POLL_HZ;
static const uint32_t FLUSH_MS        = 1000;               // flush no SD (corte de energia ao desligar o carro)

// Bytes que a ECU antepõe à resposta do comando 'r' (0x30):
//   0 = porta principal (protocolo do TunerStudio, sem cabeçalho)
//   2 = porta secundária (Serial3): responde 'r' + 0x30 antes dos dados
// VERIFICAR no seu hardware com SNIFF_DEBUG = 1.
static const uint8_t  R_REPLY_HEADER  = 0;

#define SNIFF_DEBUG 0   // 1 = imprime no USB o que o sniffer classificou

static const int      LED_PIN         = 2;
static const size_t   MAX_FRAME       = 250;
// -------------------------------------------------------------------------

// ------------------------- CANAIS GRAVADOS NO CSV ------------------------
enum Typ : uint8_t { U8, I8, U16, I16 };
struct Chan {
  const char* name;
  uint8_t     off;     // posição no pacote de tempo real
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

// ------------------------------- SD / LOG --------------------------------
static File     logFile;
static bool     sdOk = false;
static uint32_t lastFlush = 0;
static uint32_t lastFrameMs = 0;
static uint32_t lastSdTry = 0;

static bool openLog() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) return false;
  char name[24];
  for (int i = 1; i < 10000; i++) {
    snprintf(name, sizeof(name), "/log_%04d.csv", i);
    if (!SD.exists(name)) break;
  }
  logFile = SD.open(name, FILE_WRITE);
  if (!logFile) return false;

  logFile.print("time_s,src");
  for (size_t i = 0; i < NCH; i++) { logFile.print(','); logFile.print(CH[i].name); }
  logFile.print('\n');
  logFile.flush();
  Serial.printf("Log: %s\n", name);
  return true;
}

// Converte um pacote de tempo real em uma linha de CSV e grava no SD.
static void logFrame(uint8_t src, const uint8_t* p, uint8_t n) {
  lastFrameMs = millis();
  if (!sdOk) return;
  uint32_t t = lastFrameMs;

  char line[640];
  size_t pos = snprintf(line, sizeof(line), "%lu.%03lu,%s",
                        (unsigned long)(t / 1000), (unsigned long)(t % 1000),
                        src == 0 ? "poll" : "sniff");

  for (size_t i = 0; i < NCH && pos < sizeof(line) - 24; i++) {
    const Chan& c = CH[i];
    uint8_t size = (c.typ == U16 || c.typ == I16) ? 2 : 1;
    if (c.off + size > n) { line[pos++] = ','; continue; }      // pacote curto: campo vazio

    int32_t raw;
    switch (c.typ) {
      case U8:  raw = p[c.off]; break;
      case I8:  raw = (int8_t)p[c.off]; break;
      case U16: raw = (uint16_t)(p[c.off] | (p[c.off + 1] << 8)); break;
      default:  raw = (int16_t)(p[c.off] | (p[c.off + 1] << 8)); break;
    }
    float v = raw * c.scale + c.add;
    pos += snprintf(line + pos, sizeof(line) - pos, ",%.*f", c.dec, v);
  }
  line[pos++] = '\n';

  if (logFile.write((const uint8_t*)line, pos) != pos) {
    sdOk = false;               // cartão removido / erro: tenta reabrir depois
    logFile.close();
    return;
  }
  if (t - lastFlush >= FLUSH_MS) { logFile.flush(); lastFlush = t; }
}

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

// Envia 'n' e lê: 'n' | tipo(0x32) | tamanho | dados[tamanho]
// O tamanho vem no cabeçalho, então funciona mesmo que a versão do firmware mude o pacote.
static bool pollOnce() {
  while (ECU.available()) ECU.read();
  ECU.write('n');
  uint8_t hdr[3];
  if (!readExact(hdr, 3, 25) || hdr[0] != 'n' || hdr[1] != 0x32) return false;
  uint8_t len = hdr[2];
  if (len == 0 || len > MAX_FRAME) return false;
  if (!readExact(frame, len, 25)) return false;
  logFrame(0, frame, len);
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
  uint16_t len = 0;         // só para 'r'
  uint16_t expect = 0;      // bytes totais esperados da resposta
  uint16_t got = 0;
  uint32_t t = 0;
};
static Pending pend;
static uint8_t rbuf[MAX_FRAME + 8];

static uint8_t  cbuf[16];
static uint8_t  clen = 0;
static uint32_t lastClientByteMs = 0;

// Classifica o que o cliente mandou. Só interessam:
//   'n'                                  -> dados em tempo real (formato novo, auto-descritivo)
//   'r' canId 0x30 offLo offHi lenLo lenHi -> o que o TunerStudio usa (só offset 0)
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
      pend.active = false;       // qualquer outro comando: não é dado em tempo real
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
        logFrame(1, rbuf + 3, rbuf[2]);
#if SNIFF_DEBUG
        Serial.printf("sniff n len=%u\n", rbuf[2]);
#endif
      } else {
        logFrame(1, rbuf + R_REPLY_HEADER, pend.len);
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
  if (pend.active && millis() - pend.t > 150) pend.active = false;   // resposta não veio
  if (idle) delay(1);
}

// ------------------------------ TRANSIÇÕES -------------------------------
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
  uint32_t period = !sdOk ? 100 : ((millis() - lastFrameMs < 1000) ? 500 : 1500);
  if (millis() - t >= period) { t = millis(); s = !s; digitalWrite(LED_PIN, s); }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  ECU.setRxBufferSize(4096);          // aguenta uma escrita lenta no SD sem perder bytes
  ECU.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);

  sdOk = openLog();
  if (!sdOk) Serial.println("SD indisponivel (vai tentar de novo)");

#if USE_WIFI
  WiFi.softAP("SPEEDUINO-LOG", "speeduino");
  server.begin();
#else
  SerialBT.begin("Speeduino-ESP32");
  SerialBT.setPin("1234", 4);
#endif
}

void loop() {
  static bool wasConnected = false;
  bool connected = linkConnected();
  if (connected != wasConnected) { wasConnected = connected; onModeChange(connected); }

  if (!sdOk && millis() - lastSdTry > 5000) {
    lastSdTry = millis();
    sdOk = openLog();
  }

  if (connected) bridgeStep(); else pollStep();
  ledStep();
}

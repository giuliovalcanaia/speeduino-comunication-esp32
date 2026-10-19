#include "ecu_bridge.h"
#include "config.h"
#include "transport.h"
#include "frame_queue.h"
#include "log_store.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static HardwareSerial& ECU = Serial2;

void ecuInit() {
  ECU.setRxBufferSize(ECU_RX_BUF_BYTES);
  ECU.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);
}

// ---------------------------- MODO 2: POLLING ----------------------------
static uint8_t pollBuf[MAX_FRAME + 4];

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
// O tamanho vem no cabecalho, entao funciona mesmo que a versao do firmware
// mude o pacote.
static bool pollOnce() {
  while (ECU.available()) ECU.read();
  ECU.write('n');
  uint8_t hdr[3];
  if (!readExact(hdr, 3, 25) || hdr[0] != 'n' || hdr[1] != 0x32) return false;
  uint8_t len = hdr[2];
  if (len == 0 || len > MAX_FRAME) return false;
  if (!readExact(pollBuf, len, 25)) return false;
  enqueueFrame(0, pollBuf, len);
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
// O ECU Manager (e apps similares) encapsulam cada comando/resposta em:
//   [tamanho 2 bytes, big-endian] [payload N bytes] [CRC32 4 bytes]
// O sniffer precisa remover esse framing para reconhecer os comandos 'n'
// e 'r' e para extrair os dados em tempo real das respostas.

// Parser de framing ---------------------------------------------------------
struct FramingParser {
  enum State { SIZE_HI, SIZE_LO, PAYLOAD, CRC } state = SIZE_HI;
  uint16_t payloadLen = 0;
  uint16_t got = 0;
  uint8_t  crcCount = 0;
  uint8_t  payload[MAX_FRAME + 16];
};

static void framingReset(FramingParser& fp) {
  fp.state = FramingParser::SIZE_HI;
  fp.payloadLen = 0;
  fp.got = 0;
  fp.crcCount = 0;
}

// Alimenta um byte no parser. Retorna true quando um pacote completa;
// nesse caso, outPayload aponta para o payload e outLen e o tamanho.
static bool framingFeed(FramingParser& fp, uint8_t b,
                        uint8_t*& outPayload, uint16_t& outLen) {
  outPayload = nullptr;
  outLen = 0;
  switch (fp.state) {
    case FramingParser::SIZE_HI:
      fp.payloadLen = ((uint16_t)b) << 8;
      fp.state = FramingParser::SIZE_LO;
      break;
    case FramingParser::SIZE_LO:
      fp.payloadLen |= b;
      if (fp.payloadLen == 0 || fp.payloadLen > sizeof(fp.payload)) {
        framingReset(fp);
      } else {
        fp.got = 0;
        fp.state = FramingParser::PAYLOAD;
      }
      break;
    case FramingParser::PAYLOAD:
      fp.payload[fp.got++] = b;
      if (fp.got >= fp.payloadLen) {
        fp.state = FramingParser::CRC;
        fp.crcCount = 0;
      }
      break;
    case FramingParser::CRC:
      fp.crcCount++;
      if (fp.crcCount >= 4) {
        outPayload = fp.payload;
        outLen = fp.payloadLen;
        framingReset(fp);
        return true;
      }
      break;
  }
  return false;
}

static FramingParser cliFraming;
static FramingParser ecuFraming;

// Fila de comandos pendentes ------------------------------------------------
static const size_t PEND_QUEUE_SIZE = 4;

struct Pending {
  bool     active = false;
  char     kind = 0;        // 'n' ou 'r'
  uint16_t len = 0;         // so para 'r'
  uint32_t t = 0;
};

static Pending pendQueue[PEND_QUEUE_SIZE];

static void pendResetAll() {
  for (size_t i = 0; i < PEND_QUEUE_SIZE; i++) pendQueue[i] = Pending();
}

static int pendFindFree() {
  for (size_t i = 0; i < PEND_QUEUE_SIZE; i++) {
    if (!pendQueue[i].active) return (int)i;
  }
  return -1;
}

static int pendFindOldestActive() {
  int idx = -1;
  uint32_t oldest = 0;
  for (size_t i = 0; i < PEND_QUEUE_SIZE; i++) {
    if (pendQueue[i].active) {
      if (idx == -1 || (int32_t)(pendQueue[i].t - oldest) < 0) {
        idx = (int)i;
        oldest = pendQueue[i].t;
      }
    }
  }
  return idx;
}

static bool pendAdd(char kind, uint16_t len) {
  int idx = pendFindFree();
  if (idx < 0) {
#if SNIFF_DEBUG
    Serial.println("PEND queue full");
#endif
    return false;
  }
  pendQueue[idx] = Pending();
  pendQueue[idx].active = true;
  pendQueue[idx].kind = kind;
  pendQueue[idx].len = len;
  pendQueue[idx].t = millis();
  return true;
}

static void pendExpire() {
  uint32_t now = millis();
  for (size_t i = 0; i < PEND_QUEUE_SIZE; i++) {
    if (pendQueue[i].active && (now - pendQueue[i].t > 150)) {
#if SNIFF_DEBUG
      Serial.printf("PEND timeout kind=%c len=%u\n",
                    pendQueue[i].kind, pendQueue[i].len);
#endif
      pendQueue[i].active = false;
    }
  }
}

// Classifica o payload de um comando do cliente. So interessam:
//   'n'                                  -> dados em tempo real (formato novo, auto-descritivo)
//   'r' canId 0x30 offLo offHi lenLo lenHi -> o que o TunerStudio usa (so offset 0)
static void onClientPayload(const uint8_t* p, size_t n) {
  if (n == 0) return;

  if (p[0] == 'n' && n == 1) {
    if (pendAdd('n', 0)) {
      if (!logStoreIsArmed()) logStoreArm(true);
#if SNIFF_DEBUG
      Serial.println("RECOG n");
#endif
    }
  } else if (p[0] == 'r' && n >= 7) {
    uint16_t off = p[3] | (p[4] << 8);
    uint16_t len = p[5] | (p[6] << 8);
    if (p[2] == 0x30 && off == 0 && len > 0 && len <= MAX_FRAME) {
      if (pendAdd('r', len)) {
        if (!logStoreIsArmed()) logStoreArm(true);
#if SNIFF_DEBUG
        Serial.printf("RECOG r off=%u len=%u\n", off, len);
#endif
      }
    } else {
#if SNIFF_DEBUG
      Serial.printf("REJECT r can=0x%02X off=%u len=%u\n", p[2], off, len);
#endif
    }
  } else {
#if SNIFF_DEBUG
    Serial.printf("REJECT cmd=0x%02X len=%u\n", p[0], (unsigned)n);
#endif
  }
}

// Processa o payload de uma resposta da ECU, atribuindo ao comando mais antigo.
static void onEcuPayload(const uint8_t* p, size_t n) {
  int idx = pendFindOldestActive();
  if (idx < 0) return;
  Pending& pend = pendQueue[idx];

  if (pend.kind == 'n') {
    if (n >= 3 && p[0] == 'n' && p[1] == 0x32 && p[2] > 0 && p[2] <= MAX_FRAME && n >= 3 + p[2]) {
      enqueueFrame(1, p + 3, p[2]);
#if SNIFF_DEBUG
      Serial.printf("sniff n len=%u\n", p[2]);
#endif
      pend.active = false;
    } else {
#if SNIFF_DEBUG
      Serial.printf("PEND n header invalid len=%u\n", (unsigned)n);
#endif
      pend.active = false;
    }
  } else if (pend.kind == 'r') {
    if (n >= pend.len + R_REPLY_HEADER) {
      enqueueFrame(1, p + R_REPLY_HEADER, pend.len);
#if SNIFF_DEBUG
      Serial.printf("sniff r len=%u\n", pend.len);
#endif
      pend.active = false;
    } else {
#if SNIFF_DEBUG
      Serial.printf("PEND r short len=%u expected=%u\n", (unsigned)n, pend.len + R_REPLY_HEADER);
#endif
      // Nao descarta: pode ser que a resposta venha sem header e o tamanho
      // bata de outra forma. Mas para o ECU Manager o payload ja e o dado.
      pend.active = false;
    }
  }
}

#if SNIFF_DEBUG
static void printHex(const char* prefix, const uint8_t* p, size_t n) {
  Serial.print(prefix);
  for (size_t i = 0; i < n && i < 32; i++) {
    Serial.printf(" %02X", p[i]);
  }
  if (n > 32) Serial.print(" ...");
  Serial.println();
}
#endif

static void bridgeStep() {
  static uint8_t buf[512];
  bool idle = true;
  Stream& link = transportStream();

  int a = link.available();
  if (a > 0) {
    size_t n = link.readBytes(buf, min((size_t)a, sizeof(buf)));
    ECU.write(buf, n);                // a ponte nunca depende do sniffer
    for (size_t i = 0; i < n; i++) {
      uint8_t* payload;
      uint16_t plen;
      if (framingFeed(cliFraming, buf[i], payload, plen)) {
#if SNIFF_DEBUG
        printHex("CLI payload", payload, plen);
#endif
        onClientPayload(payload, plen);
      }
    }
    idle = false;
  }
  a = ECU.available();
  if (a > 0) {
    size_t n = ECU.readBytes(buf, min((size_t)a, sizeof(buf)));
    link.write(buf, n);
    for (size_t i = 0; i < n; i++) {
      uint8_t* payload;
      uint16_t plen;
      if (framingFeed(ecuFraming, buf[i], payload, plen)) {
#if SNIFF_DEBUG
        printHex("ECU payload", payload, plen);
#endif
        onEcuPayload(payload, plen);
      }
    }
    idle = false;
  }
  pendExpire();
  if (idle) delay(1);
}

// ------------------------------ TRANSICOES -------------------------------
static void drainEcu(uint32_t ms) {
  uint32_t t = millis();
  while (millis() - t < ms) { while (ECU.available()) ECU.read(); }
}

static void onModeChange(bool connected) {
  pendResetAll();
  framingReset(cliFraming);
  framingReset(ecuFraming);
  drainEcu(50);                       // descarta resposta que estava em voo
  if (connected) {
    logStoreArm(false);               // so cria arquivo no primeiro comando do cliente
    Serial.println("Modo: ponte + sniffer (aguardando primeiro comando)");
  } else {
    logStoreArm(true);                // fica pronto para gravar quando o cliente conectar
    Serial.println("Modo: aguardando cliente (polling autonomo desativado)");
  }
}

// --------------------------------- TASK ----------------------------------
void ecuTaskFn(void* parameter) {
  (void)parameter;
  // Inicia como true para forcar a transicao no primeiro loop e configurar
  // corretamente o estado de gravacao (armado no polling, desarmado no bridge).
  static bool wasConnected = true;

  for (;;) {
    bool connected = transportConnected();
    if (connected != wasConnected) {
      wasConnected = connected;
      onModeChange(connected);
    }

    if (connected) {
      bridgeStep();                       // ponte transparente + sniffer para SD
    } else {
      // Polling autonomo desativado: a ECU fica livre quando nenhum cliente
      // (TunerStudio) esta conectado. Isso evita que comandos 'n' periodicos
      // interfiram no funcionamento do Speeduino.
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Cede tempo para o sistema, mantendo a task responsiva.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

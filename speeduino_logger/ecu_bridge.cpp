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
  if (now - lastClientByteMs > CLIENT_CMD_GAP_MS) clen = 0;      // lacuna entre comandos
  lastClientByteMs = now;

  for (size_t i = 0; i < n; i++) {
    if (clen < sizeof(cbuf)) cbuf[clen++] = p[i];

    if (cbuf[0] == 'n' && clen == 1) {
      pend = Pending(); pend.active = true; pend.kind = 'n'; pend.t = now;
      clen = 0;
      if (!logStoreIsArmed()) logStoreArm(true);
    } else if (cbuf[0] == 'r') {
      if (clen >= 7) {
        uint16_t off = cbuf[3] | (cbuf[4] << 8);
        uint16_t len = cbuf[5] | (cbuf[6] << 8);
        pend = Pending();
        if (cbuf[2] == 0x30 && off == 0 && len > 0 && len <= MAX_FRAME) {
          pend.active = true; pend.kind = 'r'; pend.len = len;
          pend.expect = R_REPLY_HEADER + len; pend.t = now;
          if (!logStoreIsArmed()) logStoreArm(true);
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
  Stream& link = transportStream();

  int a = link.available();
  if (a > 0) {
    size_t n = link.readBytes(buf, min((size_t)a, sizeof(buf)));
    ECU.write(buf, n);                // a ponte nunca depende do sniffer
    onClientBytes(buf, n);
    idle = false;
  }
  a = ECU.available();
  if (a > 0) {
    size_t n = ECU.readBytes(buf, min((size_t)a, sizeof(buf)));
    link.write(buf, n);
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
  if (connected) {
    logStoreArm(false);               // so cria arquivo no primeiro comando do cliente
    Serial.println("Modo: ponte + sniffer (aguardando primeiro comando)");
  } else {
    logStoreArm(true);                // polling autonomo: grava no primeiro frame
    Serial.println("Modo: polling autonomo");
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

    if (connected) bridgeStep();
    else           pollStep();

    // Cede tempo para o sistema, mantendo a task responsiva.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

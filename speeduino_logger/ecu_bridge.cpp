#include "ecu_bridge.h"

#include "config.h"
#include "frame_queue.h"
#include "log_store.h"
#include "speeduino_protocol.h"
#include "transport.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static HardwareSerial& ECU = Serial2;

static constexpr uint32_t PEND_TIMEOUT_MS = 300;
static constexpr uint32_t FRAME_GAP_MS = 100;
static constexpr size_t PEND_QUEUE_SIZE = 16;

// ----------------------------- Modos --------------------------------------
enum class BridgeMode : uint8_t {
  Polling,
  Bridge,
};

static BridgeMode mode = BridgeMode::Polling;
static uint32_t modeChangedAt = 0;
static uint32_t nextPollAt = 0;

// ------------------------- Parsers e pendings -----------------------------
static speeduino::FrameParser cliParser;
static speeduino::FrameParser ecuParser;

struct Pending {
  bool active = false;
  uint16_t len = 0;
  uint32_t t = 0;
};

static Pending pendQueue[PEND_QUEUE_SIZE];

// Buffer estatico: a resposta de live data e lida fora da pilha da task.
static uint8_t responseBuffer[speeduino::MAX_PAYLOAD];

void ecuInit() {
  ECU.setRxBufferSize(ECU_RX_BUF_BYTES);
  ECU.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);
}

// ----------------------------- Helpers ------------------------------------
static void drainEcu(uint32_t ms) {
  uint32_t start = millis();
  while (static_cast<int32_t>(millis() - start) < static_cast<int32_t>(ms)) {
    while (ECU.available()) {
      ECU.read();
    }
    delay(1);
  }
}

static bool readExactDeadline(uint8_t* buffer, size_t length,
                              uint32_t start, uint32_t timeoutMs) {
  size_t got = 0;
  while (got < length) {
    const int available = ECU.available();
    if (available > 0) {
      size_t want = length - got;
      if (static_cast<size_t>(available) < want) {
        want = static_cast<size_t>(available);
      }
      got += ECU.readBytes(buffer + got, want);
    } else if (static_cast<int32_t>(millis() - start) >=
               static_cast<int32_t>(timeoutMs)) {
      return false;
    } else {
      delay(1);
    }
  }
  return true;
}

static bool readResponseFrame(uint8_t* payload, size_t capacity,
                              uint16_t& payloadLen) {
  payloadLen = 0;
  const uint32_t start = millis();

  uint8_t header[2];
  if (!readExactDeadline(header, sizeof(header), start, POLL_TIMEOUT_MS)) {
    return false;
  }

  payloadLen = static_cast<uint16_t>(
      (static_cast<uint16_t>(header[0]) << 8) | header[1]);
  if (payloadLen == 0 || payloadLen > capacity) {
    return false;
  }

  if (!readExactDeadline(payload, payloadLen, start, POLL_TIMEOUT_MS)) {
    return false;
  }

  uint8_t crcBytes[4];
  if (!readExactDeadline(crcBytes, sizeof(crcBytes), start, POLL_TIMEOUT_MS)) {
    return false;
  }

  const uint32_t expected =
      (static_cast<uint32_t>(crcBytes[0]) << 24) |
      (static_cast<uint32_t>(crcBytes[1]) << 16) |
      (static_cast<uint32_t>(crcBytes[2]) << 8) |
      static_cast<uint32_t>(crcBytes[3]);

  return speeduino::crc32(payload, payloadLen) == expected;
}

// ------------------------- Fila de commands 'r' ---------------------------
static void pendResetAll() {
  for (size_t i = 0; i < PEND_QUEUE_SIZE; ++i) {
    pendQueue[i] = Pending();
  }
}

static int pendFindFree() {
  for (size_t i = 0; i < PEND_QUEUE_SIZE; ++i) {
    if (!pendQueue[i].active) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static int pendFindOldestActive() {
  int index = -1;
  uint32_t oldest = 0;

  for (size_t i = 0; i < PEND_QUEUE_SIZE; ++i) {
    if (!pendQueue[i].active) {
      continue;
    }
    if (index == -1 ||
        static_cast<int32_t>(pendQueue[i].t - oldest) < 0) {
      index = static_cast<int>(i);
      oldest = pendQueue[i].t;
    }
  }
  return index;
}

static bool pendAdd(uint16_t len) {
  const int index = pendFindFree();
  if (index < 0) {
#if SNIFF_DEBUG
    Serial.println("PEND queue full");
#endif
    return false;
  }

  pendQueue[index] = Pending();
  pendQueue[index].active = true;
  pendQueue[index].len = len;
  pendQueue[index].t = millis();
  return true;
}

static void pendExpire(uint32_t now) {
  for (size_t i = 0; i < PEND_QUEUE_SIZE; ++i) {
    if (pendQueue[i].active &&
        static_cast<int32_t>(now - pendQueue[i].t) >
            static_cast<int32_t>(PEND_TIMEOUT_MS)) {
      pendQueue[i].active = false;
    }
  }
}

// --------------------------- Polling autonomo -----------------------------
static bool pollOnce() {
  while (ECU.available()) {
    ECU.read();
  }

  const uint8_t payload[7] = {
      speeduino::CMD_R,
      0x00,  // tsCanId
      speeduino::CMD_OUTPUT_CHANNELS,
      0x00,  // offset low
      0x00,  // offset high
      static_cast<uint8_t>(speeduino::OCH_BLOCK_SIZE & 0xFFu),
      static_cast<uint8_t>((speeduino::OCH_BLOCK_SIZE >> 8) & 0xFFu),
  };

  uint8_t frame[2 + sizeof(payload) + 4];
  const size_t frameLen =
      speeduino::buildFrame(frame, sizeof(frame), payload, sizeof(payload));
  if (frameLen == 0) {
    return false;
  }

  ECU.write(frame, frameLen);

  uint16_t payloadLen = 0;
  if (!readResponseFrame(responseBuffer, sizeof(responseBuffer), payloadLen)) {
#if SNIFF_DEBUG
    Serial.println("poll: resposta invalida");
#endif
    return false;
  }

  if (payloadLen != speeduino::OCH_BLOCK_SIZE + 1U ||
      responseBuffer[0] != speeduino::RC_OK) {
#if SNIFF_DEBUG
    Serial.printf("poll: payload inesperado len=%u rc=0x%02X\n",
                  payloadLen, responseBuffer[0]);
#endif
    return false;
  }

  enqueueFrame(FRAME_SRC_POLL, responseBuffer + 1,
               static_cast<uint8_t>(speeduino::OCH_BLOCK_SIZE));
  return true;
}

static void pollStep() {
  const uint32_t now = millis();

  // Deixa a Speeduino sair de um eventual receive/transmit em andamento.
  if (static_cast<int32_t>(now - modeChangedAt) <
      static_cast<int32_t>(MODE_SETTLE_MS)) {
    return;
  }

  if (static_cast<int32_t>(now - nextPollAt) < 0) {
    return;
  }

  nextPollAt += POLL_PERIOD_MS;
  if (static_cast<int32_t>(now - nextPollAt) > 0) {
    nextPollAt = now + POLL_PERIOD_MS;
  }

  (void)pollOnce();
}

// ------------------------ Bridge + sniffer passivo ------------------------
static void onClientFrame(const uint8_t* payload, uint16_t length) {
  if (length < 7 || payload[0] != speeduino::CMD_R) {
    return;
  }
  if (payload[2] != speeduino::CMD_OUTPUT_CHANNELS) {
    return;
  }

  const uint16_t offset =
      static_cast<uint16_t>(payload[3] | (payload[4] << 8));
  const uint16_t len =
      static_cast<uint16_t>(payload[5] | (payload[6] << 8));

  if (offset != 0 || len == 0 || len > speeduino::OCH_BLOCK_SIZE) {
    return;
  }

  if (pendAdd(len) && !logStoreIsArmed()) {
    logStoreArm(true);
  }

#if SNIFF_DEBUG
  Serial.printf("RECOG r off=%u len=%u\n", offset, len);
#endif
}

static void onEcuFrame(const uint8_t* payload, uint16_t length) {
  const int index = pendFindOldestActive();
  if (index < 0) {
    return;
  }

  Pending& pending = pendQueue[index];

  // So consome o pending quando a resposta realmente parece ser do 'r'.
  // Respostas de Q/S/C tem outros tamanhos e nao podem ser confundidas.
  if (length == pending.len + 1U && payload[0] == speeduino::RC_OK) {
    enqueueFrame(FRAME_SRC_SNIFF, payload + 1,
                 static_cast<uint8_t>(pending.len));
    pending.active = false;
#if SNIFF_DEBUG
    Serial.printf("sniff r len=%u\n", pending.len);
#endif
  }
}

static void bridgeStep() {
  static uint8_t buffer[512];
  bool idle = true;
  Stream& link = transportStream();
  uint32_t now = millis();

  int available = link.available();
  if (available > 0) {
    size_t count = static_cast<size_t>(available);
    if (count > sizeof(buffer)) {
      count = sizeof(buffer);
    }
    count = link.readBytes(buffer, count);
    ECU.write(buffer, count);

    for (size_t i = 0; i < count; ++i) {
      if (cliParser.feed(buffer[i], now) ==
          speeduino::FrameParser::Result::Complete) {
        onClientFrame(cliParser.payload(), cliParser.payloadLength());
      }
    }
    idle = false;
  }

  available = ECU.available();
  if (available > 0) {
    size_t count = static_cast<size_t>(available);
    if (count > sizeof(buffer)) {
      count = sizeof(buffer);
    }
    count = ECU.readBytes(buffer, count);
    link.write(buffer, count);

    for (size_t i = 0; i < count; ++i) {
      if (ecuParser.feed(buffer[i], now) ==
          speeduino::FrameParser::Result::Complete) {
        onEcuFrame(ecuParser.payload(), ecuParser.payloadLength());
      }
    }
    idle = false;
  }

  now = millis();
  cliParser.resetIfIdle(now, FRAME_GAP_MS);
  ecuParser.resetIfIdle(now, FRAME_GAP_MS);
  pendExpire(now);

  if (idle) {
    delay(1);
  }
}

// ------------------------------ Transicoes -------------------------------
static void resetProtocolState() {
  cliParser.reset();
  ecuParser.reset();
  pendResetAll();
}

static void enterBridge() {
  mode = BridgeMode::Bridge;
  modeChangedAt = millis();
  nextPollAt = modeChangedAt;

  resetProtocolState();
  drainEcu(50);
  logStoreArm(false);

  Serial.println("Modo: bridge + sniffer (aguardando 'r' do cliente)");
}

static void enterPolling() {
  mode = BridgeMode::Polling;
  modeChangedAt = millis();
  nextPollAt = modeChangedAt + MODE_SETTLE_MS;

  resetProtocolState();
  drainEcu(50);
  logStoreArm(false);
  logStoreArm(true);

  Serial.println("Modo: polling autonomo (10 Hz)");
}

// --------------------------------- Task ----------------------------------
void ecuTaskFn(void* parameter) {
  (void)parameter;

  bool connected = transportConnected();
  if (connected) {
    enterBridge();
  } else {
    enterPolling();
  }

  for (;;) {
    connected = transportConnected();
    if (connected != (mode == BridgeMode::Bridge)) {
      if (connected) {
        enterBridge();
      } else {
        enterPolling();
      }
    }

    if (mode == BridgeMode::Bridge) {
      bridgeStep();
    } else {
      pollStep();
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

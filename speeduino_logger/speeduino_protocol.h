#pragma once

#include <Arduino.h>

// Protocolo primario da Speeduino/TunerStudio/ECU Manager.
//
// Frame (ambos os sentidos):
//   [tamanho 2 bytes big-endian][payload N bytes][CRC32 4 bytes big-endian]
//
// O CRC cobre somente o payload.
// O payload de uma resposta de live data comeca com RC_OK (0x00), seguido dos
// 130 bytes de OutputChannels.

namespace speeduino {

constexpr uint16_t OCH_BLOCK_SIZE = 130;
constexpr uint8_t CMD_R = 'r';
constexpr uint8_t CMD_OUTPUT_CHANNELS = 0x30;
constexpr uint8_t RC_OK = 0x00;

// Maior payload que a Speeduino pode enviar/receber em builds com SD logging.
// Ver SERIAL_BUFFER_SIZE em comms.cpp (2048 + 3).
constexpr size_t MAX_PAYLOAD = 2048 + 3;

uint32_t crc32(const uint8_t* data, size_t len);

// Monta um frame completo em out. Retorna o total de bytes escritos ou 0 em erro.
size_t buildFrame(uint8_t* out, size_t outCapacity,
                  const uint8_t* payload, size_t payloadLen);

// Parser incremental de frames. Alimente byte a byte.
class FrameParser {
 public:
  enum class Result : uint8_t {
    NeedMore,
    Complete,
    Ignored,
  };

  void reset();
  void resetIfIdle(uint32_t nowMs, uint32_t gapMs = 50);

  Result feed(uint8_t byte, uint32_t nowMs);

  const uint8_t* payload() const { return payload_; }
  uint16_t payloadLength() const { return payloadLen_; }
  bool crcValid() const { return crcValid_; }

 private:
  enum class State : uint8_t { SizeHi, SizeLo, Payload, Crc };

  State state_ = State::SizeHi;
  uint16_t payloadLen_ = 0;
  uint16_t got_ = 0;
  uint8_t crcBytes_[4] = {0};
  uint8_t crcCount_ = 0;
  uint32_t crc_ = 0xFFFFFFFFu;
  bool crcValid_ = false;
  uint32_t lastByteMs_ = 0;
  uint8_t payload_[MAX_PAYLOAD] = {0};
};

}  // namespace speeduino

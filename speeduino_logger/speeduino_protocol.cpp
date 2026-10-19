#include "speeduino_protocol.h"

#include <cstring>

namespace speeduino {
namespace {

constexpr uint32_t CRC32_POLY = 0xEDB88320u;

uint32_t crcUpdateByte(uint32_t crc, uint8_t byte) {
  crc ^= byte;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    crc = (crc >> 1) ^ (CRC32_POLY & (0u - (crc & 1u)));
  }
  return crc;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc = crcUpdateByte(crc, data[i]);
  }
  return crc ^ 0xFFFFFFFFu;
}

size_t buildFrame(uint8_t* out, size_t outCapacity,
                  const uint8_t* payload, size_t payloadLen) {
  if (out == nullptr || payload == nullptr || payloadLen == 0 ||
      payloadLen > 0xFFFFu) {
    return 0;
  }

  const size_t total = 2 + payloadLen + 4;
  if (outCapacity < total) {
    return 0;
  }

  out[0] = static_cast<uint8_t>((payloadLen >> 8) & 0xFFu);
  out[1] = static_cast<uint8_t>(payloadLen & 0xFFu);
  memcpy(out + 2, payload, payloadLen);

  const uint32_t crc = crc32(payload, payloadLen);
  out[2 + payloadLen + 0] = static_cast<uint8_t>((crc >> 24) & 0xFFu);
  out[2 + payloadLen + 1] = static_cast<uint8_t>((crc >> 16) & 0xFFu);
  out[2 + payloadLen + 2] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
  out[2 + payloadLen + 3] = static_cast<uint8_t>(crc & 0xFFu);

  return total;
}

void FrameParser::reset() {
  state_ = State::SizeHi;
  payloadLen_ = 0;
  got_ = 0;
  crcCount_ = 0;
  crc_ = 0xFFFFFFFFu;
  crcValid_ = false;
  lastByteMs_ = 0;
}

void FrameParser::resetIfIdle(uint32_t nowMs, uint32_t gapMs) {
  if (state_ == State::SizeHi) {
    return;
  }
  if (static_cast<int32_t>(nowMs - lastByteMs_) > static_cast<int32_t>(gapMs)) {
    reset();
  }
}

FrameParser::Result FrameParser::feed(uint8_t byte, uint32_t nowMs) {
  lastByteMs_ = nowMs;

  switch (state_) {
    case State::SizeHi:
      // Handshake/comandos legados sao ASCII puro (F, C, Q, S, ...).
      // Um frame valido nunca comeca com um byte imprimivel, pois o tamanho
      // maximo usado pela Speeduino tem byte alto 0x00.
      if (byte >= 0x20u && byte <= 0x7Eu) {
        reset();
        return Result::Ignored;
      }
      payloadLen_ = static_cast<uint16_t>(byte) << 8;
      state_ = State::SizeLo;
      return Result::NeedMore;

    case State::SizeLo:
      payloadLen_ |= byte;
      if (payloadLen_ == 0 || payloadLen_ > MAX_PAYLOAD) {
        reset();
        return Result::Ignored;
      }
      got_ = 0;
      crc_ = 0xFFFFFFFFu;
      crcValid_ = false;
      state_ = State::Payload;
      return Result::NeedMore;

    case State::Payload:
      payload_[got_++] = byte;
      crc_ = crcUpdateByte(crc_, byte);
      if (got_ >= payloadLen_) {
        crcCount_ = 0;
        state_ = State::Crc;
      }
      return Result::NeedMore;

    case State::Crc:
      crcBytes_[crcCount_++] = byte;
      if (crcCount_ >= 4) {
        const uint32_t expected =
            (static_cast<uint32_t>(crcBytes_[0]) << 24) |
            (static_cast<uint32_t>(crcBytes_[1]) << 16) |
            (static_cast<uint32_t>(crcBytes_[2]) << 8) |
            static_cast<uint32_t>(crcBytes_[3]);
        const uint32_t actual = crc_ ^ 0xFFFFFFFFu;

        crcValid_ = (expected == actual);
        state_ = State::SizeHi;
        crcCount_ = 0;

        return crcValid_ ? Result::Complete : Result::Ignored;
      }
      return Result::NeedMore;
  }

  return Result::Ignored;
}

}  // namespace speeduino

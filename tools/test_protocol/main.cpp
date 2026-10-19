#include "speeduino_protocol.h"

#include <cstdio>
#include <cstring>

static int failures = 0;

#define CHECK(cond)                         \
  do {                                      \
    if (!(cond)) {                          \
      printf("FAIL: %s\n", #cond);          \
      ++failures;                           \
    }                                       \
  } while (0)

int main() {
  CHECK(speeduino::crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) ==
        0xCBF43926u);

  // Request de live data: r, tsCanId=0, 0x30, offset=0, len=130.
  const uint8_t request[7] = {0x72, 0x00, 0x30, 0x00, 0x00, 0x82, 0x00};
  const uint8_t expected[13] = {0x00, 0x07, 0x72, 0x00, 0x30, 0x00,
                                0x00, 0x82, 0x00, 0x4E, 0x0F, 0x5C, 0x33};

  uint8_t frame[13];
  const size_t frameLen =
      speeduino::buildFrame(frame, sizeof(frame), request, sizeof(request));
  CHECK(frameLen == sizeof(expected));
  CHECK(memcmp(frame, expected, sizeof(expected)) == 0);

  speeduino::FrameParser parser;
  bool complete = false;
  for (size_t i = 0; i < frameLen; ++i) {
    if (parser.feed(frame[i], 100 + i) ==
        speeduino::FrameParser::Result::Complete) {
      complete = true;
    }
  }
  CHECK(complete);
  CHECK(parser.payloadLength() == 7);
  CHECK(parser.crcValid());
  CHECK(memcmp(parser.payload(), request, 7) == 0);

  // Resposta simulada: RC_OK + 130 bytes de OutputChannels.
  uint8_t responsePayload[131];
  responsePayload[0] = 0x00;
  for (int i = 1; i < 131; ++i) {
    responsePayload[i] = static_cast<uint8_t>(i);
  }

  uint8_t responseFrame[2 + 131 + 4];
  const size_t responseLen = speeduino::buildFrame(
      responseFrame, sizeof(responseFrame), responsePayload,
      sizeof(responsePayload));
  CHECK(responseLen == sizeof(responseFrame));

  speeduino::FrameParser responseParser;
  complete = false;
  for (size_t i = 0; i < responseLen; ++i) {
    if (responseParser.feed(responseFrame[i], 1000 + i) ==
        speeduino::FrameParser::Result::Complete) {
      complete = true;
    }
  }
  CHECK(complete);
  CHECK(responseParser.payloadLength() == 131);
  CHECK(responseParser.crcValid());
  CHECK(responseParser.payload()[0] == 0x00);

  // CRC invalido deve ser descartado.
  responseFrame[responseLen - 1] ^= 0xFF;
  speeduino::FrameParser badParser;
  bool badComplete = false;
  for (size_t i = 0; i < responseLen; ++i) {
    if (badParser.feed(responseFrame[i], 2000 + i) ==
        speeduino::FrameParser::Result::Complete) {
      badComplete = true;
    }
  }
  CHECK(!badComplete);

  // Handshake legado em ASCII deve ser ignorado sem quebrar o parser.
  speeduino::FrameParser legacyParser;
  CHECK(legacyParser.feed('F', 3000) ==
        speeduino::FrameParser::Result::Ignored);
  CHECK(legacyParser.feed('0', 3001) ==
        speeduino::FrameParser::Result::Ignored);

  if (failures == 0) {
    printf("protocol tests OK\n");
  }
  return failures == 0 ? 0 : 1;
}

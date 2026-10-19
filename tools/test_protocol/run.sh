#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

g++ -std=c++17 -Wall -Wextra \
  -I tools/test_protocol \
  -I speeduino_logger \
  tools/test_protocol/main.cpp \
  speeduino_logger/speeduino_protocol.cpp \
  -o /tmp/speeduino_protocol_test

/tmp/speeduino_protocol_test

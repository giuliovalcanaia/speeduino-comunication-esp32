/*
  Speeduino logger com ESP32 (substitui o HC-06)
  Baseado em: github.com/dpfeletronica/ESP32-no-lugar-do-HC-06

  VERSAO MODULAR
  --------------
  Cada responsabilidade vive em seus proprios arquivos .h/.cpp na pasta do
  sketch, compilados automaticamente pelo Arduino IDE / arduino-cli:

    config.h          constantes e pinos (USE_WIFI fica aqui)
    transport.h/.cpp  Wi-Fi AP + TCP 2000  ou  Bluetooth SPP
    speeduino_protocol.h/.cpp framing [len][payload][CRC32]
    ecu_bridge.h/.cpp Serial2, polling 'r' framed e ponte/sniffer
    frame_queue.h/.cpp fila FreeRTOS de frames brutos
    rtc_time.h/.cpp   DS3231 e formatacao ISO 8601
    log_store.h/.cpp  SD, rotacao, limpeza e escrita CSV
    web_ui.h/.cpp     servidor HTTP de logs (USE_WIFI = 1)
    led_status.h/.cpp padroes do LED onboard

  Hardware: o ESP32 fica no conector HC-06 da Speeduino, ligado a Serial0
  (D0/D1) do Mega. Nao use USB e ESP32 ao mesmo tempo.

  Modos:
    - Cliente TCP conectado (TunerStudio/ECU Manager): ponte transparente
      ECU <-> cliente, sniffer passivo das respostas 'r' no SD.
    - Sem cliente TCP: polling autonomo com 'r' 0x30 framed a POLL_HZ.

  Compilacao: placa "ESP32 Dev Module", Partition Scheme "Huge APP".
*/

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "led_status.h"
#include "rtc_time.h"
#include "channels.h"
#include "frame_queue.h"
#include "log_store.h"
#include "transport.h"
#include "ecu_bridge.h"
#include "web_ui.h"

// Task de escrita no SD (Core 1): consome a fila e grava no cartao.
static void sdTask(void* parameter) {
  (void)parameter;

  for (;;) {
    logStoreEnsureOpen();

    LogFrame frame;
    if (dequeueFrame(frame, 100)) {
      logStoreWriteFrame(frame);
    }
  }
}

void setup() {
  Serial.begin(115200);

  ledInit();
  rtcInit();
  ecuInit();
  logStoreInit();
  transportInit();
  webUiInit();

  // A fila precisa existir antes de iniciar as tasks.
  if (!frameQueueInit()) {
    Serial.println("FATAL: nao foi possivel criar a fila de frames");
    while (true) { delay(1000); }
  }

  xTaskCreatePinnedToCore(ecuTaskFn, "ecuTask", ECU_TASK_STACK, NULL, ECU_TASK_PRIO, NULL, 0);
  xTaskCreatePinnedToCore(sdTask,    "sdTask",  SD_TASK_STACK,  NULL, SD_TASK_PRIO,  NULL, 1);

  Serial.println("Tasks FreeRTOS iniciadas.");
}

void loop() {
  ledStep();
  webUiHandle();
}

#pragma once

// ============================================================================
// Configuracao central do speeduino_logger.
// Todos os pinos, limites, credenciais e ajustes ficam aqui — nenhum modulo
// define constantes proprias.
// ============================================================================

#include <Arduino.h>

// 0 = Bluetooth SPP, 1 = Wi-Fi AP + servidor TCP (porta 2000) + web de logs
#define USE_WIFI 1

// ----------------------------- ECU (Serial2) -------------------------------
static const uint32_t ECU_BAUD         = 115200;
static const int      ECU_RX_PIN       = 16;
static const int      ECU_TX_PIN       = 17;
static const uint32_t ECU_RX_BUF_BYTES = 4096;  // aguenta uma escrita lenta no SD

// --------------------------------- SD (SPI) --------------------------------
static const int      SD_CS_PIN        = 5;
static const int      SD_SCK_PIN       = 18;
static const int      SD_MISO_PIN      = 19;
static const int      SD_MOSI_PIN      = 23;
static const uint32_t SD_SPI_HZ        = 10000000;

// ------------------------------- RTC (I2C) ---------------------------------
static const int      RTC_SDA_PIN      = 21;
static const int      RTC_SCL_PIN      = 22;

// ---------------------------- Polling / logging ----------------------------
static const uint32_t POLL_HZ            = 30;                  // taxa do modo autonomo
static const uint32_t POLL_PERIOD_MS     = 1000 / POLL_HZ;
static const uint32_t FLUSH_MS           = 1000;                // flush no SD (corte de energia ao desligar o carro)
static const uint32_t MAX_LOG_SIZE_BYTES = 10UL * 1024 * 1024;  // 10 MB por arquivo
static const uint32_t SD_FREE_MARGIN_MB  = 100;                 // mantem 100 MB livres no SD
static const uint32_t SD_REOPEN_MS       = 5000;                // intervalo para tentar reabrir o SD

// Bytes que a ECU antepoe a resposta do comando 'r' (0x30):
//   0 = porta principal (protocolo do TunerStudio, sem cabecalho)
//   2 = porta secundaria (Serial3): responde 'r' + 0x30 antes dos dados
// VERIFICAR no seu hardware com SNIFF_DEBUG = 1.
static const uint8_t  R_REPLY_HEADER     = 0;

#define SNIFF_DEBUG 0   // 1 = imprime no USB o que o sniffer classificou

// Lacuna maxima entre bytes de um mesmo comando do cliente (TunerStudio).
// Acima disso, o sniffer assume que um novo comando comecou.
static const uint32_t CLIENT_CMD_GAP_MS  = 25;

// --------------------------------- Geral -----------------------------------
static const int      LED_PIN            = 2;
static const size_t   MAX_FRAME          = 250;
static const uint16_t FRAME_QUEUE_SIZE   = 128;   // ~32 KB de RAM

// ------------------------------- Transporte --------------------------------
static const char*    WIFI_AP_SSID       = "Speeduino";
static const char*    WIFI_AP_PASS       = "qK95xvFC";
static const uint16_t TS_TCP_PORT        = 2000;  // TunerStudio (modo Wi-Fi)
static const char*    BT_NAME            = "Speeduino-ESP32";
static const char*    BT_PIN             = "1234";

// ------------------------------ Servidor web -------------------------------
static const size_t   WEB_CHUNK_BYTES    = 2048;  // chunk de download (mutex cedido entre chunks)

// ------------------------------ Tasks FreeRTOS -----------------------------
static const uint32_t ECU_TASK_STACK     = 4096;
static const uint32_t SD_TASK_STACK      = 6144;
static const uint32_t ECU_TASK_PRIO      = 2;
static const uint32_t SD_TASK_PRIO       = 1;

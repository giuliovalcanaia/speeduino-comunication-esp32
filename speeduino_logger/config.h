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
// O ESP32 fica no conector HC-06 da Speeduino, que e ligado a Serial0 (D0/D1)
// do Mega. O protocolo e o mesmo do TunerStudio (framed + CRC32).
static const uint32_t ECU_BAUD         = 115200;
static const int      ECU_RX_PIN       = 16;
static const int      ECU_TX_PIN       = 17;
static const uint32_t ECU_RX_BUF_BYTES = 4096;  // aguenta uma escrita lenta no SD

// --------------------------------- SD (SPI) --------------------------------
// Mapeamento para ESP32 de 30 pinos:
// - IO12 é evitado por causa do strapping de boot.
// - O modulo SD é alimentado em 5 V (VIN) porque possui regulador interno
//   5 V -> 3,3 V. Verifique se o seu modulo suporta isso antes de ligar.
static const int      SD_CS_PIN        = 25;
static const int      SD_SCK_PIN       = 26;
static const int      SD_MISO_PIN      = 14;
static const int      SD_MOSI_PIN      = 27;
static const uint32_t SD_SPI_HZ        = 10000000;

// ------------------------------- RTC (I2C) ---------------------------------
static const int      RTC_SDA_PIN      = 21;
static const int      RTC_SCL_PIN      = 19;  // remapeado para liberar GPIO22

// ---------------------------- Polling / logging ----------------------------
static const uint32_t POLL_HZ            = 10;                  // taxa do modo autonomo
static const uint32_t POLL_PERIOD_MS     = 1000 / POLL_HZ;
static const uint32_t POLL_TIMEOUT_MS    = 100;                 // timeout total de uma resposta 'r'
static const uint32_t MODE_SETTLE_MS     = 500;                 // espera > SERIAL_TIMEOUT (400 ms) da Speeduino
static const uint32_t FLUSH_MS           = 1000;                // flush no SD (corte de energia ao desligar o carro)
static const uint32_t MAX_LOG_SIZE_BYTES = 10UL * 1024 * 1024;  // 10 MB por arquivo
static const uint32_t SD_FREE_MARGIN_MB  = 100;                 // mantem 100 MB livres no SD
static const uint32_t SD_REOPEN_MS       = 5000;                // intervalo para tentar reabrir o SD

// O starter do bridge e o cliente TCP do TunerStudio/ECU Manager.
// false = usa apenas cliente TCP (recomendado).
// true  = considera qualquer estacao associada ao AP.
static const bool     BRIDGE_ON_ANY_WIFI_STATION = false;

// Resposta framed do protocolo primario:
//   [tamanho][RC_OK][dados...][CRC32]
// O sniffer precisa pular o byte RC_OK antes dos 130 bytes de live data.
static const uint8_t  R_REPLY_HEADER     = 1;

#define SNIFF_DEBUG 0   // 1 = imprime no USB o que o sniffer classificou

// Lacuna maxima entre bytes de um mesmo comando do cliente (TunerStudio).
// Acima disso, o sniffer assume que um novo comando comecou.
static const uint32_t CLIENT_CMD_GAP_MS  = 25;

// --------------------------------- Geral -----------------------------------
static const int      LED_PIN            = 2;
static const size_t   MAX_FRAME          = 250;
static const uint16_t FRAME_QUEUE_SIZE   = 128;   // ~32 KB de RAM

// --------------------------------- LED -------------------------------------
static const uint32_t LED_BLINK_ON_MS    = 150;   // tempo ligado em cada piscada
static const uint32_t LED_BLINK_OFF_MS   = 500;   // tempo apagado entre piscadas do mesmo codigo
static const uint32_t LED_CODE_PAUSE_MS  = 2000;  // pausa apagado entre repeticoes do codigo

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

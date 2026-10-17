#pragma once

// Wrapper do RTC DS3231 (I2C) e formatacao de datas no padrao ISO 8601.
// Se o RTC falhar no boot, o logger continua em modo fallback (/NO_RTC/).

#include <Arduino.h>
#include <RTClib.h>

// Inicializa o barramento I2C e o RTC. Imprime o status no Serial.
// Retorna true se o RTC foi detectado.
bool rtcInit();

// true = RTC presente (rtcNow() valida)
bool rtcAvailable();

// Hora atual do RTC. Chamar apenas se rtcAvailable().
DateTime rtcNow();

// "YYYY-MM-DDTHH:MM:SS"
void formatDateTime(const DateTime& dt, char* out, size_t len);

// Ajusta o RTC para a data/hora informada. Retorna true se o RTC estiver presente.
bool rtcSet(const DateTime& dt);

// Ajusta o RTC a partir de uma string ISO "YYYY-MM-DDTHH:MM:SS".
// Retorna true se o RTC estiver presente e a string for valida.
bool rtcSetFromISOString(const char* iso);

// "/YYYY/Mês/DD" (nome da pasta diaria, ex: "/2026/Outubro/17")
void dateFolderName(const DateTime& dt, char* out, size_t len);

// Nome do mes em portugues (1 = Janeiro, 12 = Dezembro).
const char* monthNameInPortuguese(int month);

// Converte nome do mes em portugues de volta para numero (1..12).
// Retorna -1 se nao reconhecer.
int monthNumberFromPortugueseName(const char* name);

// "YYYY-MM-DDTHH-MM-SS.csv" (':' viram '-' porque nao sao permitidos no FAT32)
void fileNameFromDateTime(const DateTime& dt, char* out, size_t len);

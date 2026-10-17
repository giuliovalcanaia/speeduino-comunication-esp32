#pragma once

// Indicacao do LED onboard por codigos de piscada.
// Cada codigo e repetido a cada LED_CODE_PAUSE_MS (padrao 2 s):
//   1 piscada  = erro no SD
//   2 piscadas = RTC falhou (fallback)
//   3 piscadas = logando normalmente
//   4 piscadas = aguardando conexao / polling
// Tempos configurados em config.h: LED_BLINK_ON_MS, LED_BLINK_OFF_MS,
// LED_CODE_PAUSE_MS.

void ledInit();
void ledStep();

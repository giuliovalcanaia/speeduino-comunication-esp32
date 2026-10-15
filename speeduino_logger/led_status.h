#pragma once

// Indicacao do LED onboard.
//   100 ms  = erro no SD
//   200 ms  = RTC falhou (fallback)
//   500 ms  = logando normalmente
//   1500 ms = aguardando conexao / polling

void ledInit();
void ledStep();

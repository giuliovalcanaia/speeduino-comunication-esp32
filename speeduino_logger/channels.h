#pragma once

// Tabela de canais da Speeduino gravados no CSV.
// Os offsets seguem a lista de dados em tempo real da wiki da Speeduino;
// confirme as escalas (principalmente PW, TPS e AFR) com o speeduino.ini
// da SUA versao de firmware e ajuste a tabela em channels.cpp.

#include <Arduino.h>

enum Typ : uint8_t { U8, I8, U16, I16 };

struct Chan {
  const char* name;
  uint8_t     off;     // posicao no pacote de tempo real
  Typ         typ;
  float       scale;
  int16_t     add;
  uint8_t     dec;     // casas decimais no CSV
};

extern const Chan   CH[];
extern const size_t NCH;

// Formata o valor de um canal a partir dos dados brutos do frame.
// Escreve em `out` (sem a virgula separadora) e retorna quantos caracteres
// foram escritos. Pacote curto demais para o canal: string vazia (campo
// vazio no CSV).
size_t chanFormatValue(const Chan& c, const uint8_t* data, uint8_t len,
                       char* out, size_t outLen);

#include "channels.h"

const Chan CH[] = {
  {"secl",         0, U8,  1,     0,   0},
  {"status1",      1, U8,  1,     0,   0},
  {"engine",       2, U8,  1,     0,   0},
  {"dwell_ms",     3, U8,  0.1f,  0,   1},
  {"MAP_kPa",      4, U16, 1,     0,   0},
  {"IAT_C",        6, U8,  1,   -40,   0},
  {"CLT_C",        7, U8,  1,   -40,   0},
  {"batt_V",       9, U8,  0.1f,  0,   1},
  {"O2_AFR",      10, U8,  0.1f,  0,   1},
  {"egoCorr_pct", 11, U8,  1,     0,   0},
  {"RPM",         14, U16, 1,     0,   0},
  {"gammaE_pct",  17, U8,  1,     0,   0},
  {"VE1_pct",     18, U8,  1,     0,   0},
  {"AFR_target",  19, U8,  0.1f,  0,   1},
  {"PW1_ms",      20, U16, 0.001f,0,   3},   // confirmar escala no .ini
  {"advance_deg", 23, I8,  1,     0,   0},
  {"TPS_pct",     24, U8,  0.5f,  0,   1},   // confirmar escala no .ini
  {"loops_per_s", 25, U16, 1,     0,   0},
  {"spark_bits",  31, U8,  1,     0,   0},
  {"ethanol_pct", 34, U8,  1,     0,   0},
  {"baro_kPa",    40, U8,  1,     0,   0},
  {"tpsADC",      73, U8,  1,     0,   0},
  {"errors",      74, U8,  1,     0,   0},
  {"vss",        100, U16, 1,     0,   0},
  {"gear",       102, U8,  1,     0,   0},
  {"fuelPress",  103, U8,  1,     0,   0},
  {"oilPress",   104, U8,  1,     0,   0},
};
const size_t NCH = sizeof(CH) / sizeof(CH[0]);

size_t chanFormatValue(const Chan& c, const uint8_t* data, uint8_t len,
                       char* out, size_t outLen) {
  uint8_t size = (c.typ == U16 || c.typ == I16) ? 2 : 1;
  if (c.off + size > len) {
    if (outLen > 0) out[0] = '\0';   // pacote curto: campo vazio
    return 0;
  }

  int32_t raw;
  switch (c.typ) {
    case U8:  raw = data[c.off]; break;
    case I8:  raw = (int8_t)data[c.off]; break;
    case U16: raw = (uint16_t)(data[c.off] | (data[c.off + 1] << 8)); break;
    default:  raw = (int16_t)(data[c.off] | (data[c.off + 1] << 8)); break;
  }
  float v = raw * c.scale + c.add;

  int n = snprintf(out, outLen, "%.*f", c.dec, v);
  if (n < 0) return 0;
  if ((size_t)n >= outLen) return outLen - 1;   // truncado (nao deve ocorrer)
  return (size_t)n;
}

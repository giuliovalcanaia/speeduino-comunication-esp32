#include "channels.h"

const Chan CH[] = {
  {"secl",         0, U8,  1,     0,   0},
  {"status1",      1, U8,  1,     0,   0},
  {"engine",       2, U8,  1,     0,   0},
  {"dwell_ms",    90, U16, 0.001f,0,   3},
  {"MAP_kPa",      4, U16, 1,     0,   0},
  {"IAT_C",        6, U8,  1,   -40,   0},
  {"CLT_C",        7, U8,  1,   -40,   0},
  {"batt_V",       9, U8,  0.1f,  0,   1},
  {"O2_AFR",      10, U8,  0.1f,  0,   1},
  {"egoCorr_pct", 11, U8,  1,     0,   0},
  {"RPM",         14, U16, 1,     0,   0},
  {"gammaE_pct",  17, U16, 1,     0,   0},
  {"VE1_pct",     19, U8,  1,     0,   0},
  {"AFR_target",  21, U8,  0.1f,  0,   1},
  {"PW1_ms",      76, U16, 0.001f,0,   3},
  {"advance_deg", 24, I8,  1,     0,   0},
  {"TPS_pct",     25, U8,  0.5f,  0,   1},
  {"loops_per_s", 26, U16, 1,     0,   0},
  {"spark_bits",  32, U8,  1,     0,   0},
  {"ethanol_pct", 35, U8,  1,     0,   0},
  {"baro_kPa",    41, U8,  1,     0,   0},
  {"tpsADC",      74, U8,  1,     0,   0},
  {"errors",      75, U8,  1,     0,   0},
  {"vss",        104, U16, 1,     0,   0},
  {"gear",       106, U8,  1,     0,   0},
  {"fuelPress",  107, U8,  1,     0,   0},
  {"oilPress",   108, U8,  1,     0,   0},
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

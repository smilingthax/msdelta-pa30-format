#pragma once

#include "dpatypes.h"

typedef struct _dpa_bitreader_t dpa_bitreader_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _dpa_huffdec_t {
  size_t size;

  unsigned char *lens; // (per symbol)
  uint32_t *firsts;    // (per bitlen w/ [0] = bitlen 1, ...)
  uint32_t *offsets;   // (per bitlen)
  uint32_t *symbols;   // (per symbol)
} dpa_huffdec_t;

// canonical huffman, up to 31 bit
// NOTE: initial *hd must be zero-initialized; lens[size]
int dpa_huffdec_from_lengths(dpa_huffdec_t *hd, size_t max_bits, const unsigned char *lens, size_t size);

// returns symbol, or -1
int dpa_huffdec_read(dpa_bitreader_t *br, const dpa_huffdec_t *hd);

void dpa_huffdec_free(dpa_huffdec_t *hd);

#ifdef __cplusplus
}
#endif

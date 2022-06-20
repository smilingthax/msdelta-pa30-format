#pragma once

#include "plzxtypes.h"
#include <stddef.h>

typedef struct _dpa_bitreader_t dpa_bitreader_t;

#ifdef __cplusplus
extern "C" {
#endif

struct _dpa_plzxblock_params {
  size_t start;
  unsigned char lens[DPA_NUM_PLZXLENS];
};

typedef struct {
  size_t blocks;
  struct _dpa_plzxblock_params params[0];
} dpa_plzxdec_t;

// must be free()'d (or NULL)
dpa_plzxdec_t *dpa_plzx_read_composite(dpa_bitreader_t *br);

#ifdef __cplusplus
}
#endif

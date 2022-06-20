#pragma once

#include "dpatypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _dpa_bitreader_t {
  dpa_span_t in;
  size_t pos;
  uint64_t value;
  size_t fill;
  unsigned char pad;
} dpa_bitreader_t;

// returns 0 on error
int dpa_bitreader_init(dpa_bitreader_t *br, const unsigned char *buf, size_t len);

//int dpa_bitreader_read(dpa_bitreader_t *br, size_t len, uint32_t *ret);
int dpa_bitreader_read64(dpa_bitreader_t *br, size_t len, uint64_t *ret);

int dpa_bitreader_read_number(dpa_bitreader_t *br, uint32_t *ret);
int dpa_bitreader_read_number64(dpa_bitreader_t *br, int64_t *ret);

// special coding for plzx match lengths > 255
int dpa_bitreader_read_number_8(dpa_bitreader_t *br, uint32_t *ret);

// the span points inside br->in.buf!
int dpa_bitreader_read_buffer(dpa_bitreader_t *br, dpa_span_t *ret);


// ensures at least 57 bit in br->value (if available)
static inline void _dpa_bitreader_fill(dpa_bitreader_t *br)
{
  while (br->fill <= 56 && br->pos < br->in.len) {
    br->value |= (uint64_t)br->in.buf[br->pos] << br->fill;
    br->fill += 8;
    br->pos++;
    if (br->pos == br->in.len) { // special case for very last byte
      br->fill -= br->pad;
    }
  }
}

static inline int dpa_bitreader_read_fast(dpa_bitreader_t *br, size_t len, uint32_t *ret)
{
  // assert(br);
  // assert(len <= 32);
  // assert(ret);
  if (!len) {
    *ret = 0;
    return 1;
  }

  _dpa_bitreader_fill(br);
  if (br->fill < len) {
    return 0;
  }

  *ret = br->value & (~0u >> (32 - len));

  br->value >>= len;
  br->fill -= len;

  return 1;
}

#ifdef __cplusplus
}
#endif

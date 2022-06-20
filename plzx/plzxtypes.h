#pragma once

#include <stdint.h>

#define DPA_NUM_PLZXLENS (0x258 + 0x100 + 0x10)

struct _dpa_plzx_match_t {
  union {
    unsigned char literal;  // length == 1, LITERAL
    int32_t delta;          // length >= 2, SRC
    unsigned char lru;      // length >= 2, LRU entry, 0..2
    uint32_t offset;        // length >= 2, DST, >= 1
  };
  uint32_t length;
  unsigned char type;
#define DPA_MATCHTYPE_LITERAL 0  // (iff length == 1)
#define DPA_MATCHTYPE_SRC 1      // also uses rift table -> .delta
#define DPA_MATCHTYPE_FULLSRC 2  // may span multiple rift blocks, .delta/.offset unused
#define DPA_MATCHTYPE_LRU 3
#define DPA_MATCHTYPE_DST 4      // i.e. already decoded part of stream
};


#pragma once

#include "bitreader/dpatypes.h"

#define DPA_MAX_HASH_SIZE  32

typedef struct {
  size_t HashSize;
  unsigned char HashValue[DPA_MAX_HASH_SIZE];
} dpa_hash_t;

typedef struct {
  int64_t FileTypeSet;
  int64_t FileType;
  int64_t Flags;
  size_t TargetSize;
  uint64_t TargetFileTime;
  uint32_t TargetHashAlgId;
  dpa_hash_t TargetHash;
} dpa_header_info_t;

typedef struct {
  dpa_span_t preproc;
  dpa_span_t patch;
  const unsigned char *end;
} dpa_extra_info_t;

#ifdef __cplusplus
extern "C" {
#endif

// ret_extra is only checked/returned when non-zero
int dpa_GetDeltaInfo(const dpa_span_t *input, dpa_header_info_t *ret, dpa_extra_info_t *ret_extra);

#ifdef __cplusplus
}
#endif


#include "getdeltainfo.h"
#include <stdio.h>  // TODO? fprintf
#include <string.h>   // memcmp, memcpy
#include "bitreader/bitreader.h"

static uint64_t read_uint64_LE(const unsigned char *buf)
{
  return (buf[0]) | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24) |
         ((uint64_t)((buf[4]) | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24)) << 32);
}

int dpa_GetDeltaInfo(const dpa_span_t *input, dpa_header_info_t *ret, dpa_extra_info_t *ret_extra)
{
  if (!input || !ret) {
    return 0;
  }

  if (input->len < 12) {
    return 0;
  }

  if (memcmp(input->buf, "PA30", 0) != 0) {
    if (memcmp(input->buf, "PA19", 0) == 0) {
      fprintf(stderr, "Not Implemented: fallback of PA19 files to legacy format\n");
    }
    fprintf(stderr, "Error: Expected PA30 signature\n"); // TODO? elsewhere / error code?
    return 0;
  }

  ret->TargetFileTime = read_uint64_LE(input->buf + 4);

  dpa_bitreader_t br;
  if (!dpa_bitreader_init(&br, input->buf + 12, input->len - 12)) {
    return 0;
  }

  if (!dpa_bitreader_read_number64(&br, &ret->FileTypeSet) ||
      !dpa_bitreader_read_number64(&br, &ret->FileType) ||
      !dpa_bitreader_read_number64(&br, &ret->Flags) ||
      !dpa_bitreader_read_number(&br, &ret->TargetSize) ||
      !dpa_bitreader_read_number(&br, &ret->TargetHashAlgId)) {
    return 0;
  }

  dpa_span_t hash;
  if (!dpa_bitreader_read_buffer(&br, &hash)) {
    return 0;
  }

  if (hash.len > DPA_MAX_HASH_SIZE) {
    return 0;
  }
  ret->TargetHash.HashSize = hash.len;
  memcpy(ret->TargetHash.HashValue, hash.buf, hash.len);

  if (ret_extra) {
    if (!dpa_bitreader_read_buffer(&br, &ret_extra->preproc)) {
      return 0;
    }
    if (!dpa_bitreader_read_buffer(&br, &ret_extra->patch)) {
      return 0;
    }
    ret_extra->end = br.in.buf + br.pos - br.fill / 8;
  }

  return 1;
}


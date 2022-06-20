#include "composite.h"
#include "huffdec.h"
#include "../bitreader/bitreader.h"
#include <stdlib.h>
#include <stdio.h>

dpa_plzxdec_t *dpa_plzx_read_composite(dpa_bitreader_t *br)
{
  dpa_plzxdec_t *ret;

  uint32_t is_default;
  if (!dpa_bitreader_read_fast(br, 1, &is_default)) {
    fprintf(stderr, "Error: unexpected EOF\n");
    return NULL;
  }

  if (is_default) { // -> single block with "default" lengths!
    ret = malloc(sizeof(*ret) + 1 * sizeof(*ret->params));
    if (!ret) {
      fprintf(stderr, "Error: malloc failed\n");
      return NULL;
    }

    ret->blocks = 1;
    ret->params[0].start = 0;
    _dpa_plzxlengths_default(0x10, ret->params[0].lens, 0x258);  // will not fail
    _dpa_plzxlengths_default(0x10, ret->params[0].lens + 0x258, 0x100);
    _dpa_plzxlengths_default(0x10, ret->params[0].lens + 0x358, 0x10);

    return ret;
  }

  uint32_t blocks;
  if (!dpa_bitreader_read_number(br, &blocks)) {  // 64bit does not make sense here...
    fprintf(stderr, "Error: unexpected EOF / too large\n");
    return NULL;
  }
  if (blocks < 1) {
    fprintf(stderr, "At least one block required\n");
    return NULL;
  }

  ret = malloc(sizeof(*ret) + blocks * sizeof(*ret->params));
  if (!ret) {
    fprintf(stderr, "Error: malloc failed\n");
    return NULL;
  }

  ret->blocks = blocks;

  int64_t start = 0;
  for (size_t i = 0; i < blocks; i++) {
    int64_t delta;
    if (!dpa_bitreader_read_number64(br, &delta) || delta < 0) {
      fprintf(stderr, "Error: unexpected EOF / too small\n");
      goto err;
    }
    start += delta;
    if (start > SIZE_MAX) { // TODO?
      fprintf(stderr, "Error: too large\n");
      goto err;
    }
    ret->params[i].start = start;
  }

  // setup huffman pre-tree used to compress lengths
  unsigned char pt_lens[39];
  for (size_t i = 0; i < 39; i++) {  // 0x27
    uint32_t val;
    if (!dpa_bitreader_read_fast(br, 4, &val)) {
      fprintf(stderr, "Error: unexpected EOF\n");
      goto err;
    }
    pt_lens[i] = val;
  }

  dpa_huffdec_t pt = {};   // must be zeroed!
  if (!dpa_huffdec_from_lengths(&pt, 15, pt_lens, sizeof(pt_lens)/sizeof(*pt_lens))) {
    fprintf(stderr, "Error: huffdec_from_lengths failed\n");
    goto err;
  }

  unsigned char lens0[DPA_NUM_PLZXLENS] = {};
  const unsigned char *prev = lens0;
  for (size_t i = 0; i < blocks; i++) {
    if (!_dpa_plzxlengths_read(br, &pt, prev, ret->params[i].lens)) {
      fprintf(stderr, "Error: dpa_plzxlengths_read failed\n");
      dpa_huffdec_free(&pt);
      goto err;
    }
    prev = ret->params[i].lens;
  }
  dpa_huffdec_free(&pt);

  return ret;
err:
  free(ret);
  return NULL;
}


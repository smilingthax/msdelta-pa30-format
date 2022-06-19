#include "huffman.h"
#include "bitreader.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// canonical huffman, up to 31 bit
int dpa_huffdec_from_lengths(dpa_huffdec_t *hd, size_t max_bits, const unsigned char *lens, size_t size)
{
  if (!hd ||
      max_bits > 31 ||
      !lens ||
      size > (1u << max_bits)) {
    return 0;
  }
  dpa_huffdec_free(hd); // just in case + memset(0)

  uint32_t counts[32] = {}; // max_bits <= 31
  uint32_t offs[32];

  for (size_t i = 0; i < size; i++) {
    if (lens[i] > max_bits) {   // (check before allocs...)
      return 0;
    }
    counts[lens[i]]++;
  }
  if (counts[0] == size) { // also catches size == 0
    // i.e. dpa_huffdec_read() cannot be used (always returns -1)
    return 0;   // TODO?
  }

  hd->size = size;

  hd->lens = malloc(size * sizeof(*hd->lens));
  hd->firsts = malloc(max_bits * sizeof(*hd->firsts));
  hd->offsets = malloc(max_bits * sizeof(*hd->offsets));  // size - sum(counts[0..i]) - firsts[i]
  hd->symbols = malloc(size * sizeof(*hd->symbols));

  if (!hd->lens || !hd->firsts || !hd->offsets || !hd->symbols) {
    dpa_huffdec_free(hd);
    return 0;
  }

  memcpy(hd->lens, lens, sizeof(*hd->lens) * size);

  // the number of codes using a particular bit length must not exceed the number of available bit patterns (below its prefix)
  int avail = 1; // (only for size > 0)
  for (size_t i = 1; i <= max_bits; i++) {
    avail <<= 1;
    avail -= counts[i];
    if (avail < 0) {
      fprintf(stderr, "Error: Bad huffman lengths\n");  // TODO!?
      dpa_huffdec_free(hd);
      return 0;
    }
  }
  if (avail != 0) {
    fprintf(stderr, "Error: Incomplete canonical huffman code is not allowed\n");  // TODO!?
    dpa_huffdec_free(hd);
    return 0;
  }

  size_t sum = 0;
  for (size_t i = max_bits; i > 0; i--) {
    hd->firsts[i - 1] = sum;  // 0 at the end of the array is required to stop decoding (all 0 input)!
//    sum = (sum + counts[i] + 1) >> 1;  // text book
    sum = (sum + counts[i]) >> 1;  // safe for complete codes
  }

  size_t off = counts[0];
  for (size_t i = 0; i < max_bits; i++) {
    off += counts[i + 1];
    offs[i] = size - off;  // or: reverse loop/fill
    hd->offsets[i] = offs[i] - hd->firsts[i];
  }

  for (size_t i = 0; i < size; i++) {
    const unsigned char len = hd->lens[i];
    if (len > 0) {
      hd->symbols[offs[len - 1]++] = i;
    }
  }

  return 1;
}

// returns symbol, or -1
int dpa_huffdec_read(dpa_bitreader_t *br, const dpa_huffdec_t *hd)
{
  if (!hd || !br || hd->size == 0) {
    return -1;
  }

  _dpa_bitreader_fill(br);
  size_t len = 0, v = 0;
  uint64_t value = br->value;
  while (1) {
    v |= value & 1;
    value >>= 1;
    if (v >= hd->firsts[len]) {
      if (br->fill < len + 1) {
        return -1;
      }
      br->value = value;
      br->fill -= len + 1;
      return hd->symbols[v + hd->offsets[len]];
    }
    v <<= 1;
    len++;
  }
}

void dpa_huffdec_free(dpa_huffdec_t *hd)
{
  if (!hd) {
    return;
  }

  free(hd->symbols);
  free(hd->offsets);
  free(hd->firsts);
  free(hd->lens);

  memset(hd, 0, sizeof(*hd));
}


#include "bitreader.h"
#include <string.h>  // memset

static inline unsigned int ctz32(const uint32_t val)
{
  // assert(val != 0);
  return __builtin_ctz(val);
}
/*
#else // MSVC ?
  DWORD ret;
  _BitScanForward(&ret, val);
  return ret;
#endif
*/

int dpa_bitreader_init(dpa_bitreader_t *br, const unsigned char *buf, size_t len)
{
  if (!br || len <= 0) {
    return 0;
  }

  memset(br, 0, sizeof(*br));
  br->in.buf = buf;
  br->in.len = len;

  uint32_t pad;
  dpa_bitreader_read_fast(br, 3, &pad); // (NOTE: initial br->pad == 0)
  // assert(...did not fail...); // because in.len > 0

  br->pad = pad;
  if (br->pos == br->in.len) { // last byte already in value!
    if (br->fill < pad) {
      return 0;
    }
    br->fill -= pad;
  }

  return 1;
}

int dpa_bitreader_read64(dpa_bitreader_t *br, size_t len, uint64_t *ret)
{
  if (!br || !ret || len > 64) {
    return 0;
  }

  if (!len) {
    *ret = 0;
    return 1;
  }

  _dpa_bitreader_fill(br);
  if (br->fill < len) {
    return 0;
  }
  *ret = br->value & (~(uint64_t)0 >> (64 - len));

  return 1;
}

int dpa_bitreader_read_number(dpa_bitreader_t *br, uint32_t *ret)
{
  if (!br || !ret) {
    return 0;
  }

  _dpa_bitreader_fill(br);
  unsigned int nibbles = ctz32(br->value | 0x100); // trick: prevent 0, max allowed zeros here is 8!
  if (nibbles >= 8) { // (NOTE: safe for br->fill not enough)
    return 0;
  }
  nibbles++;

  const size_t bits = 4 * nibbles; // (NOTE: at least 4)
  if (br->fill < nibbles + bits) {
    return 0;
  }

  *ret = (uint32_t)(br->value >> nibbles) & (~0u >> (32 - bits));
  br->value >>= nibbles + bits;
  br->fill -= nibbles + bits;

  return 1;
}

int dpa_bitreader_read_number64(dpa_bitreader_t *br, int64_t *ret)
{
  if (!br || !ret) {
    return 0;
  }

  _dpa_bitreader_fill(br);
  unsigned int nibbles = ctz32(br->value | 0x10000); // trick: prevent 0, max zeros here is 16!
  if (nibbles >= 16) { // (NOTE: safe for br->fill not enough)
    return 0;
  }
  nibbles++;

  if (br->fill < nibbles) {
    return 0;
  }
  br->value >>= nibbles;
  br->fill -= nibbles;

  const size_t bits = 4 * nibbles; // (NOTE: at least 4)
  if (br->fill < bits) {
    _dpa_bitreader_fill(br);
    if (br->fill < bits) {
      return 0;
    }
  }
  *ret = br->value & (~(uint64_t)0 >> (64 - bits));
  br->value >>= bits;
  br->fill -= bits;

  return 1;
}

// (may leave ret->len dirty on failure)
int dpa_bitreader_read_buffer(dpa_bitreader_t *br, dpa_span_t *ret)
{
  if (!br || !ret) {
    return 0;
  }

  if (!dpa_bitreader_read_number(br, &ret->len)) {
    return 0;
  }

  const size_t start = br->pos - br->fill / 8; // (round to next byte boundary)
  if (start + ret->len > br->in.len) {
    return 0;
  }
  br->pos = start + ret->len;
  br->value = 0;
  br->fill = 0;

  ret->buf = br->in.buf + start;

  return 1;
}


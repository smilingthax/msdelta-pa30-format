#include "huffdec.h"
#include "../bitreader/bitreader.h"
#include <string.h>

static inline unsigned int log2i(const uint32_t val)
{
  // assert(val != 0);
  return 31 - __builtin_clz(val);
// MSVC?... DWORD ret; _BitScanReverse(&ret, val); return ret;
}

// generate a complete(!) code with exactly  size  elements
int _dpa_plzxlengths_default(size_t max_bits, unsigned char *ret_lens, size_t size)
{
  // assert(ret_lens);
  if (size <= 2) {
    if (max_bits < 1) {
      return 0; // at least one bit needed...
    }
    memset(ret_lens, 1, size);
    return 1;
  }

  const size_t len = log2i(size - 1) + 1; // needed bits
  if (len > max_bits) {
    return 0; // not enough bits
  }
  // Note: (3 <= size <= (1 << len) < 2 * size)

  const size_t shorter = (1 << len) - size;  // NOTE: 0 <= shorter < size <= (1 << len)
  memset(ret_lens, len - 1, shorter);
  memset(ret_lens + shorter, len, size - shorter); // (at least one element!)

  return 1;
}

int _dpa_plzxlengths_read(dpa_bitreader_t *br, dpa_huffdec_t *hd, const unsigned char *prev, unsigned char ret_lens[])
{
  // assert(br && hd && prev && ret_lens);
  for (size_t i = 0; i < DPA_NUM_PLZXLENS; ) {
    int sym = dpa_huffdec_read(br, hd);
    if (sym < 0) {
      return 0;
    }

    if (sym < 17) { // verbatim (0..16)
      ret_lens[i] = sym;
      i++;

    } else if (sym < 20) { // prev +1/+2/+3
      const int val = prev[i] + (sym - 17) + 1;
      if (val > 16) {
        return 0;  // overflow
      }
      ret_lens[i] = val;
      i++;

    } else if (sym < 23) { // prev -1/-2/-3
      const int val = prev[i] - (sym - 20) - 1;
      if (val < 0) {
        return 0;  // underflow
      }
      ret_lens[i] = val;
      i++;

    } else { // rle fill (23..30) / copy (31..38)
      const size_t lencode = (sym - 23) & 7;

      size_t len;
      if (lencode < 3) { // 1/2/3
        len = lencode + 1;
      } else { // stored as 2..6 verbatim bits, w/ implicit leading 1
        uint32_t val;
        if (!dpa_bitreader_read_fast(br, lencode - 1, &val)) {
          return 0;
        }
        len = (1 << (lencode - 1)) | val;  // (max: 127)
      }
      if (i + len >= DPA_NUM_PLZXLENS) {
        return 0;   // too long
      }

      if (sym - 23 < 8) { // fill
        if (i == 0) {
          return 0;  // bad fill (no reference)
        }
        memset(ret_lens + i, ret_lens[i - 1], len);
      } else { // copy from prev
        memcpy(ret_lens + i, prev + i, len);
      }
      i += len;
    }
  }
  return 1;
}

int _dpa_plzxhuffdec_set_lengths(struct _dpa_plzxhuffdec_t *phd, const unsigned char *lens)
{
  // assert(phd && lens);
  if (!dpa_huffdec_from_lengths(&phd->main, 0x10, lens, 0x258) ||
      !dpa_huffdec_from_lengths(&phd->len, 0x10, lens + 0x258, 0x100) ||
      !dpa_huffdec_from_lengths(&phd->aligned, 0x10, lens + 0x358, 0x10)) {
    return 0;
  }
  return 1;
}

void _dpa_plzxhuffdec_free(struct _dpa_plzxhuffdec_t *phd)
{
  // assert(phd);
  dpa_huffdec_free(&phd->aligned);
  dpa_huffdec_free(&phd->len);
  dpa_huffdec_free(&phd->main);
}

int _dpa_plzxhuffdec_read_match(dpa_bitreader_t *br, const struct _dpa_plzxhuffdec_t *phd, struct _dpa_plzx_match_t *ret)
{
  // assert(br && phd && ret);
  int sym = dpa_huffdec_read(br, &phd->main);
  if (sym < 0) {
    return 0;
  } else if (sym < 256) { // literal
    ret->literal = sym;
    ret->length = 1;
    ret->type = DPA_MATCHTYPE_LITERAL;
    return 1;
  }

  _dpa_bitreader_fill(br); // we need up to 32 bits in sum (slot 70)
#define READ(n) \
  ({if (br->fill < (n)) { \
    return 0; \
  } \
  const uint32_t val = br->value & (~0u >> (32 - (n))); \
  br->value >>= (n); \
  br->fill -= (n); \
  val; })  // "statement expression" c extension...

  // assert(sym <= 0x258);
  sym -= 256;
  unsigned int slot = sym >> 3,
               len = sym & 7;
  if (slot == 0) {
    ret->delta = READ(14) - 0x2000; // [-0x2000..0x1fff]
    ret->type = DPA_MATCHTYPE_SRC;

  } else if (slot == 1) {
    const int32_t raw = READ(16) - 0x8000; // [-0x8000..0x7fff]
    ret->delta = (raw < 0) ? raw - 0x2000 : raw + 0x2000;
    ret->type = DPA_MATCHTYPE_SRC;

  } else if (slot == 2) {
    const int32_t raw = READ(18) - 0x20000; // [-0x20000..0x1ffff]
    ret->delta = (raw < 0) ? raw - 0xa000 : raw + 0xa000;
    ret->type = DPA_MATCHTYPE_SRC;  // [0..0x53fff]

  } else if (slot == 3) {
    ret->type = DPA_MATCHTYPE_FULLSRC; // only this may span multiple rift blocks

  } else if (slot < 7) { // 4..6
    ret->lru = slot - 4;
    ret->type = DPA_MATCHTYPE_LRU;

  } else if (slot >= 8 && slot < 11) {
    ret->offset = slot - 7;  // 1..3
    ret->type = DPA_MATCHTYPE_DST;

  } else { // (slot == 7 || slot >= 11)
    // assert(slot < 43);
    if (slot == 7) { // coding for slot numbers 36..63: 0xx, 10xxx, or 11xxxx
      if (br->fill < 6) { // (even slot 36 will read at least 13 bits after its 3-bit coding...)
        return 0;
      }
      if ((br->value & 1) == 0) {
        slot = 43 + (READ(1 + 2) >> 1); // 43..46
      } else if ((br->value & 2) == 2) {
        slot = 47 + (READ(2 + 3) >> 2); // 47..54
      } else { // ((br->value & 2) == 3)
        slot = 55 + (READ(2 + 4) >> 2); // 55..70
      }
    }
    // assert(slot >= 11 && slot <= 70);

    const unsigned int verbatim_len = (slot - 9) >> 1, // = ((slot - 11) >> 1) + 1
                       top_bits = 2 | ((slot - 11) & 1);
    // assert(verbatim_len >= 1 && verbatim_len < 31);

    ret->offset = top_bits << verbatim_len;
    if (verbatim_len < 4) { // (11 <= slot < 17)
      ret->offset |= READ(verbatim_len); // 4..31
    } else { // lowest 4 bits are actually aligned_bits
      if (verbatim_len > 4) { // (slot != 17, 18):
        ret->offset |= READ(verbatim_len - 4) << 4; // READ(0) is unfortunately not safe
      }
      const int aligned_bits = dpa_huffdec_read(br, &phd->aligned); // 0..15  or -1
      if (aligned_bits < 0) {
        return 0;
      }
      ret->offset |= aligned_bits; // 32..0xffffffff
    }
    // assert(ret->offset >= 4 && ret->offset <= 0xffffffff);
    ret->type = DPA_MATCHTYPE_DST;
//   if (ret->offset > 0xfffabffc): error   // -0x54004 - i.e. += 0x54003 does not overflow
  }

  if (len == 0) {
    const int length_bits = dpa_huffdec_read(br, &phd->len); // 0..255  or -1
    if (length_bits < 0) {
      return 0;
    } else if (length_bits == 0) { // var-length coding for length_bits >= 256
      uint32_t val;
      if (!dpa_bitreader_read_number_8(br, &val) ||
          val > 0xffffffff - 8) {
        return 0;
      }
      ret->length = val + 8;
    } else {
      ret->length = length_bits + 8; // 9...263
    }
  } else {
    ret->length = len + 1; // 2..8
  }

  return 1;
#undef READ
}


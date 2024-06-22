#include "getdeltainfo.h"
#include <stdlib.h>
#include <stdio.h>

// 100-nanosecond units since the start of January 1, 1601.
// time_t
uint32_t filetime_to_unix(uint64_t ft)
{
  return (ft - 11644473600000 * 10000) / 10000000;
}

// must be freed
static unsigned char *load_file(const char *name, size_t *ret_len) // {{{
{
  if (!name) {
    return 0;
  }

  FILE *f = fopen(name, "rb");
  if (!f) {
    return 0;
  }

  unsigned char *buf = NULL;
  size_t len = 0;
  while (!feof(f)) {
    unsigned char *tmp = realloc(buf, len + 4096);
    if (!tmp) {
      free(buf);
      return NULL;
    }
    buf = tmp;

    const int res = fread(buf + len, 1, 4096, f);
    if (res > 0) {
      len += res;
    } // else ... // TODO?!
  }
  if (ret_len) {
    *ret_len = len;
  }

  fclose(f);
  return buf;
}
// }}}

#include "bitreader/bitreader.h"
#include "plzx/huffdec.h"
#include "plzx/composite.h"
#include <ctype.h>

// NOTE: this is a full implementation, unlike lzx delta!
static void _update_lru(uint32_t lru[3], uint32_t val)
{
  if (lru[0] == val) {
    return;
  } else if (lru[1] != val) {
    lru[2] = lru[1];
  }
  lru[1] = lru[0];
  lru[0] = val;
}

#if 0
// cannot use memcpy/memmove: this must handle overlaps between the end of src and the start of dst by using already copied bytes as source
static inline void copy(unsigned char *dst, const unsigned char *src, size_t len)
{
  // assert(src && dst && src < dst);
  for (; len > 0; len--) {
    *dst++ = *src++;
  }
}
#endif

static int dump_patch(const unsigned char *buf, size_t len, size_t dst_size)
{
  // assert(buf);
  if (len == 0) {  // TODO?
    return 1;
  }

  dpa_bitreader_t br;
  if (!dpa_bitreader_init(&br, buf, len)) {
    fprintf(stderr, "Error: bitreader init failed\n");
    return 0;
  }

  uint32_t val;
#define READ(n) \
  ({if (!dpa_bitreader_read_fast(&br, n, &val)) { \
    fprintf(stderr, "Error: unexpected EOF\n"); \
    return 0; \
  } \
  val; })

  // -- base rift table --
  const char is_non_empty = READ(1);

  printf("base rift table:\n"
         "  non-empty: %d\n",
         is_non_empty);
  if (is_non_empty) {
    fprintf(stderr, "non-empty base rift table not yet supported\n");
    return 0;
  }
#undef READ

// TODO: for full decoding, the base rift table is now merged with the file-type-specific generated rift tables!
// actually, even for RAW, where preprocRiftTable = CreateEmptyRiftTable(), – but not for srcFile == NULL ... –
// the final rift table used for decoding has one entry, which seems to translate outpos=srcSize to 0 ...

  // -- composite format --

  dpa_plzxdec_t *dec = dpa_plzx_read_composite(&br);
  if (!dec) {
    fprintf(stderr, "Error: Could not read composite header\n");
    return 0;
  }

  // -- compressor bitstream --

  struct _dpa_plzxhuffdec_t phd = {};   // must be initially zeroed!
  size_t next_block = 0;
  size_t next_block_start = dec->params[0].start;   // = 0; // TODO?  (esp. when  srcSize > 0 ?)

  uint32_t lru[3] = {};  // (note: unlike lzx delta, init to 0)
  size_t opos = 0;    // FIXME:  srcSize  -> buf.size() of input after preprocessing ...
  while (br.fill > 0) { // (else assert(br.pos == br.in.len): no op will have read all 57 bit after a _fill)
    while (opos >= next_block_start) { // NOTE: safe for opos >= dst_size.
      if (!_dpa_plzxhuffdec_set_lengths(&phd, dec->params[next_block].lens)) {
        fprintf(stderr, "Error: plzdec_set_lengths failed\n");
        goto err;
      }
      next_block++;
      next_block_start = (next_block < dec->blocks) ? dec->params[next_block].start : ~0u;
    }

    struct _dpa_plzx_match_t match;
    if (!_dpa_plzxhuffdec_read_match(&br, &phd, &match)) {
      fprintf(stderr, "Error: plzdec_read_match failed\n");
      goto err;
    }

    // CAVE: this check must catch opos >= dst_size w/o overflow!
    if (opos + match.length < opos ||    // or: __builtin_add_overflow ?
        opos + match.length > dst_size) {
      fprintf(stderr, "Error: match length %d too big at %zu\n", match.length, opos);
      goto err;
    }

    switch (match.type) {
    case DPA_MATCHTYPE_LITERAL:
if (isprint(match.literal)) printf("  LITERAL: 0x%02x (%c)\n", match.literal, match.literal); else printf("  LITERAL: 0x%02x\n", match.literal);
      // no lru update.
      break;

    case DPA_MATCHTYPE_SRC:
printf("  SRC delta: %d, length: %d\n", match.delta, match.length);
      break;

    case DPA_MATCHTYPE_FULLSRC:
printf("  FULLSRC length: %d\n", match.length);
      break;

    case DPA_MATCHTYPE_LRU:
printf("  LRU %d (-> %d), length: %d\n", match.lru, lru[match.lru], match.length);
      _update_lru(lru, lru[match.lru]);
      break;

    case DPA_MATCHTYPE_DST:
printf("  DST offset: %d, length: %d\n", match.offset, match.length);
      // assert(match.offset > 0);
      if (match.offset > opos) {
        fprintf(stderr, "Error: bad dst match offset: %d at %zu\n", match.offset, opos);
        goto err;
      }
      _update_lru(lru, match.offset);  // NOTE: we can't be 100% sure that compressor has used LRU symbol to encode offset in current LRU list...
      break;

    default: // should be unreachable...
      // assert(0);
      fprintf(stderr, "BUG: bad match type: %d\n", match.type);
      goto err;
    }

    // assert(match.length > 0);
    opos += match.length;
  }
  _dpa_plzxhuffdec_free(&phd);

  free(dec);
  return 1;
err:
  _dpa_plzxhuffdec_free(&phd);
  free(dec);
  return 0;
}


int main(int argc, char **argv)
{
  const char *filename = "in1";
  if (argc >= 2) filename = argv[1];

  size_t inlen;
  unsigned char *inbuf = load_file(filename, &inlen);
  if (!inbuf) {
    fprintf(stderr, "Error: failed to load file: %s (%m)\n", filename);
    return 1;
  }

  dpa_span_t input = { inbuf, inlen };
  dpa_header_info_t dhi = {};
  dpa_extra_info_t extra = {};
//  if (!dpa_GetDeltaInfo(&input, &dhi, NULL)) {
  if (!dpa_GetDeltaInfo(&input, &dhi, &extra)) {
    fprintf(stderr, "Error: GetDeltaInfo failed\n");
    free(inbuf);
    return 1;
  }

  printf("GetDeltaInfo(%s):\n"
         "  FileTypeSet: 0x%016llx\n"
         "  FileType: 0x%016llx\n"
         "  Flags: 0x%016llx\n"
         "  TargetSize: %zu\n"
         "  TargetFileTime: 0x%016llx (unix: %d)\n"
         "  TargetHashAlgId: 0x%08x\n"
         "  TargetHash:\n"
         "    HashSize: %zu\n"
         "    HashValue:",
         filename,
         dhi.FileTypeSet, dhi.FileType, dhi.Flags,
         dhi.TargetSize, dhi.TargetFileTime, filetime_to_unix(dhi.TargetFileTime),
         dhi.TargetHashAlgId, dhi.TargetHash.HashSize);
  for (size_t i = 0; i < dhi.TargetHash.HashSize; i++) {
    printf(" %02x", dhi.TargetHash.HashValue[i]);
  }
  printf("\n");

  printf("\nextra:\n"
         "  preproc start: %zd, len: %zu\n"
         "  patch start: %zd, len: %zu\n"
         "  end: %zd  (file size: %zu)\n",
         extra.preproc.buf - inbuf, extra.preproc.len,
         extra.patch.buf - inbuf, extra.patch.len,
         extra.end - inbuf, inlen);

  if (extra.preproc.len > 0) {
    fprintf(stderr, "TODO: support preproc info\n");
  } else {
    printf("\nno preproc info\n");
  }

  if (extra.patch.len > 0) {
    if (!dump_patch(extra.patch.buf, extra.patch.len, dhi.TargetSize)) {
      free(inbuf);
      return 1;
    }
  } else {
    printf("\nno patch info\n");   // TODO?
  }

  free(inbuf);

  return 0;
}


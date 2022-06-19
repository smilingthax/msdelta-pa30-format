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
         "  TargetSize: %zd\n"
         "  TargetFileTime: 0x%016llx (unix: %d)\n"
         "  TargetHashAlgId: 0x%08x\n"
         "  TargetHash:\n"
         "    HashSize: %zd\n"
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
         "  preproc start: %zd, len: %zd\n"
         "  patch start: %zd, len: %zd\n"
         "  end: %zd  (file size: %zd)\n",
         extra.preproc.buf - inbuf, extra.preproc.len,
         extra.patch.buf - inbuf, extra.patch.len,
         extra.end - inbuf, inlen);

  free(inbuf);

  return 0;
}


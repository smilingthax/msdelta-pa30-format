#pragma once

#include "plzxtypes.h"
#include "../bitreader/huffman.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _dpa_plzxhuffdec_t {
  dpa_huffdec_t main;
  dpa_huffdec_t len;
  dpa_huffdec_t aligned;
};

int _dpa_plzxlengths_default(size_t max_bits, unsigned char *ret_lens, size_t size);
int _dpa_plzxlengths_read(dpa_bitreader_t *br, dpa_huffdec_t *hd, const unsigned char *prev, unsigned char ret_lens[]);

int _dpa_plzxhuffdec_set_lengths(struct _dpa_plzxhuffdec_t *phd, const unsigned char *lens);
void _dpa_plzxhuffdec_free(struct _dpa_plzxhuffdec_t *phd);

int _dpa_plzxhuffdec_read_match(dpa_bitreader_t *br, const struct _dpa_plzxhuffdec_t *phd, struct _dpa_plzx_match_t *ret);

#ifdef __cplusplus
}
#endif

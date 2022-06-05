#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const unsigned char *buf;
  size_t len;
} dpa_span_t;


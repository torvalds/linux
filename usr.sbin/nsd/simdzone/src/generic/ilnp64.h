/*
 * ilnp64.h -- 64-bit Locator (RFC6742 section 2.3) parser
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef ILNP64_H
#define ILNP64_H

// FIXME: very likely eligable for vectorization (or optimization even), but
//        gains are small as the type is not frequently used
nonnull_all
static really_inline int32_t parse_ilnp64(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token)
{
  uint16_t a[4] = { 0, 0, 0, 0 };
  size_t n = 0;
  const char *p = token->data, *g = p;
  for (;;) {
    const uint8_t c = (uint8_t)*p;
    if (c == ':') {
      if (n == 3 || p == g || p - g > 4)
        break;
      g = p += 1;
      n += 1;
    } else {
      uint16_t x;
      if (c >= '0' && c <= '9')
        x = c - '0';
      else if (c >= 'A' && c <= 'F')
        x = c - ('A' - 10);
      else if (c >= 'a' && c <= 'f')
        x = c - ('a' - 10);
      else
        break;
      a[n] = (uint16_t)(a[n] << 4u) + x;
      p += 1;
    }
  }

  if (n != 3 || p == g || p - g > 4 || classify[(uint8_t)*p] == CONTIGUOUS)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  a[0] = htobe16(a[0]);
  a[1] = htobe16(a[1]);
  a[2] = htobe16(a[2]);
  a[3] = htobe16(a[3]);
  memcpy(rdata->octets, a, 8);
  rdata->octets += 8;
  return 0;
}

#endif // ILNP64_H

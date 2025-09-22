/*
 * nsec.h -- NSEC (RFC4043) parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef NSEC_H
#define NSEC_H

nonnull_all
static really_inline int32_t scan_type(
  const char *, size_t, uint16_t *, const mnemonic_t **);

typedef uint8_t nsec_t[32 /* 256 / 8 */ + 2];

nonnull_all
static really_inline int32_t parse_nsec(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  token_t *token)
{
  if (likely(is_contiguous(token))) {
    nsec_t *bitmap = (void *)rdata->octets;
    assert(rdata->octets <= rdata->limit);
    assert((size_t)(rdata->limit - rdata->octets) >= 256 * sizeof(nsec_t));

    uint32_t highest_window = 0;
    uint32_t windows[256] = { 0 };

    do {
      uint16_t code;
      const mnemonic_t *mnemonic;

      if (scan_type(token->data, token->length, &code, &mnemonic) != 1)
        SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

      const uint8_t bit = (uint8_t)(code % 256);
      const uint8_t window = code / 256;
      const uint8_t block = bit / 8;

      if (!windows[window])
        memset(bitmap[window], 0, sizeof(bitmap[window]));
      if (window > highest_window)
        highest_window = window;
      windows[window] |= 1 << block;
      bitmap[window][2 + block] |= (1 << (7 - bit % 8));
      take(parser, token);
    } while (is_contiguous(token));

    for (uint32_t window = 0; window <= highest_window; window++) {
      if (!windows[window])
        continue;
      const uint8_t blocks = (uint8_t)(64 - leading_zeroes(windows[window]));
      memmove(rdata->octets, &bitmap[window], 2 + blocks);
      rdata->octets[0] = (uint8_t)window;
      rdata->octets[1] = blocks;
      rdata->octets += 2 + blocks;
    }
  }

  return have_delimiter(parser, type, token);
}

#endif // NSEC_H

/*
 * ttl.h -- Time to Live (TTL) parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef TTL_H
#define TTL_H

// [sS] = 1, [mM] = 60, [hH] = 60*60, [dD] = 24*60*60, [wW] = 7*24*60*60
static const uint32_t ttl_units[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0x00 - 0x0f
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0x10 - 0x1f
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0x20 - 0x2f
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0x30 - 0x3f
  0, 0, 0, 0, 86400, 0, 0, 0, 3600, 0, 0, 0, 0, 60, 0, 0, // 0x40 - 0x4f
  0, 0, 0, 1, 0, 0, 0, 604800, 0, 0, 0, 0, 0, 0, 0, 0,    // 0x50 - 0xf5
  0, 0, 0, 0, 86400, 0, 0, 0, 3600, 0, 0, 0, 0, 60, 0, 0, // 0x60 - 0x6f
  0, 0, 0, 1, 0, 0, 0, 604800, 0, 0, 0, 0, 0, 0, 0, 0,    // 0x70 - 0x7f
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0x80 - 0x8f
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0x90 - 0x9f
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0xa0 - 0xaf
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0xb0 - 0xbf
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0xc0 - 0xcf
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0xd0 - 0xdf
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0xe0 - 0xef
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0          // 0xf0 - 0xff
};

nonnull_all
static really_inline int32_t scan_ttl(
  const char *data, size_t length, bool allow_units, uint32_t *ttl)
{
  if (scan_int32(data, length, ttl))
    return 1;
  if (!allow_units)
    return 0;

  uint64_t sum = 0, number = (uint8_t)data[0] - '0';
  // ttls must start with a number. e.g. 1h not h1
  if (number > 9)
    return 0;

  uint64_t unit = 0, last_unit = 0;
  enum { NUMBER, UNIT } state = NUMBER;

  for (size_t count = 1; count < length; count++) {
    const uint64_t digit = (uint8_t)data[count] - '0';

    if (state == NUMBER) {
      if (digit < 10) {
        number = number * 10 + digit;
        if (number > UINT32_MAX)
          return 0;
      } else if (!(unit = ttl_units[ (uint8_t)data[count] ])) {
        return 0;
      // units must not be repeated e.g. 1m1m
      } else if (unit == last_unit) {
        return 0;
      // greater units must precede smaller units. e.g. 1m1s, not 1s1m
      } else if (unit < last_unit) {
        return 0;
      } else {
        if (UINT32_MAX / unit < number)
          return 0;
        number *= unit;
        if (UINT32_MAX - sum < number)
          return 0;
        last_unit = unit;
        sum += number;
        number = 0;
        state = UNIT;
      }
    } else if (state == UNIT) {
      // units must be followed by a number. e.g. 1h30m, not 1hh
      if (digit > 9)
        return 0;
      // units must not be followed by a number if smallest unit,
      // i.e. seconds, was previously specified
      if (last_unit == 1)
        return 0;
      number = digit;
      state = NUMBER;
    }
  }

  if (UINT32_MAX - sum < number)
    return 0;

  sum += number;
  *ttl = (uint32_t)sum;
  return 1;
}

nonnull_all
static really_inline int32_t parse_ttl(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token)
{
  uint32_t ttl;
  if (!scan_ttl(token->data, token->length, parser->options.pretty_ttls, &ttl))
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  // FIXME: comment RFC2308 msb
  if (ttl & (1u << 31))
    SEMANTIC_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  ttl = htobe32(ttl);
  memcpy(rdata->octets, &ttl, sizeof(ttl));
  rdata->octets += 4;
  return 0;
}

#endif // TTL_H

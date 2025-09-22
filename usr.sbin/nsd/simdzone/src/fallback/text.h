/*
 * text.h -- fallback string parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef TEXT_H
#define TEXT_H

nonnull_all
static really_inline uint32_t unescape(const char *text, uint8_t *wire)
{
  uint8_t d[3];

  if ((d[0] = (uint8_t)text[1] - '0') > 9) {
    *wire = (uint8_t)text[1];
    return 2u;
  } else {
    d[1] = (uint8_t)text[2] - '0';
    d[2] = (uint8_t)text[3] - '0';
    uint32_t o = d[0] * 100 + d[1] * 10 + d[2];
    *wire = (uint8_t)o;
    return (o > 255 || d[1] > 9 || d[2] > 9) ? 0 : 4u;
  }
}

nonnull_all
static really_inline int32_t scan_string(
  const char *data,
  size_t length,
  uint8_t *octets,
  const uint8_t *limit)
{
  const char *text = data, *text_limit = data + length;
  uint8_t *wire = octets;

  if (likely((uintptr_t)limit - (uintptr_t)wire >= length)) {
    while (text < text_limit) {
      *wire = (uint8_t)*text;
      if (likely(*text != '\\')) {
        text += 1;
        wire += 1;
      } else {
        const uint32_t octet = unescape(text, wire);
        if (!octet)
          return -1;
        text += octet;
        wire += 1;
      }
    }

    if (text != text_limit)
      return -1;
    return (int32_t)(wire - octets);
  } else {
    while (text < text_limit && wire < limit) {
      *wire = (uint8_t)*text;
      if (likely(*text != '\\')) {
        text += 1;
        wire += 1;
      } else {
        const uint32_t octet = unescape(text, wire);
        if (!octet)
          return -1;
        text += octet;
        wire += 1;
      }
    }

    if (text != text_limit || wire > limit)
      return -1;
    return (int32_t)(wire - octets);
  }
}

#endif // TEXT_H

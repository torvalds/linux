/*
 * name.h -- domain name parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef NAME_H
#define NAME_H

nonnull_all
static really_inline int32_t scan_name(
  const char *data,
  size_t length,
  uint8_t octets[255 + ZONE_BLOCK_SIZE],
  size_t *lengthp)
{
  uint8_t *l = octets, *w = octets + 1;
  const uint8_t *we = octets + 255;
  const char *t = data, *te = t + length;

  l[0] = 0;

  if (*t == '.')
    return (*lengthp = length) == 1 ? 0 : -1;

  while ((t < te) & (w < we)) {
    *w = (uint8_t)*t;
    if (*t == '\\') {
      uint32_t n;
      if (!(n = unescape(t, w)))
        return -1;
      w += 1; t += n;
    } else if (*t == '.') {
      if ((w - 1) - l > 63 || (w - 1) - l == 0)
        return -1;
      l[0] = (uint8_t)((w - 1) - l);
      l = w;
      l[0] = 0;
      w += 1; t += 1;
    } else {
      w += 1; t += 1;
    }
  }

  if ((w - 1) - l > 63)
    return -1;
  *l = (uint8_t)((w - 1) - l);

  if (t != te || w > we)
    return -1;

  *lengthp = (size_t)(w - octets);
  return *l != 0;
}

#endif // NAME_H

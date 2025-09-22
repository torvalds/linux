/*
 * apl.h -- Address Prefix Lists (RFC3123) parser
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef APL_H
#define APL_H

static really_inline int32_t scan_apl(
  const char *text, size_t length, uint8_t *octets, size_t size)
{
  uint8_t negate = text[0] == '!';
  uint8_t digits[3];
  int32_t count;
  uint32_t prefix;
  const uint8_t af_inet[2] = { 0x00, 0x01 }, af_inet6[2] = { 0x00, 0x02 };

  // address family is immediately followed by a colon ":"
  if (text[negate + 1] != ':')
    return -1;

  switch (text[negate]) {
    case '1':
      if (size < 8)
        return -1;
      memcpy(octets, af_inet, sizeof(af_inet));
      if (!(count = scan_ip4(&text[negate+2], &octets[4])))
        return -1;
      count += negate + 2;
      digits[0] = (uint8_t)text[count+1] - '0';
      digits[1] = (uint8_t)text[count+2] - '0';
      if (text[count] != '/' || digits[0] > 9)
        return -1;
      if (digits[1] > 9)
        (void)(count += 2), prefix = digits[0];
      else
        (void)(count += 3), prefix = digits[0] * 10 + digits[1];
      if (prefix > 32 || (size_t)count != length)
        return -1;
      octets[2] = (uint8_t)prefix;
      octets[3] = (uint8_t)((negate << 7) | 4);
      return 8;
    case '2':
      if (size < 20)
        return -1;
      memcpy(octets, af_inet6, sizeof(af_inet6));
      if (!(count = scan_ip6(&text[negate+2], &octets[4])))
        return -1;
      count += negate + 2;
      digits[0] = (uint8_t)text[count+1] - '0';
      digits[1] = (uint8_t)text[count+2] - '0';
      digits[2] = (uint8_t)text[count+3] - '0';
      if (text[count] != '/' || digits[0] > 9)
        return -1;
      if (digits[1] > 9)
        (void)(count += 2), prefix = digits[0];
      else if (digits[2] > 9)
        (void)(count += 3), prefix = digits[0] * 10 + digits[1];
      else
        (void)(count += 4), prefix = digits[0] * 100 + digits[1] * 10 + digits[0];
      if (prefix > 128 || (size_t)count != length)
        return -1;
      octets[2] = (uint8_t)prefix;
      octets[3] = (uint8_t)((negate << 7) | 16);
      return 20;
    default:
      return -1;
  }
}

#endif // APL_H

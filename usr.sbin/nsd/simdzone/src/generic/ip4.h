/*
 * ip4.h -- fallback parser for IPv4 addresses
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef IP4_H
#define IP4_H

nonnull_all
static really_inline int32_t scan_ip4(const char *text, uint8_t *wire)
{
  const char *start = text;
  uint32_t round = 0;
  for (;;) {
    uint8_t digits[3], count;
    uint32_t octet;
    digits[0] = (uint8_t)text[0] - '0';
    digits[1] = (uint8_t)text[1] - '0';
    digits[2] = (uint8_t)text[2] - '0';
    if (digits[0] > 9)
      return 0;
    else if (digits[1] > 9)
      (void)(count = 1), octet = digits[0];
    else if (digits[2] > 9)
      (void)(count = 2), octet = digits[0] * 10 + digits[1];
    else
      (void)(count = 3), octet = digits[0] * 100 + digits[1] * 10 + digits[2];

    if (octet > 255 || (count > 1 && !digits[0]))
      return 0;
    text += count;
    wire[round++] = (uint8_t)octet;
    if (text[0] != '.' || round == 4)
      break;
    text += 1;
  }

  if (round != 4)
    return 0;
  return (int32_t)(text - start);
}

nonnull_all
static really_inline int32_t parse_ip4(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *item,
  rdata_t *rdata,
  const token_t *token)
{
  if ((size_t)scan_ip4(token->data, rdata->octets) != token->length)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(item), NAME(type));
  rdata->octets += 4;
  return 0;
}

#endif // IP4_H

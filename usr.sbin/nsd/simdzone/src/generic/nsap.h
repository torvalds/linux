/*
 * nsap.h -- NSAP (RFC1706) parser
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef NSAP_H
#define NSAP_H

// https://datatracker.ietf.org/doc/html/rfc1706 (historic)

nonnull_all
static really_inline int32_t parse_nsap(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token)
{
  const uint8_t *data = (const uint8_t *)token->data;

  // RFC1706 section 7
  // NSAP format is "0x" (i.e., a zero followed by an 'x' character) followed
  // by a variable length string of hex characters (0 to 9, a to f). The hex
  // string is case-insensitive. "."s (i.e., periods) may be inserted in the
  // hex string anywhere after "0x" for readability. The "."s have no
  // significance other than for readability and are not propagated in the
  // protocol (e.g., queries or zone transfers).
  if (unlikely(data[0] != '0' || !(data[1] == 'X' || data[1] == 'x')))
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

  data += 2;

  while (rdata->octets < rdata->limit) {
    uint32_t d0 = base16_table_dec_32bit_d0[data[0]];
    uint32_t d1 = base16_table_dec_32bit_d1[data[1]];
    if ((d0 | d1) > 0xff) {
      while (*data == '.') data++;
      d0 = base16_table_dec_32bit_d0[data[0]];
      if (d0 > 0xff)
        break;
      data += 1;
      while (*data == '.') data++;
      d1 = base16_table_dec_32bit_d1[data[0]];
      if (d1 > 0xff)
        goto bad_sequence;
      data += 1;
    } else {
      data += 2;
    }
    *rdata->octets++ = (uint8_t)(d0 | d1);
  }

  if (rdata->octets <= rdata->limit && data == (uint8_t *)token->data + token->length)
    return 0;

bad_sequence:
  SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
}

#endif // NSAP_H

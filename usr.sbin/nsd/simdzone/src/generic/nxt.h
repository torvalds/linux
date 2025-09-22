/*
 * nxt.h - NXT (RFC2535) parser
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef NXT_H
#define NXT_H

nonnull_all
static really_inline int32_t scan_type(
  const char *data,
  size_t length,
  uint16_t *code,
  const mnemonic_t **mnemonic);

nonnull_all
static really_inline int32_t parse_nxt(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  token_t *token)
{
  uint16_t code;
  const mnemonic_t *mnemonic;

  if (is_contiguous(token)) {
    if (scan_type(token->data, token->length, &code, &mnemonic) != 1)
      SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
    uint8_t bit = (uint8_t)(code % 8);
    uint8_t block = (uint8_t)(code / 8), highest_block = block;

    memset(rdata->octets, 0, block + 1);
    rdata->octets[block] = (uint8_t)(1 << (7 - bit));

    take(parser, token);
    while (is_contiguous(token)) {
      if (scan_type(token->data, token->length, &code, &mnemonic) != 1)
        SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
      bit = (uint8_t)(code % 8);
      block = (uint8_t)(code / 8);
      if (block > highest_block) {
        memset(&rdata->octets[highest_block+1], 0, block - highest_block);
        highest_block = block;
      }
      rdata->octets[block] |= 1 << (7 - bit);
      take(parser, token);
    }

    rdata->octets += highest_block + 1;
  }

  return have_delimiter(parser, type, token);
}

#endif // NXT_H

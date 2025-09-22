/*
 * gpos.h -- Geographical Location (RFC1712) parser
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef GPOS_H
#define GPOS_H

nonnull_all
static really_inline int32_t parse_latitude(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token)
{
  const char *text = token->data + (token->data[0] == '-');
  uint32_t degrees;
  uint8_t digits[4];
  digits[0] = (uint8_t)text[0] - '0';
  digits[1] = (uint8_t)text[1] - '0';
  digits[2] = (uint8_t)text[2] - '0';
  digits[3] = (uint8_t)text[3] - '0';

  int32_t mask = ((digits[0] <= 9) << 0) | // 0b0001
                 ((digits[1] <= 9) << 1) | // 0b0010
                 ((digits[2] <= 9) << 2) | // 0b0100
                 ((digits[3] <= 9) << 3);  // 0b1000

  if (token->length > 255)
    goto bad_latitude;

  switch (mask) {
    case 0x01: // 0b0001 ("d...")
    case 0x09: // 0b1001 ("d..d")
      text += 1;
      break;
    case 0x03: // 0b0011 ("dd..")
      // ensure no leading zero and range is between -90 and 90
      degrees = digits[0] * 10 + digits[1];
      if (degrees < 10 || degrees > 90)
        goto bad_latitude;
      text += 2;
      break;
    case 0x05: // 0b1010 ("d.d.")
    case 0x0d: // 0b1011 ("d.dd")
      if (text[1] != '.')
        text += 1;
      else
        for (text += 2; 10u > (uint8_t)((uint8_t)text[0] - '0'); text++) ;
      break;
    case 0x0b: // 0b1011 ("dd.d")
      if (text[2] != '.')
        text += 2;
      else
        for (text += 3; 10u > (uint8_t)((uint8_t)text[0] - '0'); text++) ;
      break;
    default:
      goto bad_latitude;
  }

  if (text != token->data + token->length)
    goto bad_latitude;

  *rdata->octets = (uint8_t)token->length;
  memcpy(rdata->octets + 1, token->data, token->length);
  rdata->octets += 1 + token->length;
  return 0;
bad_latitude:
  SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
}

nonnull_all
static really_inline int32_t parse_longitude(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token)
{
  const char *text = token->data + (token->data[0] == '-');
  uint32_t degrees;
  uint8_t digits[5];
  digits[0] = (uint8_t)text[0] - '0';
  digits[1] = (uint8_t)text[1] - '0';
  digits[2] = (uint8_t)text[2] - '0';
  digits[3] = (uint8_t)text[3] - '0';
  digits[4] = (uint8_t)text[4] - '0';

  int32_t mask = ((digits[0] <= 9) << 0) | // 0b00001
                 ((digits[1] <= 9) << 1) | // 0b00010
                 ((digits[2] <= 9) << 2) | // 0b00100
                 ((digits[3] <= 9) << 3) | // 0b01000
                 ((digits[4] <= 9) << 4);  // 0b10000

  if (token->length > 255)
    goto bad_longitude;

  switch (mask) {
    case 0x01: // 0b00001 ("d....")
    case 0x09: // 0b01001 ("d..d.")
    case 0x19: // 0b11001 ("d..dd")
      text += 1;
       break;
    case 0x03: // 0b00011 ("dd...")
    case 0x13: // 0b10011 ("dd..d")
      degrees = digits[0] * 10 + digits[1];
      // ensure no leading zero
      if (degrees < 10)
        goto bad_longitude;
      text += 2;
      break;
    case 0x07: // 0b00111 ("ddd..")
      // ensure no leading zero and range is between -180 and 180
      degrees = digits[0] * 100 + digits[1] * 10 + digits[2];
      if (degrees < 100 || degrees > 180)
        goto bad_longitude;
      text += 3;
      break;
    case 0x05: // 0b00101 ("d.d..")
    case 0x0d: // 0b01101 ("d.dd.")
    case 0x1d: // 0b11101 ("d.ddd")
      if (text[1] != '.')
        text += 1;
      else
        for (text += 2; (uint8_t)((uint8_t)text[0] - '0') <= 9u; text++) ;
      break;
    case 0x0b: // 0b01011 ("dd.d.")
    case 0x1b: // 0b11011 ("dd.dd")
      // ensure no leading zero
      degrees = digits[0] * 10 + digits[1];
      if (degrees < 10)
        goto bad_longitude;
      if (text[2] != '.')
        text += 2;
      else
        for (text += 3; (uint8_t)((uint8_t)text[0] - '0') <= 9u; text++) ;
      break;
    case 0x17: // 0b10111 ("ddd.d")
      // ensure no leading zero and range is between -180 and 180
      degrees = digits[0] * 100 + digits[1] * 10 + digits[2];
      if (degrees < 100 || degrees > 180)
        goto bad_longitude;
      if (text[3] != '.')
        text += 3;
      else
        for (text += 4; (uint8_t)((uint8_t)text[0] - '0') <= 9u; text++) ;
      break;
    default:
      goto bad_longitude;
  }

  if (text != token->data + token->length)
    goto bad_longitude;

  *rdata->octets = (uint8_t)token->length;
  memcpy(rdata->octets + 1, token->data, token->length);
  rdata->octets += 1 + token->length;
  return 0;
bad_longitude:
  SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
}

nonnull_all
static really_inline int32_t parse_altitude(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token)
{
  const char *text = token->data;

  if (token->length > 255)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

  for (; (uint8_t)((uint8_t)*text - '0') <= 9u; text++) ;

  if (*text == '.')
    for (text++; (uint8_t)((uint8_t)*text - '0') <= 9u; text++) ;

  if (text != token->data + token->length)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

  *rdata->octets = (uint8_t)token->length;
  memcpy(rdata->octets + 1, token->data, token->length);
  rdata->octets += 1 + token->length;
  return 0;
}

#endif // GPOS_H

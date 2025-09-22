/*
 * loc.h -- Location Information (RFC1876) parser
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef LOC_H
#define LOC_H

nonnull_all
static really_inline int32_t scan_degrees(
	const char *text, size_t length, uint32_t *degrees)
{
  uint8_t digits[3];

  digits[0] = (uint8_t)text[0] - '0';
  digits[1] = (uint8_t)text[1] - '0';
  digits[2] = (uint8_t)text[2] - '0';

  switch (length) {
    case 1:
      *degrees = digits[0] * 3600000;
      if (digits[0] > 9)
        return -1;
      return 0;
    case 2:
      *degrees = digits[0] * 36000000 + digits[1] * 3600000;
      if (digits[0] > 9 || digits[1] > 9)
        return -1;
      return 0;
    case 3:
      *degrees = digits[0] * 360000000 +
                 digits[1] *  36000000 +
                 digits[2] *   3600000;
      if (*degrees > 648000000u)
        return -1;
      if (digits[0] > 9 || digits[1] > 9 || digits[2] > 9)
        return -1;
      return 0;
    default:
      return -1;
  }
}

nonnull_all
static really_inline int64_t scan_minutes(
	const char *text, size_t length, uint32_t *minutes)
{
  uint8_t digits[2];

  digits[0] = (uint8_t)text[0] - '0';
  digits[1] = (uint8_t)text[1] - '0';

  switch (length) {
    case 1:
      *minutes = digits[0] * 60000;
      if (digits[0] > 9)
        return -1;
      return 0;
    case 2:
      *minutes = digits[0] * 600000 + digits[1] * 60000;
      if (*minutes > 3600000 || digits[0] > 9 || digits[1] > 9)
        return -1;
      return 0;
    default:
      return -1;
  }
}

nonnull_all
static really_inline int64_t scan_seconds(
	const char *text, size_t length, uint32_t *seconds)
{
  uint8_t digits[3];
  size_t count;

  digits[0] = (uint8_t)text[0] - '0';
  digits[1] = (uint8_t)text[1] - '0';

  if (length == 1 || text[1] == '.') {
    count = 1;
    *seconds = digits[0] * 1000;
    if (digits[0] > 9)
      return -1;
    digits[0] = (uint8_t)text[2] - '0';
    digits[1] = (uint8_t)text[3] - '0';
    digits[2] = (uint8_t)text[4] - '0';
  } else if (length == 2 || text[2] == '.') {
    count = 2;
    *seconds = digits[0] * 10000 + digits[1] * 1000;
    if (*seconds > 60000 || digits[0] > 5 || digits[1] > 9)
      return -1;
    digits[0] = (uint8_t)text[3] - '0';
    digits[1] = (uint8_t)text[4] - '0';
    digits[2] = (uint8_t)text[5] - '0';
  } else {
    return -1;
  }

  switch (length - count) {
    case 0:
      return 0;
    case 1:
      return -1;
    case 2:
      *seconds += digits[0] * 100u;
      if (digits[0] > 9)
        return -1;
      return 0;
    case 3:
      *seconds += digits[0] * 100u + digits[1] * 10u;
      if (digits[0] > 9 || digits[1] > 9)
        return -1;
      return 0;
    case 4:
      *seconds += digits[0] * 100u + digits[1] * 10u + digits[2];
      if (digits[0] > 9 || digits[1] > 9 || digits[0] > 9)
        return -1;
      return 0;
    default:
      return -1;
  }
}

nonnull((1,3))
static really_inline int32_t scan_altitude(
  const char *text, size_t length, uint32_t *altitude)
{
  uint64_t negative = 0, limit = 11, maximum = 4284967295llu;

  if (text[0] == '-')
    (void)(negative = 1), (void)(limit = 8), maximum = 10000000llu;

  length -= (text[length - 1] == 'm');

  uint64_t meters = 0, index = negative;
  for (;; index++) {
    const uint8_t digit = (uint8_t)text[index] - '0';
    if (digit > 9)
      break;
    meters = meters * 10 + digit;
  }

  uint64_t centimeters = meters * 100u; // convert to centimeters
  if (text[index] == '.') {
    uint8_t digits[2];
    limit += 1;
    digits[0] = (uint8_t)text[index+1] - '0';
    digits[1] = (uint8_t)text[index+2] - '0';
    switch (length - index) {
      case 1:
        index += 1;
        break;
      case 2:
        if (digits[0] > 9)
          return -1;
        centimeters += (uint64_t)digits[0] * 10u;
        index += 2;
        break;
      case 3:
        if (digits[0] > 9 || digits[1] > 9)
          return -1;
        centimeters += (uint64_t)digits[0] * 10u + (uint64_t)digits[1];
        index += 3;
        break;
      default:
        return -1;
    }
  }

  if (index == negative || index > limit || index != length || centimeters > maximum)
    return -1;

  if (negative)
    *altitude = (uint32_t)(10000000llu - centimeters);
  else
    *altitude = (uint32_t)(10000000llu + centimeters);

  return 0;
}

// converts ascii size/precision X * 10**Y(cm) to 0xXY
nonnull((1,3))
static really_inline int32_t scan_precision(
  const char *text, size_t length, uint8_t *scientific)
{
  uint64_t meters = 0, centimeters;

  // RFC1876 conversion routines
  static uint64_t poweroften[10] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

  length -= text[length - 1] == 'm';

  size_t index = 0;
  for (;; index++) {
    const uint8_t digit = (uint8_t)text[index] - '0';
    if (digit > 9)
      break;
    meters = meters * 10 + digit;
  }

  if (index == 0 || index > 8) // 0 .. 90000000.00
    return -1; // syntax error

  centimeters = meters * 100; // convert to centimeters
  if (text[index] == '.') {
    uint8_t digits[2];
    digits[0] = (uint8_t)text[index+1] - '0';
    digits[1] = (uint8_t)text[index+2] - '0';
    switch (length - index) {
      case 1:
        index += 1;
        break;
      case 2:
        if (digits[0] > 9)
          return -1;
        index += 2;
        centimeters += digits[0] * 10;
        break;
      case 3:
        if (digits[0] > 9 || digits[1] > 9)
          return -1;
        index += 3;
        centimeters += digits[0] * 10 + digits[1];
        break;
      default:
        return -1;
    }
  }

  if (index != length)
    return -1; // syntax error

  uint8_t exponent = 0;
  while (exponent < 9 && centimeters >= poweroften[exponent+1])
    exponent++;

  uint8_t mantissa = (uint8_t)(centimeters / poweroften[exponent]);
  if (mantissa > 9u)
    mantissa = 9u;

  *scientific = (uint8_t)(mantissa << 4) | exponent;
  return 0;
}

#endif // LOC_H

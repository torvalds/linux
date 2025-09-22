/*
 * time.h -- timestamp parser
 *
 * Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef TIME_H
#define TIME_H

/* number of days per month (except for February in leap years) */
static const uint8_t days_in_month[13] = {
  0 /* no --month */, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static const uint16_t days_to_month[13] = {
  0 /* no --month */, 0, 31, 59,  90, 120, 151, 181, 212, 243, 273, 304, 334 };

static uint64_t is_leap_year(uint64_t year)
{
  return (year % 4 == 0) & ((year % 100 != 0) | (year % 400 == 0));
}

static uint64_t leap_days(uint64_t y1, uint64_t y2)
{
  --y1;
  --y2;
  return (y2/4 - y1/4) - (y2/100 - y1/100) + (y2/400 - y1/400);
}

nonnull_all
static really_inline int32_t parse_time(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *field,
  rdata_t *rdata,
  const token_t *token)
{
  if (token->length != 14)
    return parse_int32(parser, type, field, rdata, token);

  uint64_t d[14]; // YYYYmmddHHMMSS
  const char *p = token->data;
  for (int i = 0; i < 14; i++) {
    d[i] = (uint8_t)p[i] - '0';
    if (d[i] > 9)
      SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  }

  // code adapted from Python 2.4.1 sources (Lib/calendar.py)
  const uint64_t year = (d[0] * 1000) + (d[1] * 100) + (d[2] * 10) + d[3];
  const uint64_t mon = (d[4] * 10) + d[5];
  const uint64_t mday = (d[6] * 10) + d[7];
  const uint64_t hour = (d[8] * 10) + d[9];
  const uint64_t min = (d[10] * 10) + d[11];
  const uint64_t sec = (d[12] * 10) + d[13];

  if (year < 1970)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

  uint64_t leap_year = is_leap_year(year);
  uint64_t days = 365 * (year - 1970) + leap_days(1970, year);

  if (!mon || mon > 12)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  if (!mday || mday > days_in_month[mon] + (leap_year & (mon == 2)))
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));
  if (hour > 23 || min > 59 || sec > 59)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(field), NAME(type));

  days += days_to_month[mon];
  days += (mon > 2) & leap_year;
  days += mday - 1;

  const uint64_t hours = days * 24 + hour;
  const uint64_t minutes = hours * 60 + min;
  const uint64_t seconds = minutes * 60 + sec;

  uint32_t time = htobe32((uint32_t)seconds);
  memcpy(rdata->octets, &time, sizeof(time));
  rdata->octets += 4;
  return 0;
}

#endif // TIME_H

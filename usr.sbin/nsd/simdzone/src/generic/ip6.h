/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996,1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef IP6_H
#define IP6_H

#ifndef NS_INT16SZ
#define NS_INT16SZ  2
#endif

#ifndef NS_IN6ADDRSZ
#define NS_IN6ADDRSZ 16
#endif

#ifndef NS_INADDRSZ
#define NS_INADDRSZ 4
#endif

/* int
 * inet_pton4(src, dst)
 *  like inet_aton() but without all the hexadecimal and shorthand.
 * return:
 *  length if `src' is a valid dotted quad, else 0.
 * notice:
 *  does not touch `dst' unless it's returning !0.
 * author:
 *  Paul Vixie, 1996.
 */
static int
inet_pton4(const char *src, uint8_t *dst)
{
  static const char digits[] = "0123456789";
  int saw_digit, octets, ch;
  uint8_t tmp[NS_INADDRSZ], *tp;
  const char *start = src;

  saw_digit = 0;
  octets = 0;
  *(tp = tmp) = 0;
  for (; (ch = *src); src++) {
    const char *pch;

    if ((pch = strchr(digits, ch)) != NULL) {
      uint32_t new = *tp * 10 + (uint32_t)(pch - digits);

      if (new > 255)
        return (0);
      *tp = (uint8_t)new;
      if (! saw_digit) {
        if (++octets > 4)
          return (0);
        saw_digit = 1;
      }
    } else if (ch == '.' && saw_digit) {
      if (octets == 4)
        return (0);
      *++tp = 0;
      saw_digit = 0;
    } else
      break;
  }
  if (octets < 4)
    return (0);

  memcpy(dst, tmp, NS_INADDRSZ);
  return (int)(src - start);
}

/* int
 * inet_pton6(src, dst)
 *  convert presentation level address to network order binary form.
 * return:
 *  length if `src' is a valid [RFC1884 2.2] address, else 0.
 * notice:
 *  (1) does not touch `dst' unless it's returning !0.
 *  (2) :: in a full address is silently ignored.
 * credit:
 *  inspired by Mark Andrews.
 * author:
 *  Paul Vixie, 1996.
 */
static int
inet_pton6(const char *src, uint8_t *dst)
{
  static const char xdigits_l[] = "0123456789abcdef",
        xdigits_u[] = "0123456789ABCDEF";
  uint8_t tmp[NS_IN6ADDRSZ], *tp, *endp, *colonp;
  const char *xdigits, *curtok;
  int ch, saw_xdigit;
  uint32_t val;
  int len;
  const char *start = src;

  memset((tp = tmp), '\0', NS_IN6ADDRSZ);
  endp = tp + NS_IN6ADDRSZ;
  colonp = NULL;
  /* Leading :: requires some special handling. */
  if (*src == ':')
    if (*++src != ':')
      return (0);
  curtok = src;
  saw_xdigit = 0;
  val = 0;
  for (; (ch = *src); src++) {
    const char *pch;

    if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
      pch = strchr((xdigits = xdigits_u), ch);
    if (pch != NULL) {
      val <<= 4;
      val |= (pch - xdigits);
      if (val > 0xffff)
        return (0);
      saw_xdigit = 1;
      continue;
    }
    if (ch == ':') {
      curtok = src+1;
      if (!saw_xdigit) {
        if (colonp)
          return (0);
        colonp = tp;
        continue;
      }
      if (tp + NS_INT16SZ > endp)
        return (0);
      *tp++ = (uint8_t) (val >> 8) & 0xff;
      *tp++ = (uint8_t) val & 0xff;
      saw_xdigit = 0;
      val = 0;
      continue;
    }
    if (ch == '.' && ((tp + NS_INADDRSZ) <= endp) &&
        (len = inet_pton4(curtok, tp)) > 0) {
      src = curtok + len;
      tp += NS_INADDRSZ;
      saw_xdigit = 0;
      break;  /* '\0' was seen by inet_pton4(). */
    }
    break;
  }
  if (saw_xdigit) {
    if (tp + NS_INT16SZ > endp)
      return (0);
    *tp++ = (uint8_t) (val >> 8) & 0xff;
    *tp++ = (uint8_t) val & 0xff;
  }
  if (colonp != NULL) {
    /*
     * Since some memmove()'s erroneously fail to handle
     * overlapping regions, we'll do the shift by hand.
     */
    const int n = (int)(tp - colonp);
    int i;

    for (i = 1; i <= n; i++) {
      endp[- i] = colonp[n - i];
      colonp[n - i] = 0;
    }
    tp = endp;
  }
  if (tp != endp)
    return (0);
  memcpy(dst, tmp, NS_IN6ADDRSZ);
  return (int)(src - start);
}

nonnull_all
static really_inline int32_t scan_ip6(const char *text, uint8_t *wire)
{
  return inet_pton6(text, wire);
}

nonnull_all
static really_inline int32_t parse_ip6(
  parser_t *parser,
  const type_info_t *type,
  const rdata_info_t *item,
  rdata_t *rdata,
  const token_t *token)
{
  if ((size_t)inet_pton6(token->data, rdata->octets) != token->length)
    SYNTAX_ERROR(parser, "Invalid %s in %s", NAME(item), NAME(type));
  rdata->octets += 16;
  return 0;
}

#endif // IP6_H

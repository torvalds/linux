/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Brian Somers <brian@Awfulhak.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>
#ifdef __OpenBSD__
#include <net/if_types.h>
#include <net/route.h>
#endif
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/un.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include "log.h"
#include "ncpaddr.h"
#include "timer.h"
#include "fsm.h"
#include "defs.h"
#include "slcompress.h"
#include "iplist.h"
#include "throughput.h"
#include "mbuf.h"
#include "ipcp.h"
#include "descriptor.h"
#include "layer.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "mp.h"
#include "ipv6cp.h"
#include "ncp.h"


#define ncprange_ip4addr	u.ip4.ipaddr
#define ncprange_ip4mask	u.ip4.mask
#define ncprange_ip4width	u.ip4.width
#define ncpaddr_ip4addr		u.ip4addr
#ifndef NOINET6
#define ncprange_ip6addr	u.ip6.ipaddr
#define ncprange_ip6width	u.ip6.width
#define ncpaddr_ip6addr		u.ip6addr
#endif

static struct in_addr
bits2mask4(int bits)
{
  struct in_addr result;
  u_int32_t bit = 0x80000000;

  result.s_addr = 0;

  while (bits) {
    result.s_addr |= bit;
    bit >>= 1;
    bits--;
  }

  result.s_addr = htonl(result.s_addr);
  return result;
}

static int
mask42bits(struct in_addr mask)
{
  u_int32_t msk = ntohl(mask.s_addr);
  u_int32_t tst;
  int ret;

  for (ret = 32, tst = 1; tst; ret--, tst <<= 1)
    if (msk & tst)
      break;

  for (tst <<= 1; tst; tst <<= 1)
    if (!(msk & tst))
      break;

  return tst ? -1 : ret;
}

#ifndef NOINET6
static struct in6_addr
bits2mask6(int bits)
{
  struct in6_addr result;
  u_int32_t bit = 0x80;
  u_char *c = result.s6_addr;

  memset(&result, '\0', sizeof result);

  while (bits) {
    if (bit == 0) {
      bit = 0x80;
      c++;
    }
    *c |= bit;
    bit >>= 1;
    bits--;
  }

  return result;
}

static int
mask62bits(const struct in6_addr *mask)
{
  const u_char masks[] = { 0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe };
  const u_char *c, *p, *end;
  int masklen, m;

  p = (const u_char *)mask;
  for (masklen = 0, end = p + 16; p < end && *p == 0xff; p++)
    masklen += 8;

  if (p < end) {
    for (c = masks, m = 0; c < masks + sizeof masks; c++, m++)
      if (*c == *p) {
        masklen += m;
        break;
      }
  }

  return masklen;
}

#if 0
static void
adjust_linklocal(struct sockaddr_in6 *sin6)
{
    /* XXX: ?????!?!?!!!!!  This is horrible ! */
    /*
     * The kernel does not understand sin6_scope_id for routing at this moment.
     * We should rather keep the embedded ID.
     * jinmei@kame.net, 20011026
     */
    if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) ||
        IN6_IS_ADDR_MC_LINKLOCAL(&sin6->sin6_addr)) {
      sin6->sin6_scope_id =
        ntohs(*(u_short *)&sin6->sin6_addr.s6_addr[2]);
      *(u_short *)&sin6->sin6_addr.s6_addr[2] = 0;
    }
}
#endif
#endif

void
ncpaddr_init(struct ncpaddr *addr)
{
  addr->ncpaddr_family = AF_UNSPEC;
}

int
ncpaddr_isset(const struct ncpaddr *addr)
{
  return addr->ncpaddr_family != AF_UNSPEC;
}

int
ncpaddr_isdefault(const struct ncpaddr *addr)
{
  switch (addr->ncpaddr_family) {
  case AF_INET:
    if (addr->ncpaddr_ip4addr.s_addr == INADDR_ANY)
      return 1;
    break;

#ifndef NOINET6
  case AF_INET6:
    if (IN6_IS_ADDR_UNSPECIFIED(&addr->ncpaddr_ip6addr))
      return 1;
    break;
#endif
  }

  return 0;
}

int
ncpaddr_equal(const struct ncpaddr *addr, const struct ncpaddr *cmp)
{
  if (addr->ncpaddr_family != cmp->ncpaddr_family)
    return 0;

  switch (addr->ncpaddr_family) {
  case AF_INET:
    return addr->ncpaddr_ip4addr.s_addr == cmp->ncpaddr_ip4addr.s_addr;

#ifndef NOINET6
  case AF_INET6:
    return !memcmp(&addr->ncpaddr_ip6addr, &cmp->ncpaddr_ip6addr,
                   sizeof addr->ncpaddr_ip6addr);
#endif

  case AF_UNSPEC:
    return 1;
  }

  return 0;
}

void
ncpaddr_copy(struct ncpaddr *addr, const struct ncpaddr *from)
{
  switch (from->ncpaddr_family) {
  case AF_INET:
    addr->ncpaddr_family = AF_INET;
    addr->ncpaddr_ip4addr = from->ncpaddr_ip4addr;
    break;
#ifndef NOINET6
  case AF_INET6:
    addr->ncpaddr_family = AF_INET6;
    addr->ncpaddr_ip6addr = from->ncpaddr_ip6addr;
    break;
#endif
  default:
    addr->ncpaddr_family = AF_UNSPEC;
  }
}

void
ncpaddr_setip4addr(struct ncpaddr *addr, u_int32_t ip)
{
  addr->ncpaddr_family = AF_INET;
  addr->ncpaddr_ip4addr.s_addr = ip;
}

int
ncpaddr_getip4addr(const struct ncpaddr *addr, u_int32_t *ip)
{
  if (addr->ncpaddr_family != AF_INET)
    return 0;
  *ip = addr->ncpaddr_ip4addr.s_addr;
  return 1;
}

void
ncpaddr_setip4(struct ncpaddr *addr, struct in_addr ip)
{
  addr->ncpaddr_family = AF_INET;
  addr->ncpaddr_ip4addr = ip;
}

int
ncpaddr_getip4(const struct ncpaddr *addr, struct in_addr *ip)
{
  if (addr->ncpaddr_family != AF_INET)
    return 0;
  *ip = addr->ncpaddr_ip4addr;
  return 1;
}

#ifndef NOINET6
void
ncpaddr_setip6(struct ncpaddr *addr, const struct in6_addr *ip6)
{
  addr->ncpaddr_family = AF_INET6;
  addr->ncpaddr_ip6addr = *ip6;
}

int
ncpaddr_getip6(const struct ncpaddr *addr, struct in6_addr *ip6)
{
  if (addr->ncpaddr_family != AF_INET6)
    return 0;
  *ip6 = addr->ncpaddr_ip6addr;
  return 1;
}
#endif

void
ncpaddr_getsa(const struct ncpaddr *addr, struct sockaddr_storage *host)
{
  struct sockaddr_in *host4 = (struct sockaddr_in *)host;
#ifndef NOINET6
  struct sockaddr_in6 *host6 = (struct sockaddr_in6 *)host;
#endif

  memset(host, '\0', sizeof(*host));

  switch (addr->ncpaddr_family) {
  case AF_INET:
    host4->sin_family = AF_INET;
    host4->sin_len = sizeof(*host4);
    host4->sin_addr = addr->ncpaddr_ip4addr;
    break;

#ifndef NOINET6
  case AF_INET6:
    host6->sin6_family = AF_INET6;
    host6->sin6_len = sizeof(*host6);
    host6->sin6_addr = addr->ncpaddr_ip6addr;
    break;
#endif

  default:
    host->ss_family = AF_UNSPEC;
    break;
  }
}

void
ncpaddr_setsa(struct ncpaddr *addr, const struct sockaddr *host)
{
  const struct sockaddr_in *host4 = (const struct sockaddr_in *)host;
#ifndef NOINET6
  const struct sockaddr_in6 *host6 = (const struct sockaddr_in6 *)host;
#endif

  switch (host->sa_family) {
  case AF_INET:
    addr->ncpaddr_family = AF_INET;
    addr->ncpaddr_ip4addr = host4->sin_addr;
    break;

#ifndef NOINET6
  case AF_INET6:
    if (IN6_IS_ADDR_V4MAPPED(&host6->sin6_addr)) {
      addr->ncpaddr_family = AF_INET;
      addr->ncpaddr_ip4addr.s_addr =
        *(const u_int32_t *)(host6->sin6_addr.s6_addr + 12);
    } else {
      addr->ncpaddr_family = AF_INET6;
      addr->ncpaddr_ip6addr = host6->sin6_addr;
    }
    break;
#endif

  default:
    addr->ncpaddr_family = AF_UNSPEC;
  }
}

static char *
ncpaddr_ntowa(const struct ncpaddr *addr)
{
  static char res[NCP_ASCIIBUFFERSIZE];
#ifndef NOINET6
  struct sockaddr_in6 sin6;
#endif

  switch (addr->ncpaddr_family) {
  case AF_INET:
    snprintf(res, sizeof res, "%s", inet_ntoa(addr->ncpaddr_ip4addr));
    return res;

#ifndef NOINET6
  case AF_INET6:
    memset(&sin6, '\0', sizeof(sin6));
    sin6.sin6_len = sizeof(sin6);
    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr = addr->ncpaddr_ip6addr;
#if 0
    adjust_linklocal(&sin6);
#endif
    if (getnameinfo((struct sockaddr *)&sin6, sizeof sin6, res, sizeof(res),
                    NULL, 0, NI_NUMERICHOST) != 0)
      break;

    return res;
#endif
  }

  snprintf(res, sizeof res, "<AF_UNSPEC>");
  return res;
}

const char *
ncpaddr_ntoa(const struct ncpaddr *addr)
{
  return ncpaddr_ntowa(addr);
}


int
ncpaddr_aton(struct ncpaddr *addr, struct ncp *ncp, const char *data)
{
  struct ncprange range;

  if (!ncprange_aton(&range, ncp, data))
    return 0;

  if (range.ncprange_family == AF_INET && range.ncprange_ip4width != 32 &&
      range.ncprange_ip4addr.s_addr != INADDR_ANY) {
    log_Printf(LogWARN, "ncpaddr_aton: %s: Only 32 bits allowed\n", data);
    return 0;
  }

#ifndef NOINET6
  if (range.ncprange_family == AF_INET6 && range.ncprange_ip6width != 128 &&
      !IN6_IS_ADDR_UNSPECIFIED(&range.ncprange_ip6addr)) {
    log_Printf(LogWARN, "ncpaddr_aton: %s: Only 128 bits allowed\n", data);
    return 0;
  }
#endif

  switch (range.ncprange_family) {
  case AF_INET:
    addr->ncpaddr_family = range.ncprange_family;
    addr->ncpaddr_ip4addr = range.ncprange_ip4addr;
    return 1;

#ifndef NOINET6
  case AF_INET6:
    addr->ncpaddr_family = range.ncprange_family;
    addr->ncpaddr_ip6addr = range.ncprange_ip6addr;
    return 1;
#endif
  }

  return 0;
}

void
ncprange_init(struct ncprange *range)
{
  range->ncprange_family = AF_UNSPEC;
}

int
ncprange_isset(const struct ncprange *range)
{
  return range->ncprange_family != AF_UNSPEC;
}

int
ncprange_equal(const struct ncprange *range, const struct ncprange *cmp)
{
  if (range->ncprange_family != cmp->ncprange_family)
    return 0;

  switch (range->ncprange_family) {
  case AF_INET:
    if (range->ncprange_ip4addr.s_addr != cmp->ncprange_ip4addr.s_addr)
      return 0;
    return range->ncprange_ip4mask.s_addr == cmp->ncprange_ip4mask.s_addr;

#ifndef NOINET6
  case AF_INET6:
    if (range->ncprange_ip6width != cmp->ncprange_ip6width)
      return 0;
    return !memcmp(&range->ncprange_ip6addr, &cmp->ncprange_ip6addr,
                   sizeof range->ncprange_ip6addr);
#endif

  case AF_UNSPEC:
    return 1;
  }

  return 0;
}

int
ncprange_isdefault(const struct ncprange *range)
{
  switch (range->ncprange_family) {
  case AF_INET:
    if (range->ncprange_ip4addr.s_addr == INADDR_ANY)
      return 1;
    break;

#ifndef NOINET6
  case AF_INET6:
    if (range->ncprange_ip6width == 0 &&
        IN6_IS_ADDR_UNSPECIFIED(&range->ncprange_ip6addr))
      return 1;
    break;
#endif
  }

  return 0;
}

void
ncprange_setdefault(struct ncprange *range, int af)
{
  memset(range, '\0', sizeof *range);
  range->ncprange_family = af;
}

int
ncprange_contains(const struct ncprange *range, const struct ncpaddr *addr)
{
#ifndef NOINET6
  const u_char masks[] = { 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff };
  const u_char *addrp, *rangep;
  int bits;
#endif

  if (range->ncprange_family != addr->ncpaddr_family)
    return 0;

  switch (range->ncprange_family) {
  case AF_INET:
    return !((addr->ncpaddr_ip4addr.s_addr ^ range->ncprange_ip4addr.s_addr) &
             range->ncprange_ip4mask.s_addr);

#ifndef NOINET6
  case AF_INET6:
    rangep = (const u_char *)range->ncprange_ip6addr.s6_addr;
    addrp = (const u_char *)addr->ncpaddr_ip6addr.s6_addr;

    for (bits = range->ncprange_ip6width; bits > 0; bits -= 8)
      if ((*addrp++ ^ *rangep++) & masks[bits > 7 ? 7 : bits - 1])
        return 0;

    return 1;
#endif
  }

  return 0;
}

int
ncprange_containsip4(const struct ncprange *range, struct in_addr addr)
{
  switch (range->ncprange_family) {
  case AF_INET:
    return !((addr.s_addr ^ range->ncprange_ip4addr.s_addr) &
             range->ncprange_ip4mask.s_addr);
  }

  return 0;
}

void
ncprange_copy(struct ncprange *range, const struct ncprange *from)
{
  switch (from->ncprange_family) {
  case AF_INET:
    range->ncprange_family = AF_INET;
    range->ncprange_ip4addr = from->ncprange_ip4addr;
    range->ncprange_ip4mask = from->ncprange_ip4mask;
    range->ncprange_ip4width = from->ncprange_ip4width;
    break;

#ifndef NOINET6
  case AF_INET6:
    range->ncprange_family = AF_INET6;
    range->ncprange_ip6addr = from->ncprange_ip6addr;
    range->ncprange_ip6width = from->ncprange_ip6width;
    break;
#endif

  default:
    range->ncprange_family = AF_UNSPEC;
  }
}

void
ncprange_set(struct ncprange *range, const struct ncpaddr *addr, int width)
{
  ncprange_sethost(range, addr);
  ncprange_setwidth(range, width);
}

void
ncprange_sethost(struct ncprange *range, const struct ncpaddr *from)
{
  switch (from->ncpaddr_family) {
  case AF_INET:
    range->ncprange_family = AF_INET;
    range->ncprange_ip4addr = from->ncpaddr_ip4addr;
    if (from->ncpaddr_ip4addr.s_addr == INADDR_ANY) {
      range->ncprange_ip4mask.s_addr = INADDR_ANY;
      range->ncprange_ip4width = 0;
    } else {
      range->ncprange_ip4mask.s_addr = INADDR_BROADCAST;
      range->ncprange_ip4width = 32;
    }
    break;

#ifndef NOINET6
  case AF_INET6:
    range->ncprange_family = AF_INET6;
    range->ncprange_ip6addr = from->ncpaddr_ip6addr;
    range->ncprange_ip6width = 128;
    break;
#endif

  default:
    range->ncprange_family = AF_UNSPEC;
  }
}

int
ncprange_ishost(const struct ncprange *range)
{
  switch (range->ncprange_family) {
  case AF_INET:
    return range->ncprange_ip4width == 32;
#ifndef NOINET6
  case AF_INET6:
    return range->ncprange_ip6width == 128;
#endif
  }

  return (0);
}

int
ncprange_setwidth(struct ncprange *range, int width)
{
  switch (range->ncprange_family) {
  case AF_INET:
    if (width < 0 || width > 32)
      break;
    range->ncprange_ip4width = width;
    range->ncprange_ip4mask = bits2mask4(width);
    break;

#ifndef NOINET6
  case AF_INET6:
    if (width < 0 || width > 128)
      break;
    range->ncprange_ip6width = width;
    break;
#endif

  case AF_UNSPEC:
    return 1;
  }

  return 0;
}

void
ncprange_setip4host(struct ncprange *range, struct in_addr from)
{
  range->ncprange_family = AF_INET;
  range->ncprange_ip4addr = from;
  if (from.s_addr == INADDR_ANY) {
    range->ncprange_ip4mask.s_addr = INADDR_ANY;
    range->ncprange_ip4width = 0;
  } else {
    range->ncprange_ip4mask.s_addr = INADDR_BROADCAST;
    range->ncprange_ip4width = 32;
  }
}

void
ncprange_setip4(struct ncprange *range, struct in_addr from, struct in_addr msk)
{
  range->ncprange_family = AF_INET;
  range->ncprange_ip4addr = from;
  range->ncprange_ip4mask = msk;
  range->ncprange_ip4width = mask42bits(msk);
}


int
ncprange_setip4mask(struct ncprange *range, struct in_addr mask)
{
  if (range->ncprange_family != AF_INET)
    return 0;
  range->ncprange_ip4mask = mask;
  range->ncprange_ip4width = mask42bits(mask);
  return 1;
}

void
ncprange_setsa(struct ncprange *range, const struct sockaddr *host,
               const struct sockaddr *mask)
{
  const struct sockaddr_in *host4 = (const struct sockaddr_in *)host;
  const struct sockaddr_in *mask4 = (const struct sockaddr_in *)mask;
#ifndef NOINET6
  const struct sockaddr_in6 *host6 = (const struct sockaddr_in6 *)host;
  const struct sockaddr_in6 *mask6 = (const struct sockaddr_in6 *)mask;
#endif

  switch (host->sa_family) {
  case AF_INET:
    range->ncprange_family = AF_INET;
    range->ncprange_ip4addr = host4->sin_addr;
    if (host4->sin_addr.s_addr == INADDR_ANY) {
      range->ncprange_ip4mask.s_addr = INADDR_ANY;
      range->ncprange_ip4width = 0;
    } else if (mask4 && mask4->sin_family == AF_INET) {
      range->ncprange_ip4mask.s_addr = mask4->sin_addr.s_addr;
      range->ncprange_ip4width = mask42bits(mask4->sin_addr);
    } else {
      range->ncprange_ip4mask.s_addr = INADDR_BROADCAST;
      range->ncprange_ip4width = 32;
    }
    break;

#ifndef NOINET6
  case AF_INET6:
    range->ncprange_family = AF_INET6;
    range->ncprange_ip6addr = host6->sin6_addr;
    if (IN6_IS_ADDR_UNSPECIFIED(&host6->sin6_addr))
      range->ncprange_ip6width = 0;
    else
      range->ncprange_ip6width = mask6 ? mask62bits(&mask6->sin6_addr) : 128;
    break;
#endif

  default:
    range->ncprange_family = AF_UNSPEC;
  }
}

void
ncprange_getsa(const struct ncprange *range, struct sockaddr_storage *host,
               struct sockaddr_storage *mask)
{
  struct sockaddr_in *host4 = (struct sockaddr_in *)host;
  struct sockaddr_in *mask4 = (struct sockaddr_in *)mask;
#ifndef NOINET6
  struct sockaddr_in6 *host6 = (struct sockaddr_in6 *)host;
  struct sockaddr_in6 *mask6 = (struct sockaddr_in6 *)mask;
#endif

  memset(host, '\0', sizeof(*host));
  if (mask)
    memset(mask, '\0', sizeof(*mask));

  switch (range->ncprange_family) {
  case AF_INET:
    host4->sin_family = AF_INET;
    host4->sin_len = sizeof(*host4);
    host4->sin_addr = range->ncprange_ip4addr;
    if (mask4) {
      mask4->sin_family = AF_INET;
      mask4->sin_len = sizeof(*host4);
      mask4->sin_addr = range->ncprange_ip4mask;
    }
    break;

#ifndef NOINET6
  case AF_INET6:
    host6->sin6_family = AF_INET6;
    host6->sin6_len = sizeof(*host6);
    host6->sin6_addr = range->ncprange_ip6addr;
    if (mask6) {
      mask6->sin6_family = AF_INET6;
      mask6->sin6_len = sizeof(*host6);
      mask6->sin6_addr = bits2mask6(range->ncprange_ip6width);
    }
    break;
#endif

  default:
    host->ss_family = AF_UNSPEC;
    if (mask)
      mask->ss_family = AF_UNSPEC;
    break;
  }
}

int
ncprange_getaddr(const struct ncprange *range, struct ncpaddr *addr)
{
  switch (range->ncprange_family) {
  case AF_INET:
    addr->ncpaddr_family = AF_INET;
    addr->ncpaddr_ip4addr = range->ncprange_ip4addr;
    return 1;
#ifndef NOINET6
  case AF_INET6:
    addr->ncpaddr_family = AF_INET6;
    addr->ncpaddr_ip6addr =  range->ncprange_ip6addr;
    return 1;
#endif
  }

  return 0;
}

int
ncprange_getip4addr(const struct ncprange *range, struct in_addr *addr)
{
  if (range->ncprange_family != AF_INET)
    return 0;

  *addr = range->ncprange_ip4addr;
  return 1;
}

int
ncprange_getip4mask(const struct ncprange *range, struct in_addr *mask)
{
  switch (range->ncprange_family) {
  case AF_INET:
    *mask = range->ncprange_ip4mask;
    return 1;
  }

  return 0;
}

int
ncprange_getwidth(const struct ncprange *range, int *width)
{
  switch (range->ncprange_family) {
  case AF_INET:
    *width = range->ncprange_ip4width;
    return 1;
#ifndef NOINET6
  case AF_INET6:
    *width = range->ncprange_ip6width;
    return 1;
#endif
  }

  return 0;
}

const char *
ncprange_ntoa(const struct ncprange *range)
{
  char *res;
  struct ncpaddr addr;
  int len;

  if (!ncprange_getaddr(range, &addr))
    return "<AF_UNSPEC>";

  res = ncpaddr_ntowa(&addr);
  len = strlen(res);
  if (len >= NCP_ASCIIBUFFERSIZE - 1)
    return res;

  switch (range->ncprange_family) {
  case AF_INET:
    if (range->ncprange_ip4width == -1) {
      /* A non-contiguous mask */
      for (; len >= 3; res[len -= 2] = '\0')
        if (strcmp(res + len - 2, ".0"))
          break;
      snprintf(res + len, sizeof res - len, "&0x%08lx",
               (unsigned long)ntohl(range->ncprange_ip4mask.s_addr));
    } else if (range->ncprange_ip4width < 32)
      snprintf(res + len, sizeof res - len, "/%d", range->ncprange_ip4width);

    return res;

#ifndef NOINET6
  case AF_INET6:
    if (range->ncprange_ip6width != 128)
      snprintf(res + len, sizeof res - len, "/%d", range->ncprange_ip6width);

    return res;
#endif
  }

  return "<AF_UNSPEC>";
}

#ifndef NOINET6
int
ncprange_scopeid(const struct ncprange *range)
{
  const struct in6_addr *sin6;
  int scopeid = -1;

  if (range->ncprange_family == AF_INET6) {
    sin6 = &range->ncprange_ip6addr;
    if (IN6_IS_ADDR_LINKLOCAL(sin6) || IN6_IS_ADDR_MC_LINKLOCAL(sin6))
      if ((scopeid = ntohs(*(const u_short *)&sin6->s6_addr[2])) == 0)
        scopeid = -1;
  }

  return scopeid;
}
#endif

int
ncprange_aton(struct ncprange *range, struct ncp *ncp, const char *data)
{
  int bits, len;
  char *wp;
  const char *cp;
  char *s;

  len = strcspn(data, "/");

  if (ncp && strncasecmp(data, "HISADDR", len) == 0) {
    range->ncprange_family = AF_INET;
    range->ncprange_ip4addr = ncp->ipcp.peer_ip;
    range->ncprange_ip4mask.s_addr = INADDR_BROADCAST;
    range->ncprange_ip4width = 32;
    return 1;
#ifndef NOINET6
  } else if (ncp && strncasecmp(data, "HISADDR6", len) == 0) {
    range->ncprange_family = AF_INET6;
    range->ncprange_ip6addr = ncp->ipv6cp.hisaddr.ncpaddr_ip6addr;
    range->ncprange_ip6width = 128;
    return 1;
#endif
  } else if (ncp && strncasecmp(data, "MYADDR", len) == 0) {
    range->ncprange_family = AF_INET;
    range->ncprange_ip4addr = ncp->ipcp.my_ip;
    range->ncprange_ip4mask.s_addr = INADDR_BROADCAST;
    range->ncprange_ip4width = 32;
    return 1;
#ifndef NOINET6
  } else if (ncp && strncasecmp(data, "MYADDR6", len) == 0) {
    range->ncprange_family = AF_INET6;
    range->ncprange_ip6addr = ncp->ipv6cp.myaddr.ncpaddr_ip6addr;
    range->ncprange_ip6width = 128;
    return 1;
#endif
  } else if (ncp && strncasecmp(data, "DNS0", len) == 0) {
    range->ncprange_family = AF_INET;
    range->ncprange_ip4addr = ncp->ipcp.ns.dns[0];
    range->ncprange_ip4mask.s_addr = INADDR_BROADCAST;
    range->ncprange_ip4width = 32;
    return 1;
  } else if (ncp && strncasecmp(data, "DNS1", len) == 0) {
    range->ncprange_family = AF_INET;
    range->ncprange_ip4addr = ncp->ipcp.ns.dns[1];
    range->ncprange_ip4mask.s_addr = INADDR_BROADCAST;
    range->ncprange_ip4width = 32;
    return 1;
  }

  s = (char *)alloca(len + 1);
  strncpy(s, data, len);
  s[len] = '\0';
  bits = -1;

  if (data[len] != '\0') {
    bits = strtol(data + len + 1, &wp, 0);
    if (*wp || wp == data + len + 1 || bits < 0 || bits > 128) {
      log_Printf(LogWARN, "ncprange_aton: bad mask width.\n");
      return 0;
    }
  }

  if ((cp = strchr(data, ':')) == NULL) {
    range->ncprange_family = AF_INET;

    range->ncprange_ip4addr = GetIpAddr(s);

    if (range->ncprange_ip4addr.s_addr == INADDR_NONE) {
      log_Printf(LogWARN, "ncprange_aton: %s: Bad address\n", s);
      return 0;
    }

    if (range->ncprange_ip4addr.s_addr == INADDR_ANY) {
      range->ncprange_ip4mask.s_addr = INADDR_ANY;
      range->ncprange_ip4width = 0;
    } else if (bits == -1) {
      range->ncprange_ip4mask.s_addr = INADDR_BROADCAST;
      range->ncprange_ip4width = 32;
    } else if (bits > 32) {
      log_Printf(LogWARN, "ncprange_aton: bad mask width.\n");
      return 0;
    } else {
      range->ncprange_ip4mask = bits2mask4(bits);
      range->ncprange_ip4width = bits;
    }

    return 1;
#ifndef NOINET6
  } else if (strchr(cp + 1, ':') != NULL) {
    range->ncprange_family = AF_INET6;

    if (inet_pton(AF_INET6, s, &range->ncprange_ip6addr) != 1) {
      log_Printf(LogWARN, "ncprange_aton: %s: Bad address\n", s);
      return 0;
    }

    if (IN6_IS_ADDR_UNSPECIFIED(&range->ncprange_ip6addr))
      range->ncprange_ip6width = 0;
    else
      range->ncprange_ip6width = (bits == -1) ? 128 : bits;
    return 1;
#endif
  }

  return 0;
}

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 1999 Internet Business Solutions Ltd., Switzerland
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
 *
 */

#include <stdint.h>
#include <sys/param.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <net/route.h>

#ifdef LOCALRAD
#include "radlib.h"
#include "radlib_vs.h"
#else
#include <radlib.h>
#include <radlib_vs.h>
#endif

#include <errno.h>
#ifndef NODES
#include <md5.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <netdb.h>

#include "layer.h"
#include "defs.h"
#include "log.h"
#include "descriptor.h"
#include "prompt.h"
#include "timer.h"
#include "fsm.h"
#include "iplist.h"
#include "slcompress.h"
#include "throughput.h"
#include "lqr.h"
#include "hdlc.h"
#include "mbuf.h"
#include "ncpaddr.h"
#include "ip.h"
#include "ipcp.h"
#include "ipv6cp.h"
#include "route.h"
#include "command.h"
#include "filter.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "mp.h"
#include "radius.h"
#include "auth.h"
#include "async.h"
#include "physical.h"
#include "chat.h"
#include "cbcp.h"
#include "chap.h"
#include "datalink.h"
#include "ncp.h"
#include "bundle.h"
#include "proto.h"
#include "iface.h"

#ifndef NODES
struct mschap_response {
  u_char ident;
  u_char flags;
  u_char lm_response[24];
  u_char nt_response[24];
};

struct mschap2_response {
  u_char ident;
  u_char flags;
  u_char pchallenge[16];
  u_char reserved[8];
  u_char response[24];
};

#define	AUTH_LEN	16
#define	SALT_LEN	2
#endif

static const char *
radius_policyname(int policy)
{
  switch(policy) {
  case MPPE_POLICY_ALLOWED:
    return "Allowed";
  case MPPE_POLICY_REQUIRED:
    return "Required";
  }
  return NumStr(policy, NULL, 0);
}

static const char *
radius_typesname(int types)
{
  switch(types) {
  case MPPE_TYPE_40BIT:
    return "40 bit";
  case MPPE_TYPE_128BIT:
    return "128 bit";
  case MPPE_TYPE_40BIT|MPPE_TYPE_128BIT:
    return "40 or 128 bit";
  }
  return NumStr(types, NULL, 0);
}

#ifndef NODES
static void
demangle(struct radius *r, const void *mangled, size_t mlen,
         char **buf, size_t *len)
{
  char R[AUTH_LEN];		/* variable names as per rfc2548 */
  const char *S;
  u_char b[16];
  const u_char *A, *C;
  MD5_CTX Context;
  int Slen, i, Clen, Ppos;
  u_char *P;

  if (mlen % 16 != SALT_LEN) {
    log_Printf(LogWARN, "Cannot interpret mangled data of length %ld\n",
               (u_long)mlen);
    *buf = NULL;
    *len = 0;
    return;
  }

  /* We need the RADIUS Request-Authenticator */
  if (rad_request_authenticator(r->cx.rad, R, sizeof R) != AUTH_LEN) {
    log_Printf(LogWARN, "Cannot obtain the RADIUS request authenticator\n");
    *buf = NULL;
    *len = 0;
    return;
  }

  A = (const u_char *)mangled;			/* Salt comes first */
  C = (const u_char *)mangled + SALT_LEN;	/* Then the ciphertext */
  Clen = mlen - SALT_LEN;
  S = rad_server_secret(r->cx.rad);		/* We need the RADIUS secret */
  Slen = strlen(S);
  P = alloca(Clen);				/* We derive our plaintext */

  MD5Init(&Context);
  MD5Update(&Context, S, Slen);
  MD5Update(&Context, R, AUTH_LEN);
  MD5Update(&Context, A, SALT_LEN);
  MD5Final(b, &Context);
  Ppos = 0;

  while (Clen) {
    Clen -= 16;

    for (i = 0; i < 16; i++)
      P[Ppos++] = C[i] ^ b[i];

    if (Clen) {
      MD5Init(&Context);
      MD5Update(&Context, S, Slen);
      MD5Update(&Context, C, 16);
      MD5Final(b, &Context);
    }

    C += 16;
  }

  /*
   * The resulting plain text consists of a one-byte length, the text and
   * maybe some padding.
   */
  *len = *P;
  if (*len > mlen - 1) {
    log_Printf(LogWARN, "Mangled data seems to be garbage\n");
    *buf = NULL;
    *len = 0;
    return;
  }

  if ((*buf = malloc(*len)) == NULL) {
    log_Printf(LogWARN, "demangle: Out of memory (%lu bytes)\n", (u_long)*len);
    *len = 0;
  } else
    memcpy(*buf, P + 1, *len);
}
#endif

/* XXX: This should go into librarius. */
#ifndef NOINET6
static uint8_t *
rad_cvt_ipv6prefix(const void *data, size_t len)
{
	const size_t ipv6len = sizeof(struct in6_addr) + 2;
	uint8_t *s;

	if (len > ipv6len)
		return NULL;
	s = malloc(ipv6len);
	if (s != NULL) {
		memset(s, 0, ipv6len);
		memcpy(s, data, len);
	}
	return s;
}
#endif

/*
 * rad_continue_send_request() has given us `got' (non-zero).  Deal with it.
 */
static void
radius_Process(struct radius *r, int got)
{
  char *argv[MAXARGS], *nuke;
  struct bundle *bundle;
  int argc, addrs, res, width;
  size_t len;
  struct ncprange dest;
  struct ncpaddr gw;
  const void *data;
  const char *stype;
  u_int32_t ipaddr, vendor;
  struct in_addr ip;
#ifndef NOINET6
  uint8_t ipv6addr[INET6_ADDRSTRLEN];
  struct in6_addr ip6;
#endif

  r->cx.fd = -1;		/* Stop select()ing */
  stype = r->cx.auth ? "auth" : "acct";

  switch (got) {
    case RAD_ACCESS_ACCEPT:
      log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		 "Radius(%s): ACCEPT received\n", stype);
      if (!r->cx.auth) {
        rad_close(r->cx.rad);
        return;
      }
      break;

    case RAD_ACCESS_REJECT:
      log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		 "Radius(%s): REJECT received\n", stype);
      if (!r->cx.auth) {
        rad_close(r->cx.rad);
        return;
      }
      break;

    case RAD_ACCESS_CHALLENGE:
      /* we can't deal with this (for now) ! */
      log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		 "Radius: CHALLENGE received (can't handle yet)\n");
      if (r->cx.auth)
        auth_Failure(r->cx.auth);
      rad_close(r->cx.rad);
      return;

    case RAD_ACCOUNTING_RESPONSE:
      /*
       * It's probably not ideal to log this at PHASE level as we'll see
       * too much stuff going to the log when ``set rad_alive'' is used.
       * So we differ from older behaviour (ppp version 3.1 and before)
       * and just log accounting responses to LogRADIUS.
       */
      log_Printf(LogRADIUS, "Radius(%s): Accounting response received\n",
		 stype);
      if (r->cx.auth)
        auth_Failure(r->cx.auth);		/* unexpected !!! */

      /* No further processing for accounting requests, please */
      rad_close(r->cx.rad);
      return;

    case -1:
      log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		 "radius(%s): %s\n", stype, rad_strerror(r->cx.rad));
      if (r->cx.auth)
        auth_Failure(r->cx.auth);
      rad_close(r->cx.rad);
      return;

    default:
      log_Printf(LogERROR, "rad_send_request(%s): Failed %d: %s\n", stype,
                 got, rad_strerror(r->cx.rad));
      if (r->cx.auth)
        auth_Failure(r->cx.auth);
      rad_close(r->cx.rad);
      return;
  }

  /* Let's see what we've got in our reply */
  r->ip.s_addr = r->mask.s_addr = INADDR_NONE;
  r->mtu = 0;
  r->vj = 0;
  while ((res = rad_get_attr(r->cx.rad, &data, &len)) > 0) {
    switch (res) {
      case RAD_FRAMED_IP_ADDRESS:
        r->ip = rad_cvt_addr(data);
	log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		   " IP %s\n", inet_ntoa(r->ip));
        break;

      case RAD_FILTER_ID:
        free(r->filterid);
        if ((r->filterid = rad_cvt_string(data, len)) == NULL) {
          log_Printf(LogERROR, "rad_cvt_string: %s\n", rad_strerror(r->cx.rad));
          auth_Failure(r->cx.auth);
          rad_close(r->cx.rad);
          return;
        }
	log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		   " Filter \"%s\"\n", r->filterid);
        break;

      case RAD_SESSION_TIMEOUT:
        r->sessiontime = rad_cvt_int(data);
	log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		   " Session-Timeout %lu\n", r->sessiontime);
        break;

      case RAD_FRAMED_IP_NETMASK:
        r->mask = rad_cvt_addr(data);
	log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		   " Netmask %s\n", inet_ntoa(r->mask));
        break;

      case RAD_FRAMED_MTU:
        r->mtu = rad_cvt_int(data);
	log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		   " MTU %lu\n", r->mtu);
        break;

      case RAD_FRAMED_ROUTING:
        /* Disabled for now - should we automatically set up some filters ? */
        /* rad_cvt_int(data); */
        /* bit 1 = Send routing packets */
        /* bit 2 = Receive routing packets */
        break;

      case RAD_FRAMED_COMPRESSION:
        r->vj = rad_cvt_int(data) == 1 ? 1 : 0;
	log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		   " VJ %sabled\n", r->vj ? "en" : "dis");
        break;

      case RAD_FRAMED_ROUTE:
        /*
         * We expect a string of the format ``dest[/bits] gw [metrics]''
         * Any specified metrics are ignored.  MYADDR and HISADDR are
         * understood for ``dest'' and ``gw'' and ``0.0.0.0'' is the same
         * as ``HISADDR''.
         */

        if ((nuke = rad_cvt_string(data, len)) == NULL) {
          log_Printf(LogERROR, "rad_cvt_string: %s\n", rad_strerror(r->cx.rad));
          auth_Failure(r->cx.auth);
          rad_close(r->cx.rad);
          return;
        }

	log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		   " Route: %s\n", nuke);
        bundle = r->cx.auth->physical->dl->bundle;
        ip.s_addr = INADDR_ANY;
        ncpaddr_setip4(&gw, ip);
        ncprange_setip4host(&dest, ip);
        argc = command_Interpret(nuke, strlen(nuke), argv);
        if (argc < 0)
          log_Printf(LogWARN, "radius: %s: Syntax error\n",
                     argc == 1 ? argv[0] : "\"\"");
        else if (argc < 2)
          log_Printf(LogWARN, "radius: %s: Invalid route\n",
                     argc == 1 ? argv[0] : "\"\"");
        else if ((strcasecmp(argv[0], "default") != 0 &&
                  !ncprange_aton(&dest, &bundle->ncp, argv[0])) ||
                 !ncpaddr_aton(&gw, &bundle->ncp, argv[1]))
          log_Printf(LogWARN, "radius: %s %s: Invalid route\n",
                     argv[0], argv[1]);
        else {
          ncprange_getwidth(&dest, &width);
          if (width == 32 && strchr(argv[0], '/') == NULL) {
            /* No mask specified - use the natural mask */
            ncprange_getip4addr(&dest, &ip);
            ncprange_setip4mask(&dest, addr2mask(ip));
          }
          addrs = 0;

          if (!strncasecmp(argv[0], "HISADDR", 7))
            addrs = ROUTE_DSTHISADDR;
          else if (!strncasecmp(argv[0], "MYADDR", 6))
            addrs = ROUTE_DSTMYADDR;

          if (ncpaddr_getip4addr(&gw, &ipaddr) && ipaddr == INADDR_ANY) {
            addrs |= ROUTE_GWHISADDR;
            ncpaddr_setip4(&gw, bundle->ncp.ipcp.peer_ip);
          } else if (strcasecmp(argv[1], "HISADDR") == 0)
            addrs |= ROUTE_GWHISADDR;

          route_Add(&r->routes, addrs, &dest, &gw);
        }
        free(nuke);
        break;

      case RAD_REPLY_MESSAGE:
        free(r->repstr);
        if ((r->repstr = rad_cvt_string(data, len)) == NULL) {
          log_Printf(LogERROR, "rad_cvt_string: %s\n", rad_strerror(r->cx.rad));
          auth_Failure(r->cx.auth);
          rad_close(r->cx.rad);
          return;
        }
	log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		   " Reply-Message \"%s\"\n", r->repstr);
        break;

#ifndef NOINET6
      case RAD_FRAMED_IPV6_PREFIX:
	free(r->ipv6prefix);
	if ((r->ipv6prefix = rad_cvt_ipv6prefix(data, len)) == NULL) {
	  log_Printf(LogERROR, "rad_cvt_ipv6prefix: %s\n",
		     "Malformed attribute in response");
	  auth_Failure(r->cx.auth);
	  rad_close(r->cx.rad);
	  return;
	}
	inet_ntop(AF_INET6, &r->ipv6prefix[2], ipv6addr, sizeof(ipv6addr));
	log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		   " IPv6 %s/%d\n", ipv6addr, r->ipv6prefix[1]);
        break;

      case RAD_FRAMED_IPV6_ROUTE:
        /*
         * We expect a string of the format ``dest[/bits] gw [metrics]''
         * Any specified metrics are ignored.  MYADDR6 and HISADDR6 are
         * understood for ``dest'' and ``gw'' and ``::'' is the same
         * as ``HISADDR6''.
         */

        if ((nuke = rad_cvt_string(data, len)) == NULL) {
          log_Printf(LogERROR, "rad_cvt_string: %s\n", rad_strerror(r->cx.rad));
          auth_Failure(r->cx.auth);
          rad_close(r->cx.rad);
          return;
        }

	log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
		   " IPv6 Route: %s\n", nuke);
        bundle = r->cx.auth->physical->dl->bundle;
	ncpaddr_setip6(&gw, &in6addr_any);
	ncprange_set(&dest, &gw, 0);
        argc = command_Interpret(nuke, strlen(nuke), argv);
        if (argc < 0)
          log_Printf(LogWARN, "radius: %s: Syntax error\n",
                     argc == 1 ? argv[0] : "\"\"");
        else if (argc < 2)
          log_Printf(LogWARN, "radius: %s: Invalid route\n",
                     argc == 1 ? argv[0] : "\"\"");
        else if ((strcasecmp(argv[0], "default") != 0 &&
                  !ncprange_aton(&dest, &bundle->ncp, argv[0])) ||
                 !ncpaddr_aton(&gw, &bundle->ncp, argv[1]))
          log_Printf(LogWARN, "radius: %s %s: Invalid route\n",
                     argv[0], argv[1]);
        else {
          addrs = 0;

          if (!strncasecmp(argv[0], "HISADDR6", 8))
            addrs = ROUTE_DSTHISADDR6;
          else if (!strncasecmp(argv[0], "MYADDR6", 7))
            addrs = ROUTE_DSTMYADDR6;

          if (ncpaddr_getip6(&gw, &ip6) && IN6_IS_ADDR_UNSPECIFIED(&ip6)) {
            addrs |= ROUTE_GWHISADDR6;
            ncpaddr_copy(&gw, &bundle->ncp.ipv6cp.hisaddr);
          } else if (strcasecmp(argv[1], "HISADDR6") == 0)
            addrs |= ROUTE_GWHISADDR6;

          route_Add(&r->ipv6routes, addrs, &dest, &gw);
        }
        free(nuke);
        break;
#endif

      case RAD_VENDOR_SPECIFIC:
        if ((res = rad_get_vendor_attr(&vendor, &data, &len)) <= 0) {
          log_Printf(LogERROR, "rad_get_vendor_attr: %s (failing!)\n",
                     rad_strerror(r->cx.rad));
          auth_Failure(r->cx.auth);
          rad_close(r->cx.rad);
          return;
        }

	switch (vendor) {
          case RAD_VENDOR_MICROSOFT:
            switch (res) {
#ifndef NODES
              case RAD_MICROSOFT_MS_CHAP_ERROR:
                free(r->errstr);
                if (len == 0)
                  r->errstr = NULL;
                else {
                  if (len < 3 || ((const char *)data)[1] != '=') {
                    /*
                     * Only point at the String field if we don't think the
                     * peer has misformatted the response.
                     */
                    data = (const char *)data + 1;
                    len--;
                  } else
                    log_Printf(LogWARN, "Warning: The MS-CHAP-Error "
                               "attribute is mis-formatted.  Compensating\n");
                  if ((r->errstr = rad_cvt_string((const char *)data,
                                                  len)) == NULL) {
                    log_Printf(LogERROR, "rad_cvt_string: %s\n",
                               rad_strerror(r->cx.rad));
                    auth_Failure(r->cx.auth);
                    rad_close(r->cx.rad);
                    return;
                  }
		  log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
			     " MS-CHAP-Error \"%s\"\n", r->errstr);
                }
                break;

              case RAD_MICROSOFT_MS_CHAP2_SUCCESS:
                free(r->msrepstr);
                if (len == 0)
                  r->msrepstr = NULL;
                else {
                  if (len < 3 || ((const char *)data)[1] != '=') {
                    /*
                     * Only point at the String field if we don't think the
                     * peer has misformatted the response.
                     */
                    data = (const char *)data + 1;
                    len--;
                  } else
                    log_Printf(LogWARN, "Warning: The MS-CHAP2-Success "
                               "attribute is mis-formatted.  Compensating\n");
                  if ((r->msrepstr = rad_cvt_string((const char *)data,
                                                    len)) == NULL) {
                    log_Printf(LogERROR, "rad_cvt_string: %s\n",
                               rad_strerror(r->cx.rad));
                    auth_Failure(r->cx.auth);
                    rad_close(r->cx.rad);
                    return;
                  }
		  log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
			     " MS-CHAP2-Success \"%s\"\n", r->msrepstr);
                }
                break;

              case RAD_MICROSOFT_MS_MPPE_ENCRYPTION_POLICY:
                r->mppe.policy = rad_cvt_int(data);
		log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
			   " MS-MPPE-Encryption-Policy %s\n",
                           radius_policyname(r->mppe.policy));
                break;

              case RAD_MICROSOFT_MS_MPPE_ENCRYPTION_TYPES:
                r->mppe.types = rad_cvt_int(data);
		log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
			   " MS-MPPE-Encryption-Types %s\n",
                           radius_typesname(r->mppe.types));
                break;

              case RAD_MICROSOFT_MS_MPPE_RECV_KEY:
                free(r->mppe.recvkey);
		demangle(r, data, len, &r->mppe.recvkey, &r->mppe.recvkeylen);
		log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
			   " MS-MPPE-Recv-Key ********\n");
                break;

              case RAD_MICROSOFT_MS_MPPE_SEND_KEY:
		demangle(r, data, len, &r->mppe.sendkey, &r->mppe.sendkeylen);
		log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
			   " MS-MPPE-Send-Key ********\n");
                break;
#endif

              default:
                log_Printf(LogDEBUG, "Dropping MICROSOFT vendor specific "
                           "RADIUS attribute %d\n", res);
                break;
            }
            break;

          default:
            log_Printf(LogDEBUG, "Dropping vendor %lu RADIUS attribute %d\n",
                       (unsigned long)vendor, res);
            break;
        }
        break;

      default:
        log_Printf(LogDEBUG, "Dropping RADIUS attribute %d\n", res);
        break;
    }
  }

  if (res == -1) {
    log_Printf(LogERROR, "rad_get_attr: %s (failing!)\n",
               rad_strerror(r->cx.rad));
    auth_Failure(r->cx.auth);
  } else if (got == RAD_ACCESS_REJECT)
    auth_Failure(r->cx.auth);
  else {
    r->valid = 1;
    auth_Success(r->cx.auth);
  }
  rad_close(r->cx.rad);
}

/*
 * We've either timed out or select()ed on the read descriptor
 */
static void
radius_Continue(struct radius *r, int sel)
{
  struct timeval tv;
  int got;

  timer_Stop(&r->cx.timer);
  if ((got = rad_continue_send_request(r->cx.rad, sel, &r->cx.fd, &tv)) == 0) {
    log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
	       "Radius: Request re-sent\n");
    r->cx.timer.load = tv.tv_usec / TICKUNIT + tv.tv_sec * SECTICKS;
    timer_Start(&r->cx.timer);
    return;
  }

  radius_Process(r, got);
}

/*
 * Time to call rad_continue_send_request() - timed out.
 */
static void
radius_Timeout(void *v)
{
  radius_Continue((struct radius *)v, 0);
}

/*
 * Time to call rad_continue_send_request() - something to read.
 */
static void
radius_Read(struct fdescriptor *d, struct bundle *bundle __unused,
	    const fd_set *fdset __unused)
{
  radius_Continue(descriptor2radius(d), 1);
}

/*
 * Flush any pending transactions
 */
void
radius_Flush(struct radius *r)
{
  struct timeval tv;
  fd_set s;

  while (r->cx.fd != -1) {
    FD_ZERO(&s);
    FD_SET(r->cx.fd, &s);
    tv.tv_sec = 0;
    tv.tv_usec = TICKUNIT;
    select(r->cx.fd + 1, &s, NULL, NULL, &tv);
    radius_Continue(r, 1);
  }
}

/*
 * Behave as a struct fdescriptor (descriptor.h)
 */
static int
radius_UpdateSet(struct fdescriptor *d, fd_set *r, fd_set *w __unused,
		 fd_set *e __unused, int *n)
{
  struct radius *rad = descriptor2radius(d);

  if (r && rad->cx.fd != -1) {
    FD_SET(rad->cx.fd, r);
    if (*n < rad->cx.fd + 1)
      *n = rad->cx.fd + 1;
    log_Printf(LogTIMER, "Radius: fdset(r) %d\n", rad->cx.fd);
    return 1;
  }

  return 0;
}

/*
 * Behave as a struct fdescriptor (descriptor.h)
 */
static int
radius_IsSet(struct fdescriptor *d, const fd_set *fdset)
{
  struct radius *r = descriptor2radius(d);

  return r && r->cx.fd != -1 && FD_ISSET(r->cx.fd, fdset);
}

/*
 * Behave as a struct fdescriptor (descriptor.h)
 */
static int
radius_Write(struct fdescriptor *d __unused, struct bundle *bundle __unused,
	     const fd_set *fdset __unused)
{
  /* We never want to write here ! */
  log_Printf(LogALERT, "radius_Write: Internal error: Bad call !\n");
  return 0;
}

/*
 * Initialise ourselves
 */
void
radius_Init(struct radius *r)
{
  r->desc.type = RADIUS_DESCRIPTOR;
  r->desc.UpdateSet = radius_UpdateSet;
  r->desc.IsSet = radius_IsSet;
  r->desc.Read = radius_Read;
  r->desc.Write = radius_Write;
  r->cx.fd = -1;
  r->cx.rad = NULL;
  memset(&r->cx.timer, '\0', sizeof r->cx.timer);
  r->cx.auth = NULL;
  r->valid = 0;
  r->vj = 0;
  r->ip.s_addr = INADDR_ANY;
  r->mask.s_addr = INADDR_NONE;
  r->routes = NULL;
  r->mtu = DEF_MTU;
  r->msrepstr = NULL;
  r->repstr = NULL;
#ifndef NOINET6
  r->ipv6prefix = NULL;
  r->ipv6routes = NULL;
#endif
  r->errstr = NULL;
  r->mppe.policy = 0;
  r->mppe.types = 0;
  r->mppe.recvkey = NULL;
  r->mppe.recvkeylen = 0;
  r->mppe.sendkey = NULL;
  r->mppe.sendkeylen = 0;
  *r->cfg.file = '\0';
  log_Printf(LogDEBUG, "Radius: radius_Init\n");
}

/*
 * Forget everything and go back to initialised state.
 */
void
radius_Destroy(struct radius *r)
{
  r->valid = 0;
  log_Printf(LogDEBUG, "Radius: radius_Destroy\n");
  timer_Stop(&r->cx.timer);
  route_DeleteAll(&r->routes);
#ifndef NOINET6
  route_DeleteAll(&r->ipv6routes);
#endif
  free(r->filterid);
  r->filterid = NULL;
  free(r->msrepstr);
  r->msrepstr = NULL;
  free(r->repstr);
  r->repstr = NULL;
#ifndef NOINET6
  free(r->ipv6prefix);
  r->ipv6prefix = NULL;
#endif
  free(r->errstr);
  r->errstr = NULL;
  free(r->mppe.recvkey);
  r->mppe.recvkey = NULL;
  r->mppe.recvkeylen = 0;
  free(r->mppe.sendkey);
  r->mppe.sendkey = NULL;
  r->mppe.sendkeylen = 0;
  if (r->cx.fd != -1) {
    r->cx.fd = -1;
    rad_close(r->cx.rad);
  }
}

static int
radius_put_physical_details(struct radius *rad, struct physical *p)
{
  int slot, type;

  type = RAD_VIRTUAL;
  if (p->handler)
    switch (p->handler->type) {
      case I4B_DEVICE:
        type = RAD_ISDN_SYNC;
        break;

      case TTY_DEVICE:
        type = RAD_ASYNC;
        break;

      case ETHER_DEVICE:
        type = RAD_ETHERNET;
        break;

      case TCP_DEVICE:
      case UDP_DEVICE:
      case EXEC_DEVICE:
      case ATM_DEVICE:
      case NG_DEVICE:
        type = RAD_VIRTUAL;
        break;
    }

  if (rad_put_int(rad->cx.rad, RAD_NAS_PORT_TYPE, type) != 0) {
    log_Printf(LogERROR, "rad_put: rad_put_int: %s\n", rad_strerror(rad->cx.rad));
    rad_close(rad->cx.rad);
    return 0;
  }

  switch (rad->port_id_type) {
    case RPI_PID:
      slot = (int)getpid();
      break;
    case RPI_IFNUM:
      slot = p->dl->bundle->iface->index;
      break;
    case RPI_TUNNUM:
      slot = p->dl->bundle->unit;
      break;
    case RPI_DEFAULT:
    default:
      slot = physical_Slot(p);
      break;
  }
  
  if (slot >= 0)
    if (rad_put_int(rad->cx.rad, RAD_NAS_PORT, slot) != 0) {
      log_Printf(LogERROR, "rad_put: rad_put_int: %s\n", rad_strerror(rad->cx.rad));
      rad_close(rad->cx.rad);
      return 0;
    }

  return 1;
}

/*
 * Start an authentication request to the RADIUS server.
 */
int
radius_Authenticate(struct radius *r, struct authinfo *authp, const char *name,
                    const char *key, int klen, const char *nchallenge,
                    int nclen)
{
  char hostname[MAXHOSTNAMELEN];
  struct timeval tv;
  const char *what = "questionable";	/* silence warnings! */
  char *mac_addr;
  int got;
  struct hostent *hp;
  struct in_addr hostaddr;
#ifndef NODES
  struct mschap_response msresp;
  struct mschap2_response msresp2;
  const struct MSCHAPv2_resp *keyv2;
#endif

  if (!*r->cfg.file)
    return 0;

  if (r->cx.fd != -1)
    /*
     * We assume that our name/key/challenge is the same as last time,
     * and just continue to wait for the RADIUS server(s).
     */
    return 1;

  radius_Destroy(r);

  if ((r->cx.rad = rad_auth_open()) == NULL) {
    log_Printf(LogERROR, "rad_auth_open: %s\n", strerror(errno));
    return 0;
  }

  if (rad_config(r->cx.rad, r->cfg.file) != 0) {
    log_Printf(LogERROR, "rad_config: %s\n", rad_strerror(r->cx.rad));
    rad_close(r->cx.rad);
    return 0;
  }

  if (rad_create_request(r->cx.rad, RAD_ACCESS_REQUEST) != 0) {
    log_Printf(LogERROR, "rad_create_request: %s\n", rad_strerror(r->cx.rad));
    rad_close(r->cx.rad);
    return 0;
  }

  if (rad_put_string(r->cx.rad, RAD_USER_NAME, name) != 0 ||
      rad_put_int(r->cx.rad, RAD_SERVICE_TYPE, RAD_FRAMED) != 0 ||
      rad_put_int(r->cx.rad, RAD_FRAMED_PROTOCOL, RAD_PPP) != 0) {
    log_Printf(LogERROR, "rad_put: %s\n", rad_strerror(r->cx.rad));
    rad_close(r->cx.rad);
    return 0;
  }

  switch (authp->physical->link.lcp.want_auth) {
  case PROTO_PAP:
    /* We're talking PAP */
    if (rad_put_attr(r->cx.rad, RAD_USER_PASSWORD, key, klen) != 0) {
      log_Printf(LogERROR, "PAP: rad_put_string: %s\n",
                 rad_strerror(r->cx.rad));
      rad_close(r->cx.rad);
      return 0;
    }
    what = "PAP";
    break;

  case PROTO_CHAP:
    switch (authp->physical->link.lcp.want_authtype) {
    case 0x5:
      if (rad_put_attr(r->cx.rad, RAD_CHAP_PASSWORD, key, klen) != 0 ||
          rad_put_attr(r->cx.rad, RAD_CHAP_CHALLENGE, nchallenge, nclen) != 0) {
        log_Printf(LogERROR, "CHAP: rad_put_string: %s\n",
                   rad_strerror(r->cx.rad));
        rad_close(r->cx.rad);
        return 0;
      }
      what = "CHAP";
      break;

#ifndef NODES
    case 0x80:
      if (klen != 50) {
        log_Printf(LogERROR, "CHAP80: Unrecognised key length %d\n", klen);
        rad_close(r->cx.rad);
        return 0;
      }

      rad_put_vendor_attr(r->cx.rad, RAD_VENDOR_MICROSOFT,
                          RAD_MICROSOFT_MS_CHAP_CHALLENGE, nchallenge, nclen);
      msresp.ident = *key;
      msresp.flags = 0x01;
      memcpy(msresp.lm_response, key + 1, 24);
      memcpy(msresp.nt_response, key + 25, 24);
      rad_put_vendor_attr(r->cx.rad, RAD_VENDOR_MICROSOFT,
                          RAD_MICROSOFT_MS_CHAP_RESPONSE, &msresp,
                          sizeof msresp);
      what = "MSCHAP";
      break;

    case 0x81:
      if (klen != sizeof(*keyv2) + 1) {
        log_Printf(LogERROR, "CHAP81: Unrecognised key length %d\n", klen);
        rad_close(r->cx.rad);
        return 0;
      }

      keyv2 = (const struct MSCHAPv2_resp *)(key + 1);
      rad_put_vendor_attr(r->cx.rad, RAD_VENDOR_MICROSOFT,
                          RAD_MICROSOFT_MS_CHAP_CHALLENGE, nchallenge, nclen);
      msresp2.ident = *key;
      msresp2.flags = keyv2->Flags;
      memcpy(msresp2.response, keyv2->NTResponse, sizeof msresp2.response);
      memset(msresp2.reserved, '\0', sizeof msresp2.reserved);
      memcpy(msresp2.pchallenge, keyv2->PeerChallenge,
             sizeof msresp2.pchallenge);
      rad_put_vendor_attr(r->cx.rad, RAD_VENDOR_MICROSOFT,
                          RAD_MICROSOFT_MS_CHAP2_RESPONSE, &msresp2,
                          sizeof msresp2);
      what = "MSCHAPv2";
      break;
#endif
    default:
      log_Printf(LogERROR, "CHAP: Unrecognised type 0x%02x\n",
                 authp->physical->link.lcp.want_authtype);
      rad_close(r->cx.rad);
      return 0;
    }
  }

  if (gethostname(hostname, sizeof hostname) != 0)
    log_Printf(LogERROR, "rad_put: gethostname(): %s\n", strerror(errno));
  else {
    if (Enabled(authp->physical->dl->bundle, OPT_NAS_IP_ADDRESS) &&
        (hp = gethostbyname(hostname)) != NULL) {
      hostaddr.s_addr = *(u_long *)hp->h_addr;
      if (rad_put_addr(r->cx.rad, RAD_NAS_IP_ADDRESS, hostaddr) != 0) {
        log_Printf(LogERROR, "rad_put: rad_put_string: %s\n",
                   rad_strerror(r->cx.rad));
        rad_close(r->cx.rad);
        return 0;
      }
    }
    if (Enabled(authp->physical->dl->bundle, OPT_NAS_IDENTIFIER) &&
        rad_put_string(r->cx.rad, RAD_NAS_IDENTIFIER, hostname) != 0) {
      log_Printf(LogERROR, "rad_put: rad_put_string: %s\n",
                 rad_strerror(r->cx.rad));
      rad_close(r->cx.rad);
      return 0;
    }
  }

  if ((mac_addr = getenv("HISMACADDR")) != NULL &&
      rad_put_string(r->cx.rad, RAD_CALLING_STATION_ID, mac_addr) != 0) {
    log_Printf(LogERROR, "rad_put: %s\n", rad_strerror(r->cx.rad));
    rad_close(r->cx.rad);
    return 0;
  }

  radius_put_physical_details(r, authp->physical);

  log_Printf(LogRADIUS, "Radius(auth): %s data sent for %s\n", what, name);

  r->cx.auth = authp;
  if ((got = rad_init_send_request(r->cx.rad, &r->cx.fd, &tv)))
    radius_Process(r, got);
  else {
    log_Printf(log_IsKept(LogRADIUS) ? LogRADIUS : LogPHASE,
	       "Radius: Request sent\n");
    log_Printf(LogDEBUG, "Using radius_Timeout [%p]\n", radius_Timeout);
    r->cx.timer.load = tv.tv_usec / TICKUNIT + tv.tv_sec * SECTICKS;
    r->cx.timer.func = radius_Timeout;
    r->cx.timer.name = "radius auth";
    r->cx.timer.arg = r;
    timer_Start(&r->cx.timer);
  }

  return 1;
}

/* Fetch IP, netmask from IPCP */
void
radius_Account_Set_Ip(struct radacct *ac, struct in_addr *peer_ip,
		      struct in_addr *netmask)
{
  ac->proto = PROTO_IPCP;
  memcpy(&ac->peer.ip.addr, peer_ip, sizeof(ac->peer.ip.addr));
  memcpy(&ac->peer.ip.mask, netmask, sizeof(ac->peer.ip.mask));
}

#ifndef NOINET6
/* Fetch interface-id from IPV6CP */
void
radius_Account_Set_Ipv6(struct radacct *ac, u_char *ifid)
{
  ac->proto = PROTO_IPV6CP;
  memcpy(&ac->peer.ipv6.ifid, ifid, sizeof(ac->peer.ipv6.ifid));
}
#endif

/*
 * Send an accounting request to the RADIUS server
 */
void
radius_Account(struct radius *r, struct radacct *ac, struct datalink *dl,
               int acct_type, struct pppThroughput *stats)
{
  struct timeval tv;
  int got;
  char hostname[MAXHOSTNAMELEN];
  char *mac_addr;
  struct hostent *hp;
  struct in_addr hostaddr;

  if (!*r->cfg.file)
    return;

  if (r->cx.fd != -1)
    /*
     * We assume that our name/key/challenge is the same as last time,
     * and just continue to wait for the RADIUS server(s).
     */
    return;

  timer_Stop(&r->cx.timer);

  if ((r->cx.rad = rad_acct_open()) == NULL) {
    log_Printf(LogERROR, "rad_auth_open: %s\n", strerror(errno));
    return;
  }

  if (rad_config(r->cx.rad, r->cfg.file) != 0) {
    log_Printf(LogERROR, "rad_config: %s\n", rad_strerror(r->cx.rad));
    rad_close(r->cx.rad);
    return;
  }

  if (rad_create_request(r->cx.rad, RAD_ACCOUNTING_REQUEST) != 0) {
    log_Printf(LogERROR, "rad_create_request: %s\n", rad_strerror(r->cx.rad));
    rad_close(r->cx.rad);
    return;
  }

  /* Grab some accounting data and initialize structure */
  if (acct_type == RAD_START) {
    ac->rad_parent = r;
    /* Fetch username from datalink */
    strncpy(ac->user_name, dl->peer.authname, sizeof ac->user_name);
    ac->user_name[AUTHLEN-1] = '\0';

    ac->authentic = 2;		/* Assume RADIUS verified auth data */

    /* Generate a session ID */
    snprintf(ac->session_id, sizeof ac->session_id, "%s%ld-%s%lu",
             dl->bundle->cfg.auth.name, (long)getpid(),
             dl->peer.authname, (unsigned long)stats->uptime);

    /* And grab our MP socket name */
    snprintf(ac->multi_session_id, sizeof ac->multi_session_id, "%s",
             dl->bundle->ncp.mp.active ?
             dl->bundle->ncp.mp.server.socket.sun_path : "");
  }

  if (rad_put_string(r->cx.rad, RAD_USER_NAME, ac->user_name) != 0 ||
      rad_put_int(r->cx.rad, RAD_SERVICE_TYPE, RAD_FRAMED) != 0 ||
      rad_put_int(r->cx.rad, RAD_FRAMED_PROTOCOL, RAD_PPP) != 0) {
    log_Printf(LogERROR, "rad_put: %s\n", rad_strerror(r->cx.rad));
    rad_close(r->cx.rad);
    return;
  }
  switch (ac->proto) {
  case PROTO_IPCP:
    if (rad_put_addr(r->cx.rad, RAD_FRAMED_IP_ADDRESS,
		     ac->peer.ip.addr) != 0 ||
	rad_put_addr(r->cx.rad, RAD_FRAMED_IP_NETMASK,
		     ac->peer.ip.mask) != 0) {
      log_Printf(LogERROR, "rad_put: %s\n", rad_strerror(r->cx.rad));
      rad_close(r->cx.rad);
      return;
    }
    break;
#ifndef NOINET6
  case PROTO_IPV6CP:
    if (rad_put_attr(r->cx.rad, RAD_FRAMED_INTERFACE_ID, ac->peer.ipv6.ifid,
		     sizeof(ac->peer.ipv6.ifid)) != 0) {
      log_Printf(LogERROR, "rad_put_attr: %s\n", rad_strerror(r->cx.rad));
      rad_close(r->cx.rad);
      return;
    }
    if (r->ipv6prefix) {
      /*
       * Since PPP doesn't delegate an IPv6 prefix to a peer,
       * Framed-IPv6-Prefix may be not used, actually.
       */
      if (rad_put_attr(r->cx.rad, RAD_FRAMED_IPV6_PREFIX, r->ipv6prefix,
		       sizeof(struct in6_addr) + 2) != 0) {
	log_Printf(LogERROR, "rad_put_attr: %s\n", rad_strerror(r->cx.rad));
	rad_close(r->cx.rad);
	return;
      }
    }
    break;
#endif
  default:
    /* We don't log any protocol specific information */
    break;
  }

  if ((mac_addr = getenv("HISMACADDR")) != NULL &&
      rad_put_string(r->cx.rad, RAD_CALLING_STATION_ID, mac_addr) != 0) {
    log_Printf(LogERROR, "rad_put: %s\n", rad_strerror(r->cx.rad));
    rad_close(r->cx.rad);
    return;
  }

  if (gethostname(hostname, sizeof hostname) != 0)
    log_Printf(LogERROR, "rad_put: gethostname(): %s\n", strerror(errno));
  else {
    if (Enabled(dl->bundle, OPT_NAS_IP_ADDRESS) &&
        (hp = gethostbyname(hostname)) != NULL) {
      hostaddr.s_addr = *(u_long *)hp->h_addr;
      if (rad_put_addr(r->cx.rad, RAD_NAS_IP_ADDRESS, hostaddr) != 0) {
        log_Printf(LogERROR, "rad_put: rad_put_string: %s\n",
                   rad_strerror(r->cx.rad));
        rad_close(r->cx.rad);
        return;
      }
    }
    if (Enabled(dl->bundle, OPT_NAS_IDENTIFIER) &&
        rad_put_string(r->cx.rad, RAD_NAS_IDENTIFIER, hostname) != 0) {
      log_Printf(LogERROR, "rad_put: rad_put_string: %s\n",
                 rad_strerror(r->cx.rad));
      rad_close(r->cx.rad);
      return;
    }
  }

  radius_put_physical_details(r, dl->physical);

  if (rad_put_int(r->cx.rad, RAD_ACCT_STATUS_TYPE, acct_type) != 0 ||
      rad_put_string(r->cx.rad, RAD_ACCT_SESSION_ID, ac->session_id) != 0 ||
      rad_put_string(r->cx.rad, RAD_ACCT_MULTI_SESSION_ID,
                     ac->multi_session_id) != 0 ||
      rad_put_int(r->cx.rad, RAD_ACCT_DELAY_TIME, 0) != 0) {
/* XXX ACCT_DELAY_TIME should be increased each time a packet is waiting */
    log_Printf(LogERROR, "rad_put: %s\n", rad_strerror(r->cx.rad));
    rad_close(r->cx.rad);
    return;
  }

  if (acct_type == RAD_STOP || acct_type == RAD_ALIVE)
    /* Show some statistics */
    if (rad_put_int(r->cx.rad, RAD_ACCT_INPUT_OCTETS, stats->OctetsIn % UINT32_MAX) != 0 ||
        rad_put_int(r->cx.rad, RAD_ACCT_INPUT_GIGAWORDS, stats->OctetsIn / UINT32_MAX) != 0 ||
        rad_put_int(r->cx.rad, RAD_ACCT_INPUT_PACKETS, stats->PacketsIn) != 0 ||
        rad_put_int(r->cx.rad, RAD_ACCT_OUTPUT_OCTETS, stats->OctetsOut % UINT32_MAX) != 0 ||
        rad_put_int(r->cx.rad, RAD_ACCT_OUTPUT_GIGAWORDS, stats->OctetsOut / UINT32_MAX) != 0 ||
        rad_put_int(r->cx.rad, RAD_ACCT_OUTPUT_PACKETS, stats->PacketsOut)
        != 0 ||
        rad_put_int(r->cx.rad, RAD_ACCT_SESSION_TIME, throughput_uptime(stats))
        != 0) {
      log_Printf(LogERROR, "rad_put: %s\n", rad_strerror(r->cx.rad));
      rad_close(r->cx.rad);
      return;
    }

  if (log_IsKept(LogPHASE) || log_IsKept(LogRADIUS)) {
    const char *what;
    int level;

    switch (acct_type) {
    case RAD_START:
      what = "START";
      level = log_IsKept(LogPHASE) ? LogPHASE : LogRADIUS;
      break;
    case RAD_STOP:
      what = "STOP";
      level = log_IsKept(LogPHASE) ? LogPHASE : LogRADIUS;
      break;
    case RAD_ALIVE:
      what = "ALIVE";
      level = LogRADIUS;
      break;
    default:
      what = "<unknown>";
      level = log_IsKept(LogPHASE) ? LogPHASE : LogRADIUS;
      break;
    }
    log_Printf(level, "Radius(acct): %s data sent\n", what);
  }
  
  r->cx.auth = NULL;			/* Not valid for accounting requests */
  if ((got = rad_init_send_request(r->cx.rad, &r->cx.fd, &tv)))
    radius_Process(r, got);
  else {
    log_Printf(LogDEBUG, "Using radius_Timeout [%p]\n", radius_Timeout);
    r->cx.timer.load = tv.tv_usec / TICKUNIT + tv.tv_sec * SECTICKS;
    r->cx.timer.func = radius_Timeout;
    r->cx.timer.name = "radius acct";
    r->cx.timer.arg = r;
    timer_Start(&r->cx.timer);
  }
}

/*
 * How do things look at the moment ?
 */
void
radius_Show(struct radius *r, struct prompt *p)
{
  prompt_Printf(p, " Radius config:     %s",
                *r->cfg.file ? r->cfg.file : "none");
  if (r->valid) {
    prompt_Printf(p, "\n                IP: %s\n", inet_ntoa(r->ip));
    prompt_Printf(p, "           Netmask: %s\n", inet_ntoa(r->mask));
    prompt_Printf(p, "               MTU: %lu\n", r->mtu);
    prompt_Printf(p, "                VJ: %sabled\n", r->vj ? "en" : "dis");
    prompt_Printf(p, "           Message: %s\n", r->repstr ? r->repstr : "");
    prompt_Printf(p, "   MPPE Enc Policy: %s\n",
                  radius_policyname(r->mppe.policy));
    prompt_Printf(p, "    MPPE Enc Types: %s\n",
                  radius_typesname(r->mppe.types));
    prompt_Printf(p, "     MPPE Recv Key: %seceived\n",
                  r->mppe.recvkey ? "R" : "Not r");
    prompt_Printf(p, "     MPPE Send Key: %seceived\n",
                  r->mppe.sendkey ? "R" : "Not r");
    prompt_Printf(p, " MS-CHAP2-Response: %s\n",
                  r->msrepstr ? r->msrepstr : "");
    prompt_Printf(p, "     Error Message: %s\n", r->errstr ? r->errstr : "");
    if (r->routes)
      route_ShowSticky(p, r->routes, "            Routes", 16);
#ifndef NOINET6
    if (r->ipv6routes)
      route_ShowSticky(p, r->ipv6routes, "            IPv6 Routes", 16);
#endif
  } else
    prompt_Printf(p, " (not authenticated)\n");
}

static void
radius_alive(void *v)
{
  struct bundle *bundle = (struct bundle *)v;

  timer_Stop(&bundle->radius.alive.timer);
  bundle->radius.alive.timer.load = bundle->radius.alive.interval * SECTICKS;
  if (bundle->radius.alive.timer.load) {
    radius_Account(&bundle->radius, &bundle->radacct,
                   bundle->links, RAD_ALIVE, &bundle->ncp.ipcp.throughput);
    timer_Start(&bundle->radius.alive.timer);
  }
}

void
radius_StartTimer(struct bundle *bundle)
{
  if (*bundle->radius.cfg.file && bundle->radius.alive.interval) {
    bundle->radius.alive.timer.func = radius_alive;
    bundle->radius.alive.timer.name = "radius alive";
    bundle->radius.alive.timer.load = bundle->radius.alive.interval * SECTICKS;
    bundle->radius.alive.timer.arg = bundle;
    radius_alive(bundle);
  }
}

void
radius_StopTimer(struct radius *r)
{
  timer_Stop(&r->alive.timer);
}

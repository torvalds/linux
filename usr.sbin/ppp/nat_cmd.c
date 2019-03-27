/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Charles Mott <cm@linktel.net>
 *                    Brian Somers <brian@Awfulhak.org>
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

#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#ifdef LOCALNAT
#include "alias.h"
#else
#include <alias.h>
#endif

#include "layer.h"
#include "proto.h"
#include "defs.h"
#include "command.h"
#include "log.h"
#include "nat_cmd.h"
#include "descriptor.h"
#include "prompt.h"
#include "timer.h"
#include "fsm.h"
#include "slcompress.h"
#include "throughput.h"
#include "iplist.h"
#include "mbuf.h"
#include "lqr.h"
#include "hdlc.h"
#include "ncpaddr.h"
#include "ip.h"
#include "ipcp.h"
#include "ipv6cp.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "mp.h"
#include "filter.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ncp.h"
#include "bundle.h"


#define NAT_EXTRABUF (13)

static int StrToAddr(const char *, struct in_addr *);
static int StrToPortRange(const char *, u_short *, u_short *, const char *);
static int StrToAddrAndPort(const char *, struct in_addr *, u_short *,
                            u_short *, const char *);

extern struct libalias *la;

static void
lowhigh(u_short *a, u_short *b)
{
  if (a > b) {
    u_short c;

    c = *b;
    *b = *a;
    *a = c;
  }
}

int
nat_RedirectPort(struct cmdargs const *arg)
{
  if (!arg->bundle->NatEnabled) {
    prompt_Printf(arg->prompt, "Alias not enabled\n");
    return 1;
  } else if (arg->argc == arg->argn + 3 || arg->argc == arg->argn + 4) {
    char proto_constant;
    const char *proto;
    struct in_addr localaddr;
    u_short hlocalport, llocalport;
    struct in_addr aliasaddr;
    u_short haliasport, laliasport;
    struct in_addr remoteaddr;
    u_short hremoteport, lremoteport;
    struct alias_link *link;
    int error;

    proto = arg->argv[arg->argn];
    if (strcmp(proto, "tcp") == 0) {
      proto_constant = IPPROTO_TCP;
    } else if (strcmp(proto, "udp") == 0) {
      proto_constant = IPPROTO_UDP;
    } else {
      prompt_Printf(arg->prompt, "port redirect: protocol must be"
                    " tcp or udp\n");
      return -1;
    }

    error = StrToAddrAndPort(arg->argv[arg->argn+1], &localaddr, &llocalport,
                             &hlocalport, proto);
    if (error) {
      prompt_Printf(arg->prompt, "nat port: error reading localaddr:port\n");
      return -1;
    }

    error = StrToPortRange(arg->argv[arg->argn+2], &laliasport, &haliasport,
                           proto);
    if (error) {
      prompt_Printf(arg->prompt, "nat port: error reading alias port\n");
      return -1;
    }
    aliasaddr.s_addr = INADDR_ANY;

    if (arg->argc == arg->argn + 4) {
      error = StrToAddrAndPort(arg->argv[arg->argn+3], &remoteaddr,
                               &lremoteport, &hremoteport, proto);
      if (error) {
        prompt_Printf(arg->prompt, "nat port: error reading "
                      "remoteaddr:port\n");
        return -1;
      }
    } else {
      remoteaddr.s_addr = INADDR_ANY;
      lremoteport = hremoteport = 0;
    }

    lowhigh(&llocalport, &hlocalport);
    lowhigh(&laliasport, &haliasport);
    lowhigh(&lremoteport, &hremoteport);

    if (haliasport - laliasport != hlocalport - llocalport) {
      prompt_Printf(arg->prompt, "nat port: local & alias port ranges "
                    "are not equal\n");
      return -1;
    }

    if (hremoteport && hremoteport - lremoteport != hlocalport - llocalport) {
      prompt_Printf(arg->prompt, "nat port: local & remote port ranges "
                    "are not equal\n");
      return -1;
    }

    do {
      link = LibAliasRedirectPort(la, localaddr, htons(llocalport),
				     remoteaddr, htons(lremoteport),
                                     aliasaddr, htons(laliasport),
				     proto_constant);

      if (link == NULL) {
        prompt_Printf(arg->prompt, "nat port: %d: error %d\n", laliasport,
                      error);
        return 1;
      }
      llocalport++;
      if (hremoteport)
        lremoteport++;
    } while (laliasport++ < haliasport);

    return 0;
  }

  return -1;
}


int
nat_RedirectAddr(struct cmdargs const *arg)
{
  if (!arg->bundle->NatEnabled) {
    prompt_Printf(arg->prompt, "nat not enabled\n");
    return 1;
  } else if (arg->argc == arg->argn+2) {
    int error;
    struct in_addr localaddr, aliasaddr;
    struct alias_link *link;

    error = StrToAddr(arg->argv[arg->argn], &localaddr);
    if (error) {
      prompt_Printf(arg->prompt, "address redirect: invalid local address\n");
      return 1;
    }
    error = StrToAddr(arg->argv[arg->argn+1], &aliasaddr);
    if (error) {
      prompt_Printf(arg->prompt, "address redirect: invalid alias address\n");
      prompt_Printf(arg->prompt, "usage: nat %s %s\n", arg->cmd->name,
                    arg->cmd->syntax);
      return 1;
    }
    link = LibAliasRedirectAddr(la, localaddr, aliasaddr);
    if (link == NULL) {
      prompt_Printf(arg->prompt, "address redirect: packet aliasing"
                    " engine error\n");
      prompt_Printf(arg->prompt, "usage: nat %s %s\n", arg->cmd->name,
                    arg->cmd->syntax);
    }
  } else
    return -1;

  return 0;
}


int
nat_RedirectProto(struct cmdargs const *arg)
{
  if (!arg->bundle->NatEnabled) {
    prompt_Printf(arg->prompt, "nat not enabled\n");
    return 1;
  } else if (arg->argc >= arg->argn + 2 && arg->argc <= arg->argn + 4) {
    struct in_addr localIP, publicIP, remoteIP;
    struct alias_link *link;
    struct protoent *pe;
    int error;
    unsigned len;

    len = strlen(arg->argv[arg->argn]);
    if (len == 0) {
      prompt_Printf(arg->prompt, "proto redirect: invalid protocol\n");
      return 1;
    }
    if (strspn(arg->argv[arg->argn], "01234567") == len)
      pe = getprotobynumber(atoi(arg->argv[arg->argn]));
    else
      pe = getprotobyname(arg->argv[arg->argn]);
    if (pe == NULL) {
      prompt_Printf(arg->prompt, "proto redirect: invalid protocol\n");
      return 1;
    }

    error = StrToAddr(arg->argv[arg->argn + 1], &localIP);
    if (error) {
      prompt_Printf(arg->prompt, "proto redirect: invalid src address\n");
      return 1;
    }

    if (arg->argc >= arg->argn + 3) {
      error = StrToAddr(arg->argv[arg->argn + 2], &publicIP);
      if (error) {
        prompt_Printf(arg->prompt, "proto redirect: invalid alias address\n");
        prompt_Printf(arg->prompt, "usage: nat %s %s\n", arg->cmd->name,
                      arg->cmd->syntax);
        return 1;
      }
    } else
      publicIP.s_addr = INADDR_ANY;

    if (arg->argc == arg->argn + 4) {
      error = StrToAddr(arg->argv[arg->argn + 2], &remoteIP);
      if (error) {
        prompt_Printf(arg->prompt, "proto redirect: invalid dst address\n");
        prompt_Printf(arg->prompt, "usage: nat %s %s\n", arg->cmd->name,
                      arg->cmd->syntax);
        return 1;
      }
    } else
      remoteIP.s_addr = INADDR_ANY;

    link = LibAliasRedirectProto(la, localIP, remoteIP, publicIP, pe->p_proto);
    if (link == NULL) {
      prompt_Printf(arg->prompt, "proto redirect: packet aliasing"
                    " engine error\n");
      prompt_Printf(arg->prompt, "usage: nat %s %s\n", arg->cmd->name,
                    arg->cmd->syntax);
    }
  } else
    return -1;

  return 0;
}


static int
StrToAddr(const char *str, struct in_addr *addr)
{
  struct hostent *hp;

  if (inet_aton(str, addr))
    return 0;

  hp = gethostbyname(str);
  if (!hp) {
    log_Printf(LogWARN, "StrToAddr: Unknown host %s.\n", str);
    return -1;
  }
  *addr = *((struct in_addr *) hp->h_addr);
  return 0;
}


static int
StrToPort(const char *str, u_short *port, const char *proto)
{
  struct servent *sp;
  char *end;

  *port = strtol(str, &end, 10);
  if (*end != '\0') {
    sp = getservbyname(str, proto);
    if (sp == NULL) {
      log_Printf(LogWARN, "StrToAddr: Unknown port or service %s/%s.\n",
	        str, proto);
      return -1;
    }
    *port = ntohs(sp->s_port);
  }

  return 0;
}

static int
StrToPortRange(const char *str, u_short *low, u_short *high, const char *proto)
{
  char *minus;
  int res;

  minus = strchr(str, '-');
  if (minus)
    *minus = '\0';		/* Cheat the const-ness ! */

  res = StrToPort(str, low, proto);

  if (minus)
    *minus = '-';		/* Cheat the const-ness ! */

  if (res == 0) {
    if (minus)
      res = StrToPort(minus + 1, high, proto);
    else
      *high = *low;
  }

  return res;
}

static int
StrToAddrAndPort(const char *str, struct in_addr *addr, u_short *low,
                 u_short *high, const char *proto)
{
  char *colon;
  int res;

  colon = strchr(str, ':');
  if (!colon) {
    log_Printf(LogWARN, "StrToAddrAndPort: %s is missing port number.\n", str);
    return -1;
  }

  *colon = '\0';		/* Cheat the const-ness ! */
  res = StrToAddr(str, addr);
  *colon = ':';			/* Cheat the const-ness ! */
  if (res != 0)
    return -1;

  return StrToPortRange(colon + 1, low, high, proto);
}

int
nat_ProxyRule(struct cmdargs const *arg)
{
  char cmd[LINE_LEN];
  int f, pos;
  size_t len;

  if (arg->argn >= arg->argc)
    return -1;

  for (f = arg->argn, pos = 0; f < arg->argc; f++) {
    len = strlen(arg->argv[f]);
    if (sizeof cmd - pos < len + (len ? 1 : 0))
      break;
    if (len)
      cmd[pos++] = ' ';
    strcpy(cmd + pos, arg->argv[f]);
    pos += len;
  }

  return LibAliasProxyRule(la, cmd);
}

int
nat_SetTarget(struct cmdargs const *arg)
{
  struct in_addr addr;

  if (arg->argc == arg->argn) {
    addr.s_addr = INADDR_ANY;
    LibAliasSetTarget(la, addr);
    return 0;
  }

  if (arg->argc != arg->argn + 1)
    return -1;

  if (!strcasecmp(arg->argv[arg->argn], "MYADDR")) {
    addr.s_addr = INADDR_ANY;
    LibAliasSetTarget(la, addr);
    return 0;
  }

  addr = GetIpAddr(arg->argv[arg->argn]);
  if (addr.s_addr == INADDR_NONE) {
    log_Printf(LogWARN, "%s: invalid address\n", arg->argv[arg->argn]);
    return 1;
  }

  LibAliasSetTarget(la, addr);
  return 0;
}

#ifndef NO_FW_PUNCH
int
nat_PunchFW(struct cmdargs const *arg)
{
  char *end;
  long base, count;

  if (arg->argc == arg->argn) {
    LibAliasSetMode(la, 0, PKT_ALIAS_PUNCH_FW);
    return 0;
  }

  if (arg->argc != arg->argn + 2)
    return -1;

  base = strtol(arg->argv[arg->argn], &end, 10);
  if (*end != '\0' || base < 0)
    return -1;

  count = strtol(arg->argv[arg->argn + 1], &end, 10);
  if (*end != '\0' || count < 0)
    return -1;

  LibAliasSetFWBase(la, base, count);
  LibAliasSetMode(la, PKT_ALIAS_PUNCH_FW, PKT_ALIAS_PUNCH_FW);

  return 0;
}
#endif

int
nat_SkinnyPort(struct cmdargs const *arg)
{
  char *end;
  long port;

  if (arg->argc == arg->argn) {
    LibAliasSetSkinnyPort(la, 0);
    return 0;
  }

  if (arg->argc != arg->argn + 1)
    return -1;

  port = strtol(arg->argv[arg->argn], &end, 10);
  if (*end != '\0' || port < 0)
    return -1;

  LibAliasSetSkinnyPort(la, port);

  return 0;
}

static struct mbuf *
nat_LayerPush(struct bundle *bundle, struct link *l __unused, struct mbuf *bp,
                int pri __unused, u_short *proto)
{
  if (!bundle->NatEnabled || *proto != PROTO_IP)
    return bp;

  log_Printf(LogDEBUG, "nat_LayerPush: PROTO_IP -> PROTO_IP\n");
  m_settype(bp, MB_NATOUT);
  /* Ensure there's a bit of extra buffer for the NAT code... */
  bp = m_pullup(m_append(bp, NULL, NAT_EXTRABUF));
  LibAliasOut(la, MBUF_CTOP(bp), bp->m_len);
  bp->m_len = ntohs(((struct ip *)MBUF_CTOP(bp))->ip_len);

  return bp;
}

static struct mbuf *
nat_LayerPull(struct bundle *bundle, struct link *l __unused, struct mbuf *bp,
                u_short *proto)
{
  static int gfrags;
  int ret, len, nfrags;
  struct mbuf **last;
  char *fptr;

  if (!bundle->NatEnabled || *proto != PROTO_IP)
    return bp;

  log_Printf(LogDEBUG, "nat_LayerPull: PROTO_IP -> PROTO_IP\n");
  m_settype(bp, MB_NATIN);
  /* Ensure there's a bit of extra buffer for the NAT code... */
  bp = m_pullup(m_append(bp, NULL, NAT_EXTRABUF));
  ret = LibAliasIn(la, MBUF_CTOP(bp), bp->m_len);

  bp->m_len = ntohs(((struct ip *)MBUF_CTOP(bp))->ip_len);
  if (bp->m_len > MAX_MRU) {
    log_Printf(LogWARN, "nat_LayerPull: Problem with IP header length (%lu)\n",
               (unsigned long)bp->m_len);
    m_freem(bp);
    return NULL;
  }

  switch (ret) {
    case PKT_ALIAS_OK:
      break;

    case PKT_ALIAS_UNRESOLVED_FRAGMENT:
      /* Save the data for later */
      if ((fptr = malloc(bp->m_len)) == NULL) {
	log_Printf(LogWARN, "nat_LayerPull: Dropped unresolved fragment -"
		   " out of memory!\n");
	m_freem(bp);
	bp = NULL;
      } else {
	bp = mbuf_Read(bp, fptr, bp->m_len);
	LibAliasSaveFragment(la, fptr);
	log_Printf(LogDEBUG, "Store another frag (%lu) - now %d\n",
		   (unsigned long)((struct ip *)fptr)->ip_id, ++gfrags);
      }
      break;

    case PKT_ALIAS_FOUND_HEADER_FRAGMENT:
      /* Fetch all the saved fragments and chain them on the end of `bp' */
      last = &bp->m_nextpkt;
      nfrags = 0;
      while ((fptr = LibAliasGetFragment(la, MBUF_CTOP(bp))) != NULL) {
        nfrags++;
        LibAliasFragmentIn(la, MBUF_CTOP(bp), fptr);
        len = ntohs(((struct ip *)fptr)->ip_len);
        *last = m_get(len, MB_NATIN);
        memcpy(MBUF_CTOP(*last), fptr, len);
        free(fptr);
        last = &(*last)->m_nextpkt;
      }
      gfrags -= nfrags;
      log_Printf(LogDEBUG, "Found a frag header (%lu) - plus %d more frags (no"
                 "w %d)\n", (unsigned long)((struct ip *)MBUF_CTOP(bp))->ip_id,
                 nfrags, gfrags);
      break;

    case PKT_ALIAS_IGNORED:
      if (LibAliasSetMode(la, 0, 0) & PKT_ALIAS_DENY_INCOMING) {
        log_Printf(LogTCPIP, "NAT engine denied data:\n");
        m_freem(bp);
        bp = NULL;
      } else if (log_IsKept(LogTCPIP)) {
        log_Printf(LogTCPIP, "NAT engine ignored data:\n");
        PacketCheck(bundle, AF_INET, MBUF_CTOP(bp), bp->m_len, NULL,
                    NULL, NULL);
      }
      break;

    default:
      log_Printf(LogWARN, "nat_LayerPull: Dropped a packet (%d)....\n", ret);
      m_freem(bp);
      bp = NULL;
      break;
  }

  return bp;
}

struct layer natlayer =
  { LAYER_NAT, "nat", nat_LayerPush, nat_LayerPull };

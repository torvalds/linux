/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Toshiharu OHNO <tony-o@iij.ad.jp>
 *                           Internet Initiative Japan, Inc (IIJ)
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

#include "layer.h"
#include "defs.h"
#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "iplist.h"
#include "timer.h"
#include "throughput.h"
#include "lqr.h"
#include "hdlc.h"
#include "fsm.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "slcompress.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "filter.h"
#include "descriptor.h"
#include "prompt.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"

static unsigned filter_Nam2Op(const char *);

static int
ParsePort(const char *service, const char *proto)
{
  struct servent *servent;
  char *cp;
  int port;

  servent = getservbyname(service, proto);
  if (servent != NULL)
    return ntohs(servent->s_port);

  port = strtol(service, &cp, 0);
  if (cp == service) {
    log_Printf(LogWARN, "ParsePort: %s is not a port name or number.\n",
	      service);
    return 0;
  }
  return port;
}

/*
 *	ICMP Syntax:	src eq icmp_message_type
 */
static int
ParseIcmp(int argc, char const *const *argv, struct filterent *tgt)
{
  int type;
  char *cp;

  switch (argc) {
  case 0:
    /* permit/deny all ICMP types */
    tgt->f_srcop = tgt->f_dstop = OP_NONE;
    break;

  case 3:
    if (!strcmp(*argv, "src") && !strcmp(argv[1], "eq")) {
      type = strtol(argv[2], &cp, 0);
      if (cp == argv[2]) {
	log_Printf(LogWARN, "ParseIcmp: type is expected.\n");
	return 0;
      }
      tgt->f_srcop = OP_EQ;
      tgt->f_srcport = type;
      tgt->f_dstop = OP_NONE;
    }
    break;

  default:
    log_Printf(LogWARN, "ParseIcmp: bad icmp syntax.\n");
    return 0;
  }
  return 1;
}

/*
 *	UDP Syntax: [src op port] [dst op port]
 */
static int
ParseUdpOrTcp(int argc, char const *const *argv, const struct protoent *pe,
              struct filterent *tgt)
{
  tgt->f_srcop = tgt->f_dstop = OP_NONE;
  tgt->f_estab = tgt->f_syn = tgt->f_finrst = 0;

  if (argc >= 3 && !strcmp(*argv, "src")) {
    tgt->f_srcop = filter_Nam2Op(argv[1]);
    if (tgt->f_srcop == OP_NONE) {
      log_Printf(LogWARN, "ParseUdpOrTcp: bad operator\n");
      return 0;
    }
    if (pe == NULL)
      return 0;
    tgt->f_srcport = ParsePort(argv[2], pe->p_name);
    if (tgt->f_srcport == 0)
      return 0;
    argc -= 3;
    argv += 3;
  }

  if (argc >= 3 && !strcmp(argv[0], "dst")) {
    tgt->f_dstop = filter_Nam2Op(argv[1]);
    if (tgt->f_dstop == OP_NONE) {
      log_Printf(LogWARN, "ParseUdpOrTcp: bad operator\n");
      return 0;
    }
    if (pe == NULL)
      return 0;
    tgt->f_dstport = ParsePort(argv[2], pe->p_name);
    if (tgt->f_dstport == 0)
      return 0;
    argc -= 3;
    argv += 3;
  }

  if (pe && pe->p_proto == IPPROTO_TCP) {
    for (; argc > 0; argc--, argv++)
      if (!strcmp(*argv, "estab"))
        tgt->f_estab = 1;
      else if (!strcmp(*argv, "syn"))
        tgt->f_syn = 1;
      else if (!strcmp(*argv, "finrst"))
        tgt->f_finrst = 1;
      else
        break;
  }

  if (argc > 0) {
    log_Printf(LogWARN, "ParseUdpOrTcp: bad src/dst port syntax: %s\n", *argv);
    return 0;
  }

  return 1;
}

static int
ParseGeneric(int argc, struct filterent *tgt)
{
  /*
   * Filter currently is a catch-all. Requests are either permitted or
   * dropped.
   */
  if (argc != 0) {
    log_Printf(LogWARN, "ParseGeneric: Too many parameters\n");
    return 0;
  } else
    tgt->f_srcop = tgt->f_dstop = OP_NONE;

  return 1;
}

static unsigned
addrtype(const char *addr)
{
  if (!strncasecmp(addr, "MYADDR", 6) && (addr[6] == '\0' || addr[6] == '/'))
    return T_MYADDR;
  if (!strncasecmp(addr, "MYADDR6", 7) && (addr[7] == '\0' || addr[7] == '/'))
    return T_MYADDR6;
  if (!strncasecmp(addr, "HISADDR", 7) && (addr[7] == '\0' || addr[7] == '/'))
    return T_HISADDR;
  if (!strncasecmp(addr, "HISADDR6", 8) && (addr[8] == '\0' || addr[8] == '/'))
    return T_HISADDR6;
  if (!strncasecmp(addr, "DNS0", 4) && (addr[4] == '\0' || addr[4] == '/'))
    return T_DNS0;
  if (!strncasecmp(addr, "DNS1", 4) && (addr[4] == '\0' || addr[4] == '/'))
    return T_DNS1;

  return T_ADDR;
}

static const char *
addrstr(struct ncprange *addr, unsigned type)
{
  switch (type) {
    case T_MYADDR:
      return "MYADDR";
    case T_HISADDR:
      return "HISADDR";
    case T_DNS0:
      return "DNS0";
    case T_DNS1:
      return "DNS1";
  }
  return ncprange_ntoa(addr);
}

static int
filter_Parse(struct ncp *ncp, int argc, char const *const *argv,
             struct filterent *ofp)
{
  struct filterent fe;
  struct protoent *pe;
  char *wp;
  int action, family, ruleno, val, width;

  ruleno = strtol(*argv, &wp, 0);
  if (*argv == wp || ruleno >= MAXFILTERS) {
    log_Printf(LogWARN, "Parse: invalid filter number.\n");
    return 0;
  }
  if (ruleno < 0) {
    for (ruleno = 0; ruleno < MAXFILTERS; ruleno++) {
      ofp->f_action = A_NONE;
      ofp++;
    }
    log_Printf(LogWARN, "Parse: filter cleared.\n");
    return 1;
  }
  ofp += ruleno;

  if (--argc == 0) {
    log_Printf(LogWARN, "Parse: missing action.\n");
    return 0;
  }
  argv++;

  memset(&fe, '\0', sizeof fe);

  val = strtol(*argv, &wp, 0);
  if (!*wp && val >= 0 && val < MAXFILTERS) {
    if (val <= ruleno) {
      log_Printf(LogWARN, "Parse: Can only jump forward from rule %d\n",
                 ruleno);
      return 0;
    }
    action = val;
  } else if (!strcmp(*argv, "permit")) {
    action = A_PERMIT;
  } else if (!strcmp(*argv, "deny")) {
    action = A_DENY;
  } else if (!strcmp(*argv, "clear")) {
    ofp->f_action = A_NONE;
    return 1;
  } else {
    log_Printf(LogWARN, "Parse: %s: bad action\n", *argv);
    return 0;
  }
  fe.f_action = action;

  argc--;
  argv++;

  if (argc && argv[0][0] == '!' && !argv[0][1]) {
    fe.f_invert = 1;
    argc--;
    argv++;
  }

  ncprange_init(&fe.f_src);
  ncprange_init(&fe.f_dst);

  if (argc == 0)
    pe = NULL;
  else if ((pe = getprotobyname(*argv)) == NULL && strcmp(*argv, "all") != 0) {
    if (argc < 2) {
      log_Printf(LogWARN, "Parse: Protocol or address pair expected\n");
      return 0;
    } else if (strcasecmp(*argv, "any") == 0 ||
               ncprange_aton(&fe.f_src, ncp, *argv)) {
      family = ncprange_family(&fe.f_src);
      if (!ncprange_getwidth(&fe.f_src, &width))
        width = 0;
      if (width == 0)
        ncprange_init(&fe.f_src);
      fe.f_srctype = addrtype(*argv);
      argc--;
      argv++;

      if (strcasecmp(*argv, "any") == 0 ||
          ncprange_aton(&fe.f_dst, ncp, *argv)) {
        if (ncprange_family(&fe.f_dst) != AF_UNSPEC &&
            ncprange_family(&fe.f_src) != AF_UNSPEC &&
            family != ncprange_family(&fe.f_dst)) {
          log_Printf(LogWARN, "Parse: src and dst address families differ\n");
          return 0;
        }
        if (!ncprange_getwidth(&fe.f_dst, &width))
          width = 0;
        if (width == 0)
          ncprange_init(&fe.f_dst);
        fe.f_dsttype = addrtype(*argv);
        argc--;
        argv++;
      } else {
        log_Printf(LogWARN, "Parse: Protocol or address pair expected\n");
        return 0;
      }

      if (argc) {
        if ((pe = getprotobyname(*argv)) == NULL && strcmp(*argv, "all") != 0) {
          log_Printf(LogWARN, "Parse: %s: Protocol expected\n", *argv);
          return 0;
        } else {
          argc--;
          argv++;
        }
      }
    } else {
      log_Printf(LogWARN, "Parse: Protocol or address pair expected\n");
      return 0;
    }
  } else {
    argc--;
    argv++;
  }

  if (argc >= 2 && strcmp(*argv, "timeout") == 0) {
    fe.timeout = strtoul(argv[1], NULL, 10);
    argc -= 2;
    argv += 2;
  }

  val = 1;
  fe.f_proto = (pe == NULL) ? 0 : pe->p_proto;

  switch (fe.f_proto) {
  case IPPROTO_TCP:
  case IPPROTO_UDP:
  case IPPROTO_IPIP:
#ifndef NOINET6
  case IPPROTO_IPV6:
#endif
    val = ParseUdpOrTcp(argc, argv, pe, &fe);
    break;
  case IPPROTO_ICMP:
#ifndef NOINET6
  case IPPROTO_ICMPV6:
#endif
    val = ParseIcmp(argc, argv, &fe);
    break;
  default:
    val = ParseGeneric(argc, &fe);
    break;
  }

  log_Printf(LogDEBUG, "Parse: Src: %s\n", ncprange_ntoa(&fe.f_src));
  log_Printf(LogDEBUG, "Parse: Dst: %s\n", ncprange_ntoa(&fe.f_dst));
  log_Printf(LogDEBUG, "Parse: Proto: %d\n", fe.f_proto);

  log_Printf(LogDEBUG, "Parse: src:  %s (%d)\n",
            filter_Op2Nam(fe.f_srcop), fe.f_srcport);
  log_Printf(LogDEBUG, "Parse: dst:  %s (%d)\n",
            filter_Op2Nam(fe.f_dstop), fe.f_dstport);
  log_Printf(LogDEBUG, "Parse: estab: %u\n", fe.f_estab);
  log_Printf(LogDEBUG, "Parse: syn: %u\n", fe.f_syn);
  log_Printf(LogDEBUG, "Parse: finrst: %u\n", fe.f_finrst);

  if (val)
    *ofp = fe;

  return val;
}

int
filter_Set(struct cmdargs const *arg)
{
  struct filter *filter;

  if (arg->argc < arg->argn+2)
    return -1;

  if (!strcmp(arg->argv[arg->argn], "in"))
    filter = &arg->bundle->filter.in;
  else if (!strcmp(arg->argv[arg->argn], "out"))
    filter = &arg->bundle->filter.out;
  else if (!strcmp(arg->argv[arg->argn], "dial"))
    filter = &arg->bundle->filter.dial;
  else if (!strcmp(arg->argv[arg->argn], "alive"))
    filter = &arg->bundle->filter.alive;
  else {
    log_Printf(LogWARN, "filter_Set: %s: Invalid filter name.\n",
              arg->argv[arg->argn]);
    return -1;
  }

  filter_Parse(&arg->bundle->ncp, arg->argc - arg->argn - 1,
        arg->argv + arg->argn + 1, filter->rule);
  return 0;
}

const char *
filter_Action2Nam(unsigned act)
{
  static const char * const actname[] = { "  none ", "permit ", "  deny " };
  static char buf[8];

  if (act < MAXFILTERS) {
    snprintf(buf, sizeof buf, "%6d ", act);
    return buf;
  } else if (act >= A_NONE && act < A_NONE + sizeof(actname)/sizeof(char *))
    return actname[act - A_NONE];
  else
    return "?????? ";
}

static void
doShowFilter(struct filterent *fp, struct prompt *prompt)
{
  struct protoent *pe;
  int n;

  for (n = 0; n < MAXFILTERS; n++, fp++) {
    if (fp->f_action != A_NONE) {
      prompt_Printf(prompt, "  %2d %s", n, filter_Action2Nam(fp->f_action));
      prompt_Printf(prompt, "%c ", fp->f_invert ? '!' : ' ');

      if (ncprange_isset(&fp->f_src))
        prompt_Printf(prompt, "%s ", addrstr(&fp->f_src, fp->f_srctype));
      else
        prompt_Printf(prompt, "any ");

      if (ncprange_isset(&fp->f_dst))
        prompt_Printf(prompt, "%s ", addrstr(&fp->f_dst, fp->f_dsttype));
      else
        prompt_Printf(prompt, "any ");

      if (fp->f_proto) {
        if ((pe = getprotobynumber(fp->f_proto)) == NULL)
	  prompt_Printf(prompt, "P:%d", fp->f_proto);
        else
	  prompt_Printf(prompt, "%s", pe->p_name);

	if (fp->f_srcop)
	  prompt_Printf(prompt, " src %s %d", filter_Op2Nam(fp->f_srcop),
		  fp->f_srcport);
	if (fp->f_dstop)
	  prompt_Printf(prompt, " dst %s %d", filter_Op2Nam(fp->f_dstop),
		  fp->f_dstport);
	if (fp->f_estab)
	  prompt_Printf(prompt, " estab");
	if (fp->f_syn)
	  prompt_Printf(prompt, " syn");
	if (fp->f_finrst)
	  prompt_Printf(prompt, " finrst");
      } else
	prompt_Printf(prompt, "all");
      if (fp->timeout != 0)
	  prompt_Printf(prompt, " timeout %u", fp->timeout);
      prompt_Printf(prompt, "\n");
    }
  }
}

int
filter_Show(struct cmdargs const *arg)
{
  if (arg->argc > arg->argn+1)
    return -1;

  if (arg->argc == arg->argn+1) {
    struct filter *filter;

    if (!strcmp(arg->argv[arg->argn], "in"))
      filter = &arg->bundle->filter.in;
    else if (!strcmp(arg->argv[arg->argn], "out"))
      filter = &arg->bundle->filter.out;
    else if (!strcmp(arg->argv[arg->argn], "dial"))
      filter = &arg->bundle->filter.dial;
    else if (!strcmp(arg->argv[arg->argn], "alive"))
      filter = &arg->bundle->filter.alive;
    else
      return -1;
    doShowFilter(filter->rule, arg->prompt);
  } else {
    struct filter *filter[4];
    int f;

    filter[0] = &arg->bundle->filter.in;
    filter[1] = &arg->bundle->filter.out;
    filter[2] = &arg->bundle->filter.dial;
    filter[3] = &arg->bundle->filter.alive;
    for (f = 0; f < 4; f++) {
      if (f)
        prompt_Printf(arg->prompt, "\n");
      prompt_Printf(arg->prompt, "%s:\n", filter[f]->name);
      doShowFilter(filter[f]->rule, arg->prompt);
    }
  }

  return 0;
}

static const char * const opname[] = {"none", "eq", "gt", "lt"};

const char *
filter_Op2Nam(unsigned op)
{
  if (op >= sizeof opname / sizeof opname[0])
    return "unknown";
  return opname[op];

}

static unsigned
filter_Nam2Op(const char *cp)
{
  unsigned op;

  for (op = sizeof opname / sizeof opname[0] - 1; op; op--)
    if (!strcasecmp(cp, opname[op]))
      break;

  return op;
}

void
filter_AdjustAddr(struct filter *filter, struct ncpaddr *local,
                  struct ncpaddr *remote, struct in_addr *dns)
{
  struct filterent *fp;
  int n;

  for (fp = filter->rule, n = 0; n < MAXFILTERS; fp++, n++)
    if (fp->f_action != A_NONE) {
      if (local) {
        if (fp->f_srctype == T_MYADDR && ncpaddr_family(local) == AF_INET)
          ncprange_sethost(&fp->f_src, local);
        if (fp->f_dsttype == T_MYADDR && ncpaddr_family(local) == AF_INET)
          ncprange_sethost(&fp->f_dst, local);
#ifndef NOINET6
        if (fp->f_srctype == T_MYADDR6 && ncpaddr_family(local) == AF_INET6)
          ncprange_sethost(&fp->f_src, local);
        if (fp->f_dsttype == T_MYADDR6 && ncpaddr_family(local) == AF_INET6)
          ncprange_sethost(&fp->f_dst, local);
#endif
      }
      if (remote) {
        if (fp->f_srctype == T_HISADDR && ncpaddr_family(remote) == AF_INET)
          ncprange_sethost(&fp->f_src, remote);
        if (fp->f_dsttype == T_HISADDR && ncpaddr_family(remote) == AF_INET)
          ncprange_sethost(&fp->f_dst, remote);
#ifndef NOINET6
        if (fp->f_srctype == T_HISADDR6 && ncpaddr_family(remote) == AF_INET6)
          ncprange_sethost(&fp->f_src, remote);
        if (fp->f_dsttype == T_HISADDR6 && ncpaddr_family(remote) == AF_INET6)
          ncprange_sethost(&fp->f_dst, remote);
#endif
      }
      if (dns) {
        if (fp->f_srctype == T_DNS0)
          ncprange_setip4host(&fp->f_src, dns[0]);
        if (fp->f_dsttype == T_DNS0)
          ncprange_setip4host(&fp->f_dst, dns[0]);
        if (fp->f_srctype == T_DNS1)
          ncprange_setip4host(&fp->f_src, dns[1]);
        if (fp->f_dsttype == T_DNS1)
          ncprange_setip4host(&fp->f_dst, dns[1]);
      }
    }
}

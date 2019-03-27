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

#include <sys/param.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <net/route.h>
#include <sys/un.h>

#include <errno.h>
#include <resolv.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include "layer.h"
#include "defs.h"
#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "fsm.h"
#include "iplist.h"
#include "throughput.h"
#include "slcompress.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "filter.h"
#include "descriptor.h"
#include "async.h"
#include "ccp.h"
#include "link.h"
#include "physical.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "prompt.h"
#include "route.h"
#include "iface.h"
#include "chat.h"
#include "auth.h"
#include "chap.h"
#include "cbcp.h"
#include "datalink.h"


static u_short default_urgent_tcp_ports[] = {
  21,	/* ftp */
  22,	/* ssh */
  23,	/* telnet */
  513,	/* login */
  514,	/* shell */
  543,	/* klogin */
  544	/* kshell */
};

#define NDEFTCPPORTS \
  (sizeof default_urgent_tcp_ports / sizeof default_urgent_tcp_ports[0])

void
ncp_Init(struct ncp *ncp, struct bundle *bundle)
{
  ncp->afq = AF_INET;
  ncp->route = NULL;

  ncp->cfg.urgent.tcp.port = (u_short *)malloc(NDEFTCPPORTS * sizeof(u_short));
  if (ncp->cfg.urgent.tcp.port == NULL) {
    log_Printf(LogERROR, "ncp_Init: Out of memory allocating urgent ports\n");
    ncp->cfg.urgent.tcp.nports = ncp->cfg.urgent.tcp.maxports = 0;
  } else {
    ncp->cfg.urgent.tcp.nports = ncp->cfg.urgent.tcp.maxports = NDEFTCPPORTS;
    memcpy(ncp->cfg.urgent.tcp.port, default_urgent_tcp_ports,
	   NDEFTCPPORTS * sizeof(u_short));
  }
  ncp->cfg.urgent.tos = 1;

  ncp->cfg.urgent.udp.nports = ncp->cfg.urgent.udp.maxports = 0;
  ncp->cfg.urgent.udp.port = NULL;

  mp_Init(&ncp->mp, bundle);

  /* Send over the first physical link by default */
  ipcp_Init(&ncp->ipcp, bundle, &bundle->links->physical->link,
            &bundle->fsm);
#ifndef NOINET6
  ipv6cp_Init(&ncp->ipv6cp, bundle, &bundle->links->physical->link,
              &bundle->fsm);
#endif
}

void
ncp_Destroy(struct ncp *ncp)
{
  ipcp_Destroy(&ncp->ipcp);
#ifndef NOINET6
  ipv6cp_Destroy(&ncp->ipv6cp);
#endif

  if (ncp->cfg.urgent.tcp.maxports) {
    ncp->cfg.urgent.tcp.nports = ncp->cfg.urgent.tcp.maxports = 0;
    free(ncp->cfg.urgent.tcp.port);
    ncp->cfg.urgent.tcp.port = NULL;
  }
  if (ncp->cfg.urgent.udp.maxports) {
    ncp->cfg.urgent.udp.nports = ncp->cfg.urgent.udp.maxports = 0;
    free(ncp->cfg.urgent.udp.port);
    ncp->cfg.urgent.udp.port = NULL;
  }
}

int
ncp_fsmStart(struct ncp *ncp,
#ifdef NOINET6
	     struct bundle *bundle __unused
#else
	     struct bundle *bundle
#endif
	     )
{
  int res = 0;

#ifndef NOINET6
  if (Enabled(bundle, OPT_IPCP)) {
#endif
    fsm_Up(&ncp->ipcp.fsm);
    fsm_Open(&ncp->ipcp.fsm);
    res++;
#ifndef NOINET6
  }

  if (Enabled(bundle, OPT_IPV6CP)) {
    fsm_Up(&ncp->ipv6cp.fsm);
    fsm_Open(&ncp->ipv6cp.fsm);
    res++;
  }
#endif

  return res;
}

void
ncp_IfaceAddrAdded(struct ncp *ncp, const struct iface_addr *addr)
{
  switch (ncprange_family(&addr->ifa)) {
  case AF_INET:
    ipcp_IfaceAddrAdded(&ncp->ipcp, addr);
    break;
#ifndef NOINET6
  case AF_INET6:
    ipv6cp_IfaceAddrAdded(&ncp->ipv6cp, addr);
    break;
#endif
  }
}

void
ncp_IfaceAddrDeleted(struct ncp *ncp, const struct iface_addr *addr)
{
  if (ncprange_family(&addr->ifa) == AF_INET)
    ipcp_IfaceAddrDeleted(&ncp->ipcp, addr);
}

void
ncp_SetLink(struct ncp *ncp, struct link *l)
{
  ipcp_SetLink(&ncp->ipcp, l);
#ifndef NOINET6
  ipv6cp_SetLink(&ncp->ipv6cp, l);
#endif
}

/*
 * Enqueue a packet of the given address family.  Nothing will make it
 * down to the physical link level 'till ncp_FillPhysicalQueues() is used.
 */
void
ncp_Enqueue(struct ncp *ncp, int af, unsigned pri, char *ptr, int count)
{
#ifndef NOINET6
  struct ipv6cp *ipv6cp = &ncp->ipv6cp;
#endif
  struct ipcp *ipcp = &ncp->ipcp;
  struct mbuf *bp;

  /*
   * We allocate an extra 6 bytes, four at the front and two at the end.
   * This is an optimisation so that we need to do less work in
   * m_prepend() in acf_LayerPush() and proto_LayerPush() and
   * appending in hdlc_LayerPush().
   */

  switch (af) {
  case AF_INET:
    if (pri >= IPCP_QUEUES(ipcp)) {
      log_Printf(LogERROR, "Can't store in ip queue %u\n", pri);
      break;
    }

    bp = m_get(count + 6, MB_IPOUT);
    bp->m_offset += 4;
    bp->m_len -= 6;
    memcpy(MBUF_CTOP(bp), ptr, count);
    m_enqueue(ipcp->Queue + pri, bp);
    break;

#ifndef NOINET6
  case AF_INET6:
    if (pri >= IPV6CP_QUEUES(ipcp)) {
      log_Printf(LogERROR, "Can't store in ipv6 queue %u\n", pri);
      break;
    }

    bp = m_get(count + 6, MB_IPOUT);
    bp->m_offset += 4;
    bp->m_len -= 6;
    memcpy(MBUF_CTOP(bp), ptr, count);
    m_enqueue(ipv6cp->Queue + pri, bp);
    break;
#endif

  default:
      log_Printf(LogERROR, "Can't enqueue protocol family %d\n", af);
  }
}

/*
 * How many packets are queued to go out ?
 */
size_t
ncp_QueueLen(struct ncp *ncp)
{
  size_t result;

  result = ipcp_QueueLen(&ncp->ipcp);
#ifndef NOINET6
  result += ipv6cp_QueueLen(&ncp->ipv6cp);
#endif
  result += mp_QueueLen(&ncp->mp);	/* Usually empty */

  return result;
}

/*
 * Ditch all queued packets.  This is usually done after our choked timer
 * has fired - which happens because we couldn't send any traffic over
 * any links for some time.
 */
void
ncp_DeleteQueues(struct ncp *ncp)
{
#ifndef NOINET6
  struct ipv6cp *ipv6cp = &ncp->ipv6cp;
#endif
  struct ipcp *ipcp = &ncp->ipcp;
  struct mp *mp = &ncp->mp;
  struct mqueue *q;

  for (q = ipcp->Queue; q < ipcp->Queue + IPCP_QUEUES(ipcp); q++)
    while (q->top)
      m_freem(m_dequeue(q));

#ifndef NOINET6
  for (q = ipv6cp->Queue; q < ipv6cp->Queue + IPV6CP_QUEUES(ipv6cp); q++)
    while (q->top)
      m_freem(m_dequeue(q));
#endif

  link_DeleteQueue(&mp->link);	/* Usually empty anyway */
}

/*
 * Arrange that each of our links has at least one packet.  We keep the
 * number of packets queued at the link level to a minimum so that the
 * loss of a link in multi-link mode results in the minimum number of
 * dropped packets.
 */
size_t
ncp_FillPhysicalQueues(struct ncp *ncp, struct bundle *bundle)
{
  size_t total;

  if (bundle->ncp.mp.active)
    total = mp_FillPhysicalQueues(bundle);
  else {
    struct datalink *dl;
    size_t add;

    for (total = 0, dl = bundle->links; dl; dl = dl->next)
      if (dl->state == DATALINK_OPEN) {
        add = link_QueueLen(&dl->physical->link);
        if (add == 0 && dl->physical->out == NULL)
          add = ncp_PushPacket(ncp, &ncp->afq, &dl->physical->link);
        total += add;
      }
  }

  return total + ncp_QueueLen(&bundle->ncp);
}

/*
 * Push a packet into the given link.  ``af'' is used as a persistent record
 * of what is to be pushed next, coming either from mp->out or ncp->afq.
 */
int
ncp_PushPacket(struct ncp *ncp __unused,
#ifdef NOINET6
	       int *af __unused,
#else
	       int *af,
#endif
	       struct link *l)
{
  struct bundle *bundle = l->lcp.fsm.bundle;
  int res;

#ifndef NOINET6
  if (*af == AF_INET) {
    if ((res = ipcp_PushPacket(&bundle->ncp.ipcp, l)))
      *af = AF_INET6;
    else
      res = ipv6cp_PushPacket(&bundle->ncp.ipv6cp, l);
  } else {
    if ((res = ipv6cp_PushPacket(&bundle->ncp.ipv6cp, l)))
      *af = AF_INET;
    else
      res = ipcp_PushPacket(&bundle->ncp.ipcp, l);
  }
#else
  res = ipcp_PushPacket(&bundle->ncp.ipcp, l);
#endif

  return res;
}

int
ncp_IsUrgentPort(struct port_range *range, u_short src, u_short dst)
{
  unsigned f;

  for (f = 0; f < range->nports; f++)
    if (range->port[f] == src || range->port[f] == dst)
      return 1;

  return 0;
}

void
ncp_AddUrgentPort(struct port_range *range, u_short port)
{
  u_short *newport;
  unsigned p;

  if (range->nports == range->maxports) {
    range->maxports += 10;
    newport = (u_short *)realloc(range->port,
                                 range->maxports * sizeof(u_short));
    if (newport == NULL) {
      log_Printf(LogERROR, "ncp_AddUrgentPort: realloc: %s\n",
                 strerror(errno));
      range->maxports -= 10;
      return;
    }
    range->port = newport;
  }

  for (p = 0; p < range->nports; p++)
    if (range->port[p] == port) {
      log_Printf(LogWARN, "%u: Port already set to urgent\n", port);
      break;
    } else if (range->port[p] > port) {
      memmove(range->port + p + 1, range->port + p,
              (range->nports - p) * sizeof(u_short));
      range->port[p] = port;
      range->nports++;
      break;
    }

  if (p == range->nports)
    range->port[range->nports++] = port;
}

void
ncp_RemoveUrgentPort(struct port_range *range, u_short port)
{
  unsigned p;

  for (p = 0; p < range->nports; p++)
    if (range->port[p] == port) {
      if (p + 1 != range->nports)
        memmove(range->port + p, range->port + p + 1,
                (range->nports - p - 1) * sizeof(u_short));
      range->nports--;
      return;
    }

  if (p == range->nports)
    log_Printf(LogWARN, "%u: Port not set to urgent\n", port);
}

void
ncp_ClearUrgentPorts(struct port_range *range)
{
  range->nports = 0;
}

int
ncp_IsUrgentTcpLen(struct ncp *ncp, int len)
{
  if (len < ncp->cfg.urgent.len)
    return 1;
  else
    return 0;
}

void
ncp_SetUrgentTcpLen(struct ncp *ncp, int len)
{
    ncp->cfg.urgent.len = len;
}

int
ncp_Show(struct cmdargs const *arg)
{
  struct ncp *ncp = &arg->bundle->ncp;
  unsigned p;

#ifndef NOINET6
  prompt_Printf(arg->prompt, "Next queued AF: %s\n",
                ncp->afq == AF_INET6 ? "inet6" : "inet");
#endif

  if (ncp->route) {
    prompt_Printf(arg->prompt, "\n");
    route_ShowSticky(arg->prompt, ncp->route, "Sticky routes", 1);
  }

  prompt_Printf(arg->prompt, "\nDefaults:\n");
  prompt_Printf(arg->prompt, "  sendpipe:      ");
  if (ncp->cfg.sendpipe > 0)
    prompt_Printf(arg->prompt, "%-20ld\n", ncp->cfg.sendpipe);
  else
    prompt_Printf(arg->prompt, "unspecified\n");
  prompt_Printf(arg->prompt, "  recvpipe:      ");
  if (ncp->cfg.recvpipe > 0)
    prompt_Printf(arg->prompt, "%ld\n", ncp->cfg.recvpipe);
  else
    prompt_Printf(arg->prompt, "unspecified\n");

  prompt_Printf(arg->prompt, "\n  Urgent ports\n");
  prompt_Printf(arg->prompt, "         TCP:    ");
  if (ncp->cfg.urgent.tcp.nports == 0)
    prompt_Printf(arg->prompt, "none");
  else
    for (p = 0; p < ncp->cfg.urgent.tcp.nports; p++) {
      if (p)
        prompt_Printf(arg->prompt, ", ");
      prompt_Printf(arg->prompt, "%u", ncp->cfg.urgent.tcp.port[p]);
    }

  prompt_Printf(arg->prompt, "\n         UDP:    ");
  if (ncp->cfg.urgent.udp.nports == 0)
    prompt_Printf(arg->prompt, "none");
  else
    for (p = 0; p < ncp->cfg.urgent.udp.nports; p++) {
      if (p)
        prompt_Printf(arg->prompt, ", ");
      prompt_Printf(arg->prompt, "%u", ncp->cfg.urgent.udp.port[p]);
    }
  prompt_Printf(arg->prompt, "\n         TOS:    %s\n\n",
                ncp->cfg.urgent.tos ? "yes" : "no");

  return 0;
}

int
ncp_LayersOpen(struct ncp *ncp)
{
  int n;

  n = !!(ncp->ipcp.fsm.state == ST_OPENED);
#ifndef NOINET6
  n += !!(ncp->ipv6cp.fsm.state == ST_OPENED);
#endif

  return n;
}

int
ncp_LayersUnfinished(struct ncp *ncp)
{
  int n = 0;

  if (ncp->ipcp.fsm.state > ST_CLOSED ||
      ncp->ipcp.fsm.state == ST_STARTING)
    n++;

#ifndef NOINET6
  if (ncp->ipv6cp.fsm.state > ST_CLOSED ||
      ncp->ipv6cp.fsm.state == ST_STARTING)
    n++;
#endif

  return n;
}

void
ncp_Close(struct ncp *ncp)
{
  if (ncp->ipcp.fsm.state > ST_CLOSED ||
      ncp->ipcp.fsm.state == ST_STARTING)
    fsm_Close(&ncp->ipcp.fsm);

#ifndef NOINET6
  if (ncp->ipv6cp.fsm.state > ST_CLOSED ||
      ncp->ipv6cp.fsm.state == ST_STARTING)
    fsm_Close(&ncp->ipv6cp.fsm);
#endif
}

void
ncp2initial(struct ncp *ncp)
{
  fsm2initial(&ncp->ipcp.fsm);
#ifndef NOINET6
  fsm2initial(&ncp->ipv6cp.fsm);
#endif
}

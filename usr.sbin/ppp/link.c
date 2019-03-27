/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Brian Somers <brian@Awfulhak.org>
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

#include <sys/types.h>
#include <netinet/in_systm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

#include "defs.h"
#include "layer.h"
#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "lqr.h"
#include "hdlc.h"
#include "throughput.h"
#include "proto.h"
#include "fsm.h"
#include "descriptor.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "prompt.h"
#include "async.h"
#include "physical.h"
#include "mp.h"
#include "iplist.h"
#include "slcompress.h"
#include "ncpaddr.h"
#include "ip.h"
#include "ipcp.h"
#include "ipv6cp.h"
#include "auth.h"
#include "pap.h"
#include "chap.h"
#include "cbcp.h"
#include "command.h"

static void Despatch(struct bundle *, struct link *, struct mbuf *, u_short);

static inline void
link_AddInOctets(struct link *l, int n)
{
  if (l->stats.gather) {
    throughput_addin(&l->stats.total, n);
    if (l->stats.parent)
      throughput_addin(l->stats.parent, n);
  }
}

static inline void
link_AddOutOctets(struct link *l, int n)
{
  if (l->stats.gather) {
    throughput_addout(&l->stats.total, n);
    if (l->stats.parent)
      throughput_addout(l->stats.parent, n);
  }
}

void
link_SequenceQueue(struct link *l)
{
  struct mqueue *queue, *highest;

  log_Printf(LogDEBUG, "link_SequenceQueue\n");

  highest = LINK_HIGHQ(l);
  for (queue = l->Queue; queue < highest; queue++)
    while (queue->len)
      m_enqueue(highest, m_dequeue(queue));
}

void
link_DeleteQueue(struct link *l)
{
  struct mqueue *queue, *highest;

  highest = LINK_HIGHQ(l);
  for (queue = l->Queue; queue <= highest; queue++)
    while (queue->top)
      m_freem(m_dequeue(queue));
}

size_t
link_QueueLen(struct link *l)
{
  unsigned i;
  size_t len;

  for (i = 0, len = 0; i < LINK_QUEUES(l); i++)
    len += l->Queue[i].len;

  return len;
}

size_t
link_QueueBytes(struct link *l)
{
  unsigned i;
  size_t len, bytes;
  struct mbuf *m;

  bytes = 0;
  for (i = 0, len = 0; i < LINK_QUEUES(l); i++) {
    len = l->Queue[i].len;
    m = l->Queue[i].top;
    while (len--) {
      bytes += m_length(m);
      m = m->m_nextpkt;
    }
  }

  return bytes;
}

void
link_PendingLowPriorityData(struct link *l, size_t *pkts, size_t *octets)
{
  struct mqueue *queue, *highest;
  struct mbuf *m;
  size_t len;

  /*
   * This is all rfc1989 stuff... because our LQR packet is going to bypass
   * everything that's not in the highest priority queue, we must be able to
   * subtract that data from our outgoing packet/octet counts.  However,
   * we've already async-encoded our data at this point, but the async
   * encodings MUSTn't be a part of the LQR-reported payload :(  So, we have
   * the async layer record how much it's padded the packet in the mbuf's
   * priv field, and when we calculate our outgoing LQR values we subtract
   * this value for each packet from the octet count sent.
   */

  highest = LINK_HIGHQ(l);
  *pkts = *octets = 0;
  for (queue = l->Queue; queue < highest; queue++) {
    len = queue->len;
    *pkts += len;
    for (m = queue->top; len--; m = m->m_nextpkt)
      *octets += m_length(m) - m->priv;
  }
}

struct mbuf *
link_Dequeue(struct link *l)
{
  int pri;
  struct mbuf *bp;

  for (bp = NULL, pri = LINK_QUEUES(l) - 1; pri >= 0; pri--)
    if (l->Queue[pri].len) {
      bp = m_dequeue(l->Queue + pri);
      log_Printf(LogDEBUG, "link_Dequeue: Dequeued from queue %d,"
                " containing %lu more packets\n", pri,
                (u_long)l->Queue[pri].len);
      break;
    }

  return bp;
}

static struct protostatheader {
  u_short number;
  const char *name;
} ProtocolStat[NPROTOSTAT] = {
  { PROTO_IP, "IP" },
  { PROTO_VJUNCOMP, "VJ_UNCOMP" },
  { PROTO_VJCOMP, "VJ_COMP" },
  { PROTO_COMPD, "COMPD" },
  { PROTO_ICOMPD, "ICOMPD" },
  { PROTO_LCP, "LCP" },
  { PROTO_IPCP, "IPCP" },
  { PROTO_CCP, "CCP" },
  { PROTO_PAP, "PAP" },
  { PROTO_LQR, "LQR" },
  { PROTO_CHAP, "CHAP" },
  { PROTO_MP, "MULTILINK" },
  { 0, "Others" }	/* must be last */
};

void
link_ProtocolRecord(struct link *l, u_short proto, int type)
{
  int i;

  for (i = 0; i < NPROTOSTAT; i++)
    if (ProtocolStat[i].number == proto || ProtocolStat[i].number == 0) {
      if (type == PROTO_IN)
        l->proto_in[i]++;
      else
        l->proto_out[i]++;
      break;
    }
}

void
link_ReportProtocolStatus(struct link *l, struct prompt *prompt)
{
  int i;

  prompt_Printf(prompt, "    Protocol     in        out      "
                "Protocol      in       out\n");
  for (i = 0; i < NPROTOSTAT; i++) {
    prompt_Printf(prompt, "   %-9s: %8lu, %8lu",
	    ProtocolStat[i].name, l->proto_in[i], l->proto_out[i]);
    if ((i % 2) == 0)
      prompt_Printf(prompt, "\n");
  }
  if (!(i % 2))
    prompt_Printf(prompt, "\n");
}

void
link_PushPacket(struct link *l, struct mbuf *bp, struct bundle *b, int pri,
                u_short proto)
{
  int layer;

  /*
   * When we ``push'' a packet into the link, it gets processed by the
   * ``push'' function in each layer starting at the top.
   * We never expect the result of a ``push'' to be more than one
   * packet (as we do with ``pull''s).
   */

  if(pri < 0 || (unsigned)pri >= LINK_QUEUES(l))
    pri = 0;

  bp->priv = 0;		/* Adjusted by the async layer ! */
  for (layer = l->nlayers; layer && bp; layer--)
    if (l->layer[layer - 1]->push != NULL)
      bp = (*l->layer[layer - 1]->push)(b, l, bp, pri, &proto);

  if (bp) {
    link_AddOutOctets(l, m_length(bp));
    log_Printf(LogDEBUG, "link_PushPacket: Transmit proto 0x%04x\n", proto);
    m_enqueue(l->Queue + pri, m_pullup(bp));
  }
}

void
link_PullPacket(struct link *l, char *buf, size_t len, struct bundle *b)
{
  struct mbuf *bp, *lbp[LAYER_MAX], *next;
  u_short lproto[LAYER_MAX], proto;
  int layer;

  /*
   * When we ``pull'' a packet from the link, it gets processed by the
   * ``pull'' function in each layer starting at the bottom.
   * Each ``pull'' may produce multiple packets, chained together using
   * bp->m_nextpkt.
   * Each packet that results from each pull has to be pulled through
   * all of the higher layers before the next resulting packet is pulled
   * through anything; this ensures that packets that depend on the
   * fsm state resulting from the receipt of the previous packet aren't
   * surprised.
   */

  link_AddInOctets(l, len);

  memset(lbp, '\0', sizeof lbp);
  lbp[0] = m_get(len, MB_UNKNOWN);
  memcpy(MBUF_CTOP(lbp[0]), buf, len);
  lproto[0] = 0;
  layer = 0;

  while (layer || lbp[layer]) {
    if (lbp[layer] == NULL) {
      layer--;
      continue;
    }
    bp = lbp[layer];
    lbp[layer] = bp->m_nextpkt;
    bp->m_nextpkt = NULL;
    proto = lproto[layer];

    if (l->layer[layer]->pull != NULL)
      bp = (*l->layer[layer]->pull)(b, l, bp, &proto);

    if (layer == l->nlayers - 1) {
      /* We've just done the top layer, despatch the packet(s) */
      while (bp) {
        next = bp->m_nextpkt;
        bp->m_nextpkt = NULL;
        log_Printf(LogDEBUG, "link_PullPacket: Despatch proto 0x%04x\n", proto);
        Despatch(b, l, bp, proto);
        bp = next;
      }
    } else {
      lbp[++layer] = bp;
      lproto[layer] = proto;
    }
  }
}

int
link_Stack(struct link *l, struct layer *layer)
{
  if (l->nlayers == sizeof l->layer / sizeof l->layer[0]) {
    log_Printf(LogERROR, "%s: Oops, cannot stack a %s layer...\n",
               l->name, layer->name);
    return 0;
  }
  l->layer[l->nlayers++] = layer;
  return 1;
}

void
link_EmptyStack(struct link *l)
{
  l->nlayers = 0;
}

static const struct {
  u_short proto;
  struct mbuf *(*fn)(struct bundle *, struct link *, struct mbuf *);
} despatcher[] = {
  { PROTO_IP, ipv4_Input },
#ifndef NOINET6
  { PROTO_IPV6, ipv6_Input },
#endif
  { PROTO_MP, mp_Input },
  { PROTO_LCP, lcp_Input },
  { PROTO_IPCP, ipcp_Input },
#ifndef NOINET6
  { PROTO_IPV6CP, ipv6cp_Input },
#endif
  { PROTO_PAP, pap_Input },
  { PROTO_CHAP, chap_Input },
  { PROTO_CCP, ccp_Input },
  { PROTO_LQR, lqr_Input },
  { PROTO_CBCP, cbcp_Input }
};

#define DSIZE (sizeof despatcher / sizeof despatcher[0])

static void
Despatch(struct bundle *bundle, struct link *l, struct mbuf *bp, u_short proto)
{
  unsigned f;

  for (f = 0; f < DSIZE; f++)
    if (despatcher[f].proto == proto) {
      bp = (*despatcher[f].fn)(bundle, l, bp);
      break;
    }

  if (bp) {
    struct physical *p = link2physical(l);

    log_Printf(LogPHASE, "%s protocol 0x%04x (%s)\n",
               f == DSIZE ? "Unknown" : "Unexpected", proto,
               hdlc_Protocol2Nam(proto));
    bp = m_pullup(proto_Prepend(bp, proto, 0, 0));
    lcp_SendProtoRej(&l->lcp, MBUF_CTOP(bp), bp->m_len);
    if (p) {
      p->hdlc.lqm.ifInDiscards++;
      p->hdlc.stats.unknownproto++;
    }
    m_freem(bp);
  }
}

int
link_ShowLayers(struct cmdargs const *arg)
{
  struct link *l = command_ChooseLink(arg);
  int layer;

  for (layer = l->nlayers; layer; layer--)
    prompt_Printf(arg->prompt, "%s%s", layer == l->nlayers ? "" : ", ",
                  l->layer[layer - 1]->name);
  if (l->nlayers)
    prompt_Printf(arg->prompt, "\n");

  return 0;
}

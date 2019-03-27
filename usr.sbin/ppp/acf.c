/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Brian Somers <brian@Awfulhak.org>
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

#include <stdio.h>
#include <termios.h>

#include "defs.h"
#include "layer.h"
#include "timer.h"
#include "fsm.h"
#include "log.h"
#include "mbuf.h"
#include "acf.h"
#include "proto.h"
#include "throughput.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "descriptor.h"
#include "async.h"
#include "physical.h"

int
acf_WrapperOctets(struct lcp *lcp, u_short proto)
{
  return (proto == PROTO_LCP || lcp->his_acfcomp == 0) ? 2 : 0;
}

static struct mbuf *
acf_LayerPush(struct bundle *b __unused, struct link *l, struct mbuf *bp,
              int pri __unused, u_short *proto)
{
  const u_char cp[2] = { HDLC_ADDR, HDLC_UI };

  if (*proto == PROTO_LCP || l->lcp.his_acfcomp == 0) {
    bp = m_prepend(bp, cp, 2, 0);
    m_settype(bp, MB_ACFOUT);
  }

  return bp;
}

static struct mbuf *
acf_LayerPull(struct bundle *b __unused, struct link *l, struct mbuf *bp,
	      u_short *proto __unused)
{
  struct physical *p = link2physical(l);
  u_char cp[2];

  if (!p) {
    log_Printf(LogERROR, "Can't Pull an acf packet from a logical link\n");
    return bp;
  }

  if (mbuf_View(bp, cp, 2) == 2) {
    if (!p->link.lcp.want_acfcomp) {
      /* We expect the packet not to be compressed */
      bp = mbuf_Read(bp, cp, 2);
      if (cp[0] != HDLC_ADDR) {
        p->hdlc.lqm.ifInErrors++;
        p->hdlc.stats.badaddr++;
        log_Printf(LogDEBUG, "acf_LayerPull: addr 0x%02x\n", cp[0]);
        m_freem(bp);
        return NULL;
      }
      if (cp[1] != HDLC_UI) {
        p->hdlc.lqm.ifInErrors++;
        p->hdlc.stats.badcommand++;
        log_Printf(LogDEBUG, "acf_LayerPull: control 0x%02x\n", cp[1]);
        m_freem(bp);
        return NULL;
      }
      m_settype(bp, MB_ACFIN);
    } else if (cp[0] == HDLC_ADDR && cp[1] == HDLC_UI) {
      /*
       * We can receive compressed packets, but the peer still sends
       * uncompressed packets (or maybe this is a PROTO_LCP packet) !
       */
      bp = mbuf_Read(bp, cp, 2);
      m_settype(bp, MB_ACFIN);
    }
  }

  return bp;
}

struct layer acflayer = { LAYER_ACF, "acf", acf_LayerPush, acf_LayerPull };

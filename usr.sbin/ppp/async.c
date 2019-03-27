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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <string.h>
#include <termios.h>

#include "layer.h"
#include "mbuf.h"
#include "log.h"
#include "defs.h"
#include "timer.h"
#include "fsm.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "proto.h"
#include "async.h"
#include "throughput.h"
#include "ccp.h"
#include "link.h"
#include "descriptor.h"
#include "physical.h"

#define MODE_HUNT 0x01
#define MODE_ESC  0x02

void
async_Init(struct async *async)
{
  async_Setup(async);
  memset(async->cfg.EscMap, '\0', sizeof async->cfg.EscMap);
}

void
async_Setup(struct async *async)
{
  async->mode = MODE_HUNT;
  async->length = 0;
  async->my_accmap = async->his_accmap = 0xffffffff;
}

void
async_SetLinkParams(struct async *async, u_int32_t mymap, u_int32_t hismap)
{
  async->my_accmap = mymap;
  async->his_accmap = hismap | mymap;
}

/*
 * Encode into async HDLC byte code
 */
static void
async_Encode(struct async *async, u_char **cp, u_char c, int proto)
{
  u_char *wp;

  wp = *cp;
  if ((c < 0x20 && (proto == PROTO_LCP || (async->his_accmap & (1 << c))))
      || (c == HDLC_ESC) || (c == HDLC_SYN)) {
    *wp++ = HDLC_ESC;
    c ^= HDLC_XOR;
  }
  if (async->cfg.EscMap[32] && async->cfg.EscMap[c >> 3] & (1 << (c & 7))) {
    *wp++ = HDLC_ESC;
    c ^= HDLC_XOR;
  }
  *wp++ = c;
  *cp = wp;
}

static struct mbuf *
async_LayerPush(struct bundle *b __unused, struct link *l, struct mbuf *bp,
                int pri __unused, u_short *proto)
{
  struct physical *p = link2physical(l);
  u_char *cp, *sp, *ep;
  struct mbuf *wp;
  size_t oldcnt;
  size_t cnt;

  if (!p || m_length(bp) > HDLCSIZE) {
    m_freem(bp);
    return NULL;
  }

  oldcnt = m_length(bp);

  cp = p->async.xbuff;
  ep = cp + HDLCSIZE - 10;
  wp = bp;
  *cp++ = HDLC_SYN;
  while (wp) {
    sp = MBUF_CTOP(wp);
    for (cnt = wp->m_len; cnt > 0; cnt--) {
      async_Encode(&p->async, &cp, *sp++, *proto);
      if (cp >= ep) {
	m_freem(bp);
	return NULL;
      }
    }
    wp = wp->m_next;
  }
  *cp++ = HDLC_SYN;

  cnt = cp - p->async.xbuff;
  m_freem(bp);
  bp = m_get(cnt, MB_ASYNCOUT);
  memcpy(MBUF_CTOP(bp), p->async.xbuff, cnt);
  bp->priv = cnt - oldcnt;
  log_DumpBp(LogASYNC, "Write", bp);

  return bp;
}

static struct mbuf *
async_Decode(struct async *async, u_char c)
{
  struct mbuf *bp;

  if ((async->mode & MODE_HUNT) && c != HDLC_SYN)
    return NULL;

  switch (c) {
  case HDLC_SYN:
    async->mode &= ~MODE_HUNT;
    if (async->length) {		/* packet is ready. */
      bp = m_get(async->length, MB_ASYNCIN);
      mbuf_Write(bp, async->hbuff, async->length);
      async->length = 0;
      return bp;
    }
    break;
  case HDLC_ESC:
    if (!(async->mode & MODE_ESC)) {
      async->mode |= MODE_ESC;
      break;
    }
    /* FALLTHROUGH */
  default:
    if (async->length >= HDLCSIZE) {
      /* packet is too large, discard it */
      log_Printf(LogWARN, "Packet too large (%d), discarding.\n",
                 async->length);
      async->length = 0;
      async->mode = MODE_HUNT;
      break;
    }
    if (async->mode & MODE_ESC) {
      c ^= HDLC_XOR;
      async->mode &= ~MODE_ESC;
    }
    async->hbuff[async->length++] = c;
    break;
  }
  return NULL;
}

static struct mbuf *
async_LayerPull(struct bundle *b __unused, struct link *l, struct mbuf *bp,
                u_short *proto __unused)
{
  struct mbuf *nbp, **last;
  struct physical *p = link2physical(l);
  u_char *ch;
  size_t cnt;

  if (!p) {
    log_Printf(LogERROR, "Can't Pull an async packet from a logical link\n");
    return bp;
  }

  last = &nbp;

  log_DumpBp(LogASYNC, "Read", bp);
  while (bp) {
    ch = MBUF_CTOP(bp);
    for (cnt = bp->m_len; cnt; cnt--) {
      *last = async_Decode(&p->async, *ch++);
      if (*last != NULL)
        last = &(*last)->m_nextpkt;
    }
    bp = m_free(bp);
  }

  return nbp;
}

struct layer asynclayer =
  { LAYER_ASYNC, "async", async_LayerPush, async_LayerPull };

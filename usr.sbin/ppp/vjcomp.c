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
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <stdio.h>
#include <string.h>		/* strlen/memcpy */
#include <termios.h>

#include "layer.h"
#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "fsm.h"
#include "proto.h"
#include "slcompress.h"
#include "lqr.h"
#include "hdlc.h"
#include "defs.h"
#include "iplist.h"
#include "throughput.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "filter.h"
#include "descriptor.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "vjcomp.h"

#define MAX_VJHEADER 16		/* Maximum size of compressed header */

static struct mbuf *
vj_LayerPush(struct bundle *bundle, struct link *l __unused, struct mbuf *bp,
	     int pri __unused, u_short *proto)
{
  int type;
  struct ip *pip;
  u_short cproto = bundle->ncp.ipcp.peer_compproto >> 16;

  bp = m_pullup(bp);
  pip = (struct ip *)MBUF_CTOP(bp);
  if (*proto == PROTO_IP && pip->ip_p == IPPROTO_TCP &&
      cproto == PROTO_VJCOMP) {
    type = sl_compress_tcp(bp, pip, &bundle->ncp.ipcp.vj.cslc,
                           &bundle->ncp.ipcp.vj.slstat,
                           bundle->ncp.ipcp.peer_compproto & 0xff);
    log_Printf(LogDEBUG, "vj_LayerWrite: type = %x\n", type);
    switch (type) {
    case TYPE_IP:
      break;

    case TYPE_UNCOMPRESSED_TCP:
      *proto = PROTO_VJUNCOMP;
      log_Printf(LogDEBUG, "vj_LayerPush: PROTO_IP -> PROTO_VJUNCOMP\n");
      m_settype(bp, MB_VJOUT);
      break;

    case TYPE_COMPRESSED_TCP:
      *proto = PROTO_VJCOMP;
      log_Printf(LogDEBUG, "vj_LayerPush: PROTO_IP -> PROTO_VJUNCOMP\n");
      m_settype(bp, MB_VJOUT);
      break;

    default:
      log_Printf(LogERROR, "vj_LayerPush: Unknown frame type %x\n", type);
      m_freem(bp);
      return NULL;
    }
  }

  return bp;
}

static struct mbuf *
VjUncompressTcp(struct ipcp *ipcp, struct mbuf *bp, u_char type)
{
  u_char *bufp;
  int len, olen, rlen;
  u_char work[MAX_HDR + MAX_VJHEADER];	/* enough to hold TCP/IP header */

  bp = m_pullup(bp);
  olen = len = m_length(bp);
  if (type == TYPE_UNCOMPRESSED_TCP) {
    /*
     * Uncompressed packet does NOT change its size, so that we can use mbuf
     * space for uncompression job.
     */
    bufp = MBUF_CTOP(bp);
    len = sl_uncompress_tcp(&bufp, len, type, &ipcp->vj.cslc, &ipcp->vj.slstat,
                            (ipcp->my_compproto >> 8) & 255);
    if (len <= 0) {
      m_freem(bp);
      bp = NULL;
    } else
      m_settype(bp, MB_VJIN);
    return bp;
  }

  /*
   * Handle compressed packet. 1) Read up to MAX_VJHEADER bytes into work
   * space. 2) Try to uncompress it. 3) Compute amount of necessary space. 4)
   * Copy unread data info there.
   */
  if (len > MAX_VJHEADER)
    len = MAX_VJHEADER;
  rlen = len;
  bufp = work + MAX_HDR;
  bp = mbuf_Read(bp, bufp, rlen);
  len = sl_uncompress_tcp(&bufp, olen, type, &ipcp->vj.cslc, &ipcp->vj.slstat,
                          (ipcp->my_compproto >> 8) & 255);
  if (len <= 0) {
    m_freem(bp);
    return NULL;
  }
  len -= olen;
  len += rlen;

  bp = m_prepend(bp, bufp, len, 0);
  m_settype(bp, MB_VJIN);

  return bp;
}

static struct mbuf *
vj_LayerPull(struct bundle *bundle, struct link *l __unused, struct mbuf *bp,
             u_short *proto)
{
  u_char type;

  switch (*proto) {
  case PROTO_VJCOMP:
    type = TYPE_COMPRESSED_TCP;
    log_Printf(LogDEBUG, "vj_LayerPull: PROTO_VJCOMP -> PROTO_IP\n");
    break;
  case PROTO_VJUNCOMP:
    type = TYPE_UNCOMPRESSED_TCP;
    log_Printf(LogDEBUG, "vj_LayerPull: PROTO_VJUNCOMP -> PROTO_IP\n");
    break;
  default:
    return bp;
  }

  *proto = PROTO_IP;
  return VjUncompressTcp(&bundle->ncp.ipcp, bp, type);
}

const char *
vj2asc(u_int32_t val)
{
  static char asc[50];		/* The return value is used immediately */

  if (val)
    snprintf(asc, sizeof asc, "%d VJ slots with%s slot compression",
            (int)((val>>8)&15)+1, val & 1 ?  "" : "out");
  else
    strcpy(asc, "VJ disabled");
  return asc;
}

struct layer vjlayer = { LAYER_VJ, "vj", vj_LayerPush, vj_LayerPull };

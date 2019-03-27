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

#ifdef __FreeBSD__
#include <netinet/in.h>
#endif
#include <sys/un.h>

#include <string.h>
#include <termios.h>

#include "layer.h"
#include "mbuf.h"
#include "log.h"
#include "defs.h"
#include "timer.h"
#include "fsm.h"
#include "acf.h"
#include "proto.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "async.h"
#include "throughput.h"
#include "ccp.h"
#include "link.h"
#include "descriptor.h"
#include "physical.h"
#include "mp.h"
#include "chat.h"
#include "auth.h"
#include "chap.h"
#include "command.h"
#include "cbcp.h"
#include "datalink.h"

struct echolqr {
  u_int32_t magic;
  u_int32_t signature;
  u_int32_t sequence;
};

#define	SIGNATURE  0x594e4f54

static void
SendEchoReq(struct lcp *lcp)
{
  struct hdlc *hdlc = &link2physical(lcp->fsm.link)->hdlc;
  struct echolqr echo;

  echo.magic = htonl(lcp->want_magic);
  echo.signature = htonl(SIGNATURE);
  echo.sequence = htonl(hdlc->lqm.echo.seq_sent);
  fsm_Output(&lcp->fsm, CODE_ECHOREQ, hdlc->lqm.echo.seq_sent++,
            (u_char *)&echo, sizeof echo, MB_ECHOOUT);
}

struct mbuf *
lqr_RecvEcho(struct fsm *fp, struct mbuf *bp)
{
  struct hdlc *hdlc = &link2physical(fp->link)->hdlc;
  struct lcp *lcp = fsm2lcp(fp);
  struct echolqr lqr;

  if (m_length(bp) >= sizeof lqr) {
    m_freem(mbuf_Read(bp, &lqr, sizeof lqr));
    bp = NULL;
    lqr.magic = ntohl(lqr.magic);
    lqr.signature = ntohl(lqr.signature);
    lqr.sequence = ntohl(lqr.sequence);

    /* Tolerate echo replies with either magic number */
    if (lqr.magic != 0 && lqr.magic != lcp->his_magic &&
        lqr.magic != lcp->want_magic) {
      log_Printf(LogWARN, "%s: lqr_RecvEcho: Bad magic: expected 0x%08x,"
                 " got 0x%08x\n", fp->link->name, lcp->his_magic, lqr.magic);
      /*
       * XXX: We should send a terminate request. But poor implementations may
       *      die as a result.
       */
    }
    if (lqr.signature == SIGNATURE
	|| lqr.signature == lcp->want_magic) {			/* some implementations return the wrong magic */
      /* careful not to update lqm.echo.seq_recv with older values */
      if ((hdlc->lqm.echo.seq_recv > (u_int32_t)0 - 5 && lqr.sequence < 5) ||
          (hdlc->lqm.echo.seq_recv <= (u_int32_t)0 - 5 &&
           lqr.sequence > hdlc->lqm.echo.seq_recv))
        hdlc->lqm.echo.seq_recv = lqr.sequence;
    } else
      log_Printf(LogWARN, "lqr_RecvEcho: Got sig 0x%08lx, not 0x%08lx !\n",
                (u_long)lqr.signature, (u_long)SIGNATURE);
  } else
    log_Printf(LogWARN, "lqr_RecvEcho: Got packet size %zd, expecting %ld !\n",
              m_length(bp), (long)sizeof(struct echolqr));
  return bp;
}

void
lqr_ChangeOrder(struct lqrdata *src, struct lqrdata *dst)
{
  u_int32_t *sp, *dp;
  unsigned n;

  sp = (u_int32_t *) src;
  dp = (u_int32_t *) dst;
  for (n = 0; n < sizeof(struct lqrdata) / sizeof(u_int32_t); n++, sp++, dp++)
    *dp = ntohl(*sp);
}

static void
SendLqrData(struct lcp *lcp)
{
  struct mbuf *bp;
  int extra;

  extra = proto_WrapperOctets(lcp, PROTO_LQR) +
          acf_WrapperOctets(lcp, PROTO_LQR);
  bp = m_get(sizeof(struct lqrdata) + extra, MB_LQROUT);
  bp->m_len -= extra;
  bp->m_offset += extra;

  /*
   * Send on the highest priority queue.  We send garbage - the real data
   * is written by lqr_LayerPush() where we know how to fill in all the
   * fields.  Note, lqr_LayerPush() ``knows'' that we're pushing onto the
   * highest priority queue, and factors out packet & octet values from
   * other queues!
   */
  link_PushPacket(lcp->fsm.link, bp, lcp->fsm.bundle,
                  LINK_QUEUES(lcp->fsm.link) - 1, PROTO_LQR);
}

static void
SendLqrReport(void *v)
{
  struct lcp *lcp = (struct lcp *)v;
  struct physical *p = link2physical(lcp->fsm.link);

  timer_Stop(&p->hdlc.lqm.timer);

  if (p->hdlc.lqm.method & LQM_LQR) {
    if (p->hdlc.lqm.lqr.resent > 5) {
      /* XXX: Should implement LQM strategy */
      log_Printf(LogPHASE, "%s: ** Too many LQR packets lost **\n",
                lcp->fsm.link->name);
      log_Printf(LogLQM, "%s: Too many LQR packets lost\n",
                lcp->fsm.link->name);
      p->hdlc.lqm.method = 0;
      datalink_Down(p->dl, CLOSE_NORMAL);
    } else {
      SendLqrData(lcp);
      p->hdlc.lqm.lqr.resent++;
    }
  } else if (p->hdlc.lqm.method & LQM_ECHO) {
    if ((p->hdlc.lqm.echo.seq_sent > 5 &&
         p->hdlc.lqm.echo.seq_sent - 5 > p->hdlc.lqm.echo.seq_recv) ||
        (p->hdlc.lqm.echo.seq_sent <= 5 &&
         p->hdlc.lqm.echo.seq_sent > p->hdlc.lqm.echo.seq_recv + 5)) {
      log_Printf(LogPHASE, "%s: ** Too many LCP ECHO packets lost **\n",
                lcp->fsm.link->name);
      log_Printf(LogLQM, "%s: Too many LCP ECHO packets lost\n",
                lcp->fsm.link->name);
      p->hdlc.lqm.method = 0;
      datalink_Down(p->dl, CLOSE_NORMAL);
    } else
      SendEchoReq(lcp);
  }
  if (p->hdlc.lqm.method && p->hdlc.lqm.timer.load)
    timer_Start(&p->hdlc.lqm.timer);
}

struct mbuf *
lqr_Input(struct bundle *bundle __unused, struct link *l, struct mbuf *bp)
{
  struct physical *p = link2physical(l);
  struct lcp *lcp = p->hdlc.lqm.owner;
  int len;

  if (p == NULL) {
    log_Printf(LogERROR, "lqr_Input: Not a physical link - dropped\n");
    m_freem(bp);
    return NULL;
  }

  len = m_length(bp);
  if (len != sizeof(struct lqrdata))
    log_Printf(LogWARN, "lqr_Input: Got packet size %d, expecting %ld !\n",
              len, (long)sizeof(struct lqrdata));
  else if (!IsAccepted(l->lcp.cfg.lqr) && !(p->hdlc.lqm.method & LQM_LQR)) {
    bp = m_pullup(proto_Prepend(bp, PROTO_LQR, 0, 0));
    lcp_SendProtoRej(lcp, MBUF_CTOP(bp), bp->m_len);
  } else {
    struct lqrdata *lqr;

    bp = m_pullup(bp);
    lqr = (struct lqrdata *)MBUF_CTOP(bp);
    if (ntohl(lqr->MagicNumber) != lcp->his_magic)
      log_Printf(LogWARN, "lqr_Input: magic 0x%08lx is wrong,"
                 " expecting 0x%08lx\n",
		 (u_long)ntohl(lqr->MagicNumber), (u_long)lcp->his_magic);
    else {
      struct lqrdata lastlqr;

      memcpy(&lastlqr, &p->hdlc.lqm.lqr.peer, sizeof lastlqr);
      lqr_ChangeOrder(lqr, &p->hdlc.lqm.lqr.peer);
      lqr_Dump(l->name, "Input", &p->hdlc.lqm.lqr.peer);
      /* we have received an LQR from our peer */
      p->hdlc.lqm.lqr.resent = 0;

      /* Snapshot our state when the LQR packet was received */
      memcpy(&p->hdlc.lqm.lqr.prevSave, &p->hdlc.lqm.lqr.Save,
             sizeof p->hdlc.lqm.lqr.prevSave);
      p->hdlc.lqm.lqr.Save.InLQRs = ++p->hdlc.lqm.lqr.InLQRs;
      p->hdlc.lqm.lqr.Save.InPackets = p->hdlc.lqm.ifInUniPackets;
      p->hdlc.lqm.lqr.Save.InDiscards = p->hdlc.lqm.ifInDiscards;
      p->hdlc.lqm.lqr.Save.InErrors = p->hdlc.lqm.ifInErrors;
      p->hdlc.lqm.lqr.Save.InOctets = p->hdlc.lqm.lqr.InGoodOctets;

      lqr_Analyse(&p->hdlc, &lastlqr, &p->hdlc.lqm.lqr.peer);

      /*
       * Generate an LQR response if we're not running an LQR timer OR
       * two successive LQR's PeerInLQRs are the same.
       */
      if (p->hdlc.lqm.timer.load == 0 || !(p->hdlc.lqm.method & LQM_LQR) ||
          (lastlqr.PeerInLQRs &&
           lastlqr.PeerInLQRs == p->hdlc.lqm.lqr.peer.PeerInLQRs))
        SendLqrData(lcp);
    }
  }
  m_freem(bp);
  return NULL;
}

/*
 *  When LCP is reached to opened state, We'll start LQM activity.
 */
static void
lqr_Setup(struct lcp *lcp)
{
  struct physical *physical = link2physical(lcp->fsm.link);
  int period;

  physical->hdlc.lqm.lqr.resent = 0;
  physical->hdlc.lqm.echo.seq_sent = 0;
  physical->hdlc.lqm.echo.seq_recv = 0;
  memset(&physical->hdlc.lqm.lqr.peer, '\0',
         sizeof physical->hdlc.lqm.lqr.peer);

  physical->hdlc.lqm.method = lcp->cfg.echo ? LQM_ECHO : 0;
  if (IsEnabled(lcp->cfg.lqr) && !REJECTED(lcp, TY_QUALPROTO))
    physical->hdlc.lqm.method |= LQM_LQR;
  timer_Stop(&physical->hdlc.lqm.timer);

  physical->hdlc.lqm.lqr.peer_timeout = lcp->his_lqrperiod;
  if (lcp->his_lqrperiod)
    log_Printf(LogLQM, "%s: Expecting LQR every %d.%02d secs\n",
              physical->link.name, lcp->his_lqrperiod / 100,
              lcp->his_lqrperiod % 100);

  period = lcp->want_lqrperiod ?
    lcp->want_lqrperiod : lcp->cfg.lqrperiod * 100;
  physical->hdlc.lqm.timer.func = SendLqrReport;
  physical->hdlc.lqm.timer.name = "lqm";
  physical->hdlc.lqm.timer.arg = lcp;

  if (lcp->want_lqrperiod || physical->hdlc.lqm.method & LQM_ECHO) {
    log_Printf(LogLQM, "%s: Will send %s every %d.%02d secs\n",
              physical->link.name, lcp->want_lqrperiod ? "LQR" : "LCP ECHO",
              period / 100, period % 100);
    physical->hdlc.lqm.timer.load = period * SECTICKS / 100;
  } else {
    physical->hdlc.lqm.timer.load = 0;
    if (!lcp->his_lqrperiod)
      log_Printf(LogLQM, "%s: LQR/LCP ECHO not negotiated\n",
                 physical->link.name);
  }
}

void
lqr_Start(struct lcp *lcp)
{
  struct physical *p = link2physical(lcp->fsm.link);

  lqr_Setup(lcp);
  if (p->hdlc.lqm.timer.load)
    SendLqrReport(lcp);
}

void
lqr_reStart(struct lcp *lcp)
{
  struct physical *p = link2physical(lcp->fsm.link);

  lqr_Setup(lcp);
  if (p->hdlc.lqm.timer.load)
    timer_Start(&p->hdlc.lqm.timer);
}

void
lqr_StopTimer(struct physical *physical)
{
  timer_Stop(&physical->hdlc.lqm.timer);
}

void
lqr_Stop(struct physical *physical, int method)
{
  if (method == LQM_LQR)
    log_Printf(LogLQM, "%s: Stop sending LQR, Use LCP ECHO instead.\n",
               physical->link.name);
  if (method == LQM_ECHO)
    log_Printf(LogLQM, "%s: Stop sending LCP ECHO.\n",
               physical->link.name);
  physical->hdlc.lqm.method &= ~method;
  if (physical->hdlc.lqm.method)
    SendLqrReport(physical->hdlc.lqm.owner);
  else
    timer_Stop(&physical->hdlc.lqm.timer);
}

void
lqr_Dump(const char *link, const char *message, const struct lqrdata *lqr)
{
  if (log_IsKept(LogLQM)) {
    log_Printf(LogLQM, "%s: %s:\n", link, message);
    log_Printf(LogLQM, "  Magic:          %08x   LastOutLQRs:    %08x\n",
	      lqr->MagicNumber, lqr->LastOutLQRs);
    log_Printf(LogLQM, "  LastOutPackets: %08x   LastOutOctets:  %08x\n",
	      lqr->LastOutPackets, lqr->LastOutOctets);
    log_Printf(LogLQM, "  PeerInLQRs:     %08x   PeerInPackets:  %08x\n",
	      lqr->PeerInLQRs, lqr->PeerInPackets);
    log_Printf(LogLQM, "  PeerInDiscards: %08x   PeerInErrors:   %08x\n",
	      lqr->PeerInDiscards, lqr->PeerInErrors);
    log_Printf(LogLQM, "  PeerInOctets:   %08x   PeerOutLQRs:    %08x\n",
	      lqr->PeerInOctets, lqr->PeerOutLQRs);
    log_Printf(LogLQM, "  PeerOutPackets: %08x   PeerOutOctets:  %08x\n",
	      lqr->PeerOutPackets, lqr->PeerOutOctets);
  }
}

void
lqr_Analyse(const struct hdlc *hdlc, const struct lqrdata *oldlqr,
            const struct lqrdata *newlqr)
{
  u_int32_t LQRs, transitLQRs, pkts, octets, disc, err;

  if (!newlqr->PeerInLQRs)	/* No analysis possible yet! */
    return;

  log_Printf(LogLQM, "Analysis:\n");

  LQRs = (newlqr->LastOutLQRs - oldlqr->LastOutLQRs) -
         (newlqr->PeerInLQRs - oldlqr->PeerInLQRs);
  transitLQRs = hdlc->lqm.lqr.OutLQRs - newlqr->LastOutLQRs;
  pkts = (newlqr->LastOutPackets - oldlqr->LastOutPackets) -
         (newlqr->PeerInPackets - oldlqr->PeerInPackets);
  octets = (newlqr->LastOutOctets - oldlqr->LastOutOctets) -
           (newlqr->PeerInOctets - oldlqr->PeerInOctets);
  log_Printf(LogLQM, "  Outbound lossage: %d LQR%s (%d en route), %d packet%s,"
             " %d octet%s\n", (int)LQRs, LQRs == 1 ? "" : "s", (int)transitLQRs,
	     (int)pkts, pkts == 1 ? "" : "s",
	     (int)octets, octets == 1 ? "" : "s");

  pkts = (newlqr->PeerOutPackets - oldlqr->PeerOutPackets) -
    (hdlc->lqm.lqr.Save.InPackets - hdlc->lqm.lqr.prevSave.InPackets);
  octets = (newlqr->PeerOutOctets - oldlqr->PeerOutOctets) -
    (hdlc->lqm.lqr.Save.InOctets - hdlc->lqm.lqr.prevSave.InOctets);
  log_Printf(LogLQM, "  Inbound lossage: %d packet%s, %d octet%s\n",
	     (int)pkts, pkts == 1 ? "" : "s",
	     (int)octets, octets == 1 ? "" : "s");

  disc = newlqr->PeerInDiscards - oldlqr->PeerInDiscards;
  err = newlqr->PeerInErrors - oldlqr->PeerInErrors;
  if (disc && err)
    log_Printf(LogLQM, "                   Likely due to both peer congestion"
               " and physical errors\n");
  else if (disc)
    log_Printf(LogLQM, "                   Likely due to peer congestion\n");
  else if (err)
    log_Printf(LogLQM, "                   Likely due to physical errors\n");
  else if (pkts)
    log_Printf(LogLQM, "                   Likely due to transport "
	       "congestion\n");
}

static struct mbuf *
lqr_LayerPush(struct bundle *b __unused, struct link *l, struct mbuf *bp,
              int pri __unused, u_short *proto)
{
  struct physical *p = link2physical(l);
  int len, layer;

  if (!p) {
    /* Oops - can't happen :-] */
    m_freem(bp);
    return NULL;
  }

  bp = m_pullup(bp);
  len = m_length(bp);

  /*-
   * From rfc1989:
   *
   *  All octets which are included in the FCS calculation MUST be counted,
   *  including the packet header, the information field, and any padding.
   *  The FCS octets MUST also be counted, and one flag octet per frame
   *  MUST be counted.  All other octets (such as additional flag
   *  sequences, and escape bits or octets) MUST NOT be counted.
   *
   * As we're stacked higher than the HDLC layer (otherwise HDLC wouldn't be
   * able to calculate the FCS), we must not forget about these additional
   * bytes when we're asynchronous.
   *
   * We're also expecting to be stacked *before* the likes of the proto and
   * acf layers (to avoid alignment issues), so deal with this too.
   */

  p->hdlc.lqm.ifOutUniPackets++;
  p->hdlc.lqm.ifOutOctets += len + 1;		/* plus 1 flag octet! */
  for (layer = 0; layer < l->nlayers; layer++)
    switch (l->layer[layer]->type) {
      case LAYER_ACF:
        p->hdlc.lqm.ifOutOctets += acf_WrapperOctets(&l->lcp, *proto);
        break;
      case LAYER_ASYNC:
        /* Not included - see rfc1989 */
        break;
      case LAYER_HDLC:
        p->hdlc.lqm.ifOutOctets += hdlc_WrapperOctets();
        break;
      case LAYER_LQR:
        layer = l->nlayers;
        break;
      case LAYER_PROTO:
        p->hdlc.lqm.ifOutOctets += proto_WrapperOctets(&l->lcp, *proto);
        break;
      case LAYER_SYNC:
        /* Nothing to add on */
        break;
      default:
        log_Printf(LogWARN, "Oops, don't know how to do octets for %s layer\n",
                   l->layer[layer]->name);
        break;
    }

  if (*proto == PROTO_LQR) {
    /* Overwrite the entire packet (created in SendLqrData()) */
    struct lqrdata lqr;
    size_t pending_pkts, pending_octets;

    p->hdlc.lqm.lqr.OutLQRs++;

    /*
     * We need to compensate for the fact that we're pushing our data
     * onto the highest priority queue by factoring out packet & octet
     * values from other queues!
     */
    link_PendingLowPriorityData(l, &pending_pkts, &pending_octets);

    memset(&lqr, '\0', sizeof lqr);
    lqr.MagicNumber = p->link.lcp.want_magic;
    lqr.LastOutLQRs = p->hdlc.lqm.lqr.peer.PeerOutLQRs;
    lqr.LastOutPackets = p->hdlc.lqm.lqr.peer.PeerOutPackets;
    lqr.LastOutOctets = p->hdlc.lqm.lqr.peer.PeerOutOctets;
    lqr.PeerInLQRs = p->hdlc.lqm.lqr.Save.InLQRs;
    lqr.PeerInPackets = p->hdlc.lqm.lqr.Save.InPackets;
    lqr.PeerInDiscards = p->hdlc.lqm.lqr.Save.InDiscards;
    lqr.PeerInErrors = p->hdlc.lqm.lqr.Save.InErrors;
    lqr.PeerInOctets = p->hdlc.lqm.lqr.Save.InOctets;
    lqr.PeerOutLQRs = p->hdlc.lqm.lqr.OutLQRs;
    lqr.PeerOutPackets = p->hdlc.lqm.ifOutUniPackets - pending_pkts;
    /* Don't forget our ``flag'' octets.... */
    lqr.PeerOutOctets = p->hdlc.lqm.ifOutOctets - pending_octets - pending_pkts;
    lqr_Dump(l->name, "Output", &lqr);
    lqr_ChangeOrder(&lqr, (struct lqrdata *)MBUF_CTOP(bp));
  }

  return bp;
}

static struct mbuf *
lqr_LayerPull(struct bundle *b __unused, struct link *l __unused,
	      struct mbuf *bp, u_short *proto)
{
  /*
   * This is the ``Rx'' process from rfc1989, although a part of it is
   * actually performed by sync_LayerPull() & hdlc_LayerPull() so that
   * our octet counts are correct.
   */

  if (*proto == PROTO_LQR)
    m_settype(bp, MB_LQRIN);
  return bp;
}

/*
 * Statistics for pulled packets are recorded either in hdlc_PullPacket()
 * or sync_PullPacket()
 */

struct layer lqrlayer = { LAYER_LQR, "lqr", lqr_LayerPush, lqr_LayerPull };

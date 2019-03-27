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
 */

#include <sys/param.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if_dl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "layer.h"
#ifndef NONAT
#include "nat_cmd.h"
#endif
#include "vjcomp.h"
#include "ua.h"
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
#include "ncpaddr.h"
#include "ipcp.h"
#include "auth.h"
#include "lcp.h"
#include "async.h"
#include "ccp.h"
#include "link.h"
#include "descriptor.h"
#include "physical.h"
#include "chat.h"
#include "proto.h"
#include "filter.h"
#include "mp.h"
#include "chap.h"
#include "cbcp.h"
#include "datalink.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "prompt.h"
#include "id.h"
#include "arp.h"

void
peerid_Init(struct peerid *peer)
{
  peer->enddisc.class = 0;
  *peer->enddisc.address = '\0';
  peer->enddisc.len = 0;
  *peer->authname = '\0';
}

int
peerid_Equal(const struct peerid *p1, const struct peerid *p2)
{
  return !strcmp(p1->authname, p2->authname) &&
         p1->enddisc.class == p2->enddisc.class &&
         p1->enddisc.len == p2->enddisc.len &&
         !memcmp(p1->enddisc.address, p2->enddisc.address, p1->enddisc.len);
}

static u_int32_t
inc_seq(unsigned is12bit, u_int32_t seq)
{
  seq++;
  if (is12bit) {
    if (seq & 0xfffff000)
      seq = 0;
  } else if (seq & 0xff000000)
    seq = 0;
  return seq;
}

static int
isbefore(unsigned is12bit, u_int32_t seq1, u_int32_t seq2)
{
  u_int32_t max = (is12bit ? 0xfff : 0xffffff) - 0x200;

  if (seq1 > max) {
    if (seq2 < 0x200 || seq2 > seq1)
      return 1;
  } else if ((seq1 > 0x200 || seq2 <= max) && seq1 < seq2)
    return 1;

  return 0;
}

static int
mp_ReadHeader(struct mp *mp, struct mbuf *m, struct mp_header *header)
{
  if (mp->local_is12bit) {
    u_int16_t val;

    ua_ntohs(MBUF_CTOP(m), &val);
    if (val & 0x3000) {
      log_Printf(LogWARN, "Oops - MP header without required zero bits\n");
      return 0;
    }
    header->begin = val & 0x8000 ? 1 : 0;
    header->end = val & 0x4000 ? 1 : 0;
    header->seq = val & 0x0fff;
    return 2;
  } else {
    ua_ntohl(MBUF_CTOP(m), &header->seq);
    if (header->seq & 0x3f000000) {
      log_Printf(LogWARN, "Oops - MP header without required zero bits\n");
      return 0;
    }
    header->begin = header->seq & 0x80000000 ? 1 : 0;
    header->end = header->seq & 0x40000000 ? 1 : 0;
    header->seq &= 0x00ffffff;
    return 4;
  }
}

static void
mp_LayerStart(void *v __unused, struct fsm *fp __unused)
{
  /* The given FSM (ccp) is about to start up ! */
}

static void
mp_LayerUp(void *v __unused, struct fsm *fp)
{
  /* The given fsm (ccp) is now up */

  bundle_CalculateBandwidth(fp->bundle);	/* Against ccp_MTUOverhead */
}

static void
mp_LayerDown(void *v __unused, struct fsm *fp __unused)
{
  /* The given FSM (ccp) has been told to come down */
}

static void
mp_LayerFinish(void *v __unused, struct fsm *fp)
{
  /* The given fsm (ccp) is now down */
  if (fp->state == ST_CLOSED && fp->open_mode == OPEN_PASSIVE)
    fsm_Open(fp);		/* CCP goes to ST_STOPPED */
}

static void
mp_UpDown(void *v)
{
  struct mp *mp = (struct mp *)v;
  int percent;

  percent = MAX(mp->link.stats.total.in.OctetsPerSecond,
                mp->link.stats.total.out.OctetsPerSecond) * 800 /
            mp->bundle->bandwidth;
  if (percent >= mp->cfg.autoload.max) {
    log_Printf(LogDEBUG, "%d%% saturation - bring a link up ?\n", percent);
    bundle_AutoAdjust(mp->bundle, percent, AUTO_UP);
  } else if (percent <= mp->cfg.autoload.min) {
    log_Printf(LogDEBUG, "%d%% saturation - bring a link down ?\n", percent);
    bundle_AutoAdjust(mp->bundle, percent, AUTO_DOWN);
  }
}

void
mp_StopAutoloadTimer(struct mp *mp)
{
  throughput_stop(&mp->link.stats.total);
}

void
mp_CheckAutoloadTimer(struct mp *mp)
{
  if (mp->link.stats.total.SamplePeriod != mp->cfg.autoload.period) {
    throughput_destroy(&mp->link.stats.total);
    throughput_init(&mp->link.stats.total, mp->cfg.autoload.period);
    throughput_callback(&mp->link.stats.total, mp_UpDown, mp);
  }

  if (bundle_WantAutoloadTimer(mp->bundle))
    throughput_start(&mp->link.stats.total, "MP throughput", 1);
  else
    mp_StopAutoloadTimer(mp);
}

void
mp_RestartAutoloadTimer(struct mp *mp)
{
  if (mp->link.stats.total.SamplePeriod != mp->cfg.autoload.period)
    mp_CheckAutoloadTimer(mp);
  else
    throughput_clear(&mp->link.stats.total, THROUGHPUT_OVERALL, NULL);
}

void
mp_Init(struct mp *mp, struct bundle *bundle)
{
  mp->peer_is12bit = mp->local_is12bit = 0;
  mp->peer_mrru = mp->local_mrru = 0;

  peerid_Init(&mp->peer);

  mp->out.seq = 0;
  mp->out.link = 0;
  mp->out.af = AF_INET;
  mp->seq.min_in = 0;
  mp->seq.next_in = 0;
  mp->inbufs = NULL;
  mp->bundle = bundle;

  mp->link.type = LOGICAL_LINK;
  mp->link.name = "mp";
  mp->link.len = sizeof *mp;

  mp->cfg.autoload.period = SAMPLE_PERIOD;
  mp->cfg.autoload.min = mp->cfg.autoload.max = 0;
  throughput_init(&mp->link.stats.total, mp->cfg.autoload.period);
  throughput_callback(&mp->link.stats.total, mp_UpDown, mp);
  mp->link.stats.parent = NULL;
  mp->link.stats.gather = 0;	/* Let the physical links gather stats */
  memset(mp->link.Queue, '\0', sizeof mp->link.Queue);
  memset(mp->link.proto_in, '\0', sizeof mp->link.proto_in);
  memset(mp->link.proto_out, '\0', sizeof mp->link.proto_out);

  mp->fsmp.LayerStart = mp_LayerStart;
  mp->fsmp.LayerUp = mp_LayerUp;
  mp->fsmp.LayerDown = mp_LayerDown;
  mp->fsmp.LayerFinish = mp_LayerFinish;
  mp->fsmp.object = mp;

  mpserver_Init(&mp->server);

  mp->cfg.mrru = 0;
  mp->cfg.shortseq = NEG_ENABLED|NEG_ACCEPTED;
  mp->cfg.negenddisc = NEG_ENABLED|NEG_ACCEPTED;
  mp->cfg.enddisc.class = 0;
  *mp->cfg.enddisc.address = '\0';
  mp->cfg.enddisc.len = 0;

  lcp_Init(&mp->link.lcp, mp->bundle, &mp->link, NULL);
  ccp_Init(&mp->link.ccp, mp->bundle, &mp->link, &mp->fsmp);

  link_EmptyStack(&mp->link);
  link_Stack(&mp->link, &protolayer);
  link_Stack(&mp->link, &ccplayer);
  link_Stack(&mp->link, &vjlayer);
#ifndef NONAT
  link_Stack(&mp->link, &natlayer);
#endif
}

int
mp_Up(struct mp *mp, struct datalink *dl)
{
  struct lcp *lcp = &dl->physical->link.lcp;

  if (mp->active) {
    /* We're adding a link - do a last validation on our parameters */
    if (!peerid_Equal(&dl->peer, &mp->peer)) {
      log_Printf(LogPHASE, "%s: Inappropriate peer !\n", dl->name);
      log_Printf(LogPHASE, "  Attached to peer %s/%s\n", mp->peer.authname,
                 mp_Enddisc(mp->peer.enddisc.class, mp->peer.enddisc.address,
                            mp->peer.enddisc.len));
      log_Printf(LogPHASE, "  New link is peer %s/%s\n", dl->peer.authname,
                 mp_Enddisc(dl->peer.enddisc.class, dl->peer.enddisc.address,
                            dl->peer.enddisc.len));
      return MP_FAILED;
    }
    if (mp->local_mrru != lcp->want_mrru ||
        mp->peer_mrru != lcp->his_mrru ||
        mp->local_is12bit != lcp->want_shortseq ||
        mp->peer_is12bit != lcp->his_shortseq) {
      log_Printf(LogPHASE, "%s: Invalid MRRU/SHORTSEQ MP parameters !\n",
                dl->name);
      return MP_FAILED;
    }
    return MP_ADDED;
  } else {
    /* First link in multilink mode */

    mp->local_mrru = lcp->want_mrru;
    mp->peer_mrru = lcp->his_mrru;
    mp->local_is12bit = lcp->want_shortseq;
    mp->peer_is12bit = lcp->his_shortseq;
    mp->peer = dl->peer;

    throughput_destroy(&mp->link.stats.total);
    throughput_init(&mp->link.stats.total, mp->cfg.autoload.period);
    throughput_callback(&mp->link.stats.total, mp_UpDown, mp);
    memset(mp->link.Queue, '\0', sizeof mp->link.Queue);
    memset(mp->link.proto_in, '\0', sizeof mp->link.proto_in);
    memset(mp->link.proto_out, '\0', sizeof mp->link.proto_out);

    /* Tell the link who it belongs to */
    dl->physical->link.stats.parent = &mp->link.stats.total;

    mp->out.seq = 0;
    mp->out.link = 0;
    mp->out.af = AF_INET;
    mp->seq.min_in = 0;
    mp->seq.next_in = 0;

    /*
     * Now we create our server socket.
     * If it already exists, join it.  Otherwise, create and own it
     */
    switch (mpserver_Open(&mp->server, &mp->peer)) {
    case MPSERVER_CONNECTED:
      log_Printf(LogPHASE, "mp: Transfer link on %s\n",
                mp->server.socket.sun_path);
      mp->server.send.dl = dl;		/* Defer 'till it's safe to send */
      return MP_LINKSENT;
    case MPSERVER_FAILED:
      return MP_FAILED;
    case MPSERVER_LISTENING:
      log_Printf(LogPHASE, "mp: Listening on %s\n", mp->server.socket.sun_path);
      log_Printf(LogPHASE, "    First link: %s\n", dl->name);

      /* Re-point our NCP layers at our MP link */
      ncp_SetLink(&mp->bundle->ncp, &mp->link);

      /* Our lcp's already up 'cos of the NULL parent */
      if (ccp_SetOpenMode(&mp->link.ccp)) {
        fsm_Up(&mp->link.ccp.fsm);
        fsm_Open(&mp->link.ccp.fsm);
      }

      mp->active = 1;
      break;
    }
  }

  return MP_UP;
}

void
mp_Down(struct mp *mp)
{
  if (mp->active) {
    struct mbuf *next;

    /* Stop that ! */
    mp_StopAutoloadTimer(mp);

    /* Don't want any more of these */
    mpserver_Close(&mp->server);

    /* CCP goes down with a bang */
    fsm2initial(&mp->link.ccp.fsm);

    /* Received fragments go in the bit-bucket */
    while (mp->inbufs) {
      next = mp->inbufs->m_nextpkt;
      m_freem(mp->inbufs);
      mp->inbufs = next;
    }

    peerid_Init(&mp->peer);
    mp->active = 0;
  }
}

void
mp_linkInit(struct mp_link *mplink)
{
  mplink->seq = 0;
  mplink->bandwidth = 0;
}

static void
mp_Assemble(struct mp *mp, struct mbuf *m, struct physical *p)
{
  struct mp_header mh, h;
  struct mbuf *q, *last;
  u_int32_t seq;

  /*
   * When `m' and `p' are NULL, it means our oldest link has gone down.
   * We want to determine a new min, and process any intermediate stuff
   * as normal
   */

  if (m && mp_ReadHeader(mp, m, &mh) == 0) {
    m_freem(m);
    return;
  }

  if (p) {
    seq = p->dl->mp.seq;
    p->dl->mp.seq = mh.seq;
  } else
    seq = mp->seq.min_in;

  if (mp->seq.min_in == seq) {
    /*
     * We've received new data on the link that has our min (oldest) seq.
     * Figure out which link now has the smallest (oldest) seq.
     */
    struct datalink *dl;

    mp->seq.min_in = (u_int32_t)-1;
    for (dl = mp->bundle->links; dl; dl = dl->next)
      if (dl->state == DATALINK_OPEN &&
          (mp->seq.min_in == (u_int32_t)-1 ||
           isbefore(mp->local_is12bit, dl->mp.seq, mp->seq.min_in)))
        mp->seq.min_in = dl->mp.seq;
  }

  /*
   * Now process as many of our fragments as we can, adding our new
   * fragment in as we go, and ordering with the oldest at the top of
   * the queue.
   */

  last = NULL;
  seq = mp->seq.next_in;
  q = mp->inbufs;
  while (q || m) {
    if (!q) {
      if (last)
        last->m_nextpkt = m;
      else
        mp->inbufs = m;
      q = m;
      m = NULL;
      h = mh;
    } else {
      mp_ReadHeader(mp, q, &h);

      if (m && isbefore(mp->local_is12bit, mh.seq, h.seq)) {
        /* Our received fragment fits in before this one, so link it in */
        if (last)
          last->m_nextpkt = m;
        else
          mp->inbufs = m;
        m->m_nextpkt = q;
        q = m;
        h = mh;
        m = NULL;
      }
    }

    if (h.seq != seq) {
      /* we're missing something :-( */
      if (isbefore(mp->local_is12bit, seq, mp->seq.min_in)) {
        /* we're never gonna get it */
        struct mbuf *next;

        /* Zap all older fragments */
        while (mp->inbufs != q) {
          log_Printf(LogDEBUG, "Drop frag\n");
          next = mp->inbufs->m_nextpkt;
          m_freem(mp->inbufs);
          mp->inbufs = next;
        }

        /*
         * Zap everything until the next `end' fragment OR just before
         * the next `begin' fragment OR 'till seq.min_in - whichever
         * comes first.
         */
        do {
          mp_ReadHeader(mp, mp->inbufs, &h);
          if (h.begin) {
            /* We might be able to process this ! */
            h.seq--;  /* We're gonna look for fragment with h.seq+1 */
            break;
          }
          next = mp->inbufs->m_nextpkt;
          log_Printf(LogDEBUG, "Drop frag %u\n", h.seq);
          m_freem(mp->inbufs);
          mp->inbufs = next;
        } while (mp->inbufs && (isbefore(mp->local_is12bit, mp->seq.min_in,
                                         h.seq) || h.end));

        /*
         * Continue processing things from here.
         * This deals with the possibility that we received a fragment
         * on the slowest link that invalidates some of our data (because
         * of the hole at `q'), but where there are subsequent `whole'
         * packets that have already been received.
         */

        mp->seq.next_in = seq = inc_seq(mp->local_is12bit, h.seq);
        last = NULL;
        q = mp->inbufs;
      } else
        /* we may still receive the missing fragment */
        break;
    } else if (h.end) {
      /* We've got something, reassemble */
      struct mbuf **frag = &q;
      int len;
      long long first = -1;

      do {
        *frag = mp->inbufs;
        mp->inbufs = mp->inbufs->m_nextpkt;
        len = mp_ReadHeader(mp, *frag, &h);
        if (first == -1)
          first = h.seq;
        if (frag == &q && !h.begin) {
          log_Printf(LogWARN, "Oops - MP frag %lu should have a begin flag\n",
                    (u_long)h.seq);
          m_freem(q);
          q = NULL;
        } else if (frag != &q && h.begin) {
          log_Printf(LogWARN, "Oops - MP frag %lu should have an end flag\n",
                    (u_long)h.seq - 1);
          /*
           * Stuff our fragment back at the front of the queue and zap
           * our half-assembled packet.
           */
          (*frag)->m_nextpkt = mp->inbufs;
          mp->inbufs = *frag;
          *frag = NULL;
          m_freem(q);
          q = NULL;
          frag = &q;
          h.end = 0;	/* just in case it's a whole packet */
        } else {
          (*frag)->m_offset += len;
          (*frag)->m_len -= len;
          (*frag)->m_nextpkt = NULL;
          do
            frag = &(*frag)->m_next;
          while (*frag != NULL);
        }
      } while (!h.end);

      if (q) {
        q = m_pullup(q);
        log_Printf(LogDEBUG, "MP: Reassembled frags %lu-%lu, length %zd\n",
                   (u_long)first, (u_long)h.seq, m_length(q));
        link_PullPacket(&mp->link, MBUF_CTOP(q), q->m_len, mp->bundle);
        m_freem(q);
      }

      mp->seq.next_in = seq = inc_seq(mp->local_is12bit, h.seq);
      last = NULL;
      q = mp->inbufs;
    } else {
      /* Look for the next fragment */
      seq = inc_seq(mp->local_is12bit, seq);
      last = q;
      q = q->m_nextpkt;
    }
  }

  if (m) {
    /* We still have to find a home for our new fragment */
    last = NULL;
    for (q = mp->inbufs; q; last = q, q = q->m_nextpkt) {
      mp_ReadHeader(mp, q, &h);
      if (isbefore(mp->local_is12bit, mh.seq, h.seq))
        break;
    }
    /* Our received fragment fits in here */
    if (last)
      last->m_nextpkt = m;
    else
      mp->inbufs = m;
    m->m_nextpkt = q;
  }
}

struct mbuf *
mp_Input(struct bundle *bundle, struct link *l, struct mbuf *bp)
{
  struct physical *p = link2physical(l);

  if (!bundle->ncp.mp.active)
    /* Let someone else deal with it ! */
    return bp;

  if (p == NULL) {
    log_Printf(LogWARN, "DecodePacket: Can't do MP inside MP !\n");
    m_freem(bp);
  } else {
    m_settype(bp, MB_MPIN);
    mp_Assemble(&bundle->ncp.mp, bp, p);
  }

  return NULL;
}

static void
mp_Output(struct mp *mp, struct bundle *bundle, struct link *l,
          struct mbuf *m, u_int32_t begin, u_int32_t end)
{
  char prepend[4];

  /* Stuff an MP header on the front of our packet and send it */

  if (mp->peer_is12bit) {
    u_int16_t val;

    val = (begin << 15) | (end << 14) | (u_int16_t)mp->out.seq;
    ua_htons(&val, prepend);
    m = m_prepend(m, prepend, 2, 0);
  } else {
    u_int32_t val;

    val = (begin << 31) | (end << 30) | (u_int32_t)mp->out.seq;
    ua_htonl(&val, prepend);
    m = m_prepend(m, prepend, 4, 0);
  }
  if (log_IsKept(LogDEBUG))
    log_Printf(LogDEBUG, "MP[frag %d]: Send %zd bytes on link `%s'\n",
               mp->out.seq, m_length(m), l->name);
  mp->out.seq = inc_seq(mp->peer_is12bit, mp->out.seq);

  if (l->ccp.fsm.state != ST_OPENED && ccp_Required(&l->ccp)) {
    log_Printf(LogPHASE, "%s: Not transmitting... waiting for CCP\n", l->name);
    return;
  }

  link_PushPacket(l, m, bundle, LINK_QUEUES(l) - 1, PROTO_MP);
}

int
mp_FillPhysicalQueues(struct bundle *bundle)
{
  struct mp *mp = &bundle->ncp.mp;
  struct datalink *dl, *fdl;
  size_t total, add, len;
  int thislink, nlinks, nopenlinks, sendasip;
  u_int32_t begin, end;
  struct mbuf *m, *mo;
  struct link *bestlink;

  thislink = nlinks = nopenlinks = 0;
  for (fdl = NULL, dl = bundle->links; dl; dl = dl->next) {
    /* Include non-open links here as mp->out.link will stay more correct */
    if (!fdl) {
      if (thislink == mp->out.link)
        fdl = dl;
      else
        thislink++;
    }
    nlinks++;
    if (dl->state == DATALINK_OPEN)
      nopenlinks++;
  }

  if (!fdl) {
    fdl = bundle->links;
    if (!fdl)
      return 0;
    thislink = 0;
  }

  total = 0;
  for (dl = fdl; nlinks > 0; dl = dl->next, nlinks--, thislink++) {
    if (!dl) {
      dl = bundle->links;
      thislink = 0;
    }

    if (dl->state != DATALINK_OPEN)
      continue;

    if (dl->physical->out)
      /* this link has suffered a short write.  Let it continue */
      continue;

    add = link_QueueLen(&dl->physical->link);
    if (add) {
      /* this link has got stuff already queued.  Let it continue */
      total += add;
      continue;
    }

    if (!mp_QueueLen(mp)) {
      int mrutoosmall;

      /*
       * If there's only a single open link in our bundle and we haven't got
       * MP level link compression, queue outbound traffic directly via that
       * link's protocol stack rather than using the MP link.  This results
       * in the outbound traffic going out as PROTO_IP or PROTO_IPV6 rather
       * than PROTO_MP.
       */

      mrutoosmall = 0;
      sendasip = nopenlinks < 2;
      if (sendasip) {
        if (dl->physical->link.lcp.his_mru < mp->peer_mrru) {
          /*
           * Actually, forget it.  This test is done against the MRRU rather
           * than the packet size so that we don't end up sending some data
           * in MP fragments and some data in PROTO_IP packets.  That's just
           * too likely to upset some ppp implementations.
           */
          mrutoosmall = 1;
          sendasip = 0;
        }
      }

      bestlink = sendasip ? &dl->physical->link : &mp->link;
      if (!ncp_PushPacket(&bundle->ncp, &mp->out.af, bestlink))
        break;	/* Nothing else to send */

      if (mrutoosmall)
        log_Printf(LogDEBUG, "Don't send data as PROTO_IP, MRU < MRRU\n");
      else if (sendasip)
        log_Printf(LogDEBUG, "Sending data as PROTO_IP, not PROTO_MP\n");

      if (sendasip) {
        add = link_QueueLen(&dl->physical->link);
        if (add) {
          /* this link has got stuff already queued.  Let it continue */
          total += add;
          continue;
        }
      }
    }

    m = link_Dequeue(&mp->link);
    if (m) {
      len = m_length(m);
      begin = 1;
      end = 0;

      while (!end) {
        if (dl->state == DATALINK_OPEN) {
          /* Write at most his_mru bytes to the physical link */
          if (len <= dl->physical->link.lcp.his_mru) {
            mo = m;
            end = 1;
            m_settype(mo, MB_MPOUT);
          } else {
            /* It's > his_mru, chop the packet (`m') into bits */
            mo = m_get(dl->physical->link.lcp.his_mru, MB_MPOUT);
            len -= mo->m_len;
            m = mbuf_Read(m, MBUF_CTOP(mo), mo->m_len);
          }
          mp_Output(mp, bundle, &dl->physical->link, mo, begin, end);
          begin = 0;
        }

        if (!end) {
          nlinks--;
          dl = dl->next;
          if (!dl) {
            dl = bundle->links;
            thislink = 0;
          } else
            thislink++;
        }
      }
    }
  }
  mp->out.link = thislink;		/* Start here next time */

  return total;
}

int
mp_SetDatalinkBandwidth(struct cmdargs const *arg)
{
  int val;

  if (arg->argc != arg->argn+1)
    return -1;

  val = atoi(arg->argv[arg->argn]);
  if (val <= 0) {
    log_Printf(LogWARN, "The link bandwidth must be greater than zero\n");
    return 1;
  }
  arg->cx->mp.bandwidth = val;

  if (arg->cx->state == DATALINK_OPEN)
    bundle_CalculateBandwidth(arg->bundle);

  return 0;
}

int
mp_ShowStatus(struct cmdargs const *arg)
{
  struct mp *mp = &arg->bundle->ncp.mp;

  prompt_Printf(arg->prompt, "Multilink is %sactive\n", mp->active ? "" : "in");
  if (mp->active) {
    struct mbuf *m, *lm;
    int bufs = 0;

    lm = NULL;
    prompt_Printf(arg->prompt, "Socket:         %s\n",
                  mp->server.socket.sun_path);
    for (m = mp->inbufs; m; m = m->m_nextpkt) {
      bufs++;
      lm = m;
    }
    prompt_Printf(arg->prompt, "Pending frags:  %d", bufs);
    if (bufs) {
      struct mp_header mh;
      unsigned long first, last;

      first = mp_ReadHeader(mp, mp->inbufs, &mh) ? mh.seq : 0;
      last = mp_ReadHeader(mp, lm, &mh) ? mh.seq : 0;
      prompt_Printf(arg->prompt, " (Have %lu - %lu, want %lu, lowest %lu)\n",
                    first, last, (unsigned long)mp->seq.next_in,
                    (unsigned long)mp->seq.min_in);
      prompt_Printf(arg->prompt, "                First has %sbegin bit and "
                    "%send bit", mh.begin ? "" : "no ", mh.end ? "" : "no ");
    }
    prompt_Printf(arg->prompt, "\n");
  }

  prompt_Printf(arg->prompt, "\nMy Side:\n");
  if (mp->active) {
    prompt_Printf(arg->prompt, " Output SEQ:    %u\n", mp->out.seq);
    prompt_Printf(arg->prompt, " MRRU:          %u\n", mp->local_mrru);
    prompt_Printf(arg->prompt, " Short Seq:     %s\n",
                  mp->local_is12bit ? "on" : "off");
  }
  prompt_Printf(arg->prompt, " Discriminator: %s\n",
                mp_Enddisc(mp->cfg.enddisc.class, mp->cfg.enddisc.address,
                           mp->cfg.enddisc.len));

  prompt_Printf(arg->prompt, "\nHis Side:\n");
  if (mp->active) {
    prompt_Printf(arg->prompt, " Auth Name:     %s\n", mp->peer.authname);
    prompt_Printf(arg->prompt, " Input SEQ:     %u\n", mp->seq.next_in);
    prompt_Printf(arg->prompt, " MRRU:          %u\n", mp->peer_mrru);
    prompt_Printf(arg->prompt, " Short Seq:     %s\n",
                  mp->peer_is12bit ? "on" : "off");
  }
  prompt_Printf(arg->prompt,   " Discriminator: %s\n",
                mp_Enddisc(mp->peer.enddisc.class, mp->peer.enddisc.address,
                           mp->peer.enddisc.len));

  prompt_Printf(arg->prompt, "\nDefaults:\n");

  prompt_Printf(arg->prompt, " MRRU:          ");
  if (mp->cfg.mrru)
    prompt_Printf(arg->prompt, "%d (multilink enabled)\n", mp->cfg.mrru);
  else
    prompt_Printf(arg->prompt, "disabled\n");
  prompt_Printf(arg->prompt, " Short Seq:     %s\n",
                  command_ShowNegval(mp->cfg.shortseq));
  prompt_Printf(arg->prompt, " Discriminator: %s\n",
                  command_ShowNegval(mp->cfg.negenddisc));
  prompt_Printf(arg->prompt, " AutoLoad:      min %d%%, max %d%%,"
                " period %d secs\n", mp->cfg.autoload.min,
                mp->cfg.autoload.max, mp->cfg.autoload.period);

  return 0;
}

const char *
mp_Enddisc(u_char c, const char *address, size_t len)
{
  static char result[100];	/* Used immediately after it's returned */
  unsigned f, header;

  switch (c) {
    case ENDDISC_NULL:
      sprintf(result, "Null Class");
      break;

    case ENDDISC_LOCAL:
	    snprintf(result, sizeof result, "Local Addr: %.*s", (int)len,
		address);
      break;

    case ENDDISC_IP:
      if (len == 4)
        snprintf(result, sizeof result, "IP %s",
                 inet_ntoa(*(const struct in_addr *)address));
      else
        sprintf(result, "IP[%zd] ???", len);
      break;

    case ENDDISC_MAC:
      if (len == 6) {
        const u_char *m = (const u_char *)address;
        snprintf(result, sizeof result, "MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 m[0], m[1], m[2], m[3], m[4], m[5]);
      } else
        sprintf(result, "MAC[%zd] ???", len);
      break;

    case ENDDISC_MAGIC:
      sprintf(result, "Magic: 0x");
      header = strlen(result);
      if (len + header + 1 > sizeof result)
        len = sizeof result - header - 1;
      for (f = 0; f < len; f++)
        sprintf(result + header + 2 * f, "%02x", address[f]);
      break;

    case ENDDISC_PSN:
	    snprintf(result, sizeof result, "PSN: %.*s", (int)len, address);
      break;

    default:
      sprintf(result, "%d: ", (int)c);
      header = strlen(result);
      if (len + header + 1 > sizeof result)
        len = sizeof result - header - 1;
      for (f = 0; f < len; f++)
        sprintf(result + header + 2 * f, "%02x", address[f]);
      break;
  }
  return result;
}

int
mp_SetEnddisc(struct cmdargs const *arg)
{
  struct mp *mp = &arg->bundle->ncp.mp;
  struct in_addr addr;

  switch (bundle_Phase(arg->bundle)) {
    case PHASE_DEAD:
      break;
    case PHASE_ESTABLISH:
      /* Make sure none of our links are DATALINK_LCP or greater */
      if (bundle_HighestState(arg->bundle) >= DATALINK_LCP) {
        log_Printf(LogWARN, "enddisc: Only changeable before"
                   " LCP negotiations\n");
        return 1;
      }
      break;
    default:
      log_Printf(LogWARN, "enddisc: Only changeable at phase DEAD/ESTABLISH\n");
      return 1;
  }

  if (arg->argc == arg->argn) {
    mp->cfg.enddisc.class = 0;
    *mp->cfg.enddisc.address = '\0';
    mp->cfg.enddisc.len = 0;
  } else if (arg->argc > arg->argn) {
    if (!strcasecmp(arg->argv[arg->argn], "label")) {
      mp->cfg.enddisc.class = ENDDISC_LOCAL;
      strcpy(mp->cfg.enddisc.address, arg->bundle->cfg.label);
      mp->cfg.enddisc.len = strlen(mp->cfg.enddisc.address);
    } else if (!strcasecmp(arg->argv[arg->argn], "ip")) {
      if (arg->bundle->ncp.ipcp.my_ip.s_addr == INADDR_ANY)
        ncprange_getip4addr(&arg->bundle->ncp.ipcp.cfg.my_range, &addr);
      else
        addr = arg->bundle->ncp.ipcp.my_ip;
      memcpy(mp->cfg.enddisc.address, &addr.s_addr, sizeof addr.s_addr);
      mp->cfg.enddisc.class = ENDDISC_IP;
      mp->cfg.enddisc.len = sizeof arg->bundle->ncp.ipcp.my_ip.s_addr;
    } else if (!strcasecmp(arg->argv[arg->argn], "mac")) {
      struct sockaddr_dl hwaddr;

      if (arg->bundle->ncp.ipcp.my_ip.s_addr == INADDR_ANY)
        ncprange_getip4addr(&arg->bundle->ncp.ipcp.cfg.my_range, &addr);
      else
        addr = arg->bundle->ncp.ipcp.my_ip;

      if (arp_EtherAddr(addr, &hwaddr, 1)) {
        mp->cfg.enddisc.class = ENDDISC_MAC;
        memcpy(mp->cfg.enddisc.address, hwaddr.sdl_data + hwaddr.sdl_nlen,
               hwaddr.sdl_alen);
        mp->cfg.enddisc.len = hwaddr.sdl_alen;
      } else {
        log_Printf(LogWARN, "set enddisc: Can't locate MAC address for %s\n",
                  inet_ntoa(addr));
        return 4;
      }
    } else if (!strcasecmp(arg->argv[arg->argn], "magic")) {
      int f;

      randinit();
      for (f = 0; f < 20; f += sizeof(long))
        *(long *)(mp->cfg.enddisc.address + f) = random();
      mp->cfg.enddisc.class = ENDDISC_MAGIC;
      mp->cfg.enddisc.len = 20;
    } else if (!strcasecmp(arg->argv[arg->argn], "psn")) {
      if (arg->argc > arg->argn+1) {
        mp->cfg.enddisc.class = ENDDISC_PSN;
        strcpy(mp->cfg.enddisc.address, arg->argv[arg->argn+1]);
        mp->cfg.enddisc.len = strlen(mp->cfg.enddisc.address);
      } else {
        log_Printf(LogWARN, "PSN endpoint requires additional data\n");
        return 5;
      }
    } else {
      log_Printf(LogWARN, "%s: Unrecognised endpoint type\n",
                arg->argv[arg->argn]);
      return 6;
    }
  }

  return 0;
}

static int
mpserver_UpdateSet(struct fdescriptor *d, fd_set *r, fd_set *w, fd_set *e,
                   int *n)
{
  struct mpserver *s = descriptor2mpserver(d);
  int result;

  result = 0;
  if (s->send.dl != NULL) {
    /* We've connect()ed */
    if (!link_QueueLen(&s->send.dl->physical->link) &&
        !s->send.dl->physical->out) {
      /* Only send if we've transmitted all our data (i.e. the ConfigAck) */
      result -= datalink_RemoveFromSet(s->send.dl, r, w, e);
      bundle_SendDatalink(s->send.dl, s->fd, &s->socket);
      s->send.dl = NULL;
      s->fd = -1;
    } else
      /* Never read from a datalink that's on death row ! */
      result -= datalink_RemoveFromSet(s->send.dl, r, NULL, NULL);
  } else if (r && s->fd >= 0) {
    if (*n < s->fd + 1)
      *n = s->fd + 1;
    FD_SET(s->fd, r);
    log_Printf(LogTIMER, "mp: fdset(r) %d\n", s->fd);
    result++;
  }
  return result;
}

static int
mpserver_IsSet(struct fdescriptor *d, const fd_set *fdset)
{
  struct mpserver *s = descriptor2mpserver(d);
  return s->fd >= 0 && FD_ISSET(s->fd, fdset);
}

static void
mpserver_Read(struct fdescriptor *d, struct bundle *bundle,
	      const fd_set *fdset __unused)
{
  struct mpserver *s = descriptor2mpserver(d);

  bundle_ReceiveDatalink(bundle, s->fd);
}

static int
mpserver_Write(struct fdescriptor *d __unused, struct bundle *bundle __unused,
               const fd_set *fdset __unused)
{
  /* We never want to write here ! */
  log_Printf(LogALERT, "mpserver_Write: Internal error: Bad call !\n");
  return 0;
}

void
mpserver_Init(struct mpserver *s)
{
  s->desc.type = MPSERVER_DESCRIPTOR;
  s->desc.UpdateSet = mpserver_UpdateSet;
  s->desc.IsSet = mpserver_IsSet;
  s->desc.Read = mpserver_Read;
  s->desc.Write = mpserver_Write;
  s->send.dl = NULL;
  s->fd = -1;
  memset(&s->socket, '\0', sizeof s->socket);
}

int
mpserver_Open(struct mpserver *s, struct peerid *peer)
{
  int f, l;
  mode_t mask;

  if (s->fd != -1) {
    log_Printf(LogALERT, "Internal error !  mpserver already open\n");
    mpserver_Close(s);
  }

  l = snprintf(s->socket.sun_path, sizeof s->socket.sun_path, "%sppp-%s-%02x-",
               _PATH_VARRUN, peer->authname, peer->enddisc.class);
  if (l < 0) {
    log_Printf(LogERROR, "mpserver: snprintf(): %s\n", strerror(errno));
    return MPSERVER_FAILED;
  }

  for (f = 0;
       f < peer->enddisc.len && (size_t)l < sizeof s->socket.sun_path - 2;
       f++) {
    snprintf(s->socket.sun_path + l, sizeof s->socket.sun_path - l,
             "%02x", *(u_char *)(peer->enddisc.address+f));
    l += 2;
  }

  s->socket.sun_family = AF_LOCAL;
  s->socket.sun_len = sizeof s->socket;
  s->fd = ID0socket(PF_LOCAL, SOCK_DGRAM, 0);
  if (s->fd < 0) {
    log_Printf(LogERROR, "mpserver: socket(): %s\n", strerror(errno));
    return MPSERVER_FAILED;
  }

  setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, (struct sockaddr *)&s->socket,
             sizeof s->socket);
  mask = umask(0177);

  /*
   * Try to bind the socket.  If we succeed we play server, if we fail
   * we connect() and hand the link off.
   */

  if (ID0bind_un(s->fd, &s->socket) < 0) {
    if (errno != EADDRINUSE) {
      log_Printf(LogPHASE, "mpserver: can't create bundle socket %s (%s)\n",
                s->socket.sun_path, strerror(errno));
      umask(mask);
      close(s->fd);
      s->fd = -1;
      return MPSERVER_FAILED;
    }

    /* So we're the sender */
    umask(mask);
    if (ID0connect_un(s->fd, &s->socket) < 0) {
      log_Printf(LogPHASE, "mpserver: can't connect to bundle socket %s (%s)\n",
                s->socket.sun_path, strerror(errno));
      if (errno == ECONNREFUSED)
        log_Printf(LogPHASE, "          The previous server died badly !\n");
      close(s->fd);
      s->fd = -1;
      return MPSERVER_FAILED;
    }

    /* Donate our link to the other guy */
    return MPSERVER_CONNECTED;
  }

  return MPSERVER_LISTENING;
}

void
mpserver_Close(struct mpserver *s)
{
  if (s->send.dl != NULL) {
    bundle_SendDatalink(s->send.dl, s->fd, &s->socket);
    s->send.dl = NULL;
    s->fd = -1;
  } else if (s->fd >= 0) {
    close(s->fd);
    if (ID0unlink(s->socket.sun_path) == -1)
      log_Printf(LogERROR, "%s: Failed to remove: %s\n", s->socket.sun_path,
                strerror(errno));
    memset(&s->socket, '\0', sizeof s->socket);
    s->fd = -1;
  }
}

void
mp_LinkLost(struct mp *mp, struct datalink *dl)
{
  if (mp->seq.min_in == dl->mp.seq)
    /* We've lost the link that's holding everything up ! */
    mp_Assemble(mp, NULL, NULL);
}

size_t
mp_QueueLen(struct mp *mp)
{
  return link_QueueLen(&mp->link);
}

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
#include <sys/un.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

#include "defs.h"
#include "layer.h"
#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "fsm.h"
#include "lqr.h"
#include "hdlc.h"
#include "throughput.h"
#include "auth.h"
#include "lcp.h"
#include "async.h"
#include "ccp.h"
#include "link.h"
#include "descriptor.h"
#include "chap.h"
#include "physical.h"
#include "prompt.h"
#include "chat.h"
#include "mp.h"
#include "cbcp.h"
#include "datalink.h"

static u_int16_t const fcstab[256] = {
   /* 00 */ 0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
   /* 08 */ 0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
   /* 10 */ 0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
   /* 18 */ 0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
   /* 20 */ 0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
   /* 28 */ 0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
   /* 30 */ 0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
   /* 38 */ 0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
   /* 40 */ 0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
   /* 48 */ 0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
   /* 50 */ 0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
   /* 58 */ 0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
   /* 60 */ 0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
   /* 68 */ 0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
   /* 70 */ 0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
   /* 78 */ 0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
   /* 80 */ 0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
   /* 88 */ 0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
   /* 90 */ 0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
   /* 98 */ 0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
   /* a0 */ 0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
   /* a8 */ 0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
   /* b0 */ 0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
   /* b8 */ 0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
   /* c0 */ 0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
   /* c8 */ 0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
   /* d0 */ 0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
   /* d8 */ 0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
   /* e0 */ 0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
   /* e8 */ 0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
   /* f0 */ 0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
   /* f8 */ 0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

void
hdlc_Init(struct hdlc *hdlc, struct lcp *lcp)
{
  memset(hdlc, '\0', sizeof(struct hdlc));
  hdlc->lqm.owner = lcp;
}

/*
 *  HDLC FCS computation. Read RFC 1171 Appendix B and CCITT X.25 section
 *  2.27 for further details.
 */
u_short
hdlc_Fcs(u_char *cp, size_t len)
{
  u_short fcs = INITFCS;

  while (len--)
    fcs = (fcs >> 8) ^ fcstab[(fcs ^ *cp++) & 0xff];

  return fcs;
}

static inline u_short
HdlcFcsBuf(u_short fcs, struct mbuf *m)
{
  int len;
  u_char *pos, *end;

  len = m_length(m);
  pos = MBUF_CTOP(m);
  end = pos + m->m_len;
  while (len--) {
    fcs = (fcs >> 8) ^ fcstab[(fcs ^ *pos++) & 0xff];
    if (pos == end && len) {
      m = m->m_next;
      pos = MBUF_CTOP(m);
      end = pos + m->m_len;
    }
  }
  return (fcs);
}

static struct mbuf *
hdlc_LayerPush(struct bundle *bundle __unused, struct link *l __unused,
	       struct mbuf *bp, int pri __unused, u_short *proto __unused)
{
  struct mbuf *last;
  u_char *cp;
  u_short fcs;

  m_settype(bp, MB_HDLCOUT);
  fcs = HdlcFcsBuf(INITFCS, bp);
  fcs = ~fcs;

  for (last = bp; last->m_next; last = last->m_next)
    ;

  if (last->m_size - last->m_offset - last->m_len >= 2) {
    cp = MBUF_CTOP(last) + last->m_len;
    last->m_len += 2;
  } else {
    struct mbuf *tail = m_get(2, MB_HDLCOUT);
    last->m_next = tail;
    cp = MBUF_CTOP(tail);
  }

  *cp++ = fcs & 0377;		/* Low byte first (nothing like consistency) */
  *cp++ = fcs >> 8;

  log_DumpBp(LogHDLC, "hdlc_Output", bp);

  return bp;
}

/* Check out the latest ``Assigned numbers'' rfc (rfc1700.txt) */
static struct {
  u_short from;
  u_short to;
  const char *name;
} protocols[] = {
  { 0x0001, 0x0001, "Padding Protocol" },
  { 0x0003, 0x001f, "reserved (transparency inefficient)" },
  { 0x0021, 0x0021, "Internet Protocol" },
  { 0x0023, 0x0023, "OSI Network Layer" },
  { 0x0025, 0x0025, "Xerox NS IDP" },
  { 0x0027, 0x0027, "DECnet Phase IV" },
  { 0x0029, 0x0029, "Appletalk" },
  { 0x002b, 0x002b, "Novell IPX" },
  { 0x002d, 0x002d, "Van Jacobson Compressed TCP/IP" },
  { 0x002f, 0x002f, "Van Jacobson Uncompressed TCP/IP" },
  { 0x0031, 0x0031, "Bridging PDU" },
  { 0x0033, 0x0033, "Stream Protocol (ST-II)" },
  { 0x0035, 0x0035, "Banyan Vines" },
  { 0x0037, 0x0037, "reserved (until 1993)" },
  { 0x0039, 0x0039, "AppleTalk EDDP" },
  { 0x003b, 0x003b, "AppleTalk SmartBuffered" },
  { 0x003d, 0x003d, "Multi-Link" },
  { 0x003f, 0x003f, "NETBIOS Framing" },
  { 0x0041, 0x0041, "Cisco Systems" },
  { 0x0043, 0x0043, "Ascom Timeplex" },
  { 0x0045, 0x0045, "Fujitsu Link Backup and Load Balancing (LBLB)" },
  { 0x0047, 0x0047, "DCA Remote Lan" },
  { 0x0049, 0x0049, "Serial Data Transport Protocol (PPP-SDTP)" },
  { 0x004b, 0x004b, "SNA over 802.2" },
  { 0x004d, 0x004d, "SNA" },
  { 0x004f, 0x004f, "IP6 Header Compression" },
  { 0x0051, 0x0051, "KNX Bridging Data" },
  { 0x0053, 0x0053, "Encryption" },
  { 0x0055, 0x0055, "Individual Link Encryption" },
  { 0x0057, 0x0057, "Internet Protocol V6" },
  { 0x006f, 0x006f, "Stampede Bridging" },
  { 0x0071, 0x0071, "BAP Bandwidth Allocation Protocol" },
  { 0x0073, 0x0073, "MP+ Protocol" },
  { 0x007d, 0x007d, "reserved (Control Escape)" },
  { 0x007f, 0x007f, "reserved (compression inefficient)" },
  { 0x00cf, 0x00cf, "reserved (PPP NLPID)" },
  { 0x00fb, 0x00fb, "compression on single link in multilink group" },
  { 0x00fd, 0x00fd, "1st choice compression" },
  { 0x00ff, 0x00ff, "reserved (compression inefficient)" },
  { 0x0200, 0x02ff, "(compression inefficient)" },
  { 0x0201, 0x0201, "802.1d Hello Packets" },
  { 0x0203, 0x0203, "IBM Source Routing BPDU" },
  { 0x0205, 0x0205, "DEC LANBridge100 Spanning Tree" },
  { 0x0207, 0x0207, "Cisco Discovery Protocol" },
  { 0x0209, 0x0209, "Netcs Twin Routing" },
  { 0x0231, 0x0231, "Luxcom" },
  { 0x0233, 0x0233, "Sigma Network Systems" },
  { 0x0235, 0x0235, "Apple Client Server Protocol" },
  { 0x1e00, 0x1eff, "(compression inefficient)" },
  { 0x4001, 0x4001, "Cray Communications Control Protocol" },
  { 0x4003, 0x4003, "CDPD Mobile Network Registration Protocol" },
  { 0x4021, 0x4021, "Stacker LZS" },
  { 0x8001, 0x801f, "Not Used - reserved" },
  { 0x8021, 0x8021, "Internet Protocol Control Protocol" },
  { 0x8023, 0x8023, "OSI Network Layer Control Protocol" },
  { 0x8025, 0x8025, "Xerox NS IDP Control Protocol" },
  { 0x8027, 0x8027, "DECnet Phase IV Control Protocol" },
  { 0x8029, 0x8029, "Appletalk Control Protocol" },
  { 0x802b, 0x802b, "Novell IPX Control Protocol" },
  { 0x802d, 0x802d, "reserved" },
  { 0x802f, 0x802f, "reserved" },
  { 0x8031, 0x8031, "Bridging NCP" },
  { 0x8033, 0x8033, "Stream Protocol Control Protocol" },
  { 0x8035, 0x8035, "Banyan Vines Control Protocol" },
  { 0x8037, 0x8037, "reserved till 1993" },
  { 0x8039, 0x8039, "reserved" },
  { 0x803b, 0x803b, "reserved" },
  { 0x803d, 0x803d, "Multi-Link Control Protocol" },
  { 0x803f, 0x803f, "NETBIOS Framing Control Protocol" },
  { 0x8041, 0x8041, "Cisco Systems Control Protocol" },
  { 0x8043, 0x8043, "Ascom Timeplex" },
  { 0x8045, 0x8045, "Fujitsu LBLB Control Protocol" },
  { 0x8047, 0x8047, "DCA Remote Lan Network Control Protocol (RLNCP)" },
  { 0x8049, 0x8049, "Serial Data Control Protocol (PPP-SDCP)" },
  { 0x804b, 0x804b, "SNA over 802.2 Control Protocol" },
  { 0x804d, 0x804d, "SNA Control Protocol" },
  { 0x804f, 0x804f, "IP6 Header Compression Control Protocol" },
  { 0x8051, 0x8051, "KNX Bridging Control Protocol" },
  { 0x8053, 0x8053, "Encryption Control Protocol" },
  { 0x8055, 0x8055, "Individual Link Encryption Control Protocol" },
  { 0x8057, 0x8057, "Internet Protocol V6 Control Protocol" },
  { 0x806f, 0x806f, "Stampede Bridging Control Protocol" },
  { 0x8073, 0x8073, "MP+ Control Protocol" },
  { 0x8071, 0x8071, "BACP Bandwidth Allocation Control Protocol" },
  { 0x807d, 0x807d, "Not Used - reserved" },
  { 0x80cf, 0x80cf, "Not Used - reserved" },
  { 0x80fb, 0x80fb, "compression on single link in multilink group control" },
  { 0x80fd, 0x80fd, "Compression Control Protocol" },
  { 0x80ff, 0x80ff, "Not Used - reserved" },
  { 0x8207, 0x8207, "Cisco Discovery Protocol Control" },
  { 0x8209, 0x8209, "Netcs Twin Routing" },
  { 0x8235, 0x8235, "Apple Client Server Protocol Control" },
  { 0xc021, 0xc021, "Link Control Protocol" },
  { 0xc023, 0xc023, "Password Authentication Protocol" },
  { 0xc025, 0xc025, "Link Quality Report" },
  { 0xc027, 0xc027, "Shiva Password Authentication Protocol" },
  { 0xc029, 0xc029, "CallBack Control Protocol (CBCP)" },
  { 0xc081, 0xc081, "Container Control Protocol" },
  { 0xc223, 0xc223, "Challenge Handshake Authentication Protocol" },
  { 0xc225, 0xc225, "RSA Authentication Protocol" },
  { 0xc227, 0xc227, "Extensible Authentication Protocol" },
  { 0xc26f, 0xc26f, "Stampede Bridging Authorization Protocol" },
  { 0xc281, 0xc281, "Proprietary Authentication Protocol" },
  { 0xc283, 0xc283, "Proprietary Authentication Protocol" },
  { 0xc481, 0xc481, "Proprietary Node ID Authentication Protocol" }
};

#define NPROTOCOLS (sizeof protocols/sizeof protocols[0])

const char *
hdlc_Protocol2Nam(u_short proto)
{
  unsigned f;

  for (f = 0; f < NPROTOCOLS; f++)
    if (proto >= protocols[f].from && proto <= protocols[f].to)
      return protocols[f].name;
    else if (proto < protocols[f].from)
      break;
  return "unrecognised protocol";
}

static struct mbuf *
hdlc_LayerPull(struct bundle *b __unused, struct link *l, struct mbuf *bp,
               u_short *proto __unused)
{
  struct physical *p = link2physical(l);
  u_short fcs;
  int len;

  if (!p) {
    log_Printf(LogERROR, "Can't Pull a hdlc packet from a logical link\n");
    return bp;
  }

  log_DumpBp(LogHDLC, "hdlc_LayerPull:", bp);

  bp = m_pullup(bp);
  len = m_length(bp);
  fcs = hdlc_Fcs(MBUF_CTOP(bp), len);

  log_Printf(LogDEBUG, "%s: hdlc_LayerPull: fcs = %04x (%s)\n",
             p->link.name, fcs, (fcs == GOODFCS) ? "good" : "BAD!");

  p->hdlc.lqm.ifInOctets += len + 1;			/* plus 1 flag octet! */

  if (fcs != GOODFCS) {
    p->hdlc.lqm.ifInErrors++;
    p->hdlc.stats.badfcs++;
    m_freem(bp);
    return NULL;
  }

  /* Either done here or by the sync layer */
  p->hdlc.lqm.lqr.InGoodOctets += len + 1;		/* plus 1 flag octet! */
  p->hdlc.lqm.ifInUniPackets++;

  if (len < 4) {			/* rfc1662 section 4.3 */
    m_freem(bp);
    bp = NULL;
  }

  bp = m_adj(bp, -2);			/* discard the FCS */
  m_settype(bp, MB_HDLCIN);

  return bp;
}

/* Detect a HDLC frame */

static const struct frameheader {
  const u_char *data;
  int len;
} FrameHeaders[] = {
  { "\176\377\003\300\041", 5 },
  { "\176\377\175\043\300\041", 6 },
  { "\176\177\175\043\100\041", 6 },
  { "\176\175\337\175\043\300\041", 7 },
  { "\176\175\137\175\043\100\041", 7 },
  { NULL, 0 }
};

int
hdlc_Detect(u_char const **cp, unsigned n, int issync)
{
  const struct frameheader *fh;
  const u_char *h;
  size_t len, cmp;

  while (n) {
    for (fh = FrameHeaders; fh->len; fh++) {
      h = issync ? fh->data + 1 : fh->data;
      len = issync ? fh->len - 1 : fh->len;
      cmp = n >= len ? len : n;
      if (memcmp(*cp, h, cmp) == 0)
        return cmp == len;
    }
    n--;
    (*cp)++;
  }

  return 0;
}

int
hdlc_ReportStatus(struct cmdargs const *arg)
{
  struct hdlc *hdlc = &arg->cx->physical->hdlc;

  prompt_Printf(arg->prompt, "%s HDLC level errors:\n", arg->cx->name);
  prompt_Printf(arg->prompt, " Bad Frame Check Sequence fields: %u\n",
	        hdlc->stats.badfcs);
  prompt_Printf(arg->prompt, " Bad address (!= 0x%02x) fields:    %u\n",
	        HDLC_ADDR, hdlc->stats.badaddr);
  prompt_Printf(arg->prompt, " Bad command (!= 0x%02x) fields:    %u\n",
	        HDLC_UI, hdlc->stats.badcommand);
  prompt_Printf(arg->prompt, " Unrecognised protocol fields:    %u\n",
	        hdlc->stats.unknownproto);
  return 0;
}

static void
hdlc_ReportTime(void *v)
{
  /* Moan about HDLC errors */
  struct hdlc *hdlc = (struct hdlc *)v;

  timer_Stop(&hdlc->ReportTimer);

  if (memcmp(&hdlc->laststats, &hdlc->stats, sizeof hdlc->stats)) {
    log_Printf(LogPHASE,
              "%s: HDLC errors -> FCS: %u, ADDR: %u, COMD: %u, PROTO: %u\n",
              hdlc->lqm.owner->fsm.link->name,
	      hdlc->stats.badfcs - hdlc->laststats.badfcs,
              hdlc->stats.badaddr - hdlc->laststats.badaddr,
              hdlc->stats.badcommand - hdlc->laststats.badcommand,
              hdlc->stats.unknownproto - hdlc->laststats.unknownproto);
    hdlc->laststats = hdlc->stats;
  }

  timer_Start(&hdlc->ReportTimer);
}

void
hdlc_StartTimer(struct hdlc *hdlc)
{
  timer_Stop(&hdlc->ReportTimer);
  hdlc->ReportTimer.load = 60 * SECTICKS;
  hdlc->ReportTimer.arg = hdlc;
  hdlc->ReportTimer.func = hdlc_ReportTime;
  hdlc->ReportTimer.name = "hdlc";
  timer_Start(&hdlc->ReportTimer);
}

void
hdlc_StopTimer(struct hdlc *hdlc)
{
  timer_Stop(&hdlc->ReportTimer);
}

struct layer hdlclayer = { LAYER_HDLC, "hdlc", hdlc_LayerPush, hdlc_LayerPull };

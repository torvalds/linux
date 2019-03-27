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

/*
 *  Definition for Async HDLC
 */
#define HDLC_SYN 0x7e		/* SYNC character */
#define HDLC_ESC 0x7d		/* Escape character */
#define HDLC_XOR 0x20		/* Modifier value */

#define	HDLC_ADDR 0xff
#define	HDLC_UI	  0x03
/*
 *  Definition for HDLC Frame Check Sequence
 */
#define INITFCS 0xffff		/* Initial value for FCS computation */
#define GOODFCS 0xf0b8		/* Good FCS value */

#define	DEF_MRU		1500
#define	MAX_MRU		2048
#define	MIN_MRU		296

#define	DEF_MTU		0	/* whatever peer says */
#define	MAX_MTU		2048
#define	MIN_MTU		296

struct physical;
struct link;
struct lcp;
struct bundle;
struct mbuf;
struct cmdargs;

struct hdlc {
  struct pppTimer ReportTimer;

  struct {
    int badfcs;
    int badaddr;
    int badcommand;
    int unknownproto;
  } laststats, stats;

  struct {
    struct lcp *owner;			/* parent LCP */
    struct pppTimer timer;		/* When to send */
    int method;				/* bit-mask for LQM_* from lqr.h */

    u_int32_t ifOutUniPackets;		/* Packets sent by me */
    u_int32_t ifOutOctets;		/* Octets sent by me */
    u_int32_t ifInUniPackets;		/* Packets received from peer */
    u_int32_t ifInDiscards;		/* Discards */
    u_int32_t ifInErrors;		/* Errors */
    u_int32_t ifInOctets;		/* Octets received from peer (unused) */

    struct {
      u_int32_t InGoodOctets;		/* Good octets received from peer */
      u_int32_t OutLQRs;		/* LQRs sent by me */
      u_int32_t InLQRs;			/* LQRs received from peer */

      struct lqrsavedata Save;		/* Our last LQR */
      struct lqrsavedata prevSave;	/* Our last-but-one LQR (analysis) */

      struct lqrdata peer;		/* Last LQR from peer */
      int peer_timeout;			/* peers max lqr timeout */
      int resent;			/* Resent last packet `resent' times */
    } lqr;

    struct {
      u_int32_t seq_sent;		/* last echo sent */
      u_int32_t seq_recv;		/* last echo received */
    } echo;
  } lqm;
};


extern void hdlc_Init(struct hdlc *, struct lcp *);
extern void hdlc_StartTimer(struct hdlc *);
extern void hdlc_StopTimer(struct hdlc *);
extern int hdlc_ReportStatus(struct cmdargs const *);
extern const char *hdlc_Protocol2Nam(u_short);
extern void hdlc_DecodePacket(struct bundle *, u_short, struct mbuf *,
                              struct link *);

extern u_short hdlc_Fcs(u_char *, size_t);
extern int hdlc_Detect(u_char const **, unsigned, int);
#define hdlc_WrapperOctets() (2)

extern struct layer hdlclayer;

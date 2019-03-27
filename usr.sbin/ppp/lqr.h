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
 *  Structure of LQR packet defined in RFC1989
 */
struct lqrdata {
  u_int32_t MagicNumber;
  u_int32_t LastOutLQRs;	/* most recently received PeerOutLQRs */
  u_int32_t LastOutPackets;	/* most recently received PeerOutPackets */
  u_int32_t LastOutOctets;	/* most recently received PeerOutOctets */
  u_int32_t PeerInLQRs;		/* Peers SaveInLQRs */
  u_int32_t PeerInPackets;	/* Peers SaveInPackets */
  u_int32_t PeerInDiscards;	/* Peers SaveInDiscards */
  u_int32_t PeerInErrors;	/* Peers SaveInErrors */
  u_int32_t PeerInOctets;	/* Peers SaveInOctets */
  u_int32_t PeerOutLQRs;	/* Peers OutLQRs (hdlc.h) */
  u_int32_t PeerOutPackets;	/* Peers OutPackets (hdlc.h) */
  u_int32_t PeerOutOctets;	/* Peers OutOctets (hdlc.h) */
};

struct lqrsavedata {	/* Saved on receipt of an LQR */
  u_int32_t InLQRs;	/* From ifInLQRs */
  u_int32_t InPackets;	/* From ifInPackets */
  u_int32_t InDiscards;	/* From ifInDiscards */
  u_int32_t InErrors;	/* From ifInErrors */
  u_int32_t InOctets;	/* From InGoodOctets ! */
};

/*
 *  We support LQR and ECHO as LQM method
 */
#define	LQM_LQR	  1
#define	LQM_ECHO  2

struct mbuf;
struct physical;
struct lcp;
struct fsm;
struct hdlc;
struct link;
struct bundle;

extern void lqr_Dump(const char *, const char *, const struct lqrdata *);
extern void lqr_Analyse(const struct hdlc *, const struct lqrdata *,
                        const struct lqrdata *);
extern void lqr_ChangeOrder(struct lqrdata *, struct lqrdata *);
extern void lqr_Start(struct lcp *);
extern void lqr_reStart(struct lcp *);
extern void lqr_Stop(struct physical *, int);
extern void lqr_StopTimer(struct physical *);
extern struct mbuf *lqr_RecvEcho(struct fsm *, struct mbuf *);
extern struct mbuf *lqr_Input(struct bundle *, struct link *, struct mbuf *);

extern struct layer lqrlayer;

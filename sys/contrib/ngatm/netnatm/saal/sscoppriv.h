/*
 * Copyright (c) 1996-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/netnatm/saal/sscoppriv.h,v 1.4 2004/07/08 08:22:17 brandt Exp $
 *
 * Private SSCOP definitions.
 *
 */
#ifdef _KERNEL
#ifdef __FreeBSD__
#include <netgraph/atm/sscop/ng_sscop_cust.h>
#endif
#else	/* !_KERNEL */
#include "sscopcust.h"
#endif

/* Argh. BSDi */
#ifndef _BYTE_ORDER
#ifndef BYTE_ORDER
#error "_BYTE_ORDER not defined"
#endif
#define _BYTE_ORDER	BYTE_ORDER
#define _LITTLE_ENDIAN	LITTLE_ENDIAN
#define	_BIG_ENDIAN	BIG_ENDIAN
#endif

/*
 * PDU trailer
 */
union pdu {
  u_int			sscop_null;
  struct {
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int		pl : 2;		/* pad length */
	u_int		: 1;		/* reserved field */
	u_int		s : 1;		/* source */
	u_int		type : 4;	/* PDU type */
	u_int		ns : 24;	/* sequence number */
#else
	u_int		ns : 24;	/* sequence number */
	u_int		type : 4;	/* PDU type */
	u_int		s : 1;		/* source */
	u_int		: 1;		/* reserved field */
	u_int		pl : 2;		/* pad length */
#endif
  } ss;
};
#define sscop_pl	ss.pl
#define sscop_s		ss.s
#define sscop_type	ss.type
#define sscop_ns	ss.ns

/*
 * seqno list entry format
 */
union seqno {
  u_int			sscop_null;
  struct {
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int		: 8;		/* pad */
	u_int		n : 24;		/* seqno */
#else
	u_int		n : 24;		/* seqno */
	u_int		: 8;		/* pad */
#endif
  } ss;
};
#define sscop_n	ss.n

/*
 * Begin pdu
 */
union bgn {
  u_int			sscop_null;
  struct {
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int	: 24;			/* reserved */
	u_int	bgns : 8;		/* VT_MR */
#else
	u_int	bgns : 8;		/* VT_MR */
	u_int	: 24;			/* reserved */
#endif
  } ss;
};
#define sscop_bgns	ss.bgns

/*
 * pdu types
 */
enum pdu_type {
	PDU_BGN		= 0x1,	/* request initialization */
	PDU_BGAK	= 0x2,	/* request acknowledgement */
	PDU_END		= 0x3,	/* disconnect command */
	PDU_ENDAK	= 0x4,	/* disconnect acknowledgement */
	PDU_RS		= 0x5,	/* resynchronisation command */
	PDU_RSAK	= 0x6,	/* resynchronisation acknowledgement */
	PDU_BGREJ	= 0x7,	/* connection reject */
	PDU_SD		= 0x8,	/* sequenced connection-mode data */
	PDU_ER		= 0x9,	/* recovery command */
	PDU_POLL	= 0xa,	/* xmit state info with req. for recv state */
	PDU_STAT	= 0xb,	/* solicited receiver state info */
	PDU_USTAT	= 0xc,	/* unsolicited receiver state info */
	PDU_UD		= 0xd,	/* unumbered user data */
	PDU_MD		= 0xe,	/* unumbered management data */
	PDU_ERAK	= 0xf,	/* recovery acknowledgement */
};


/*
 * These are all signals, that are used by SSCOP. Don't change the order or
 * number without also changing the associated tables.
 */
enum sscop_sigtype {
	/* received PDU's */
	SIG_BGN,		/* request initialization */
	SIG_BGAK,		/* request acknowledgement */
	SIG_END,		/* disconnect command */
	SIG_ENDAK,		/* disconnect acknowledgement */
	SIG_RS,			/* resynchronisation command */
	SIG_RSAK,		/* resynchronisation acknowledgement */
	SIG_BGREJ,		/* connection reject */
	SIG_SD,			/* sequenced connection-mode data */
	SIG_ER,			/* recovery command */
	SIG_POLL,		/* xmitter state info with req for recv state */
	SIG_STAT,		/* solicited receiver state info */
	SIG_USTAT,		/* unsolicited receiver state info */
	SIG_UD,			/* unumbered user data */
	SIG_MD,			/* unumbered management data */
	SIG_ERAK,		/* recovery acknoledgement */

	/* timer expiry */
	SIG_T_CC,		/* CC timer */
	SIG_T_POLL,		/* POLL timer */
	SIG_T_KA,		/* KEEP ALIVE timer */
	SIG_T_NR,		/* NO RESPONSE timer */
	SIG_T_IDLE,		/* IDLE timer */

	/* user originated signals */
	SIG_PDU_Q,		/* PDU enqueued pseudosignal */
	SIG_USER_DATA,		/* user data request */
	SIG_ESTAB_REQ,		/* establish connection request */
	SIG_ESTAB_RESP,		/* establish connection response */
	SIG_RELEASE_REQ,	/* release connection request */
	SIG_RECOVER,		/* automatic recover response */
	SIG_SYNC_REQ,		/* resynchronisation request */
	SIG_SYNC_RESP,		/* resynchronisation response */
	SIG_UDATA,		/* UDATA request */
	SIG_MDATA,		/* MDATA request */
	SIG_UPDU_Q,		/* UDATA PDU enqueued pseudosignal */
	SIG_MPDU_Q,		/* MDATA PDU enqueued pseudosignal */
	SIG_RETRIEVE,		/* RETRIEVE */

	/* number of signals */
	SIG_NUM
};

/*
 * This is a message as contained in a sscop message queue. It holds a pointer
 * to the real message.
 */
struct sscop_msg {
	sscop_msgq_link_t link;
	u_int		seqno;		/* seq no */
	u_int		poll_seqno;	/* poll seqno (for messages in xmit buffer) */
	u_int		rexmit;		/* in retransmission queue? */
	struct SSCOP_MBUF_T *m;		/* the message */
};

/*
 * This structure is used to hold signals in the signal queue
 */
struct sscop_sig {
	sscop_sigq_link_t link;		/* next signal */
	enum sscop_sigtype sig;		/* THE signal */
	struct sscop_msg *msg;		/* signal argument (message) */
};

/*
 * This structure holds the entire sscop state
 */
struct sscop {
	enum sscop_state state;	/* current state */
	const struct sscop_funcs *funcs;

	/* send state */
	u_int	vt_s;		/* seqno for next pdu first time transmitted */
	u_int	vt_ps;		/* current poll seqno */
	u_int	vt_a;		/* next expected in-sequence sd pdu */
	u_int	vt_pa;		/* poll seqno of next stat pdu */
	u_int	vt_ms;		/* maximum allowed send sd seqno */
	u_int	vt_pd;		/* poll data state */
	u_int	vt_cc;		/* connection control state */
	u_int	vt_sq;		/* transmitter connection sequence */

	/* receive state */
	u_int	vr_r;		/* receive state */
	u_int	vr_h;		/* highes expected state */
	u_int	vr_mr;		/* maximum acceptable */
	u_int	vr_sq;		/* receiver connection state */

	/* timers */
	sscop_timer_t t_cc;	/* timer_CC */
	sscop_timer_t t_nr;	/* timer_NO_RESPONSE */
	sscop_timer_t t_ka;	/* timer KEEP_ALIVE */
	sscop_timer_t t_poll;	/* timer_POLL */
	sscop_timer_t t_idle;	/* idle timer */

	/* maximum values */
	u_int	maxj;		/* maximum uu-info */
	u_int	maxk;		/* maximum info */
	u_int	maxcc;		/* maximum number of bgn, end, er and rs */
	u_int	maxpd;		/* maximum value of vt_pd */
	u_int	maxstat;	/* maximum length of list */
	u_int	timercc;	/* connection control timer */
	u_int	timerka;	/* keep alive timer */
	u_int	timernr;	/* no response timer */
	u_int	timerpoll;	/* polling */
	u_int	timeridle;	/* idle timer */
	u_int	robustness;	/* atmf/97-0216 robustness enhancement */
	u_int	poll_after_rex;	/* optional POLL after re-transmission */
	u_int	mr;		/* initial window */

	/*
	 * buffers and queues.
	 * All expect the xq hold SD PDUs.
	 */
	sscop_msgq_head_t xq;	/* xmit queue (input from user before xmit) */
	sscop_msgq_head_t uxq;	/* UD xmit queue */
	sscop_msgq_head_t mxq;	/* MD xmit queue */
	sscop_msgq_head_t xbuf;	/* transmission buffer (SD PDUs transmitted) */
	int	rxq;		/* number of PDUs in retransmission queue */
	sscop_msgq_head_t rbuf;	/* receive buffer (SD PDUs) */
	int	last_end_src;	/* source field from last xmitted end pdu */
	int	clear_buffers;	/* flag */
	int	credit;		/* send window not closed */
	u_int	ll_busy;	/* lower layer busy */
	u_int	rs_mr;		/* N(MR) in last RS PDU */
	u_int	rs_sq;		/* N(SQ) in last RS PDU */
	struct SSCOP_MBUF_T *uu_bgn;	/* last UU data */
	struct SSCOP_MBUF_T *uu_bgak;	/*  ... */
	struct SSCOP_MBUF_T *uu_bgrej;	/*  ... */
	struct SSCOP_MBUF_T *uu_end;	/*  ... */
	struct SSCOP_MBUF_T *uu_rs;	/*  ... */

	/* signal queues */
	sscop_sigq_head_t	sigs;		/* saved signals */
	sscop_sigq_head_t	saved_sigs;	/* saved signals */
	int	in_sig;		/* in signal handler */

	/* debugging */
	u_int		debug;

	/* AA interface */
	void		*aarg;
};


/*
 * Default values for SSCOP
 */
enum {
	MAXK		= 4096,
	MAXMAXK		= 65528,
	MAXJ		= 4096,
	MAXMAXJ		= 65524,
	MAXCC		= 4,
	MAXSTAT		= 67,
	MAXPD		= 25,
	MAXMR		= 128,		/* ??? */
	TIMERCC		= 1000,
	TIMERKA		= 2000,
	TIMERNR		= 7000,
	TIMERPOLL	= 750,
	TIMERIDLE	= 15000,
};

/*
 * Sequence number arithmetic
 */
#define SEQNO_DIFF(A,B)  (((A) < (B)) ? ((A) + (1<<24) - (B)) : ((A) - (B)))

/*
 * Debugging
 */
#ifdef SSCOP_DEBUG
#define VERBOSE(S,M,F)	if ((S)->debug & (M)) (S)->funcs->verbose F
#define VERBERR(S,M,F)	if ((S)->debug & (M)) (S)->funcs->verbose F
#define ISVERBOSE(S,M)	((S)->debug & (M))
#else
#define VERBOSE(S,M,F)
#define VERBERR(S,M,F)
#define ISVERBOSE(S,M)	(0)
#endif

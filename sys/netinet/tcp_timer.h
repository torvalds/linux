/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)tcp_timer.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef _NETINET_TCP_TIMER_H_
#define _NETINET_TCP_TIMER_H_

/*
 * The TCPT_REXMT timer is used to force retransmissions.
 * The TCP has the TCPT_REXMT timer set whenever segments
 * have been sent for which ACKs are expected but not yet
 * received.  If an ACK is received which advances tp->snd_una,
 * then the retransmit timer is cleared (if there are no more
 * outstanding segments) or reset to the base value (if there
 * are more ACKs expected).  Whenever the retransmit timer goes off,
 * we retransmit one unacknowledged segment, and do a backoff
 * on the retransmit timer.
 *
 * The TCPT_PERSIST timer is used to keep window size information
 * flowing even if the window goes shut.  If all previous transmissions
 * have been acknowledged (so that there are no retransmissions in progress),
 * and the window is too small to bother sending anything, then we start
 * the TCPT_PERSIST timer.  When it expires, if the window is nonzero,
 * we go to transmit state.  Otherwise, at intervals send a single byte
 * into the peer's window to force him to update our window information.
 * We do this at most as often as TCPT_PERSMIN time intervals,
 * but no more frequently than the current estimate of round-trip
 * packet time.  The TCPT_PERSIST timer is cleared whenever we receive
 * a window update from the peer.
 *
 * The TCPT_KEEP timer is used to keep connections alive.  If an
 * connection is idle (no segments received) for TCPTV_KEEP_INIT amount of time,
 * but not yet established, then we drop the connection.  Once the connection
 * is established, if the connection is idle for TCPTV_KEEP_IDLE time
 * (and keepalives have been enabled on the socket), we begin to probe
 * the connection.  We force the peer to send us a segment by sending:
 *	<SEQ=SND.UNA-1><ACK=RCV.NXT><CTL=ACK>
 * This segment is (deliberately) outside the window, and should elicit
 * an ack segment in response from the peer.  If, despite the TCPT_KEEP
 * initiated segments we cannot elicit a response from a peer in TCPT_MAXIDLE
 * amount of time probing, then we drop the connection.
 */

/*
 * Time constants.
 */
#define	TCPTV_MSL	( 30*hz)		/* max seg lifetime (hah!) */
#define	TCPTV_SRTTBASE	0			/* base roundtrip time;
						   if 0, no idea yet */
#define	TCPTV_RTOBASE	(  1*hz)		/* assumed RTO if no info */

#define	TCPTV_PERSMIN	(  5*hz)		/* minimum persist interval */
#define	TCPTV_PERSMAX	( 60*hz)		/* maximum persist interval */

#define	TCPTV_KEEP_INIT	( 75*hz)		/* initial connect keepalive */
#define	TCPTV_KEEP_IDLE	(120*60*hz)		/* dflt time before probing */
#define	TCPTV_KEEPINTVL	( 75*hz)		/* default probe interval */
#define	TCPTV_KEEPCNT	8			/* max probes before drop */

#define TCPTV_FINWAIT2_TIMEOUT (60*hz)         /* FIN_WAIT_2 timeout if no receiver */

/*
 * Minimum retransmit timer is 3 ticks, for algorithmic stability.
 * TCPT_RANGESET() will add another TCPTV_CPU_VAR to deal with
 * the expected worst-case processing variances by the kernels
 * representing the end points.  Such variances do not always show
 * up in the srtt because the timestamp is often calculated at
 * the interface rather then at the TCP layer.  This value is
 * typically 50ms.  However, it is also possible that delayed
 * acks (typically 100ms) could create issues so we set the slop
 * to 200ms to try to cover it.  Note that, properly speaking,
 * delayed-acks should not create a major issue for interactive
 * environments which 'P'ush the last segment, at least as
 * long as implementations do the required 'at least one ack
 * for every two packets' for the non-interactive streaming case.
 * (maybe the RTO calculation should use 2*RTT instead of RTT
 * to handle the ack-every-other-packet case).
 *
 * The prior minimum of 1*hz (1 second) badly breaks throughput on any
 * networks faster then a modem that has minor (e.g. 1%) packet loss.
 */
#define	TCPTV_MIN	( hz/33 )		/* minimum allowable value */
#define TCPTV_CPU_VAR	( hz/5 )		/* cpu variance allowed (200ms) */
#define	TCPTV_REXMTMAX	( 64*hz)		/* max allowable REXMT value */

#define TCPTV_TWTRUNC	8			/* RTO factor to truncate TW */

#define	TCP_LINGERTIME	120			/* linger at most 2 minutes */

#define	TCP_MAXRXTSHIFT	12			/* maximum retransmits */

#define	TCPTV_DELACK	( hz/10 )		/* 100ms timeout */

/*
 * If we exceed this number of retransmits for a single segment, we'll consider
 * the current srtt measurement no longer valid and will recalculate from
 * scratch starting with the next ACK.
 */
#define TCP_RTT_INVALIDATE (TCP_MAXRXTSHIFT / 4)

#ifdef	TCPTIMERS
static const char *tcptimers[] =
    { "REXMT", "PERSIST", "KEEP", "2MSL", "DELACK" };
#endif

/*
 * Force a time value to be in a certain range.
 */
#define	TCPT_RANGESET(tv, value, tvmin, tvmax) do { \
	(tv) = (value) + tcp_rexmit_slop; \
	if ((u_long)(tv) < (u_long)(tvmin)) \
		(tv) = (tvmin); \
	if ((u_long)(tv) > (u_long)(tvmax)) \
		(tv) = (tvmax); \
} while(0)

#ifdef _KERNEL

struct xtcp_timer;

struct tcp_timer {
	struct	callout tt_rexmt;	/* retransmit timer */
	struct	callout tt_persist;	/* retransmit persistence */
	struct	callout tt_keep;	/* keepalive */
	struct	callout tt_2msl;	/* 2*msl TIME_WAIT timer */
	struct	callout tt_delack;	/* delayed ACK timer */
	uint32_t	tt_flags;	/* Timers flags */
	uint32_t	tt_draincnt;	/* Count being drained */
};

/*
 * Flags for the tt_flags field.
 */
#define TT_DELACK	0x0001
#define TT_REXMT	0x0002
#define TT_PERSIST	0x0004
#define TT_KEEP		0x0008
#define TT_2MSL		0x0010
#define TT_MASK		(TT_DELACK|TT_REXMT|TT_PERSIST|TT_KEEP|TT_2MSL)

/* 
 * Suspend flags - used when suspending a timer
 * from ever running again.
 */
#define TT_DELACK_SUS	0x0100
#define TT_REXMT_SUS	0x0200
#define TT_PERSIST_SUS	0x0400
#define TT_KEEP_SUS	0x0800
#define TT_2MSL_SUS	0x1000

#define TT_STOPPED	0x00010000

#define	TP_KEEPINIT(tp)	((tp)->t_keepinit ? (tp)->t_keepinit : tcp_keepinit)
#define	TP_KEEPIDLE(tp)	((tp)->t_keepidle ? (tp)->t_keepidle : tcp_keepidle)
#define	TP_KEEPINTVL(tp) ((tp)->t_keepintvl ? (tp)->t_keepintvl : tcp_keepintvl)
#define	TP_KEEPCNT(tp)	((tp)->t_keepcnt ? (tp)->t_keepcnt : tcp_keepcnt)
#define	TP_MAXIDLE(tp)	(TP_KEEPCNT(tp) * TP_KEEPINTVL(tp))

extern int tcp_persmin;			/* minimum persist interval */
extern int tcp_persmax;			/* maximum persist interval */
extern int tcp_keepinit;		/* time to establish connection */
extern int tcp_keepidle;		/* time before keepalive probes begin */
extern int tcp_keepintvl;		/* time between keepalive probes */
extern int tcp_keepcnt;			/* number of keepalives */
extern int tcp_delacktime;		/* time before sending a delayed ACK */
extern int tcp_maxpersistidle;
extern int tcp_rexmit_initial;
extern int tcp_rexmit_min;
extern int tcp_rexmit_slop;
extern int tcp_msl;
extern int tcp_ttl;			/* time to live for TCP segs */
extern int tcp_backoff[];
extern int tcp_totbackoff;
extern int tcp_rexmit_drop_options;

extern int tcp_always_keepalive;
extern int tcp_finwait2_timeout;
extern int tcp_fast_finwait2_recycle;

VNET_DECLARE(int, tcp_pmtud_blackhole_detect);
#define V_tcp_pmtud_blackhole_detect	VNET(tcp_pmtud_blackhole_detect)
VNET_DECLARE(int, tcp_pmtud_blackhole_mss);
#define	V_tcp_pmtud_blackhole_mss	VNET(tcp_pmtud_blackhole_mss)
VNET_DECLARE(int, tcp_v6pmtud_blackhole_mss);
#define V_tcp_v6pmtud_blackhole_mss	VNET(tcp_v6pmtud_blackhole_mss)

void tcp_inpinfo_lock_del(struct inpcb *inp, struct tcpcb *tp);

void	tcp_timer_init(void);
void	tcp_timer_2msl(void *xtp);
void	tcp_timer_discard(void *);
struct tcptw *
	tcp_tw_2msl_scan(int reuse);	/* XXX temporary? */
void	tcp_timer_keep(void *xtp);
void	tcp_timer_persist(void *xtp);
void	tcp_timer_rexmt(void *xtp);
void	tcp_timer_delack(void *xtp);

#endif /* _KERNEL */

#endif /* !_NETINET_TCP_TIMER_H_ */

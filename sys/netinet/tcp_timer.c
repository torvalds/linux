/*	$OpenBSD: tcp_timer.c,v 1.88 2025/09/17 17:29:14 bluhm Exp $	*/
/*	$NetBSD: tcp_timer.c,v 1.14 1996/02/13 23:44:09 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
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
 *	@(#)tcp_timer.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/pool.h>

#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_debug.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp_seq.h>

/*
 * Locks used to protect struct members in this file:
 *	T	tcp_timer_mtx		global tcp timer data structures
 */

int	tcp_always_keepalive;
int	tcp_keepinit = TCPTV_KEEPINIT;
int	tcp_keepidle = TCPTV_KEEPIDLE;
int	tcp_keepintvl = TCPTV_KEEPINTVL;
int	tcp_keepinit_sec = TCPTV_KEEPINIT / TCP_TIME(1);
int	tcp_keepidle_sec = TCPTV_KEEPIDLE / TCP_TIME(1);
int	tcp_keepintvl_sec = TCPTV_KEEPINTVL / TCP_TIME(1);
int	tcp_maxpersistidle = TCPTV_KEEPIDLE;	/* max idle time in persist */
int	tcp_delack_msecs = TCP_DELACK_MSECS;	/* time to delay the ACK */

void	tcp_timer_rexmt(void *);
void	tcp_timer_persist(void *);
void	tcp_timer_keep(void *);
void	tcp_timer_2msl(void *);
void	tcp_timer_delack(void *);

const tcp_timer_func_t tcp_timer_funcs[TCPT_NTIMERS] = {
	tcp_timer_rexmt,
	tcp_timer_persist,
	tcp_timer_keep,
	tcp_timer_2msl,
	tcp_timer_delack,
};

static inline int
tcp_timer_enter(struct inpcb *inp, struct socket **so, struct tcpcb **tp,
    u_int timer)
{
	KASSERT(timer < TCPT_NTIMERS);

	NET_LOCK_SHARED();
	*so = in_pcbsolock(inp);
	if (*so == NULL) {
		*tp = NULL;
		return -1;
	}
	*tp = intotcpcb(inp);
	/* Ignore canceled timeouts or timeouts that have been rescheduled. */
	if (*tp == NULL || !ISSET((*tp)->t_flags, TF_TIMER << timer) ||
	    timeout_pending(&(*tp)->t_timer[timer]))
		return -1;
	CLR((*tp)->t_flags, TF_TIMER << timer);

	return 0;
}

static inline void
tcp_timer_leave(struct inpcb *inp, struct socket *so)
{
	in_pcbsounlock(inp, so);
	NET_UNLOCK_SHARED();
	in_pcbunref(inp);
}

/*
 * Callout to process delayed ACKs for a TCPCB.
 */
void
tcp_timer_delack(void *arg)
{
	struct inpcb *inp = arg;
	struct socket *so;
	struct tcpcb *otp = NULL, *tp;
	short ostate;

	/*
	 * If tcp_output() wasn't able to transmit the ACK
	 * for whatever reason, it will restart the delayed
	 * ACK callout.
	 */
	if (tcp_timer_enter(inp, &so, &tp, TCPT_DELACK))
		goto out;

	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}
	tp->t_flags |= TF_ACKNOW;
	(void) tcp_output(tp);
	if (otp)
		tcp_trace(TA_TIMER, ostate, tp, otp, NULL, TCPT_DELACK, 0);
 out:
	tcp_timer_leave(inp, so);
}

/*
 * Tcp protocol timeout routine called every 500 ms.
 * Updates the timers in all active tcb's and
 * causes finite state machine actions if timers expire.
 */
void
tcp_slowtimo(void)
{
	mtx_enter(&tcp_timer_mtx);
	tcp_iss += TCP_ISSINCR2/PR_SLOWHZ;		/* increment iss */
	mtx_leave(&tcp_timer_mtx);
}

/*
 * Cancel all timers for TCP tp.
 */
void
tcp_canceltimers(struct tcpcb *tp)
{
	int i;

	for (i = 0; i < TCPT_NTIMERS; i++)
		TCP_TIMER_DISARM(tp, i);
}

const int tcp_backoff[TCP_MAXRXTSHIFT + 1] =
    { 1, 2, 4, 8, 16, 32, 64, 64, 64, 64, 64, 64, 64 };

const int tcp_totbackoff = 511;	/* sum of tcp_backoff[] */

/*
 * TCP timer processing.
 */

void	tcp_timer_freesack(struct tcpcb *);

void
tcp_timer_freesack(struct tcpcb *tp)
{
	struct sackhole *p, *q;
	/*
	 * Free SACK holes for 2MSL and REXMT timers.
	 */
	q = tp->snd_holes;
	while (q != NULL) {
		p = q;
		q = q->next;
		pool_put(&sackhl_pool, p);
	}
	tp->snd_holes = 0;
}

void
tcp_timer_rexmt(void *arg)
{
	struct inpcb *inp = arg;
	struct socket *so;
	struct tcpcb *otp = NULL, *tp;
	short ostate;
	uint32_t rto;

	if (tcp_timer_enter(inp, &so, &tp, TCPT_REXMT))
		goto out;

	if ((tp->t_flags & TF_PMTUD_PEND) &&
	    SEQ_GEQ(tp->t_pmtud_th_seq, tp->snd_una) &&
	    SEQ_LT(tp->t_pmtud_th_seq, (int)(tp->snd_una + tp->t_maxseg))) {
		struct sockaddr_in sin;
		struct icmp icmp;
		u_int rtableid;

		/* TF_PMTUD_PEND is set in tcp_ctlinput() which is IPv4 only */
		KASSERT(!ISSET(inp->inp_flags, INP_IPV6));
		tp->t_flags &= ~TF_PMTUD_PEND;

		rtableid = inp->inp_rtableid;

		/* XXX create fake icmp message with relevant entries */
		icmp.icmp_nextmtu = tp->t_pmtud_nextmtu;
		icmp.icmp_ip.ip_len = tp->t_pmtud_ip_len;
		icmp.icmp_ip.ip_hl = tp->t_pmtud_ip_hl;
		icmp.icmp_ip.ip_dst = inp->inp_faddr;

		/*
		 * Notify all connections to the same peer about
		 * new mss and trigger retransmit.
		 */
		bzero(&sin, sizeof(sin));
		sin.sin_len = sizeof(sin);
		sin.sin_family = AF_INET;
		sin.sin_addr = inp->inp_faddr;

		in_pcbsounlock(inp, so);
		in_pcbunref(inp);

		icmp_mtudisc(&icmp, rtableid);
		in_pcbnotifyall(&tcbtable, &sin, rtableid, EMSGSIZE,
		    tcp_mtudisc);

		NET_UNLOCK_SHARED();
		return;
	}

	tcp_timer_freesack(tp);
	if (++tp->t_rxtshift > TCP_MAXRXTSHIFT) {
		tp->t_rxtshift = TCP_MAXRXTSHIFT;
		tcpstat_inc(tcps_timeoutdrop);
		tp = tcp_drop(tp, tp->t_softerror ?
		    tp->t_softerror : ETIMEDOUT);
		goto out;
	}
	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}
	tcpstat_inc(tcps_rexmttimeo);
	rto = TCP_REXMTVAL(tp);
	if (rto < tp->t_rttmin)
		rto = tp->t_rttmin;
	TCPT_RANGESET(tp->t_rxtcur,
	    rto * tcp_backoff[tp->t_rxtshift],
	    tp->t_rttmin, TCPTV_REXMTMAX);
	TCP_TIMER_ARM(tp, TCPT_REXMT, tp->t_rxtcur);

	/*
	 * If we are losing and we are trying path MTU discovery,
	 * try turning it off.  This will avoid black holes in
	 * the network which suppress or fail to send "packet
	 * too big" ICMP messages.  We should ideally do
	 * lots more sophisticated searching to find the right
	 * value here...
	 */
	if (atomic_load_int(&ip_mtudisc) &&
	    TCPS_HAVEESTABLISHED(tp->t_state) &&
	    tp->t_rxtshift > TCP_MAXRXTSHIFT / 6) {
		struct rtentry *rt = NULL;

		/* No data to send means path mtu is not a problem */
		if (!READ_ONCE(so->so_snd.sb_cc))
			goto leave;

		rt = in_pcbrtentry(inp);
		/* Check if path MTU discovery is disabled already */
		if (rt && (rt->rt_flags & RTF_HOST) &&
		    (rt->rt_locks & RTV_MTU))
			goto leave;

		rt = NULL;
		switch(tp->pf) {
#ifdef INET6
		case PF_INET6:
			/*
			 * We can not turn off path MTU for IPv6.
			 * Do nothing for now, maybe lower to
			 * minimum MTU.
			 */
			break;
#endif
		case PF_INET:
			rt = icmp_mtudisc_clone(inp->inp_faddr,
			    inp->inp_rtableid, 0);
			break;
		}
		if (rt != NULL) {
			/* Disable path MTU discovery */
			if ((rt->rt_locks & RTV_MTU) == 0) {
				rt->rt_locks |= RTV_MTU;
				in_pcbrtchange(inp, 0);
			}

			rtfree(rt);
		}
	leave:
		;
	}

	/*
	 * If losing, let the lower level know and try for
	 * a better route.  Also, if we backed off this far,
	 * our srtt estimate is probably bogus.  Clobber it
	 * so we'll take the next rtt measurement as our srtt;
	 * move the current srtt into rttvar to keep the current
	 * retransmit times until then.
	 */
	if (tp->t_rxtshift > TCP_MAXRXTSHIFT / 4) {
		in_losing(inp);
		tp->t_rttvar += (tp->t_srtt >> TCP_RTT_SHIFT);
		tp->t_srtt = 0;
	}
	tp->snd_nxt = tp->snd_una;
	/*
	 * Note:  We overload snd_last to function also as the
	 * snd_last variable described in RFC 2582
	 */
	tp->snd_last = tp->snd_max;
	/*
	 * If timing a segment in this window, stop the timer.
	 */
	tp->t_rtttime = 0;
#ifdef TCP_ECN
	/*
	 * if ECN is enabled, there might be a broken firewall which
	 * blocks ecn packets.  fall back to non-ecn.
	 */
	if ((tp->t_state == TCPS_SYN_SENT || tp->t_state == TCPS_SYN_RECEIVED)
	    && atomic_load_int(&tcp_do_ecn) && !(tp->t_flags & TF_DISABLE_ECN))
		tp->t_flags |= TF_DISABLE_ECN;
#endif
	/*
	 * Close the congestion window down to one segment
	 * (we'll open it by one segment for each ack we get).
	 * Since we probably have a window's worth of unacked
	 * data accumulated, this "slow start" keeps us from
	 * dumping all that data as back-to-back packets (which
	 * might overwhelm an intermediate gateway).
	 *
	 * There are two phases to the opening: Initially we
	 * open by one mss on each ack.  This makes the window
	 * size increase exponentially with time.  If the
	 * window is larger than the path can handle, this
	 * exponential growth results in dropped packet(s)
	 * almost immediately.  To get more time between
	 * drops but still "push" the network to take advantage
	 * of improving conditions, we switch from exponential
	 * to linear window opening at some threshold size.
	 * For a threshold, we use half the current window
	 * size, truncated to a multiple of the mss.
	 *
	 * (the minimum cwnd that will give us exponential
	 * growth is 2 mss.  We don't allow the threshold
	 * to go below this.)
	 */
	{
		u_long win;

		win = ulmin(tp->snd_wnd, tp->snd_cwnd) / 2 / tp->t_maxseg;
		if (win < 2)
			win = 2;
		tp->snd_cwnd = tp->t_maxseg;
		tp->snd_ssthresh = win * tp->t_maxseg;
		tp->t_dupacks = 0;
#ifdef TCP_ECN
		tp->snd_last = tp->snd_max;
		tp->t_flags |= TF_SEND_CWR;
#endif
#if 1 /* TCP_ECN */
		tcpstat_inc(tcps_cwr_timeout);
#endif
	}
	(void) tcp_output(tp);
	if (otp)
		tcp_trace(TA_TIMER, ostate, tp, otp, NULL, TCPT_REXMT, 0);
 out:
	tcp_timer_leave(inp, so);
}

void
tcp_timer_persist(void *arg)
{
	struct inpcb *inp = arg;
	struct socket *so;
	struct tcpcb *otp = NULL, *tp;
	short ostate;
	uint64_t now;
	uint32_t rto;

	if (tcp_timer_enter(inp, &so, &tp, TCPT_PERSIST))
		goto out;

	if (TCP_TIMER_ISARMED(tp, TCPT_REXMT))
		goto out;

	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}
	tcpstat_inc(tcps_persisttimeo);
	/*
	 * Hack: if the peer is dead/unreachable, we do not
	 * time out if the window is closed.  After a full
	 * backoff, drop the connection if the idle time
	 * (no responses to probes) reaches the maximum
	 * backoff that we would use if retransmitting.
	 */
	rto = TCP_REXMTVAL(tp);
	if (rto < tp->t_rttmin)
		rto = tp->t_rttmin;
	now = tcp_now();
	if (tp->t_rxtshift == TCP_MAXRXTSHIFT &&
	    ((now - tp->t_rcvtime) >= tcp_maxpersistidle ||
	    (now - tp->t_rcvtime) >= rto * tcp_totbackoff)) {
		tcpstat_inc(tcps_persistdrop);
		tp = tcp_drop(tp, ETIMEDOUT);
		goto out;
	}
	tcp_setpersist(tp);
	tp->t_force = 1;
	(void) tcp_output(tp);
	tp->t_force = 0;
	if (otp)
		tcp_trace(TA_TIMER, ostate, tp, otp, NULL, TCPT_PERSIST, 0);
 out:
	tcp_timer_leave(inp, so);
}

void
tcp_timer_keep(void *arg)
{
	struct inpcb *inp = arg;
	struct socket *so;
	struct tcpcb *otp = NULL, *tp;
	short ostate;

	if (tcp_timer_enter(inp, &so, &tp, TCPT_KEEP))
		goto out;

	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}
	tcpstat_inc(tcps_keeptimeo);
	if (TCPS_HAVEESTABLISHED(tp->t_state) == 0) {
		tcpstat_inc(tcps_keepdrops);
		tp = tcp_drop(tp, ETIMEDOUT);
		goto out;
	}
	if ((atomic_load_int(&tcp_always_keepalive) ||
	    so->so_options & SO_KEEPALIVE) &&
	    tp->t_state <= TCPS_CLOSING) {
		int keepidle, keepintvl, maxidle;
		uint64_t now;

		keepidle = atomic_load_int(&tcp_keepidle);
		keepintvl = atomic_load_int(&tcp_keepintvl);
		maxidle = TCPTV_KEEPCNT * keepintvl;
		now = tcp_now();
		if ((maxidle > 0) &&
		    ((now - tp->t_rcvtime) >= keepidle + maxidle)) {
			tcpstat_inc(tcps_keepdrops);
			tp = tcp_drop(tp, ETIMEDOUT);
			goto out;
		}
		/*
		 * Send a packet designed to force a response
		 * if the peer is up and reachable:
		 * either an ACK if the connection is still alive,
		 * or an RST if the peer has closed the connection
		 * due to timeout or reboot.
		 * Using sequence number tp->snd_una-1
		 * causes the transmitted zero-length segment
		 * to lie outside the receive window;
		 * by the protocol spec, this requires the
		 * correspondent TCP to respond.
		 */
		tcpstat_inc(tcps_keepprobe);
		tcp_respond(tp, mtod(tp->t_template, caddr_t),
		    NULL, tp->rcv_nxt, tp->snd_una - 1, 0, 0, now);
		TCP_TIMER_ARM(tp, TCPT_KEEP, keepintvl);
	} else
		TCP_TIMER_ARM(tp, TCPT_KEEP, atomic_load_int(&tcp_keepidle));
	if (otp)
		tcp_trace(TA_TIMER, ostate, tp, otp, NULL, TCPT_KEEP, 0);
 out:
	tcp_timer_leave(inp, so);
}

void
tcp_timer_2msl(void *arg)
{
	struct inpcb *inp = arg;
	struct socket *so;
	struct tcpcb *otp = NULL, *tp;
	short ostate;
	uint64_t now;
	int keepintvl, maxidle;

	if (tcp_timer_enter(inp, &so, &tp, TCPT_2MSL))
		goto out;

	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}
	tcp_timer_freesack(tp);

	keepintvl = atomic_load_int(&tcp_keepintvl);
	maxidle = TCPTV_KEEPCNT * keepintvl;
	now = tcp_now();
	if (tp->t_state != TCPS_TIME_WAIT &&
	    ((maxidle == 0) || ((now - tp->t_rcvtime) <= maxidle)))
		TCP_TIMER_ARM(tp, TCPT_2MSL, keepintvl);
	else
		tp = tcp_close(tp);
	if (otp)
		tcp_trace(TA_TIMER, ostate, tp, otp, NULL, TCPT_2MSL, 0);
 out:
	tcp_timer_leave(inp, so);
}

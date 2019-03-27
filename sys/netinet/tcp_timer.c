/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1995
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
 *	@(#)tcp_timer.c	8.2 (Berkeley) 5/24/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_tcpdebug.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/protosw.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/route.h>
#include <net/rss_config.h>
#include <net/vnet.h>
#include <net/netisr.h>

#include <netinet/in.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_pcb.h>
#include <netinet/in_rss.h>
#include <netinet/in_systm.h>
#ifdef INET6
#include <netinet6/in6_pcb.h>
#endif
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_log_buf.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_seq.h>
#include <netinet/cc/cc.h>
#ifdef INET6
#include <netinet6/tcp6_var.h>
#endif
#include <netinet/tcpip.h>
#ifdef TCPDEBUG
#include <netinet/tcp_debug.h>
#endif

int    tcp_persmin;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, persmin, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_persmin, 0, sysctl_msec_to_ticks, "I", "minimum persistence interval");

int    tcp_persmax;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, persmax, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_persmax, 0, sysctl_msec_to_ticks, "I", "maximum persistence interval");

int	tcp_keepinit;
SYSCTL_PROC(_net_inet_tcp, TCPCTL_KEEPINIT, keepinit, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_keepinit, 0, sysctl_msec_to_ticks, "I", "time to establish connection");

int	tcp_keepidle;
SYSCTL_PROC(_net_inet_tcp, TCPCTL_KEEPIDLE, keepidle, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_keepidle, 0, sysctl_msec_to_ticks, "I", "time before keepalive probes begin");

int	tcp_keepintvl;
SYSCTL_PROC(_net_inet_tcp, TCPCTL_KEEPINTVL, keepintvl, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_keepintvl, 0, sysctl_msec_to_ticks, "I", "time between keepalive probes");

int	tcp_delacktime;
SYSCTL_PROC(_net_inet_tcp, TCPCTL_DELACKTIME, delacktime, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_delacktime, 0, sysctl_msec_to_ticks, "I",
    "Time before a delayed ACK is sent");

int	tcp_msl;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, msl, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_msl, 0, sysctl_msec_to_ticks, "I", "Maximum segment lifetime");

int	tcp_rexmit_initial;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, rexmit_initial, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_rexmit_initial, 0, sysctl_msec_to_ticks, "I",
    "Initial Retransmission Timeout");

int	tcp_rexmit_min;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, rexmit_min, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_rexmit_min, 0, sysctl_msec_to_ticks, "I",
    "Minimum Retransmission Timeout");

int	tcp_rexmit_slop;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, rexmit_slop, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_rexmit_slop, 0, sysctl_msec_to_ticks, "I",
    "Retransmission Timer Slop");

int	tcp_always_keepalive = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, always_keepalive, CTLFLAG_RW,
    &tcp_always_keepalive , 0, "Assume SO_KEEPALIVE on all TCP connections");

int    tcp_fast_finwait2_recycle = 0;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, fast_finwait2_recycle, CTLFLAG_RW, 
    &tcp_fast_finwait2_recycle, 0,
    "Recycle closed FIN_WAIT_2 connections faster");

int    tcp_finwait2_timeout;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, finwait2_timeout, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_finwait2_timeout, 0, sysctl_msec_to_ticks, "I", "FIN-WAIT2 timeout");

int	tcp_keepcnt = TCPTV_KEEPCNT;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, keepcnt, CTLFLAG_RW, &tcp_keepcnt, 0,
    "Number of keepalive probes to send");

	/* max idle probes */
int	tcp_maxpersistidle;

int	tcp_rexmit_drop_options = 0;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, rexmit_drop_options, CTLFLAG_RW,
    &tcp_rexmit_drop_options, 0,
    "Drop TCP options from 3rd and later retransmitted SYN");

VNET_DEFINE(int, tcp_pmtud_blackhole_detect);
SYSCTL_INT(_net_inet_tcp, OID_AUTO, pmtud_blackhole_detection,
    CTLFLAG_RW|CTLFLAG_VNET,
    &VNET_NAME(tcp_pmtud_blackhole_detect), 0,
    "Path MTU Discovery Black Hole Detection Enabled");

#ifdef INET
VNET_DEFINE(int, tcp_pmtud_blackhole_mss) = 1200;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, pmtud_blackhole_mss,
    CTLFLAG_RW|CTLFLAG_VNET,
    &VNET_NAME(tcp_pmtud_blackhole_mss), 0,
    "Path MTU Discovery Black Hole Detection lowered MSS");
#endif

#ifdef INET6
VNET_DEFINE(int, tcp_v6pmtud_blackhole_mss) = 1220;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, v6pmtud_blackhole_mss,
    CTLFLAG_RW|CTLFLAG_VNET,
    &VNET_NAME(tcp_v6pmtud_blackhole_mss), 0,
    "Path MTU Discovery IPv6 Black Hole Detection lowered MSS");
#endif

#ifdef	RSS
static int	per_cpu_timers = 1;
#else
static int	per_cpu_timers = 0;
#endif
SYSCTL_INT(_net_inet_tcp, OID_AUTO, per_cpu_timers, CTLFLAG_RW,
    &per_cpu_timers , 0, "run tcp timers on all cpus");

/*
 * Map the given inp to a CPU id.
 *
 * This queries RSS if it's compiled in, else it defaults to the current
 * CPU ID.
 */
inline int
inp_to_cpuid(struct inpcb *inp)
{
	u_int cpuid;

#ifdef	RSS
	if (per_cpu_timers) {
		cpuid = rss_hash2cpuid(inp->inp_flowid, inp->inp_flowtype);
		if (cpuid == NETISR_CPUID_NONE)
			return (curcpu);	/* XXX */
		else
			return (cpuid);
	}
#else
	/* Legacy, pre-RSS behaviour */
	if (per_cpu_timers) {
		/*
		 * We don't have a flowid -> cpuid mapping, so cheat and
		 * just map unknown cpuids to curcpu.  Not the best, but
		 * apparently better than defaulting to swi 0.
		 */
		cpuid = inp->inp_flowid % (mp_maxid + 1);
		if (! CPU_ABSENT(cpuid))
			return (cpuid);
		return (curcpu);
	}
#endif
	/* Default for RSS and non-RSS - cpuid 0 */
	else {
		return (0);
	}
}

/*
 * Tcp protocol timeout routine called every 500 ms.
 * Updates timestamps used for TCP
 * causes finite state machine actions if timers expire.
 */
void
tcp_slowtimo(void)
{
	VNET_ITERATOR_DECL(vnet_iter);

	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		(void) tcp_tw_2msl_scan(0);
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
}

int	tcp_backoff[TCP_MAXRXTSHIFT + 1] =
    { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 512, 512, 512 };

int tcp_totbackoff = 2559;	/* sum of tcp_backoff[] */

/*
 * TCP timer processing.
 */

void
tcp_timer_delack(void *xtp)
{
	struct tcpcb *tp = xtp;
	struct inpcb *inp;
	CURVNET_SET(tp->t_vnet);

	inp = tp->t_inpcb;
	KASSERT(inp != NULL, ("%s: tp %p tp->t_inpcb == NULL", __func__, tp));
	INP_WLOCK(inp);
	if (callout_pending(&tp->t_timers->tt_delack) ||
	    !callout_active(&tp->t_timers->tt_delack)) {
		INP_WUNLOCK(inp);
		CURVNET_RESTORE();
		return;
	}
	callout_deactivate(&tp->t_timers->tt_delack);
	if ((inp->inp_flags & INP_DROPPED) != 0) {
		INP_WUNLOCK(inp);
		CURVNET_RESTORE();
		return;
	}
	tp->t_flags |= TF_ACKNOW;
	TCPSTAT_INC(tcps_delack);
	(void) tp->t_fb->tfb_tcp_output(tp);
	INP_WUNLOCK(inp);
	CURVNET_RESTORE();
}

void
tcp_inpinfo_lock_del(struct inpcb *inp, struct tcpcb *tp)
{
	if (inp && tp != NULL)
		INP_WUNLOCK(inp);
}

void
tcp_timer_2msl(void *xtp)
{
	struct tcpcb *tp = xtp;
	struct inpcb *inp;
	struct epoch_tracker et;
	CURVNET_SET(tp->t_vnet);
#ifdef TCPDEBUG
	int ostate;

	ostate = tp->t_state;
#endif
	inp = tp->t_inpcb;
	KASSERT(inp != NULL, ("%s: tp %p tp->t_inpcb == NULL", __func__, tp));
	INP_WLOCK(inp);
	tcp_free_sackholes(tp);
	if (callout_pending(&tp->t_timers->tt_2msl) ||
	    !callout_active(&tp->t_timers->tt_2msl)) {
		INP_WUNLOCK(tp->t_inpcb);
		CURVNET_RESTORE();
		return;
	}
	callout_deactivate(&tp->t_timers->tt_2msl);
	if ((inp->inp_flags & INP_DROPPED) != 0) {
		INP_WUNLOCK(inp);
		CURVNET_RESTORE();
		return;
	}
	KASSERT((tp->t_timers->tt_flags & TT_STOPPED) == 0,
		("%s: tp %p tcpcb can't be stopped here", __func__, tp));
	/*
	 * 2 MSL timeout in shutdown went off.  If we're closed but
	 * still waiting for peer to close and connection has been idle
	 * too long delete connection control block.  Otherwise, check
	 * again in a bit.
	 *
	 * If in TIME_WAIT state just ignore as this timeout is handled in
	 * tcp_tw_2msl_scan().
	 *
	 * If fastrecycle of FIN_WAIT_2, in FIN_WAIT_2 and receiver has closed, 
	 * there's no point in hanging onto FIN_WAIT_2 socket. Just close it. 
	 * Ignore fact that there were recent incoming segments.
	 */
	if ((inp->inp_flags & INP_TIMEWAIT) != 0) {
		INP_WUNLOCK(inp);
		CURVNET_RESTORE();
		return;
	}
	if (tcp_fast_finwait2_recycle && tp->t_state == TCPS_FIN_WAIT_2 &&
	    tp->t_inpcb && tp->t_inpcb->inp_socket && 
	    (tp->t_inpcb->inp_socket->so_rcv.sb_state & SBS_CANTRCVMORE)) {
		TCPSTAT_INC(tcps_finwait2_drops);
		if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
			tcp_inpinfo_lock_del(inp, tp);
			goto out;
		}
		INP_INFO_RLOCK_ET(&V_tcbinfo, et);
		tp = tcp_close(tp);             
		INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
		tcp_inpinfo_lock_del(inp, tp);
		goto out;
	} else {
		if (ticks - tp->t_rcvtime <= TP_MAXIDLE(tp)) {
			callout_reset(&tp->t_timers->tt_2msl,
				      TP_KEEPINTVL(tp), tcp_timer_2msl, tp);
		} else {
			if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
				tcp_inpinfo_lock_del(inp, tp);
				goto out;
			}
			INP_INFO_RLOCK_ET(&V_tcbinfo, et);
			tp = tcp_close(tp);
			INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
			tcp_inpinfo_lock_del(inp, tp);
			goto out;
		}
	}

#ifdef TCPDEBUG
	if (tp != NULL && (tp->t_inpcb->inp_socket->so_options & SO_DEBUG))
		tcp_trace(TA_USER, ostate, tp, (void *)0, (struct tcphdr *)0,
			  PRU_SLOWTIMO);
#endif
	TCP_PROBE2(debug__user, tp, PRU_SLOWTIMO);

	if (tp != NULL)
		INP_WUNLOCK(inp);
out:
	CURVNET_RESTORE();
}

void
tcp_timer_keep(void *xtp)
{
	struct tcpcb *tp = xtp;
	struct tcptemp *t_template;
	struct inpcb *inp;
	struct epoch_tracker et;
	CURVNET_SET(tp->t_vnet);
#ifdef TCPDEBUG
	int ostate;

	ostate = tp->t_state;
#endif
	inp = tp->t_inpcb;
	KASSERT(inp != NULL, ("%s: tp %p tp->t_inpcb == NULL", __func__, tp));
	INP_WLOCK(inp);
	if (callout_pending(&tp->t_timers->tt_keep) ||
	    !callout_active(&tp->t_timers->tt_keep)) {
		INP_WUNLOCK(inp);
		CURVNET_RESTORE();
		return;
	}
	callout_deactivate(&tp->t_timers->tt_keep);
	if ((inp->inp_flags & INP_DROPPED) != 0) {
		INP_WUNLOCK(inp);
		CURVNET_RESTORE();
		return;
	}
	KASSERT((tp->t_timers->tt_flags & TT_STOPPED) == 0,
		("%s: tp %p tcpcb can't be stopped here", __func__, tp));

	/*
	 * Because we don't regularly reset the keepalive callout in
	 * the ESTABLISHED state, it may be that we don't actually need
	 * to send a keepalive yet. If that occurs, schedule another
	 * call for the next time the keepalive timer might expire.
	 */
	if (TCPS_HAVEESTABLISHED(tp->t_state)) {
		u_int idletime;

		idletime = ticks - tp->t_rcvtime;
		if (idletime < TP_KEEPIDLE(tp)) {
			callout_reset(&tp->t_timers->tt_keep,
			    TP_KEEPIDLE(tp) - idletime, tcp_timer_keep, tp);
			INP_WUNLOCK(inp);
			CURVNET_RESTORE();
			return;
		}
	}

	/*
	 * Keep-alive timer went off; send something
	 * or drop connection if idle for too long.
	 */
	TCPSTAT_INC(tcps_keeptimeo);
	if (tp->t_state < TCPS_ESTABLISHED)
		goto dropit;
	if ((tcp_always_keepalive ||
	    inp->inp_socket->so_options & SO_KEEPALIVE) &&
	    tp->t_state <= TCPS_CLOSING) {
		if (ticks - tp->t_rcvtime >= TP_KEEPIDLE(tp) + TP_MAXIDLE(tp))
			goto dropit;
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
		TCPSTAT_INC(tcps_keepprobe);
		t_template = tcpip_maketemplate(inp);
		if (t_template) {
			tcp_respond(tp, t_template->tt_ipgen,
				    &t_template->tt_t, (struct mbuf *)NULL,
				    tp->rcv_nxt, tp->snd_una - 1, 0);
			free(t_template, M_TEMP);
		}
		callout_reset(&tp->t_timers->tt_keep, TP_KEEPINTVL(tp),
			      tcp_timer_keep, tp);
	} else
		callout_reset(&tp->t_timers->tt_keep, TP_KEEPIDLE(tp),
			      tcp_timer_keep, tp);

#ifdef TCPDEBUG
	if (inp->inp_socket->so_options & SO_DEBUG)
		tcp_trace(TA_USER, ostate, tp, (void *)0, (struct tcphdr *)0,
			  PRU_SLOWTIMO);
#endif
	TCP_PROBE2(debug__user, tp, PRU_SLOWTIMO);
	INP_WUNLOCK(inp);
	CURVNET_RESTORE();
	return;

dropit:
	TCPSTAT_INC(tcps_keepdrops);
	if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
		tcp_inpinfo_lock_del(inp, tp);
		goto out;
	}
	INP_INFO_RLOCK_ET(&V_tcbinfo, et);
	tp = tcp_drop(tp, ETIMEDOUT);

#ifdef TCPDEBUG
	if (tp != NULL && (tp->t_inpcb->inp_socket->so_options & SO_DEBUG))
		tcp_trace(TA_USER, ostate, tp, (void *)0, (struct tcphdr *)0,
			  PRU_SLOWTIMO);
#endif
	TCP_PROBE2(debug__user, tp, PRU_SLOWTIMO);
	INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
	tcp_inpinfo_lock_del(inp, tp);
 out:
	CURVNET_RESTORE();
}

void
tcp_timer_persist(void *xtp)
{
	struct tcpcb *tp = xtp;
	struct inpcb *inp;
	struct epoch_tracker et;
	CURVNET_SET(tp->t_vnet);
#ifdef TCPDEBUG
	int ostate;

	ostate = tp->t_state;
#endif
	inp = tp->t_inpcb;
	KASSERT(inp != NULL, ("%s: tp %p tp->t_inpcb == NULL", __func__, tp));
	INP_WLOCK(inp);
	if (callout_pending(&tp->t_timers->tt_persist) ||
	    !callout_active(&tp->t_timers->tt_persist)) {
		INP_WUNLOCK(inp);
		CURVNET_RESTORE();
		return;
	}
	callout_deactivate(&tp->t_timers->tt_persist);
	if ((inp->inp_flags & INP_DROPPED) != 0) {
		INP_WUNLOCK(inp);
		CURVNET_RESTORE();
		return;
	}
	KASSERT((tp->t_timers->tt_flags & TT_STOPPED) == 0,
		("%s: tp %p tcpcb can't be stopped here", __func__, tp));
	/*
	 * Persistence timer into zero window.
	 * Force a byte to be output, if possible.
	 */
	TCPSTAT_INC(tcps_persisttimeo);
	/*
	 * Hack: if the peer is dead/unreachable, we do not
	 * time out if the window is closed.  After a full
	 * backoff, drop the connection if the idle time
	 * (no responses to probes) reaches the maximum
	 * backoff that we would use if retransmitting.
	 */
	if (tp->t_rxtshift == TCP_MAXRXTSHIFT &&
	    (ticks - tp->t_rcvtime >= tcp_maxpersistidle ||
	     ticks - tp->t_rcvtime >= TCP_REXMTVAL(tp) * tcp_totbackoff)) {
		TCPSTAT_INC(tcps_persistdrop);
		if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
			tcp_inpinfo_lock_del(inp, tp);
			goto out;
		}
		INP_INFO_RLOCK_ET(&V_tcbinfo, et);
		tp = tcp_drop(tp, ETIMEDOUT);
		INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
		tcp_inpinfo_lock_del(inp, tp);
		goto out;
	}
	/*
	 * If the user has closed the socket then drop a persisting
	 * connection after a much reduced timeout.
	 */
	if (tp->t_state > TCPS_CLOSE_WAIT &&
	    (ticks - tp->t_rcvtime) >= TCPTV_PERSMAX) {
		TCPSTAT_INC(tcps_persistdrop);
		if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
			tcp_inpinfo_lock_del(inp, tp);
			goto out;
		}
		INP_INFO_RLOCK_ET(&V_tcbinfo, et);
		tp = tcp_drop(tp, ETIMEDOUT);
		INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
		tcp_inpinfo_lock_del(inp, tp);
		goto out;
	}
	tcp_setpersist(tp);
	tp->t_flags |= TF_FORCEDATA;
	(void) tp->t_fb->tfb_tcp_output(tp);
	tp->t_flags &= ~TF_FORCEDATA;

#ifdef TCPDEBUG
	if (tp != NULL && tp->t_inpcb->inp_socket->so_options & SO_DEBUG)
		tcp_trace(TA_USER, ostate, tp, NULL, NULL, PRU_SLOWTIMO);
#endif
	TCP_PROBE2(debug__user, tp, PRU_SLOWTIMO);
	INP_WUNLOCK(inp);
out:
	CURVNET_RESTORE();
}

void
tcp_timer_rexmt(void * xtp)
{
	struct tcpcb *tp = xtp;
	CURVNET_SET(tp->t_vnet);
	int rexmt;
	struct inpcb *inp;
	struct epoch_tracker et;
#ifdef TCPDEBUG
	int ostate;

	ostate = tp->t_state;
#endif
	inp = tp->t_inpcb;
	KASSERT(inp != NULL, ("%s: tp %p tp->t_inpcb == NULL", __func__, tp));
	INP_WLOCK(inp);
	if (callout_pending(&tp->t_timers->tt_rexmt) ||
	    !callout_active(&tp->t_timers->tt_rexmt)) {
		INP_WUNLOCK(inp);
		CURVNET_RESTORE();
		return;
	}
	callout_deactivate(&tp->t_timers->tt_rexmt);
	if ((inp->inp_flags & INP_DROPPED) != 0) {
		INP_WUNLOCK(inp);
		CURVNET_RESTORE();
		return;
	}
	KASSERT((tp->t_timers->tt_flags & TT_STOPPED) == 0,
		("%s: tp %p tcpcb can't be stopped here", __func__, tp));
	tcp_free_sackholes(tp);
	TCP_LOG_EVENT(tp, NULL, NULL, NULL, TCP_LOG_RTO, 0, 0, NULL, false);
	if (tp->t_fb->tfb_tcp_rexmit_tmr) {
		/* The stack has a timer action too. */
		(*tp->t_fb->tfb_tcp_rexmit_tmr)(tp);
	}
	/*
	 * Retransmission timer went off.  Message has not
	 * been acked within retransmit interval.  Back off
	 * to a longer retransmit interval and retransmit one segment.
	 */
	if (++tp->t_rxtshift > TCP_MAXRXTSHIFT) {
		tp->t_rxtshift = TCP_MAXRXTSHIFT;
		TCPSTAT_INC(tcps_timeoutdrop);
		if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
			tcp_inpinfo_lock_del(inp, tp);
			goto out;
		}
		INP_INFO_RLOCK_ET(&V_tcbinfo, et);
		tp = tcp_drop(tp, ETIMEDOUT);
		INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
		tcp_inpinfo_lock_del(inp, tp);
		goto out;
	}
	if (tp->t_state == TCPS_SYN_SENT) {
		/*
		 * If the SYN was retransmitted, indicate CWND to be
		 * limited to 1 segment in cc_conn_init().
		 */
		tp->snd_cwnd = 1;
	} else if (tp->t_rxtshift == 1) {
		/*
		 * first retransmit; record ssthresh and cwnd so they can
		 * be recovered if this turns out to be a "bad" retransmit.
		 * A retransmit is considered "bad" if an ACK for this
		 * segment is received within RTT/2 interval; the assumption
		 * here is that the ACK was already in flight.  See
		 * "On Estimating End-to-End Network Path Properties" by
		 * Allman and Paxson for more details.
		 */
		tp->snd_cwnd_prev = tp->snd_cwnd;
		tp->snd_ssthresh_prev = tp->snd_ssthresh;
		tp->snd_recover_prev = tp->snd_recover;
		if (IN_FASTRECOVERY(tp->t_flags))
			tp->t_flags |= TF_WASFRECOVERY;
		else
			tp->t_flags &= ~TF_WASFRECOVERY;
		if (IN_CONGRECOVERY(tp->t_flags))
			tp->t_flags |= TF_WASCRECOVERY;
		else
			tp->t_flags &= ~TF_WASCRECOVERY;
		if ((tp->t_flags & TF_RCVD_TSTMP) == 0)
			tp->t_badrxtwin = ticks + (tp->t_srtt >> (TCP_RTT_SHIFT + 1));
		/* In the event that we've negotiated timestamps
		 * badrxtwin will be set to the value that we set
		 * the retransmitted packet's to_tsval to by tcp_output
		 */
		tp->t_flags |= TF_PREVVALID;
	} else
		tp->t_flags &= ~TF_PREVVALID;
	TCPSTAT_INC(tcps_rexmttimeo);
	if ((tp->t_state == TCPS_SYN_SENT) ||
	    (tp->t_state == TCPS_SYN_RECEIVED))
		rexmt = tcp_rexmit_initial * tcp_backoff[tp->t_rxtshift];
	else
		rexmt = TCP_REXMTVAL(tp) * tcp_backoff[tp->t_rxtshift];
	TCPT_RANGESET(tp->t_rxtcur, rexmt,
		      tp->t_rttmin, TCPTV_REXMTMAX);

	/*
	 * We enter the path for PLMTUD if connection is established or, if
	 * connection is FIN_WAIT_1 status, reason for the last is that if
	 * amount of data we send is very small, we could send it in couple of
	 * packets and process straight to FIN. In that case we won't catch
	 * ESTABLISHED state.
	 */
	if (V_tcp_pmtud_blackhole_detect && (((tp->t_state == TCPS_ESTABLISHED))
	    || (tp->t_state == TCPS_FIN_WAIT_1))) {
#ifdef INET6
		int isipv6;
#endif

		/*
		 * Idea here is that at each stage of mtu probe (usually, 1448
		 * -> 1188 -> 524) should be given 2 chances to recover before
		 *  further clamping down. 'tp->t_rxtshift % 2 == 0' should
		 *  take care of that.
		 */
		if (((tp->t_flags2 & (TF2_PLPMTU_PMTUD|TF2_PLPMTU_MAXSEGSNT)) ==
		    (TF2_PLPMTU_PMTUD|TF2_PLPMTU_MAXSEGSNT)) &&
		    (tp->t_rxtshift >= 2 && tp->t_rxtshift < 6 &&
		    tp->t_rxtshift % 2 == 0)) {
			/*
			 * Enter Path MTU Black-hole Detection mechanism:
			 * - Disable Path MTU Discovery (IP "DF" bit).
			 * - Reduce MTU to lower value than what we
			 *   negotiated with peer.
			 */
			if ((tp->t_flags2 & TF2_PLPMTU_BLACKHOLE) == 0) {
				/* Record that we may have found a black hole. */
				tp->t_flags2 |= TF2_PLPMTU_BLACKHOLE;
				/* Keep track of previous MSS. */
				tp->t_pmtud_saved_maxseg = tp->t_maxseg;
			}

			/* 
			 * Reduce the MSS to blackhole value or to the default
			 * in an attempt to retransmit.
			 */
#ifdef INET6
			isipv6 = (tp->t_inpcb->inp_vflag & INP_IPV6) ? 1 : 0;
			if (isipv6 &&
			    tp->t_maxseg > V_tcp_v6pmtud_blackhole_mss) {
				/* Use the sysctl tuneable blackhole MSS. */
				tp->t_maxseg = V_tcp_v6pmtud_blackhole_mss;
				TCPSTAT_INC(tcps_pmtud_blackhole_activated);
			} else if (isipv6) {
				/* Use the default MSS. */
				tp->t_maxseg = V_tcp_v6mssdflt;
				/*
				 * Disable Path MTU Discovery when we switch to
				 * minmss.
				 */
				tp->t_flags2 &= ~TF2_PLPMTU_PMTUD;
				TCPSTAT_INC(tcps_pmtud_blackhole_activated_min_mss);
			}
#endif
#if defined(INET6) && defined(INET)
			else
#endif
#ifdef INET
			if (tp->t_maxseg > V_tcp_pmtud_blackhole_mss) {
				/* Use the sysctl tuneable blackhole MSS. */
				tp->t_maxseg = V_tcp_pmtud_blackhole_mss;
				TCPSTAT_INC(tcps_pmtud_blackhole_activated);
			} else {
				/* Use the default MSS. */
				tp->t_maxseg = V_tcp_mssdflt;
				/*
				 * Disable Path MTU Discovery when we switch to
				 * minmss.
				 */
				tp->t_flags2 &= ~TF2_PLPMTU_PMTUD;
				TCPSTAT_INC(tcps_pmtud_blackhole_activated_min_mss);
			}
#endif
			/*
			 * Reset the slow-start flight size
			 * as it may depend on the new MSS.
			 */
			if (CC_ALGO(tp)->conn_init != NULL)
				CC_ALGO(tp)->conn_init(tp->ccv);
		} else {
			/*
			 * If further retransmissions are still unsuccessful
			 * with a lowered MTU, maybe this isn't a blackhole and
			 * we restore the previous MSS and blackhole detection
			 * flags.
			 * The limit '6' is determined by giving each probe
			 * stage (1448, 1188, 524) 2 chances to recover.
			 */
			if ((tp->t_flags2 & TF2_PLPMTU_BLACKHOLE) &&
			    (tp->t_rxtshift >= 6)) {
				tp->t_flags2 |= TF2_PLPMTU_PMTUD;
				tp->t_flags2 &= ~TF2_PLPMTU_BLACKHOLE;
				tp->t_maxseg = tp->t_pmtud_saved_maxseg;
				TCPSTAT_INC(tcps_pmtud_blackhole_failed);
				/*
				 * Reset the slow-start flight size as it
				 * may depend on the new MSS.
				 */
				if (CC_ALGO(tp)->conn_init != NULL)
					CC_ALGO(tp)->conn_init(tp->ccv);
			}
		}
	}

	/*
	 * Disable RFC1323 and SACK if we haven't got any response to
	 * our third SYN to work-around some broken terminal servers
	 * (most of which have hopefully been retired) that have bad VJ
	 * header compression code which trashes TCP segments containing
	 * unknown-to-them TCP options.
	 */
	if (tcp_rexmit_drop_options && (tp->t_state == TCPS_SYN_SENT) &&
	    (tp->t_rxtshift == 3))
		tp->t_flags &= ~(TF_REQ_SCALE|TF_REQ_TSTMP|TF_SACK_PERMIT);
	/*
	 * If we backed off this far, notify the L3 protocol that we're having
	 * connection problems.
	 */
	if (tp->t_rxtshift > TCP_RTT_INVALIDATE) {
#ifdef INET6
		if ((tp->t_inpcb->inp_vflag & INP_IPV6) != 0)
			in6_losing(tp->t_inpcb);
		else
#endif
			in_losing(tp->t_inpcb);
	}
	tp->snd_nxt = tp->snd_una;
	tp->snd_recover = tp->snd_max;
	/*
	 * Force a segment to be sent.
	 */
	tp->t_flags |= TF_ACKNOW;
	/*
	 * If timing a segment in this window, stop the timer.
	 */
	tp->t_rtttime = 0;

	cc_cong_signal(tp, NULL, CC_RTO);

	(void) tp->t_fb->tfb_tcp_output(tp);

#ifdef TCPDEBUG
	if (tp != NULL && (tp->t_inpcb->inp_socket->so_options & SO_DEBUG))
		tcp_trace(TA_USER, ostate, tp, (void *)0, (struct tcphdr *)0,
			  PRU_SLOWTIMO);
#endif
	TCP_PROBE2(debug__user, tp, PRU_SLOWTIMO);
	INP_WUNLOCK(inp);
out:
	CURVNET_RESTORE();
}

void
tcp_timer_activate(struct tcpcb *tp, uint32_t timer_type, u_int delta)
{
	struct callout *t_callout;
	timeout_t *f_callout;
	struct inpcb *inp = tp->t_inpcb;
	int cpu = inp_to_cpuid(inp);

#ifdef TCP_OFFLOAD
	if (tp->t_flags & TF_TOE)
		return;
#endif

	if (tp->t_timers->tt_flags & TT_STOPPED)
		return;

	switch (timer_type) {
		case TT_DELACK:
			t_callout = &tp->t_timers->tt_delack;
			f_callout = tcp_timer_delack;
			break;
		case TT_REXMT:
			t_callout = &tp->t_timers->tt_rexmt;
			f_callout = tcp_timer_rexmt;
			break;
		case TT_PERSIST:
			t_callout = &tp->t_timers->tt_persist;
			f_callout = tcp_timer_persist;
			break;
		case TT_KEEP:
			t_callout = &tp->t_timers->tt_keep;
			f_callout = tcp_timer_keep;
			break;
		case TT_2MSL:
			t_callout = &tp->t_timers->tt_2msl;
			f_callout = tcp_timer_2msl;
			break;
		default:
			if (tp->t_fb->tfb_tcp_timer_activate) {
				tp->t_fb->tfb_tcp_timer_activate(tp, timer_type, delta);
				return;
			}
			panic("tp %p bad timer_type %#x", tp, timer_type);
		}
	if (delta == 0) {
		callout_stop(t_callout);
	} else {
		callout_reset_on(t_callout, delta, f_callout, tp, cpu);
	}
}

int
tcp_timer_active(struct tcpcb *tp, uint32_t timer_type)
{
	struct callout *t_callout;

	switch (timer_type) {
		case TT_DELACK:
			t_callout = &tp->t_timers->tt_delack;
			break;
		case TT_REXMT:
			t_callout = &tp->t_timers->tt_rexmt;
			break;
		case TT_PERSIST:
			t_callout = &tp->t_timers->tt_persist;
			break;
		case TT_KEEP:
			t_callout = &tp->t_timers->tt_keep;
			break;
		case TT_2MSL:
			t_callout = &tp->t_timers->tt_2msl;
			break;
		default:
			if (tp->t_fb->tfb_tcp_timer_active) {
				return(tp->t_fb->tfb_tcp_timer_active(tp, timer_type));
			}
			panic("tp %p bad timer_type %#x", tp, timer_type);
		}
	return callout_active(t_callout);
}

/*
 * Stop the timer from running, and apply a flag
 * against the timer_flags that will force the
 * timer never to run. The flag is needed to assure
 * a race does not leave it running and cause
 * the timer to possibly restart itself (keep and persist
 * especially do this). 
 */
int
tcp_timer_suspend(struct tcpcb *tp, uint32_t timer_type)
{
	struct callout *t_callout;
	uint32_t t_flags;

	switch (timer_type) {
		case TT_DELACK:
			t_flags = TT_DELACK_SUS;
			t_callout = &tp->t_timers->tt_delack;
			break;
		case TT_REXMT:
			t_flags = TT_REXMT_SUS;
			t_callout = &tp->t_timers->tt_rexmt;
			break;
		case TT_PERSIST:
			t_flags = TT_PERSIST_SUS;
			t_callout = &tp->t_timers->tt_persist;
			break;
		case TT_KEEP:
			t_flags = TT_KEEP_SUS;
			t_callout = &tp->t_timers->tt_keep;
			break;
		case TT_2MSL:
			t_flags = TT_2MSL_SUS;
			t_callout = &tp->t_timers->tt_2msl;
			break;
		default:
			panic("tp:%p bad timer_type 0x%x", tp, timer_type);
	}
	tp->t_timers->tt_flags |= t_flags;
	return (callout_stop(t_callout));
}

void
tcp_timers_unsuspend(struct tcpcb *tp, uint32_t timer_type)
{
	switch (timer_type) {
		case TT_DELACK:
			if (tp->t_timers->tt_flags & TT_DELACK_SUS) {
				tp->t_timers->tt_flags &= ~TT_DELACK_SUS;
				if (tp->t_flags & TF_DELACK) {
					/* Delayed ack timer should be up activate a timer */
					tp->t_flags &= ~TF_DELACK;
					tcp_timer_activate(tp, TT_DELACK,
					    tcp_delacktime);
				}
			}
			break;
		case TT_REXMT:
			if (tp->t_timers->tt_flags & TT_REXMT_SUS) {
				tp->t_timers->tt_flags &= ~TT_REXMT_SUS;
				if (SEQ_GT(tp->snd_max, tp->snd_una) &&
				    (tcp_timer_active((tp), TT_PERSIST) == 0) &&
				    tp->snd_wnd) {
					/* We have outstanding data activate a timer */
					tcp_timer_activate(tp, TT_REXMT, 
                                            tp->t_rxtcur);
				}
			}
			break;
		case TT_PERSIST:
			if (tp->t_timers->tt_flags & TT_PERSIST_SUS) {
				tp->t_timers->tt_flags &= ~TT_PERSIST_SUS;
				if (tp->snd_wnd == 0) {
					/* Activate the persists timer */
					tp->t_rxtshift = 0;
					tcp_setpersist(tp);
				}
			}
			break;
		case TT_KEEP:
			if (tp->t_timers->tt_flags & TT_KEEP_SUS) {
				tp->t_timers->tt_flags &= ~TT_KEEP_SUS;
				tcp_timer_activate(tp, TT_KEEP,
					    TCPS_HAVEESTABLISHED(tp->t_state) ?
					    TP_KEEPIDLE(tp) : TP_KEEPINIT(tp));
			}
			break;
		case TT_2MSL:
			if (tp->t_timers->tt_flags &= TT_2MSL_SUS) {
				tp->t_timers->tt_flags &= ~TT_2MSL_SUS;
				if ((tp->t_state == TCPS_FIN_WAIT_2) &&
				    ((tp->t_inpcb->inp_socket == NULL) ||
				     (tp->t_inpcb->inp_socket->so_rcv.sb_state & SBS_CANTRCVMORE))) {
					/* Star the 2MSL timer */
					tcp_timer_activate(tp, TT_2MSL,
					    (tcp_fast_finwait2_recycle) ?
					    tcp_finwait2_timeout : TP_MAXIDLE(tp));
				}
			}
			break;
		default:
			panic("tp:%p bad timer_type 0x%x", tp, timer_type);
	}
}

void
tcp_timer_stop(struct tcpcb *tp, uint32_t timer_type)
{
	struct callout *t_callout;

	tp->t_timers->tt_flags |= TT_STOPPED;
	switch (timer_type) {
		case TT_DELACK:
			t_callout = &tp->t_timers->tt_delack;
			break;
		case TT_REXMT:
			t_callout = &tp->t_timers->tt_rexmt;
			break;
		case TT_PERSIST:
			t_callout = &tp->t_timers->tt_persist;
			break;
		case TT_KEEP:
			t_callout = &tp->t_timers->tt_keep;
			break;
		case TT_2MSL:
			t_callout = &tp->t_timers->tt_2msl;
			break;
		default:
			if (tp->t_fb->tfb_tcp_timer_stop) {
				/* 
				 * XXXrrs we need to look at this with the
				 * stop case below (flags).
				 */
				tp->t_fb->tfb_tcp_timer_stop(tp, timer_type);
				return;
			}
			panic("tp %p bad timer_type %#x", tp, timer_type);
		}

	if (callout_async_drain(t_callout, tcp_timer_discard) == 0) {
		/*
		 * Can't stop the callout, defer tcpcb actual deletion
		 * to the last one. We do this using the async drain
		 * function and incrementing the count in 
		 */
		tp->t_timers->tt_draincnt++;
	}
}

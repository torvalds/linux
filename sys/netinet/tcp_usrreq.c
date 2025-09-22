/*	$OpenBSD: tcp_usrreq.c,v 1.252 2025/07/08 00:47:41 jsg Exp $	*/
/*	$NetBSD: tcp_usrreq.c,v 1.20 1996/02/13 23:44:16 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *	@(#)COPYRIGHT	1.1 (NRL) 17 January 1995
 *
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed at the Information
 *	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/domain.h>
#include <sys/pool.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet6/ip6_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_debug.h>

#ifdef INET6
#include <netinet6/in6_var.h>
#endif

/*
 * Locks used to protect global variables in this file:
 *	I	immutable after creation
 */

#ifndef TCP_SENDSPACE
#define	TCP_SENDSPACE	1024*16
#endif
u_int	tcp_sendspace = TCP_SENDSPACE;		/* [I] */
#ifndef TCP_RECVSPACE
#define	TCP_RECVSPACE	1024*16
#endif
u_int	tcp_recvspace = TCP_RECVSPACE;		/* [I] */
u_int	tcp_autorcvbuf_inc = 16 * 1024;		/* [I] */

const struct pr_usrreqs tcp_usrreqs = {
	.pru_attach	= tcp_attach,
	.pru_detach	= tcp_detach,
	.pru_bind	= tcp_bind,
	.pru_listen	= tcp_listen,
	.pru_connect	= tcp_connect,
	.pru_accept	= tcp_accept,
	.pru_disconnect	= tcp_disconnect,
	.pru_shutdown	= tcp_shutdown,
	.pru_rcvd	= tcp_rcvd,
	.pru_send	= tcp_send,
	.pru_abort	= tcp_abort,
	.pru_sense	= tcp_sense,
	.pru_rcvoob	= tcp_rcvoob,
	.pru_sendoob	= tcp_sendoob,
	.pru_control	= in_control,
	.pru_sockaddr	= tcp_sockaddr,
	.pru_peeraddr	= tcp_peeraddr,
};

#ifdef INET6
const struct pr_usrreqs tcp6_usrreqs = {
	.pru_attach	= tcp_attach,
	.pru_detach	= tcp_detach,
	.pru_bind	= tcp_bind,
	.pru_listen	= tcp_listen,
	.pru_connect	= tcp_connect,
	.pru_accept	= tcp_accept,
	.pru_disconnect	= tcp_disconnect,
	.pru_shutdown	= tcp_shutdown,
	.pru_rcvd	= tcp_rcvd,
	.pru_send	= tcp_send,
	.pru_abort	= tcp_abort,
	.pru_sense	= tcp_sense,
	.pru_rcvoob	= tcp_rcvoob,
	.pru_sendoob	= tcp_sendoob,
	.pru_control	= in6_control,
	.pru_sockaddr	= tcp_sockaddr,
	.pru_peeraddr	= tcp_peeraddr,
};
#endif

#ifndef SMALL_KERNEL
const struct sysctl_bounded_args tcpctl_vars[] = {
	{ TCPCTL_KEEPINITTIME, &tcp_keepinit_sec, 1,
	    3 * TCPTV_KEEPINIT / TCP_TIME(1) },
	{ TCPCTL_KEEPIDLE, &tcp_keepidle_sec, 1,
	    5 * TCPTV_KEEPIDLE / TCP_TIME(1) },
	{ TCPCTL_KEEPINTVL, &tcp_keepintvl_sec, 1,
	    3 * TCPTV_KEEPINTVL / TCP_TIME(1) },
	{ TCPCTL_RFC1323, &tcp_do_rfc1323, 0, 1 },
	{ TCPCTL_SACK, &tcp_do_sack, 0, 1 },
	{ TCPCTL_MSSDFLT, &tcp_mssdflt, TCP_MSS, 65535 },
	{ TCPCTL_RSTPPSLIMIT, &tcp_rst_ppslim, 1, 1000 * 1000 },
	{ TCPCTL_ACK_ON_PUSH, &tcp_ack_on_push, 0, 1 },
#ifdef TCP_ECN
	{ TCPCTL_ECN, &tcp_do_ecn, 0, 1 },
#endif
	{ TCPCTL_SYN_CACHE_LIMIT, &tcp_syn_cache_limit, 1, 1000 * 1000 },
	{ TCPCTL_SYN_BUCKET_LIMIT, &tcp_syn_bucket_limit, 1, INT_MAX },
	{ TCPCTL_RFC3390, &tcp_do_rfc3390, 0, 2 },
	{ TCPCTL_ALWAYS_KEEPALIVE, &tcp_always_keepalive, 0, 1 },
	{ TCPCTL_TSO, &tcp_do_tso, 0, 1 },
};
#endif /* SMALL_KERNEL */

struct	inpcbtable tcbtable;
#ifdef INET6
struct	inpcbtable tcb6table;
#endif

int	tcp_fill_info(struct tcpcb *, struct socket *, struct mbuf *);
int	tcp_ident(void *, size_t *, void *, size_t, int);

static inline int tcp_sogetpcb(struct socket *, struct inpcb **,
		    struct tcpcb **);

static inline int
tcp_sogetpcb(struct socket *so, struct inpcb **rinp, struct tcpcb **rtp)
{
	struct inpcb *inp;
	struct tcpcb *tp;

	/*
	 * When a TCP is attached to a socket, then there will be
	 * a (struct inpcb) pointed at by the socket, and this
	 * structure will point at a subsidiary (struct tcpcb).
	 */
	if ((inp = sotoinpcb(so)) == NULL || (tp = intotcpcb(inp)) == NULL) {
		int error;

		if ((error = READ_ONCE(so->so_error)))
			return error;
		return EINVAL;
	}

	*rinp = inp;
	*rtp = tp;

	return 0;
}

/*
 * Export internal TCP state information via a struct tcp_info without
 * leaking any sensitive information. Sequence numbers are reported
 * relative to the initial sequence number.
 */
int
tcp_fill_info(struct tcpcb *tp, struct socket *so, struct mbuf *m)
{
	struct proc *p = curproc;
	struct tcp_info *ti;
	u_int t = 1000;		/* msec => usec */
	uint64_t now;

	if (sizeof(*ti) > MLEN) {
		MCLGETL(m, M_WAITOK, sizeof(*ti));
		if (!ISSET(m->m_flags, M_EXT))
			return ENOMEM;
	}
	ti = mtod(m, struct tcp_info *);
	m->m_len = sizeof(*ti);
	memset(ti, 0, sizeof(*ti));
	now = tcp_now();

	ti->tcpi_state = tp->t_state;
	if ((tp->t_flags & TF_REQ_TSTMP) && (tp->t_flags & TF_RCVD_TSTMP))
		ti->tcpi_options |= TCPI_OPT_TIMESTAMPS;
	if (tp->t_flags & TF_SACK_PERMIT)
		ti->tcpi_options |= TCPI_OPT_SACK;
	if ((tp->t_flags & TF_REQ_SCALE) && (tp->t_flags & TF_RCVD_SCALE)) {
		ti->tcpi_options |= TCPI_OPT_WSCALE;
		ti->tcpi_snd_wscale = tp->snd_scale;
		ti->tcpi_rcv_wscale = tp->rcv_scale;
	}
#ifdef TCP_ECN
	if (tp->t_flags & TF_ECN_PERMIT)
		ti->tcpi_options |= TCPI_OPT_ECN;
#endif

	ti->tcpi_rto = tp->t_rxtcur * t;
	ti->tcpi_snd_mss = tp->t_maxseg;
	ti->tcpi_rcv_mss = tp->t_peermss;

	ti->tcpi_last_data_sent = (now - tp->t_sndtime) * t;
	ti->tcpi_last_ack_sent = (now - tp->t_sndacktime) * t;
	ti->tcpi_last_data_recv = (now - tp->t_rcvtime) * t;
	ti->tcpi_last_ack_recv = (now - tp->t_rcvacktime) * t;

	ti->tcpi_rtt = ((uint64_t)tp->t_srtt * t) >>
	    (TCP_RTT_SHIFT + TCP_RTT_BASE_SHIFT);
	ti->tcpi_rttvar = ((uint64_t)tp->t_rttvar * t) >>
	    (TCP_RTTVAR_SHIFT + TCP_RTT_BASE_SHIFT);
	ti->tcpi_snd_ssthresh = tp->snd_ssthresh;
	ti->tcpi_snd_cwnd = tp->snd_cwnd;

	ti->tcpi_rcv_space = tp->rcv_wnd;

	/*
	 * Provide only minimal information for unprivileged processes.
	 */
	if (suser(p) != 0)
		return 0;

	/* FreeBSD-specific extension fields for tcp_info.  */
	ti->tcpi_snd_wnd = tp->snd_wnd;
	ti->tcpi_snd_nxt = tp->snd_nxt - tp->iss;
	ti->tcpi_rcv_nxt = tp->rcv_nxt - tp->irs;
	/* missing tcpi_toe_tid */
	ti->tcpi_snd_rexmitpack = tp->t_sndrexmitpack;
	ti->tcpi_rcv_ooopack = tp->t_rcvoopack;
	ti->tcpi_snd_zerowin = tp->t_sndzerowin;

	/* OpenBSD extensions */
	ti->tcpi_rttmin = tp->t_rttmin * t;
	ti->tcpi_max_sndwnd = tp->max_sndwnd;
	ti->tcpi_rcv_adv = tp->rcv_adv - tp->irs;
	ti->tcpi_rcv_up = tp->rcv_up - tp->irs;
	ti->tcpi_snd_una = tp->snd_una - tp->iss;
	ti->tcpi_snd_up = tp->snd_up - tp->iss;
	ti->tcpi_snd_wl1 = tp->snd_wl1 - tp->iss;
	ti->tcpi_snd_wl2 = tp->snd_wl2 - tp->iss;
	ti->tcpi_snd_max = tp->snd_max - tp->iss;

	ti->tcpi_ts_recent = tp->ts_recent; /* XXX value from the wire */
	ti->tcpi_ts_recent_age = (now - tp->ts_recent_age) * t;
	ti->tcpi_rfbuf_cnt = tp->rfbuf_cnt;
	ti->tcpi_rfbuf_ts = (now - tp->rfbuf_ts) * t;

	mtx_enter(&so->so_rcv.sb_mtx);
	ti->tcpi_so_rcv_sb_cc = so->so_rcv.sb_cc;
	ti->tcpi_so_rcv_sb_hiwat = so->so_rcv.sb_hiwat;
	ti->tcpi_so_rcv_sb_lowat = so->so_rcv.sb_lowat;
	ti->tcpi_so_rcv_sb_wat = so->so_rcv.sb_wat;
	mtx_leave(&so->so_rcv.sb_mtx);
	mtx_enter(&so->so_snd.sb_mtx);
	ti->tcpi_so_snd_sb_cc = so->so_snd.sb_cc;
	ti->tcpi_so_snd_sb_hiwat = so->so_snd.sb_hiwat;
	ti->tcpi_so_snd_sb_lowat = so->so_snd.sb_lowat;
	ti->tcpi_so_snd_sb_wat = so->so_snd.sb_wat;
	mtx_leave(&so->so_snd.sb_mtx);

	return 0;
}

int
tcp_ctloutput(int op, struct socket *so, int level, int optname,
    struct mbuf *m)
{
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;
	int i;

	inp = sotoinpcb(so);
	if (inp == NULL)
		return (ECONNRESET);
	if (level != IPPROTO_TCP) {
#ifdef INET6
		if (ISSET(inp->inp_flags, INP_IPV6))
			error = ip6_ctloutput(op, so, level, optname, m);
		else
#endif
			error = ip_ctloutput(op, so, level, optname, m);
		return (error);
	}
	tp = intotcpcb(inp);

	switch (op) {

	case PRCO_SETOPT:
		switch (optname) {

		case TCP_NODELAY:
			if (m == NULL || m->m_len < sizeof (int))
				error = EINVAL;
			else if (*mtod(m, int *))
				tp->t_flags |= TF_NODELAY;
			else
				tp->t_flags &= ~TF_NODELAY;
			break;

		case TCP_NOPUSH:
			if (m == NULL || m->m_len < sizeof (int))
				error = EINVAL;
			else if (*mtod(m, int *))
				tp->t_flags |= TF_NOPUSH;
			else if (tp->t_flags & TF_NOPUSH) {
				tp->t_flags &= ~TF_NOPUSH;
				if (TCPS_HAVEESTABLISHED(tp->t_state))
					error = tcp_output(tp);
			}
			break;

		case TCP_MAXSEG:
			if (m == NULL || m->m_len < sizeof (int)) {
				error = EINVAL;
				break;
			}

			i = *mtod(m, int *);
			if (i > 0 && i <= tp->t_maxseg)
				tp->t_maxseg = i;
			else
				error = EINVAL;
			break;

		case TCP_SACK_ENABLE:
			if (m == NULL || m->m_len < sizeof (int)) {
				error = EINVAL;
				break;
			}

			if (TCPS_HAVEESTABLISHED(tp->t_state)) {
				error = EPERM;
				break;
			}

			if (tp->t_flags & TF_SIGNATURE) {
				error = EPERM;
				break;
			}

			if (*mtod(m, int *))
				tp->sack_enable = 1;
			else
				tp->sack_enable = 0;
			break;
#ifdef TCP_SIGNATURE
		case TCP_MD5SIG:
			if (m == NULL || m->m_len < sizeof (int)) {
				error = EINVAL;
				break;
			}

			if (TCPS_HAVEESTABLISHED(tp->t_state)) {
				error = EPERM;
				break;
			}

			if (*mtod(m, int *)) {
				tp->t_flags |= TF_SIGNATURE;
				tp->sack_enable = 0;
			} else
				tp->t_flags &= ~TF_SIGNATURE;
			break;
#endif /* TCP_SIGNATURE */
		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	case PRCO_GETOPT:
		switch (optname) {
		case TCP_NODELAY:
			m->m_len = sizeof(int);
			*mtod(m, int *) = tp->t_flags & TF_NODELAY;
			break;
		case TCP_NOPUSH:
			m->m_len = sizeof(int);
			*mtod(m, int *) = tp->t_flags & TF_NOPUSH;
			break;
		case TCP_MAXSEG:
			m->m_len = sizeof(int);
			*mtod(m, int *) = tp->t_maxseg;
			break;
		case TCP_SACK_ENABLE:
			m->m_len = sizeof(int);
			*mtod(m, int *) = tp->sack_enable;
			break;
		case TCP_INFO:
			error = tcp_fill_info(tp, so, m);
			break;
#ifdef TCP_SIGNATURE
		case TCP_MD5SIG:
			m->m_len = sizeof(int);
			*mtod(m, int *) = tp->t_flags & TF_SIGNATURE;
			break;
#endif
		default:
			error = ENOPROTOOPT;
			break;
		}
		break;
	}
	return (error);
}

/*
 * Attach TCP protocol to socket, allocating
 * internet protocol control block, tcp control block,
 * buffer space, and entering LISTEN state to accept connections.
 */
int
tcp_attach(struct socket *so, int proto, int wait)
{
	struct inpcbtable *table;
	struct tcpcb *tp;
	struct inpcb *inp;
	int error;

	if (so->so_pcb)
		return EISCONN;
	if (so->so_snd.sb_hiwat == 0 || so->so_rcv.sb_hiwat == 0 ||
	    sbcheckreserve(so->so_snd.sb_wat, tcp_sendspace) ||
	    sbcheckreserve(so->so_rcv.sb_wat, tcp_recvspace)) {
		error = soreserve(so, tcp_sendspace, tcp_recvspace);
		if (error)
			return (error);
	}

#ifdef INET6
	if (so->so_proto->pr_domain->dom_family == PF_INET6)
		table = &tcb6table;
	else
#endif
		table = &tcbtable;
	error = in_pcballoc(so, table, wait);
	if (error)
		return (error);
	inp = sotoinpcb(so);
	tp = tcp_newtcpcb(inp, wait);
	if (tp == NULL) {
		unsigned int nofd = so->so_state & SS_NOFDREF;	/* XXX */

		so->so_state &= ~SS_NOFDREF;	/* don't free the socket yet */
		in_pcbdetach(inp);
		so->so_state |= nofd;
		return (ENOBUFS);
	}
	tp->t_state = TCPS_CLOSED;
#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6))
		tp->pf = PF_INET6;
	else
#endif
		tp->pf = PF_INET;
	if ((so->so_options & SO_LINGER) && so->so_linger == 0)
		so->so_linger = TCP_LINGERTIME;

	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_USER, TCPS_CLOSED, tp, tp, NULL, PRU_ATTACH, 0);
	return (0);
}

int
tcp_detach(struct socket *so)
{
	struct inpcb *inp;
	struct tcpcb *otp = NULL, *tp;
	int error;
	short ostate;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}

	/*
	 * Detach the TCP protocol from the socket.
	 * If the protocol state is non-embryonic, then can't
	 * do this directly: have to initiate a PRU_DISCONNECT,
	 * which may finish later; embryonic TCB's can just
	 * be discarded here.
	 */
	tp = tcp_dodisconnect(tp);

	if (otp)
		tcp_trace(TA_USER, ostate, tp, otp, NULL, PRU_DETACH, 0);
	return (0);
}

/*
 * Give the socket an address.
 */
int
tcp_bind(struct socket *so, struct mbuf *nam, struct proc *p)
{
	struct inpcb *inp;
	struct tcpcb *tp;
	int error;
	short ostate;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	if (so->so_options & SO_DEBUG)
		ostate = tp->t_state;

	error = in_pcbbind(inp, nam, p);

	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_USER, ostate, tp, tp, NULL, PRU_BIND, 0);
	return (error);
}

/*
 * Prepare to accept connections.
 */
int
tcp_listen(struct socket *so)
{
	struct inpcb *inp;
	struct tcpcb *tp, *otp = NULL;
	int error;
	short ostate;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}

	if (inp->inp_lport == 0)
		if ((error = in_pcbbind(inp, NULL, curproc)))
			goto out;

	/*
	 * If the in_pcbbind() above is called, the tp->pf
	 * should still be whatever it was before.
	 */
	tp->t_state = TCPS_LISTEN;

out:
	if (otp)
		tcp_trace(TA_USER, ostate, tp, otp, NULL, PRU_LISTEN, 0);
	return (error);
}

/*
 * Initiate connection to peer.
 * Create a template for use in transmissions on this connection.
 * Enter SYN_SENT state, and mark socket as connecting.
 * Start keep-alive timer, and seed output sequence space.
 * Send initial segment on connection.
 */
int
tcp_connect(struct socket *so, struct mbuf *nam)
{
	struct inpcb *inp;
	struct tcpcb *tp, *otp = NULL;
	int error;
	short ostate;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}

#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6)) {
		struct sockaddr_in6 *sin6;

		if ((error = in6_nam2sin6(nam, &sin6)))
			goto out;
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr)) {
			error = EINVAL;
			goto out;
		}
	} else
#endif
	{
		struct sockaddr_in *sin;

		if ((error = in_nam2sin(nam, &sin)))
			goto out;
		if ((sin->sin_addr.s_addr == INADDR_ANY) ||
		    (sin->sin_addr.s_addr == INADDR_BROADCAST) ||
		    IN_MULTICAST(sin->sin_addr.s_addr) ||
		    in_broadcast(sin->sin_addr, inp->inp_rtableid)) {
			error = EINVAL;
			goto out;
		}
	}
	error = in_pcbconnect(inp, nam);
	if (error)
		goto out;

	tp->t_template = tcp_template(tp);
	if (tp->t_template == 0) {
		in_pcbunset_faddr(inp);
		in_pcbdisconnect(inp);
		error = ENOBUFS;
		goto out;
	}

	so->so_state |= SS_CONNECTOUT;

	/* Compute window scaling to request.  */
	tcp_rscale(tp, sb_max);

	soisconnecting(so);
	tcpstat_inc(tcps_connattempt);
	tp->t_state = TCPS_SYN_SENT;
	TCP_TIMER_ARM(tp, TCPT_KEEP, atomic_load_int(&tcp_keepinit));
	tcp_set_iss_tsm(tp);
	tcp_sendseqinit(tp);
	tp->snd_last = tp->snd_una;
	error = tcp_output(tp);

out:
	if (otp)
		tcp_trace(TA_USER, ostate, tp, otp, NULL, PRU_CONNECT, 0);
	return (error);
}

/*
 * Accept a connection.  Essentially all the work is done at higher
 * levels; just return the address of the peer, storing through addr.
 */
int
tcp_accept(struct socket *so, struct mbuf *nam)
{
	struct inpcb *inp;
	struct tcpcb *tp;
	int error;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	in_setpeeraddr(inp, nam);

	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_USER, tp->t_state, tp, tp, NULL, PRU_ACCEPT, 0);
	return (0);
}

/*
 * Initiate disconnect from peer.
 * If connection never passed embryonic stage, just drop;
 * else if don't need to let data drain, then can just drop anyways,
 * else have to begin TCP shutdown process: mark socket disconnecting,
 * drain unread data, state switch to reflect user close, and
 * send segment (e.g. FIN) to peer.  Socket will be really disconnected
 * when peer sends FIN and acks ours.
 *
 * SHOULD IMPLEMENT LATER PRU_CONNECT VIA REALLOC TCPCB.
 */
int
tcp_disconnect(struct socket *so)
{
	struct inpcb *inp;
	struct tcpcb *tp, *otp = NULL;
	int error;
	short ostate;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}

	tp = tcp_dodisconnect(tp);

	if (otp)
		tcp_trace(TA_USER, ostate, tp, otp, NULL, PRU_DISCONNECT, 0);
	return (0);
}

/*
 * Mark the connection as being incapable of further output.
 */
int
tcp_shutdown(struct socket *so)
{
	struct inpcb *inp;
	struct tcpcb *tp, *otp = NULL;
	int error;
	short ostate;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}

	if (so->so_snd.sb_state & SS_CANTSENDMORE)
		goto out;

	socantsendmore(so);
	tp = tcp_usrclosed(tp);
	if (tp)
		error = tcp_output(tp);

out:
	if (otp)
		tcp_trace(TA_USER, ostate, tp, otp, NULL, PRU_SHUTDOWN, 0);
	return (error);
}

/*
 * After a receive, possibly send window update to peer.
 */
void
tcp_rcvd(struct socket *so)
{
	struct inpcb *inp;
	struct tcpcb *tp;
	short ostate;

	soassertlocked(so);

	if (tcp_sogetpcb(so, &inp, &tp))
		return;

	if (so->so_options & SO_DEBUG)
		ostate = tp->t_state;

	/*
	 * soreceive() calls this function when a user receives
	 * ancillary data on a listening socket. We don't call
	 * tcp_output in such a case, since there is no header
	 * template for a listening socket and hence the kernel
	 * will panic.
	 */
	if ((so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING)) != 0)
		(void) tcp_output(tp);

	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_USER, ostate, tp, tp, NULL, PRU_RCVD, 0);
}

/*
 * Do a send by putting data in output queue and updating urgent
 * marker if URG set.  Possibly send more data.
 */
int
tcp_send(struct socket *so, struct mbuf *m, struct mbuf *nam,
    struct mbuf *control)
{
	struct inpcb *inp;
	struct tcpcb *tp;
	int error;
	short ostate;

	soassertlocked(so);

	if (control && control->m_len) {
		error = EINVAL;
		goto out;
	}

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		goto out;

	if (so->so_options & SO_DEBUG)
		ostate = tp->t_state;

	mtx_enter(&so->so_snd.sb_mtx);
	sbappendstream(&so->so_snd, m);
	mtx_leave(&so->so_snd.sb_mtx);
	m = NULL;

	error = tcp_output(tp);

	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_USER, ostate, tp, tp, NULL, PRU_SEND, 0);

out:
	m_freem(control);
	m_freem(m);

	return (error);
}

/*
 * Abort the TCP.
 */
void
tcp_abort(struct socket *so)
{
	struct inpcb *inp;
	struct tcpcb *tp, *otp = NULL;
	short ostate;

	soassertlocked(so);

	if (tcp_sogetpcb(so, &inp, &tp))
		return;

	if (so->so_options & SO_DEBUG) {
		otp = tp;
		ostate = tp->t_state;
	}

	tp = tcp_drop(tp, ECONNABORTED);

	if (otp)
		tcp_trace(TA_USER, ostate, tp, otp, NULL, PRU_ABORT, 0);
}

int
tcp_sense(struct socket *so, struct stat *ub)
{
	struct inpcb *inp;
	struct tcpcb *tp;
	int error;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	mtx_enter(&so->so_snd.sb_mtx);
	ub->st_blksize = so->so_snd.sb_hiwat;
	mtx_leave(&so->so_snd.sb_mtx);

	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_USER, tp->t_state, tp, tp, NULL, PRU_SENSE, 0);
	return (0);
}

int
tcp_rcvoob(struct socket *so, struct mbuf *m, int flags)
{
	struct inpcb *inp;
	struct tcpcb *tp;
	int error;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	if ((so->so_oobmark == 0 &&
	    (so->so_rcv.sb_state & SS_RCVATMARK) == 0) ||
	    so->so_options & SO_OOBINLINE ||
	    tp->t_oobflags & TCPOOB_HADDATA) {
		error = EINVAL;
		goto out;
	}
	if ((tp->t_oobflags & TCPOOB_HAVEDATA) == 0) {
		error = EWOULDBLOCK;
		goto out;
	}
	m->m_len = 1;
	*mtod(m, caddr_t) = tp->t_iobc;
	if ((flags & MSG_PEEK) == 0)
		tp->t_oobflags ^= (TCPOOB_HAVEDATA | TCPOOB_HADDATA);
out:
	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_USER, tp->t_state, tp, tp, NULL, PRU_RCVOOB, 0);
	return (error);
}

int
tcp_sendoob(struct socket *so, struct mbuf *m, struct mbuf *nam,
    struct mbuf *control)
{
	struct inpcb *inp;
	struct tcpcb *tp;
	int error;
	short ostate;

	soassertlocked(so);

	if (control && control->m_len) {
		error = EINVAL;
		goto release;
	}

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		goto release;

	if (so->so_options & SO_DEBUG)
		ostate = tp->t_state;

	if (sbspace(&so->so_snd) < -512) {
		error = ENOBUFS;
		goto out;
	}

	/*
	 * According to RFC961 (Assigned Protocols),
	 * the urgent pointer points to the last octet
	 * of urgent data.  We continue, however,
	 * to consider it to indicate the first octet
	 * of data past the urgent section.
	 * Otherwise, snd_up should be one lower.
	 */
	mtx_enter(&so->so_snd.sb_mtx);
	sbappendstream(&so->so_snd, m);
	mtx_leave(&so->so_snd.sb_mtx);
	m = NULL;
	tp->snd_up = tp->snd_una + so->so_snd.sb_cc;
	tp->t_force = 1;
	error = tcp_output(tp);
	tp->t_force = 0;

out:
	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_USER, ostate, tp, tp, NULL, PRU_SENDOOB, 0);

release:
	m_freem(control);
	m_freem(m);

	return (error);
}

int
tcp_sockaddr(struct socket *so, struct mbuf *nam)
{
	struct inpcb *inp;
	struct tcpcb *tp;
	int error;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	in_setsockaddr(inp, nam);

	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_USER, tp->t_state, tp, tp, NULL,
		    PRU_SOCKADDR, 0);
	return (0);
}

int
tcp_peeraddr(struct socket *so, struct mbuf *nam)
{
	struct inpcb *inp;
	struct tcpcb *tp;
	int error;

	soassertlocked(so);

	if ((error = tcp_sogetpcb(so, &inp, &tp)))
		return (error);

	in_setpeeraddr(inp, nam);

	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_USER, tp->t_state, tp, tp, NULL, PRU_PEERADDR, 0);
	return (0);
}

/*
 * Initiate (or continue) disconnect.
 * If embryonic state, just send reset (once).
 * If in ``let data drain'' option and linger null, just drop.
 * Otherwise (hard), mark socket disconnecting and drop
 * current input data; switch states based on user close, and
 * send segment to peer (with FIN).
 */
struct tcpcb *
tcp_dodisconnect(struct tcpcb *tp)
{
	struct socket *so = tp->t_inpcb->inp_socket;

	if (TCPS_HAVEESTABLISHED(tp->t_state) == 0)
		tp = tcp_close(tp);
	else if ((so->so_options & SO_LINGER) && so->so_linger == 0)
		tp = tcp_drop(tp, 0);
	else {
		soisdisconnecting(so);
		mtx_enter(&so->so_rcv.sb_mtx);
		sbflush(&so->so_rcv);
		mtx_leave(&so->so_rcv.sb_mtx);
		tp = tcp_usrclosed(tp);
		if (tp)
			(void) tcp_output(tp);
	}
	return (tp);
}

/*
 * User issued close, and wish to trail through shutdown states:
 * if never received SYN, just forget it.  If got a SYN from peer,
 * but haven't sent FIN, then go to FIN_WAIT_1 state to send peer a FIN.
 * If already got a FIN from peer, then almost done; go to LAST_ACK
 * state.  In all other cases, have already sent FIN to peer (e.g.
 * after PRU_SHUTDOWN), and just have to play tedious game waiting
 * for peer to send FIN or not respond to keep-alives, etc.
 * We can let the user exit from the close as soon as the FIN is acked.
 */
struct tcpcb *
tcp_usrclosed(struct tcpcb *tp)
{

	switch (tp->t_state) {

	case TCPS_CLOSED:
	case TCPS_LISTEN:
	case TCPS_SYN_SENT:
		tp->t_state = TCPS_CLOSED;
		tp = tcp_close(tp);
		break;

	case TCPS_SYN_RECEIVED:
	case TCPS_ESTABLISHED:
		tp->t_state = TCPS_FIN_WAIT_1;
		break;

	case TCPS_CLOSE_WAIT:
		tp->t_state = TCPS_LAST_ACK;
		break;
	}
	if (tp && tp->t_state >= TCPS_FIN_WAIT_2) {
		soisdisconnected(tp->t_inpcb->inp_socket);
		/*
		 * If we are in FIN_WAIT_2, we arrived here because the
		 * application did a shutdown of the send side.  Like the
		 * case of a transition from FIN_WAIT_1 to FIN_WAIT_2 after
		 * a full close, we start a timer to make sure sockets are
		 * not left in FIN_WAIT_2 forever.
		 */
		if (tp->t_state == TCPS_FIN_WAIT_2) {
			int maxidle;

			maxidle = TCPTV_KEEPCNT *
			    atomic_load_int(&tcp_keepidle);
			TCP_TIMER_ARM(tp, TCPT_2MSL, maxidle);
		}
	}
	return (tp);
}

/*
 * Look up a socket for ident or tcpdrop, ...
 */
int
tcp_ident(void *oldp, size_t *oldlenp, void *newp, size_t newlen, int dodrop)
{
	int error = 0;
	struct tcp_ident_mapping tir;
	struct inpcb *inp;
	struct socket *so = NULL;
	struct sockaddr_in *fin, *lin;
#ifdef INET6
	struct sockaddr_in6 *fin6, *lin6;
	struct in6_addr f6, l6;
#endif

	if (dodrop) {
		if (oldp != NULL || *oldlenp != 0)
			return (EINVAL);
		if (newp == NULL)
			return (EPERM);
		if (newlen < sizeof(tir))
			return (ENOMEM);
		if ((error = copyin(newp, &tir, sizeof (tir))) != 0 )
			return (error);
	} else {
		if (oldp == NULL)
			return (EINVAL);
		if (*oldlenp < sizeof(tir))
			return (ENOMEM);
		if (newp != NULL || newlen != 0)
			return (EINVAL);
		if ((error = copyin(oldp, &tir, sizeof (tir))) != 0 )
			return (error);
	}

	NET_LOCK_SHARED();

	switch (tir.faddr.ss_family) {
#ifdef INET6
	case AF_INET6:
		if (tir.laddr.ss_family != AF_INET6) {
			NET_UNLOCK_SHARED();
			return (EAFNOSUPPORT);
		}
		fin6 = (struct sockaddr_in6 *)&tir.faddr;
		error = in6_embedscope(&f6, fin6, NULL, NULL);
		if (error) {
			NET_UNLOCK_SHARED();
			return EINVAL;	/*?*/
		}
		lin6 = (struct sockaddr_in6 *)&tir.laddr;
		error = in6_embedscope(&l6, lin6, NULL, NULL);
		if (error) {
			NET_UNLOCK_SHARED();
			return EINVAL;	/*?*/
		}
		break;
#endif
	case AF_INET:
		if (tir.laddr.ss_family != AF_INET) {
			NET_UNLOCK_SHARED();
			return (EAFNOSUPPORT);
		}
		fin = (struct sockaddr_in *)&tir.faddr;
		lin = (struct sockaddr_in *)&tir.laddr;
		break;
	default:
		NET_UNLOCK_SHARED();
		return (EAFNOSUPPORT);
	}

	switch (tir.faddr.ss_family) {
#ifdef INET6
	case AF_INET6:
		inp = in6_pcblookup(&tcb6table, &f6,
		    fin6->sin6_port, &l6, lin6->sin6_port, tir.rdomain);
		break;
#endif
	case AF_INET:
		inp = in_pcblookup(&tcbtable, fin->sin_addr,
		    fin->sin_port, lin->sin_addr, lin->sin_port, tir.rdomain);
		break;
	default:
		unhandled_af(tir.faddr.ss_family);
	}

	if (dodrop) {
		struct tcpcb *tp = NULL;

		if (inp != NULL) {
			so = in_pcbsolock(inp);
			if (so != NULL)
				tp = intotcpcb(inp);
		}
		if (tp != NULL && !ISSET(so->so_options, SO_ACCEPTCONN))
			tp = tcp_drop(tp, ECONNABORTED);
		else
			error = ESRCH;

		in_pcbsounlock(inp, so);
		NET_UNLOCK_SHARED();
		in_pcbunref(inp);
		return (error);
	}

	if (inp == NULL) {
		tcpstat_inc(tcps_pcbhashmiss);
		switch (tir.faddr.ss_family) {
#ifdef INET6
		case AF_INET6:
			inp = in6_pcblookup_listen(&tcb6table,
			    &l6, lin6->sin6_port, NULL, tir.rdomain);
			break;
#endif
		case AF_INET:
			inp = in_pcblookup_listen(&tcbtable,
			    lin->sin_addr, lin->sin_port, NULL, tir.rdomain);
			break;
		}
	}

	if (inp != NULL)
		so = in_pcbsolock(inp);

	if (so != NULL && ISSET(so->so_state, SS_CONNECTOUT)) {
		tir.ruid = so->so_ruid;
		tir.euid = so->so_euid;
	} else {
		tir.ruid = -1;
		tir.euid = -1;
	}

	in_pcbsounlock(inp, so);
	NET_UNLOCK_SHARED();
	in_pcbunref(inp);

	*oldlenp = sizeof(tir);
	return copyout(&tir, oldp, sizeof(tir));
}

#ifndef SMALL_KERNEL
int
tcp_sysctl_tcpstat(void *oldp, size_t *oldlenp, void *newp)
{
	uint64_t counters[tcps_ncounters];
	struct tcpstat tcpstat;
	struct syn_cache_set *set;
	int i = 0;

#define ASSIGN(field)	do { tcpstat.field = counters[i++]; } while (0)

	memset(&tcpstat, 0, sizeof tcpstat);
	counters_read(tcpcounters, counters, nitems(counters), NULL);
	ASSIGN(tcps_connattempt);
	ASSIGN(tcps_accepts);
	ASSIGN(tcps_connects);
	ASSIGN(tcps_drops);
	ASSIGN(tcps_conndrops);
	ASSIGN(tcps_closed);
	ASSIGN(tcps_segstimed);
	ASSIGN(tcps_rttupdated);
	ASSIGN(tcps_delack);
	ASSIGN(tcps_timeoutdrop);
	ASSIGN(tcps_rexmttimeo);
	ASSIGN(tcps_persisttimeo);
	ASSIGN(tcps_persistdrop);
	ASSIGN(tcps_keeptimeo);
	ASSIGN(tcps_keepprobe);
	ASSIGN(tcps_keepdrops);
	ASSIGN(tcps_sndtotal);
	ASSIGN(tcps_sndpack);
	ASSIGN(tcps_sndbyte);
	ASSIGN(tcps_sndrexmitpack);
	ASSIGN(tcps_sndrexmitbyte);
	ASSIGN(tcps_sndrexmitfast);
	ASSIGN(tcps_sndacks);
	ASSIGN(tcps_sndprobe);
	ASSIGN(tcps_sndurg);
	ASSIGN(tcps_sndwinup);
	ASSIGN(tcps_sndctrl);
	ASSIGN(tcps_rcvtotal);
	ASSIGN(tcps_rcvpack);
	ASSIGN(tcps_rcvbyte);
	ASSIGN(tcps_rcvbadsum);
	ASSIGN(tcps_rcvbadoff);
	ASSIGN(tcps_rcvmemdrop);
	ASSIGN(tcps_rcvnosec);
	ASSIGN(tcps_rcvshort);
	ASSIGN(tcps_rcvduppack);
	ASSIGN(tcps_rcvdupbyte);
	ASSIGN(tcps_rcvpartduppack);
	ASSIGN(tcps_rcvpartdupbyte);
	ASSIGN(tcps_rcvoopack);
	ASSIGN(tcps_rcvoobyte);
	ASSIGN(tcps_rcvpackafterwin);
	ASSIGN(tcps_rcvbyteafterwin);
	ASSIGN(tcps_rcvafterclose);
	ASSIGN(tcps_rcvwinprobe);
	ASSIGN(tcps_rcvdupack);
	ASSIGN(tcps_rcvacktoomuch);
	ASSIGN(tcps_rcvacktooold);
	ASSIGN(tcps_rcvackpack);
	ASSIGN(tcps_rcvackbyte);
	ASSIGN(tcps_rcvwinupd);
	ASSIGN(tcps_pawsdrop);
	ASSIGN(tcps_predack);
	ASSIGN(tcps_preddat);
	ASSIGN(tcps_pcbhashmiss);
	ASSIGN(tcps_noport);
	ASSIGN(tcps_closing);
	ASSIGN(tcps_badsyn);
	ASSIGN(tcps_dropsyn);
	ASSIGN(tcps_rcvbadsig);
	ASSIGN(tcps_rcvgoodsig);
	ASSIGN(tcps_inswcsum);
	ASSIGN(tcps_outswcsum);
	ASSIGN(tcps_ecn_accepts);
	ASSIGN(tcps_ecn_rcvece);
	ASSIGN(tcps_ecn_rcvcwr);
	ASSIGN(tcps_ecn_rcvce);
	ASSIGN(tcps_ecn_sndect);
	ASSIGN(tcps_ecn_sndece);
	ASSIGN(tcps_ecn_sndcwr);
	ASSIGN(tcps_cwr_ecn);
	ASSIGN(tcps_cwr_frecovery);
	ASSIGN(tcps_cwr_timeout);
	ASSIGN(tcps_sc_added);
	ASSIGN(tcps_sc_completed);
	ASSIGN(tcps_sc_timed_out);
	ASSIGN(tcps_sc_overflowed);
	ASSIGN(tcps_sc_reset);
	ASSIGN(tcps_sc_unreach);
	ASSIGN(tcps_sc_bucketoverflow);
	ASSIGN(tcps_sc_aborted);
	ASSIGN(tcps_sc_dupesyn);
	ASSIGN(tcps_sc_dropped);
	ASSIGN(tcps_sc_collisions);
	ASSIGN(tcps_sc_retransmitted);
	ASSIGN(tcps_sc_seedrandom);
	ASSIGN(tcps_sc_hash_size);
	ASSIGN(tcps_sc_entry_count);
	ASSIGN(tcps_sc_entry_limit);
	ASSIGN(tcps_sc_bucket_maxlen);
	ASSIGN(tcps_sc_bucket_limit);
	ASSIGN(tcps_sc_uses_left);
	ASSIGN(tcps_conndrained);
	ASSIGN(tcps_sack_recovery_episode);
	ASSIGN(tcps_sack_rexmits);
	ASSIGN(tcps_sack_rexmit_bytes);
	ASSIGN(tcps_sack_rcv_opts);
	ASSIGN(tcps_sack_snd_opts);
	ASSIGN(tcps_sack_drop_opts);
	ASSIGN(tcps_outswtso);
	ASSIGN(tcps_outhwtso);
	ASSIGN(tcps_outpkttso);
	ASSIGN(tcps_outbadtso);
	ASSIGN(tcps_inswlro);
	ASSIGN(tcps_inhwlro);
	ASSIGN(tcps_inpktlro);
	ASSIGN(tcps_inbadlro);

#undef ASSIGN

	mtx_enter(&syn_cache_mtx);
	set = &tcp_syn_cache[tcp_syn_cache_active];
	tcpstat.tcps_sc_hash_size = set->scs_size;
	tcpstat.tcps_sc_entry_count = set->scs_count;
	tcpstat.tcps_sc_entry_limit = atomic_load_int(&tcp_syn_cache_limit);
	tcpstat.tcps_sc_bucket_maxlen = 0;
	for (i = 0; i < set->scs_size; i++) {
		if (tcpstat.tcps_sc_bucket_maxlen <
		    set->scs_buckethead[i].sch_length)
			tcpstat.tcps_sc_bucket_maxlen =
				set->scs_buckethead[i].sch_length;
	}
	tcpstat.tcps_sc_bucket_limit = atomic_load_int(&tcp_syn_bucket_limit);
	tcpstat.tcps_sc_uses_left = set->scs_use;
	mtx_leave(&syn_cache_mtx);

	return (sysctl_rdstruct(oldp, oldlenp, newp,
	    &tcpstat, sizeof(tcpstat)));
}

/*
 * Sysctl for tcp variables.
 */
int
tcp_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	int error, oval, nval;

	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case TCPCTL_ROOTONLY:
		if (newp && (int)atomic_load_int(&securelevel) > 0)
			return (EPERM);
		/* FALLTHROUGH */
	case TCPCTL_BADDYNAMIC: {
		struct baddynamicports *ports = (name[0] == TCPCTL_ROOTONLY ?
		    &rootonlyports : &baddynamicports);
		const size_t bufitems = DP_MAPSIZE;
		const size_t buflen = bufitems * sizeof(uint32_t);
		size_t i;
		uint32_t *buf;
		int error;

		buf = malloc(buflen, M_SYSCTL, M_WAITOK | M_ZERO);

		NET_LOCK_SHARED();
		for (i = 0; i < bufitems; ++i)
			buf[i] = ports->tcp[i];
		NET_UNLOCK_SHARED();

		error = sysctl_struct(oldp, oldlenp, newp, newlen,
		    buf, buflen);

		if (error == 0 && newp) {
			NET_LOCK();
			for (i = 0; i < bufitems; ++i)
				ports->tcp[i] = buf[i];
			NET_UNLOCK();
		}

		free(buf, M_SYSCTL, buflen);

		return (error);
	}
	case TCPCTL_IDENT:
		return tcp_ident(oldp, oldlenp, newp, newlen, 0);

	case TCPCTL_DROP:
		return tcp_ident(oldp, oldlenp, newp, newlen, 1);

	case TCPCTL_REASS_LIMIT:
	case TCPCTL_SACKHOLE_LIMIT: {
		struct pool *pool;
		int *var;

		if (name[0] == TCPCTL_REASS_LIMIT) {
			pool = &tcpqe_pool;
			var = &tcp_reass_limit;
		} else {
			pool = &sackhl_pool;
			var = &tcp_sackhole_limit;
		}

		oval = nval = atomic_load_int(var);
		error = sysctl_int(oldp, oldlenp, newp, newlen, &nval);

		if (error == 0 && oval != nval) {
			extern struct rwlock sysctl_lock;

			error = rw_enter(&sysctl_lock, RW_WRITE | RW_INTR);
			if (error)
				return (error);
			if (nval != atomic_load_int(var)) {
				error = pool_sethardlimit(pool, nval);
				if (error == 0)
					atomic_store_int(var, nval);
			}
			rw_exit(&sysctl_lock);
		}

		return (error);
	}
	case TCPCTL_STATS:
		return (tcp_sysctl_tcpstat(oldp, oldlenp, newp));

	case TCPCTL_SYN_USE_LIMIT:
		oval = nval = atomic_load_int(&tcp_syn_use_limit);
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &nval, 0, INT_MAX);
		if (!error && oval != nval) {
			/*
			 * Global tcp_syn_use_limit is used when reseeding a
			 * new cache.  Also update the value in active cache.
			 */
			mtx_enter(&syn_cache_mtx);
			if (tcp_syn_cache[0].scs_use > nval)
				tcp_syn_cache[0].scs_use = nval;
			if (tcp_syn_cache[1].scs_use > nval)
				tcp_syn_cache[1].scs_use = nval;
			tcp_syn_use_limit = nval;
			mtx_leave(&syn_cache_mtx);
		}
		return (error);

	case TCPCTL_SYN_HASH_SIZE:
		oval = nval = atomic_load_int(&tcp_syn_hash_size);
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &nval, 1, 100000);
		if (!error && oval != nval) {
			/*
			 * If global hash size has been changed,
			 * switch sets as soon as possible.  Then
			 * the actual hash array will be reallocated.
			 */
			mtx_enter(&syn_cache_mtx);
			if (tcp_syn_cache[0].scs_size != nval)
				tcp_syn_cache[0].scs_use = 0;
			if (tcp_syn_cache[1].scs_size != nval)
				tcp_syn_cache[1].scs_use = 0;
			tcp_syn_hash_size = nval;
			mtx_leave(&syn_cache_mtx);
		}
		return (error);

	default:
		error = sysctl_bounded_arr(tcpctl_vars, nitems(tcpctl_vars),
		    name, namelen, oldp, oldlenp, newp, newlen);
		switch (name[0]) {
		case TCPCTL_KEEPINITTIME:
			atomic_store_int(&tcp_keepinit,
			    atomic_load_int(&tcp_keepinit_sec) * TCP_TIME(1));
			break;
		case TCPCTL_KEEPIDLE:
			atomic_store_int(&tcp_keepidle,
			    atomic_load_int(&tcp_keepidle_sec) * TCP_TIME(1));
			break;
		case TCPCTL_KEEPINTVL:
			atomic_store_int(&tcp_keepintvl,
			    atomic_load_int(&tcp_keepintvl_sec) * TCP_TIME(1));
			break;
		}
		return (error);
	}
	/* NOTREACHED */
}
#endif /* SMALL_KERNEL */

/*
 * Scale the send buffer so that inflight data is not accounted against
 * the limit. The buffer will scale with the congestion window, if the
 * the receiver stops acking data the window will shrink and therefore
 * the buffer size will shrink as well.
 * In low memory situation try to shrink the buffer to the initial size
 * disabling the send buffer scaling as long as the situation persists.
 */
void
tcp_update_sndspace(struct tcpcb *tp)
{
	struct socket *so = tp->t_inpcb->inp_socket;
	u_long nmax;

	mtx_enter(&so->so_snd.sb_mtx);

	nmax = so->so_snd.sb_hiwat;

	if (sbchecklowmem()) {
		/* low on memory try to get rid of some */
		if (tcp_sendspace < nmax)
			nmax = tcp_sendspace;
	} else if (so->so_snd.sb_wat != tcp_sendspace) {
		/* user requested buffer size, auto-scaling disabled */
		nmax = so->so_snd.sb_wat;
	} else {
		/* automatic buffer scaling */
		nmax = MIN(sb_max, so->so_snd.sb_wat + tp->snd_max -
		    tp->snd_una);
	}

	/* a writable socket must be preserved because of poll(2) semantics */
	if (sbspace_locked(&so->so_snd) >= so->so_snd.sb_lowat) {
		if (nmax < so->so_snd.sb_cc + so->so_snd.sb_lowat)
			nmax = so->so_snd.sb_cc + so->so_snd.sb_lowat;
		/* keep in sync with sbreserve() calculation */
		if (nmax * 8 < so->so_snd.sb_mbcnt + so->so_snd.sb_lowat)
			nmax = (so->so_snd.sb_mbcnt+so->so_snd.sb_lowat+7) / 8;
	}

	/* round to MSS boundary */
	nmax = roundup(nmax, tp->t_maxseg);

	if (nmax != so->so_snd.sb_hiwat)
		sbreserve(&so->so_snd, nmax);

	mtx_leave(&so->so_snd.sb_mtx);
}

/*
 * Scale the recv buffer by looking at how much data was transferred in
 * one approximated RTT. If more than a big part of the recv buffer was
 * transferred during that time we increase the buffer by a constant.
 * In low memory situation try to shrink the buffer to the initial size.
 */
void
tcp_update_rcvspace(struct tcpcb *tp)
{
	struct socket *so = tp->t_inpcb->inp_socket;
	u_long nmax;

	mtx_enter(&so->so_rcv.sb_mtx);

	nmax = so->so_rcv.sb_hiwat;

	if (sbchecklowmem()) {
		/* low on memory try to get rid of some */
		if (tcp_recvspace < nmax)
			nmax = tcp_recvspace;
	} else if (so->so_rcv.sb_wat != tcp_recvspace) {
		/* user requested buffer size, auto-scaling disabled */
		nmax = so->so_rcv.sb_wat;
	} else {
		/* automatic buffer scaling */
		if (tp->rfbuf_cnt > so->so_rcv.sb_hiwat / 8 * 7)
			nmax = MIN(sb_max, so->so_rcv.sb_hiwat +
			    tcp_autorcvbuf_inc);
	}

	/* a readable socket must be preserved because of poll(2) semantics */
	mtx_enter(&so->so_snd.sb_mtx);
	if (so->so_rcv.sb_cc >= so->so_rcv.sb_lowat &&
	    nmax < so->so_snd.sb_lowat)
		nmax = so->so_snd.sb_lowat;
	mtx_leave(&so->so_snd.sb_mtx);

	if (nmax != so->so_rcv.sb_hiwat) {
		/* round to MSS boundary */
		nmax = roundup(nmax, tp->t_maxseg);
		sbreserve(&so->so_rcv, nmax);
	}

	mtx_leave(&so->so_rcv.sb_mtx);
}

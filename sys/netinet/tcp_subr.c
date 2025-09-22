/*	$OpenBSD: tcp_subr.c,v 1.216 2025/07/18 08:39:14 mvs Exp $	*/
/*	$NetBSD: tcp_subr.c,v 1.22 1996/02/13 23:44:00 christos Exp $	*/

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
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/pool.h>

#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet6/ip6_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#ifdef INET6
#include <netinet6/ip6protosw.h>
#endif /* INET6 */

#include <crypto/md5.h>
#include <crypto/sha2.h>

/*
 * Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	T	tcp_timer_mtx		global tcp timer data structures
 */

struct mutex tcp_timer_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);

/* patchable/settable parameters for tcp */
int	tcp_mssdflt = TCP_MSS;
int	tcp_rttdflt = TCPTV_SRTTDFLT;

/* values controllable via sysctl */
int	tcp_do_rfc1323 = 1;
int	tcp_do_sack = 1;	/* RFC 2018 selective ACKs */
int	tcp_ack_on_push = 0;	/* set to enable immediate ACK-on-PUSH */
#ifdef TCP_ECN
int	tcp_do_ecn = 0;		/* RFC3168 ECN enabled/disabled? */
#endif
int	tcp_do_rfc3390 = 2;	/* Increase TCP's Initial Window to 10*mss */
int	tcp_do_tso = 1;		/* TCP segmentation offload for output */

#ifndef TCB_INITIAL_HASH_SIZE
#define	TCB_INITIAL_HASH_SIZE	128
#endif

int tcp_reass_limit = NMBCLUSTERS / 8; /* hardlimit for tcpqe_pool */
int tcp_sackhole_limit = 32*1024; /* hardlimit for sackhl_pool */

struct pool tcpcb_pool;
struct pool tcpqe_pool;
struct pool sackhl_pool;

struct cpumem *tcpcounters;		/* tcp statistics */

u_char		tcp_secret[16];	/* [I] */
SHA2_CTX	tcp_secret_ctx;	/* [I] */
tcp_seq		tcp_iss;	/* [T] updated by timer and connection */
uint64_t	tcp_starttime;	/* [I] random offset for tcp_now() */

/*
 * Tcp initialization
 */
void
tcp_init(void)
{
	tcp_iss = 1;		/* wrong */
	/* 0 is treated special so add 1, 63 bits to count is enough */
	arc4random_buf(&tcp_starttime, sizeof(tcp_starttime));
	tcp_starttime = 1ULL + (tcp_starttime / 2);
	pool_init(&tcpcb_pool, sizeof(struct tcpcb), 0, IPL_SOFTNET, 0,
	    "tcpcb", NULL);
	pool_init(&tcpqe_pool, sizeof(struct tcpqent), 0, IPL_SOFTNET, 0,
	    "tcpqe", NULL);
	pool_sethardlimit(&tcpqe_pool, tcp_reass_limit);
	pool_init(&sackhl_pool, sizeof(struct sackhole), 0, IPL_SOFTNET, 0,
	    "sackhl", NULL);
	pool_sethardlimit(&sackhl_pool, tcp_sackhole_limit);
	in_pcbinit(&tcbtable, TCB_INITIAL_HASH_SIZE);
#ifdef INET6
	in_pcbinit(&tcb6table, TCB_INITIAL_HASH_SIZE);
#endif
	tcpcounters = counters_alloc(tcps_ncounters);

	arc4random_buf(tcp_secret, sizeof(tcp_secret));
	SHA512Init(&tcp_secret_ctx);
	SHA512Update(&tcp_secret_ctx, tcp_secret, sizeof(tcp_secret));

#ifdef INET6
	/*
	 * Since sizeof(struct ip6_hdr) > sizeof(struct ip), we
	 * do max length checks/computations only on the former.
	 */
	if (max_protohdr < (sizeof(struct ip6_hdr) + sizeof(struct tcphdr)))
		max_protohdr = (sizeof(struct ip6_hdr) + sizeof(struct tcphdr));
	if ((max_linkhdr + sizeof(struct ip6_hdr) + sizeof(struct tcphdr)) >
	    MHLEN)
		panic("tcp_init");

	icmp6_mtudisc_callback_register(tcp6_mtudisc_callback);
#endif /* INET6 */

	/* Initialize the compressed state engine. */
	syn_cache_init();
}

/*
 * Create template to be used to send tcp packets on a connection.
 * Call after host entry created, allocates an mbuf and fills
 * in a skeletal tcp/ip header, minimizing the amount of work
 * necessary when the connection is used.
 *
 * To support IPv6 in addition to IPv4 and considering that the sizes of
 * the IPv4 and IPv6 headers are not the same, we now use a separate pointer
 * for the TCP header.  Also, we made the former tcpiphdr header pointer
 * into just an IP overlay pointer, with casting as appropriate for v6. rja
 */
struct mbuf *
tcp_template(struct tcpcb *tp)
{
	struct inpcb *inp = tp->t_inpcb;
	struct mbuf *m;
	struct tcphdr *th;

	CTASSERT(sizeof(struct ip) + sizeof(struct tcphdr) <= MHLEN);
	CTASSERT(sizeof(struct ip6_hdr) + sizeof(struct tcphdr) <= MHLEN);

	if ((m = tp->t_template) == 0) {
		m = m_get(M_DONTWAIT, MT_HEADER);
		if (m == NULL)
			return (0);

		switch (tp->pf) {
		case 0:	/*default to PF_INET*/
		case AF_INET:
			m->m_len = sizeof(struct ip);
			break;
#ifdef INET6
		case AF_INET6:
			m->m_len = sizeof(struct ip6_hdr);
			break;
#endif /* INET6 */
		}
		m->m_len += sizeof (struct tcphdr);
	}

	switch(tp->pf) {
	case AF_INET:
		{
			struct ipovly *ipovly;

			ipovly = mtod(m, struct ipovly *);

			bzero(ipovly->ih_x1, sizeof ipovly->ih_x1);
			ipovly->ih_pr = IPPROTO_TCP;
			ipovly->ih_len = htons(sizeof (struct tcphdr));
			ipovly->ih_src = inp->inp_laddr;
			ipovly->ih_dst = inp->inp_faddr;

			th = (struct tcphdr *)(mtod(m, caddr_t) +
				sizeof(struct ip));
		}
		break;
#ifdef INET6
	case AF_INET6:
		{
			struct ip6_hdr *ip6;

			ip6 = mtod(m, struct ip6_hdr *);

			ip6->ip6_src = inp->inp_laddr6;
			ip6->ip6_dst = inp->inp_faddr6;
			ip6->ip6_flow = htonl(0x60000000) |
			    (inp->inp_flowinfo & IPV6_FLOWLABEL_MASK);

			ip6->ip6_nxt = IPPROTO_TCP;
			ip6->ip6_plen = htons(sizeof(struct tcphdr)); /*XXX*/
			ip6->ip6_hlim = in6_selecthlim(inp);	/*XXX*/

			th = (struct tcphdr *)(mtod(m, caddr_t) +
				sizeof(struct ip6_hdr));
		}
		break;
#endif /* INET6 */
	}

	th->th_sport = inp->inp_lport;
	th->th_dport = inp->inp_fport;
	th->th_seq = 0;
	th->th_ack = 0;
	th->th_x2  = 0;
	th->th_off = 5;
	th->th_flags = 0;
	th->th_win = 0;
	th->th_urp = 0;
	th->th_sum = 0;
	return (m);
}

/*
 * Send a single message to the TCP at address specified by
 * the given TCP/IP header.  If m == 0, then we make a copy
 * of the tcpiphdr at ti and send directly to the addressed host.
 * This is used to force keep alive messages out using the TCP
 * template for a connection tp->t_template.  If flags are given
 * then we send a message back to the TCP which originated the
 * segment ti, and discard the mbuf containing it and any other
 * attached mbufs.
 *
 * In any case the ack and sequence number of the transmitted
 * segment are as specified by the parameters.
 */
void
tcp_respond(struct tcpcb *tp, caddr_t template, struct tcphdr *th0,
    tcp_seq ack, tcp_seq seq, int flags, u_int rtableid, uint64_t now)
{
	int tlen;
	int win = 0;
	struct mbuf *m = NULL;
	struct tcphdr *th;
	struct ip *ip;
#ifdef INET6
	struct ip6_hdr *ip6;
#endif
	int af;		/* af on wire */

	if (tp) {
		struct socket *so = tp->t_inpcb->inp_socket;
		win = sbspace(&so->so_rcv);
		/*
		 * If this is called with an unconnected
		 * socket/tp/pcb (tp->pf is 0), we lose.
		 */
		af = tp->pf;
	} else
		af = (((struct ip *)template)->ip_v == 6) ? AF_INET6 : AF_INET;

	m = m_gethdr(M_DONTWAIT, MT_HEADER);
	if (m == NULL)
		return;
	m->m_data += max_linkhdr;
	tlen = 0;

#define xchg(a,b,type) do { type t; t=a; a=b; b=t; } while (0)
	switch (af) {
#ifdef INET6
	case AF_INET6:
		ip6 = mtod(m, struct ip6_hdr *);
		th = (struct tcphdr *)(ip6 + 1);
		tlen = sizeof(*ip6) + sizeof(*th);
		if (th0) {
			memcpy(ip6, template, sizeof(*ip6));
			memcpy(th, th0, sizeof(*th));
			xchg(ip6->ip6_dst, ip6->ip6_src, struct in6_addr);
		} else {
			memcpy(ip6, template, tlen);
		}
		break;
#endif /* INET6 */
	case AF_INET:
		ip = mtod(m, struct ip *);
		th = (struct tcphdr *)(ip + 1);
		tlen = sizeof(*ip) + sizeof(*th);
		if (th0) {
			memcpy(ip, template, sizeof(*ip));
			memcpy(th, th0, sizeof(*th));
			xchg(ip->ip_dst.s_addr, ip->ip_src.s_addr, u_int32_t);
		} else {
			memcpy(ip, template, tlen);
		}
		break;
	}
	if (th0)
		xchg(th->th_dport, th->th_sport, u_int16_t);
	else
		flags = TH_ACK;
#undef xchg

	th->th_seq = htonl(seq);
	th->th_ack = htonl(ack);
	th->th_x2 = 0;
	th->th_off = sizeof (struct tcphdr) >> 2;
	th->th_flags = flags;
	if (tp)
		win >>= tp->rcv_scale;
	if (win > TCP_MAXWIN)
		win = TCP_MAXWIN;
	th->th_win = htons((u_int16_t)win);
	th->th_urp = 0;

	if (tp && (tp->t_flags & (TF_REQ_TSTMP|TF_NOOPT)) == TF_REQ_TSTMP &&
	    (flags & TH_RST) == 0 && (tp->t_flags & TF_RCVD_TSTMP)) {
		u_int32_t *lp = (u_int32_t *)(th + 1);
		/* Form timestamp option as shown in appendix A of RFC 1323. */
		*lp++ = htonl(TCPOPT_TSTAMP_HDR);
		*lp++ = htonl(now + tp->ts_modulate);
		*lp   = htonl(tp->ts_recent);
		tlen += TCPOLEN_TSTAMP_APPA;
		th->th_off = (sizeof(struct tcphdr) + TCPOLEN_TSTAMP_APPA) >> 2;
	}

	m->m_len = tlen;
	m->m_pkthdr.len = tlen;
	m->m_pkthdr.ph_ifidx = 0;
	m->m_pkthdr.csum_flags |= M_TCP_CSUM_OUT;

	/* force routing table */
	if (tp)
		m->m_pkthdr.ph_rtableid = tp->t_inpcb->inp_rtableid;
	else
		m->m_pkthdr.ph_rtableid = rtableid;

	switch (af) {
#ifdef INET6
	case AF_INET6:
		ip6->ip6_flow = htonl(0x60000000);
		ip6->ip6_nxt  = IPPROTO_TCP;
		ip6->ip6_hlim = in6_selecthlim(tp ? tp->t_inpcb : NULL); /*XXX*/
		ip6->ip6_plen = tlen - sizeof(struct ip6_hdr);
		ip6->ip6_plen = htons(ip6->ip6_plen);
		ip6_output(m, tp ? tp->t_inpcb->inp_outputopts6 : NULL,
		    tp ? &tp->t_inpcb->inp_route : NULL,
		    0, NULL,
		    tp ? &tp->t_inpcb->inp_seclevel : NULL);
		break;
#endif /* INET6 */
	case AF_INET:
		ip->ip_len = htons(tlen);
		ip->ip_ttl = atomic_load_int(&ip_defttl);
		ip->ip_tos = 0;
		ip_output(m, NULL,
		    tp ? &tp->t_inpcb->inp_route : NULL,
		    atomic_load_int(&ip_mtudisc) ? IP_MTUDISC : 0, NULL,
		    tp ? &tp->t_inpcb->inp_seclevel : NULL, 0);
		break;
	}
}

/*
 * Create a new TCP control block, making an
 * empty reassembly queue and hooking it to the argument
 * protocol control block.
 */
struct tcpcb *
tcp_newtcpcb(struct inpcb *inp, int wait)
{
	struct tcpcb *tp;
	int i;

	tp = pool_get(&tcpcb_pool, (wait == M_WAIT ? PR_WAITOK : PR_NOWAIT) |
	    PR_ZERO);
	if (tp == NULL)
		return (NULL);
	TAILQ_INIT(&tp->t_segq);
	tp->t_maxseg = atomic_load_int(&tcp_mssdflt);
	tp->t_maxopd = 0;

	tp->t_inpcb = inp;
	for (i = 0; i < TCPT_NTIMERS; i++)
		TCP_TIMER_INIT(tp, i);

	tp->sack_enable = atomic_load_int(&tcp_do_sack);
	tp->t_flags = atomic_load_int(&tcp_do_rfc1323) ?
	    (TF_REQ_SCALE|TF_REQ_TSTMP) : 0;
	/*
	 * Init srtt to TCPTV_SRTTBASE (0), so we can tell that we have no
	 * rtt estimate.  Set rttvar so that srtt + 2 * rttvar gives
	 * reasonable initial retransmit time.
	 */
	tp->t_srtt = TCPTV_SRTTBASE;
	tp->t_rttvar = tcp_rttdflt <<
	    (TCP_RTTVAR_SHIFT + TCP_RTT_BASE_SHIFT - 1);
	tp->t_rttmin = TCPTV_MIN;
	TCPT_RANGESET(tp->t_rxtcur, TCP_REXMTVAL(tp),
	    TCPTV_MIN, TCPTV_REXMTMAX);
	tp->snd_cwnd = TCP_MAXWIN << TCP_MAX_WINSHIFT;
	tp->snd_ssthresh = TCP_MAXWIN << TCP_MAX_WINSHIFT;

	tp->t_pmtud_mtu_sent = 0;
	tp->t_pmtud_mss_acked = 0;

#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6)) {
		tp->pf = PF_INET6;
		inp->inp_ipv6.ip6_hlim = atomic_load_int(&ip6_defhlim);
	} else
#endif
	{
		tp->pf = PF_INET;
		inp->inp_ip.ip_ttl = atomic_load_int(&ip_defttl);
	}

	inp->inp_ppcb = (caddr_t)tp;
	return (tp);
}

/*
 * Drop a TCP connection, reporting
 * the specified error.  If connection is synchronized,
 * then send a RST to peer.
 */
struct tcpcb *
tcp_drop(struct tcpcb *tp, int errno)
{
	struct socket *so = tp->t_inpcb->inp_socket;

	if (TCPS_HAVERCVDSYN(tp->t_state)) {
		tp->t_state = TCPS_CLOSED;
		(void) tcp_output(tp);
		tcpstat_inc(tcps_drops);
	} else
		tcpstat_inc(tcps_conndrops);
	if (errno == ETIMEDOUT && tp->t_softerror)
		errno = tp->t_softerror;
	so->so_error = errno;
	return (tcp_close(tp));
}

/*
 * Close a TCP control block:
 *	discard all space held by the tcp
 *	discard internet protocol block
 *	wake up any sleepers
 */
struct tcpcb *
tcp_close(struct tcpcb *tp)
{
	struct inpcb *inp = tp->t_inpcb;
	struct socket *so = inp->inp_socket;
	struct sackhole *p, *q;

	/* free the reassembly queue, if any */
	tcp_freeq(tp);

	tcp_canceltimers(tp);
	syn_cache_cleanup(tp);

	/* Free SACK holes. */
	q = p = tp->snd_holes;
	while (p != 0) {
		q = p->next;
		pool_put(&sackhl_pool, p);
		p = q;
	}

	m_free(tp->t_template);
	inp->inp_ppcb = NULL;
	pool_put(&tcpcb_pool, tp);
	soisdisconnected(so);
	in_pcbdetach(inp);
	tcpstat_inc(tcps_closed);
	return (NULL);
}

int
tcp_freeq(struct tcpcb *tp)
{
	struct tcpqent *qe;
	int rv = 0;

	while ((qe = TAILQ_FIRST(&tp->t_segq)) != NULL) {
		TAILQ_REMOVE(&tp->t_segq, qe, tcpqe_q);
		m_freem(qe->tcpqe_m);
		pool_put(&tcpqe_pool, qe);
		rv = 1;
	}
	return (rv);
}

/*
 * Compute proper scaling value for receiver window from buffer space
 */

void
tcp_rscale(struct tcpcb *tp, u_long hiwat)
{
	tp->request_r_scale = 0;
	while (tp->request_r_scale < TCP_MAX_WINSHIFT &&
	       TCP_MAXWIN << tp->request_r_scale < hiwat)
		tp->request_r_scale++;
}

/*
 * Notify a tcp user of an asynchronous error;
 * store error as soft error, but wake up user
 * (for now, won't do anything until can select for soft error).
 */
void
tcp_notify(struct inpcb *inp, int error)
{
	struct tcpcb *tp = intotcpcb(inp);
	struct socket *so = inp->inp_socket;

	soassertlocked(so);

	/*
	 * Ignore some errors if we are hooked up.
	 * If connection hasn't completed, has retransmitted several times,
	 * and receives a second error, give up now.  This is better
	 * than waiting a long time to establish a connection that
	 * can never complete.
	 */
	if (tp->t_state == TCPS_ESTABLISHED &&
	     (error == EHOSTUNREACH || error == ENETUNREACH ||
	      error == EHOSTDOWN)) {
		return;
	} else if (TCPS_HAVEESTABLISHED(tp->t_state) == 0 &&
	    tp->t_rxtshift > 3 && tp->t_softerror)
		so->so_error = error;
	else
		tp->t_softerror = error;
	wakeup((caddr_t) &so->so_timeo);
	sorwakeup(so);
	sowwakeup(so);
}

#ifdef INET6
void
tcp6_ctlinput(int cmd, struct sockaddr *sa, u_int rdomain, void *d)
{
	struct tcphdr th;
	void (*notify)(struct inpcb *, int) = tcp_notify;
	struct ip6_hdr *ip6;
	const struct sockaddr_in6 *sa6_src = NULL;
	struct sockaddr_in6 *sa6 = satosin6(sa);
	struct mbuf *m;
	tcp_seq seq;
	int off;
	struct {
		u_int16_t th_sport;
		u_int16_t th_dport;
		u_int32_t th_seq;
	} *thp;

	CTASSERT(sizeof(*thp) <= sizeof(th));
	if (sa->sa_family != AF_INET6 ||
	    sa->sa_len != sizeof(struct sockaddr_in6) ||
	    IN6_IS_ADDR_UNSPECIFIED(&sa6->sin6_addr) ||
	    IN6_IS_ADDR_V4MAPPED(&sa6->sin6_addr))
		return;
	if ((unsigned)cmd >= PRC_NCMDS)
		return;
	else if (cmd == PRC_QUENCH) {
		/*
		 * Don't honor ICMP Source Quench messages meant for
		 * TCP connections.
		 */
		/* XXX there's no PRC_QUENCH in IPv6 */
		return;
	} else if (PRC_IS_REDIRECT(cmd))
		notify = in_pcbrtchange, d = NULL;
	else if (cmd == PRC_MSGSIZE)
		; /* special code is present, see below */
	else if (cmd == PRC_HOSTDEAD)
		d = NULL;
	else if (inet6ctlerrmap[cmd] == 0)
		return;

	/* if the parameter is from icmp6, decode it. */
	if (d != NULL) {
		struct ip6ctlparam *ip6cp = (struct ip6ctlparam *)d;
		m = ip6cp->ip6c_m;
		ip6 = ip6cp->ip6c_ip6;
		off = ip6cp->ip6c_off;
		sa6_src = ip6cp->ip6c_src;
	} else {
		m = NULL;
		ip6 = NULL;
		sa6_src = &sa6_any;
	}

	if (ip6) {
		struct inpcb *inp;
		struct socket *so = NULL;
		struct tcpcb *tp = NULL;

		/*
		 * XXX: We assume that when ip6 is non NULL,
		 * M and OFF are valid.
		 */

		/* check if we can safely examine src and dst ports */
		if (m->m_pkthdr.len < off + sizeof(*thp))
			return;

		bzero(&th, sizeof(th));
		m_copydata(m, off, sizeof(*thp), &th);

		/*
		 * Check to see if we have a valid TCP connection
		 * corresponding to the address in the ICMPv6 message
		 * payload.
		 */
		inp = in6_pcblookup(&tcb6table, &sa6->sin6_addr,
		    th.th_dport, &sa6_src->sin6_addr, th.th_sport, rdomain);
		if (cmd == PRC_MSGSIZE) {
			/*
			 * Depending on the value of "valid" and routing table
			 * size (mtudisc_{hi,lo}wat), we will:
			 * - recalculate the new MTU and create the
			 *   corresponding routing entry, or
			 * - ignore the MTU change notification.
			 */
			icmp6_mtudisc_update((struct ip6ctlparam *)d,
			    inp != NULL);
			in_pcbunref(inp);
			return;
		}
		if (inp != NULL)
			so = in_pcbsolock(inp);
		if (so != NULL)
			tp = intotcpcb(inp);
		if (tp != NULL) {
			seq = ntohl(th.th_seq);
			if ((tp = intotcpcb(inp)) &&
			    SEQ_GEQ(seq, tp->snd_una) &&
			    SEQ_LT(seq, tp->snd_max))
				notify(inp, inet6ctlerrmap[cmd]);
		}
		in_pcbsounlock(inp, so);
		in_pcbunref(inp);

		if (tp == NULL &&
		    (inet6ctlerrmap[cmd] == EHOSTUNREACH ||
		    inet6ctlerrmap[cmd] == ENETUNREACH ||
		    inet6ctlerrmap[cmd] == EHOSTDOWN)) {
			syn_cache_unreach(sin6tosa_const(sa6_src), sa, &th,
			    rdomain);
		}
	} else {
		in6_pcbnotify(&tcb6table, sa6, 0,
		    sa6_src, 0, rdomain, cmd, NULL, notify);
	}
}
#endif

void
tcp_ctlinput(int cmd, struct sockaddr *sa, u_int rdomain, void *v)
{
	struct ip *ip = v;
	struct tcphdr *th;
	struct in_addr faddr;
	tcp_seq seq;
	u_int mtu;
	void (*notify)(struct inpcb *, int) = tcp_notify;
	int errno;

	if (sa->sa_family != AF_INET)
		return;
	faddr = satosin(sa)->sin_addr;
	if (faddr.s_addr == INADDR_ANY)
		return;

	if ((unsigned)cmd >= PRC_NCMDS)
		return;
	errno = inetctlerrmap[cmd];
	if (cmd == PRC_QUENCH)
		/*
		 * Don't honor ICMP Source Quench messages meant for
		 * TCP connections.
		 */
		return;
	else if (PRC_IS_REDIRECT(cmd))
		notify = in_pcbrtchange, ip = NULL;
	else if (cmd == PRC_MSGSIZE && atomic_load_int(&ip_mtudisc) && ip) {
		struct inpcb *inp;
		struct socket *so = NULL;
		struct tcpcb *tp = NULL;

		/*
		 * Verify that the packet in the icmp payload refers
		 * to an existing TCP connection.
		 */
		th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		seq = ntohl(th->th_seq);
		inp = in_pcblookup(&tcbtable,
		    ip->ip_dst, th->th_dport, ip->ip_src, th->th_sport,
		    rdomain);
		if (inp != NULL)
			so = in_pcbsolock(inp);
		if (so != NULL)
			tp = intotcpcb(inp);
		if (tp != NULL &&
		    SEQ_GEQ(seq, tp->snd_una) &&
		    SEQ_LT(seq, tp->snd_max)) {
			struct icmp *icp;
			icp = (struct icmp *)((caddr_t)ip -
					      offsetof(struct icmp, icmp_ip));

			/*
			 * If the ICMP message advertises a Next-Hop MTU
			 * equal or larger than the maximum packet size we have
			 * ever sent, drop the message.
			 */
			mtu = (u_int)ntohs(icp->icmp_nextmtu);
			if (mtu >= tp->t_pmtud_mtu_sent) {
				in_pcbsounlock(inp, so);
				in_pcbunref(inp);
				return;
			}
			if (mtu >= tcp_hdrsz(tp) + tp->t_pmtud_mss_acked) {
				/*
				 * Calculate new MTU, and create corresponding
				 * route (traditional PMTUD).
				 */
				tp->t_flags &= ~TF_PMTUD_PEND;
				icmp_mtudisc(icp, inp->inp_rtableid);
			} else {
				/*
				 * Record the information got in the ICMP
				 * message; act on it later.
				 * If we had already recorded an ICMP message,
				 * replace the old one only if the new message
				 * refers to an older TCP segment
				 */
				if (tp->t_flags & TF_PMTUD_PEND) {
					if (SEQ_LT(tp->t_pmtud_th_seq, seq)) {
						in_pcbsounlock(inp, so);
						in_pcbunref(inp);
						return;
					}
				} else
					tp->t_flags |= TF_PMTUD_PEND;
				tp->t_pmtud_th_seq = seq;
				tp->t_pmtud_nextmtu = icp->icmp_nextmtu;
				tp->t_pmtud_ip_len = icp->icmp_ip.ip_len;
				tp->t_pmtud_ip_hl = icp->icmp_ip.ip_hl;
				in_pcbsounlock(inp, so);
				in_pcbunref(inp);
				return;
			}
		} else {
			/* ignore if we don't have a matching connection */
			in_pcbsounlock(inp, so);
			in_pcbunref(inp);
			return;
		}
		in_pcbsounlock(inp, so);
		in_pcbunref(inp);
		notify = tcp_mtudisc, ip = NULL;
	} else if (cmd == PRC_MTUINC)
		notify = tcp_mtudisc_increase, ip = NULL;
	else if (cmd == PRC_HOSTDEAD)
		ip = NULL;
	else if (errno == 0)
		return;

	if (ip) {
		struct inpcb *inp;
		struct socket *so = NULL;
		struct tcpcb *tp = NULL;

		th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		inp = in_pcblookup(&tcbtable,
		    ip->ip_dst, th->th_dport, ip->ip_src, th->th_sport,
		    rdomain);
		if (inp != NULL)
			so = in_pcbsolock(inp);
		if (so != NULL)
			tp = intotcpcb(inp);
		if (tp != NULL) {
			seq = ntohl(th->th_seq);
			if (SEQ_GEQ(seq, tp->snd_una) &&
			    SEQ_LT(seq, tp->snd_max))
				notify(inp, errno);
		}
		in_pcbsounlock(inp, so);
		in_pcbunref(inp);

		if (tp == NULL &&
		    (inetctlerrmap[cmd] == EHOSTUNREACH ||
		    inetctlerrmap[cmd] == ENETUNREACH ||
		    inetctlerrmap[cmd] == EHOSTDOWN)) {
			struct sockaddr_in sin;

			bzero(&sin, sizeof(sin));
			sin.sin_len = sizeof(sin);
			sin.sin_family = AF_INET;
			sin.sin_port = th->th_sport;
			sin.sin_addr = ip->ip_src;
			syn_cache_unreach(sintosa(&sin), sa, th, rdomain);
		}
	} else
		in_pcbnotifyall(&tcbtable, satosin(sa), rdomain, errno, notify);
}


#ifdef INET6
/*
 * Path MTU Discovery handlers.
 */
void
tcp6_mtudisc_callback(struct sockaddr_in6 *sin6, u_int rdomain)
{
	in6_pcbnotify(&tcb6table, sin6, 0,
	    &sa6_any, 0, rdomain, PRC_MSGSIZE, NULL, tcp_mtudisc);
}
#endif /* INET6 */

/*
 * On receipt of path MTU corrections, flush old route and replace it
 * with the new one.  Retransmit all unacknowledged packets, to ensure
 * that all packets will be received.
 */
void
tcp_mtudisc(struct inpcb *inp, int errno)
{
	struct tcpcb *tp = intotcpcb(inp);
	struct rtentry *rt;
	int orig_maxseg, change = 0;

	if (tp == NULL)
		return;
	orig_maxseg = tp->t_maxseg;

	rt = in_pcbrtentry(inp);
	if (rt != NULL) {
		unsigned int orig_mtulock = (rt->rt_locks & RTV_MTU);

		/*
		 * If this was not a host route, remove and realloc.
		 */
		if ((rt->rt_flags & RTF_HOST) == 0) {
			in_pcbrtchange(inp, errno);
			if ((rt = in_pcbrtentry(inp)) == NULL)
				return;
		}
		if (orig_mtulock < (rt->rt_locks & RTV_MTU))
			change = 1;
	}
	tcp_mss(tp, -1);
	if (orig_maxseg > tp->t_maxseg)
		change = 1;

	/*
	 * Resend unacknowledged packets
	 */
	tp->snd_nxt = tp->snd_una;
	if (change || errno > 0)
		tcp_output(tp);
}

void
tcp_mtudisc_increase(struct inpcb *inp, int errno)
{
	struct tcpcb *tp = intotcpcb(inp);
	struct rtentry *rt = in_pcbrtentry(inp);

	if (tp != 0 && rt != 0) {
		/*
		 * If this was a host route, remove and realloc.
		 */
		if (rt->rt_flags & RTF_HOST)
			in_pcbrtchange(inp, errno);

		/* also takes care of congestion window */
		tcp_mss(tp, -1);
	}
}

/*
 * Generate new ISNs with a method based on RFC1948
 */
#define TCP_ISS_CONN_INC 4096

void
tcp_set_iss_tsm(struct tcpcb *tp)
{
	SHA2_CTX ctx;
	union {
		uint8_t bytes[SHA512_DIGEST_LENGTH];
		uint32_t words[2];
	} digest;
	u_int rdomain = rtable_l2(tp->t_inpcb->inp_rtableid);
	tcp_seq iss;

	mtx_enter(&tcp_timer_mtx);
	tcp_iss += TCP_ISS_CONN_INC;
	iss = tcp_iss;
	mtx_leave(&tcp_timer_mtx);

	ctx = tcp_secret_ctx;
	SHA512Update(&ctx, &rdomain, sizeof(rdomain));
	SHA512Update(&ctx, &tp->t_inpcb->inp_lport, sizeof(u_short));
	SHA512Update(&ctx, &tp->t_inpcb->inp_fport, sizeof(u_short));
	if (tp->pf == AF_INET6) {
		SHA512Update(&ctx, &tp->t_inpcb->inp_laddr6,
		    sizeof(struct in6_addr));
		SHA512Update(&ctx, &tp->t_inpcb->inp_faddr6,
		    sizeof(struct in6_addr));
	} else {
		SHA512Update(&ctx, &tp->t_inpcb->inp_laddr,
		    sizeof(struct in_addr));
		SHA512Update(&ctx, &tp->t_inpcb->inp_faddr,
		    sizeof(struct in_addr));
	}
	SHA512Final(digest.bytes, &ctx);
	tp->iss = digest.words[0] + iss;
	tp->ts_modulate = digest.words[1];
}

#ifdef TCP_SIGNATURE
int
tcp_signature_tdb_attach(void)
{
	return (0);
}

int
tcp_signature_tdb_init(struct tdb *tdbp, const struct xformsw *xsp,
    struct ipsecinit *ii)
{
	if ((ii->ii_authkeylen < 1) || (ii->ii_authkeylen > 80))
		return (EINVAL);

	tdbp->tdb_amxkey = malloc(ii->ii_authkeylen, M_XDATA, M_NOWAIT);
	if (tdbp->tdb_amxkey == NULL)
		return (ENOMEM);
	memcpy(tdbp->tdb_amxkey, ii->ii_authkey, ii->ii_authkeylen);
	tdbp->tdb_amxkeylen = ii->ii_authkeylen;

	return (0);
}

int
tcp_signature_tdb_zeroize(struct tdb *tdbp)
{
	if (tdbp->tdb_amxkey) {
		explicit_bzero(tdbp->tdb_amxkey, tdbp->tdb_amxkeylen);
		free(tdbp->tdb_amxkey, M_XDATA, tdbp->tdb_amxkeylen);
		tdbp->tdb_amxkey = NULL;
	}

	return (0);
}

int
tcp_signature_tdb_input(struct mbuf **mp, struct tdb *tdbp, int skip,
    int protoff, struct netstack *sn)
{
	m_freemp(mp);
	return (IPPROTO_DONE);
}

int
tcp_signature_tdb_output(struct mbuf *m, struct tdb *tdbp, int skip,
    int protoff)
{
	m_freem(m);
	return (EINVAL);
}

int
tcp_signature_apply(caddr_t fstate, caddr_t data, unsigned int len)
{
	MD5Update((MD5_CTX *)fstate, (char *)data, len);
	return 0;
}

int
tcp_signature(struct tdb *tdb, int af, struct mbuf *m, struct tcphdr *th,
    int iphlen, int doswap, char *sig)
{
	MD5_CTX ctx;
	int len;
	struct tcphdr th0;

	MD5Init(&ctx);

	switch(af) {
	case 0:
	case AF_INET: {
		struct ippseudo ippseudo;
		struct ip *ip;

		ip = mtod(m, struct ip *);

		ippseudo.ippseudo_src = ip->ip_src;
		ippseudo.ippseudo_dst = ip->ip_dst;
		ippseudo.ippseudo_pad = 0;
		ippseudo.ippseudo_p = IPPROTO_TCP;
		ippseudo.ippseudo_len = htons(m->m_pkthdr.len - iphlen);

		MD5Update(&ctx, (char *)&ippseudo,
		    sizeof(struct ippseudo));
		break;
		}
#ifdef INET6
	case AF_INET6: {
		struct ip6_hdr_pseudo ip6pseudo;
		struct ip6_hdr *ip6;

		ip6 = mtod(m, struct ip6_hdr *);
		bzero(&ip6pseudo, sizeof(ip6pseudo));
		ip6pseudo.ip6ph_src = ip6->ip6_src;
		ip6pseudo.ip6ph_dst = ip6->ip6_dst;
		in6_clearscope(&ip6pseudo.ip6ph_src);
		in6_clearscope(&ip6pseudo.ip6ph_dst);
		ip6pseudo.ip6ph_nxt = IPPROTO_TCP;
		ip6pseudo.ip6ph_len = htonl(m->m_pkthdr.len - iphlen);

		MD5Update(&ctx, (char *)&ip6pseudo,
		    sizeof(ip6pseudo));
		break;
		}
#endif
	}

	th0 = *th;
	th0.th_sum = 0;

	if (doswap) {
		th0.th_seq = htonl(th0.th_seq);
		th0.th_ack = htonl(th0.th_ack);
		th0.th_win = htons(th0.th_win);
		th0.th_urp = htons(th0.th_urp);
	}
	MD5Update(&ctx, (char *)&th0, sizeof(th0));

	len = m->m_pkthdr.len - iphlen - th->th_off * sizeof(uint32_t);

	if (len > 0 &&
	    m_apply(m, iphlen + th->th_off * sizeof(uint32_t), len,
	    tcp_signature_apply, (caddr_t)&ctx))
		return (-1);

	MD5Update(&ctx, tdb->tdb_amxkey, tdb->tdb_amxkeylen);
	MD5Final(sig, &ctx);

	return (0);
}
#endif /* TCP_SIGNATURE */

/*	$OpenBSD: tcp_input.c,v 1.464 2025/09/16 17:29:35 bluhm Exp $	*/
/*	$NetBSD: tcp_input.c,v 1.23 1996/02/13 23:43:44 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1994
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

#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <sys/kernel.h>
#include <sys/pool.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>

#include <netinet/if_ether.h>
#include <netinet/in.h>
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

#if NPF > 0
#include <net/pfvar.h>
#endif

#ifdef INET6
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#endif /* INET6 */

const int tcprexmtthresh = 3;

int tcp_rst_ppslim = 100;		/* 100pps */
int tcp_rst_ppslim_count = 0;
struct timeval tcp_rst_ppslim_last;

int tcp_ackdrop_ppslim = 100;		/* 100pps */
int tcp_ackdrop_ppslim_count = 0;
struct timeval tcp_ackdrop_ppslim_last;

#define TCP_PAWS_IDLE	TCP_TIME(24 * 24 * 60 * 60)

/* for modulo comparisons of timestamps */
#define TSTMP_LT(a,b)	((int32_t)((a)-(b)) < 0)
#define TSTMP_GEQ(a,b)	((int32_t)((a)-(b)) >= 0)

/* for TCP SACK comparisons */
#define	SEQ_MIN(a,b)	(SEQ_LT(a,b) ? (a) : (b))
#define	SEQ_MAX(a,b)	(SEQ_GT(a,b) ? (a) : (b))

#ifdef TCP_ECN
/*
 * ECN (Explicit Congestion Notification) support based on RFC3168
 * implementation note:
 *   snd_last is used to track a recovery phase.
 *   when cwnd is reduced, snd_last is set to snd_max.
 *   while snd_last > snd_una, the sender is in a recovery phase and
 *   its cwnd should not be reduced again.
 *   snd_last follows snd_una when not in a recovery phase.
 */
#endif

/*
 * Macro to compute ACK transmission behavior.  Delay the ACK unless
 * we have already delayed an ACK (must send an ACK every two segments).
 * We also ACK immediately if we received a PUSH and the ACK-on-PUSH
 * option is enabled or when the packet is coming from a loopback
 * interface.
 */
#define	TCP_SETUP_ACK(tp, tiflags, m) \
do { \
	struct ifnet *ifp = NULL; \
	if (m && (m->m_flags & M_PKTHDR)) \
		ifp = if_get(m->m_pkthdr.ph_ifidx); \
	if (TCP_TIMER_ISARMED(tp, TCPT_DELACK) || \
	    (atomic_load_int(&tcp_ack_on_push) && (tiflags) & TH_PUSH) || \
	    (ifp && (ifp->if_flags & IFF_LOOPBACK))) \
		tp->t_flags |= TF_ACKNOW; \
	else \
		TCP_TIMER_ARM(tp, TCPT_DELACK, tcp_delack_msecs); \
	if_put(ifp); \
} while (0)

int	 tcp_input_solocked(struct mbuf **, int *, int, int, struct socket **);
int	 tcp_mss_adv(struct rtentry *, int);
int	 tcp_flush_queue(struct tcpcb *);
void	 tcp_sack_partialack(struct tcpcb *, struct tcphdr *);
void	 tcp_newreno_partialack(struct tcpcb *, struct tcphdr *);

void	 syn_cache_put(struct syn_cache *);
void	 syn_cache_rm(struct syn_cache *);
int	 syn_cache_respond(struct syn_cache *, struct mbuf *, uint64_t, int);
void	 syn_cache_timer(void *);
void	 syn_cache_insert(struct syn_cache *, struct tcpcb *);
void	 syn_cache_reset(struct sockaddr *, struct sockaddr *,
		struct tcphdr *, u_int);
int	 syn_cache_add(struct sockaddr *, struct sockaddr *, struct tcphdr *,
		unsigned int, struct socket *, struct mbuf *, u_char *, int,
		struct tcp_opt_info *, tcp_seq *, uint64_t, int);
struct socket *syn_cache_get(struct sockaddr *, struct sockaddr *,
		struct tcphdr *, unsigned int, unsigned int, struct socket *,
		struct mbuf *, uint64_t, int);
struct syn_cache *syn_cache_lookup(const struct sockaddr *,
		const struct sockaddr *, struct syn_cache_head **, u_int);

/*
 * Insert segment ti into reassembly queue of tcp with
 * control block tp.  Return TH_FIN if reassembly now includes
 * a segment with FIN.  The macro form does the common case inline
 * (segment is the next to be received on an established connection,
 * and the queue is empty), avoiding linkage into and removal
 * from the queue and repetition of various conversions.
 * Set DELACK for segments received in order, but ack immediately
 * when segments are out of order (so fast retransmit can work).
 */

int
tcp_reass(struct tcpcb *tp, struct tcphdr *th, struct mbuf *m, int *tlen)
{
	struct tcpqent *p, *q, *nq, *tiqe;

	/*
	 * Allocate a new queue entry, before we throw away any data.
	 * If we can't, just drop the packet.  XXX
	 */
	tiqe = pool_get(&tcpqe_pool, PR_NOWAIT);
	if (tiqe == NULL) {
		tiqe = TAILQ_LAST(&tp->t_segq, tcpqehead);
		if (tiqe != NULL && th->th_seq == tp->rcv_nxt) {
			/* Reuse last entry since new segment fills a hole */
			m_freem(tiqe->tcpqe_m);
			TAILQ_REMOVE(&tp->t_segq, tiqe, tcpqe_q);
		}
		if (tiqe == NULL || th->th_seq != tp->rcv_nxt) {
			/* Flush segment queue for this connection */
			tcp_freeq(tp);
			tcpstat_inc(tcps_rcvmemdrop);
			m_freem(m);
			return (0);
		}
	}

	/*
	 * Find a segment which begins after this one does.
	 */
	for (p = NULL, q = TAILQ_FIRST(&tp->t_segq); q != NULL;
	    p = q, q = TAILQ_NEXT(q, tcpqe_q))
		if (SEQ_GT(q->tcpqe_tcp->th_seq, th->th_seq))
			break;

	/*
	 * If there is a preceding segment, it may provide some of
	 * our data already.  If so, drop the data from the incoming
	 * segment.  If it provides all of our data, drop us.
	 */
	if (p != NULL) {
		struct tcphdr *phdr = p->tcpqe_tcp;
		int i;

		/* conversion to int (in i) handles seq wraparound */
		i = phdr->th_seq + phdr->th_reseqlen - th->th_seq;
		if (i > 0) {
			if (i >= *tlen) {
				tcpstat_pkt(tcps_rcvduppack, tcps_rcvdupbyte,
				    *tlen);
				m_freem(m);
				pool_put(&tcpqe_pool, tiqe);
				return (0);
			}
			m_adj(m, i);
			*tlen -= i;
			th->th_seq += i;
		}
	}
	tcpstat_pkt(tcps_rcvoopack, tcps_rcvoobyte, *tlen);
	tp->t_rcvoopack++;

	/*
	 * While we overlap succeeding segments trim them or,
	 * if they are completely covered, dequeue them.
	 */
	for (; q != NULL; q = nq) {
		struct tcphdr *qhdr = q->tcpqe_tcp;
		int i = (th->th_seq + *tlen) - qhdr->th_seq;

		if (i <= 0)
			break;
		if (i < qhdr->th_reseqlen) {
			qhdr->th_seq += i;
			qhdr->th_reseqlen -= i;
			m_adj(q->tcpqe_m, i);
			break;
		}
		nq = TAILQ_NEXT(q, tcpqe_q);
		m_freem(q->tcpqe_m);
		TAILQ_REMOVE(&tp->t_segq, q, tcpqe_q);
		pool_put(&tcpqe_pool, q);
	}

	/* Insert the new segment queue entry into place. */
	tiqe->tcpqe_m = m;
	th->th_reseqlen = *tlen;
	tiqe->tcpqe_tcp = th;
	if (p == NULL) {
		TAILQ_INSERT_HEAD(&tp->t_segq, tiqe, tcpqe_q);
	} else {
		TAILQ_INSERT_AFTER(&tp->t_segq, p, tiqe, tcpqe_q);
	}

	if (th->th_seq != tp->rcv_nxt)
		return (0);

	return (tcp_flush_queue(tp));
}

int
tcp_flush_queue(struct tcpcb *tp)
{
	struct socket *so = tp->t_inpcb->inp_socket;
	struct tcpqent *q, *nq;
	int flags;

	/*
	 * Present data to user, advancing rcv_nxt through
	 * completed sequence space.
	 */
	if (TCPS_HAVEESTABLISHED(tp->t_state) == 0)
		return (0);
	q = TAILQ_FIRST(&tp->t_segq);
	if (q == NULL || q->tcpqe_tcp->th_seq != tp->rcv_nxt)
		return (0);
	if (tp->t_state == TCPS_SYN_RECEIVED && q->tcpqe_tcp->th_reseqlen)
		return (0);
	do {
		tp->rcv_nxt += q->tcpqe_tcp->th_reseqlen;
		flags = q->tcpqe_tcp->th_flags & TH_FIN;

		nq = TAILQ_NEXT(q, tcpqe_q);
		TAILQ_REMOVE(&tp->t_segq, q, tcpqe_q);
		if (so->so_rcv.sb_state & SS_CANTRCVMORE)
			m_freem(q->tcpqe_m);
		else {
			mtx_enter(&so->so_rcv.sb_mtx);
			sbappendstream(&so->so_rcv, q->tcpqe_m);
			mtx_leave(&so->so_rcv.sb_mtx);
		}
		pool_put(&tcpqe_pool, q);
		q = nq;
	} while (q != NULL && q->tcpqe_tcp->th_seq == tp->rcv_nxt);
	sorwakeup(so);
	return (flags);
}

int
tcp_input(struct mbuf **mp, int *offp, int proto, int af, struct netstack *ns)
{
	if (ns == NULL)
		return tcp_input_solocked(mp, offp, proto, af, NULL);
	(*mp)->m_pkthdr.ph_cookie = (void *)(long)(*offp);
	switch (af) {
	case AF_INET:
		ml_enqueue(&ns->ns_tcp_ml, *mp);
		break;
#ifdef INET6
	case AF_INET6:
		ml_enqueue(&ns->ns_tcp6_ml, *mp);
		break;
#endif
	default:
		m_freemp(mp);
	}
	*mp = NULL;
	return IPPROTO_DONE;
}

void
tcp_input_mlist(struct mbuf_list *ml, int af)
{
	struct socket *so = NULL;
	struct mbuf *m;

	while ((m = ml_dequeue(ml)) != NULL) {
		int off, nxt;

		off = (long)m->m_pkthdr.ph_cookie;
		m->m_pkthdr.ph_cookie = NULL;
		nxt = tcp_input_solocked(&m, &off, IPPROTO_TCP, af, &so);
		KASSERT(nxt == IPPROTO_DONE);
	}

	in_pcbsounlock(NULL, so);
}

/*
 * TCP input routine, follows pages 65-76 of the
 * protocol specification dated September, 1981 very closely.
 */
int
tcp_input_solocked(struct mbuf **mp, int *offp, int proto, int af,
    struct socket **solocked)
{
	struct mbuf *m = *mp;
	int iphlen = *offp;
	struct ip *ip = NULL;
	struct inpcb *inp = NULL;
	u_int8_t *optp = NULL;
	int optlen = 0;
	int tlen, off;
	struct tcpcb *otp = NULL, *tp = NULL;
	int tiflags;
	struct socket *so = NULL;
	int todrop, acked, ourfinisacked;
	int hdroptlen = 0;
	short ostate;
	union {
		struct	tcpiphdr tcpip;
#ifdef INET6
		struct  tcpipv6hdr tcpip6;
#endif
		char	caddr;
	} saveti;
	tcp_seq iss, *reuse = NULL;
	uint64_t now;
	u_long tiwin;
	struct tcp_opt_info opti;
	struct tcphdr *th;
#ifdef INET6
	struct ip6_hdr *ip6 = NULL;
#endif /* INET6 */
	int do_ecn = 0;
#ifdef TCP_ECN
	u_char iptos;
#endif

	tcpstat_inc(tcps_rcvtotal);

	opti.ts_present = 0;
	opti.maxseg = 0;
	now = tcp_now();
#ifdef TCP_ECN
	do_ecn = atomic_load_int(&tcp_do_ecn);
#endif

	/*
	 * RFC1122 4.2.3.10, p. 104: discard bcast/mcast SYN
	 */
	if (m->m_flags & (M_BCAST|M_MCAST))
		goto drop;

	/*
	 * Get IP and TCP header together in first mbuf.
	 * Note: IP leaves IP header in first mbuf.
	 */
	th = ip6_exthdr_get(mp, iphlen, sizeof(*th));
	if (th == NULL) {
		tcpstat_inc(tcps_rcvshort);
		return IPPROTO_DONE;
	}

	tlen = m->m_pkthdr.len - iphlen;
	switch (af) {
	case AF_INET:
		ip = mtod(m, struct ip *);
#ifdef TCP_ECN
		/* save ip_tos before clearing it for checksum */
		iptos = ip->ip_tos;
#endif
		break;
#ifdef INET6
	case AF_INET6:
		ip6 = mtod(m, struct ip6_hdr *);
#ifdef TCP_ECN
		iptos = (ntohl(ip6->ip6_flow) >> 20) & 0xff;
#endif

		/*
		 * Be proactive about unspecified IPv6 address in source.
		 * As we use all-zero to indicate unbounded/unconnected pcb,
		 * unspecified IPv6 address can be used to confuse us.
		 *
		 * Note that packets with unspecified IPv6 destination is
		 * already dropped in ip6_input.
		 */
		if (IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src)) {
			/* XXX stat */
			goto drop;
		}

		/* Discard packets to multicast */
		if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
			/* XXX stat */
			goto drop;
		}
		break;
#endif
	default:
		unhandled_af(af);
	}

	/*
	 * Checksum extended TCP header and data.
	 */
	if ((m->m_pkthdr.csum_flags & M_TCP_CSUM_IN_OK) == 0) {
		int sum;

		if (m->m_pkthdr.csum_flags & M_TCP_CSUM_IN_BAD) {
			tcpstat_inc(tcps_rcvbadsum);
			goto drop;
		}
		tcpstat_inc(tcps_inswcsum);
		switch (af) {
		case AF_INET:
			sum = in4_cksum(m, IPPROTO_TCP, iphlen, tlen);
			break;
#ifdef INET6
		case AF_INET6:
			sum = in6_cksum(m, IPPROTO_TCP, iphlen, tlen);
			break;
#endif
		}
		if (sum != 0) {
			tcpstat_inc(tcps_rcvbadsum);
			goto drop;
		}
	}

	/*
	 * Check that TCP offset makes sense,
	 * pull out TCP options and adjust length.		XXX
	 */
	off = th->th_off << 2;
	if (off < sizeof(struct tcphdr) || off > tlen) {
		tcpstat_inc(tcps_rcvbadoff);
		goto drop;
	}
	tlen -= off;
	if (off > sizeof(struct tcphdr)) {
		th = ip6_exthdr_get(mp, iphlen, off);
		if (th == NULL) {
			tcpstat_inc(tcps_rcvshort);
			return IPPROTO_DONE;
		}
		optlen = off - sizeof(struct tcphdr);
		optp = (u_int8_t *)(th + 1);
		/*
		 * Do quick retrieval of timestamp options ("options
		 * prediction?").  If timestamp is the only option and it's
		 * formatted as recommended in RFC 1323 appendix A, we
		 * quickly get the values now and not bother calling
		 * tcp_dooptions(), etc.
		 */
		if ((optlen == TCPOLEN_TSTAMP_APPA ||
		     (optlen > TCPOLEN_TSTAMP_APPA &&
		      optp[TCPOLEN_TSTAMP_APPA] == TCPOPT_EOL)) &&
		     *(u_int32_t *)optp == htonl(TCPOPT_TSTAMP_HDR) &&
		     (th->th_flags & TH_SYN) == 0) {
			opti.ts_present = 1;
			opti.ts_val = ntohl(*(u_int32_t *)(optp + 4));
			opti.ts_ecr = ntohl(*(u_int32_t *)(optp + 8));
			optp = NULL;	/* we've parsed the options */
		}
	}
	tiflags = th->th_flags;

	/*
	 * Convert TCP protocol specific fields to host format.
	 */
	th->th_seq = ntohl(th->th_seq);
	th->th_ack = ntohl(th->th_ack);
	th->th_win = ntohs(th->th_win);
	th->th_urp = ntohs(th->th_urp);

	if (th->th_dport == 0) {
		tcpstat_inc(tcps_noport);
		goto dropwithreset_ratelim;
	}

	/*
	 * Locate pcb for segment.
	 */
#if NPF > 0
	inp = pf_inp_lookup(m);
#endif
findpcb:
	if (inp == NULL) {
		switch (af) {
#ifdef INET6
		case AF_INET6:
			inp = in6_pcblookup(&tcb6table, &ip6->ip6_src,
			    th->th_sport, &ip6->ip6_dst, th->th_dport,
			    m->m_pkthdr.ph_rtableid);
			break;
#endif
		case AF_INET:
			inp = in_pcblookup(&tcbtable, ip->ip_src,
			    th->th_sport, ip->ip_dst, th->th_dport,
			    m->m_pkthdr.ph_rtableid);
			break;
		}
	}
	if (inp == NULL) {
		tcpstat_inc(tcps_pcbhashmiss);
		switch (af) {
#ifdef INET6
		case AF_INET6:
			inp = in6_pcblookup_listen(&tcb6table, &ip6->ip6_dst,
			    th->th_dport, m, m->m_pkthdr.ph_rtableid);
			break;
#endif
		case AF_INET:
			inp = in_pcblookup_listen(&tcbtable, ip->ip_dst,
			    th->th_dport, m, m->m_pkthdr.ph_rtableid);
			break;
		}
		/*
		 * If the state is CLOSED (i.e., TCB does not exist) then
		 * all data in the incoming segment is discarded.
		 * If the TCB exists but is in CLOSED state, it is embryonic,
		 * but should either do a listen or a connect soon.
		 */
	}
#ifdef IPSEC
	if (ipsec_in_use) {
		struct m_tag *mtag;
		struct tdb *tdb = NULL;
		int error;

		/* Find most recent IPsec tag */
		mtag = m_tag_find(m, PACKET_TAG_IPSEC_IN_DONE, NULL);
		if (mtag != NULL) {
			struct tdb_ident *tdbi;

			tdbi = (struct tdb_ident *)(mtag + 1);
			tdb = gettdb(tdbi->rdomain, tdbi->spi,
			    &tdbi->dst, tdbi->proto);
		}
		error = ipsp_spd_lookup(m, af, iphlen, IPSP_DIRECTION_IN,
		    tdb, inp ? &inp->inp_seclevel : NULL, NULL, NULL);
		tdb_unref(tdb);
		if (error) {
			tcpstat_inc(tcps_rcvnosec);
			goto drop;
		}
	}
#endif /* IPSEC */

	if (inp == NULL) {
		tcpstat_inc(tcps_noport);
		goto dropwithreset_ratelim;
	}
	/*
	 * Avoid needless lock and unlock operation when handling multiple
	 * TCP packets from the same stream consecutively.
	 */
	if (solocked != NULL && *solocked != NULL &&
	    sotoinpcb(*solocked) == inp) {
		so = *solocked;
		*solocked = NULL;
	} else {
		if (solocked != NULL && *solocked != NULL) {
			in_pcbsounlock(NULL, *solocked);
			*solocked = NULL;
		}
		so = in_pcbsolock(inp);
	}
	if (so == NULL) {
		tcpstat_inc(tcps_closing);
		goto dropwithreset_ratelim;
	}
	KASSERT(sotoinpcb(inp->inp_socket) == inp);
	KASSERT(intotcpcb(inp) == NULL || intotcpcb(inp)->t_inpcb == inp);
	soassertlocked(inp->inp_socket);

	/* Check the minimum TTL for socket. */
	switch (af) {
	case AF_INET:
		if (inp->inp_ip_minttl && inp->inp_ip_minttl > ip->ip_ttl)
			goto drop;
		break;
#ifdef INET6
	case AF_INET6:
		if (inp->inp_ip6_minhlim &&
		    inp->inp_ip6_minhlim > ip6->ip6_hlim)
			goto drop;
		break;
#endif
	}

	tp = intotcpcb(inp);
	if (tp == NULL)
		goto dropwithreset_ratelim;
	if (tp->t_state == TCPS_CLOSED)
		goto drop;

	/* Unscale the window into a 32-bit value. */
	if ((tiflags & TH_SYN) == 0)
		tiwin = th->th_win << tp->snd_scale;
	else
		tiwin = th->th_win;

	if (so->so_options & (SO_DEBUG|SO_ACCEPTCONN)) {
		union syn_cache_sa src;
		union syn_cache_sa dst;

		bzero(&src, sizeof(src));
		bzero(&dst, sizeof(dst));
		switch (af) {
		case AF_INET:
			src.sin.sin_len = sizeof(struct sockaddr_in);
			src.sin.sin_family = AF_INET;
			src.sin.sin_addr = ip->ip_src;
			src.sin.sin_port = th->th_sport;

			dst.sin.sin_len = sizeof(struct sockaddr_in);
			dst.sin.sin_family = AF_INET;
			dst.sin.sin_addr = ip->ip_dst;
			dst.sin.sin_port = th->th_dport;
			break;
#ifdef INET6
		case AF_INET6:
			src.sin6.sin6_len = sizeof(struct sockaddr_in6);
			src.sin6.sin6_family = AF_INET6;
			src.sin6.sin6_addr = ip6->ip6_src;
			src.sin6.sin6_port = th->th_sport;

			dst.sin6.sin6_len = sizeof(struct sockaddr_in6);
			dst.sin6.sin6_family = AF_INET6;
			dst.sin6.sin6_addr = ip6->ip6_dst;
			dst.sin6.sin6_port = th->th_dport;
			break;
#endif /* INET6 */
		}

		if (so->so_options & SO_DEBUG) {
			otp = tp;
			ostate = tp->t_state;
			switch (af) {
#ifdef INET6
			case AF_INET6:
				saveti.tcpip6.ti6_i = *ip6;
				saveti.tcpip6.ti6_t = *th;
				break;
#endif
			case AF_INET:
				memcpy(&saveti.tcpip.ti_i, ip, sizeof(*ip));
				saveti.tcpip.ti_t = *th;
				break;
			}
		}
		if (so->so_options & SO_ACCEPTCONN) {
			switch (tiflags & (TH_RST|TH_SYN|TH_ACK)) {

			case TH_SYN|TH_ACK|TH_RST:
			case TH_SYN|TH_RST:
			case TH_ACK|TH_RST:
			case TH_RST:
				syn_cache_reset(&src.sa, &dst.sa, th,
				    inp->inp_rtableid);
				goto drop;

			case TH_SYN|TH_ACK:
				/*
				 * Received a SYN,ACK.  This should
				 * never happen while we are in
				 * LISTEN.  Send an RST.
				 */
				goto badsyn;

			case TH_ACK:
				so = syn_cache_get(&src.sa, &dst.sa,
				    th, iphlen, tlen, so, m, now, do_ecn);
				if (so == NULL) {
					/*
					 * We don't have a SYN for
					 * this ACK; send an RST.
					 */
					goto badsyn;
				} else if (so == (struct socket *)(-1)) {
					/*
					 * We were unable to create
					 * the connection.  If the
					 * 3-way handshake was
					 * completed, and RST has
					 * been sent to the peer.
					 * Since the mbuf might be
					 * in use for the reply,
					 * do not free it.
					 */
					so = NULL;
					m = *mp = NULL;
					goto drop;
				} else {
					/*
					 * We have created a
					 * full-blown connection.
					 */
					in_pcbunref(inp);
					/* syn_cache_get() has refcounted inp */
					inp = sotoinpcb(so);
					tp = intotcpcb(inp);
					if (tp == NULL)
						goto badsyn;	/*XXX*/
				}
				break;

			default:
				/*
				 * None of RST, SYN or ACK was set.
				 * This is an invalid packet for a
				 * TCB in LISTEN state.  Send a RST.
				 */
				goto badsyn;

			case TH_SYN:
				/*
				 * Received a SYN.
				 */

				/*
				 * LISTEN socket received a SYN
				 * from itself?  This can't possibly
				 * be valid; drop the packet.
				 */
				if (th->th_dport == th->th_sport) {
					switch (af) {
#ifdef INET6
					case AF_INET6:
						if (IN6_ARE_ADDR_EQUAL(&ip6->ip6_src,
						    &ip6->ip6_dst)) {
							tcpstat_inc(tcps_badsyn);
							goto drop;
						}
						break;
#endif /* INET6 */
					case AF_INET:
						if (ip->ip_dst.s_addr == ip->ip_src.s_addr) {
							tcpstat_inc(tcps_badsyn);
							goto drop;
						}
						break;
					}
				}

				/*
				 * SYN looks ok; create compressed TCP
				 * state for it.
				 */
				if (so->so_qlen > so->so_qlimit ||
				    syn_cache_add(&src.sa, &dst.sa, th, iphlen,
				    so, m, optp, optlen, &opti, reuse, now,
				    do_ecn) == -1) {
					tcpstat_inc(tcps_dropsyn);
					goto drop;
				}
				if (solocked != NULL)
					*solocked = so;
				else
					in_pcbsounlock(inp, so);
				in_pcbunref(inp);
				return IPPROTO_DONE;
			}
		}
	}

#ifdef DIAGNOSTIC
	/*
	 * Should not happen now that all embryonic connections
	 * are handled with compressed state.
	 */
	if (tp->t_state == TCPS_LISTEN)
		panic("tcp_input: TCPS_LISTEN");
#endif

#if NPF > 0
	pf_inp_link(m, inp);
#endif

	/*
	 * Segment received on connection.
	 * Reset idle time and keep-alive timer.
	 */
	tp->t_rcvtime = now;
	if (TCPS_HAVEESTABLISHED(tp->t_state))
		TCP_TIMER_ARM(tp, TCPT_KEEP, atomic_load_int(&tcp_keepidle));

	if (tp->sack_enable)
		tcp_del_sackholes(tp, th); /* Delete stale SACK holes */

	/*
	 * Process options.
	 */
	if (optp
#ifdef TCP_SIGNATURE
	    || (tp->t_flags & TF_SIGNATURE)
#endif
	    ) {
		if (tcp_dooptions(tp, optp, optlen, th, m, iphlen, &opti,
		    m->m_pkthdr.ph_rtableid, now))
			goto drop;
	}

	if (opti.ts_present && opti.ts_ecr) {
		int32_t rtt_test;

		/* subtract out the tcp timestamp modulator */
		opti.ts_ecr -= tp->ts_modulate;

		/* make sure ts_ecr is sensible */
		rtt_test = now - opti.ts_ecr;
		if (rtt_test < 0 || rtt_test > TCP_RTT_MAX)
			opti.ts_ecr = 0;
	}

#ifdef TCP_ECN
	/* if congestion experienced, set ECE bit in subsequent packets. */
	if ((iptos & IPTOS_ECN_MASK) == IPTOS_ECN_CE) {
		tp->t_flags |= TF_RCVD_CE;
		tcpstat_inc(tcps_ecn_rcvce);
	}
#endif
	/*
	 * Header prediction: check for the two common cases
	 * of a uni-directional data xfer.  If the packet has
	 * no control flags, is in-sequence, the window didn't
	 * change and we're not retransmitting, it's a
	 * candidate.  If the length is zero and the ack moved
	 * forward, we're the sender side of the xfer.  Just
	 * free the data acked & wake any higher level process
	 * that was blocked waiting for space.  If the length
	 * is non-zero and the ack didn't move, we're the
	 * receiver side.  If we're getting packets in-order
	 * (the reassembly queue is empty), add the data to
	 * the socket buffer and note that we need a delayed ack.
	 */
	if (tp->t_state == TCPS_ESTABLISHED &&
#ifdef TCP_ECN
	    (tiflags & (TH_SYN|TH_FIN|TH_RST|TH_URG|TH_ECE|TH_CWR|TH_ACK)) == TH_ACK &&
#else
	    (tiflags & (TH_SYN|TH_FIN|TH_RST|TH_URG|TH_ACK)) == TH_ACK &&
#endif
	    (!opti.ts_present || TSTMP_GEQ(opti.ts_val, tp->ts_recent)) &&
	    th->th_seq == tp->rcv_nxt &&
	    tiwin && tiwin == tp->snd_wnd &&
	    tp->snd_nxt == tp->snd_max) {

		/*
		 * If last ACK falls within this segment's sequence numbers,
		 *  record the timestamp.
		 * Fix from Braden, see Stevens p. 870
		 */
		if (opti.ts_present && SEQ_LEQ(th->th_seq, tp->last_ack_sent)) {
			tp->ts_recent_age = now;
			tp->ts_recent = opti.ts_val;
		}

		if (tlen == 0) {
			if (SEQ_GT(th->th_ack, tp->snd_una) &&
			    SEQ_LEQ(th->th_ack, tp->snd_max) &&
			    tp->snd_cwnd >= tp->snd_wnd &&
			    tp->t_dupacks == 0) {
				/*
				 * this is a pure ack for outstanding data.
				 */
				tcpstat_inc(tcps_predack);
				if (opti.ts_present && opti.ts_ecr)
					tcp_xmit_timer(tp, now - opti.ts_ecr);
				else if (tp->t_rtttime &&
				    SEQ_GT(th->th_ack, tp->t_rtseq))
					tcp_xmit_timer(tp, now - tp->t_rtttime);
				acked = th->th_ack - tp->snd_una;
				tcpstat_pkt(tcps_rcvackpack, tcps_rcvackbyte,
				    acked);
				tp->t_rcvacktime = now;

				mtx_enter(&so->so_snd.sb_mtx);
				sbdrop(&so->so_snd, acked);
				mtx_leave(&so->so_snd.sb_mtx);

				/*
				 * If we had a pending ICMP message that
				 * refers to data that have just been
				 * acknowledged, disregard the recorded ICMP
				 * message.
				 */
				if ((tp->t_flags & TF_PMTUD_PEND) &&
				    SEQ_GT(th->th_ack, tp->t_pmtud_th_seq))
					tp->t_flags &= ~TF_PMTUD_PEND;

				/*
				 * Keep track of the largest chunk of data
				 * acknowledged since last PMTU update
				 */
				if (tp->t_pmtud_mss_acked < acked)
					tp->t_pmtud_mss_acked = acked;

				tp->snd_una = th->th_ack;
				/* Pull snd_wl2 up to prevent seq wrap. */
				tp->snd_wl2 = th->th_ack;
				/*
				 * We want snd_last to track snd_una so
				 * as to avoid sequence wraparound problems
				 * for very large transfers.
				 */
#ifdef TCP_ECN
				if (SEQ_GT(tp->snd_una, tp->snd_last))
#endif
				tp->snd_last = tp->snd_una;
				m_freem(m);

				/*
				 * If all outstanding data are acked, stop
				 * retransmit timer, otherwise restart timer
				 * using current (possibly backed-off) value.
				 * If process is waiting for space,
				 * wakeup/selwakeup/signal.  If data
				 * are ready to send, let tcp_output
				 * decide between more output or persist.
				 */
				if (tp->snd_una == tp->snd_max)
					TCP_TIMER_DISARM(tp, TCPT_REXMT);
				else if (TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0)
					TCP_TIMER_ARM(tp, TCPT_REXMT, tp->t_rxtcur);

				tcp_update_sndspace(tp);
				if (sb_notify(&so->so_snd))
					sowwakeup(so);
				if (so->so_snd.sb_cc ||
				    tp->t_flags & TF_NEEDOUTPUT)
					(void) tcp_output(tp);
				if (solocked != NULL)
					*solocked = so;
				else
					in_pcbsounlock(inp, so);
				in_pcbunref(inp);
				return IPPROTO_DONE;
			}
		} else if (th->th_ack == tp->snd_una &&
		    TAILQ_EMPTY(&tp->t_segq) &&
		    tlen <= sbspace(&so->so_rcv)) {
			/*
			 * This is a pure, in-sequence data packet
			 * with nothing on the reassembly queue and
			 * we have enough buffer space to take it.
			 */
			/* Clean receiver SACK report if present */
			if (tp->sack_enable && tp->rcv_numsacks)
				tcp_clean_sackreport(tp);
			tcpstat_inc(tcps_preddat);
			tp->rcv_nxt += tlen;
			/* Pull snd_wl1 and rcv_up up to prevent seq wrap. */
			tp->snd_wl1 = th->th_seq;
			/* Packet has most recent segment, no urgent exists. */
			tp->rcv_up = tp->rcv_nxt;
			tcpstat_pkt(tcps_rcvpack, tcps_rcvbyte, tlen);

			TCP_SETUP_ACK(tp, tiflags, m);
			/*
			 * Drop TCP, IP headers and TCP options then add data
			 * to socket buffer.
			 */
			if (so->so_rcv.sb_state & SS_CANTRCVMORE)
				m_freem(m);
			else {
				if (tp->t_srtt != 0 && tp->rfbuf_ts != 0 &&
				    now - tp->rfbuf_ts > (tp->t_srtt >>
				    (TCP_RTT_SHIFT + TCP_RTT_BASE_SHIFT))) {
					tcp_update_rcvspace(tp);
					/* Start over with next RTT. */
					tp->rfbuf_cnt = 0;
					tp->rfbuf_ts = 0;
				} else
					tp->rfbuf_cnt += tlen;
				m_adj(m, iphlen + off);
				mtx_enter(&so->so_rcv.sb_mtx);
				sbappendstream(&so->so_rcv, m);
				mtx_leave(&so->so_rcv.sb_mtx);
			}
			sorwakeup(so);
			if (tp->t_flags & (TF_ACKNOW|TF_NEEDOUTPUT))
				(void) tcp_output(tp);
			if (solocked != NULL)
				*solocked = so;
			else
				in_pcbsounlock(inp, so);
			in_pcbunref(inp);
			return IPPROTO_DONE;
		}
	}

	/*
	 * Compute mbuf offset to TCP data segment.
	 */
	hdroptlen = iphlen + off;

	/*
	 * Calculate amount of space in receive window,
	 * and then do TCP input processing.
	 * Receive window is amount of space in rcv queue,
	 * but not less than advertised window.
	 */
	{
		int win;

		win = sbspace(&so->so_rcv);
		if (win < 0)
			win = 0;
		tp->rcv_wnd = imax(win, (int)(tp->rcv_adv - tp->rcv_nxt));
	}

	switch (tp->t_state) {

	/*
	 * If the state is SYN_RECEIVED:
	 *	if seg contains SYN/ACK, send an RST.
	 *	if seg contains an ACK, but not for our SYN/ACK, send an RST
	 */

	case TCPS_SYN_RECEIVED:
		if (tiflags & TH_ACK) {
			if (tiflags & TH_SYN) {
				tcpstat_inc(tcps_badsyn);
				goto dropwithreset;
			}
			if (SEQ_LEQ(th->th_ack, tp->snd_una) ||
			    SEQ_GT(th->th_ack, tp->snd_max))
				goto dropwithreset;
		}
		break;

	/*
	 * If the state is SYN_SENT:
	 *	if seg contains an ACK, but not for our SYN, drop the input.
	 *	if seg contains a RST, then drop the connection.
	 *	if seg does not contain SYN, then drop it.
	 * Otherwise this is an acceptable SYN segment
	 *	initialize tp->rcv_nxt and tp->irs
	 *	if seg contains ack then advance tp->snd_una
	 *	if SYN has been acked change to ESTABLISHED else SYN_RCVD state
	 *	arrange for segment to be acked (eventually)
	 *	continue processing rest of data/controls, beginning with URG
	 */
	case TCPS_SYN_SENT:
		if ((tiflags & TH_ACK) &&
		    (SEQ_LEQ(th->th_ack, tp->iss) ||
		     SEQ_GT(th->th_ack, tp->snd_max)))
			goto dropwithreset;
		if (tiflags & TH_RST) {
#ifdef TCP_ECN
			/* if ECN is enabled, fall back to non-ecn at rexmit */
			if (do_ecn && !(tp->t_flags & TF_DISABLE_ECN))
				goto drop;
#endif
			if (tiflags & TH_ACK)
				tp = tcp_drop(tp, ECONNREFUSED);
			goto drop;
		}
		if ((tiflags & TH_SYN) == 0)
			goto drop;
		if (tiflags & TH_ACK) {
			tp->snd_una = th->th_ack;
			if (SEQ_LT(tp->snd_nxt, tp->snd_una))
				tp->snd_nxt = tp->snd_una;
		}
		TCP_TIMER_DISARM(tp, TCPT_REXMT);
		tp->irs = th->th_seq;
		tcp_mss(tp, opti.maxseg);
		/* Reset initial window to 1 segment for retransmit */
		if (tp->t_rxtshift > 0)
			tp->snd_cwnd = tp->t_maxseg;
		tcp_rcvseqinit(tp);
		tp->t_flags |= TF_ACKNOW;
		/*
		 * If we've sent a SACK_PERMITTED option, and the peer
		 * also replied with one, then TF_SACK_PERMIT should have
		 * been set in tcp_dooptions().  If it was not, disable SACKs.
		 */
		if (tp->sack_enable)
			tp->sack_enable = tp->t_flags & TF_SACK_PERMIT;
#ifdef TCP_ECN
		/*
		 * if ECE is set but CWR is not set for SYN-ACK, or
		 * both ECE and CWR are set for simultaneous open,
		 * peer is ECN capable.
		 */
		if (do_ecn) {
			switch (tiflags & (TH_ACK|TH_ECE|TH_CWR)) {
			case TH_ACK|TH_ECE:
			case TH_ECE|TH_CWR:
				tp->t_flags |= TF_ECN_PERMIT;
				tiflags &= ~(TH_ECE|TH_CWR);
				tcpstat_inc(tcps_ecn_accepts);
			}
		}
#endif

		if (tiflags & TH_ACK && SEQ_GT(tp->snd_una, tp->iss)) {
			tcpstat_inc(tcps_connects);
			soisconnected(so);
			tp->t_state = TCPS_ESTABLISHED;
			TCP_TIMER_ARM(tp, TCPT_KEEP,
			    atomic_load_int(&tcp_keepidle));
			/* Do window scaling on this connection? */
			if ((tp->t_flags & (TF_RCVD_SCALE|TF_REQ_SCALE)) ==
				(TF_RCVD_SCALE|TF_REQ_SCALE)) {
				tp->snd_scale = tp->requested_s_scale;
				tp->rcv_scale = tp->request_r_scale;
			}
			tcp_flush_queue(tp);

			/*
			 * if we didn't have to retransmit the SYN,
			 * use its rtt as our initial srtt & rtt var.
			 */
			if (tp->t_rtttime)
				tcp_xmit_timer(tp, now - tp->t_rtttime);
			/*
			 * Since new data was acked (the SYN), open the
			 * congestion window by one MSS.  We do this
			 * here, because we won't go through the normal
			 * ACK processing below.  And since this is the
			 * start of the connection, we know we are in
			 * the exponential phase of slow-start.
			 */
			tp->snd_cwnd += tp->t_maxseg;
		} else
			tp->t_state = TCPS_SYN_RECEIVED;

#if 0
trimthenstep6:
#endif
		/*
		 * Advance th->th_seq to correspond to first data byte.
		 * If data, trim to stay within window,
		 * dropping FIN if necessary.
		 */
		th->th_seq++;
		if (tlen > tp->rcv_wnd) {
			todrop = tlen - tp->rcv_wnd;
			m_adj(m, -todrop);
			tlen = tp->rcv_wnd;
			tiflags &= ~TH_FIN;
			tcpstat_pkt(tcps_rcvpackafterwin, tcps_rcvbyteafterwin,
			    todrop);
		}
		tp->snd_wl1 = th->th_seq - 1;
		tp->rcv_up = th->th_seq;
		goto step6;
	/*
	 * If a new connection request is received while in TIME_WAIT,
	 * drop the old connection and start over if the if the
	 * timestamp or the sequence numbers are above the previous
	 * ones.
	 */
	case TCPS_TIME_WAIT:
		if (((tiflags & (TH_SYN|TH_ACK)) == TH_SYN) &&
		    ((opti.ts_present &&
		    TSTMP_LT(tp->ts_recent, opti.ts_val)) ||
		    SEQ_GT(th->th_seq, tp->rcv_nxt))) {
#if NPF > 0
			/*
			 * The socket will be recreated but the new state
			 * has already been linked to the socket.  Remove the
			 * link between old socket and new state.
			 */
			pf_inp_unlink(inp);
#endif
			/*
			* Advance the iss by at least 32768, but
			* clear the msb in order to make sure
			* that SEG_LT(snd_nxt, iss).
			*/
			iss = tp->snd_nxt +
			    ((arc4random() & 0x7fffffff) | 0x8000);
			reuse = &iss;
			tp = tcp_close(tp);
			in_pcbsounlock(inp, so);
			so = NULL;
			in_pcbunref(inp);
			inp = NULL;
			goto findpcb;
		}
	}

	/*
	 * States other than LISTEN or SYN_SENT.
	 * First check timestamp, if present.
	 * Then check that at least some bytes of segment are within
	 * receive window.  If segment begins before rcv_nxt,
	 * drop leading data (and SYN); if nothing left, just ack.
	 *
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment
	 * and it's less than opti.ts_recent, drop it.
	 */
	if (opti.ts_present && (tiflags & TH_RST) == 0 && tp->ts_recent &&
	    TSTMP_LT(opti.ts_val, tp->ts_recent)) {

		/* Check to see if ts_recent is over 24 days old.  */
		if (now - tp->ts_recent_age > TCP_PAWS_IDLE) {
			/*
			 * Invalidate ts_recent.  If this segment updates
			 * ts_recent, the age will be reset later and ts_recent
			 * will get a valid value.  If it does not, setting
			 * ts_recent to zero will at least satisfy the
			 * requirement that zero be placed in the timestamp
			 * echo reply when ts_recent isn't valid.  The
			 * age isn't reset until we get a valid ts_recent
			 * because we don't want out-of-order segments to be
			 * dropped when ts_recent is old.
			 */
			tp->ts_recent = 0;
		} else {
			tcpstat_pkt(tcps_rcvduppack, tcps_rcvdupbyte, tlen);
			tcpstat_inc(tcps_pawsdrop);
			if (tlen)
				goto dropafterack;
			goto drop;
		}
	}

	todrop = tp->rcv_nxt - th->th_seq;
	if (todrop > 0) {
		if (tiflags & TH_SYN) {
			tiflags &= ~TH_SYN;
			th->th_seq++;
			if (th->th_urp > 1)
				th->th_urp--;
			else
				tiflags &= ~TH_URG;
			todrop--;
		}
		if (todrop > tlen ||
		    (todrop == tlen && (tiflags & TH_FIN) == 0)) {
			/*
			 * Any valid FIN must be to the left of the
			 * window.  At this point, FIN must be a
			 * duplicate or out-of-sequence, so drop it.
			 */
			tiflags &= ~TH_FIN;
			/*
			 * Send ACK to resynchronize, and drop any data,
			 * but keep on processing for RST or ACK.
			 */
			tp->t_flags |= TF_ACKNOW;
			todrop = tlen;
			tcpstat_pkt(tcps_rcvduppack, tcps_rcvdupbyte, todrop);
		} else {
			tcpstat_pkt(tcps_rcvpartduppack, tcps_rcvpartdupbyte,
			    todrop);
		}
		hdroptlen += todrop;	/* drop from head afterwards */
		th->th_seq += todrop;
		tlen -= todrop;
		if (th->th_urp > todrop)
			th->th_urp -= todrop;
		else {
			tiflags &= ~TH_URG;
			th->th_urp = 0;
		}
	}

	/*
	 * If new data are received on a connection after the
	 * user processes are gone, then RST the other end.
	 */
	if ((so->so_state & SS_NOFDREF) &&
	    tp->t_state > TCPS_CLOSE_WAIT && tlen) {
		tp = tcp_close(tp);
		tcpstat_inc(tcps_rcvafterclose);
		goto dropwithreset;
	}

	/*
	 * If segment ends after window, drop trailing data
	 * (and PUSH and FIN); if nothing left, just ACK.
	 */
	todrop = (th->th_seq + tlen) - (tp->rcv_nxt+tp->rcv_wnd);
	if (todrop > 0) {
		tcpstat_inc(tcps_rcvpackafterwin);
		if (todrop >= tlen) {
			tcpstat_add(tcps_rcvbyteafterwin, tlen);
			/*
			 * If window is closed can only take segments at
			 * window edge, and have to drop data and PUSH from
			 * incoming segments.  Continue processing, but
			 * remember to ack.  Otherwise, drop segment
			 * and ack.
			 */
			if (tp->rcv_wnd == 0 && th->th_seq == tp->rcv_nxt) {
				tp->t_flags |= TF_ACKNOW;
				tcpstat_inc(tcps_rcvwinprobe);
			} else
				goto dropafterack;
		} else
			tcpstat_add(tcps_rcvbyteafterwin, todrop);
		m_adj(m, -todrop);
		tlen -= todrop;
		tiflags &= ~(TH_PUSH|TH_FIN);
	}

	/*
	 * If last ACK falls within this segment's sequence numbers,
	 * record its timestamp if it's more recent.
	 * NOTE that the test is modified according to the latest
	 * proposal of the tcplw@cray.com list (Braden 1993/04/26).
	 */
	if (opti.ts_present && TSTMP_GEQ(opti.ts_val, tp->ts_recent) &&
	    SEQ_LEQ(th->th_seq, tp->last_ack_sent)) {
		tp->ts_recent_age = now;
		tp->ts_recent = opti.ts_val;
	}

	/*
	 * If the RST bit is set examine the state:
	 *    SYN_RECEIVED STATE:
	 *	If passive open, return to LISTEN state.
	 *	If active open, inform user that connection was refused.
	 *    ESTABLISHED, FIN_WAIT_1, FIN_WAIT2, CLOSE_WAIT STATES:
	 *	Inform user that connection was reset, and close tcb.
	 *    CLOSING, LAST_ACK, TIME_WAIT STATES
	 *	Close the tcb.
	 */
	if (tiflags & TH_RST) {
		if (th->th_seq != tp->last_ack_sent &&
		    th->th_seq != tp->rcv_nxt &&
		    th->th_seq != (tp->rcv_nxt + 1))
			goto drop;

		switch (tp->t_state) {
		case TCPS_SYN_RECEIVED:
#ifdef TCP_ECN
			/* if ECN is enabled, fall back to non-ecn at rexmit */
			if (do_ecn && !(tp->t_flags & TF_DISABLE_ECN))
				goto drop;
#endif
			so->so_error = ECONNREFUSED;
			goto close;

		case TCPS_ESTABLISHED:
		case TCPS_FIN_WAIT_1:
		case TCPS_FIN_WAIT_2:
		case TCPS_CLOSE_WAIT:
			so->so_error = ECONNRESET;
		close:
			tp->t_state = TCPS_CLOSED;
			tcpstat_inc(tcps_drops);
			tp = tcp_close(tp);
			goto drop;
		case TCPS_CLOSING:
		case TCPS_LAST_ACK:
		case TCPS_TIME_WAIT:
			tp = tcp_close(tp);
			goto drop;
		}
	}

	/*
	 * If a SYN is in the window, then this is an
	 * error and we ACK and drop the packet.
	 */
	if (tiflags & TH_SYN)
		goto dropafterack_ratelim;

	/*
	 * If the ACK bit is off we drop the segment and return.
	 */
	if ((tiflags & TH_ACK) == 0) {
		if (tp->t_flags & TF_ACKNOW)
			goto dropafterack;
		else
			goto drop;
	}

	/*
	 * Ack processing.
	 */
	switch (tp->t_state) {

	/*
	 * In SYN_RECEIVED state, the ack ACKs our SYN, so enter
	 * ESTABLISHED state and continue processing.
	 * The ACK was checked above.
	 */
	case TCPS_SYN_RECEIVED:
		tcpstat_inc(tcps_connects);
		soisconnected(so);
		tp->t_state = TCPS_ESTABLISHED;
		TCP_TIMER_ARM(tp, TCPT_KEEP, atomic_load_int(&tcp_keepidle));
		/* Do window scaling? */
		if ((tp->t_flags & (TF_RCVD_SCALE|TF_REQ_SCALE)) ==
			(TF_RCVD_SCALE|TF_REQ_SCALE)) {
			tp->snd_scale = tp->requested_s_scale;
			tp->rcv_scale = tp->request_r_scale;
			tiwin = th->th_win << tp->snd_scale;
		}
		tcp_flush_queue(tp);
		tp->snd_wl1 = th->th_seq - 1;
		/* fall into ... */

	/*
	 * In ESTABLISHED state: drop duplicate ACKs; ACK out of range
	 * ACKs.  If the ack is in the range
	 *	tp->snd_una < th->th_ack <= tp->snd_max
	 * then advance tp->snd_una to th->th_ack and drop
	 * data from the retransmission queue.  If this ACK reflects
	 * more up to date window information we update our window information.
	 */
	case TCPS_ESTABLISHED:
	case TCPS_FIN_WAIT_1:
	case TCPS_FIN_WAIT_2:
	case TCPS_CLOSE_WAIT:
	case TCPS_CLOSING:
	case TCPS_LAST_ACK:
	case TCPS_TIME_WAIT:
#ifdef TCP_ECN
		/*
		 * if we receive ECE and are not already in recovery phase,
		 * reduce cwnd by half but don't slow-start.
		 * advance snd_last to snd_max not to reduce cwnd again
		 * until all outstanding packets are acked.
		 */
		if (do_ecn && (tiflags & TH_ECE)) {
			if ((tp->t_flags & TF_ECN_PERMIT) &&
			    SEQ_GEQ(tp->snd_una, tp->snd_last)) {
				u_int win;

				win = min(tp->snd_wnd, tp->snd_cwnd) / tp->t_maxseg;
				if (win > 1) {
					tp->snd_ssthresh = win / 2 * tp->t_maxseg;
					tp->snd_cwnd = tp->snd_ssthresh;
					tp->snd_last = tp->snd_max;
					tp->t_flags |= TF_SEND_CWR;
					tcpstat_inc(tcps_cwr_ecn);
				}
			}
			tcpstat_inc(tcps_ecn_rcvece);
		}
		/*
		 * if we receive CWR, we know that the peer has reduced
		 * its congestion window.  stop sending ecn-echo.
		 */
		if ((tiflags & TH_CWR)) {
			tp->t_flags &= ~TF_RCVD_CE;
			tcpstat_inc(tcps_ecn_rcvcwr);
		}
#endif /* TCP_ECN */

		if (SEQ_LEQ(th->th_ack, tp->snd_una)) {
			/*
			 * Duplicate/old ACK processing.
			 * Increments t_dupacks:
			 *	Pure duplicate (same seq/ack/window, no data)
			 * Doesn't affect t_dupacks:
			 *	Data packets.
			 *	Normal window updates (window opens)
			 * Resets t_dupacks:
			 *	New data ACKed.
			 *	Window shrinks
			 *	Old ACK
			 */
			if (tlen) {
				/* Drop very old ACKs unless th_seq matches */
				if (th->th_seq != tp->rcv_nxt &&
				   SEQ_LT(th->th_ack,
				   tp->snd_una - tp->max_sndwnd)) {
					tcpstat_inc(tcps_rcvacktooold);
					goto drop;
				}
				break;
			}
			/*
			 * If we get an old ACK, there is probably packet
			 * reordering going on.  Be conservative and reset
			 * t_dupacks so that we are less aggressive in
			 * doing a fast retransmit.
			 */
			if (th->th_ack != tp->snd_una) {
				tp->t_dupacks = 0;
				break;
			}
			if (tiwin == tp->snd_wnd) {
				tcpstat_inc(tcps_rcvdupack);
				/*
				 * If we have outstanding data (other than
				 * a window probe), this is a completely
				 * duplicate ack (ie, window info didn't
				 * change), the ack is the biggest we've
				 * seen and we've seen exactly our rexmt
				 * threshold of them, assume a packet
				 * has been dropped and retransmit it.
				 * Kludge snd_nxt & the congestion
				 * window so we send only this one
				 * packet.
				 *
				 * We know we're losing at the current
				 * window size so do congestion avoidance
				 * (set ssthresh to half the current window
				 * and pull our congestion window back to
				 * the new ssthresh).
				 *
				 * Dup acks mean that packets have left the
				 * network (they're now cached at the receiver)
				 * so bump cwnd by the amount in the receiver
				 * to keep a constant cwnd packets in the
				 * network.
				 */
				if (TCP_TIMER_ISARMED(tp, TCPT_REXMT) == 0)
					tp->t_dupacks = 0;
				else if (++tp->t_dupacks == tcprexmtthresh) {
					tcp_seq onxt = tp->snd_nxt;
					u_long win =
					    ulmin(tp->snd_wnd, tp->snd_cwnd) /
						2 / tp->t_maxseg;

					if (SEQ_LT(th->th_ack, tp->snd_last)){
						/*
						 * False fast retx after
						 * timeout.  Do not cut window.
						 */
						tp->t_dupacks = 0;
						goto drop;
					}
					if (win < 2)
						win = 2;
					tp->snd_ssthresh = win * tp->t_maxseg;
					tp->snd_last = tp->snd_max;
					if (tp->sack_enable) {
						TCP_TIMER_DISARM(tp, TCPT_REXMT);
						tp->t_rtttime = 0;
#ifdef TCP_ECN
						tp->t_flags |= TF_SEND_CWR;
#endif
						tcpstat_inc(tcps_cwr_frecovery);
						tcpstat_inc(tcps_sack_recovery_episode);
						/*
						 * tcp_output() will send
						 * oldest SACK-eligible rtx.
						 */
						(void) tcp_output(tp);
						tp->snd_cwnd = tp->snd_ssthresh+
						   tp->t_maxseg * tp->t_dupacks;
						goto drop;
					}
					TCP_TIMER_DISARM(tp, TCPT_REXMT);
					tp->t_rtttime = 0;
					tp->snd_nxt = th->th_ack;
					tp->snd_cwnd = tp->t_maxseg;
#ifdef TCP_ECN
					tp->t_flags |= TF_SEND_CWR;
#endif
					tcpstat_inc(tcps_cwr_frecovery);
					tcpstat_inc(tcps_sndrexmitfast);
					(void) tcp_output(tp);

					tp->snd_cwnd = tp->snd_ssthresh +
					    tp->t_maxseg * tp->t_dupacks;
					if (SEQ_GT(onxt, tp->snd_nxt))
						tp->snd_nxt = onxt;
					goto drop;
				} else if (tp->t_dupacks > tcprexmtthresh) {
					tp->snd_cwnd += tp->t_maxseg;
					(void) tcp_output(tp);
					goto drop;
				}
			} else if (tiwin < tp->snd_wnd) {
				/*
				 * The window was retracted!  Previous dup
				 * ACKs may have been due to packets arriving
				 * after the shrunken window, not a missing
				 * packet, so play it safe and reset t_dupacks
				 */
				tp->t_dupacks = 0;
			}
			break;
		}
		/*
		 * If the congestion window was inflated to account
		 * for the other side's cached packets, retract it.
		 */
		if (tp->t_dupacks >= tcprexmtthresh) {
			/* Check for a partial ACK */
			if (SEQ_LT(th->th_ack, tp->snd_last)) {
				if (tp->sack_enable)
					tcp_sack_partialack(tp, th);
				else
					tcp_newreno_partialack(tp, th);
			} else {
				/* Out of fast recovery */
				tp->snd_cwnd = tp->snd_ssthresh;
				if (tcp_seq_subtract(tp->snd_max, th->th_ack) <
				    tp->snd_ssthresh)
					tp->snd_cwnd =
					    tcp_seq_subtract(tp->snd_max,
					    th->th_ack);
				tp->t_dupacks = 0;
			}
		} else {
			/*
			 * Reset the duplicate ACK counter if we
			 * were not in fast recovery.
			 */
			tp->t_dupacks = 0;
		}
		if (SEQ_GT(th->th_ack, tp->snd_max)) {
			tcpstat_inc(tcps_rcvacktoomuch);
			goto dropafterack_ratelim;
		}
		acked = th->th_ack - tp->snd_una;
		tcpstat_pkt(tcps_rcvackpack, tcps_rcvackbyte, acked);
		tp->t_rcvacktime = now;

		/*
		 * If we have a timestamp reply, update smoothed
		 * round trip time.  If no timestamp is present but
		 * transmit timer is running and timed sequence
		 * number was acked, update smoothed round trip time.
		 * Since we now have an rtt measurement, cancel the
		 * timer backoff (cf., Phil Karn's retransmit alg.).
		 * Recompute the initial retransmit timer.
		 */
		if (opti.ts_present && opti.ts_ecr)
			tcp_xmit_timer(tp, now - opti.ts_ecr);
		else if (tp->t_rtttime && SEQ_GT(th->th_ack, tp->t_rtseq))
			tcp_xmit_timer(tp, now - tp->t_rtttime);

		/*
		 * If all outstanding data is acked, stop retransmit
		 * timer and remember to restart (more output or persist).
		 * If there is more data to be acked, restart retransmit
		 * timer, using current (possibly backed-off) value.
		 */
		if (th->th_ack == tp->snd_max) {
			TCP_TIMER_DISARM(tp, TCPT_REXMT);
			tp->t_flags |= TF_NEEDOUTPUT;
		} else if (TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0)
			TCP_TIMER_ARM(tp, TCPT_REXMT, tp->t_rxtcur);
		/*
		 * When new data is acked, open the congestion window.
		 * If the window gives us less than ssthresh packets
		 * in flight, open exponentially (maxseg per packet).
		 * Otherwise open linearly: maxseg per window
		 * (maxseg^2 / cwnd per packet).
		 */
		{
		u_int cw = tp->snd_cwnd;
		u_int incr = tp->t_maxseg;

		if (cw > tp->snd_ssthresh)
			incr = max(incr * incr / cw, 1);
		if (tp->t_dupacks < tcprexmtthresh)
			tp->snd_cwnd = ulmin(cw + incr,
			    TCP_MAXWIN << tp->snd_scale);
		}
		if (acked > so->so_snd.sb_cc) {
			if (tp->snd_wnd > so->so_snd.sb_cc)
				tp->snd_wnd -= so->so_snd.sb_cc;
			else
				tp->snd_wnd = 0;
			mtx_enter(&so->so_snd.sb_mtx);
			sbdrop(&so->so_snd, (int)so->so_snd.sb_cc);
			mtx_leave(&so->so_snd.sb_mtx);
			ourfinisacked = 1;
		} else {
			mtx_enter(&so->so_snd.sb_mtx);
			sbdrop(&so->so_snd, acked);
			mtx_leave(&so->so_snd.sb_mtx);
			if (tp->snd_wnd > acked)
				tp->snd_wnd -= acked;
			else
				tp->snd_wnd = 0;
			ourfinisacked = 0;
		}

		tcp_update_sndspace(tp);
		if (sb_notify(&so->so_snd))
			sowwakeup(so);

		/*
		 * If we had a pending ICMP message that referred to data
		 * that have just been acknowledged, disregard the recorded
		 * ICMP message.
		 */
		if ((tp->t_flags & TF_PMTUD_PEND) &&
		    SEQ_GT(th->th_ack, tp->t_pmtud_th_seq))
			tp->t_flags &= ~TF_PMTUD_PEND;

		/*
		 * Keep track of the largest chunk of data acknowledged
		 * since last PMTU update
		 */
		if (tp->t_pmtud_mss_acked < acked)
			tp->t_pmtud_mss_acked = acked;

		tp->snd_una = th->th_ack;
#ifdef TCP_ECN
		/* sync snd_last with snd_una */
		if (SEQ_GT(tp->snd_una, tp->snd_last))
			tp->snd_last = tp->snd_una;
#endif
		if (SEQ_LT(tp->snd_nxt, tp->snd_una))
			tp->snd_nxt = tp->snd_una;

		switch (tp->t_state) {

		/*
		 * In FIN_WAIT_1 STATE in addition to the processing
		 * for the ESTABLISHED state if our FIN is now acknowledged
		 * then enter FIN_WAIT_2.
		 */
		case TCPS_FIN_WAIT_1:
			if (ourfinisacked) {
				/*
				 * If we can't receive any more
				 * data, then closing user can proceed.
				 * Starting the timer is contrary to the
				 * specification, but if we don't get a FIN
				 * we'll hang forever.
				 */
				if (so->so_rcv.sb_state & SS_CANTRCVMORE) {
					int maxidle;

					soisdisconnected(so);
					maxidle = TCPTV_KEEPCNT *
					    atomic_load_int(&tcp_keepidle);
					TCP_TIMER_ARM(tp, TCPT_2MSL, maxidle);
				}
				tp->t_state = TCPS_FIN_WAIT_2;
			}
			break;

		/*
		 * In CLOSING STATE in addition to the processing for
		 * the ESTABLISHED state if the ACK acknowledges our FIN
		 * then enter the TIME-WAIT state, otherwise ignore
		 * the segment.
		 */
		case TCPS_CLOSING:
			if (ourfinisacked) {
				tp->t_state = TCPS_TIME_WAIT;
				tcp_canceltimers(tp);
				TCP_TIMER_ARM(tp, TCPT_2MSL, 2 * TCPTV_MSL);
				soisdisconnected(so);
			}
			break;

		/*
		 * In LAST_ACK, we may still be waiting for data to drain
		 * and/or to be acked, as well as for the ack of our FIN.
		 * If our FIN is now acknowledged, delete the TCB,
		 * enter the closed state and return.
		 */
		case TCPS_LAST_ACK:
			if (ourfinisacked) {
				tp = tcp_close(tp);
				goto drop;
			}
			break;

		/*
		 * In TIME_WAIT state the only thing that should arrive
		 * is a retransmission of the remote FIN.  Acknowledge
		 * it and restart the finack timer.
		 */
		case TCPS_TIME_WAIT:
			TCP_TIMER_ARM(tp, TCPT_2MSL, 2 * TCPTV_MSL);
			goto dropafterack;
		}
	}

step6:
	/*
	 * Update window information.
	 * Don't look at window if no ACK: TAC's send garbage on first SYN.
	 */
	if ((tiflags & TH_ACK) &&
	    (SEQ_LT(tp->snd_wl1, th->th_seq) || (tp->snd_wl1 == th->th_seq &&
	    (SEQ_LT(tp->snd_wl2, th->th_ack) ||
	    (tp->snd_wl2 == th->th_ack && tiwin > tp->snd_wnd))))) {
		/* keep track of pure window updates */
		if (tlen == 0 &&
		    tp->snd_wl2 == th->th_ack && tiwin > tp->snd_wnd)
			tcpstat_inc(tcps_rcvwinupd);
		tp->snd_wnd = tiwin;
		tp->snd_wl1 = th->th_seq;
		tp->snd_wl2 = th->th_ack;
		if (tp->snd_wnd > tp->max_sndwnd)
			tp->max_sndwnd = tp->snd_wnd;
		tp->t_flags |= TF_NEEDOUTPUT;
	}

	/*
	 * Process segments with URG.
	 */
	if ((tiflags & TH_URG) && th->th_urp &&
	    TCPS_HAVERCVDFIN(tp->t_state) == 0) {
		u_long urgent;

		/*
		 * This is a kludge, but if we receive and accept
		 * random urgent pointers, we'll crash in
		 * soreceive.  It's hard to imagine someone
		 * actually wanting to send this much urgent data.
		 */
		mtx_enter(&so->so_rcv.sb_mtx);
		urgent = th->th_urp + so->so_rcv.sb_cc;
		mtx_leave(&so->so_rcv.sb_mtx);

		if (urgent > sb_max) {
			th->th_urp = 0;			/* XXX */
			tiflags &= ~TH_URG;		/* XXX */
			goto dodata;			/* XXX */
		}
		/*
		 * If this segment advances the known urgent pointer,
		 * then mark the data stream.  This should not happen
		 * in CLOSE_WAIT, CLOSING, LAST_ACK or TIME_WAIT STATES since
		 * a FIN has been received from the remote side.
		 * In these states we ignore the URG.
		 *
		 * According to RFC961 (Assigned Protocols),
		 * the urgent pointer points to the last octet
		 * of urgent data.  We continue, however,
		 * to consider it to indicate the first octet
		 * of data past the urgent section as the original
		 * spec states (in one of two places).
		 */
		if (SEQ_GT(th->th_seq+th->th_urp, tp->rcv_up)) {
			tp->rcv_up = th->th_seq + th->th_urp;
			mtx_enter(&so->so_rcv.sb_mtx);
			so->so_oobmark = so->so_rcv.sb_cc +
			    (tp->rcv_up - tp->rcv_nxt) - 1;
			if (so->so_oobmark == 0)
				so->so_rcv.sb_state |= SS_RCVATMARK;
			mtx_leave(&so->so_rcv.sb_mtx);
			sohasoutofband(so);
			tp->t_oobflags &= ~(TCPOOB_HAVEDATA | TCPOOB_HADDATA);
		}
		/*
		 * Remove out of band data so doesn't get presented to user.
		 * This can happen independent of advancing the URG pointer,
		 * but if two URG's are pending at once, some out-of-band
		 * data may creep in... ick.
		 */
		if (th->th_urp <= (u_int16_t) tlen &&
		    (so->so_options & SO_OOBINLINE) == 0)
			tcp_pulloutofband(so, th->th_urp, m, hdroptlen);
	} else
		/*
		 * If no out of band data is expected,
		 * pull receive urgent pointer along
		 * with the receive window.
		 */
		if (SEQ_GT(tp->rcv_nxt, tp->rcv_up))
			tp->rcv_up = tp->rcv_nxt;
dodata:							/* XXX */

	/*
	 * Process the segment text, merging it into the TCP sequencing queue,
	 * and arranging for acknowledgment of receipt if necessary.
	 * This process logically involves adjusting tp->rcv_wnd as data
	 * is presented to the user (this happens in tcp_usrreq.c,
	 * case PRU_RCVD).  If a FIN has already been received on this
	 * connection then we just ignore the text.
	 */
	if ((tlen || (tiflags & TH_FIN)) &&
	    TCPS_HAVERCVDFIN(tp->t_state) == 0) {
		tcp_seq laststart = th->th_seq;
		tcp_seq lastend = th->th_seq + tlen;

		if (th->th_seq == tp->rcv_nxt && TAILQ_EMPTY(&tp->t_segq) &&
		    tp->t_state == TCPS_ESTABLISHED) {
			TCP_SETUP_ACK(tp, tiflags, m);
			tp->rcv_nxt += tlen;
			tiflags = th->th_flags & TH_FIN;
			tcpstat_pkt(tcps_rcvpack, tcps_rcvbyte, tlen);
			if (so->so_rcv.sb_state & SS_CANTRCVMORE)
				m_freem(m);
			else {
				m_adj(m, hdroptlen);
				mtx_enter(&so->so_rcv.sb_mtx);
				sbappendstream(&so->so_rcv, m);
				mtx_leave(&so->so_rcv.sb_mtx);
			}
			sorwakeup(so);
		} else {
			m_adj(m, hdroptlen);
			tiflags = tcp_reass(tp, th, m, &tlen);
			tp->t_flags |= TF_ACKNOW;
		}
		if (tp->sack_enable)
			tcp_update_sack_list(tp, laststart, lastend);

		/*
		 * variable len never referenced again in modern BSD,
		 * so why bother computing it ??
		 */
#if 0
		/*
		 * Note the amount of data that peer has sent into
		 * our window, in order to estimate the sender's
		 * buffer size.
		 */
		len = so->so_rcv.sb_hiwat - (tp->rcv_adv - tp->rcv_nxt);
#endif /* 0 */
	} else {
		m_freem(m);
		tiflags &= ~TH_FIN;
	}

	/*
	 * If FIN is received ACK the FIN and let the user know
	 * that the connection is closing.  Ignore a FIN received before
	 * the connection is fully established.
	 */
	if ((tiflags & TH_FIN) && TCPS_HAVEESTABLISHED(tp->t_state)) {
		if (TCPS_HAVERCVDFIN(tp->t_state) == 0) {
			socantrcvmore(so);
			tp->t_flags |= TF_ACKNOW;
			tp->rcv_nxt++;
		}
		switch (tp->t_state) {

		/*
		 * In ESTABLISHED STATE enter the CLOSE_WAIT state.
		 */
		case TCPS_ESTABLISHED:
			tp->t_state = TCPS_CLOSE_WAIT;
			break;

		/*
		 * If still in FIN_WAIT_1 STATE FIN has not been acked so
		 * enter the CLOSING state.
		 */
		case TCPS_FIN_WAIT_1:
			tp->t_state = TCPS_CLOSING;
			break;

		/*
		 * In FIN_WAIT_2 state enter the TIME_WAIT state,
		 * starting the time-wait timer, turning off the other
		 * standard timers.
		 */
		case TCPS_FIN_WAIT_2:
			tp->t_state = TCPS_TIME_WAIT;
			tcp_canceltimers(tp);
			TCP_TIMER_ARM(tp, TCPT_2MSL, 2 * TCPTV_MSL);
			soisdisconnected(so);
			break;

		/*
		 * In TIME_WAIT state restart the 2 MSL time_wait timer.
		 */
		case TCPS_TIME_WAIT:
			TCP_TIMER_ARM(tp, TCPT_2MSL, 2 * TCPTV_MSL);
			break;
		}
	}
	if (otp)
		tcp_trace(TA_INPUT, ostate, tp, otp, &saveti.caddr, 0, tlen);

	/*
	 * Return any desired output.
	 */
	if (tp->t_flags & (TF_ACKNOW|TF_NEEDOUTPUT))
		(void) tcp_output(tp);
	if (solocked != NULL)
		*solocked = so;
	else
		in_pcbsounlock(inp, so);
	in_pcbunref(inp);
	return IPPROTO_DONE;

badsyn:
	/*
	 * Received a bad SYN.  Increment counters and dropwithreset.
	 */
	tcpstat_inc(tcps_badsyn);
	tp = NULL;
	goto dropwithreset;

dropafterack_ratelim:
	if (ppsratecheck(&tcp_ackdrop_ppslim_last, &tcp_ackdrop_ppslim_count,
	    tcp_ackdrop_ppslim) == 0) {
		/* XXX stat */
		goto drop;
	}
	/* ...fall into dropafterack... */

dropafterack:
	/*
	 * Generate an ACK dropping incoming segment if it occupies
	 * sequence space, where the ACK reflects our state.
	 */
	if (tiflags & TH_RST)
		goto drop;
	m_freem(m);
	tp->t_flags |= TF_ACKNOW;
	(void) tcp_output(tp);
	if (solocked != NULL)
		*solocked = so;
	else
		in_pcbsounlock(inp, so);
	in_pcbunref(inp);
	return IPPROTO_DONE;

dropwithreset_ratelim:
	/*
	 * We may want to rate-limit RSTs in certain situations,
	 * particularly if we are sending an RST in response to
	 * an attempt to connect to or otherwise communicate with
	 * a port for which we have no socket.
	 */
	if (ppsratecheck(&tcp_rst_ppslim_last, &tcp_rst_ppslim_count,
	    atomic_load_int(&tcp_rst_ppslim)) == 0) {
		/* XXX stat */
		goto drop;
	}
	/* ...fall into dropwithreset... */

dropwithreset:
	/*
	 * Generate a RST, dropping incoming segment.
	 * Make ACK acceptable to originator of segment.
	 * Don't bother to respond to RST.
	 */
	if (tiflags & TH_RST)
		goto drop;
	if (tiflags & TH_ACK) {
		tcp_respond(tp, mtod(m, caddr_t), th, (tcp_seq)0, th->th_ack,
		    TH_RST, m->m_pkthdr.ph_rtableid, now);
	} else {
		if (tiflags & TH_SYN)
			tlen++;
		tcp_respond(tp, mtod(m, caddr_t), th, th->th_seq + tlen,
		    (tcp_seq)0, TH_RST|TH_ACK, m->m_pkthdr.ph_rtableid, now);
	}
	m_freem(m);
	in_pcbsounlock(inp, so);
	in_pcbunref(inp);
	return IPPROTO_DONE;

drop:
	/*
	 * Drop space held by incoming segment and return.
	 */
	if (otp)
		tcp_trace(TA_DROP, ostate, tp, otp, &saveti.caddr, 0, tlen);

	m_freem(m);
	in_pcbsounlock(inp, so);
	in_pcbunref(inp);
	return IPPROTO_DONE;
}

int
tcp_dooptions(struct tcpcb *tp, u_char *cp, int cnt, struct tcphdr *th,
    struct mbuf *m, int iphlen, struct tcp_opt_info *oi,
    u_int rtableid, uint64_t now)
{
	u_int16_t mss = 0;
	int opt, optlen;
#ifdef TCP_SIGNATURE
	caddr_t sigp = NULL;
	struct tdb *tdb = NULL;
#endif

	for (; cp && cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[0];
		if (opt == TCPOPT_EOL)
			break;
		if (opt == TCPOPT_NOP)
			optlen = 1;
		else {
			if (cnt < 2)
				break;
			optlen = cp[1];
			if (optlen < 2 || optlen > cnt)
				break;
		}
		switch (opt) {

		default:
			continue;

		case TCPOPT_MAXSEG:
			if (optlen != TCPOLEN_MAXSEG)
				continue;
			if (!(th->th_flags & TH_SYN))
				continue;
			if (TCPS_HAVERCVDSYN(tp->t_state))
				continue;
			memcpy(&mss, cp + 2, sizeof(mss));
			mss = ntohs(mss);
			oi->maxseg = mss;
			break;

		case TCPOPT_WINDOW:
			if (optlen != TCPOLEN_WINDOW)
				continue;
			if (!(th->th_flags & TH_SYN))
				continue;
			if (TCPS_HAVERCVDSYN(tp->t_state))
				continue;
			tp->t_flags |= TF_RCVD_SCALE;
			tp->requested_s_scale = min(cp[2], TCP_MAX_WINSHIFT);
			break;

		case TCPOPT_TIMESTAMP:
			if (optlen != TCPOLEN_TIMESTAMP)
				continue;
			oi->ts_present = 1;
			memcpy(&oi->ts_val, cp + 2, sizeof(oi->ts_val));
			oi->ts_val = ntohl(oi->ts_val);
			memcpy(&oi->ts_ecr, cp + 6, sizeof(oi->ts_ecr));
			oi->ts_ecr = ntohl(oi->ts_ecr);

			if (!(th->th_flags & TH_SYN))
				continue;
			if (TCPS_HAVERCVDSYN(tp->t_state))
				continue;
			/*
			 * A timestamp received in a SYN makes
			 * it ok to send timestamp requests and replies.
			 */
			tp->t_flags |= TF_RCVD_TSTMP;
			tp->ts_recent = oi->ts_val;
			tp->ts_recent_age = now;
			break;

		case TCPOPT_SACK_PERMITTED:
			if (!tp->sack_enable || optlen!=TCPOLEN_SACK_PERMITTED)
				continue;
			if (!(th->th_flags & TH_SYN))
				continue;
			if (TCPS_HAVERCVDSYN(tp->t_state))
				continue;
			/* MUST only be set on SYN */
			tp->t_flags |= TF_SACK_PERMIT;
			break;
		case TCPOPT_SACK:
			tcp_sack_option(tp, th, cp, optlen);
			break;
#ifdef TCP_SIGNATURE
		case TCPOPT_SIGNATURE:
			if (optlen != TCPOLEN_SIGNATURE)
				continue;

			if (sigp && timingsafe_bcmp(sigp, cp + 2, 16))
				goto bad;

			sigp = cp + 2;
			break;
#endif /* TCP_SIGNATURE */
		}
	}

#ifdef TCP_SIGNATURE
	if (tp->t_flags & TF_SIGNATURE) {
		union sockaddr_union src, dst;

		memset(&src, 0, sizeof(union sockaddr_union));
		memset(&dst, 0, sizeof(union sockaddr_union));

		switch (tp->pf) {
		case 0:
		case AF_INET:
			src.sa.sa_len = sizeof(struct sockaddr_in);
			src.sa.sa_family = AF_INET;
			src.sin.sin_addr = mtod(m, struct ip *)->ip_src;
			dst.sa.sa_len = sizeof(struct sockaddr_in);
			dst.sa.sa_family = AF_INET;
			dst.sin.sin_addr = mtod(m, struct ip *)->ip_dst;
			break;
#ifdef INET6
		case AF_INET6:
			src.sa.sa_len = sizeof(struct sockaddr_in6);
			src.sa.sa_family = AF_INET6;
			src.sin6.sin6_addr = mtod(m, struct ip6_hdr *)->ip6_src;
			dst.sa.sa_len = sizeof(struct sockaddr_in6);
			dst.sa.sa_family = AF_INET6;
			dst.sin6.sin6_addr = mtod(m, struct ip6_hdr *)->ip6_dst;
			break;
#endif /* INET6 */
		}

		tdb = gettdbbysrcdst(rtable_l2(rtableid),
		    0, &src, &dst, IPPROTO_TCP);

		/*
		 * We don't have an SA for this peer, so we turn off
		 * TF_SIGNATURE on the listen socket
		 */
		if (tdb == NULL && tp->t_state == TCPS_LISTEN)
			tp->t_flags &= ~TF_SIGNATURE;

	}

	if ((sigp ? TF_SIGNATURE : 0) ^ (tp->t_flags & TF_SIGNATURE)) {
		tcpstat_inc(tcps_rcvbadsig);
		goto bad;
	}

	if (sigp) {
		char sig[16];

		if (tdb == NULL) {
			tcpstat_inc(tcps_rcvbadsig);
			goto bad;
		}

		if (tcp_signature(tdb, tp->pf, m, th, iphlen, 1, sig) < 0)
			goto bad;

		if (timingsafe_bcmp(sig, sigp, 16)) {
			tcpstat_inc(tcps_rcvbadsig);
			goto bad;
		}

		tcpstat_inc(tcps_rcvgoodsig);
	}

	tdb_unref(tdb);
#endif /* TCP_SIGNATURE */

	return (0);

#ifdef TCP_SIGNATURE
 bad:
	tdb_unref(tdb);
#endif
	return (-1);
}

u_long
tcp_seq_subtract(u_long a, u_long b)
{
	return ((long)(a - b));
}

/*
 * This function is called upon receipt of new valid data (while not in header
 * prediction mode), and it updates the ordered list of sacks.
 */
void
tcp_update_sack_list(struct tcpcb *tp, tcp_seq rcv_laststart,
    tcp_seq rcv_lastend)
{
	/*
	 * First reported block MUST be the most recent one.  Subsequent
	 * blocks SHOULD be in the order in which they arrived at the
	 * receiver.  These two conditions make the implementation fully
	 * compliant with RFC 2018.
	 */
	int i, j = 0, count = 0, lastpos = -1;
	struct sackblk sack, firstsack, temp[MAX_SACK_BLKS];

	/* First clean up current list of sacks */
	for (i = 0; i < tp->rcv_numsacks; i++) {
		sack = tp->sackblks[i];
		if (sack.start == 0 && sack.end == 0) {
			count++; /* count = number of blocks to be discarded */
			continue;
		}
		if (SEQ_LEQ(sack.end, tp->rcv_nxt)) {
			tp->sackblks[i].start = tp->sackblks[i].end = 0;
			count++;
		} else {
			temp[j].start = tp->sackblks[i].start;
			temp[j++].end = tp->sackblks[i].end;
		}
	}
	tp->rcv_numsacks -= count;
	if (tp->rcv_numsacks == 0) { /* no sack blocks currently (fast path) */
		tcp_clean_sackreport(tp);
		if (SEQ_LT(tp->rcv_nxt, rcv_laststart)) {
			/* ==> need first sack block */
			tp->sackblks[0].start = rcv_laststart;
			tp->sackblks[0].end = rcv_lastend;
			tp->rcv_numsacks = 1;
		}
		return;
	}
	/* Otherwise, sack blocks are already present. */
	for (i = 0; i < tp->rcv_numsacks; i++)
		tp->sackblks[i] = temp[i]; /* first copy back sack list */
	if (SEQ_GEQ(tp->rcv_nxt, rcv_lastend))
		return;     /* sack list remains unchanged */
	/*
	 * From here, segment just received should be (part of) the 1st sack.
	 * Go through list, possibly coalescing sack block entries.
	 */
	firstsack.start = rcv_laststart;
	firstsack.end = rcv_lastend;
	for (i = 0; i < tp->rcv_numsacks; i++) {
		sack = tp->sackblks[i];
		if (SEQ_LT(sack.end, firstsack.start) ||
		    SEQ_GT(sack.start, firstsack.end))
			continue; /* no overlap */
		if (sack.start == firstsack.start && sack.end == firstsack.end){
			/*
			 * identical block; delete it here since we will
			 * move it to the front of the list.
			 */
			tp->sackblks[i].start = tp->sackblks[i].end = 0;
			lastpos = i;    /* last posn with a zero entry */
			continue;
		}
		if (SEQ_LEQ(sack.start, firstsack.start))
			firstsack.start = sack.start; /* merge blocks */
		if (SEQ_GEQ(sack.end, firstsack.end))
			firstsack.end = sack.end;     /* merge blocks */
		tp->sackblks[i].start = tp->sackblks[i].end = 0;
		lastpos = i;    /* last posn with a zero entry */
	}
	if (lastpos != -1) {    /* at least one merge */
		for (i = 0, j = 1; i < tp->rcv_numsacks; i++) {
			sack = tp->sackblks[i];
			if (sack.start == 0 && sack.end == 0)
				continue;
			temp[j++] = sack;
		}
		tp->rcv_numsacks = j; /* including first blk (added later) */
		for (i = 1; i < tp->rcv_numsacks; i++) /* now copy back */
			tp->sackblks[i] = temp[i];
	} else {        /* no merges -- shift sacks by 1 */
		if (tp->rcv_numsacks < MAX_SACK_BLKS)
			tp->rcv_numsacks++;
		for (i = tp->rcv_numsacks-1; i > 0; i--)
			tp->sackblks[i] = tp->sackblks[i-1];
	}
	tp->sackblks[0] = firstsack;
	return;
}

/*
 * Process the TCP SACK option.  tp->snd_holes is an ordered list
 * of holes (oldest to newest, in terms of the sequence space).
 */
void
tcp_sack_option(struct tcpcb *tp, struct tcphdr *th, u_char *cp, int optlen)
{
	int tmp_olen;
	u_char *tmp_cp;
	struct sackhole *cur, *p, *temp;

	if (!tp->sack_enable)
		return;
	/* SACK without ACK doesn't make sense. */
	if ((th->th_flags & TH_ACK) == 0)
		return;
	/* Make sure the ACK on this segment is in [snd_una, snd_max]. */
	if (SEQ_LT(th->th_ack, tp->snd_una) ||
	    SEQ_GT(th->th_ack, tp->snd_max))
		return;
	/* Note: TCPOLEN_SACK must be 2*sizeof(tcp_seq) */
	if (optlen <= 2 || (optlen - 2) % TCPOLEN_SACK != 0)
		return;
	/* Note: TCPOLEN_SACK must be 2*sizeof(tcp_seq) */
	tmp_cp = cp + 2;
	tmp_olen = optlen - 2;
	tcpstat_inc(tcps_sack_rcv_opts);
	if (tp->snd_numholes < 0)
		tp->snd_numholes = 0;
	if (tp->t_maxseg == 0)
		panic("tcp_sack_option"); /* Should never happen */
	while (tmp_olen > 0) {
		struct sackblk sack;

		memcpy(&sack.start, tmp_cp, sizeof(tcp_seq));
		sack.start = ntohl(sack.start);
		memcpy(&sack.end, tmp_cp + sizeof(tcp_seq), sizeof(tcp_seq));
		sack.end = ntohl(sack.end);
		tmp_olen -= TCPOLEN_SACK;
		tmp_cp += TCPOLEN_SACK;
		if (SEQ_LEQ(sack.end, sack.start))
			continue; /* bad SACK fields */
		if (SEQ_LEQ(sack.end, tp->snd_una))
			continue; /* old block */
		if (SEQ_GT(th->th_ack, tp->snd_una)) {
			if (SEQ_LT(sack.start, th->th_ack))
				continue;
		}
		if (SEQ_GT(sack.end, tp->snd_max))
			continue;
		if (tp->snd_holes == NULL) { /* first hole */
			tp->snd_holes = (struct sackhole *)
			    pool_get(&sackhl_pool, PR_NOWAIT);
			if (tp->snd_holes == NULL) {
				/* ENOBUFS, so ignore SACKed block for now */
				goto dropped;
			}
			cur = tp->snd_holes;
			cur->start = th->th_ack;
			cur->end = sack.start;
			cur->rxmit = cur->start;
			cur->next = NULL;
			tp->snd_numholes = 1;
			tp->rcv_lastsack = sack.end;
			/*
			 * dups is at least one.  If more data has been
			 * SACKed, it can be greater than one.
			 */
			cur->dups = min(tcprexmtthresh,
			    ((sack.end - cur->end)/tp->t_maxseg));
			if (cur->dups < 1)
				cur->dups = 1;
			continue; /* with next sack block */
		}
		/* Go thru list of holes:  p = previous,  cur = current */
		p = cur = tp->snd_holes;
		while (cur) {
			if (SEQ_LEQ(sack.end, cur->start))
				/* SACKs data before the current hole */
				break; /* no use going through more holes */
			if (SEQ_GEQ(sack.start, cur->end)) {
				/* SACKs data beyond the current hole */
				cur->dups++;
				if (((sack.end - cur->end)/tp->t_maxseg) >=
				    tcprexmtthresh)
					cur->dups = tcprexmtthresh;
				p = cur;
				cur = cur->next;
				continue;
			}
			if (SEQ_LEQ(sack.start, cur->start)) {
				/* Data acks at least the beginning of hole */
				if (SEQ_GEQ(sack.end, cur->end)) {
					/* Acks entire hole, so delete hole */
					if (p != cur) {
						p->next = cur->next;
						pool_put(&sackhl_pool, cur);
						cur = p->next;
					} else {
						cur = cur->next;
						pool_put(&sackhl_pool, p);
						p = cur;
						tp->snd_holes = p;
					}
					tp->snd_numholes--;
					continue;
				}
				/* otherwise, move start of hole forward */
				cur->start = sack.end;
				cur->rxmit = SEQ_MAX(cur->rxmit, cur->start);
				p = cur;
				cur = cur->next;
				continue;
			}
			/* move end of hole backward */
			if (SEQ_GEQ(sack.end, cur->end)) {
				cur->end = sack.start;
				cur->rxmit = SEQ_MIN(cur->rxmit, cur->end);
				cur->dups++;
				if (((sack.end - cur->end)/tp->t_maxseg) >=
				    tcprexmtthresh)
					cur->dups = tcprexmtthresh;
				p = cur;
				cur = cur->next;
				continue;
			}
			if (SEQ_LT(cur->start, sack.start) &&
			    SEQ_GT(cur->end, sack.end)) {
				/*
				 * ACKs some data in middle of a hole; need to
				 * split current hole
				 */
				if (tp->snd_numholes >= TCP_SACKHOLE_LIMIT)
					goto dropped;
				temp = (struct sackhole *)
				    pool_get(&sackhl_pool, PR_NOWAIT);
				if (temp == NULL)
					goto dropped; /* ENOBUFS */
				temp->next = cur->next;
				temp->start = sack.end;
				temp->end = cur->end;
				temp->dups = cur->dups;
				temp->rxmit = SEQ_MAX(cur->rxmit, temp->start);
				cur->end = sack.start;
				cur->rxmit = SEQ_MIN(cur->rxmit, cur->end);
				cur->dups++;
				if (((sack.end - cur->end)/tp->t_maxseg) >=
					tcprexmtthresh)
					cur->dups = tcprexmtthresh;
				cur->next = temp;
				p = temp;
				cur = p->next;
				tp->snd_numholes++;
			}
		}
		/* At this point, p points to the last hole on the list */
		if (SEQ_LT(tp->rcv_lastsack, sack.start)) {
			/*
			 * Need to append new hole at end.
			 * Last hole is p (and it's not NULL).
			 */
			if (tp->snd_numholes >= TCP_SACKHOLE_LIMIT)
				goto dropped;
			temp = (struct sackhole *)
			    pool_get(&sackhl_pool, PR_NOWAIT);
			if (temp == NULL)
				goto dropped; /* ENOBUFS */
			temp->start = tp->rcv_lastsack;
			temp->end = sack.start;
			temp->dups = min(tcprexmtthresh,
			    ((sack.end - sack.start)/tp->t_maxseg));
			if (temp->dups < 1)
				temp->dups = 1;
			temp->rxmit = temp->start;
			temp->next = 0;
			p->next = temp;
			tp->rcv_lastsack = sack.end;
			tp->snd_numholes++;
		}
	}
	return;
dropped:
	tcpstat_inc(tcps_sack_drop_opts);
}

/*
 * Delete stale (i.e, cumulatively ack'd) holes.  Hole is deleted only if
 * it is completely acked; otherwise, tcp_sack_option(), called from
 * tcp_dooptions(), will fix up the hole.
 */
void
tcp_del_sackholes(struct tcpcb *tp, struct tcphdr *th)
{
	if (tp->sack_enable && tp->t_state != TCPS_LISTEN) {
		/* max because this could be an older ack just arrived */
		tcp_seq lastack = SEQ_GT(th->th_ack, tp->snd_una) ?
			th->th_ack : tp->snd_una;
		struct sackhole *cur = tp->snd_holes;
		struct sackhole *prev;
		while (cur)
			if (SEQ_LEQ(cur->end, lastack)) {
				prev = cur;
				cur = cur->next;
				pool_put(&sackhl_pool, prev);
				tp->snd_numholes--;
			} else if (SEQ_LT(cur->start, lastack)) {
				cur->start = lastack;
				if (SEQ_LT(cur->rxmit, cur->start))
					cur->rxmit = cur->start;
				break;
			} else
				break;
		tp->snd_holes = cur;
	}
}

/*
 * Delete all receiver-side SACK information.
 */
void
tcp_clean_sackreport(struct tcpcb *tp)
{
	int i;

	tp->rcv_numsacks = 0;
	for (i = 0; i < MAX_SACK_BLKS; i++)
		tp->sackblks[i].start = tp->sackblks[i].end=0;

}

/*
 * Partial ack handling within a sack recovery episode.  When a partial ack
 * arrives, turn off retransmission timer, deflate the window, do not clear
 * tp->t_dupacks.
 */
void
tcp_sack_partialack(struct tcpcb *tp, struct tcphdr *th)
{
	/* Turn off retx. timer (will start again next segment) */
	TCP_TIMER_DISARM(tp, TCPT_REXMT);
	tp->t_rtttime = 0;
	/*
	 * Partial window deflation.  This statement relies on the
	 * fact that tp->snd_una has not been updated yet.
	 */
	if (tp->snd_cwnd > (th->th_ack - tp->snd_una)) {
		tp->snd_cwnd -= th->th_ack - tp->snd_una;
		tp->snd_cwnd += tp->t_maxseg;
	} else
		tp->snd_cwnd = tp->t_maxseg;
	tp->snd_cwnd += tp->t_maxseg;
	tp->t_flags |= TF_NEEDOUTPUT;
}

/*
 * Pull out of band byte out of a segment so
 * it doesn't appear in the user's data queue.
 * It is still reflected in the segment length for
 * sequencing purposes.
 */
void
tcp_pulloutofband(struct socket *so, u_int urgent, struct mbuf *m, int off)
{
	int cnt = off + urgent - 1;

	while (cnt >= 0) {
		if (m->m_len > cnt) {
			char *cp = mtod(m, caddr_t) + cnt;
			struct tcpcb *tp = sototcpcb(so);

			tp->t_iobc = *cp;
			tp->t_oobflags |= TCPOOB_HAVEDATA;
			memmove(cp, cp + 1, m->m_len - cnt - 1);
			m->m_len--;
			return;
		}
		cnt -= m->m_len;
		m = m->m_next;
		if (m == NULL)
			break;
	}
	panic("tcp_pulloutofband");
}

/*
 * Collect new round-trip time estimate
 * and update averages and current timeout.
 */
void
tcp_xmit_timer(struct tcpcb *tp, int32_t rtt)
{
	int delta, rttmin;

	if (rtt < 0)
		rtt = 0;
	else if (rtt > TCP_RTT_MAX)
		rtt = TCP_RTT_MAX;

	tcpstat_inc(tcps_rttupdated);
	if (tp->t_srtt != 0) {
		/*
		 * delta is fixed point with 2 (TCP_RTT_BASE_SHIFT) bits
		 * after the binary point (scaled by 4), whereas
		 * srtt is stored as fixed point with 5 bits after the
		 * binary point (i.e., scaled by 32).  The following magic
		 * is equivalent to the smoothing algorithm in rfc793 with
		 * an alpha of .875 (srtt = rtt/8 + srtt*7/8 in fixed
		 * point).
		 */
		delta = (rtt << TCP_RTT_BASE_SHIFT) -
		    (tp->t_srtt >> TCP_RTT_SHIFT);
		if ((tp->t_srtt += delta) <= 0)
			tp->t_srtt = 1 << TCP_RTT_BASE_SHIFT;
		/*
		 * We accumulate a smoothed rtt variance (actually, a
		 * smoothed mean difference), then set the retransmit
		 * timer to smoothed rtt + 4 times the smoothed variance.
		 * rttvar is stored as fixed point with 4 bits after the
		 * binary point (scaled by 16).  The following is
		 * equivalent to rfc793 smoothing with an alpha of .75
		 * (rttvar = rttvar*3/4 + |delta| / 4).  This replaces
		 * rfc793's wired-in beta.
		 */
		if (delta < 0)
			delta = -delta;
		delta -= (tp->t_rttvar >> TCP_RTTVAR_SHIFT);
		if ((tp->t_rttvar += delta) <= 0)
			tp->t_rttvar = 1 << TCP_RTT_BASE_SHIFT;
	} else {
		/*
		 * No rtt measurement yet - use the unsmoothed rtt.
		 * Set the variance to half the rtt (so our first
		 * retransmit happens at 3*rtt).
		 */
		tp->t_srtt = (rtt + 1) << (TCP_RTT_SHIFT + TCP_RTT_BASE_SHIFT);
		tp->t_rttvar = (rtt + 1) <<
		    (TCP_RTTVAR_SHIFT + TCP_RTT_BASE_SHIFT - 1);
	}
	tp->t_rtttime = 0;
	tp->t_rxtshift = 0;

	/*
	 * the retransmit should happen at rtt + 4 * rttvar.
	 * Because of the way we do the smoothing, srtt and rttvar
	 * will each average +1/2 tick of bias.  When we compute
	 * the retransmit timer, we want 1/2 tick of rounding and
	 * 1 extra tick because of +-1/2 tick uncertainty in the
	 * firing of the timer.  The bias will give us exactly the
	 * 1.5 tick we need.  But, because the bias is
	 * statistical, we have to test that we don't drop below
	 * the minimum feasible timer (which is 2 ticks).
	 */
	rttmin = min(max(tp->t_rttmin, rtt + 2 * (TCP_TIME(1) / hz)),
	    TCPTV_REXMTMAX);
	TCPT_RANGESET(tp->t_rxtcur, TCP_REXMTVAL(tp), rttmin, TCPTV_REXMTMAX);

	/*
	 * We received an ack for a packet that wasn't retransmitted;
	 * it is probably safe to discard any error indications we've
	 * received recently.  This isn't quite right, but close enough
	 * for now (a route might have failed after we sent a segment,
	 * and the return path might not be symmetrical).
	 */
	tp->t_softerror = 0;
}

/*
 * Determine a reasonable value for maxseg size.
 * If the route is known, check route for mtu.
 * If none, use an mss that can be handled on the outgoing
 * interface without forcing IP to fragment; if bigger than
 * an mbuf cluster (MCLBYTES), round down to nearest multiple of MCLBYTES
 * to utilize large mbufs.  If no route is found, route has no mtu,
 * or the destination isn't local, use a default, hopefully conservative
 * size (usually 512 or the default IP max size, but no more than the mtu
 * of the interface), as we can't discover anything about intervening
 * gateways or networks.  We also initialize the congestion/slow start
 * window to be a single segment if the destination isn't local.
 * While looking at the routing entry, we also initialize other path-dependent
 * parameters from pre-set or cached values in the routing entry.
 *
 * Also take into account the space needed for options that we
 * send regularly.  Make maxseg shorter by that amount to assure
 * that we can send maxseg amount of data even when the options
 * are present.  Store the upper limit of the length of options plus
 * data in maxopd.
 *
 * NOTE: offer == -1 indicates that the maxseg size changed due to
 * Path MTU discovery.
 */
int
tcp_mss(struct tcpcb *tp, int offer)
{
	struct rtentry *rt;
	struct ifnet *ifp;
	int mss, mssopt, mssdflt, iphlen, do_rfc3390;
	u_int rtmtu;

	mss = mssopt = mssdflt = atomic_load_int(&tcp_mssdflt);

	rt = in_pcbrtentry(tp->t_inpcb);
	if (rt == NULL)
		goto out;

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		goto out;

	switch (tp->pf) {
	case AF_INET:
		iphlen = sizeof(struct ip);
		break;
#ifdef INET6
	case AF_INET6:
		iphlen = sizeof(struct ip6_hdr);
		break;
#endif
	default:
		unhandled_af(tp->pf);
	}

	/*
	 * if there's an mtu associated with the route and we support
	 * path MTU discovery for the underlying protocol family, use it.
	 */
	rtmtu = atomic_load_int(&rt->rt_mtu);
	if (rtmtu) {
		/*
		 * One may wish to lower MSS to take into account options,
		 * especially security-related options.
		 */
		if (tp->pf == AF_INET6 && rtmtu < IPV6_MMTU) {
			/*
			 * RFC2460 section 5, last paragraph: if path MTU is
			 * smaller than 1280, use 1280 as packet size and
			 * attach fragment header.
			 */
			mss = IPV6_MMTU - iphlen - sizeof(struct ip6_frag) -
			    sizeof(struct tcphdr);
		} else {
			mss = rtmtu - iphlen - sizeof(struct tcphdr);
		}
	} else if (ifp->if_flags & IFF_LOOPBACK) {
		mss = ifp->if_mtu - iphlen - sizeof(struct tcphdr);
	} else if (tp->pf == AF_INET) {
		if (atomic_load_int(&ip_mtudisc))
			mss = ifp->if_mtu - iphlen - sizeof(struct tcphdr);
	}
#ifdef INET6
	else if (tp->pf == AF_INET6) {
		/*
		 * for IPv6, path MTU discovery is always turned on,
		 * or the node must use packet size <= 1280.
		 */
		mss = ifp->if_mtu - iphlen - sizeof(struct tcphdr);
	}
#endif /* INET6 */

	/* Calculate the value that we offer in TCPOPT_MAXSEG */
	if (offer != -1) {
		mssopt = ifp->if_mtu - iphlen - sizeof(struct tcphdr);
		mssopt = imax(mssopt, mssdflt);
	}
	if_put(ifp);
 out:
	/*
	 * The current mss, t_maxseg, is initialized to the default value.
	 * If we compute a smaller value, reduce the current mss.
	 * If we compute a larger value, return it for use in sending
	 * a max seg size option, but don't store it for use
	 * unless we received an offer at least that large from peer.
	 *
	 * However, do not accept offers lower than the minimum of
	 * the interface MTU and 216.
	 */
	if (offer > 0)
		tp->t_peermss = offer;
	if (tp->t_peermss)
		mss = imin(mss, max(tp->t_peermss, 216));

	/* sanity - at least max opt. space */
	mss = imax(mss, 64);

	/*
	 * maxopd stores the maximum length of data AND options
	 * in a segment; maxseg is the amount of data in a normal
	 * segment.  We need to store this value (maxopd) apart
	 * from maxseg, because now every segment carries options
	 * and thus we normally have somewhat less data in segments.
	 */
	tp->t_maxopd = mss;

	if ((tp->t_flags & (TF_REQ_TSTMP|TF_NOOPT)) == TF_REQ_TSTMP &&
	    (tp->t_flags & TF_RCVD_TSTMP) == TF_RCVD_TSTMP)
		mss -= TCPOLEN_TSTAMP_APPA;
#ifdef TCP_SIGNATURE
	if (tp->t_flags & TF_SIGNATURE)
		mss -= TCPOLEN_SIGLEN;
#endif

	do_rfc3390 = atomic_load_int(&tcp_do_rfc3390);
	if (offer == -1) {
		/* mss changed due to Path MTU discovery */
		tp->t_flags &= ~TF_PMTUD_PEND;
		tp->t_pmtud_mtu_sent = 0;
		tp->t_pmtud_mss_acked = 0;
		if (mss < tp->t_maxseg) {
			/*
			 * Follow suggestion in RFC 2414 to reduce the
			 * congestion window by the ratio of the old
			 * segment size to the new segment size.
			 */
			tp->snd_cwnd = ulmax((tp->snd_cwnd / tp->t_maxseg) *
			    mss, mss);
		}
	} else if (do_rfc3390 == 2) {
		/* increase initial window  */
		tp->snd_cwnd = ulmin(10 * mss, ulmax(2 * mss, 14600));
	} else if (do_rfc3390) {
		/* increase initial window  */
		tp->snd_cwnd = ulmin(4 * mss, ulmax(2 * mss, 4380));
	} else
		tp->snd_cwnd = mss;

	tp->t_maxseg = mss;

	return (offer != -1 ? mssopt : mss);
}

u_int
tcp_hdrsz(struct tcpcb *tp)
{
	u_int hlen;

	switch (tp->pf) {
#ifdef INET6
	case AF_INET6:
		hlen = sizeof(struct ip6_hdr);
		break;
#endif
	case AF_INET:
		hlen = sizeof(struct ip);
		break;
	default:
		hlen = 0;
		break;
	}
	hlen += sizeof(struct tcphdr);

	if ((tp->t_flags & (TF_REQ_TSTMP|TF_NOOPT)) == TF_REQ_TSTMP &&
	    (tp->t_flags & TF_RCVD_TSTMP) == TF_RCVD_TSTMP)
		hlen += TCPOLEN_TSTAMP_APPA;
#ifdef TCP_SIGNATURE
	if (tp->t_flags & TF_SIGNATURE)
		hlen += TCPOLEN_SIGLEN;
#endif
	return (hlen);
}

/*
 * Set connection variables based on the effective MSS.
 * We are passed the TCPCB for the actual connection.  If we
 * are the server, we are called by the compressed state engine
 * when the 3-way handshake is complete.  If we are the client,
 * we are called when we receive the SYN,ACK from the server.
 *
 * NOTE: The t_maxseg value must be initialized in the TCPCB
 * before this routine is called!
 */
void
tcp_mss_update(struct tcpcb *tp)
{
	int mss;
	u_long bufsize;
	struct rtentry *rt;
	struct socket *so;

	so = tp->t_inpcb->inp_socket;
	mss = tp->t_maxseg;

	rt = in_pcbrtentry(tp->t_inpcb);
	if (rt == NULL)
		return;

	mtx_enter(&so->so_snd.sb_mtx);
	bufsize = so->so_snd.sb_hiwat;
	if (bufsize < mss) {
		mtx_leave(&so->so_snd.sb_mtx);
		mss = bufsize;
		/* Update t_maxseg and t_maxopd */
		tcp_mss(tp, mss);
	} else {
		bufsize = roundup(bufsize, mss);
		if (bufsize > sb_max)
			bufsize = sb_max;
		(void)sbreserve(&so->so_snd, bufsize);
		mtx_leave(&so->so_snd.sb_mtx);
	}

	mtx_enter(&so->so_rcv.sb_mtx);
	bufsize = so->so_rcv.sb_hiwat;
	if (bufsize > mss) {
		bufsize = roundup(bufsize, mss);
		if (bufsize > sb_max)
			bufsize = sb_max;
		(void)sbreserve(&so->so_rcv, bufsize);
	}
	mtx_leave(&so->so_rcv.sb_mtx);
}

/*
 * When a partial ack arrives, force the retransmission of the
 * next unacknowledged segment.  Do not clear tp->t_dupacks.
 * By setting snd_nxt to ti_ack, this forces retransmission timer
 * to be started again.
 */
void
tcp_newreno_partialack(struct tcpcb *tp, struct tcphdr *th)
{
	/*
	 * snd_una has not been updated and the socket send buffer
	 * not yet drained of the acked data, so we have to leave
	 * snd_una as it was to get the correct data offset in
	 * tcp_output().
	 */
	tcp_seq onxt = tp->snd_nxt;
	u_long  ocwnd = tp->snd_cwnd;

	TCP_TIMER_DISARM(tp, TCPT_REXMT);
	tp->t_rtttime = 0;
	tp->snd_nxt = th->th_ack;
	/*
	 * Set snd_cwnd to one segment beyond acknowledged offset
	 * (tp->snd_una not yet updated when this function is called)
	 */
	tp->snd_cwnd = tp->t_maxseg + (th->th_ack - tp->snd_una);
	(void)tcp_output(tp);
	tp->snd_cwnd = ocwnd;
	if (SEQ_GT(onxt, tp->snd_nxt))
		tp->snd_nxt = onxt;
	/*
	 * Partial window deflation.  Relies on fact that tp->snd_una
	 * not updated yet.
	 */
	if (tp->snd_cwnd > th->th_ack - tp->snd_una)
		tp->snd_cwnd -= th->th_ack - tp->snd_una;
	else
		tp->snd_cwnd = 0;
	tp->snd_cwnd += tp->t_maxseg;
}

int
tcp_mss_adv(struct rtentry *rt, int af)
{
	struct ifnet *ifp;
	int iphlen, mss, mssdflt;

	mssdflt = atomic_load_int(&tcp_mssdflt);

	if (rt == NULL)
		return mssdflt;

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		return mssdflt;

	switch (af) {
	case AF_INET:
		iphlen = sizeof(struct ip);
		break;
#ifdef INET6
	case AF_INET6:
		iphlen = sizeof(struct ip6_hdr);
		break;
#endif
	default:
		unhandled_af(af);
	}
	mss = ifp->if_mtu - iphlen - sizeof(struct tcphdr);
	if_put(ifp);

	return imax(mss, mssdflt);
}

/*
 * TCP compressed state engine.  Currently used to hold compressed
 * state for SYN_RECEIVED.
 */

/*
 * Locks used to protect global data and struct members:
 *	a	atomic operations
 *	N	net lock
 *	S	syn_cache_mtx		tcp syn cache global mutex
 */

/* syn hash parameters */
int	tcp_syn_hash_size = TCP_SYN_HASH_SIZE;	/* [S] size of hash table */
int	tcp_syn_cache_limit =			/* [a] global entry limit */
	    TCP_SYN_HASH_SIZE * TCP_SYN_BUCKET_SIZE;
int	tcp_syn_bucket_limit =			/* [a] per bucket limit */
	    3 * TCP_SYN_BUCKET_SIZE;
int	tcp_syn_use_limit = 100000;		/* [S] reseed after uses */

struct pool syn_cache_pool;
struct syn_cache_set tcp_syn_cache[2];		/* [S] */
int tcp_syn_cache_active;			/* [S] */
struct mutex syn_cache_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);

static inline uint32_t
syn_cache_hash(const struct sockaddr *src, const struct sockaddr *dst,
    uint32_t rand[])
{
	switch (src->sa_family) {
	case AF_INET: {
		uint32_t src_port = satosin_const(src)->sin_port;
		uint32_t dst_port = satosin_const(dst)->sin_port;
		const in_addr_t *src_addr =
		    &satosin_const(src)->sin_addr.s_addr;

		return ((((dst_port << 16) + src_port) ^ rand[4]) *
		    (*src_addr ^ rand[0]));
	    }
#ifdef INET6
	case AF_INET6: {
		uint32_t src_port = satosin6_const(src)->sin6_port;
		uint32_t dst_port = satosin6_const(dst)->sin6_port;
		const uint32_t *src_addr6 =
		    satosin6_const(src)->sin6_addr.s6_addr32;

		return ((((dst_port << 16) + src_port) ^ rand[4]) *
		    (src_addr6[0] ^ rand[0]) *
		    (src_addr6[1] ^ rand[1]) *
		    (src_addr6[2] ^ rand[2]) *
		    (src_addr6[3] ^ rand[3]));
	    }
#endif
	default:
		unhandled_af(src->sa_family);
	}
}

void
syn_cache_rm(struct syn_cache *sc)
{
	MUTEX_ASSERT_LOCKED(&syn_cache_mtx);

	TAILQ_REMOVE(&sc->sc_buckethead->sch_bucket, sc, sc_bucketq);
	in_pcbunref(sc->sc_inplisten);
	sc->sc_inplisten = NULL;
	LIST_REMOVE(sc, sc_tpq);
	refcnt_rele(&sc->sc_refcnt);
	sc->sc_buckethead->sch_length--;
	if (timeout_del(&sc->sc_timer))
		refcnt_rele(&sc->sc_refcnt);
	sc->sc_set->scs_count--;
}

void
syn_cache_put(struct syn_cache *sc)
{
	if (refcnt_rele(&sc->sc_refcnt) == 0)
		return;

	/* Dealing with last reference, no lock needed. */
	m_free(sc->sc_ipopts);
	rtfree(sc->sc_route.ro_rt);

	pool_put(&syn_cache_pool, sc);
}

void
syn_cache_init(void)
{
	int i;

	/* Initialize the hash buckets. */
	tcp_syn_cache[0].scs_buckethead = mallocarray(tcp_syn_hash_size,
	    sizeof(struct syn_cache_head), M_SYNCACHE, M_WAITOK|M_ZERO);
	tcp_syn_cache[1].scs_buckethead = mallocarray(tcp_syn_hash_size,
	    sizeof(struct syn_cache_head), M_SYNCACHE, M_WAITOK|M_ZERO);
	tcp_syn_cache[0].scs_size = tcp_syn_hash_size;
	tcp_syn_cache[1].scs_size = tcp_syn_hash_size;
	for (i = 0; i < tcp_syn_hash_size; i++) {
		TAILQ_INIT(&tcp_syn_cache[0].scs_buckethead[i].sch_bucket);
		TAILQ_INIT(&tcp_syn_cache[1].scs_buckethead[i].sch_bucket);
	}

	/* Initialize the syn cache pool. */
	pool_init(&syn_cache_pool, sizeof(struct syn_cache), 0, IPL_SOFTNET,
	    0, "syncache", NULL);
}

void
syn_cache_insert(struct syn_cache *sc, struct tcpcb *tp)
{
	struct syn_cache_set *set;
	struct syn_cache_head *scp;
	struct syn_cache *sc2;
	int i;

	NET_ASSERT_LOCKED();
	MUTEX_ASSERT_LOCKED(&syn_cache_mtx);

	set = &tcp_syn_cache[tcp_syn_cache_active];

	/*
	 * If there are no entries in the hash table, reinitialize
	 * the hash secrets.  To avoid useless cache swaps and
	 * reinitialization, use it until the limit is reached.
	 * An empty cache is also the opportunity to resize the hash.
	 */
	if (set->scs_count == 0 && set->scs_use <= 0) {
		set->scs_use = tcp_syn_use_limit;
		if (set->scs_size != tcp_syn_hash_size) {
			scp = mallocarray(tcp_syn_hash_size, sizeof(struct
			    syn_cache_head), M_SYNCACHE, M_NOWAIT|M_ZERO);
			if (scp == NULL) {
				/* Try again next time. */
				set->scs_use = 0;
			} else {
				free(set->scs_buckethead, M_SYNCACHE,
				    set->scs_size *
				    sizeof(struct syn_cache_head));
				set->scs_buckethead = scp;
				set->scs_size = tcp_syn_hash_size;
				for (i = 0; i < tcp_syn_hash_size; i++)
					TAILQ_INIT(&scp[i].sch_bucket);
			}
		}
		arc4random_buf(set->scs_random, sizeof(set->scs_random));
		tcpstat_inc(tcps_sc_seedrandom);
	}

	sc->sc_hash = syn_cache_hash(&sc->sc_src.sa, &sc->sc_dst.sa,
	    set->scs_random);
	scp = &set->scs_buckethead[sc->sc_hash % set->scs_size];
	sc->sc_buckethead = scp;

	/*
	 * Make sure that we don't overflow the per-bucket
	 * limit or the total cache size limit.
	 */
	if (scp->sch_length >= atomic_load_int(&tcp_syn_bucket_limit)) {
		tcpstat_inc(tcps_sc_bucketoverflow);
		/*
		 * Someone might attack our bucket hash function.  Reseed
		 * with random as soon as the passive syn cache gets empty.
		 */
		set->scs_use = 0;
		/*
		 * The bucket is full.  Toss the oldest element in the
		 * bucket.  This will be the first entry in the bucket.
		 */
		sc2 = TAILQ_FIRST(&scp->sch_bucket);
#ifdef DIAGNOSTIC
		/*
		 * This should never happen; we should always find an
		 * entry in our bucket.
		 */
		if (sc2 == NULL)
			panic("%s: bucketoverflow: impossible", __func__);
#endif
		syn_cache_rm(sc2);
		syn_cache_put(sc2);
	} else if (set->scs_count >= atomic_load_int(&tcp_syn_cache_limit)) {
		struct syn_cache_head *scp2, *sce;

		tcpstat_inc(tcps_sc_overflowed);
		/*
		 * The cache is full.  Toss the oldest entry in the
		 * first non-empty bucket we can find.
		 *
		 * XXX We would really like to toss the oldest
		 * entry in the cache, but we hope that this
		 * condition doesn't happen very often.
		 */
		scp2 = scp;
		if (TAILQ_EMPTY(&scp2->sch_bucket)) {
			sce = &set->scs_buckethead[set->scs_size];
			for (++scp2; scp2 != scp; scp2++) {
				if (scp2 >= sce)
					scp2 = &set->scs_buckethead[0];
				if (! TAILQ_EMPTY(&scp2->sch_bucket))
					break;
			}
#ifdef DIAGNOSTIC
			/*
			 * This should never happen; we should always find a
			 * non-empty bucket.
			 */
			if (scp2 == scp)
				panic("%s: cacheoverflow: impossible",
				    __func__);
#endif
		}
		sc2 = TAILQ_FIRST(&scp2->sch_bucket);
		syn_cache_rm(sc2);
		syn_cache_put(sc2);
	}

	/*
	 * Initialize the entry's timer.  We don't estimate RTT
	 * with SYNs, so each packet starts with the default RTT
	 * and each timer step has a fixed timeout value.
	 */
	sc->sc_rxttot = 0;
	sc->sc_rxtshift = 0;
	TCPT_RANGESET(sc->sc_rxtcur,
	    TCPTV_SRTTDFLT * tcp_backoff[sc->sc_rxtshift], TCPTV_MIN,
	    TCPTV_REXMTMAX);
	if (timeout_add_msec(&sc->sc_timer, sc->sc_rxtcur))
		refcnt_take(&sc->sc_refcnt);

	/* Link it from tcpcb entry */
	refcnt_take(&sc->sc_refcnt);
	LIST_INSERT_HEAD(&tp->t_sc, sc, sc_tpq);

	/* Put it into the bucket. */
	TAILQ_INSERT_TAIL(&scp->sch_bucket, sc, sc_bucketq);
	scp->sch_length++;
	sc->sc_set = set;
	set->scs_count++;
	set->scs_use--;

	tcpstat_inc(tcps_sc_added);

	/*
	 * If the active cache has exceeded its use limit and
	 * the passive syn cache is empty, exchange their roles.
	 */
	if (set->scs_use <= 0 &&
	    tcp_syn_cache[!tcp_syn_cache_active].scs_count == 0)
		tcp_syn_cache_active = !tcp_syn_cache_active;
}

/*
 * Walk the timer queues, looking for SYN,ACKs that need to be retransmitted.
 * If we have retransmitted an entry the maximum number of times, expire
 * that entry.
 */
void
syn_cache_timer(void *arg)
{
	struct syn_cache *sc = arg;
	struct inpcb *inp;
	struct socket *so;
	uint64_t now;
	int lastref, do_ecn = 0;

	mtx_enter(&syn_cache_mtx);
	inp = in_pcbref(sc->sc_inplisten);
	if (inp == NULL)
		goto freeit;

	if (__predict_false(sc->sc_rxtshift == TCP_MAXRXTSHIFT)) {
		/* Drop it -- too many retransmissions. */
		goto dropit;
	}

	/*
	 * Compute the total amount of time this entry has
	 * been on a queue.  If this entry has been on longer
	 * than the keep alive timer would allow, expire it.
	 */
	sc->sc_rxttot += sc->sc_rxtcur;
	if (sc->sc_rxttot >= atomic_load_int(&tcp_keepinit))
		goto dropit;

	/* Advance the timer back-off. */
	sc->sc_rxtshift++;
	TCPT_RANGESET(sc->sc_rxtcur,
	    TCPTV_SRTTDFLT * tcp_backoff[sc->sc_rxtshift], TCPTV_MIN,
	    TCPTV_REXMTMAX);
	if (timeout_add_msec(&sc->sc_timer, sc->sc_rxtcur))
		refcnt_take(&sc->sc_refcnt);
	mtx_leave(&syn_cache_mtx);

	NET_LOCK_SHARED();
	so = in_pcbsolock(inp);
	if (so != NULL) {
		now = tcp_now();
#ifdef TCP_ECN
		do_ecn = atomic_load_int(&tcp_do_ecn);
#endif
		(void) syn_cache_respond(sc, NULL, now, do_ecn);
		tcpstat_inc(tcps_sc_retransmitted);
	}
	in_pcbsounlock(inp, so);
	NET_UNLOCK_SHARED();

	in_pcbunref(inp);
	syn_cache_put(sc);
	return;

 dropit:
	tcpstat_inc(tcps_sc_timed_out);
	syn_cache_rm(sc);
	in_pcbunref(inp);
	/* Decrement reference of the timer and free object after remove. */
	lastref = refcnt_rele(&sc->sc_refcnt);
	KASSERT(lastref == 0);
	(void)lastref;
 freeit:
	mtx_leave(&syn_cache_mtx);
	syn_cache_put(sc);
}

/*
 * Remove syn cache created by the specified tcb entry,
 * because this does not make sense to keep them
 * (if there's no tcb entry, syn cache entry will never be used)
 */
void
syn_cache_cleanup(struct tcpcb *tp)
{
	struct syn_cache *sc, *nsc;

	NET_ASSERT_LOCKED();

	mtx_enter(&syn_cache_mtx);
	LIST_FOREACH_SAFE(sc, &tp->t_sc, sc_tpq, nsc) {
		KASSERT(sc->sc_inplisten == tp->t_inpcb);
		syn_cache_rm(sc);
		syn_cache_put(sc);
	}
	mtx_leave(&syn_cache_mtx);

	KASSERT(LIST_EMPTY(&tp->t_sc));
}

/*
 * Find an entry in the syn cache.
 */
struct syn_cache *
syn_cache_lookup(const struct sockaddr *src, const struct sockaddr *dst,
    struct syn_cache_head **headp, u_int rtableid)
{
	struct syn_cache_set *sets[2];
	struct syn_cache *sc;
	struct syn_cache_head *scp;
	u_int32_t hash;
	int i;

	NET_ASSERT_LOCKED();
	MUTEX_ASSERT_LOCKED(&syn_cache_mtx);

	/* Check the active cache first, the passive cache is likely empty. */
	sets[0] = &tcp_syn_cache[tcp_syn_cache_active];
	sets[1] = &tcp_syn_cache[!tcp_syn_cache_active];
	for (i = 0; i < 2; i++) {
		if (sets[i]->scs_count == 0)
			continue;
		hash = syn_cache_hash(src, dst, sets[i]->scs_random);
		scp = &sets[i]->scs_buckethead[hash % sets[i]->scs_size];
		*headp = scp;
		TAILQ_FOREACH(sc, &scp->sch_bucket, sc_bucketq) {
			if (sc->sc_hash != hash)
				continue;
			if (!bcmp(&sc->sc_src, src, src->sa_len) &&
			    !bcmp(&sc->sc_dst, dst, dst->sa_len) &&
			    rtable_l2(rtableid) == rtable_l2(sc->sc_rtableid))
				return (sc);
		}
	}
	return (NULL);
}

/*
 * This function gets called when we receive an ACK for a
 * socket in the LISTEN state.  We look up the connection
 * in the syn cache, and if its there, we pull it out of
 * the cache and turn it into a full-blown connection in
 * the SYN-RECEIVED state.
 *
 * The return values may not be immediately obvious, and their effects
 * can be subtle, so here they are:
 *
 *	NULL	SYN was not found in cache; caller should drop the
 *		packet and send an RST.
 *
 *	-1	We were unable to create the new connection, and are
 *		aborting it.  An ACK,RST is being sent to the peer
 *		(unless we got screwy sequence numbers; see below),
 *		because the 3-way handshake has been completed.  Caller
 *		should not free the mbuf, since we may be using it.  If
 *		we are not, we will free it.
 *
 *	Otherwise, the return value is a pointer to the new socket
 *	associated with the connection.
 */
struct socket *
syn_cache_get(struct sockaddr *src, struct sockaddr *dst, struct tcphdr *th,
    u_int hlen, u_int tlen, struct socket *so, struct mbuf *m, uint64_t now,
    int do_ecn)
{
	struct syn_cache *sc;
	struct syn_cache_head *scp;
	struct socket *listenso;
	struct inpcb *inp, *listeninp;
	struct tcpcb *tp = NULL;
	u_int rtableid;

	NET_ASSERT_LOCKED();

	inp = sotoinpcb(so);

	mtx_enter(&syn_cache_mtx);
	sc = syn_cache_lookup(src, dst, &scp, inp->inp_rtableid);
	if (sc == NULL) {
		mtx_leave(&syn_cache_mtx);
		in_pcbsounlock(inp, so);
		return (NULL);
	}

	/*
	 * Verify the sequence and ack numbers.  Try getting the correct
	 * response again.
	 */
	if ((th->th_ack != sc->sc_iss + 1) ||
	    SEQ_LEQ(th->th_seq, sc->sc_irs) ||
	    SEQ_GT(th->th_seq, sc->sc_irs + 1 + sc->sc_win)) {
		refcnt_take(&sc->sc_refcnt);
		mtx_leave(&syn_cache_mtx);
		(void) syn_cache_respond(sc, m, now, do_ecn);
		in_pcbsounlock(inp, so);
		syn_cache_put(sc);
		return ((struct socket *)(-1));
	}

	/* Remove this cache entry */
	syn_cache_rm(sc);
	mtx_leave(&syn_cache_mtx);

	/*
	 * Ok, create the full blown connection, and set things up
	 * as they would have been set up if we had created the
	 * connection when the SYN arrived.  If we can't create
	 * the connection, abort it.
	 */
	listenso = so;
	listeninp = inp;
	inp = NULL;
	so = sonewconn(listenso, SS_ISCONNECTED, M_DONTWAIT);
	if (so == NULL)
		goto resetandabort;
	soassertlocked(so);
	/* inpcb does refcount socket, both so and inp cannot go away */
	inp = in_pcbref(sotoinpcb(so));
	tp = intotcpcb(inp);

#ifdef IPSEC
	/*
	 * We need to copy the required security levels from the listen pcb.
	 * Ditto for any other IPsec-related information.
	 */
	inp->inp_seclevel = listeninp->inp_seclevel;
#endif /* IPSEC */
#ifdef INET6
	if (ISSET(inp->inp_flags, INP_IPV6)) {
		KASSERT(ISSET(listeninp->inp_flags, INP_IPV6));

		inp->inp_ipv6.ip6_hlim = listeninp->inp_ipv6.ip6_hlim;
		inp->inp_hops = listeninp->inp_hops;
	} else
#endif
	{
		KASSERT(!ISSET(listeninp->inp_flags, INP_IPV6));

		inp->inp_ip.ip_ttl = listeninp->inp_ip.ip_ttl;
		inp->inp_options = ip_srcroute(m);
		if (inp->inp_options == NULL) {
			inp->inp_options = sc->sc_ipopts;
			sc->sc_ipopts = NULL;
		}
	}

	/* inherit rtable from listening socket */
	rtableid = sc->sc_rtableid;
#if NPF > 0
	if (m->m_pkthdr.pf.flags & PF_TAG_DIVERTED) {
		struct pf_divert *divert;

		divert = pf_find_divert(m);
		KASSERT(divert != NULL);
		rtableid = divert->rdomain;
	}
#endif
	if (in_pcbset_addr(inp, src, dst, rtableid))
		goto resetandabort;

	/*
	 * Give the new socket our cached route reference.
	 */
	inp->inp_route = sc->sc_route;		/* struct assignment */
	sc->sc_route.ro_rt = NULL;

	tp->t_flags = intotcpcb(listeninp)->t_flags & (TF_NOPUSH|TF_NODELAY);
	if (sc->sc_request_r_scale != 15) {
		tp->requested_s_scale = sc->sc_requested_s_scale;
		tp->request_r_scale = sc->sc_request_r_scale;
		tp->t_flags |= TF_REQ_SCALE|TF_RCVD_SCALE;
	}
	if (ISSET(sc->sc_fixflags, SCF_TIMESTAMP))
		tp->t_flags |= TF_REQ_TSTMP|TF_RCVD_TSTMP;

	tp->t_template = tcp_template(tp);
	if (tp->t_template == NULL)
		goto abort;
	tp->sack_enable = ISSET(sc->sc_fixflags, SCF_SACK_PERMIT);
	tp->ts_modulate = sc->sc_modulate;
	tp->ts_recent = sc->sc_timestamp;
	tp->iss = sc->sc_iss;
	tp->irs = sc->sc_irs;
	tcp_sendseqinit(tp);
	tp->snd_last = tp->snd_una;
#ifdef TCP_ECN
	if (ISSET(sc->sc_fixflags, SCF_ECN_PERMIT)) {
		tp->t_flags |= TF_ECN_PERMIT;
		tcpstat_inc(tcps_ecn_accepts);
	}
#endif
	if (ISSET(sc->sc_fixflags, SCF_SACK_PERMIT))
		tp->t_flags |= TF_SACK_PERMIT;
#ifdef TCP_SIGNATURE
	if (ISSET(sc->sc_fixflags, SCF_SIGNATURE))
		tp->t_flags |= TF_SIGNATURE;
#endif
	tcp_rcvseqinit(tp);
	tp->t_state = TCPS_SYN_RECEIVED;
	tp->t_rcvtime = now;
	tp->t_sndtime = now;
	tp->t_rcvacktime = now;
	tp->t_sndacktime = now;
	TCP_TIMER_ARM(tp, TCPT_KEEP, atomic_load_int(&tcp_keepinit));
	tcpstat_inc(tcps_accepts);

	tcp_mss(tp, sc->sc_peermaxseg);	 /* sets t_maxseg */
	if (sc->sc_peermaxseg)
		tcp_mss_update(tp);
	/* Reset initial window to 1 segment for retransmit */
	if (READ_ONCE(sc->sc_rxtshift) > 0)
		tp->snd_cwnd = tp->t_maxseg;
	tp->snd_wl1 = sc->sc_irs;
	tp->rcv_up = sc->sc_irs + 1;

	/*
	 * This is what would have happened in tcp_output() when
	 * the SYN,ACK was sent.
	 */
	tp->snd_up = tp->snd_una;
	tp->snd_max = tp->snd_nxt = tp->iss+1;
	TCP_TIMER_ARM(tp, TCPT_REXMT, tp->t_rxtcur);
	if (sc->sc_win > 0 && SEQ_GT(tp->rcv_nxt + sc->sc_win, tp->rcv_adv))
		tp->rcv_adv = tp->rcv_nxt + sc->sc_win;
	tp->last_ack_sent = tp->rcv_nxt;

	in_pcbsounlock(listeninp, listenso);
	tcpstat_inc(tcps_sc_completed);
	syn_cache_put(sc);
	return (so);

resetandabort:
	tcp_respond(NULL, mtod(m, caddr_t), th, (tcp_seq)0, th->th_ack, TH_RST,
	    m->m_pkthdr.ph_rtableid, now);
abort:
	if (tp != NULL)
		tp = tcp_drop(tp, ECONNABORTED);	/* destroys socket */
	in_pcbsounlock(inp, so);
	in_pcbsounlock(listeninp, listenso);
	in_pcbunref(inp);
	m_freem(m);
	syn_cache_put(sc);
	tcpstat_inc(tcps_sc_aborted);
	return ((struct socket *)(-1));
}

/*
 * This function is called when we get a RST for a
 * non-existent connection, so that we can see if the
 * connection is in the syn cache.  If it is, zap it.
 */

void
syn_cache_reset(struct sockaddr *src, struct sockaddr *dst, struct tcphdr *th,
    u_int rtableid)
{
	struct syn_cache *sc;
	struct syn_cache_head *scp;

	NET_ASSERT_LOCKED();

	mtx_enter(&syn_cache_mtx);
	sc = syn_cache_lookup(src, dst, &scp, rtableid);
	if (sc == NULL) {
		mtx_leave(&syn_cache_mtx);
		return;
	}
	if (SEQ_LT(th->th_seq, sc->sc_irs) ||
	    SEQ_GT(th->th_seq, sc->sc_irs + 1)) {
		mtx_leave(&syn_cache_mtx);
		return;
	}
	syn_cache_rm(sc);
	mtx_leave(&syn_cache_mtx);
	tcpstat_inc(tcps_sc_reset);
	syn_cache_put(sc);
}

void
syn_cache_unreach(const struct sockaddr *src, const struct sockaddr *dst,
    struct tcphdr *th, u_int rtableid)
{
	struct syn_cache *sc;
	struct syn_cache_head *scp;

	NET_ASSERT_LOCKED();

	mtx_enter(&syn_cache_mtx);
	sc = syn_cache_lookup(src, dst, &scp, rtableid);
	if (sc == NULL) {
		mtx_leave(&syn_cache_mtx);
		return;
	}
	/* If the sequence number != sc_iss, then it's a bogus ICMP msg */
	if (ntohl (th->th_seq) != sc->sc_iss) {
		mtx_leave(&syn_cache_mtx);
		return;
	}

	/*
	 * If we've retransmitted 3 times and this is our second error,
	 * we remove the entry.  Otherwise, we allow it to continue on.
	 * This prevents us from incorrectly nuking an entry during a
	 * spurious network outage.
	 *
	 * See tcp_notify().
	 */
	if (!ISSET(sc->sc_dynflags, SCF_UNREACH) || sc->sc_rxtshift < 3) {
		SET(sc->sc_dynflags, SCF_UNREACH);
		mtx_leave(&syn_cache_mtx);
		return;
	}

	syn_cache_rm(sc);
	mtx_leave(&syn_cache_mtx);
	tcpstat_inc(tcps_sc_unreach);
	syn_cache_put(sc);
}

/*
 * Given a LISTEN socket and an inbound SYN request, add
 * this to the syn cache, and send back a segment:
 *	<SEQ=ISS><ACK=RCV_NXT><CTL=SYN,ACK>
 * to the source.
 *
 * IMPORTANT NOTE: We do _NOT_ ACK data that might accompany the SYN.
 * Doing so would require that we hold onto the data and deliver it
 * to the application.  However, if we are the target of a SYN-flood
 * DoS attack, an attacker could send data which would eventually
 * consume all available buffer space if it were ACKed.  By not ACKing
 * the data, we avoid this DoS scenario.
 */

int
syn_cache_add(struct sockaddr *src, struct sockaddr *dst, struct tcphdr *th,
    u_int iphlen, struct socket *so, struct mbuf *m, u_char *optp, int optlen,
    struct tcp_opt_info *oi, tcp_seq *issp, uint64_t now, int do_ecn)
{
	struct tcpcb tb, *tp;
	long win;
	struct syn_cache *sc;
	struct syn_cache_head *scp;
	struct mbuf *ipopts;
	struct rtentry *rt = NULL;

	soassertlocked(so);

	tp = sototcpcb(so);

	/*
	 * RFC1122 4.2.3.10, p. 104: discard bcast/mcast SYN
	 *
	 * Note this check is performed in tcp_input() very early on.
	 */

	/*
	 * Initialize some local state.
	 */
	win = sbspace(&so->so_rcv);
	if (win > TCP_MAXWIN)
		win = TCP_MAXWIN;

	bzero(&tb, sizeof(tb));
	if (optp
#ifdef TCP_SIGNATURE
	    || (tp->t_flags & TF_SIGNATURE)
#endif
	    ) {
		tb.pf = tp->pf;
		tb.sack_enable = tp->sack_enable;
		tb.t_flags = atomic_load_int(&tcp_do_rfc1323) ?
		    (TF_REQ_SCALE|TF_REQ_TSTMP) : 0;
#ifdef TCP_SIGNATURE
		if (tp->t_flags & TF_SIGNATURE)
			tb.t_flags |= TF_SIGNATURE;
#endif
		tb.t_state = TCPS_LISTEN;
		if (tcp_dooptions(&tb, optp, optlen, th, m, iphlen, oi,
		    sotoinpcb(so)->inp_rtableid, now))
			return (-1);
	}

	switch (src->sa_family) {
	case AF_INET:
		/*
		 * Remember the IP options, if any.
		 */
		ipopts = ip_srcroute(m);
		break;
	default:
		ipopts = NULL;
	}

	/*
	 * See if we already have an entry for this connection.
	 * If we do, resend the SYN,ACK.  We do not count this
	 * as a retransmission (XXX though maybe we should).
	 */
	mtx_enter(&syn_cache_mtx);
	sc = syn_cache_lookup(src, dst, &scp, sotoinpcb(so)->inp_rtableid);
	if (sc != NULL) {
		refcnt_take(&sc->sc_refcnt);
		mtx_leave(&syn_cache_mtx);
		tcpstat_inc(tcps_sc_dupesyn);
		if (ipopts) {
			/*
			 * If we were remembering a previous source route,
			 * forget it and use the new one we've been given.
			 */
			m_free(sc->sc_ipopts);
			sc->sc_ipopts = ipopts;
		}
		sc->sc_timestamp = tb.ts_recent;
		if (syn_cache_respond(sc, m, now, do_ecn) == 0) {
			tcpstat_inc(tcps_sndacks);
			tcpstat_inc(tcps_sndtotal);
		}
		syn_cache_put(sc);
		return (0);
	}
	mtx_leave(&syn_cache_mtx);

	sc = pool_get(&syn_cache_pool, PR_NOWAIT|PR_ZERO);
	if (sc == NULL) {
		m_free(ipopts);
		return (-1);
	}
	refcnt_init_trace(&sc->sc_refcnt, DT_REFCNT_IDX_SYNCACHE);
	timeout_set_flags(&sc->sc_timer, syn_cache_timer, sc,
	    KCLOCK_NONE, TIMEOUT_PROC | TIMEOUT_MPSAFE);

	/*
	 * Fill in the cache, and put the necessary IP and TCP
	 * options into the reply.
	 */
	memcpy(&sc->sc_src, src, src->sa_len);
	memcpy(&sc->sc_dst, dst, dst->sa_len);
	sc->sc_rtableid = sotoinpcb(so)->inp_rtableid;
	switch (sc->sc_src.sa.sa_family) {
	case AF_INET:
		if (sc->sc_src.sin.sin_addr.s_addr != INADDR_ANY) {
			rt = route_mpath(&sc->sc_route,
			    &sc->sc_src.sin.sin_addr,
			    &sc->sc_dst.sin.sin_addr, sc->sc_rtableid);
		}
		break;
#ifdef INET6
	case AF_INET6:
		if (!IN6_IS_ADDR_UNSPECIFIED(&sc->sc_src.sin6.sin6_addr)) {
			rt = route6_mpath(&sc->sc_route,
			    &sc->sc_src.sin6.sin6_addr,
			    &sc->sc_dst.sin6.sin6_addr, sc->sc_rtableid);
		}
		break;
#endif
	}
	sc->sc_ipopts = ipopts;
	sc->sc_irs = th->th_seq;

	sc->sc_iss = issp ? *issp : arc4random();
	sc->sc_peermaxseg = oi->maxseg;
	sc->sc_ourmaxseg = tcp_mss_adv(rt, sc->sc_src.sa.sa_family);
	sc->sc_win = win;
	sc->sc_timestamp = tb.ts_recent;
	if ((tb.t_flags & (TF_REQ_TSTMP|TF_RCVD_TSTMP)) ==
	    (TF_REQ_TSTMP|TF_RCVD_TSTMP)) {
		SET(sc->sc_fixflags, SCF_TIMESTAMP);
		sc->sc_modulate = arc4random();
	}
	if ((tb.t_flags & (TF_RCVD_SCALE|TF_REQ_SCALE)) ==
	    (TF_RCVD_SCALE|TF_REQ_SCALE)) {
		sc->sc_requested_s_scale = tb.requested_s_scale;
		sc->sc_request_r_scale = 0;
		/*
		 * Pick the smallest possible scaling factor that
		 * will still allow us to scale up to sb_max.
		 *
		 * We do this because there are broken firewalls that
		 * will corrupt the window scale option, leading to
		 * the other endpoint believing that our advertised
		 * window is unscaled.  At scale factors larger than
		 * 5 the unscaled window will drop below 1500 bytes,
		 * leading to serious problems when traversing these
		 * broken firewalls.
		 *
		 * With the default sbmax of 256K, a scale factor
		 * of 3 will be chosen by this algorithm.  Those who
		 * choose a larger sbmax should watch out
		 * for the compatibility problems mentioned above.
		 *
		 * RFC1323: The Window field in a SYN (i.e., a <SYN>
		 * or <SYN,ACK>) segment itself is never scaled.
		 */
		while (sc->sc_request_r_scale < TCP_MAX_WINSHIFT &&
		    (TCP_MAXWIN << sc->sc_request_r_scale) < sb_max)
			sc->sc_request_r_scale++;
	} else {
		sc->sc_requested_s_scale = 15;
		sc->sc_request_r_scale = 15;
	}
#ifdef TCP_ECN
	/*
	 * if both ECE and CWR flag bits are set, peer is ECN capable.
	 */
	if (do_ecn && (th->th_flags & (TH_ECE|TH_CWR)) == (TH_ECE|TH_CWR))
		SET(sc->sc_fixflags, SCF_ECN_PERMIT);
#endif
	/*
	 * Set SCF_SACK_PERMIT if peer did send a SACK_PERMITTED option
	 * (i.e., if tcp_dooptions() did set TF_SACK_PERMIT).
	 */
	if (tb.sack_enable && (tb.t_flags & TF_SACK_PERMIT))
		SET(sc->sc_fixflags, SCF_SACK_PERMIT);
#ifdef TCP_SIGNATURE
	if (tb.t_flags & TF_SIGNATURE)
		SET(sc->sc_fixflags, SCF_SIGNATURE);
#endif
	sc->sc_inplisten = in_pcbref(tp->t_inpcb);
	if (syn_cache_respond(sc, m, now, do_ecn) == 0) {
		mtx_enter(&syn_cache_mtx);
		/*
		 * Socket lock prevents another insert after our
		 * syn_cache_lookup() and before syn_cache_insert().
		 */
		syn_cache_insert(sc, tp);
		mtx_leave(&syn_cache_mtx);
		tcpstat_inc(tcps_sndacks);
		tcpstat_inc(tcps_sndtotal);
	} else {
		in_pcbunref(sc->sc_inplisten);
		syn_cache_put(sc);
		tcpstat_inc(tcps_sc_dropped);
	}

	return (0);
}

int
syn_cache_respond(struct syn_cache *sc, struct mbuf *m, uint64_t now,
    int do_ecn)
{
	u_int8_t *optp;
	int optlen, error;
	u_int16_t tlen;
	struct ip *ip = NULL;
#ifdef INET6
	struct ip6_hdr *ip6 = NULL;
#endif
	struct tcphdr *th;
	u_int hlen;
	struct inpcb *inp;

	NET_ASSERT_LOCKED();

	switch (sc->sc_src.sa.sa_family) {
	case AF_INET:
		hlen = sizeof(struct ip);
		break;
#ifdef INET6
	case AF_INET6:
		hlen = sizeof(struct ip6_hdr);
		break;
#endif
	default:
		m_freem(m);
		return (EAFNOSUPPORT);
	}

	/* Compute the size of the TCP options. */
	optlen = 4 + (sc->sc_request_r_scale != 15 ? 4 : 0) +
	    (ISSET(sc->sc_fixflags, SCF_SACK_PERMIT) ? 4 : 0) +
#ifdef TCP_SIGNATURE
	    (ISSET(sc->sc_fixflags, SCF_SIGNATURE) ? TCPOLEN_SIGLEN : 0) +
#endif
	    (ISSET(sc->sc_fixflags, SCF_TIMESTAMP) ? TCPOLEN_TSTAMP_APPA : 0);

	tlen = hlen + sizeof(struct tcphdr) + optlen;

	/*
	 * Create the IP+TCP header from scratch.
	 */
	m_freem(m);
#ifdef DIAGNOSTIC
	if (max_linkhdr + tlen > MCLBYTES)
		return (ENOBUFS);
#endif
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m && max_linkhdr + tlen > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			m = NULL;
		}
	}
	if (m == NULL)
		return (ENOBUFS);

	/* Fixup the mbuf. */
	m->m_data += max_linkhdr;
	m->m_len = m->m_pkthdr.len = tlen;
	m->m_pkthdr.ph_ifidx = 0;
	m->m_pkthdr.ph_rtableid = sc->sc_rtableid;
	memset(mtod(m, u_char *), 0, tlen);

	switch (sc->sc_src.sa.sa_family) {
	case AF_INET:
		ip = mtod(m, struct ip *);
		ip->ip_dst = sc->sc_src.sin.sin_addr;
		ip->ip_src = sc->sc_dst.sin.sin_addr;
		ip->ip_p = IPPROTO_TCP;
		th = (struct tcphdr *)(ip + 1);
		th->th_dport = sc->sc_src.sin.sin_port;
		th->th_sport = sc->sc_dst.sin.sin_port;
		break;
#ifdef INET6
	case AF_INET6:
		ip6 = mtod(m, struct ip6_hdr *);
		ip6->ip6_dst = sc->sc_src.sin6.sin6_addr;
		ip6->ip6_src = sc->sc_dst.sin6.sin6_addr;
		ip6->ip6_nxt = IPPROTO_TCP;
		th = (struct tcphdr *)(ip6 + 1);
		th->th_dport = sc->sc_src.sin6.sin6_port;
		th->th_sport = sc->sc_dst.sin6.sin6_port;
		break;
#endif
	}

	th->th_seq = htonl(sc->sc_iss);
	th->th_ack = htonl(sc->sc_irs + 1);
	th->th_off = (sizeof(struct tcphdr) + optlen) >> 2;
	th->th_flags = TH_SYN|TH_ACK;
#ifdef TCP_ECN
	/* Set ECE for SYN-ACK if peer supports ECN. */
	if (do_ecn && ISSET(sc->sc_fixflags, SCF_ECN_PERMIT))
		th->th_flags |= TH_ECE;
#endif
	th->th_win = htons(sc->sc_win);
	/* th_sum already 0 */
	/* th_urp already 0 */

	/* Tack on the TCP options. */
	optp = (u_int8_t *)(th + 1);
	*optp++ = TCPOPT_MAXSEG;
	*optp++ = 4;
	*optp++ = (sc->sc_ourmaxseg >> 8) & 0xff;
	*optp++ = sc->sc_ourmaxseg & 0xff;

	/* Include SACK_PERMIT_HDR option if peer has already done so. */
	if (ISSET(sc->sc_fixflags, SCF_SACK_PERMIT)) {
		*((u_int32_t *)optp) = htonl(TCPOPT_SACK_PERMIT_HDR);
		optp += 4;
	}

	if (sc->sc_request_r_scale != 15) {
		*((u_int32_t *)optp) = htonl(TCPOPT_NOP << 24 |
		    TCPOPT_WINDOW << 16 | TCPOLEN_WINDOW << 8 |
		    sc->sc_request_r_scale);
		optp += 4;
	}

	if (ISSET(sc->sc_fixflags, SCF_TIMESTAMP)) {
		u_int32_t *lp = (u_int32_t *)(optp);
		/* Form timestamp option as shown in appendix A of RFC 1323. */
		*lp++ = htonl(TCPOPT_TSTAMP_HDR);
		*lp++ = htonl(now + sc->sc_modulate);
		*lp   = htonl(sc->sc_timestamp);
		optp += TCPOLEN_TSTAMP_APPA;
	}

#ifdef TCP_SIGNATURE
	if (ISSET(sc->sc_fixflags, SCF_SIGNATURE)) {
		union sockaddr_union src, dst;
		struct tdb *tdb;

		bzero(&src, sizeof(union sockaddr_union));
		bzero(&dst, sizeof(union sockaddr_union));
		src.sa.sa_len = sc->sc_src.sa.sa_len;
		src.sa.sa_family = sc->sc_src.sa.sa_family;
		dst.sa.sa_len = sc->sc_dst.sa.sa_len;
		dst.sa.sa_family = sc->sc_dst.sa.sa_family;

		switch (sc->sc_src.sa.sa_family) {
		case 0:	/*default to PF_INET*/
		case AF_INET:
			src.sin.sin_addr = mtod(m, struct ip *)->ip_src;
			dst.sin.sin_addr = mtod(m, struct ip *)->ip_dst;
			break;
#ifdef INET6
		case AF_INET6:
			src.sin6.sin6_addr = mtod(m, struct ip6_hdr *)->ip6_src;
			dst.sin6.sin6_addr = mtod(m, struct ip6_hdr *)->ip6_dst;
			break;
#endif /* INET6 */
		}

		tdb = gettdbbysrcdst(rtable_l2(sc->sc_rtableid),
		    0, &src, &dst, IPPROTO_TCP);
		if (tdb == NULL) {
			m_freem(m);
			return (EPERM);
		}

		/* Send signature option */
		*(optp++) = TCPOPT_SIGNATURE;
		*(optp++) = TCPOLEN_SIGNATURE;

		if (tcp_signature(tdb, sc->sc_src.sa.sa_family, m, th,
		    hlen, 0, optp) < 0) {
			m_freem(m);
			tdb_unref(tdb);
			return (EINVAL);
		}
		tdb_unref(tdb);
		optp += 16;

		/* Pad options list to the next 32 bit boundary and
		 * terminate it.
		 */
		*optp++ = TCPOPT_NOP;
		*optp++ = TCPOPT_EOL;
	}
#endif /* TCP_SIGNATURE */

	SET(m->m_pkthdr.csum_flags, M_TCP_CSUM_OUT);

	/* use IPsec policy and ttl from listening socket, on SYN ACK */
	mtx_enter(&syn_cache_mtx);
	inp = in_pcbref(sc->sc_inplisten);
	mtx_leave(&syn_cache_mtx);

	/*
	 * Fill in some straggling IP bits.  Note the stack expects
	 * ip_len to be in host order, for convenience.
	 */
	switch (sc->sc_src.sa.sa_family) {
	case AF_INET:
		ip->ip_len = htons(tlen);
		ip->ip_ttl = inp ?
		    inp->inp_ip.ip_ttl : atomic_load_int(&ip_defttl);
		if (inp != NULL)
			ip->ip_tos = inp->inp_ip.ip_tos;

		error = ip_output(m, sc->sc_ipopts, &sc->sc_route,
		    (atomic_load_int(&ip_mtudisc) ? IP_MTUDISC : 0),  NULL,
		    inp ? &inp->inp_seclevel : NULL, 0);
		break;
#ifdef INET6
	case AF_INET6:
		ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
		ip6->ip6_vfc |= IPV6_VERSION;
		/* ip6_plen will be updated in ip6_output() */
		ip6->ip6_hlim = in6_selecthlim(inp);
		/* leave flowlabel = 0, it is legal and require no state mgmt */

		error = ip6_output(m, NULL /*XXX*/, &sc->sc_route, 0,
		    NULL, inp ? &inp->inp_seclevel : NULL);
		break;
#endif
	}
	in_pcbunref(inp);
	return (error);
}

#ifndef SMALL_KERNEL
static int
tcp_softlro_check(struct mbuf *m, struct ether_extracted *ext)
{
	/* Don't merge packets with invalid TCP checksum. */
	if (!ISSET(m->m_pkthdr.csum_flags, M_TCP_CSUM_IN_OK))
		return 0;

	if (ext->ip4) {
		/* Don't merge packets with invalid IP header checksum. */
		if (!ISSET(m->m_pkthdr.csum_flags, M_IPV4_CSUM_IN_OK))
			return 0;

		/* Don't merge IPv4 packets with IP options. */
		if (ext->iphlen != sizeof(struct ip))
			return 0;
	}

	/* Check TCP protocol and header. */
	if (!ext->tcp)
		return 0;

	/* Don't merge empty TCP segments. */
	if (ext->paylen == 0)
		return 0;

	/* Just ACK and PUSH TCP flags are allowed. */
	if (ISSET(ext->tcp->th_flags, TH_ACK|TH_PUSH) != ext->tcp->th_flags)
		return 0;

	/* TCP ACK flag has to be set. */
	if (!ISSET(ext->tcp->th_flags, TH_ACK))
		return 0;

	/* Either no TCP options or timestamp as in RFC 1323 appendix A. */
	if (ext->tcphlen > sizeof(struct tcphdr)) {
		int optlen = ext->tcphlen - sizeof(struct tcphdr);
		uint8_t *optp = (uint8_t *)(ext->tcp + 1);

		/* Same logic as in TCP input quick retrieval. */
		if ((optlen != TCPOLEN_TSTAMP_APPA &&
		    (optlen <= TCPOLEN_TSTAMP_APPA ||
		    optp[TCPOLEN_TSTAMP_APPA] != TCPOPT_EOL)) ||
		    ((uint32_t *)optp)[0] != htonl(TCPOPT_TSTAMP_HDR))
			return 0;
	}

	return 1;
}

static int
tcp_softlro_compare(struct ether_extracted *head, struct ether_extracted *tail)
{
	/* Don't merge packets inside and outside of VLANs */
	if (head->evh && tail->evh) {
		/* Don't merge packets of different VLANs */
		if (EVL_VLANOFTAG(head->evh->evl_tag) !=
		    EVL_VLANOFTAG(tail->evh->evl_tag))
			return 0;

		/* Don't merge packets of different priorities */
		if (EVL_PRIOFTAG(head->evh->evl_tag) !=
		    EVL_PRIOFTAG(tail->evh->evl_tag))
			return 0;
	} else if (head->evh || tail->evh)
		return 0;

	/* Check TCP ports. */
	if (head->tcp->th_sport != tail->tcp->th_sport ||
	    head->tcp->th_dport != tail->tcp->th_dport)
		return 0;

	/* Check IP header. */
	if (head->ip4 && tail->ip4) {
		/* Check IPv4 addresses. */
		if (head->ip4->ip_src.s_addr != tail->ip4->ip_src.s_addr ||
		    head->ip4->ip_dst.s_addr != tail->ip4->ip_dst.s_addr)
			return 0;

		/* Check max. IPv4 length. */
		if (head->iplen + tail->iplen > IP_MAXPACKET - max_linkhdr)
			return 0;
	} else if (head->ip6 && tail->ip6) {
		/* Check IPv6 addresses. */
		if (!IN6_ARE_ADDR_EQUAL(&head->ip6->ip6_src,
		    &tail->ip6->ip6_src) ||
		    !IN6_ARE_ADDR_EQUAL(&head->ip6->ip6_dst,
		    &tail->ip6->ip6_dst))
			return 0;

		/* Check max. IPv6 length. */
		if ((head->iplen - head->iphlen) +
		    (tail->iplen - tail->iphlen) > IPV6_MAXPACKET - max_linkhdr)
			return 0;
	} else {
		/* Address family does not match. */
		return 0;
	}

	/* Check for contiguous segments. */
	if (ntohl(head->tcp->th_seq) + head->paylen != ntohl(tail->tcp->th_seq))
		return 0;

	/* Ignore segments with different TCP options. */
	if (head->tcphlen != tail->tcphlen)
		return 0;

	/* TCP timestamp options must match, type and length already checked. */
	if (head->tcphlen > sizeof(struct tcphdr)) {
		uint32_t *hoptp = (uint32_t *)(head->tcp + 1);
		uint32_t *toptp = (uint32_t *)(tail->tcp + 1);

		/* Tail timestamps must be more recent. */
		if (TSTMP_LT(ntohl(hoptp[1]), ntohl(toptp[1])) ||
		    TSTMP_LT(ntohl(hoptp[2]), ntohl(toptp[2])))
			return 0;
	}

	return 1;
}

static void
tcp_softlro_concat(struct mbuf *mhead, struct ether_extracted *head,
    struct mbuf *mtail, struct ether_extracted *tail)
{
	struct mbuf *m;
	unsigned int hdrlen;

	/* Adjust IP header length. */
	if (head->ip4) {
		head->ip4->ip_len = htons(head->iplen + tail->paylen);
	} else if (head->ip6) {
		head->ip6->ip6_plen =
		    htons(head->iplen - head->iphlen + tail->paylen);
	}

	/* Combine TCP flags from head and tail. */
	if (ISSET(tail->tcp->th_flags, TH_PUSH))
		SET(head->tcp->th_flags, TH_PUSH);

	/* Adjust TCP header. */
	head->tcp->th_win = tail->tcp->th_win;
	head->tcp->th_ack = tail->tcp->th_ack;

	/* Use more recent timestamps from tail. */
	if (head->tcphlen > sizeof(struct tcphdr)) {
		uint32_t *hoptp = (uint32_t *)(head->tcp + 1);
		uint32_t *toptp = (uint32_t *)(tail->tcp + 1);

		hoptp[1] = toptp[1];
		hoptp[2] = toptp[2];
	}

	/* Calculate header length of tail packet. */
	hdrlen = sizeof(*tail->eh);
	if (tail->evh)
		hdrlen = sizeof(*tail->evh);
	hdrlen += tail->iphlen;
	hdrlen += tail->tcphlen;

	/* Skip protocol headers in tail. */
	m_adj(mtail, hdrlen);
	CLR(mtail->m_flags, M_PKTHDR);

	/* Concatenate */
	for (m = mhead; m->m_next != NULL; m = m->m_next)
		;
	m->m_next = mtail;
	mhead->m_pkthdr.len += tail->paylen;

	/* Flag mbuf as TSO packet with MSS. */
	if (!ISSET(mhead->m_pkthdr.csum_flags, M_TCP_TSO)) {
		/* Set CSUM_OUT flags in case of forwarding. */
		SET(mhead->m_pkthdr.csum_flags, M_TCP_CSUM_OUT);
		head->tcp->th_sum = 0;
		if (head->ip4) {
			SET(mhead->m_pkthdr.csum_flags, M_IPV4_CSUM_OUT);
			head->ip4->ip_sum = 0;
		}

		SET(mhead->m_pkthdr.csum_flags, M_TCP_TSO);
		mhead->m_pkthdr.ph_mss = head->paylen;
		tcpstat_inc(tcps_inswlro);
		tcpstat_inc(tcps_inpktlro);	/* count head */
	}
	mhead->m_pkthdr.ph_mss = MAX(mhead->m_pkthdr.ph_mss, tail->paylen);
	tcpstat_inc(tcps_inpktlro);	/* count tail */
}

void
tcp_softlro_glue(struct mbuf_list *ml, struct mbuf *mtail, struct ifnet *ifp)
{
	struct ether_extracted head, tail;
	struct mbuf *mhead;

	if (!ISSET(ifp->if_xflags, IFXF_LRO))
		goto dontmerge;

	mtail->m_pkthdr.ph_mss = 0;

	ether_extract_headers(mtail, &tail);

	if (tail.tcp) {
		int tcpdatalen;

		/* Remove possible ethernet padding at the end. */
		tcpdatalen = tail.iplen - tail.iphlen - tail.tcphlen;
		if (tcpdatalen < tail.paylen ) {
			m_adj(mtail, tcpdatalen - tail.paylen);
			tail.paylen = tcpdatalen;
		}
	}

	if (!tcp_softlro_check(mtail, &tail))
		goto dontmerge;

	mtail->m_pkthdr.ph_mss = tail.paylen;

	for (mhead = ml->ml_head; mhead != NULL; mhead = mhead->m_nextpkt) {
		/* This packet has been checked and was not mergable before. */
		if (mhead->m_pkthdr.ph_mss == 0)
			continue;

		/* Use RSS hash to skip packets of different connections. */
		if (ISSET(mhead->m_pkthdr.csum_flags, M_FLOWID) &&
		    ISSET(mtail->m_pkthdr.csum_flags, M_FLOWID) &&
		    mhead->m_pkthdr.ph_flowid != mtail->m_pkthdr.ph_flowid)
			continue;

		/* Don't merge packets inside and outside of VLANs */
		if (ISSET(mhead->m_flags, M_VLANTAG) !=
		    ISSET(mtail->m_flags, M_VLANTAG))
			continue;

		if (ISSET(mhead->m_flags, M_VLANTAG)) {
			/* Don't merge packets of different VLANs */
			if (EVL_VLANOFTAG(mhead->m_pkthdr.ether_vtag) !=
			    EVL_VLANOFTAG(mtail->m_pkthdr.ether_vtag))
				continue;

			/* Don't merge packets of different priorities */
			if (EVL_PRIOFTAG(mhead->m_pkthdr.ether_vtag) !=
			    EVL_PRIOFTAG(mtail->m_pkthdr.ether_vtag))
				continue;
		}

		ether_extract_headers(mhead, &head);
		if (!tcp_softlro_compare(&head, &tail))
			continue;

		tcp_softlro_concat(mhead, &head, mtail, &tail);
		return;
	}
 dontmerge:
	ml_enqueue(ml, mtail);
}
#endif

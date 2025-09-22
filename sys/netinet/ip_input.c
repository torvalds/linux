/*	$OpenBSD: ip_input.c,v 1.425 2025/07/31 09:05:11 mvs Exp $	*/
/*	$NetBSD: ip_input.c,v 1.30 1996/03/16 23:53:58 christos Exp $	*/

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
 *	@(#)ip_input.c	8.2 (Berkeley) 1/4/94
 */

#include "pf.h"
#include "carp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/mutex.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/pool.h>
#include <sys/task.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/netisr.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <net/if_types.h>

#ifdef INET6
#include <netinet6/ip6_var.h>
#endif

#if NPF > 0
#include <net/pfvar.h>
#endif

#ifdef MROUTING
#include <netinet/ip_mroute.h>
#endif

#ifdef IPSEC
#include <netinet/ip_ipsp.h>
#endif /* IPSEC */

#if NCARP > 0
#include <netinet/ip_carp.h>
#endif

/*
 * Locks used to protect global variables in this file:
 *	I	immutable after creation
 *	N	net lock
 *	Q	ipq_mutex
 *	a	atomic operations
 */

/* values controllable via sysctl */
int	ip_forwarding = 0;			/* [a] */
int	ipmforwarding = 0;			/* [a] */
int	ipmultipath = 0;			/* [a] */
int	ip_sendredirects = 1;			/* [a] */
int	ip_dosourceroute = 0;			/* [a] */
int	ip_defttl = IPDEFTTL;			/* [a] */
int	ip_mtudisc = 1;				/* [a] */
int	ip_mtudisc_timeout = IPMTUDISCTIMEOUT;	/* [a] */
int	ip_directedbcast = 0;			/* [a] */

struct mutex	ipq_mutex = MUTEX_INITIALIZER(IPL_SOFTNET);

/* IP reassembly queue */
LIST_HEAD(, ipq) ipq;				/* [Q] */

/* Keep track of memory used for reassembly */
int	ip_maxqueue = 300;			/* [a] */
int	ip_frags = 0;				/* [Q] */

#ifndef SMALL_KERNEL
const struct sysctl_bounded_args ipctl_vars[] = {
	{ IPCTL_FORWARDING, &ip_forwarding, 0, 2 },
	{ IPCTL_SENDREDIRECTS, &ip_sendredirects, 0, 1 },
	{ IPCTL_DIRECTEDBCAST, &ip_directedbcast, 0, 1 },
#ifdef MROUTING
	{ IPCTL_MRTPROTO, &ip_mrtproto, SYSCTL_INT_READONLY },
#endif
	{ IPCTL_DEFTTL, &ip_defttl, 0, 255 },
	{ IPCTL_IPPORT_FIRSTAUTO, &ipport_firstauto, 0, 65535 },
	{ IPCTL_IPPORT_LASTAUTO, &ipport_lastauto, 0, 65535 },
	{ IPCTL_IPPORT_HIFIRSTAUTO, &ipport_hifirstauto, 0, 65535 },
	{ IPCTL_IPPORT_HILASTAUTO, &ipport_hilastauto, 0, 65535 },
	{ IPCTL_IPPORT_MAXQUEUE, &ip_maxqueue, 0, 10000 },
	{ IPCTL_MFORWARDING, &ipmforwarding, 0, 1 },
	{ IPCTL_ARPTIMEOUT, &arpt_keep, 0, INT_MAX },
	{ IPCTL_ARPDOWN, &arpt_down, 0, INT_MAX },
};
#endif /* SMALL_KERNEL */

struct niqueue ipintrq = NIQUEUE_INITIALIZER(IPQ_MAXLEN, NETISR_IP);

struct pool ipqent_pool;
struct pool ipq_pool;

struct cpumem *ipcounters;

int ip_sysctl_ipstat(void *, size_t *, void *);

static struct mbuf_queue	ipsend_mq;
static struct mbuf_queue	ipsendraw_mq;

extern struct niqueue		arpinq;

int	ip_ours(struct mbuf **, int *, int, int, struct netstack *);
int	ip_ours_enqueue(struct mbuf **mp, int *offp, int nxt);
int	ip_dooptions(struct mbuf *, struct ifnet *, int);
int	in_ouraddr(struct mbuf *, struct ifnet *, struct route *, int);

int		ip_fragcheck(struct mbuf **, int *);
struct mbuf *	ip_reass(struct ipqent *, struct ipq *);
void		ip_freef(struct ipq *);
void		ip_flush(int);

static void ip_send_dispatch(void *);
static void ip_sendraw_dispatch(void *);
static struct task ipsend_task = TASK_INITIALIZER(ip_send_dispatch, &ipsend_mq);
static struct task ipsendraw_task =
	TASK_INITIALIZER(ip_sendraw_dispatch, &ipsendraw_mq);

/*
 * Used to save the IP options in case a protocol wants to respond
 * to an incoming packet over the same route if the packet got here
 * using IP source routing.  This allows connection establishment and
 * maintenance when the remote end is on a network that is not known
 * to us.
 */
struct ip_srcrt {
	int		isr_nhops;		   /* number of hops */
	struct in_addr	isr_dst;		   /* final destination */
	char		isr_nop;		   /* one NOP to align */
	char		isr_hdr[IPOPT_OFFSET + 1]; /* OPTVAL, OLEN & OFFSET */
	struct in_addr	isr_routes[MAX_IPOPTLEN/sizeof(struct in_addr)];
};

void save_rte(struct mbuf *, u_char *, struct in_addr);

/*
 * IP initialization: fill in IP protocol switch table.
 * All protocols not implemented in kernel go to raw IP protocol handler.
 */
void
ip_init(void)
{
	const struct protosw *pr;
	int i;
	const u_int16_t defbaddynamicports_tcp[] = DEFBADDYNAMICPORTS_TCP;
	const u_int16_t defbaddynamicports_udp[] = DEFBADDYNAMICPORTS_UDP;
	const u_int16_t defrootonlyports_tcp[] = DEFROOTONLYPORTS_TCP;
	const u_int16_t defrootonlyports_udp[] = DEFROOTONLYPORTS_UDP;

	ipcounters = counters_alloc(ips_ncounters);

	pool_init(&ipqent_pool, sizeof(struct ipqent), 0,
	    IPL_SOFTNET, 0, "ipqe",  NULL);
	pool_init(&ipq_pool, sizeof(struct ipq), 0,
	    IPL_SOFTNET, 0, "ipq", NULL);

	pr = pffindproto(PF_INET, IPPROTO_RAW, SOCK_RAW);
	if (pr == NULL)
		panic("ip_init");
	for (i = 0; i < IPPROTO_MAX; i++)
		ip_protox[i] = pr - inetsw;
	for (pr = inetdomain.dom_protosw;
	    pr < inetdomain.dom_protoswNPROTOSW; pr++)
		if (pr->pr_domain->dom_family == PF_INET &&
		    pr->pr_protocol && pr->pr_protocol != IPPROTO_RAW &&
		    pr->pr_protocol < IPPROTO_MAX)
			ip_protox[pr->pr_protocol] = pr - inetsw;
	LIST_INIT(&ipq);

	/* Fill in list of ports not to allocate dynamically. */
	memset(&baddynamicports, 0, sizeof(baddynamicports));
	for (i = 0; defbaddynamicports_tcp[i] != 0; i++)
		DP_SET(baddynamicports.tcp, defbaddynamicports_tcp[i]);
	for (i = 0; defbaddynamicports_udp[i] != 0; i++)
		DP_SET(baddynamicports.udp, defbaddynamicports_udp[i]);

	/* Fill in list of ports only root can bind to. */
	memset(&rootonlyports, 0, sizeof(rootonlyports));
	for (i = 0; defrootonlyports_tcp[i] != 0; i++)
		DP_SET(rootonlyports.tcp, defrootonlyports_tcp[i]);
	for (i = 0; defrootonlyports_udp[i] != 0; i++)
		DP_SET(rootonlyports.udp, defrootonlyports_udp[i]);

	mq_init(&ipsend_mq, 64, IPL_SOFTNET);
	mq_init(&ipsendraw_mq, 64, IPL_SOFTNET);

	arpinit();
#ifdef IPSEC
	ipsec_init();
#endif
#ifdef MROUTING
	mrt_init();
#endif
}

/*
 * Enqueue packet for local delivery.  Queuing is used as a boundary
 * between the network layer (input/forward path) running with
 * NET_LOCK_SHARED() and the transport layer needing it exclusively.
 */
int
ip_ours(struct mbuf **mp, int *offp, int nxt, int af, struct netstack *ns)
{
	nxt = ip_fragcheck(mp, offp);
	if (nxt == IPPROTO_DONE)
		return IPPROTO_DONE;

	/* We are already in a IPv4/IPv6 local deliver loop. */
	if (af != AF_UNSPEC)
		return nxt;

	nxt = ip_deliver(mp, offp, nxt, AF_INET, 1, ns);
	if (nxt == IPPROTO_DONE)
		return IPPROTO_DONE;

	return ip_ours_enqueue(mp, offp, nxt);
}

int
ip_ours_enqueue(struct mbuf **mp, int *offp, int nxt)
{
	/* save values for later, use after dequeue */
	if (*offp != sizeof(struct ip)) {
		struct m_tag *mtag;
		struct ipoffnxt *ion;

		/* mbuf tags are expensive, but only used for header options */
		mtag = m_tag_get(PACKET_TAG_IP_OFFNXT, sizeof(*ion),
		    M_NOWAIT);
		if (mtag == NULL) {
			ipstat_inc(ips_idropped);
			m_freemp(mp);
			return IPPROTO_DONE;
		}
		ion = (struct ipoffnxt *)(mtag + 1);
		ion->ion_off = *offp;
		ion->ion_nxt = nxt;

		m_tag_prepend(*mp, mtag);
	}

	niq_enqueue(&ipintrq, *mp);
	*mp = NULL;
	return IPPROTO_DONE;
}

/*
 * Dequeue and process locally delivered packets.
 * This is called with exclusive NET_LOCK().
 */
void
ipintr(void)
{
	struct mbuf *m;

	while ((m = niq_dequeue(&ipintrq)) != NULL) {
		struct m_tag *mtag;
		int off, nxt;

#ifdef DIAGNOSTIC
		if ((m->m_flags & M_PKTHDR) == 0)
			panic("ipintr no HDR");
#endif
		mtag = m_tag_find(m, PACKET_TAG_IP_OFFNXT, NULL);
		if (mtag != NULL) {
			struct ipoffnxt *ion;

			ion = (struct ipoffnxt *)(mtag + 1);
			off = ion->ion_off;
			nxt = ion->ion_nxt;

			m_tag_delete(m, mtag);
		} else {
			struct ip *ip;

			ip = mtod(m, struct ip *);
			off = ip->ip_hl << 2;
			nxt = ip->ip_p;
		}

		nxt = ip_deliver(&m, &off, nxt, AF_INET, 0, NULL);
		KASSERT(nxt == IPPROTO_DONE);
	}
}

/*
 * IPv4 input routine.
 *
 * Checksum and byte swap header.  Process options. Forward or deliver.
 */
void
ipv4_input(struct ifnet *ifp, struct mbuf *m, struct netstack *ns)
{
	int off, nxt;

	off = 0;
	nxt = ip_input_if(&m, &off, IPPROTO_IPV4, AF_UNSPEC, ifp, ns);
	KASSERT(nxt == IPPROTO_DONE);
}

struct mbuf *
ipv4_check(struct ifnet *ifp, struct mbuf *m)
{
	struct ip *ip;
	int hlen, len;

	if (m->m_len < sizeof(*ip)) {
		m = m_pullup(m, sizeof(*ip));
		if (m == NULL) {
			ipstat_inc(ips_toosmall);
			return (NULL);
		}
	}

	ip = mtod(m, struct ip *);
	if (ip->ip_v != IPVERSION) {
		ipstat_inc(ips_badvers);
		goto bad;
	}

	hlen = ip->ip_hl << 2;
	if (hlen < sizeof(*ip)) {	/* minimum header length */
		ipstat_inc(ips_badhlen);
		goto bad;
	}
	if (hlen > m->m_len) {
		m = m_pullup(m, hlen);
		if (m == NULL) {
			ipstat_inc(ips_badhlen);
			return (NULL);
		}
		ip = mtod(m, struct ip *);
	}

	/* 127/8 must not appear on wire - RFC1122 */
	if ((ntohl(ip->ip_dst.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET ||
	    (ntohl(ip->ip_src.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET) {
		if ((ifp->if_flags & IFF_LOOPBACK) == 0) {
			ipstat_inc(ips_badaddr);
			goto bad;
		}
	}

	if (!ISSET(m->m_pkthdr.csum_flags, M_IPV4_CSUM_IN_OK)) {
		if (ISSET(m->m_pkthdr.csum_flags, M_IPV4_CSUM_IN_BAD)) {
			ipstat_inc(ips_badsum);
			goto bad;
		}

		ipstat_inc(ips_inswcsum);
		if (in_cksum(m, hlen) != 0) {
			ipstat_inc(ips_badsum);
			goto bad;
		}

		SET(m->m_pkthdr.csum_flags, M_IPV4_CSUM_IN_OK);
	}

	/* Retrieve the packet length. */
	len = ntohs(ip->ip_len);

	/*
	 * Convert fields to host representation.
	 */
	if (len < hlen) {
		ipstat_inc(ips_badlen);
		goto bad;
	}

	/*
	 * Check that the amount of data in the buffers
	 * is at least as much as the IP header would have us expect.
	 * Trim mbufs if longer than we expect.
	 * Drop packet if shorter than we expect.
	 */
	if (m->m_pkthdr.len < len) {
		ipstat_inc(ips_tooshort);
		goto bad;
	}
	if (m->m_pkthdr.len > len) {
		if (m->m_len == m->m_pkthdr.len) {
			m->m_len = len;
			m->m_pkthdr.len = len;
		} else
			m_adj(m, len - m->m_pkthdr.len);
	}

	return (m);
bad:
	m_freem(m);
	return (NULL);
}

int
ip_input_if(struct mbuf **mp, int *offp, int nxt, int af, struct ifnet *ifp,
    struct netstack *ns)
{
	struct route iproute, *ro = NULL;
	struct mbuf *m;
	struct ip *ip;
	int hlen;
#if NPF > 0
	struct in_addr odst;
#endif
	int flags = 0;

	KASSERT(*offp == 0);

	ipstat_inc(ips_total);
	m = *mp = ipv4_check(ifp, *mp);
	if (m == NULL)
		goto bad;

	ip = mtod(m, struct ip *);

#if NCARP > 0
	if (carp_lsdrop(ifp, m, AF_INET, &ip->ip_src.s_addr,
	    &ip->ip_dst.s_addr, (ip->ip_p == IPPROTO_ICMP ? 0 : 1)))
		goto bad;
#endif

#if NPF > 0
	/*
	 * Packet filter
	 */
	odst = ip->ip_dst;
	if (pf_test(AF_INET, PF_IN, ifp, mp) != PF_PASS)
		goto bad;
	m = *mp;
	if (m == NULL)
		goto bad;

	ip = mtod(m, struct ip *);
	if (odst.s_addr != ip->ip_dst.s_addr)
		SET(flags, IP_REDIRECT);
#endif

	switch (atomic_load_int(&ip_forwarding)) {
	case 2:
		SET(flags, IP_FORWARDING_IPSEC);
		/* FALLTHROUGH */
	case 1:
		SET(flags, IP_FORWARDING);
		break;
	}
	if (atomic_load_int(&ip_directedbcast))
		SET(flags, IP_ALLOWBROADCAST);

	hlen = ip->ip_hl << 2;

	/*
	 * Process options and, if not destined for us,
	 * ship it on.  ip_dooptions returns 1 when an
	 * error was detected (causing an icmp message
	 * to be sent and the original packet to be freed).
	 */
	if (hlen > sizeof (struct ip) && ip_dooptions(m, ifp, flags)) {
		m = *mp = NULL;
		goto bad;
	}

	if (ns == NULL) {
		ro = &iproute;
		ro->ro_rt = NULL;
	} else {
		ro = &ns->ns_route;
	}
	switch (in_ouraddr(m, ifp, ro, flags)) {
	case 2:
		goto bad;
	case 1:
		nxt = ip_ours(mp, offp, nxt, af, ns);
		goto out;
	}

	if (IN_MULTICAST(ip->ip_dst.s_addr)) {
		/*
		 * Make sure M_MCAST is set.  It should theoretically
		 * already be there, but let's play safe because upper
		 * layers check for this flag.
		 */
		m->m_flags |= M_MCAST;

#ifdef MROUTING
		if (atomic_load_int(&ipmforwarding) &&
		    ip_mrouter[ifp->if_rdomain]) {
			int error;

			if (m->m_flags & M_EXT) {
				if ((m = *mp = m_pullup(m, hlen)) == NULL) {
					ipstat_inc(ips_toosmall);
					goto bad;
				}
				ip = mtod(m, struct ip *);
			}
			/*
			 * If we are acting as a multicast router, all
			 * incoming multicast packets are passed to the
			 * kernel-level multicast forwarding function.
			 * The packet is returned (relatively) intact; if
			 * ip_mforward() returns a non-zero value, the packet
			 * must be discarded, else it may be accepted below.
			 *
			 * (The IP ident field is put in the same byte order
			 * as expected when ip_mforward() is called from
			 * ip_output().)
			 */
			KERNEL_LOCK();
			error = ip_mforward(m, ifp, flags);
			KERNEL_UNLOCK();
			if (error) {
				ipstat_inc(ips_cantforward);
				goto bad;
			}

			/*
			 * The process-level routing daemon needs to receive
			 * all multicast IGMP packets, whether or not this
			 * host belongs to their destination groups.
			 */
			if (ip->ip_p == IPPROTO_IGMP) {
				nxt = ip_ours(mp, offp, nxt, af, ns);
				goto out;
			}
			ipstat_inc(ips_forward);
		}
#endif
		/*
		 * See if we belong to the destination multicast group on the
		 * arrival interface.
		 */
		if (!in_hasmulti(&ip->ip_dst, ifp)) {
			ipstat_inc(ips_notmember);
			if (!IN_LOCAL_GROUP(ip->ip_dst.s_addr))
				ipstat_inc(ips_cantforward);
			goto bad;
		}
		nxt = ip_ours(mp, offp, nxt, af, ns);
		goto out;
	}

#if NCARP > 0
	if (ip->ip_p == IPPROTO_ICMP &&
	    carp_lsdrop(ifp, m, AF_INET, &ip->ip_src.s_addr,
	    &ip->ip_dst.s_addr, 1))
		goto bad;
#endif
	/*
	 * Not for us; forward if possible and desirable.
	 */
	if (!ISSET(flags, IP_FORWARDING)) {
		ipstat_inc(ips_cantforward);
		goto bad;
	}
#ifdef IPSEC
	if (ipsec_in_use) {
		int rv;

		rv = ipsec_forward_check(m, hlen, AF_INET);
		if (rv != 0) {
			ipstat_inc(ips_cantforward);
			goto bad;
		}
		/*
		 * Fall through, forward packet. Outbound IPsec policy
		 * checking will occur in ip_output().
		 */
	}
#endif /* IPSEC */

	ip_forward(m, ifp, ro, flags);
	*mp = NULL;
	if (ro == &iproute)
		rtfree(ro->ro_rt);
	return IPPROTO_DONE;
 bad:
	nxt = IPPROTO_DONE;
	m_freemp(mp);
 out:
	if (ro == &iproute)
		rtfree(ro->ro_rt);
	return nxt;
}

int
ip_fragcheck(struct mbuf **mp, int *offp)
{
	struct ip *ip;
	struct ipq *fp;
	struct ipqent *ipqe;
	int hlen;
	uint16_t mff;

	ip = mtod(*mp, struct ip *);
	hlen = ip->ip_hl << 2;

	/*
	 * If offset or more fragments are set, must reassemble.
	 * Otherwise, nothing need be done.
	 * (We could look in the reassembly queue to see
	 * if the packet was previously fragmented,
	 * but it's not worth the time; just let them time out.)
	 */
	if (ISSET(ip->ip_off, htons(IP_OFFMASK | IP_MF))) {
		if ((*mp)->m_flags & M_EXT) {		/* XXX */
			if ((*mp = m_pullup(*mp, hlen)) == NULL) {
				ipstat_inc(ips_toosmall);
				return IPPROTO_DONE;
			}
			ip = mtod(*mp, struct ip *);
		}

		/*
		 * Adjust ip_len to not reflect header,
		 * set ipqe_mff if more fragments are expected,
		 * convert offset of this to bytes.
		 */
		ip->ip_len = htons(ntohs(ip->ip_len) - hlen);
		mff = ISSET(ip->ip_off, htons(IP_MF));
		if (mff) {
			/*
			 * Make sure that fragments have a data length
			 * that's a non-zero multiple of 8 bytes.
			 */
			if (ntohs(ip->ip_len) == 0 ||
			    (ntohs(ip->ip_len) & 0x7) != 0) {
				ipstat_inc(ips_badfrags);
				m_freemp(mp);
				return IPPROTO_DONE;
			}
		}
		ip->ip_off = htons(ntohs(ip->ip_off) << 3);

		mtx_enter(&ipq_mutex);

		/*
		 * Look for queue of fragments
		 * of this datagram.
		 */
		LIST_FOREACH(fp, &ipq, ipq_q) {
			if (ip->ip_id == fp->ipq_id &&
			    ip->ip_src.s_addr == fp->ipq_src.s_addr &&
			    ip->ip_dst.s_addr == fp->ipq_dst.s_addr &&
			    ip->ip_p == fp->ipq_p)
				break;
		}

		/*
		 * If datagram marked as having more fragments
		 * or if this is not the first fragment,
		 * attempt reassembly; if it succeeds, proceed.
		 */
		if (mff || ip->ip_off) {
			int ip_maxqueue_local = atomic_load_int(&ip_maxqueue);

			ipstat_inc(ips_fragments);
			if (ip_frags + 1 > ip_maxqueue_local) {
				ip_flush(ip_maxqueue_local);
				ipstat_inc(ips_rcvmemdrop);
				goto bad;
			}

			ipqe = pool_get(&ipqent_pool, PR_NOWAIT);
			if (ipqe == NULL) {
				ipstat_inc(ips_rcvmemdrop);
				goto bad;
			}
			ip_frags++;
			ipqe->ipqe_mff = mff;
			ipqe->ipqe_m = *mp;
			ipqe->ipqe_ip = ip;
			*mp = ip_reass(ipqe, fp);
			if (*mp == NULL)
				goto bad;
			ipstat_inc(ips_reassembled);
			ip = mtod(*mp, struct ip *);
			hlen = ip->ip_hl << 2;
			ip->ip_len = htons(ntohs(ip->ip_len) + hlen);
		} else {
			if (fp != NULL)
				ip_freef(fp);
		}

		mtx_leave(&ipq_mutex);
	}

	*offp = hlen;
	return ip->ip_p;

 bad:
	mtx_leave(&ipq_mutex);
	m_freemp(mp);
	return IPPROTO_DONE;
}

#ifndef INET6
#define IPSTAT_INC(name)	ipstat_inc(ips_##name)
#else
#define IPSTAT_INC(name)	(af == AF_INET ?	\
    ipstat_inc(ips_##name) : ip6stat_inc(ip6s_##name))
#endif

int
ip_deliver(struct mbuf **mp, int *offp, int nxt, int af, int shared,
    struct netstack *ns)
{
#ifdef INET6
	int nest = 0;
#endif

	/*
	 * Tell launch routine the next header
	 */
	IPSTAT_INC(delivered);

	while (nxt != IPPROTO_DONE) {
		const struct protosw *psw;
		int naf;

		switch (af) {
		case AF_INET:
			psw = &inetsw[ip_protox[nxt]];
			break;
#ifdef INET6
		case AF_INET6:
			psw = &inet6sw[ip6_protox[nxt]];
			break;
#endif
		}
		if (shared && !ISSET(psw->pr_flags, PR_MPINPUT)) {
			/* delivery not finished, decrement counter, queue */
			switch (af) {
			case AF_INET:
				counters_dec(ipcounters, ips_delivered);
				return ip_ours_enqueue(mp, offp, nxt);
#ifdef INET6
			case AF_INET6:
				counters_dec(ip6counters, ip6s_delivered);
				return ip6_ours_enqueue(mp, offp, nxt);
#endif
			}
			break;
		}

#ifdef INET6
		if (af == AF_INET6 &&
		    (++nest > atomic_load_int(&ip6_hdrnestlimit))) {
			ip6stat_inc(ip6s_toomanyhdr);
			goto bad;
		}
#endif

		/*
		 * protection against faulty packet - there should be
		 * more sanity checks in header chain processing.
		 */
		if ((*mp)->m_pkthdr.len < *offp) {
			IPSTAT_INC(tooshort);
			goto bad;
		}

#ifdef IPSEC
		if (ipsec_in_use) {
			if (ipsec_local_check(*mp, *offp, nxt, af) != 0) {
				IPSTAT_INC(cantforward);
				goto bad;
			}
		}
		/* Otherwise, just fall through and deliver the packet */
#endif

		switch (nxt) {
		case IPPROTO_IPV4:
			naf = AF_INET;
			ipstat_inc(ips_delivered);
			break;
#ifdef INET6
		case IPPROTO_IPV6:
			naf = AF_INET6;
			ip6stat_inc(ip6s_delivered);
			break;
#endif
		default:
			naf = af;
			break;
		}
		nxt = (*psw->pr_input)(mp, offp, nxt, af, ns);
		af = naf;
	}
	return nxt;
 bad:
	m_freemp(mp);
	return IPPROTO_DONE;
}
#undef IPSTAT_INC

int
in_ouraddr(struct mbuf *m, struct ifnet *ifp, struct route *ro, int flags)
{
	struct rtentry		*rt;
	struct ip		*ip;
	int			 match = 0;

#if NPF > 0
	switch (pf_ouraddr(m)) {
	case 0:
		return (0);
	case 1:
		return (1);
	default:
		/* pf does not know it */
		break;
	}
#endif

	ip = mtod(m, struct ip *);

	if (ip->ip_dst.s_addr == INADDR_BROADCAST ||
	    ip->ip_dst.s_addr == INADDR_ANY) {
		m->m_flags |= M_BCAST;
		return (1);
	}

	rt = route_mpath(ro, &ip->ip_dst, &ip->ip_src, m->m_pkthdr.ph_rtableid);
	if (rt != NULL) {
		if (ISSET(rt->rt_flags, RTF_LOCAL))
			match = 1;

		/*
		 * If directedbcast is enabled we only consider it local
		 * if it is received on the interface with that address.
		 */
		if (ISSET(rt->rt_flags, RTF_BROADCAST) &&
		    (!ISSET(flags, IP_ALLOWBROADCAST) ||
		    rt->rt_ifidx == ifp->if_index)) {
			match = 1;

			/* Make sure M_BCAST is set */
			m->m_flags |= M_BCAST;
		}
	}

	if (!match) {
		struct ifaddr *ifa;

		/*
		 * No local address or broadcast address found, so check for
		 * ancient classful broadcast addresses.
		 * It must have been broadcast on the link layer, and for an
		 * address on the interface it was received on.
		 */
		if (!ISSET(m->m_flags, M_BCAST) ||
		    !IN_CLASSFULBROADCAST(ip->ip_dst.s_addr, ip->ip_dst.s_addr))
			return (0);

		if (ifp->if_rdomain != rtable_l2(m->m_pkthdr.ph_rtableid))
			return (0);
		/*
		 * The check in the loop assumes you only rx a packet on an UP
		 * interface, and that M_BCAST will only be set on a BROADCAST
		 * interface.
		 */
		NET_ASSERT_LOCKED();
		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;

			if (IN_CLASSFULBROADCAST(ip->ip_dst.s_addr,
			    ifatoia(ifa)->ia_addr.sin_addr.s_addr)) {
				match = 1;
				break;
			}
		}
	} else if (!ISSET(flags, IP_FORWARDING) &&
	    rt->rt_ifidx != ifp->if_index &&
	    !((ifp->if_flags & IFF_LOOPBACK) || (ifp->if_type == IFT_ENC) ||
	    (m->m_pkthdr.pf.flags & PF_TAG_TRANSLATE_LOCALHOST))) {
		/* received on wrong interface. */
#if NCARP > 0
		struct ifnet *out_if;

		/*
		 * Virtual IPs on carp interfaces need to be checked also
		 * against the parent interface and other carp interfaces
		 * sharing the same parent.
		 */
		out_if = if_get(rt->rt_ifidx);
		if (!(out_if && carp_strict_addr_chk(out_if, ifp))) {
			ipstat_inc(ips_wrongif);
			match = 2;
		}
		if_put(out_if);
#else
		ipstat_inc(ips_wrongif);
		match = 2;
#endif
	}

	return (match);
}

/*
 * Take incoming datagram fragment and try to
 * reassemble it into whole datagram.  If a chain for
 * reassembly of this datagram already exists, then it
 * is given as fp; otherwise have to make a chain.
 */
struct mbuf *
ip_reass(struct ipqent *ipqe, struct ipq *fp)
{
	struct mbuf *m = ipqe->ipqe_m;
	struct ipqent *nq, *p, *q;
	struct ip *ip;
	struct mbuf *t;
	int hlen = ipqe->ipqe_ip->ip_hl << 2;
	int i, next;
	u_int8_t ecn, ecn0;

	MUTEX_ASSERT_LOCKED(&ipq_mutex);

	/*
	 * Presence of header sizes in mbufs
	 * would confuse code below.
	 */
	m->m_data += hlen;
	m->m_len -= hlen;

	/*
	 * If first fragment to arrive, create a reassembly queue.
	 */
	if (fp == NULL) {
		fp = pool_get(&ipq_pool, PR_NOWAIT);
		if (fp == NULL)
			goto dropfrag;
		LIST_INSERT_HEAD(&ipq, fp, ipq_q);
		fp->ipq_ttl = IPFRAGTTL;
		fp->ipq_p = ipqe->ipqe_ip->ip_p;
		fp->ipq_id = ipqe->ipqe_ip->ip_id;
		LIST_INIT(&fp->ipq_fragq);
		fp->ipq_src = ipqe->ipqe_ip->ip_src;
		fp->ipq_dst = ipqe->ipqe_ip->ip_dst;
		p = NULL;
		goto insert;
	}

	/*
	 * Handle ECN by comparing this segment with the first one;
	 * if CE is set, do not lose CE.
	 * drop if CE and not-ECT are mixed for the same packet.
	 */
	ecn = ipqe->ipqe_ip->ip_tos & IPTOS_ECN_MASK;
	ecn0 = LIST_FIRST(&fp->ipq_fragq)->ipqe_ip->ip_tos & IPTOS_ECN_MASK;
	if (ecn == IPTOS_ECN_CE) {
		if (ecn0 == IPTOS_ECN_NOTECT)
			goto dropfrag;
		if (ecn0 != IPTOS_ECN_CE)
			LIST_FIRST(&fp->ipq_fragq)->ipqe_ip->ip_tos |=
			    IPTOS_ECN_CE;
	}
	if (ecn == IPTOS_ECN_NOTECT && ecn0 != IPTOS_ECN_NOTECT)
		goto dropfrag;

	/*
	 * Find a segment which begins after this one does.
	 */
	for (p = NULL, q = LIST_FIRST(&fp->ipq_fragq); q != NULL;
	    p = q, q = LIST_NEXT(q, ipqe_q))
		if (ntohs(q->ipqe_ip->ip_off) > ntohs(ipqe->ipqe_ip->ip_off))
			break;

	/*
	 * If there is a preceding segment, it may provide some of
	 * our data already.  If so, drop the data from the incoming
	 * segment.  If it provides all of our data, drop us.
	 */
	if (p != NULL) {
		i = ntohs(p->ipqe_ip->ip_off) + ntohs(p->ipqe_ip->ip_len) -
		    ntohs(ipqe->ipqe_ip->ip_off);
		if (i > 0) {
			if (i >= ntohs(ipqe->ipqe_ip->ip_len))
				goto dropfrag;
			m_adj(ipqe->ipqe_m, i);
			ipqe->ipqe_ip->ip_off =
			    htons(ntohs(ipqe->ipqe_ip->ip_off) + i);
			ipqe->ipqe_ip->ip_len =
			    htons(ntohs(ipqe->ipqe_ip->ip_len) - i);
		}
	}

	/*
	 * While we overlap succeeding segments trim them or,
	 * if they are completely covered, dequeue them.
	 */
	for (; q != NULL &&
	    ntohs(ipqe->ipqe_ip->ip_off) + ntohs(ipqe->ipqe_ip->ip_len) >
	    ntohs(q->ipqe_ip->ip_off); q = nq) {
		i = (ntohs(ipqe->ipqe_ip->ip_off) +
		    ntohs(ipqe->ipqe_ip->ip_len)) - ntohs(q->ipqe_ip->ip_off);
		if (i < ntohs(q->ipqe_ip->ip_len)) {
			q->ipqe_ip->ip_len =
			    htons(ntohs(q->ipqe_ip->ip_len) - i);
			q->ipqe_ip->ip_off =
			    htons(ntohs(q->ipqe_ip->ip_off) + i);
			m_adj(q->ipqe_m, i);
			break;
		}
		nq = LIST_NEXT(q, ipqe_q);
		m_freem(q->ipqe_m);
		LIST_REMOVE(q, ipqe_q);
		pool_put(&ipqent_pool, q);
		ip_frags--;
	}

insert:
	/*
	 * Stick new segment in its place;
	 * check for complete reassembly.
	 */
	if (p == NULL) {
		LIST_INSERT_HEAD(&fp->ipq_fragq, ipqe, ipqe_q);
	} else {
		LIST_INSERT_AFTER(p, ipqe, ipqe_q);
	}
	next = 0;
	for (p = NULL, q = LIST_FIRST(&fp->ipq_fragq); q != NULL;
	    p = q, q = LIST_NEXT(q, ipqe_q)) {
		if (ntohs(q->ipqe_ip->ip_off) != next)
			return (0);
		next += ntohs(q->ipqe_ip->ip_len);
	}
	if (p->ipqe_mff)
		return (0);

	/*
	 * Reassembly is complete.  Check for a bogus message size and
	 * concatenate fragments.
	 */
	q = LIST_FIRST(&fp->ipq_fragq);
	ip = q->ipqe_ip;
	if ((next + (ip->ip_hl << 2)) > IP_MAXPACKET) {
		ipstat_inc(ips_toolong);
		ip_freef(fp);
		return (0);
	}
	m = q->ipqe_m;
	t = m->m_next;
	m->m_next = 0;
	m_cat(m, t);
	nq = LIST_NEXT(q, ipqe_q);
	pool_put(&ipqent_pool, q);
	ip_frags--;
	for (q = nq; q != NULL; q = nq) {
		t = q->ipqe_m;
		nq = LIST_NEXT(q, ipqe_q);
		pool_put(&ipqent_pool, q);
		ip_frags--;
		m_removehdr(t);
		m_cat(m, t);
	}

	/*
	 * Create header for new ip packet by
	 * modifying header of first packet;
	 * dequeue and discard fragment reassembly header.
	 * Make header visible.
	 */
	ip->ip_len = htons(next);
	ip->ip_src = fp->ipq_src;
	ip->ip_dst = fp->ipq_dst;
	LIST_REMOVE(fp, ipq_q);
	pool_put(&ipq_pool, fp);
	m->m_len += (ip->ip_hl << 2);
	m->m_data -= (ip->ip_hl << 2);
	m_calchdrlen(m);
	return (m);

dropfrag:
	ipstat_inc(ips_fragdropped);
	m_freem(m);
	pool_put(&ipqent_pool, ipqe);
	ip_frags--;
	return (NULL);
}

/*
 * Free a fragment reassembly header and all
 * associated datagrams.
 */
void
ip_freef(struct ipq *fp)
{
	struct ipqent *q;

	MUTEX_ASSERT_LOCKED(&ipq_mutex);

	while ((q = LIST_FIRST(&fp->ipq_fragq)) != NULL) {
		LIST_REMOVE(q, ipqe_q);
		m_freem(q->ipqe_m);
		pool_put(&ipqent_pool, q);
		ip_frags--;
	}
	LIST_REMOVE(fp, ipq_q);
	pool_put(&ipq_pool, fp);
}

/*
 * IP timer processing;
 * if a timer expires on a reassembly queue, discard it.
 */
void
ip_slowtimo(void)
{
	struct ipq *fp, *nfp;

	mtx_enter(&ipq_mutex);
	LIST_FOREACH_SAFE(fp, &ipq, ipq_q, nfp) {
		if (--fp->ipq_ttl == 0) {
			ipstat_inc(ips_fragtimeout);
			ip_freef(fp);
		}
	}
	mtx_leave(&ipq_mutex);
}

/*
 * Flush a bunch of datagram fragments, till we are down to 75%.
 */
void
ip_flush(int maxqueue)
{
	int max = 50;

	MUTEX_ASSERT_LOCKED(&ipq_mutex);

	while (!LIST_EMPTY(&ipq) && ip_frags > maxqueue * 3 / 4 && --max) {
		ipstat_inc(ips_fragdropped);
		ip_freef(LIST_FIRST(&ipq));
	}
}

/*
 * Do option processing on a datagram,
 * possibly discarding it if bad options are encountered,
 * or forwarding it if source-routed.
 * Returns 1 if packet has been forwarded/freed,
 * 0 if the packet should be processed further.
 */
int
ip_dooptions(struct mbuf *m, struct ifnet *ifp, int flags)
{
	struct ip *ip = mtod(m, struct ip *);
	unsigned int rtableid = m->m_pkthdr.ph_rtableid;
	struct rtentry *rt;
	struct sockaddr_in ipaddr;
	u_char *cp;
	struct ip_timestamp ipt;
	struct in_ifaddr *ia;
	int opt, optlen, cnt, off, code, type = ICMP_PARAMPROB, forward = 0;
	struct in_addr sin, dst;
	u_int32_t ntime;

	dst = ip->ip_dst;
	cp = (u_char *)(ip + 1);
	cnt = (ip->ip_hl << 2) - sizeof (struct ip);

	KERNEL_LOCK();
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[IPOPT_OPTVAL];
		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP)
			optlen = 1;
		else {
			if (cnt < IPOPT_OLEN + sizeof(*cp)) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			optlen = cp[IPOPT_OLEN];
			if (optlen < IPOPT_OLEN + sizeof(*cp) || optlen > cnt) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
		}

		switch (opt) {

		default:
			break;

		/*
		 * Source routing with record.
		 * Find interface with current destination address.
		 * If none on this machine then drop if strictly routed,
		 * or do nothing if loosely routed.
		 * Record interface address and bring up next address
		 * component.  If strictly routed make sure next
		 * address is on directly accessible net.
		 */
		case IPOPT_LSRR:
		case IPOPT_SSRR:
			if (atomic_load_int(&ip_dosourceroute) == 0) {
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_SRCFAIL;
				goto bad;
			}
			if (optlen < IPOPT_OFFSET + sizeof(*cp)) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			if ((off = cp[IPOPT_OFFSET]) < IPOPT_MINOFF) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			memset(&ipaddr, 0, sizeof(ipaddr));
			ipaddr.sin_family = AF_INET;
			ipaddr.sin_len = sizeof(ipaddr);
			ipaddr.sin_addr = ip->ip_dst;
			ia = ifatoia(ifa_ifwithaddr(sintosa(&ipaddr),
			    m->m_pkthdr.ph_rtableid));
			if (ia == NULL) {
				if (opt == IPOPT_SSRR) {
					type = ICMP_UNREACH;
					code = ICMP_UNREACH_SRCFAIL;
					goto bad;
				}
				/*
				 * Loose routing, and not at next destination
				 * yet; nothing to do except forward.
				 */
				break;
			}
			off--;			/* 0 origin */
			if ((off + sizeof(struct in_addr)) > optlen) {
				/*
				 * End of source route.  Should be for us.
				 */
				save_rte(m, cp, ip->ip_src);
				break;
			}

			/*
			 * locate outgoing interface
			 */
			memset(&ipaddr, 0, sizeof(ipaddr));
			ipaddr.sin_family = AF_INET;
			ipaddr.sin_len = sizeof(ipaddr);
			memcpy(&ipaddr.sin_addr, cp + off,
			    sizeof(ipaddr.sin_addr));
			/* keep packet in the virtual instance */
			rt = rtalloc(sintosa(&ipaddr), RT_RESOLVE, rtableid);
			if (!rtisvalid(rt) || ((opt == IPOPT_SSRR) &&
			    ISSET(rt->rt_flags, RTF_GATEWAY))) {
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_SRCFAIL;
				rtfree(rt);
				goto bad;
			}
			ia = ifatoia(rt->rt_ifa);
			memcpy(cp + off, &ia->ia_addr.sin_addr,
			    sizeof(struct in_addr));
			rtfree(rt);
			cp[IPOPT_OFFSET] += sizeof(struct in_addr);
			ip->ip_dst = ipaddr.sin_addr;
			/*
			 * Let ip_intr's mcast routing check handle mcast pkts
			 */
			forward = !IN_MULTICAST(ip->ip_dst.s_addr);
			break;

		case IPOPT_RR:
			if (optlen < IPOPT_OFFSET + sizeof(*cp)) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			if ((off = cp[IPOPT_OFFSET]) < IPOPT_MINOFF) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}

			/*
			 * If no space remains, ignore.
			 */
			off--;			/* 0 origin */
			if ((off + sizeof(struct in_addr)) > optlen)
				break;
			memset(&ipaddr, 0, sizeof(ipaddr));
			ipaddr.sin_family = AF_INET;
			ipaddr.sin_len = sizeof(ipaddr);
			ipaddr.sin_addr = ip->ip_dst;
			/*
			 * locate outgoing interface; if we're the destination,
			 * use the incoming interface (should be same).
			 * Again keep the packet inside the virtual instance.
			 */
			rt = rtalloc(sintosa(&ipaddr), RT_RESOLVE, rtableid);
			if (!rtisvalid(rt)) {
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_HOST;
				rtfree(rt);
				goto bad;
			}
			ia = ifatoia(rt->rt_ifa);
			memcpy(cp + off, &ia->ia_addr.sin_addr,
			    sizeof(struct in_addr));
			rtfree(rt);
			cp[IPOPT_OFFSET] += sizeof(struct in_addr);
			break;

		case IPOPT_TS:
			code = cp - (u_char *)ip;
			if (optlen < sizeof(struct ip_timestamp))
				goto bad;
			memcpy(&ipt, cp, sizeof(struct ip_timestamp));
			if (ipt.ipt_ptr < 5 || ipt.ipt_len < 5)
				goto bad;
			if (ipt.ipt_ptr - 1 + sizeof(u_int32_t) > ipt.ipt_len) {
				if (++ipt.ipt_oflw == 0)
					goto bad;
				break;
			}
			memcpy(&sin, cp + ipt.ipt_ptr - 1, sizeof sin);
			switch (ipt.ipt_flg) {

			case IPOPT_TS_TSONLY:
				break;

			case IPOPT_TS_TSANDADDR:
				if (ipt.ipt_ptr - 1 + sizeof(u_int32_t) +
				    sizeof(struct in_addr) > ipt.ipt_len)
					goto bad;
				memset(&ipaddr, 0, sizeof(ipaddr));
				ipaddr.sin_family = AF_INET;
				ipaddr.sin_len = sizeof(ipaddr);
				ipaddr.sin_addr = dst;
				ia = ifatoia(ifaof_ifpforaddr(sintosa(&ipaddr),
				    ifp));
				if (ia == NULL)
					continue;
				memcpy(&sin, &ia->ia_addr.sin_addr,
				    sizeof(struct in_addr));
				ipt.ipt_ptr += sizeof(struct in_addr);
				break;

			case IPOPT_TS_PRESPEC:
				if (ipt.ipt_ptr - 1 + sizeof(u_int32_t) +
				    sizeof(struct in_addr) > ipt.ipt_len)
					goto bad;
				memset(&ipaddr, 0, sizeof(ipaddr));
				ipaddr.sin_family = AF_INET;
				ipaddr.sin_len = sizeof(ipaddr);
				ipaddr.sin_addr = sin;
				if (ifa_ifwithaddr(sintosa(&ipaddr),
				    m->m_pkthdr.ph_rtableid) == NULL)
					continue;
				ipt.ipt_ptr += sizeof(struct in_addr);
				break;

			default:
				/* XXX can't take &ipt->ipt_flg */
				code = (u_char *)&ipt.ipt_ptr -
				    (u_char *)ip + 1;
				goto bad;
			}
			ntime = iptime();
			memcpy(cp + ipt.ipt_ptr - 1, &ntime, sizeof(u_int32_t));
			ipt.ipt_ptr += sizeof(u_int32_t);
		}
	}
	KERNEL_UNLOCK();
	if (forward && ISSET(flags, IP_FORWARDING)) {
		ip_forward(m, ifp, NULL, flags | IP_REDIRECT);
		return (1);
	}
	return (0);
bad:
	KERNEL_UNLOCK();
	icmp_error(m, type, code, 0, 0);
	ipstat_inc(ips_badoptions);
	return (1);
}

/*
 * Save incoming source route for use in replies,
 * to be picked up later by ip_srcroute if the receiver is interested.
 */
void
save_rte(struct mbuf *m, u_char *option, struct in_addr dst)
{
	struct ip_srcrt *isr;
	struct m_tag *mtag;
	unsigned olen;

	olen = option[IPOPT_OLEN];
	if (olen > sizeof(isr->isr_hdr) + sizeof(isr->isr_routes))
		return;

	mtag = m_tag_get(PACKET_TAG_SRCROUTE, sizeof(*isr), M_NOWAIT);
	if (mtag == NULL) {
		ipstat_inc(ips_idropped);
		return;
	}
	isr = (struct ip_srcrt *)(mtag + 1);

	memcpy(isr->isr_hdr, option, olen);
	isr->isr_nhops = (olen - IPOPT_OFFSET - 1) / sizeof(struct in_addr);
	isr->isr_dst = dst;
	m_tag_prepend(m, mtag);
}

/*
 * Retrieve incoming source route for use in replies,
 * in the same form used by setsockopt.
 * The first hop is placed before the options, will be removed later.
 */
struct mbuf *
ip_srcroute(struct mbuf *m0)
{
	struct in_addr *p, *q;
	struct mbuf *m;
	struct ip_srcrt *isr;
	struct m_tag *mtag;

	if (atomic_load_int(&ip_dosourceroute) == 0)
		return (NULL);

	mtag = m_tag_find(m0, PACKET_TAG_SRCROUTE, NULL);
	if (mtag == NULL)
		return (NULL);
	isr = (struct ip_srcrt *)(mtag + 1);

	if (isr->isr_nhops == 0)
		return (NULL);
	m = m_get(M_DONTWAIT, MT_SOOPTS);
	if (m == NULL) {
		ipstat_inc(ips_idropped);
		return (NULL);
	}

#define OPTSIZ	(sizeof(isr->isr_nop) + sizeof(isr->isr_hdr))

	/* length is (nhops+1)*sizeof(addr) + sizeof(nop + header) */
	m->m_len = (isr->isr_nhops + 1) * sizeof(struct in_addr) + OPTSIZ;

	/*
	 * First save first hop for return route
	 */
	p = &(isr->isr_routes[isr->isr_nhops - 1]);
	*(mtod(m, struct in_addr *)) = *p--;

	/*
	 * Copy option fields and padding (nop) to mbuf.
	 */
	isr->isr_nop = IPOPT_NOP;
	isr->isr_hdr[IPOPT_OFFSET] = IPOPT_MINOFF;
	memcpy(mtod(m, caddr_t) + sizeof(struct in_addr), &isr->isr_nop,
	    OPTSIZ);
	q = (struct in_addr *)(mtod(m, caddr_t) +
	    sizeof(struct in_addr) + OPTSIZ);
#undef OPTSIZ
	/*
	 * Record return path as an IP source route,
	 * reversing the path (pointers are now aligned).
	 */
	while (p >= isr->isr_routes) {
		*q++ = *p--;
	}
	/*
	 * Last hop goes to final destination.
	 */
	*q = isr->isr_dst;
	m_tag_delete(m0, (struct m_tag *)isr);
	return (m);
}

/*
 * Strip out IP options, at higher level protocol in the kernel.
 */
void
ip_stripoptions(struct mbuf *m)
{
	int i;
	struct ip *ip = mtod(m, struct ip *);
	caddr_t opts;
	int olen;

	olen = (ip->ip_hl<<2) - sizeof (struct ip);
	opts = (caddr_t)(ip + 1);
	i = m->m_len - (sizeof (struct ip) + olen);
	memmove(opts, opts  + olen, i);
	m->m_len -= olen;
	if (m->m_flags & M_PKTHDR)
		m->m_pkthdr.len -= olen;
	ip->ip_hl = sizeof(struct ip) >> 2;
	ip->ip_len = htons(ntohs(ip->ip_len) - olen);
}

const u_char inetctlerrmap[PRC_NCMDS] = {
	0,		0,		0,		0,
	0,		EMSGSIZE,	EHOSTDOWN,	EHOSTUNREACH,
	EHOSTUNREACH,	EHOSTUNREACH,	ECONNREFUSED,	ECONNREFUSED,
	EMSGSIZE,	EHOSTUNREACH,	0,		0,
	0,		0,		0,		0,
	ENOPROTOOPT
};

/*
 * Forward a packet.  If some error occurs return the sender
 * an icmp packet.  Note we can't always generate a meaningful
 * icmp message because icmp doesn't have a large enough repertoire
 * of codes and types.
 *
 * If not forwarding, just drop the packet.  This could be confusing
 * if ip_forwarding was zero but some routing protocol was advancing
 * us as a gateway to somewhere.  However, we must let the routing
 * protocol deal with that.
 *
 * The srcrt parameter indicates whether the packet is being forwarded
 * via a source route.
 */
void
ip_forward(struct mbuf *m, struct ifnet *ifp, struct route *ro, int flags)
{
	struct ip *ip = mtod(m, struct ip *);
	struct route iproute;
	struct rtentry *rt;
	u_int rtableid = m->m_pkthdr.ph_rtableid;
	u_int8_t loopcnt = m->m_pkthdr.ph_loopcnt;
	u_int icmp_len;
	char icmp_buf[68];
	CTASSERT(sizeof(icmp_buf) <= MHLEN);
	u_short mflags, pfflags;
	struct mbuf *mcopy;
	int error = 0, type = 0, code = 0, destmtu = 0;
	u_int32_t dest;

	dest = 0;
	if (m->m_flags & (M_BCAST|M_MCAST) || in_canforward(ip->ip_dst) == 0) {
		ipstat_inc(ips_cantforward);
		m_freem(m);
		goto done;
	}
	if (ip->ip_ttl <= IPTTLDEC) {
		icmp_error(m, ICMP_TIMXCEED, ICMP_TIMXCEED_INTRANS, dest, 0);
		goto done;
	}

	if (ro == NULL) {
		ro = &iproute;
		ro->ro_rt = NULL;
	}
	rt = route_mpath(ro, &ip->ip_dst, &ip->ip_src, rtableid);
	if (rt == NULL) {
		ipstat_inc(ips_noroute);
		icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_HOST, dest, 0);
		goto done;
	}

	/*
	 * Save at most 68 bytes of the packet in case we need to generate
	 * an ICMP message to the src.  The data is saved on the stack.
	 * A new mbuf is only allocated when ICMP is actually created.
	 */
	icmp_len = min(sizeof(icmp_buf), ntohs(ip->ip_len));
	mflags = m->m_flags;
	pfflags = m->m_pkthdr.pf.flags;
	m_copydata(m, 0, icmp_len, icmp_buf);

	ip->ip_ttl -= IPTTLDEC;

	/*
	 * If forwarding packet using same interface that it came in on,
	 * perhaps should send a redirect to sender to shortcut a hop.
	 * Only send redirect if source is sending directly to us,
	 * and if packet was not source routed (or has any options).
	 * Also, don't send redirect if forwarding using a default route
	 * or a route modified by a redirect.
	 * Don't send redirect if we advertise destination's arp address
	 * as ours (proxy arp).
	 */
	if (rt->rt_ifidx == ifp->if_index &&
	    !ISSET(rt->rt_flags, RTF_DYNAMIC|RTF_MODIFIED) &&
	    satosin(rt_key(rt))->sin_addr.s_addr != INADDR_ANY &&
	    !ISSET(flags, IP_REDIRECT) &&
	    atomic_load_int(&ip_sendredirects) &&
	    !arpproxy(satosin(rt_key(rt))->sin_addr, rtableid)) {
		if ((ip->ip_src.s_addr & ifatoia(rt->rt_ifa)->ia_netmask) ==
		    ifatoia(rt->rt_ifa)->ia_net) {
		    if (rt->rt_flags & RTF_GATEWAY)
			dest = satosin(rt->rt_gateway)->sin_addr.s_addr;
		    else
			dest = ip->ip_dst.s_addr;
		    /* Router requirements says to only send host redirects */
		    type = ICMP_REDIRECT;
		    code = ICMP_REDIRECT_HOST;
		}
	}

	error = ip_output(m, NULL, ro, flags | IP_FORWARDING, NULL, NULL, 0);
	rt = ro->ro_rt;
	if (error)
		ipstat_inc(ips_cantforward);
	else {
		ipstat_inc(ips_forward);
		if (type)
			ipstat_inc(ips_redirectsent);
		else
			goto done;
	}
	switch (error) {
	case 0:				/* forwarded, but need redirect */
		/* type, code set above */
		break;

	case EMSGSIZE:
		type = ICMP_UNREACH;
		code = ICMP_UNREACH_NEEDFRAG;
		if (rt != NULL) {
			u_int rtmtu;

			rtmtu = atomic_load_int(&rt->rt_mtu);
			if (rtmtu != 0) {
				destmtu = rtmtu;
			} else {
				struct ifnet *destifp;

				destifp = if_get(rt->rt_ifidx);
				if (destifp != NULL)
					destmtu = destifp->if_mtu;
				if_put(destifp);
			}
		}
		ipstat_inc(ips_cantfrag);
		if (destmtu == 0)
			goto done;
		break;

	case EACCES:
		/*
		 * pf(4) blocked the packet. There is no need to send an ICMP
		 * packet back since pf(4) takes care of it.
		 */
		goto done;

	case ENOBUFS:
		/*
		 * a router should not generate ICMP_SOURCEQUENCH as
		 * required in RFC1812 Requirements for IP Version 4 Routers.
		 * source quench could be a big problem under DoS attacks,
		 * or the underlying interface is rate-limited.
		 */
		goto done;

	case ENETUNREACH:		/* shouldn't happen, checked above */
	case EHOSTUNREACH:
	case ENETDOWN:
	case EHOSTDOWN:
	default:
		type = ICMP_UNREACH;
		code = ICMP_UNREACH_HOST;
		break;
	}

	mcopy = m_gethdr(M_DONTWAIT, MT_DATA);
	if (mcopy == NULL)
		goto done;
	mcopy->m_len = mcopy->m_pkthdr.len = icmp_len;
	mcopy->m_flags |= (mflags & M_COPYFLAGS);
	mcopy->m_pkthdr.ph_rtableid = rtableid;
	mcopy->m_pkthdr.ph_ifidx = ifp->if_index;
	mcopy->m_pkthdr.ph_loopcnt = loopcnt;
	mcopy->m_pkthdr.pf.flags |= (pfflags & PF_TAG_GENERATED);
	memcpy(mcopy->m_data, icmp_buf, icmp_len);
	icmp_error(mcopy, type, code, dest, destmtu);

 done:
	if (ro == &iproute)
		rtfree(ro->ro_rt);
}

#ifndef SMALL_KERNEL

int
ip_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	int oldval, newval, error;

	/* Almost all sysctl names at this level are terminal. */
	if (namelen != 1 && name[0] != IPCTL_IFQUEUE &&
	    name[0] != IPCTL_ARPQUEUE)
		return (ENOTDIR);

	switch (name[0]) {
	case IPCTL_SOURCEROUTE:
		return (sysctl_securelevel_int(oldp, oldlenp, newp, newlen,
		    &ip_dosourceroute));
	case IPCTL_MTUDISC:
		oldval = newval = atomic_load_int(&ip_mtudisc);
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &newval, 0, 1);
		if (error == 0 && oldval != newval &&
		    oldval == atomic_cas_uint(&ip_mtudisc, oldval, newval) &&
		    newval == 0) {
			NET_LOCK();
			rt_timer_queue_flush(&ip_mtudisc_timeout_q);
			NET_UNLOCK();
		}

		return (error);
	case IPCTL_MTUDISCTIMEOUT:
		oldval = newval = atomic_load_int(&ip_mtudisc_timeout);
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &newval, 0, INT_MAX);
		if (error == 0 && oldval != newval) {
			rw_enter_write(&sysctl_lock);
			atomic_store_int(&ip_mtudisc_timeout, newval);
			rt_timer_queue_change(&ip_mtudisc_timeout_q, newval);
			rw_exit_write(&sysctl_lock);
		}

		return (error);
#ifdef IPSEC
	case IPCTL_ENCDEBUG:
	case IPCTL_IPSEC_STATS:
	case IPCTL_IPSEC_EXPIRE_ACQUIRE:
	case IPCTL_IPSEC_EMBRYONIC_SA_TIMEOUT:
	case IPCTL_IPSEC_REQUIRE_PFS:
	case IPCTL_IPSEC_SOFT_ALLOCATIONS:
	case IPCTL_IPSEC_ALLOCATIONS:
	case IPCTL_IPSEC_SOFT_BYTES:
	case IPCTL_IPSEC_BYTES:
	case IPCTL_IPSEC_TIMEOUT:
	case IPCTL_IPSEC_SOFT_TIMEOUT:
	case IPCTL_IPSEC_SOFT_FIRSTUSE:
	case IPCTL_IPSEC_FIRSTUSE:
	case IPCTL_IPSEC_ENC_ALGORITHM:
	case IPCTL_IPSEC_AUTH_ALGORITHM:
	case IPCTL_IPSEC_IPCOMP_ALGORITHM:
		return (ipsec_sysctl(name, namelen, oldp, oldlenp, newp,
		    newlen));
#endif
	case IPCTL_IFQUEUE:
		return (sysctl_niq(name + 1, namelen - 1,
		    oldp, oldlenp, newp, newlen, &ipintrq));
	case IPCTL_ARPQUEUE:
		return (sysctl_niq(name + 1, namelen - 1,
		    oldp, oldlenp, newp, newlen, &arpinq));
	case IPCTL_ARPQUEUED:
		return (sysctl_rdint(oldp, oldlenp, newp,
		    atomic_load_int(&la_hold_total)));
	case IPCTL_STATS:
		return (ip_sysctl_ipstat(oldp, oldlenp, newp));
#ifdef MROUTING
	case IPCTL_MRTSTATS:
		return (mrt_sysctl_mrtstat(oldp, oldlenp, newp));
	case IPCTL_MRTMFC:
		if (newp)
			return (EPERM);
		return (mrt_sysctl_mfc(oldp, oldlenp));
	case IPCTL_MRTVIF:
		if (newp)
			return (EPERM);
		return (mrt_sysctl_vif(oldp, oldlenp));
#else
	case IPCTL_MRTPROTO:
	case IPCTL_MRTSTATS:
	case IPCTL_MRTMFC:
	case IPCTL_MRTVIF:
		return (EOPNOTSUPP);
#endif
	case IPCTL_MULTIPATH:
		oldval = newval = atomic_load_int(&ipmultipath);
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &newval, 0, 1);
		if (error == 0 && oldval != newval) {
			atomic_store_int(&ipmultipath, newval);
			membar_producer();
			atomic_inc_long(&rtgeneration);
		}

		return (error);
	default:
		return (sysctl_bounded_arr(ipctl_vars, nitems(ipctl_vars),
		    name, namelen, oldp, oldlenp, newp, newlen));
	}
	/* NOTREACHED */
}

int
ip_sysctl_ipstat(void *oldp, size_t *oldlenp, void *newp)
{
	uint64_t counters[ips_ncounters];
	struct ipstat ipstat;
	u_long *words = (u_long *)&ipstat;
	int i;

	CTASSERT(sizeof(ipstat) == (nitems(counters) * sizeof(u_long)));
	memset(&ipstat, 0, sizeof ipstat);
	counters_read(ipcounters, counters, nitems(counters), NULL);

	for (i = 0; i < nitems(counters); i++)
		words[i] = (u_long)counters[i];

	return (sysctl_rdstruct(oldp, oldlenp, newp, &ipstat, sizeof(ipstat)));
}
#endif /* SMALL_KERNEL */

void
ip_savecontrol(struct inpcb *inp, struct mbuf **mp, struct ip *ip,
    struct mbuf *m)
{
	if (inp->inp_socket->so_options & SO_TIMESTAMP) {
		struct timeval tv;

		m_microtime(m, &tv);
		*mp = sbcreatecontrol((caddr_t) &tv, sizeof(tv),
		    SCM_TIMESTAMP, SOL_SOCKET);
		if (*mp)
			mp = &(*mp)->m_next;
	}

	if (inp->inp_flags & INP_RECVDSTADDR) {
		*mp = sbcreatecontrol((caddr_t) &ip->ip_dst,
		    sizeof(struct in_addr), IP_RECVDSTADDR, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
#ifdef notyet
	/* this code is broken and will probably never be fixed. */
	/* options were tossed already */
	if (inp->inp_flags & INP_RECVOPTS) {
		*mp = sbcreatecontrol((caddr_t) opts_deleted_above,
		    sizeof(struct in_addr), IP_RECVOPTS, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
	/* ip_srcroute doesn't do what we want here, need to fix */
	if (inp->inp_flags & INP_RECVRETOPTS) {
		*mp = sbcreatecontrol((caddr_t) ip_srcroute(m),
		    sizeof(struct in_addr), IP_RECVRETOPTS, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
#endif
	if (inp->inp_flags & INP_RECVIF) {
		struct sockaddr_dl sdl;
		struct ifnet *ifp;

		ifp = if_get(m->m_pkthdr.ph_ifidx);
		if (ifp == NULL || ifp->if_sadl == NULL) {
			memset(&sdl, 0, sizeof(sdl));
			sdl.sdl_len = offsetof(struct sockaddr_dl, sdl_data[0]);
			sdl.sdl_family = AF_LINK;
			sdl.sdl_index = ifp != NULL ? ifp->if_index : 0;
			sdl.sdl_nlen = sdl.sdl_alen = sdl.sdl_slen = 0;
			*mp = sbcreatecontrol((caddr_t) &sdl, sdl.sdl_len,
			    IP_RECVIF, IPPROTO_IP);
		} else {
			*mp = sbcreatecontrol((caddr_t) ifp->if_sadl,
			    ifp->if_sadl->sdl_len, IP_RECVIF, IPPROTO_IP);
		}
		if (*mp)
			mp = &(*mp)->m_next;
		if_put(ifp);
	}
	if (inp->inp_flags & INP_RECVTTL) {
		*mp = sbcreatecontrol((caddr_t) &ip->ip_ttl,
		    sizeof(u_int8_t), IP_RECVTTL, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
	if (inp->inp_flags & INP_RECVRTABLE) {
		u_int rtableid = inp->inp_rtableid;

#if NPF > 0
		if (m && m->m_pkthdr.pf.flags & PF_TAG_DIVERTED) {
			struct pf_divert *divert;

			divert = pf_find_divert(m);
			KASSERT(divert != NULL);
			rtableid = divert->rdomain;
		}
#endif

		*mp = sbcreatecontrol((caddr_t) &rtableid,
		    sizeof(u_int), IP_RECVRTABLE, IPPROTO_IP);
		if (*mp)
			mp = &(*mp)->m_next;
	}
}

void
ip_send_do_dispatch(void *xmq, int flags)
{
	struct mbuf_queue *mq = xmq;
	struct mbuf *m;
	struct mbuf_list ml;
	struct m_tag *mtag;

	mq_delist(mq, &ml);
	if (ml_empty(&ml))
		return;

	NET_LOCK_SHARED();
	while ((m = ml_dequeue(&ml)) != NULL) {
		u_int32_t ipsecflowinfo = 0;

		if ((mtag = m_tag_find(m, PACKET_TAG_IPSEC_FLOWINFO, NULL))
		    != NULL) {
			ipsecflowinfo = *(u_int32_t *)(mtag + 1);
			m_tag_delete(m, mtag);
		}
		ip_output(m, NULL, NULL, flags, NULL, NULL, ipsecflowinfo);
	}
	NET_UNLOCK_SHARED();
}

void
ip_sendraw_dispatch(void *xmq)
{
	ip_send_do_dispatch(xmq, IP_RAWOUTPUT);
}

void
ip_send_dispatch(void *xmq)
{
	ip_send_do_dispatch(xmq, 0);
}

void
ip_send(struct mbuf *m)
{
	mq_enqueue(&ipsend_mq, m);
	task_add(net_tq(0), &ipsend_task);
}

void
ip_send_raw(struct mbuf *m)
{
	mq_enqueue(&ipsendraw_mq, m);
	task_add(net_tq(0), &ipsendraw_task);
}

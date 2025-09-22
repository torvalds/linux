/*	$OpenBSD: icmp6.c,v 1.279 2025/09/16 09:19:43 florian Exp $	*/
/*	$KAME: icmp6.c,v 1.217 2001/06/20 15:03:29 jinmei Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

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
 *	@(#)ip_icmp.c	8.2 (Berkeley) 1/4/94
 */

#include "carp.h"
#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/sysctl.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/if_dl.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>
#include <netinet6/mld6_var.h>
#include <netinet/in_pcb.h>
#include <netinet6/nd6.h>
#include <netinet6/ip6protosw.h>

#if NCARP > 0
#include <netinet/ip_carp.h>
#endif

#if NPF > 0
#include <net/pfvar.h>
#endif

/*
 * Locks used to protect data:
 *	a	atomic
 */

struct cpumem *icmp6counters;

extern int icmp6errppslim;
static int icmp6errpps_count = 0;
static struct timeval icmp6errppslim_last;

/*
 * List of callbacks to notify when Path MTU changes are made.
 */
struct icmp6_mtudisc_callback {
	LIST_ENTRY(icmp6_mtudisc_callback) mc_list;
	void (*mc_func)(struct sockaddr_in6 *, u_int);
};

LIST_HEAD(, icmp6_mtudisc_callback) icmp6_mtudisc_callbacks =
    LIST_HEAD_INITIALIZER(icmp6_mtudisc_callbacks);

struct rttimer_queue icmp6_mtudisc_timeout_q;

/* XXX do these values make any sense? */
static int icmp6_mtudisc_hiwat = 1280;	/* [a] */
static int icmp6_mtudisc_lowat = 256;	/* [a] */

/*
 * keep track of # of redirect routes.
 */
struct rttimer_queue icmp6_redirect_timeout_q;

void	icmp6_errcount(int, int);
int	icmp6_ratelimit(const struct in6_addr *, const int, const int);
int	icmp6_notify_error(struct mbuf *, int, int, int);
void	icmp6_mtudisc_timeout(struct rtentry *, u_int);

void
icmp6_init(void)
{
	mld6_init();
	rt_timer_queue_init(&icmp6_mtudisc_timeout_q, ip6_mtudisc_timeout,
	    &icmp6_mtudisc_timeout);
	rt_timer_queue_init(&icmp6_redirect_timeout_q, icmp6_redirtimeout,
	    NULL);
	icmp6counters = counters_alloc(icp6s_ncounters);
}

void
icmp6_errcount(int type, int code)
{
	enum icmp6stat_counters c = icp6s_ounknown;

	switch (type) {
	case ICMP6_DST_UNREACH:
		switch (code) {
		case ICMP6_DST_UNREACH_NOROUTE:
			c = icp6s_odst_unreach_noroute;
			break;
		case ICMP6_DST_UNREACH_ADMIN:
			c = icp6s_odst_unreach_admin;
			break;
		case ICMP6_DST_UNREACH_BEYONDSCOPE:
			c = icp6s_odst_unreach_beyondscope;
			break;
		case ICMP6_DST_UNREACH_ADDR:
			c = icp6s_odst_unreach_addr;
			break;
		case ICMP6_DST_UNREACH_NOPORT:
			c = icp6s_odst_unreach_noport;
			break;
		}
		break;
	case ICMP6_PACKET_TOO_BIG:
		c = icp6s_opacket_too_big;
		break;
	case ICMP6_TIME_EXCEEDED:
		switch (code) {
		case ICMP6_TIME_EXCEED_TRANSIT:
			c = icp6s_otime_exceed_transit;
			break;
		case ICMP6_TIME_EXCEED_REASSEMBLY:
			c = icp6s_otime_exceed_reassembly;
			break;
		}
		break;
	case ICMP6_PARAM_PROB:
		switch (code) {
		case ICMP6_PARAMPROB_HEADER:
			c = icp6s_oparamprob_header;
			break;
		case ICMP6_PARAMPROB_NEXTHEADER:
			c = icp6s_oparamprob_nextheader;
			break;
		case ICMP6_PARAMPROB_OPTION:
			c = icp6s_oparamprob_option;
			break;
		}
		break;
	case ND_REDIRECT:
		c = icp6s_oredirect;
		break;
	}

	icmp6stat_inc(c);
}

/*
 * Register a Path MTU Discovery callback.
 */
void
icmp6_mtudisc_callback_register(void (*func)(struct sockaddr_in6 *, u_int))
{
	struct icmp6_mtudisc_callback *mc;

	LIST_FOREACH(mc, &icmp6_mtudisc_callbacks, mc_list) {
		if (mc->mc_func == func)
			return;
	}

	mc = malloc(sizeof(*mc), M_PCB, M_NOWAIT);
	if (mc == NULL)
		panic("%s", __func__);

	mc->mc_func = func;
	LIST_INSERT_HEAD(&icmp6_mtudisc_callbacks, mc, mc_list);
}

struct mbuf *
icmp6_do_error(struct mbuf *m, int type, int code, int param)
{
	struct ip6_hdr *oip6, *nip6;
	struct icmp6_hdr *icmp6;
	u_int preplen;
	int off;
	int nxt;

	icmp6stat_inc(icp6s_error);

	/* count per-type-code statistics */
	icmp6_errcount(type, code);

	if (m->m_len < sizeof(struct ip6_hdr)) {
		m = m_pullup(m, sizeof(struct ip6_hdr));
		if (m == NULL)
			return (NULL);
	}
	oip6 = mtod(m, struct ip6_hdr *);

	/*
	 * If the destination address of the erroneous packet is a multicast
	 * address, or the packet was sent using link-layer multicast,
	 * we should basically suppress sending an error (RFC 2463, Section
	 * 2.4).
	 * We have two exceptions (the item e.2 in that section):
	 * - the Packet Too Big message can be sent for path MTU discovery.
	 * - the Parameter Problem Message that can be allowed an icmp6 error
	 *   in the option type field.  This check has been done in
	 *   ip6_unknown_opt(), so we can just check the type and code.
	 */
	if ((m->m_flags & (M_BCAST|M_MCAST) ||
	     IN6_IS_ADDR_MULTICAST(&oip6->ip6_dst)) &&
	    (type != ICMP6_PACKET_TOO_BIG &&
	     (type != ICMP6_PARAM_PROB ||
	      code != ICMP6_PARAMPROB_OPTION)))
		goto freeit;

	/*
	 * RFC 2463, 2.4 (e.5): source address check.
	 * XXX: the case of anycast source?
	 */
	if (IN6_IS_ADDR_UNSPECIFIED(&oip6->ip6_src) ||
	    IN6_IS_ADDR_MULTICAST(&oip6->ip6_src))
		goto freeit;

	/*
	 * If we are about to send ICMPv6 against ICMPv6 error/redirect,
	 * don't do it.
	 */
	nxt = -1;
	off = ip6_lasthdr(m, 0, IPPROTO_IPV6, &nxt);
	if (off >= 0 && nxt == IPPROTO_ICMPV6) {
		struct icmp6_hdr *icp;

		icp = ip6_exthdr_get(&m, off, sizeof(*icp));
		if (icp == NULL) {
			icmp6stat_inc(icp6s_tooshort);
			return (NULL);
		}
		if (icp->icmp6_type < ICMP6_ECHO_REQUEST ||
		    icp->icmp6_type == ND_REDIRECT) {
			/*
			 * ICMPv6 error
			 * Special case: for redirect (which is
			 * informational) we must not send icmp6 error.
			 */
			icmp6stat_inc(icp6s_canterror);
			goto freeit;
		} else {
			/* ICMPv6 informational - send the error */
		}
	}
	else {
		/* non-ICMPv6 - send the error */
	}

	oip6 = mtod(m, struct ip6_hdr *); /* adjust pointer */

	/* Finally, do rate limitation check. */
	if (icmp6_ratelimit(&oip6->ip6_src, type, code)) {
		icmp6stat_inc(icp6s_toofreq);
		goto freeit;
	}

	/*
	 * OK, ICMP6 can be generated.
	 */

	if (m->m_pkthdr.len >= ICMPV6_PLD_MAXLEN)
		m_adj(m, ICMPV6_PLD_MAXLEN - m->m_pkthdr.len);

	preplen = sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr);
	M_PREPEND(m, preplen, M_DONTWAIT);
	if (m && m->m_len < preplen)
		m = m_pullup(m, preplen);
	if (m == NULL)
		return (NULL);

	nip6 = mtod(m, struct ip6_hdr *);
	nip6->ip6_src  = oip6->ip6_src;
	nip6->ip6_dst  = oip6->ip6_dst;

	if (IN6_IS_SCOPE_EMBED(&oip6->ip6_src))
		oip6->ip6_src.s6_addr16[1] = 0;
	if (IN6_IS_SCOPE_EMBED(&oip6->ip6_dst))
		oip6->ip6_dst.s6_addr16[1] = 0;

	icmp6 = (struct icmp6_hdr *)(nip6 + 1);
	icmp6->icmp6_type = type;
	icmp6->icmp6_code = code;
	icmp6->icmp6_pptr = htonl((u_int32_t)param);

	/*
	 * icmp6_reflect() is designed to be in the input path.
	 * icmp6_error() can be called from both input and output path,
	 * and if we are in output path rcvif could contain bogus value.
	 * clear m->m_pkthdr.ph_ifidx for safety, we should have enough
	 * scope information in ip header (nip6).
	 */
	m->m_pkthdr.ph_ifidx = 0;

	icmp6stat_inc(icp6s_outhist + type);

	return (m);

  freeit:
	/*
	 * If we can't tell whether or not we can generate ICMP6, free it.
	 */
	return (m_freem(m));
}

/*
 * Generate an error packet of type error in response to bad IP6 packet.
 */
void
icmp6_error(struct mbuf *m, int type, int code, int param)
{
	struct mbuf	*n;

	n = icmp6_do_error(m, type, code, param);
	if (n != NULL) {
		/* header order: IPv6 - ICMPv6 */
		if (!icmp6_reflect(&n, sizeof(struct ip6_hdr), NULL))
			ip6_send(n);
	}
}

/*
 * Process a received ICMP6 message.
 */
int
icmp6_input(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
#if NCARP > 0
	struct ifnet *ifp;
#endif
	struct mbuf *m = *mp, *n;
	struct ip6_hdr *ip6, *nip6;
	struct icmp6_hdr *icmp6, *nicmp6;
	int off = *offp;
	int icmp6len = m->m_pkthdr.len - off;
	int code, sum, noff;

	/*
	 * Locate icmp6 structure in mbuf, and check
	 * that not corrupted and of at least minimum length
	 */

	ip6 = mtod(m, struct ip6_hdr *);
	if (icmp6len < sizeof(struct icmp6_hdr)) {
		icmp6stat_inc(icp6s_tooshort);
		goto freeit;
	}

	/*
	 * calculate the checksum
	 */
	icmp6 = ip6_exthdr_get(mp, off, sizeof(*icmp6));
	if (icmp6 == NULL) {
		icmp6stat_inc(icp6s_tooshort);
		return IPPROTO_DONE;
	}
	code = icmp6->icmp6_code;

	if ((sum = in6_cksum(m, IPPROTO_ICMPV6, off, icmp6len)) != 0) {
		icmp6stat_inc(icp6s_checksum);
		goto freeit;
	}

#if NPF > 0
	if (m->m_pkthdr.pf.flags & PF_TAG_DIVERTED) {
		switch (icmp6->icmp6_type) {
		/*
		 * These ICMP6 types map to other connections.  They must be
		 * delivered to pr_ctlinput() also for diverted connections.
		 */
		case ICMP6_DST_UNREACH:
		case ICMP6_PACKET_TOO_BIG:
		case ICMP6_TIME_EXCEEDED:
		case ICMP6_PARAM_PROB:
			/*
			 * Do not use the divert-to property of the TCP or UDP
			 * rule when doing the PCB lookup for the raw socket.
			 */
			m->m_pkthdr.pf.flags &=~ PF_TAG_DIVERTED;
			break;
		default:
			goto raw;
		}
	}
#endif /* NPF */

#if NCARP > 0
	ifp = if_get(m->m_pkthdr.ph_ifidx);
	if (ifp == NULL)
		goto freeit;

	if (icmp6->icmp6_type == ICMP6_ECHO_REQUEST &&
	    carp_lsdrop(ifp, m, AF_INET6, ip6->ip6_src.s6_addr32,
	    ip6->ip6_dst.s6_addr32, 1)) {
		if_put(ifp);
		goto freeit;
	}

	if_put(ifp);
#endif
	icmp6stat_inc(icp6s_inhist + icmp6->icmp6_type);

	switch (icmp6->icmp6_type) {
	case ICMP6_DST_UNREACH:
		switch (code) {
		case ICMP6_DST_UNREACH_NOROUTE:
			code = PRC_UNREACH_NET;
			break;
		case ICMP6_DST_UNREACH_ADMIN:
			code = PRC_UNREACH_PROTOCOL; /* is this a good code? */
			break;
		case ICMP6_DST_UNREACH_ADDR:
			code = PRC_HOSTDEAD;
			break;
		case ICMP6_DST_UNREACH_BEYONDSCOPE:
			/* I mean "source address was incorrect." */
			code = PRC_PARAMPROB;
			break;
		case ICMP6_DST_UNREACH_NOPORT:
			code = PRC_UNREACH_PORT;
			break;
		default:
			goto badcode;
		}
		goto deliver;

	case ICMP6_PACKET_TOO_BIG:
		/* MTU is checked in icmp6_mtudisc_update. */
		code = PRC_MSGSIZE;

		/*
		 * Updating the path MTU will be done after examining
		 * intermediate extension headers.
		 */
		goto deliver;

	case ICMP6_TIME_EXCEEDED:
		switch (code) {
		case ICMP6_TIME_EXCEED_TRANSIT:
			code = PRC_TIMXCEED_INTRANS;
			break;
		case ICMP6_TIME_EXCEED_REASSEMBLY:
			code = PRC_TIMXCEED_REASS;
			break;
		default:
			goto badcode;
		}
		goto deliver;

	case ICMP6_PARAM_PROB:
		switch (code) {
		case ICMP6_PARAMPROB_NEXTHEADER:
			code = PRC_UNREACH_PROTOCOL;
			break;
		case ICMP6_PARAMPROB_HEADER:
		case ICMP6_PARAMPROB_OPTION:
			code = PRC_PARAMPROB;
			break;
		default:
			goto badcode;
		}
		goto deliver;

	case ICMP6_ECHO_REQUEST:
		if (code != 0)
			goto badcode;
		/*
		 * Copy mbuf to send to two data paths: userland socket(s),
		 * and to the querier (echo reply).
		 * m: a copy for socket, n: a copy for querier
		 */
		if ((n = m_copym(m, 0, M_COPYALL, M_DONTWAIT)) == NULL) {
			/* Give up local */
			n = m;
			m = *mp = NULL;
			goto deliverecho;
		}
		/*
		 * If the first mbuf is shared, or the first mbuf is too short,
		 * copy the first part of the data into a fresh mbuf.
		 * Otherwise, we will wrongly overwrite both copies.
		 */
		if ((n->m_flags & M_EXT) != 0 ||
		    n->m_len < off + sizeof(struct icmp6_hdr)) {
			struct mbuf *n0 = n;
			const int maxlen = sizeof(*nip6) + sizeof(*nicmp6);

			/*
			 * Prepare an internal mbuf.  m_pullup() doesn't
			 * always copy the length we specified.
			 */
			if (maxlen >= MCLBYTES) {
				/* Give up remote */
				m_freem(n0);
				break;
			}
			MGETHDR(n, M_DONTWAIT, n0->m_type);
			if (n && maxlen >= MHLEN) {
				MCLGET(n, M_DONTWAIT);
				if ((n->m_flags & M_EXT) == 0) {
					m_free(n);
					n = NULL;
				}
			}
			if (n == NULL) {
				/* Give up local */
				m_freem(n0);
				n = m;
				m = *mp = NULL;
				goto deliverecho;
			}
			M_MOVE_PKTHDR(n, n0);
			/*
			 * Copy IPv6 and ICMPv6 only.
			 */
			nip6 = mtod(n, struct ip6_hdr *);
			bcopy(ip6, nip6, sizeof(struct ip6_hdr));
			nicmp6 = (struct icmp6_hdr *)(nip6 + 1);
			bcopy(icmp6, nicmp6, sizeof(struct icmp6_hdr));
			noff = sizeof(struct ip6_hdr);
			n->m_len = noff + sizeof(struct icmp6_hdr);
			/*
			 * Adjust mbuf.  ip6_plen will be adjusted in
			 * ip6_output().
			 * n->m_pkthdr.len == n0->m_pkthdr.len at this point.
			 */
			n->m_pkthdr.len += noff + sizeof(struct icmp6_hdr);
			n->m_pkthdr.len -= (off + sizeof(struct icmp6_hdr));
			m_adj(n0, off + sizeof(struct icmp6_hdr));
			n->m_next = n0;
		} else {
	 deliverecho:
			nicmp6 = ip6_exthdr_get(&n, off, sizeof(*nicmp6));
			noff = off;
		}
		if (n) {
			nicmp6->icmp6_type = ICMP6_ECHO_REPLY;
			nicmp6->icmp6_code = 0;
			icmp6stat_inc(icp6s_reflect);
			icmp6stat_inc(icp6s_outhist + ICMP6_ECHO_REPLY);
			if (!icmp6_reflect(&n, noff, NULL))
				ip6_send(n);
		}
		if (!m)
			goto freeit;
		break;

	case ICMP6_ECHO_REPLY:
		if (code != 0)
			goto badcode;
		break;

	case MLD_LISTENER_QUERY:
	case MLD_LISTENER_REPORT:
		if (icmp6len < sizeof(struct mld_hdr))
			goto badlen;
		if ((n = m_copym(m, 0, M_COPYALL, M_DONTWAIT)) == NULL) {
			/* give up local */
			mld6_input(m, off);
			m = NULL;
			goto freeit;
		}
		mld6_input(n, off);
		/* m stays. */
		break;

	case MLD_LISTENER_DONE:
		if (icmp6len < sizeof(struct mld_hdr))	/* necessary? */
			goto badlen;
		break;		/* nothing to be done in kernel */

	case MLD_MTRACE_RESP:
	case MLD_MTRACE:
		/* XXX: these two are experimental.  not officially defined. */
		/* XXX: per-interface statistics? */
		break;		/* just pass it to applications */

	case ICMP6_WRUREQUEST:	/* ICMP6_FQDN_QUERY */
		/* IPv6 Node Information Queries are not supported */
		break;
	case ICMP6_WRUREPLY:
		break;

	case ND_ROUTER_SOLICIT:
	case ND_ROUTER_ADVERT:
		if (code != 0)
			goto badcode;
		if ((icmp6->icmp6_type == ND_ROUTER_SOLICIT && icmp6len <
		    sizeof(struct nd_router_solicit)) ||
		    (icmp6->icmp6_type == ND_ROUTER_ADVERT && icmp6len <
		    sizeof(struct nd_router_advert)))
			goto badlen;

		if ((n = m_copym(m, 0, M_COPYALL, M_DONTWAIT)) == NULL) {
			/* give up local */
			nd6_rtr_cache(m, off, icmp6len,
			    icmp6->icmp6_type);
			m = NULL;
			goto freeit;
		}
		nd6_rtr_cache(n, off, icmp6len, icmp6->icmp6_type);
		/* m stays. */
		break;

	case ND_NEIGHBOR_SOLICIT:
		if (code != 0)
			goto badcode;
		if (icmp6len < sizeof(struct nd_neighbor_solicit))
			goto badlen;
		if ((n = m_copym(m, 0, M_COPYALL, M_DONTWAIT)) == NULL) {
			/* give up local */
			nd6_ns_input(m, off, icmp6len);
			m = NULL;
			goto freeit;
		}
		nd6_ns_input(n, off, icmp6len);
		/* m stays. */
		break;

	case ND_NEIGHBOR_ADVERT:
		if (code != 0)
			goto badcode;
		if (icmp6len < sizeof(struct nd_neighbor_advert))
			goto badlen;
		if ((n = m_copym(m, 0, M_COPYALL, M_DONTWAIT)) == NULL) {
			/* give up local */
			nd6_na_input(m, off, icmp6len);
			m = NULL;
			goto freeit;
		}
		nd6_na_input(n, off, icmp6len);
		/* m stays. */
		break;

	case ND_REDIRECT:
		if (code != 0)
			goto badcode;
		if (icmp6len < sizeof(struct nd_redirect))
			goto badlen;
		if ((n = m_copym(m, 0, M_COPYALL, M_DONTWAIT)) == NULL) {
			/* give up local */
			icmp6_redirect_input(m, off);
			m = NULL;
			goto freeit;
		}
		icmp6_redirect_input(n, off);
		/* m stays. */
		break;

	case ICMP6_ROUTER_RENUMBERING:
		if (code != ICMP6_ROUTER_RENUMBERING_COMMAND &&
		    code != ICMP6_ROUTER_RENUMBERING_RESULT)
			goto badcode;
		if (icmp6len < sizeof(struct icmp6_router_renum))
			goto badlen;
		break;

	default:
		if (icmp6->icmp6_type < ICMP6_ECHO_REQUEST) {
			/* ICMPv6 error: MUST deliver it by spec... */
			code = PRC_NCMDS;
			/* deliver */
		} else {
			/* ICMPv6 informational: MUST not deliver */
			break;
		}
deliver:
		if (icmp6_notify_error(m, off, icmp6len, code)) {
			/* In this case, m should've been freed. */
			return (IPPROTO_DONE);
		}
		break;

badcode:
		icmp6stat_inc(icp6s_badcode);
		break;

badlen:
		icmp6stat_inc(icp6s_badlen);
		break;
	}

#if NPF > 0
raw:
#endif
	/* deliver the packet to appropriate sockets */
	return rip6_input(mp, offp, proto, af, ns);

 freeit:
	m_freem(m);
	return IPPROTO_DONE;
}

int
icmp6_notify_error(struct mbuf *m, int off, int icmp6len, int code)
{
	struct icmp6_hdr *icmp6;
	struct ip6_hdr *eip6;
	u_int32_t notifymtu;
	struct sockaddr_in6 icmp6src, icmp6dst;

	if (icmp6len < sizeof(struct icmp6_hdr) + sizeof(struct ip6_hdr)) {
		icmp6stat_inc(icp6s_tooshort);
		goto freeit;
	}
	icmp6 = ip6_exthdr_get(&m, off,
	    sizeof(*icmp6) + sizeof(struct ip6_hdr));
	if (icmp6 == NULL) {
		icmp6stat_inc(icp6s_tooshort);
		return (-1);
	}
	eip6 = (struct ip6_hdr *)(icmp6 + 1);

	/* Detect the upper level protocol */
	{
		void (*ctlfunc)(int, struct sockaddr *, u_int, void *);
		u_int8_t nxt = eip6->ip6_nxt;
		int eoff = off + sizeof(struct icmp6_hdr) +
			sizeof(struct ip6_hdr);
		struct ip6ctlparam ip6cp;
		struct in6_addr *finaldst = NULL;
		int icmp6type = icmp6->icmp6_type;
		struct ip6_frag *fh;
		struct ip6_rthdr *rth;
		struct ip6_rthdr0 *rth0;
		int rthlen;

		while (1) { /* XXX: should avoid infinite loop explicitly? */
			struct ip6_ext *eh;

			switch (nxt) {
			case IPPROTO_HOPOPTS:
			case IPPROTO_DSTOPTS:
			case IPPROTO_AH:
				eh = ip6_exthdr_get(&m, eoff, sizeof(*eh));
				if (eh == NULL) {
					icmp6stat_inc(icp6s_tooshort);
					return (-1);
				}

				if (nxt == IPPROTO_AH)
					eoff += (eh->ip6e_len + 2) << 2;
				else
					eoff += (eh->ip6e_len + 1) << 3;
				nxt = eh->ip6e_nxt;
				break;
			case IPPROTO_ROUTING:
				/*
				 * When the erroneous packet contains a
				 * routing header, we should examine the
				 * header to determine the final destination.
				 * Otherwise, we can't properly update
				 * information that depends on the final
				 * destination (e.g. path MTU).
				 */
				rth  = ip6_exthdr_get(&m, eoff, sizeof(*rth));
				if (rth == NULL) {
					icmp6stat_inc(icp6s_tooshort);
					return (-1);
				}
				rthlen = (rth->ip6r_len + 1) << 3;
				/*
				 * XXX: currently there is no
				 * officially defined type other
				 * than type-0.
				 * Note that if the segment left field
				 * is 0, all intermediate hops must
				 * have been passed.
				 */
				if (rth->ip6r_segleft &&
				    rth->ip6r_type == IPV6_RTHDR_TYPE_0) {
					int hops;

					rth0 = ip6_exthdr_get(&m, eoff, rthlen);
					if (rth0 == NULL) {
						icmp6stat_inc(icp6s_tooshort);
						return (-1);
					}
					/* just ignore a bogus header */
					if ((rth0->ip6r0_len % 2) == 0 &&
					    (hops = rth0->ip6r0_len/2)) {
						finaldst = (struct in6_addr *)
						    (rth0 + 1) + (hops - 1);
					}
				}
				eoff += rthlen;
				nxt = rth->ip6r_nxt;
				break;
			case IPPROTO_FRAGMENT:
				fh = ip6_exthdr_get(&m, eoff, sizeof(*fh));
				if (fh == NULL) {
					icmp6stat_inc(icp6s_tooshort);
					return (-1);
				}
				/*
				 * Data after a fragment header is meaningless
				 * unless it is the first fragment, but
				 * we'll go to the notify label for path MTU
				 * discovery.
				 */
				if (fh->ip6f_offlg & IP6F_OFF_MASK)
					goto notify;

				eoff += sizeof(struct ip6_frag);
				nxt = fh->ip6f_nxt;
				break;
			default:
				/*
				 * This case includes ESP and the No Next
				 * Header.  In such cases going to the notify
				 * label does not have any meaning
				 * (i.e. ctlfunc will be NULL), but we go
				 * anyway since we might have to update
				 * path MTU information.
				 */
				goto notify;
			}
		}
	  notify:
		icmp6 = ip6_exthdr_get(&m, off,
		    sizeof(*icmp6) + sizeof(struct ip6_hdr));
		if (icmp6 == NULL) {
			icmp6stat_inc(icp6s_tooshort);
			return (-1);
		}

		eip6 = (struct ip6_hdr *)(icmp6 + 1);
		bzero(&icmp6dst, sizeof(icmp6dst));
		icmp6dst.sin6_len = sizeof(struct sockaddr_in6);
		icmp6dst.sin6_family = AF_INET6;
		if (finaldst == NULL)
			icmp6dst.sin6_addr = eip6->ip6_dst;
		else
			icmp6dst.sin6_addr = *finaldst;
		icmp6dst.sin6_scope_id = in6_addr2scopeid(m->m_pkthdr.ph_ifidx,
		    &icmp6dst.sin6_addr);
		if (in6_embedscope(&icmp6dst.sin6_addr, &icmp6dst,
		    NULL, NULL)) {
			/* interface went away */
			goto freeit;
		}

		/*
		 * retrieve parameters from the inner IPv6 header, and convert
		 * them into sockaddr structures.
		 */
		bzero(&icmp6src, sizeof(icmp6src));
		icmp6src.sin6_len = sizeof(struct sockaddr_in6);
		icmp6src.sin6_family = AF_INET6;
		icmp6src.sin6_addr = eip6->ip6_src;
		icmp6src.sin6_scope_id = in6_addr2scopeid(m->m_pkthdr.ph_ifidx,
		    &icmp6src.sin6_addr);
		if (in6_embedscope(&icmp6src.sin6_addr, &icmp6src,
		    NULL, NULL)) {
			/* interface went away */
			goto freeit;
		}
		icmp6src.sin6_flowinfo =
		    (eip6->ip6_flow & IPV6_FLOWLABEL_MASK);

		if (finaldst == NULL)
			finaldst = &eip6->ip6_dst;
		ip6cp.ip6c_m = m;
		ip6cp.ip6c_icmp6 = icmp6;
		ip6cp.ip6c_ip6 = (struct ip6_hdr *)(icmp6 + 1);
		ip6cp.ip6c_off = eoff;
		ip6cp.ip6c_finaldst = finaldst;
		ip6cp.ip6c_src = &icmp6src;
		ip6cp.ip6c_nxt = nxt;
#if NPF > 0
		pf_pkt_addr_changed(m);
#endif

		if (icmp6type == ICMP6_PACKET_TOO_BIG) {
			notifymtu = ntohl(icmp6->icmp6_mtu);
			ip6cp.ip6c_cmdarg = (void *)&notifymtu;
		}

		ctlfunc = inet6sw[ip6_protox[nxt]].pr_ctlinput;
		if (ctlfunc)
			(*ctlfunc)(code, sin6tosa(&icmp6dst),
			    m->m_pkthdr.ph_rtableid, &ip6cp);
	}
	return (0);

  freeit:
	m_freem(m);
	return (-1);
}

void
icmp6_mtudisc_update(struct ip6ctlparam *ip6cp, int validated)
{
	unsigned long rtcount;
	struct icmp6_mtudisc_callback *mc;
	struct in6_addr *dst = ip6cp->ip6c_finaldst;
	struct icmp6_hdr *icmp6 = ip6cp->ip6c_icmp6;
	struct mbuf *m = ip6cp->ip6c_m;	/* will be necessary for scope issue */
	u_int mtu = ntohl(icmp6->icmp6_mtu);
	struct rtentry *rt = NULL;
	struct sockaddr_in6 sin6;

	if (mtu < IPV6_MMTU)
		return;

	/*
	 * allow non-validated cases if memory is plenty, to make traffic
	 * from non-connected pcb happy.
	 */
	rtcount = rt_timer_queue_count(&icmp6_mtudisc_timeout_q);
	if (validated) {
		if (rtcount > atomic_load_int(&icmp6_mtudisc_hiwat))
			return;
		else if (rtcount > atomic_load_int(&icmp6_mtudisc_lowat)) {
			/*
			 * XXX nuke a victim, install the new one.
			 */
		}
	} else {
		if (rtcount > atomic_load_int(&icmp6_mtudisc_lowat))
			return;
	}

	bzero(&sin6, sizeof(sin6));
	sin6.sin6_family = PF_INET6;
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	sin6.sin6_addr = *dst;
	/* XXX normally, this won't happen */
	if (IN6_IS_ADDR_LINKLOCAL(dst)) {
		sin6.sin6_addr.s6_addr16[1] = htons(m->m_pkthdr.ph_ifidx);
	}
	sin6.sin6_scope_id = in6_addr2scopeid(m->m_pkthdr.ph_ifidx,
	    &sin6.sin6_addr);

	rt = icmp6_mtudisc_clone(&sin6, m->m_pkthdr.ph_rtableid, 0);

	if (rt != NULL && ISSET(rt->rt_flags, RTF_HOST) &&
	    !(rt->rt_locks & RTV_MTU)) {
		u_int rtmtu;

		rtmtu = atomic_load_int(&rt->rt_mtu);
		if (rtmtu > mtu || rtmtu == 0) {
			struct ifnet *ifp;

			ifp = if_get(rt->rt_ifidx);
			if (ifp != NULL && mtu < ifp->if_mtu) {
				icmp6stat_inc(icp6s_pmtuchg);
				atomic_cas_uint(&rt->rt_mtu, rtmtu, mtu);
			}
			if_put(ifp);
		}
	}
	rtfree(rt);

	/*
	 * Notify protocols that the MTU for this destination
	 * has changed.
	 */
	LIST_FOREACH(mc, &icmp6_mtudisc_callbacks, mc_list)
		(*mc->mc_func)(&sin6, m->m_pkthdr.ph_rtableid);
}

/*
 * Reflect the ip6 packet back to the source.
 * OFF points to the icmp6 header, counted from the top of the mbuf.
 */
int
icmp6_reflect(struct mbuf **mp, size_t off, struct sockaddr *sa)
{
	struct mbuf *m = *mp;
	struct rtentry *rt = NULL;
	struct ip6_hdr *ip6;
	struct icmp6_hdr *icmp6;
	struct in6_addr t, *src = NULL;
	struct sockaddr_in6 sa6_src, sa6_dst;
	u_int rtableid;
	u_int8_t pfflags;

	CTASSERT(sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr) <= MHLEN);

	/* too short to reflect */
	if (off < sizeof(struct ip6_hdr))
		goto bad;

	if (m->m_pkthdr.ph_loopcnt++ >= M_MAXLOOP) {
		m_freemp(mp);
		return (ELOOP);
	}
	rtableid = m->m_pkthdr.ph_rtableid;
	pfflags = m->m_pkthdr.pf.flags;
	m_resethdr(m);
	m->m_pkthdr.ph_rtableid = rtableid;
	m->m_pkthdr.pf.flags = pfflags & PF_TAG_GENERATED;

	/*
	 * If there are extra headers between IPv6 and ICMPv6, strip
	 * off that header first.
	 */
	if (off > sizeof(struct ip6_hdr)) {
		size_t l;
		struct ip6_hdr nip6;

		l = off - sizeof(struct ip6_hdr);
		m_copydata(m, 0, sizeof(nip6), (caddr_t)&nip6);
		m_adj(m, l);
		l = sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr);
		if (m->m_len < l) {
			if ((m = *mp = m_pullup(m, l)) == NULL)
				return (EMSGSIZE);
		}
		memcpy(mtod(m, caddr_t), &nip6, sizeof(nip6));
	} else /* off == sizeof(struct ip6_hdr) */ {
		size_t l;
		l = sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr);
		if (m->m_len < l) {
			if ((m = *mp = m_pullup(m, l)) == NULL)
				return (EMSGSIZE);
		}
	}
	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_nxt = IPPROTO_ICMPV6;
	icmp6 = (struct icmp6_hdr *)(ip6 + 1);

	t = ip6->ip6_dst;
	/*
	 * ip6_input() drops a packet if its src is multicast.
	 * So, the src is never multicast.
	 */
	ip6->ip6_dst = ip6->ip6_src;

	/*
	 * XXX: make sure to embed scope zone information, using
	 * already embedded IDs or the received interface (if any).
	 * Note that rcvif may be NULL.
	 * TODO: scoped routing case (XXX).
	 */
	bzero(&sa6_src, sizeof(sa6_src));
	sa6_src.sin6_family = AF_INET6;
	sa6_src.sin6_len = sizeof(sa6_src);
	sa6_src.sin6_addr = ip6->ip6_dst;
	bzero(&sa6_dst, sizeof(sa6_dst));
	sa6_dst.sin6_family = AF_INET6;
	sa6_dst.sin6_len = sizeof(sa6_dst);
	sa6_dst.sin6_addr = t;

	if (sa == NULL) {
		/*
		 * If the incoming packet was addressed directly to us (i.e.
		 * unicast), use dst as the src for the reply. The
		 * IN6_IFF_TENTATIVE|IN6_IFF_DUPLICATED case would be VERY rare,
		 * but is possible (for example) when we encounter an error
		 * while forwarding procedure destined to a duplicated address
		 * of ours.
		 */
		rt = rtalloc(sin6tosa(&sa6_dst), 0, rtableid);
		if (rtisvalid(rt) && ISSET(rt->rt_flags, RTF_LOCAL) &&
		    !ISSET(ifatoia6(rt->rt_ifa)->ia6_flags,
		    IN6_IFF_ANYCAST|IN6_IFF_TENTATIVE|IN6_IFF_DUPLICATED)) {
			src = &t;
		}
		rtfree(rt);
		rt = NULL;
		sa = sin6tosa(&sa6_src);
	}

	if (src == NULL) {
		struct in6_ifaddr *ia6;

		/*
		 * This case matches to multicasts, our anycast, or unicasts
		 * that we do not own.  Select a source address based on the
		 * source address of the erroneous packet.
		 */
		rt = rtalloc(sa, RT_RESOLVE, rtableid);
		if (!rtisvalid(rt)) {
			rtfree(rt);
			goto bad;
		}
		ia6 = in6_ifawithscope(rt->rt_ifa->ifa_ifp, &t, rtableid, rt);
		if (ia6 != NULL)
			src = &ia6->ia_addr.sin6_addr;
		if (src == NULL)
			src = &ifatoia6(rt->rt_ifa)->ia_addr.sin6_addr;

		/* route sourceaddr may override src address selection */
		if (ISSET(rt->rt_flags, RTF_GATEWAY)) {
			struct sockaddr *sourceaddr;

			sourceaddr = rtable_getsource(rtableid, AF_INET6);
			if (sourceaddr != NULL) {
				struct ifaddr *ifa;
				ifa = ifa_ifwithaddr(sourceaddr, rtableid);
				if (ifa != NULL &&
				    ISSET(ifa->ifa_ifp->if_flags, IFF_UP))
					src = &satosin6(sourceaddr)->sin6_addr;
			}
		}
	}

	ip6->ip6_src = *src;
	rtfree(rt);

	ip6->ip6_flow = 0;
	ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc |= IPV6_VERSION;
	ip6->ip6_nxt = IPPROTO_ICMPV6;
	ip6->ip6_hlim = atomic_load_int(&ip6_defhlim);

	icmp6->icmp6_cksum = 0;
	m->m_pkthdr.csum_flags = M_ICMP_CSUM_OUT;

	/*
	 * XXX option handling
	 */

	m->m_flags &= ~(M_BCAST|M_MCAST);
	return (0);

 bad:
	m_freemp(mp);
	return (EHOSTUNREACH);
}

void
icmp6_fasttimo(void)
{
	mld6_fasttimeo();
}

void
icmp6_redirect_input(struct mbuf *m, int off)
{
	struct ifnet *ifp;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct nd_redirect *nd_rd;
	int icmp6len = ntohs(ip6->ip6_plen);
	char *lladdr = NULL;
	int lladdrlen = 0;
	struct rtentry *rt = NULL;
	int i_am_router = (atomic_load_int(&ip6_forwarding) != 0);
	int is_router;
	int is_onlink;
	struct in6_addr src6 = ip6->ip6_src;
	struct in6_addr redtgt6;
	struct in6_addr reddst6;
	struct nd_opts ndopts;

	ifp = if_get(m->m_pkthdr.ph_ifidx);
	if (ifp == NULL)
		return;

	/* if we are router, we don't update route by icmp6 redirect */
	if (i_am_router)
		goto freeit;
	if (!(ifp->if_xflags & IFXF_AUTOCONF6))
		goto freeit;

	nd_rd = ip6_exthdr_get(&m, off, icmp6len);
	if (nd_rd == NULL) {
		icmp6stat_inc(icp6s_tooshort);
		if_put(ifp);
		return;
	}
	redtgt6 = nd_rd->nd_rd_target;
	reddst6 = nd_rd->nd_rd_dst;

	if (IN6_IS_ADDR_LINKLOCAL(&redtgt6))
		redtgt6.s6_addr16[1] = htons(ifp->if_index);
	if (IN6_IS_ADDR_LINKLOCAL(&reddst6))
		reddst6.s6_addr16[1] = htons(ifp->if_index);

	/* validation */
	if (!IN6_IS_ADDR_LINKLOCAL(&src6))
		goto bad;
	if (ip6->ip6_hlim != 255)
		goto bad;
	if (IN6_IS_ADDR_MULTICAST(&reddst6))
		goto bad;

    {
	/* ip6->ip6_src must be equal to gw for icmp6->icmp6_reddst */
	struct sockaddr_in6 sin6;
	struct in6_addr *gw6;

	bzero(&sin6, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	memcpy(&sin6.sin6_addr, &reddst6, sizeof(reddst6));
	rt = rtalloc(sin6tosa(&sin6), 0, m->m_pkthdr.ph_rtableid);
	if (!rt)
		goto bad;

	if (rt->rt_gateway == NULL || rt->rt_gateway->sa_family != AF_INET6) {
		rtfree(rt);
		goto bad;
	}

	gw6 = &(satosin6(rt->rt_gateway)->sin6_addr);
	if (bcmp(&src6, gw6, sizeof(struct in6_addr)) != 0) {
		rtfree(rt);
		goto bad;
	}
	rtfree(rt);
	rt = NULL;
    }

	is_router = is_onlink = 0;
	if (IN6_IS_ADDR_LINKLOCAL(&redtgt6))
		is_router = 1;	/* router case */
	if (bcmp(&redtgt6, &reddst6, sizeof(redtgt6)) == 0)
		is_onlink = 1;	/* on-link destination case */
	if (!is_router && !is_onlink)
		goto bad;

	/* validation passed */

	icmp6len -= sizeof(*nd_rd);
	if (nd6_options(nd_rd + 1, icmp6len, &ndopts) < 0)
		/* nd6_options have incremented stats */
		goto freeit;

	if (ndopts.nd_opts_tgt_lladdr) {
		lladdr = (char *)(ndopts.nd_opts_tgt_lladdr + 1);
		lladdrlen = ndopts.nd_opts_tgt_lladdr->nd_opt_len << 3;
	}

	if (lladdr && ((ifp->if_addrlen + 2 + 7) & ~7) != lladdrlen)
		goto bad;

	/* RFC 2461 8.3 */
	nd6_cache_lladdr(ifp, &redtgt6, lladdr, lladdrlen, ND_REDIRECT,
	    is_onlink ? ND_REDIRECT_ONLINK : ND_REDIRECT_ROUTER, i_am_router);

	if (!is_onlink) {	/* better router case.  perform rtredirect. */
		/* perform rtredirect */
		struct sockaddr_in6 sdst;
		struct sockaddr_in6 sgw;
		struct sockaddr_in6 ssrc;
		unsigned long rtcount;
		struct rtentry *newrt = NULL;

		/*
		 * do not install redirect route, if the number of entries
		 * is too much (> hiwat).  note that, the node (= host) will
		 * work just fine even if we do not install redirect route
		 * (there will be additional hops, though).
		 */
		rtcount = rt_timer_queue_count(&icmp6_redirect_timeout_q);
		if (rtcount >= atomic_load_int(&ip6_maxdynroutes))
			goto freeit;

		bzero(&sdst, sizeof(sdst));
		bzero(&sgw, sizeof(sgw));
		bzero(&ssrc, sizeof(ssrc));
		sdst.sin6_family = sgw.sin6_family = ssrc.sin6_family = AF_INET6;
		sdst.sin6_len = sgw.sin6_len = ssrc.sin6_len =
			sizeof(struct sockaddr_in6);
		memcpy(&sgw.sin6_addr, &redtgt6, sizeof(struct in6_addr));
		memcpy(&sdst.sin6_addr, &reddst6, sizeof(struct in6_addr));
		memcpy(&ssrc.sin6_addr, &src6, sizeof(struct in6_addr));
		rtredirect(sin6tosa(&sdst), sin6tosa(&sgw), sin6tosa(&ssrc),
		    &newrt, m->m_pkthdr.ph_rtableid);
		if (newrt != NULL && icmp6_redirtimeout > 0) {
			rt_timer_add(newrt, &icmp6_redirect_timeout_q,
			    m->m_pkthdr.ph_rtableid);
		}
		rtfree(newrt);
	}
	/* finally update cached route in each socket via pfctlinput */
	{
		struct sockaddr_in6 sdst;

		bzero(&sdst, sizeof(sdst));
		sdst.sin6_family = AF_INET6;
		sdst.sin6_len = sizeof(struct sockaddr_in6);
		memcpy(&sdst.sin6_addr, &reddst6, sizeof(struct in6_addr));
		pfctlinput(PRC_REDIRECT_HOST, sin6tosa(&sdst));
	}

 freeit:
	if_put(ifp);
	m_freem(m);
	return;

 bad:
	if_put(ifp);
	icmp6stat_inc(icp6s_badredirect);
	m_freem(m);
}

void
icmp6_redirect_output(struct mbuf *m0, struct rtentry *rt)
{
	struct ifnet *ifp = NULL;
	struct in6_addr *ifp_ll6;
	struct in6_addr *nexthop;
	struct ip6_hdr *sip6;	/* m0 as struct ip6_hdr */
	struct mbuf *m = NULL;	/* newly allocated one */
	struct ip6_hdr *ip6;	/* m as struct ip6_hdr */
	struct nd_redirect *nd_rd;
	size_t maxlen;
	u_char *p;
	struct sockaddr_in6 src_sa;
	int i_am_router = (atomic_load_int(&ip6_forwarding) != 0);

	icmp6_errcount(ND_REDIRECT, 0);

	/* if we are not router, we don't send icmp6 redirect */
	if (!i_am_router)
		goto fail;

	/* sanity check */
	if (m0 == NULL || !rtisvalid(rt))
		goto fail;

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		goto fail;

	/*
	 * Address check:
	 *  the source address must identify a neighbor, and
	 *  the destination address must not be a multicast address
	 *  [RFC 2461, sec 8.2]
	 */
	sip6 = mtod(m0, struct ip6_hdr *);
	bzero(&src_sa, sizeof(src_sa));
	src_sa.sin6_family = AF_INET6;
	src_sa.sin6_len = sizeof(src_sa);
	src_sa.sin6_addr = sip6->ip6_src;
	/* we don't currently use sin6_scope_id, but eventually use it */
	src_sa.sin6_scope_id = in6_addr2scopeid(ifp->if_index, &sip6->ip6_src);
	if (nd6_is_addr_neighbor(&src_sa, ifp) == 0)
		goto fail;
	if (IN6_IS_ADDR_MULTICAST(&sip6->ip6_dst))
		goto fail;	/* what should we do here? */

	/* rate limit */
	if (icmp6_ratelimit(&sip6->ip6_src, ND_REDIRECT, 0))
		goto fail;

	/*
	 * Since we are going to append up to 1280 bytes (= IPV6_MMTU),
	 * we almost always ask for an mbuf cluster for simplicity.
	 * (MHLEN < IPV6_MMTU is almost always true)
	 */
#if IPV6_MMTU >= MCLBYTES
# error assumption failed about IPV6_MMTU and MCLBYTES
#endif
	MGETHDR(m, M_DONTWAIT, MT_HEADER);
	if (m && IPV6_MMTU >= MHLEN)
		MCLGET(m, M_DONTWAIT);
	if (!m)
		goto fail;
	m->m_pkthdr.ph_ifidx = 0;
	m->m_len = 0;
	maxlen = m_trailingspace(m);
	maxlen = min(IPV6_MMTU, maxlen);
	/* just for safety */
	if (maxlen < sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr) +
	    ((sizeof(struct nd_opt_hdr) + ifp->if_addrlen + 7) & ~7)) {
		goto fail;
	}

	{
		/* get ip6 linklocal address for ifp(my outgoing interface). */
		struct in6_ifaddr *ia6;
		if ((ia6 = in6ifa_ifpforlinklocal(ifp, IN6_IFF_TENTATIVE|
		    IN6_IFF_DUPLICATED|IN6_IFF_ANYCAST)) == NULL)
			goto fail;
		ifp_ll6 = &ia6->ia_addr.sin6_addr;
	}

	/* get ip6 linklocal address for the router. */
	if (rt->rt_gateway && (rt->rt_flags & RTF_GATEWAY)) {
		struct sockaddr_in6 *sin6;
		sin6 = satosin6(rt->rt_gateway);
		nexthop = &sin6->sin6_addr;
		if (!IN6_IS_ADDR_LINKLOCAL(nexthop))
			nexthop = NULL;
	} else
		nexthop = NULL;

	/* ip6 */
	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow = 0;
	ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc |= IPV6_VERSION;
	/* ip6->ip6_plen will be set later */
	ip6->ip6_nxt = IPPROTO_ICMPV6;
	ip6->ip6_hlim = 255;
	/* ip6->ip6_src must be linklocal addr for my outgoing if. */
	bcopy(ifp_ll6, &ip6->ip6_src, sizeof(struct in6_addr));
	bcopy(&sip6->ip6_src, &ip6->ip6_dst, sizeof(struct in6_addr));

	/* ND Redirect */
	nd_rd = (struct nd_redirect *)(ip6 + 1);
	nd_rd->nd_rd_type = ND_REDIRECT;
	nd_rd->nd_rd_code = 0;
	nd_rd->nd_rd_reserved = 0;
	if (rt->rt_flags & RTF_GATEWAY) {
		/*
		 * nd_rd->nd_rd_target must be a link-local address in
		 * better router cases.
		 */
		if (!nexthop)
			goto fail;
		bcopy(nexthop, &nd_rd->nd_rd_target,
		      sizeof(nd_rd->nd_rd_target));
		bcopy(&sip6->ip6_dst, &nd_rd->nd_rd_dst,
		      sizeof(nd_rd->nd_rd_dst));
	} else {
		/* make sure redtgt == reddst */
		nexthop = &sip6->ip6_dst;
		bcopy(&sip6->ip6_dst, &nd_rd->nd_rd_target,
		      sizeof(nd_rd->nd_rd_target));
		bcopy(&sip6->ip6_dst, &nd_rd->nd_rd_dst,
		      sizeof(nd_rd->nd_rd_dst));
	}

	p = (u_char *)(nd_rd + 1);

	{
		/* target lladdr option */
		struct rtentry *nrt;
		int len;
		struct sockaddr_dl *sdl;
		struct nd_opt_hdr *nd_opt;
		char *lladdr;

		len = sizeof(*nd_opt) + ifp->if_addrlen;
		len = (len + 7) & ~7;	/* round by 8 */
		/* safety check */
		if (len + (p - (u_char *)ip6) > maxlen)
			goto nolladdropt;
		nrt = nd6_lookup(nexthop, 0, ifp, ifp->if_rdomain);
		if ((nrt != NULL) &&
		    (nrt->rt_flags & (RTF_GATEWAY|RTF_LLINFO)) == RTF_LLINFO &&
		    (nrt->rt_gateway->sa_family == AF_LINK) &&
		    (sdl = satosdl(nrt->rt_gateway)) &&
		    sdl->sdl_alen) {
			nd_opt = (struct nd_opt_hdr *)p;
			nd_opt->nd_opt_type = ND_OPT_TARGET_LINKADDR;
			nd_opt->nd_opt_len = len >> 3;
			lladdr = (char *)(nd_opt + 1);
			bcopy(LLADDR(sdl), lladdr, ifp->if_addrlen);
			p += len;
		}
		rtfree(nrt);
	}
  nolladdropt:;

	m->m_pkthdr.len = m->m_len = p - (u_char *)ip6;

	/* just to be safe */
	if (p - (u_char *)ip6 > maxlen)
		goto noredhdropt;

	{
		/* redirected header option */
		int len;
		struct nd_opt_rd_hdr *nd_opt_rh;

		/*
		 * compute the maximum size for icmp6 redirect header option.
		 * XXX room for auth header?
		 */
		len = maxlen - (p - (u_char *)ip6);
		len &= ~7;

		/*
		 * Redirected header option spec (RFC2461 4.6.3) talks nothing
		 * about padding/truncate rule for the original IP packet.
		 * From the discussion on IPv6imp in Feb 1999,
		 * the consensus was:
		 * - "attach as much as possible" is the goal
		 * - pad if not aligned (original size can be guessed by
		 *   original ip6 header)
		 * Following code adds the padding if it is simple enough,
		 * and truncates if not.
		 */
		if (len - sizeof(*nd_opt_rh) < m0->m_pkthdr.len) {
			/* not enough room, truncate */
			m_adj(m0, (len - sizeof(*nd_opt_rh)) -
			    m0->m_pkthdr.len);
		} else {
			/*
			 * enough room, truncate if not aligned.
			 * we don't pad here for simplicity.
			 */
			size_t extra;

			extra = m0->m_pkthdr.len % 8;
			if (extra) {
				/* truncate */
				m_adj(m0, -extra);
			}
			len = m0->m_pkthdr.len + sizeof(*nd_opt_rh);
		}

		nd_opt_rh = (struct nd_opt_rd_hdr *)p;
		bzero(nd_opt_rh, sizeof(*nd_opt_rh));
		nd_opt_rh->nd_opt_rh_type = ND_OPT_REDIRECTED_HEADER;
		nd_opt_rh->nd_opt_rh_len = len >> 3;
		p += sizeof(*nd_opt_rh);
		m->m_pkthdr.len = m->m_len = p - (u_char *)ip6;

		/* connect m0 to m */
		m->m_pkthdr.len += m0->m_pkthdr.len;
		m_cat(m, m0);
		m0 = NULL;
	}
noredhdropt:
	m_freem(m0);
	m0 = NULL;

	sip6 = mtod(m, struct ip6_hdr *);
	if (IN6_IS_ADDR_LINKLOCAL(&sip6->ip6_src))
		sip6->ip6_src.s6_addr16[1] = 0;
	if (IN6_IS_ADDR_LINKLOCAL(&sip6->ip6_dst))
		sip6->ip6_dst.s6_addr16[1] = 0;
#if 0
	if (IN6_IS_ADDR_LINKLOCAL(&ip6->ip6_src))
		ip6->ip6_src.s6_addr16[1] = 0;
	if (IN6_IS_ADDR_LINKLOCAL(&ip6->ip6_dst))
		ip6->ip6_dst.s6_addr16[1] = 0;
#endif
	if (IN6_IS_ADDR_LINKLOCAL(&nd_rd->nd_rd_target))
		nd_rd->nd_rd_target.s6_addr16[1] = 0;
	if (IN6_IS_ADDR_LINKLOCAL(&nd_rd->nd_rd_dst))
		nd_rd->nd_rd_dst.s6_addr16[1] = 0;

	ip6->ip6_plen = htons(m->m_pkthdr.len - sizeof(struct ip6_hdr));

	nd_rd->nd_rd_cksum = 0;
	m->m_pkthdr.csum_flags = M_ICMP_CSUM_OUT;

	/* send the packet to outside... */
	ip6_output(m, NULL, NULL, 0, NULL, NULL);

	icmp6stat_inc(icp6s_outhist + ND_REDIRECT);

	if_put(ifp);
	return;

fail:
	if_put(ifp);
	m_freem(m);
	m_freem(m0);
}

/*
 * ICMPv6 socket option processing.
 */
int
icmp6_ctloutput(int op, struct socket *so, int level, int optname,
    struct mbuf *m)
{
	int error = 0;
	struct inpcb *inp = sotoinpcb(so);

	if (level != IPPROTO_ICMPV6)
		return EINVAL;

	switch (op) {
	case PRCO_SETOPT:
		switch (optname) {
		case ICMP6_FILTER:
		    {
			struct icmp6_filter *p;

			if (m == NULL || m->m_len != sizeof(*p)) {
				error = EMSGSIZE;
				break;
			}
			p = mtod(m, struct icmp6_filter *);
			if (!p || !inp->inp_icmp6filt) {
				error = EINVAL;
				break;
			}
			bcopy(p, inp->inp_icmp6filt,
				sizeof(struct icmp6_filter));
			error = 0;
			break;
		    }

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	case PRCO_GETOPT:
		switch (optname) {
		case ICMP6_FILTER:
		    {
			struct icmp6_filter *p;

			if (!inp->inp_icmp6filt) {
				error = EINVAL;
				break;
			}
			m->m_len = sizeof(struct icmp6_filter);
			p = mtod(m, struct icmp6_filter *);
			bcopy(inp->inp_icmp6filt, p,
				sizeof(struct icmp6_filter));
			error = 0;
			break;
		    }

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;
	}

	return (error);
}

/*
 * Perform rate limit check.
 * Returns 0 if it is okay to send the icmp6 packet.
 * Returns 1 if the router SHOULD NOT send this icmp6 packet due to rate
 * limitation.
 *
 * XXX per-destination/type check necessary?
 *
 * dst - not used at this moment
 * type - not used at this moment
 * code - not used at this moment
 */
int
icmp6_ratelimit(const struct in6_addr *dst, const int type, const int code)
{
	/* PPS limit */
	if (!ppsratecheck(&icmp6errppslim_last, &icmp6errpps_count,
	    icmp6errppslim))
		return 1;	/* The packet is subject to rate limit */
	return 0;		/* okay to send */
}

struct rtentry *
icmp6_mtudisc_clone(struct sockaddr_in6 *dst, u_int rtableid, int ipsec)
{
	struct rtentry *rt;
	int    error;

	rt = rtalloc(sin6tosa(dst), RT_RESOLVE, rtableid);

	/* Check if the route is actually usable */
	if (!rtisvalid(rt))
		goto bad;
	/* IPsec needs the route only for PMTU, it can use reject for that */
	if (!ipsec && (rt->rt_flags & (RTF_REJECT|RTF_BLACKHOLE)))
		goto bad;

	/*
	 * No PMTU for local routes and permanent neighbors,
	 * ARP and NDP use the same expire timer as the route.
	 */
	if (ISSET(rt->rt_flags, RTF_LOCAL) ||
	    (ISSET(rt->rt_flags, RTF_LLINFO) && rt->rt_expire == 0))
		goto bad;

	/* If we didn't get a host route, allocate one */
	if ((rt->rt_flags & RTF_HOST) == 0) {
		struct rtentry *nrt;
		struct rt_addrinfo info;
		struct sockaddr_rtlabel sa_rl;

		memset(&info, 0, sizeof(info));
		info.rti_ifa = rt->rt_ifa;
		info.rti_flags = RTF_GATEWAY | RTF_HOST | RTF_DYNAMIC;
		info.rti_info[RTAX_DST] = sin6tosa(dst);
		info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
		info.rti_info[RTAX_LABEL] =
		    rtlabel_id2sa(rt->rt_labelid, &sa_rl);

		error = rtrequest(RTM_ADD, &info, rt->rt_priority, &nrt,
		    rtableid);
		if (error)
			goto bad;
		nrt->rt_rmx = rt->rt_rmx;
		rtfree(rt);
		rt = nrt;
		rtm_send(rt, RTM_ADD, 0, rtableid);
	}
	error = rt_timer_add(rt, &icmp6_mtudisc_timeout_q, rtableid);
	if (error)
		goto bad;

	return (rt);
bad:
	rtfree(rt);
	return (NULL);
}

void
icmp6_mtudisc_timeout(struct rtentry *rt, u_int rtableid)
{
	struct ifnet *ifp;

	NET_ASSERT_LOCKED();

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		return;

	if ((rt->rt_flags & (RTF_DYNAMIC|RTF_HOST)) == (RTF_DYNAMIC|RTF_HOST)) {
		rtdeletemsg(rt, ifp, rtableid);
	} else {
		if (!(rt->rt_locks & RTV_MTU))
			atomic_store_int(&rt->rt_mtu, 0);
	}

	if_put(ifp);
}

#ifndef SMALL_KERNEL
const struct sysctl_bounded_args icmpv6ctl_vars_unlocked[] = {
	{ ICMPV6CTL_ND6_DELAY, &nd6_delay, 0, INT_MAX },
	{ ICMPV6CTL_ND6_UMAXTRIES, &nd6_umaxtries, 0, INT_MAX },
	{ ICMPV6CTL_ND6_MMAXTRIES, &nd6_mmaxtries, 0, INT_MAX },
	{ ICMPV6CTL_MTUDISC_HIWAT, &icmp6_mtudisc_hiwat, 0, INT_MAX },
	{ ICMPV6CTL_MTUDISC_LOWAT, &icmp6_mtudisc_lowat, 0, INT_MAX },
};

const struct sysctl_bounded_args icmpv6ctl_vars[] = {
	{ ICMPV6CTL_ERRPPSLIMIT, &icmp6errppslim, -1, 1000 },
};

int
icmp6_sysctl_icmp6stat(void *oldp, size_t *oldlenp, void *newp)
{
	struct icmp6stat *icmp6stat;
	int ret;

	CTASSERT(sizeof(*icmp6stat) == icp6s_ncounters * sizeof(uint64_t));
	icmp6stat = malloc(sizeof(*icmp6stat), M_TEMP, M_WAITOK|M_ZERO);
	counters_read(icmp6counters, (uint64_t *)icmp6stat, icp6s_ncounters,
	    NULL);
	ret = sysctl_rdstruct(oldp, oldlenp, newp,
	    icmp6stat, sizeof(*icmp6stat));
	free(icmp6stat, M_TEMP, sizeof(*icmp6stat));

	return (ret);
}

/*
 * Temporary, should be replaced with sysctl_lock after icmp6_sysctl()
 * unlocking.
 */
struct rwlock icmp6_sysctl_lock = RWLOCK_INITIALIZER("icmp6rwl"); 

int
icmp6_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	int error;

	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case ICMPV6CTL_REDIRTIMEOUT: {
		int oldval, newval, error;

		oldval = newval = atomic_load_int(&icmp6_redirtimeout);
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &newval, 0, INT_MAX);
		if (error == 0 && oldval != newval) {
			rw_enter_write(&icmp6_sysctl_lock);
			atomic_store_int(&icmp6_redirtimeout, newval);
			rt_timer_queue_change(&icmp6_redirect_timeout_q,
			    newval);
			rw_exit_write(&icmp6_sysctl_lock);
		}

		return (error);
	}
	case ICMPV6CTL_STATS:
		error = icmp6_sysctl_icmp6stat(oldp, oldlenp, newp);
		break;

	case ICMPV6CTL_ND6_QUEUED:
		error = sysctl_rdint(oldp, oldlenp, newp,
		    atomic_load_int(&ln_hold_total));
		break;

	case ICMPV6CTL_ND6_DELAY:
	case ICMPV6CTL_ND6_UMAXTRIES:
	case ICMPV6CTL_ND6_MMAXTRIES:
	case ICMPV6CTL_MTUDISC_HIWAT:
	case ICMPV6CTL_MTUDISC_LOWAT:
		error = sysctl_bounded_arr(icmpv6ctl_vars_unlocked,
		    nitems(icmpv6ctl_vars_unlocked), name, namelen,
		    oldp, oldlenp, newp, newlen);
		break;

	default:
		NET_LOCK();
		error = sysctl_bounded_arr(icmpv6ctl_vars,
		    nitems(icmpv6ctl_vars), name, namelen, oldp, oldlenp, newp,
		    newlen);
		NET_UNLOCK();
		break;
	}

	return (error);
}
#endif /* SMALL_KERNEL */

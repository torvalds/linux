/*-
 * Copyright (c) 2014-2016 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet6.h"
#include "opt_ipstealth.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/pfil.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/in6_var.h>
#include <netinet6/in6_fib.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>

static int
ip6_findroute(struct nhop6_basic *pnh, const struct sockaddr_in6 *dst,
    struct mbuf *m)
{

	if (fib6_lookup_nh_basic(M_GETFIB(m), &dst->sin6_addr,
	    dst->sin6_scope_id, 0, dst->sin6_flowinfo, pnh) != 0) {
		IP6STAT_INC(ip6s_noroute);
		IP6STAT_INC(ip6s_cantforward);
		icmp6_error(m, ICMP6_DST_UNREACH,
		    ICMP6_DST_UNREACH_NOROUTE, 0);
		return (EHOSTUNREACH);
	}
	if (pnh->nh_flags & NHF_BLACKHOLE) {
		IP6STAT_INC(ip6s_cantforward);
		m_freem(m);
		return (EHOSTUNREACH);
	}

	if (pnh->nh_flags & NHF_REJECT) {
		IP6STAT_INC(ip6s_cantforward);
		icmp6_error(m, ICMP6_DST_UNREACH,
		    ICMP6_DST_UNREACH_REJECT, 0);
		return (EHOSTUNREACH);
	}
	return (0);
}

struct mbuf*
ip6_tryforward(struct mbuf *m)
{
	struct sockaddr_in6 dst;
	struct nhop6_basic nh;
	struct m_tag *fwd_tag;
	struct ip6_hdr *ip6;
	struct ifnet *rcvif;
	uint32_t plen;
	int error;

	/*
	 * Fallback conditions to ip6_input for slow path processing.
	 */
	ip6 = mtod(m, struct ip6_hdr *);
	if ((m->m_flags & (M_BCAST | M_MCAST)) != 0 ||
	    ip6->ip6_nxt == IPPROTO_HOPOPTS ||
	    IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst) ||
	    IN6_IS_ADDR_LINKLOCAL(&ip6->ip6_dst) ||
	    IN6_IS_ADDR_LINKLOCAL(&ip6->ip6_src) ||
	    IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src) ||
	    in6_localip(&ip6->ip6_dst))
		return (m);
	/*
	 * Check that the amount of data in the buffers
	 * is as at least much as the IPv6 header would have us expect.
	 * Trim mbufs if longer than we expect.
	 * Drop packet if shorter than we expect.
	 */
	rcvif = m->m_pkthdr.rcvif;
	plen = ntohs(ip6->ip6_plen);
	if (plen == 0) {
		/*
		 * Jumbograms must have hop-by-hop header and go via
		 * slow path.
		 */
		IP6STAT_INC(ip6s_badoptions);
		goto dropin;
	}
	if (m->m_pkthdr.len - sizeof(struct ip6_hdr) < plen) {
		IP6STAT_INC(ip6s_tooshort);
		in6_ifstat_inc(rcvif, ifs6_in_truncated);
		goto dropin;
	}
	if (m->m_pkthdr.len > sizeof(struct ip6_hdr) + plen) {
		if (m->m_len == m->m_pkthdr.len) {
			m->m_len = sizeof(struct ip6_hdr) + plen;
			m->m_pkthdr.len = sizeof(struct ip6_hdr) + plen;
		} else
			m_adj(m, sizeof(struct ip6_hdr) + plen -
			    m->m_pkthdr.len);
	}

	/*
	 * Hop limit.
	 */
#ifdef IPSTEALTH
	if (!V_ip6stealth)
#endif
	if (ip6->ip6_hlim <= IPV6_HLIMDEC) {
		icmp6_error(m, ICMP6_TIME_EXCEEDED,
		    ICMP6_TIME_EXCEED_TRANSIT, 0);
		m = NULL;
		goto dropin;
	}

	bzero(&dst, sizeof(dst));
	dst.sin6_family = AF_INET6;
	dst.sin6_len = sizeof(dst);
	dst.sin6_addr = ip6->ip6_dst;

	/*
	 * Incoming packet firewall processing.
	 */
	if (!PFIL_HOOKED_IN(V_inet6_pfil_head))
		goto passin;
	if (pfil_run_hooks(V_inet6_pfil_head, &m, rcvif, PFIL_IN, NULL) !=
	    PFIL_PASS)
		goto dropin;
	/*
	 * If packet filter sets the M_FASTFWD_OURS flag, this means
	 * that new destination or next hop is our local address.
	 * So, we can just go back to ip6_input.
	 * XXX: should we decrement ip6_hlim in such case?
	 *
	 * Also it can forward packet to another destination, e.g.
	 * M_IP6_NEXTHOP flag is set and fwd_tag is attached to mbuf.
	 */
	if (m->m_flags & M_FASTFWD_OURS)
		return (m);

	ip6 = mtod(m, struct ip6_hdr *);
	if ((m->m_flags & M_IP6_NEXTHOP) &&
	    (fwd_tag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL)) != NULL) {
		/*
		 * Now we will find route to forwarded by pfil destination.
		 */
		bcopy((fwd_tag + 1), &dst, sizeof(dst));
		m->m_flags &= ~M_IP6_NEXTHOP;
		m_tag_delete(m, fwd_tag);
	} else {
		/* Update dst since pfil could change it */
		dst.sin6_addr = ip6->ip6_dst;
	}
passin:
	/*
	 * Find route to destination.
	 */
	if (ip6_findroute(&nh, &dst, m) != 0) {
		m = NULL;
		in6_ifstat_inc(rcvif, ifs6_in_noroute);
		goto dropin;
	}
	if (!PFIL_HOOKED_OUT(V_inet6_pfil_head)) {
		if (m->m_pkthdr.len > nh.nh_mtu) {
			in6_ifstat_inc(nh.nh_ifp, ifs6_in_toobig);
			icmp6_error(m, ICMP6_PACKET_TOO_BIG, 0, nh.nh_mtu);
			m = NULL;
			goto dropout;
		}
		goto passout;
	}

	/*
	 * Outgoing packet firewall processing.
	 */
	if (pfil_run_hooks(V_inet6_pfil_head, &m, nh.nh_ifp, PFIL_OUT |
	    PFIL_FWD, NULL) != PFIL_PASS)
		goto dropout;

	/*
	 * We used slow path processing for packets with scoped addresses.
	 * So, scope checks aren't needed here.
	 */
	if (m->m_pkthdr.len > nh.nh_mtu) {
		in6_ifstat_inc(nh.nh_ifp, ifs6_in_toobig);
		icmp6_error(m, ICMP6_PACKET_TOO_BIG, 0, nh.nh_mtu);
		m = NULL;
		goto dropout;
	}

	/*
	 * If packet filter sets the M_FASTFWD_OURS flag, this means
	 * that new destination or next hop is our local address.
	 * So, we can just go back to ip6_input.
	 *
	 * Also it can forward packet to another destination, e.g.
	 * M_IP6_NEXTHOP flag is set and fwd_tag is attached to mbuf.
	 */
	if (m->m_flags & M_FASTFWD_OURS) {
		/*
		 * XXX: we did one hop and should decrement hop limit. But
		 * now we are the destination and just don't pay attention.
		 */
		return (m);
	}
	/*
	 * Again. A packet filter could change the destination address.
	 */
	ip6 = mtod(m, struct ip6_hdr *);
	if (m->m_flags & M_IP6_NEXTHOP)
		fwd_tag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL);
	else
		fwd_tag = NULL;

	if (fwd_tag != NULL ||
	    !IN6_ARE_ADDR_EQUAL(&dst.sin6_addr, &ip6->ip6_dst)) {
		if (fwd_tag != NULL) {
			bcopy((fwd_tag + 1), &dst, sizeof(dst));
			m->m_flags &= ~M_IP6_NEXTHOP;
			m_tag_delete(m, fwd_tag);
		} else
			dst.sin6_addr = ip6->ip6_dst;
		/*
		 * Redo route lookup with new destination address
		 */
		if (ip6_findroute(&nh, &dst, m) != 0) {
			m = NULL;
			goto dropout;
		}
	}
passout:
#ifdef IPSTEALTH
	if (!V_ip6stealth)
#endif
	{
		ip6->ip6_hlim -= IPV6_HLIMDEC;
	}

	m_clrprotoflags(m);	/* Avoid confusing lower layers. */
	IP_PROBE(send, NULL, NULL, ip6, nh.nh_ifp, NULL, ip6);

	/*
	 * XXX: we need to use destination address with embedded scope
	 * zone id, because LLTABLE uses such form of addresses for lookup.
	 */
	dst.sin6_addr = nh.nh_addr;
	if (IN6_IS_SCOPE_LINKLOCAL(&dst.sin6_addr))
		dst.sin6_addr.s6_addr16[1] = htons(nh.nh_ifp->if_index & 0xffff);

	error = (*nh.nh_ifp->if_output)(nh.nh_ifp, m,
	    (struct sockaddr *)&dst, NULL);
	if (error != 0) {
		in6_ifstat_inc(nh.nh_ifp, ifs6_out_discard);
		IP6STAT_INC(ip6s_cantforward);
	} else {
		in6_ifstat_inc(nh.nh_ifp, ifs6_out_forward);
		IP6STAT_INC(ip6s_forward);
	}
	return (NULL);
dropin:
	in6_ifstat_inc(rcvif, ifs6_in_discard);
	goto drop;
dropout:
	in6_ifstat_inc(nh.nh_ifp, ifs6_out_discard);
drop:
	if (m != NULL)
		m_freem(m);
	return (NULL);
}


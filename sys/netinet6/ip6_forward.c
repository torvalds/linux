/*	$OpenBSD: ip6_forward.c,v 1.129 2025/09/16 09:18:55 florian Exp $	*/
/*	$KAME: ip6_forward.c,v 1.75 2001/06/29 12:42:13 jinmei Exp $	*/

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

#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#if NPF > 0
#include <net/pfvar.h>
#endif

#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>
#include <netinet6/nd6.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#ifdef IPSEC
#include <netinet/ip_ipsp.h>
#endif

/*
 * Forward a packet.  If some error occurs return the sender
 * an icmp packet.  Note we can't always generate a meaningful
 * icmp message because icmp doesn't have a large enough repertoire
 * of codes and types.
 *
 * If not forwarding, just drop the packet.  This could be confusing
 * if ip6_forwarding was zero but some routing protocol was advancing
 * us as a gateway to somewhere.  However, we must let the routing
 * protocol deal with that.
 *
 */

void
ip6_forward(struct mbuf *m, struct route *ro, int flags)
{
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct route iproute;
	struct rtentry *rt;
	struct sockaddr *dst;
	struct ifnet *ifp = NULL;
	u_int rtableid = m->m_pkthdr.ph_rtableid;
	u_int ifidx = m->m_pkthdr.ph_ifidx;
	u_int8_t loopcnt = m->m_pkthdr.ph_loopcnt;
	u_int icmp_len;
	char icmp_buf[MHLEN];
	CTASSERT(sizeof(struct ip6_hdr) + sizeof(struct tcphdr) +
	    MAX_TCPOPTLEN <= sizeof(icmp_buf));
	u_short mflags, pfflags;
	struct mbuf *mcopy;
	int error = 0, type = 0, code = 0, destmtu = 0;
	u_int orig_rtableid;
#ifdef IPSEC
	struct tdb *tdb = NULL;
#endif /* IPSEC */

	/*
	 * Do not forward packets to multicast destination (should be handled
	 * by ip6_mforward().
	 * Do not forward packets with unspecified source.  It was discussed
	 * in July 2000, on ipngwg mailing list.
	 */
	if ((m->m_flags & (M_BCAST|M_MCAST)) != 0 ||
	    IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst) ||
	    IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src)) {
		ip6stat_inc(ip6s_cantforward);
		m_freem(m);
		goto done;
	}

	if (ip6->ip6_hlim <= IPV6_HLIMDEC) {
		icmp6_error(m, ICMP6_TIME_EXCEEDED,
				ICMP6_TIME_EXCEED_TRANSIT, 0);
		goto done;
	}
	ip6->ip6_hlim -= IPV6_HLIMDEC;

	/*
	 * Save at most ICMPV6_PLD_MAXLEN (= the min IPv6 MTU -
	 * size of IPv6 + ICMPv6 headers) bytes of the packet in case
	 * we need to generate an ICMP6 message to the src.
	 * Thanks to M_EXT, in most cases copy will not occur.
	 * For small packets copy original onto stack instead of mbuf.
	 *
	 * For final protocol header like TCP or UDP, full header chain in
	 * ICMP6 packet is not necessary.  In this case only copy small
	 * part of original packet and save it on stack instead of mbuf.
	 * Although this violates RFC 4443 2.4. (c), it avoids additional
	 * mbuf allocations.  Also pf nat and rdr do not affect the shared
	 * mbuf cluster.
	 *
	 * It is important to save it before IPsec processing as IPsec
	 * processing may modify the mbuf.
	 */
	switch (ip6->ip6_nxt) {
	case IPPROTO_TCP:
		icmp_len = sizeof(struct ip6_hdr) + sizeof(struct tcphdr) +
		    MAX_TCPOPTLEN;
		break;
	case IPPROTO_UDP:
		icmp_len = sizeof(struct ip6_hdr) + sizeof(struct udphdr);
		break;
	case IPPROTO_ESP:
		icmp_len = sizeof(struct ip6_hdr) + 2 * sizeof(u_int32_t);
		break;
	default:
		icmp_len = ICMPV6_PLD_MAXLEN;
		break;
	}
	if (icmp_len > m->m_pkthdr.len)
		icmp_len = m->m_pkthdr.len;
	if (icmp_len <= sizeof(icmp_buf)) {
		mflags = m->m_flags;
		pfflags = m->m_pkthdr.pf.flags;
		m_copydata(m, 0, icmp_len, icmp_buf);
		mcopy = NULL;
	} else {
		mcopy = m_copym(m, 0, icmp_len, M_NOWAIT);
		icmp_len = 0;
	}

	orig_rtableid = m->m_pkthdr.ph_rtableid;
#if NPF > 0
reroute:
#endif

#ifdef IPSEC
	if (ipsec_in_use) {
		error = ip6_output_ipsec_lookup(m, NULL, &tdb);
		if (error) {
			/*
			 * -EINVAL is used to indicate that the packet should
			 * be silently dropped, typically because we've asked
			 * key management for an SA.
			 */
			if (error == -EINVAL) /* Should silently drop packet */
				error = 0;

			m_freem(m);
			goto freecopy;
		}
	}
#endif /* IPSEC */

	if (ro == NULL) {
		ro = &iproute;
		ro->ro_rt = NULL;
	}
	rt = route6_mpath(ro, &ip6->ip6_dst, &ip6->ip6_src,
	    m->m_pkthdr.ph_rtableid);
	if (rt == NULL) {
		ip6stat_inc(ip6s_noroute);
		type = ICMP6_DST_UNREACH;
		code = ICMP6_DST_UNREACH_NOROUTE;
		m_freem(m);
		goto icmperror;
	}
	dst = &ro->ro_dstsa;

	/*
	 * Scope check: if a packet can't be delivered to its destination
	 * for the reason that the destination is beyond the scope of the
	 * source address, discard the packet and return an icmp6 destination
	 * unreachable error with Code 2 (beyond scope of source address).
	 * [draft-ietf-ipngwg-icmp-v3-00.txt, Section 3.1]
	 */
	if (in6_addr2scopeid(ifidx, &ip6->ip6_src) !=
	    in6_addr2scopeid(rt->rt_ifidx, &ip6->ip6_src)) {
		ip6stat_inc(ip6s_cantforward);
		ip6stat_inc(ip6s_badscope);
		type = ICMP6_DST_UNREACH;
		code = ICMP6_DST_UNREACH_BEYONDSCOPE;
		m_freem(m);
		goto icmperror;
	}

#ifdef IPSEC
	/*
	 * Check if the packet needs encapsulation.
	 * ipsp_process_packet will never come back to here.
	 */
	if (tdb != NULL) {
		/* Callee frees mbuf */
		error = ip6_output_ipsec_send(tdb, m, ro, orig_rtableid, 0, 1);
		rt = ro->ro_rt;
		if (error)
			goto senderr;
		goto freecopy;
	}
#endif /* IPSEC */

	if (rt->rt_flags & RTF_GATEWAY)
		dst = rt->rt_gateway;

	/*
	 * If we are to forward the packet using the same interface
	 * as one we got the packet from, perhaps we should send a redirect
	 * to sender to shortcut a hop.
	 * Only send redirect if source is sending directly to us,
	 * and if packet was not source routed (or has any options).
	 * Also, don't send redirect if forwarding using a route
	 * modified by a redirect.
	 */
	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL) {
		m_freem(m);
		goto freecopy;
	}
	if (rt->rt_ifidx == ifidx &&
	    !ISSET(rt->rt_flags, RTF_DYNAMIC|RTF_MODIFIED) &&
	    !ISSET(flags, IPV6_REDIRECT) &&
	    atomic_load_int(&ip6_sendredirects)) {
		if ((ifp->if_flags & IFF_POINTOPOINT) &&
		    nd6_is_addr_neighbor(&ro->ro_dstsin6, ifp)) {
			/*
			 * If the incoming interface is equal to the outgoing
			 * one, the link attached to the interface is
			 * point-to-point, and the IPv6 destination is
			 * regarded as on-link on the link, then it will be
			 * highly probable that the destination address does
			 * not exist on the link and that the packet is going
			 * to loop.  Thus, we immediately drop the packet and
			 * send an ICMPv6 error message.
			 * For other routing loops, we dare to let the packet
			 * go to the loop, so that a remote diagnosing host
			 * can detect the loop by traceroute.
			 * type/code is based on suggestion by Rich Draves.
			 * not sure if it is the best pick.
			 */
			type = ICMP6_DST_UNREACH;
			code = ICMP6_DST_UNREACH_ADDR;
			m_freem(m);
			goto icmperror;
		}
		type = ND_REDIRECT;
	}

	/*
	 * Fake scoped addresses. Note that even link-local source or
	 * destination can appear, if the originating node just sends the
	 * packet to us (without address resolution for the destination).
	 * Since both icmp6_error and icmp6_redirect_output fill the embedded
	 * link identifiers, we can do this stuff after making a copy for
	 * returning an error.
	 */
	if (IN6_IS_SCOPE_EMBED(&ip6->ip6_src))
		ip6->ip6_src.s6_addr16[1] = 0;
	if (IN6_IS_SCOPE_EMBED(&ip6->ip6_dst))
		ip6->ip6_dst.s6_addr16[1] = 0;

#if NPF > 0
	if (pf_test(AF_INET6, PF_FWD, ifp, &m) != PF_PASS) {
		m_freem(m);
		goto senderr;
	}
	if (m == NULL)
		goto senderr;
	ip6 = mtod(m, struct ip6_hdr *);
	if ((m->m_pkthdr.pf.flags & (PF_TAG_REROUTE | PF_TAG_GENERATED)) ==
	    (PF_TAG_REROUTE | PF_TAG_GENERATED)) {
		/* already rerun the route lookup, go on */
		m->m_pkthdr.pf.flags &= ~(PF_TAG_GENERATED | PF_TAG_REROUTE);
	} else if (m->m_pkthdr.pf.flags & PF_TAG_REROUTE) {
		/* tag as generated to skip over pf_test on rerun */
		m->m_pkthdr.pf.flags |= PF_TAG_GENERATED;
		SET(flags, IPV6_REDIRECT);
		if (ro == &iproute)
			rtfree(ro->ro_rt);
		ro = NULL;
		if_put(ifp);
		ifp = NULL;
		goto reroute;
	}
#endif

#ifdef IPSEC
	if (ISSET(flags, IPV6_FORWARDING) &&
	    ISSET(flags, IPV6_FORWARDING_IPSEC) &&
	    !ISSET(m->m_pkthdr.ph_tagsset, PACKET_TAG_IPSEC_IN_DONE)) {
		error = EHOSTUNREACH;
		goto senderr;
	}
#endif

	error = if_output_tso(ifp, &m, dst, rt, ifp->if_mtu);
	if (error)
		ip6stat_inc(ip6s_cantforward);
	else if (m == NULL)
		ip6stat_inc(ip6s_forward);
	if (error || m == NULL)
		goto senderr;

	type = ICMP6_PACKET_TOO_BIG;
	destmtu = ifp->if_mtu;
	m_freem(m);
	goto icmperror;

senderr:
	if (mcopy == NULL && icmp_len == 0)
		goto done;

	switch (error) {
	case 0:
		if (type == ND_REDIRECT) {
			if (icmp_len != 0) {
				mcopy = m_gethdr(M_DONTWAIT, MT_DATA);
				if (mcopy == NULL)
					goto done;
				mcopy->m_len = mcopy->m_pkthdr.len = icmp_len;
				mcopy->m_flags |= (mflags & M_COPYFLAGS);
				mcopy->m_pkthdr.ph_rtableid = rtableid;
				mcopy->m_pkthdr.ph_ifidx = ifidx;
				mcopy->m_pkthdr.ph_loopcnt = loopcnt;
				mcopy->m_pkthdr.pf.flags |=
				    (pfflags & PF_TAG_GENERATED);
				memcpy(mcopy->m_data, icmp_buf, icmp_len);
			}
			if (mcopy != NULL) {
				icmp6_redirect_output(mcopy, rt);
				ip6stat_inc(ip6s_redirectsent);
			}
			goto done;
		}
		goto freecopy;

	case EMSGSIZE:
		type = ICMP6_PACKET_TOO_BIG;
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
		ip6stat_inc(ip6s_cantfrag);
		if (destmtu == 0)
			goto freecopy;
		break;

	case EACCES:
		/*
		 * pf(4) blocked the packet. There is no need to send an ICMP
		 * packet back since pf(4) takes care of it.
		 */
		goto freecopy;

	case ENOBUFS:
		/* Tell source to slow down like source quench in IP? */
		goto freecopy;

	case ENETUNREACH:	/* shouldn't happen, checked above */
	case EHOSTUNREACH:
	case ENETDOWN:
	case EHOSTDOWN:
	default:
		type = ICMP6_DST_UNREACH;
		code = ICMP6_DST_UNREACH_ADDR;
		break;
	}
 icmperror:
	if (icmp_len != 0) {
		mcopy = m_gethdr(M_DONTWAIT, MT_DATA);
		if (mcopy == NULL)
			goto done;
		mcopy->m_len = mcopy->m_pkthdr.len = icmp_len;
		mcopy->m_flags |= (mflags & M_COPYFLAGS);
		mcopy->m_pkthdr.ph_rtableid = rtableid;
		mcopy->m_pkthdr.ph_ifidx = ifidx;
		mcopy->m_pkthdr.ph_loopcnt = loopcnt;
		mcopy->m_pkthdr.pf.flags |= (pfflags & PF_TAG_GENERATED);
		memcpy(mcopy->m_data, icmp_buf, icmp_len);
	}
	if (mcopy != NULL)
		icmp6_error(mcopy, type, code, destmtu);
	goto done;

 freecopy:
	m_freem(mcopy);
 done:
	if (ro == &iproute)
		rtfree(ro->ro_rt);
	if_put(ifp);
#ifdef IPSEC
	tdb_unref(tdb);
#endif /* IPSEC */
}

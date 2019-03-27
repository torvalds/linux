/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Andre Oppermann, Internet Business Solutions AG
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
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * ip_fastforward gets its speed from processing the forwarded packet to
 * completion (if_output on the other side) without any queues or netisr's.
 * The receiving interface DMAs the packet into memory, the upper half of
 * driver calls ip_fastforward, we do our routing table lookup and directly
 * send it off to the outgoing interface, which DMAs the packet to the
 * network card. The only part of the packet we touch with the CPU is the
 * IP header (unless there are complex firewall rules touching other parts
 * of the packet, but that is up to you). We are essentially limited by bus
 * bandwidth and how fast the network card/driver can set up receives and
 * transmits.
 *
 * We handle basic errors, IP header errors, checksum errors,
 * destination unreachable, fragmentation and fragmentation needed and
 * report them via ICMP to the sender.
 *
 * Else if something is not pure IPv4 unicast forwarding we fall back to
 * the normal ip_input processing path. We should only be called from
 * interfaces connected to the outside world.
 *
 * Firewalling is fully supported including divert, ipfw fwd and ipfilter
 * ipnat and address rewrite.
 *
 * IPSEC is not supported if this host is a tunnel broker. IPSEC is
 * supported for connections to/from local host.
 *
 * We try to do the least expensive (in CPU ops) checks and operations
 * first to catch junk with as little overhead as possible.
 * 
 * We take full advantage of hardware support for IP checksum and
 * fragmentation offloading.
 *
 * We don't do ICMP redirect in the fast forwarding path. I have had my own
 * cases where two core routers with Zebra routing suite would send millions
 * ICMP redirects to connected hosts if the destination router was not the
 * default gateway. In one case it was filling the routing table of a host
 * with approximately 300.000 cloned redirect entries until it ran out of
 * kernel memory. However the networking code proved very robust and it didn't
 * crash or fail in other ways.
 */

/*
 * Many thanks to Matt Thomas of NetBSD for basic structure of ip_flow.c which
 * is being followed here.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ipstealth.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/sdt.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/pfil.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_fib.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_options.h>

#include <machine/in_cksum.h>

static int
ip_findroute(struct nhop4_basic *pnh, struct in_addr dest, struct mbuf *m)
{

	bzero(pnh, sizeof(*pnh));
	if (fib4_lookup_nh_basic(M_GETFIB(m), dest, 0, 0, pnh) != 0) {
		IPSTAT_INC(ips_noroute);
		IPSTAT_INC(ips_cantforward);
		icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_HOST, 0, 0);
		return (EHOSTUNREACH);
	}
	/*
	 * Drop blackholed traffic and directed broadcasts.
	 */
	if ((pnh->nh_flags & (NHF_BLACKHOLE | NHF_BROADCAST)) != 0) {
		IPSTAT_INC(ips_cantforward);
		m_freem(m);
		return (EHOSTUNREACH);
	}

	if (pnh->nh_flags & NHF_REJECT) {
		IPSTAT_INC(ips_cantforward);
		icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_HOST, 0, 0);
		return (EHOSTUNREACH);
	}

	return (0);
}

/*
 * Try to forward a packet based on the destination address.
 * This is a fast path optimized for the plain forwarding case.
 * If the packet is handled (and consumed) here then we return NULL;
 * otherwise mbuf is returned and the packet should be delivered
 * to ip_input for full processing.
 */
struct mbuf *
ip_tryforward(struct mbuf *m)
{
	struct ip *ip;
	struct mbuf *m0 = NULL;
	struct nhop4_basic nh;
	struct sockaddr_in dst;
	struct in_addr dest, odest, rtdest;
	uint16_t ip_len, ip_off;
	int error = 0;
	struct m_tag *fwd_tag = NULL;

	/*
	 * Are we active and forwarding packets?
	 */

	M_ASSERTVALID(m);
	M_ASSERTPKTHDR(m);

#ifdef ALTQ
	/*
	 * Is packet dropped by traffic conditioner?
	 */
	if (altq_input != NULL && (*altq_input)(m, AF_INET) == 0)
		goto drop;
#endif

	/*
	 * Only IP packets without options
	 */
	ip = mtod(m, struct ip *);

	if (ip->ip_hl != (sizeof(struct ip) >> 2)) {
		if (V_ip_doopts == 1)
			return m;
		else if (V_ip_doopts == 2) {
			icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_FILTER_PROHIB,
				0, 0);
			return NULL;	/* mbuf already free'd */
		}
		/* else ignore IP options and continue */
	}

	/*
	 * Only unicast IP, not from loopback, no L2 or IP broadcast,
	 * no multicast, no INADDR_ANY
	 *
	 * XXX: Probably some of these checks could be direct drop
	 * conditions.  However it is not clear whether there are some
	 * hacks or obscure behaviours which make it necessary to
	 * let ip_input handle it.  We play safe here and let ip_input
	 * deal with it until it is proven that we can directly drop it.
	 */
	if ((m->m_flags & (M_BCAST|M_MCAST)) ||
	    (m->m_pkthdr.rcvif->if_flags & IFF_LOOPBACK) ||
	    ntohl(ip->ip_src.s_addr) == (u_long)INADDR_BROADCAST ||
	    ntohl(ip->ip_dst.s_addr) == (u_long)INADDR_BROADCAST ||
	    IN_MULTICAST(ntohl(ip->ip_src.s_addr)) ||
	    IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) ||
	    IN_LINKLOCAL(ntohl(ip->ip_src.s_addr)) ||
	    IN_LINKLOCAL(ntohl(ip->ip_dst.s_addr)) ||
	    ip->ip_src.s_addr == INADDR_ANY ||
	    ip->ip_dst.s_addr == INADDR_ANY )
		return m;

	/*
	 * Is it for a local address on this host?
	 */
	if (in_localip(ip->ip_dst))
		return m;

	IPSTAT_INC(ips_total);

	/*
	 * Step 3: incoming packet firewall processing
	 */

	odest.s_addr = dest.s_addr = ip->ip_dst.s_addr;

	/*
	 * Run through list of ipfilter hooks for input packets
	 */
	if (!PFIL_HOOKED_IN(V_inet_pfil_head))
		goto passin;

	if (pfil_run_hooks(V_inet_pfil_head, &m, m->m_pkthdr.rcvif, PFIL_IN,
	    NULL) != PFIL_PASS)
		goto drop;

	M_ASSERTVALID(m);
	M_ASSERTPKTHDR(m);

	ip = mtod(m, struct ip *);	/* m may have changed by pfil hook */
	dest.s_addr = ip->ip_dst.s_addr;

	/*
	 * Destination address changed?
	 */
	if (odest.s_addr != dest.s_addr) {
		/*
		 * Is it now for a local address on this host?
		 */
		if (in_localip(dest))
			goto forwardlocal;
		/*
		 * Go on with new destination address
		 */
	}

	if (m->m_flags & M_FASTFWD_OURS) {
		/*
		 * ipfw changed it for a local address on this host.
		 */
		goto forwardlocal;
	}

passin:
	/*
	 * Step 4: decrement TTL and look up route
	 */

	/*
	 * Check TTL
	 */
#ifdef IPSTEALTH
	if (!V_ipstealth) {
#endif
	if (ip->ip_ttl <= IPTTLDEC) {
		icmp_error(m, ICMP_TIMXCEED, ICMP_TIMXCEED_INTRANS, 0, 0);
		return NULL;	/* mbuf already free'd */
	}

	/*
	 * Decrement the TTL and incrementally change the IP header checksum.
	 * Don't bother doing this with hw checksum offloading, it's faster
	 * doing it right here.
	 */
	ip->ip_ttl -= IPTTLDEC;
	if (ip->ip_sum >= (u_int16_t) ~htons(IPTTLDEC << 8))
		ip->ip_sum -= ~htons(IPTTLDEC << 8);
	else
		ip->ip_sum += htons(IPTTLDEC << 8);
#ifdef IPSTEALTH
	}
#endif

	/*
	 * Next hop forced by pfil(9) hook?
	 */
	if ((m->m_flags & M_IP_NEXTHOP) &&
	    ((fwd_tag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL)) != NULL)) {
		/*
		 * Now we will find route to forced destination.
		 */
		dest.s_addr = ((struct sockaddr_in *)
			    (fwd_tag + 1))->sin_addr.s_addr;
		m_tag_delete(m, fwd_tag);
		m->m_flags &= ~M_IP_NEXTHOP;
	}

	/*
	 * Find route to destination.
	 */
	if (ip_findroute(&nh, dest, m) != 0)
		return (NULL);	/* icmp unreach already sent */

	/*
	 * Avoid second route lookup by caching destination.
	 */
	rtdest.s_addr = dest.s_addr;

	/*
	 * Step 5: outgoing firewall packet processing
	 */
	if (!PFIL_HOOKED_OUT(V_inet_pfil_head))
		goto passout;

	if (pfil_run_hooks(V_inet_pfil_head, &m, nh.nh_ifp,
	    PFIL_OUT | PFIL_FWD, NULL) != PFIL_PASS)
		goto drop;

	M_ASSERTVALID(m);
	M_ASSERTPKTHDR(m);

	ip = mtod(m, struct ip *);
	dest.s_addr = ip->ip_dst.s_addr;

	/*
	 * Destination address changed?
	 */
	if (m->m_flags & M_IP_NEXTHOP)
		fwd_tag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL);
	else
		fwd_tag = NULL;
	if (odest.s_addr != dest.s_addr || fwd_tag != NULL) {
		/*
		 * Is it now for a local address on this host?
		 */
		if (m->m_flags & M_FASTFWD_OURS || in_localip(dest)) {
forwardlocal:
			/*
			 * Return packet for processing by ip_input().
			 */
			m->m_flags |= M_FASTFWD_OURS;
			return (m);
		}
		/*
		 * Redo route lookup with new destination address
		 */
		if (fwd_tag) {
			dest.s_addr = ((struct sockaddr_in *)
				    (fwd_tag + 1))->sin_addr.s_addr;
			m_tag_delete(m, fwd_tag);
			m->m_flags &= ~M_IP_NEXTHOP;
		}
		if (dest.s_addr != rtdest.s_addr &&
		    ip_findroute(&nh, dest, m) != 0)
			return (NULL);	/* icmp unreach already sent */
	}

passout:
	/*
	 * Step 6: send off the packet
	 */
	ip_len = ntohs(ip->ip_len);
	ip_off = ntohs(ip->ip_off);

	bzero(&dst, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(dst);
	dst.sin_addr = nh.nh_addr;

	/*
	 * Check if packet fits MTU or if hardware will fragment for us
	 */
	if (ip_len <= nh.nh_mtu) {
		/*
		 * Avoid confusing lower layers.
		 */
		m_clrprotoflags(m);
		/*
		 * Send off the packet via outgoing interface
		 */
		IP_PROBE(send, NULL, NULL, ip, nh.nh_ifp, ip, NULL);
		error = (*nh.nh_ifp->if_output)(nh.nh_ifp, m,
		    (struct sockaddr *)&dst, NULL);
	} else {
		/*
		 * Handle EMSGSIZE with icmp reply needfrag for TCP MTU discovery
		 */
		if (ip_off & IP_DF) {
			IPSTAT_INC(ips_cantfrag);
			icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_NEEDFRAG,
				0, nh.nh_mtu);
			goto consumed;
		} else {
			/*
			 * We have to fragment the packet
			 */
			m->m_pkthdr.csum_flags |= CSUM_IP;
			if (ip_fragment(ip, &m, nh.nh_mtu,
			    nh.nh_ifp->if_hwassist) != 0)
				goto drop;
			KASSERT(m != NULL, ("null mbuf and no error"));
			/*
			 * Send off the fragments via outgoing interface
			 */
			error = 0;
			do {
				m0 = m->m_nextpkt;
				m->m_nextpkt = NULL;
				/*
				 * Avoid confusing lower layers.
				 */
				m_clrprotoflags(m);

				IP_PROBE(send, NULL, NULL,
				    mtod(m, struct ip *), nh.nh_ifp,
				    mtod(m, struct ip *), NULL);
				/* XXX: we can use cached route here */
				error = (*nh.nh_ifp->if_output)(nh.nh_ifp, m,
				    (struct sockaddr *)&dst, NULL);
				if (error)
					break;
			} while ((m = m0) != NULL);
			if (error) {
				/* Reclaim remaining fragments */
				for (m = m0; m; m = m0) {
					m0 = m->m_nextpkt;
					m_freem(m);
				}
			} else
				IPSTAT_INC(ips_fragmented);
		}
	}

	if (error != 0)
		IPSTAT_INC(ips_odropped);
	else {
		IPSTAT_INC(ips_forward);
		IPSTAT_INC(ips_fastforward);
	}
consumed:
	return NULL;
drop:
	if (m)
		m_freem(m);
	return NULL;
}

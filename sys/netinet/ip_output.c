/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)ip_output.c	8.3 (Berkeley) 1/21/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_ratelimit.h"
#include "opt_ipsec.h"
#include "opt_mbuf_stress_test.h"
#include "opt_mpath.h"
#include "opt_route.h"
#include "opt_sctp.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/rmlock.h>
#include <sys/sdt.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/ucred.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_llatbl.h>
#include <net/netisr.h>
#include <net/pfil.h>
#include <net/route.h>
#ifdef RADIX_MPATH
#include <net/radix_mpath.h>
#endif
#include <net/rss_config.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_rss.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_options.h>

#include <netinet/udp.h>
#include <netinet/udp_var.h>

#ifdef SCTP
#include <netinet/sctp.h>
#include <netinet/sctp_crc32.h>
#endif

#include <netipsec/ipsec_support.h>

#include <machine/in_cksum.h>

#include <security/mac/mac_framework.h>

#ifdef MBUF_STRESS_TEST
static int mbuf_frag_size = 0;
SYSCTL_INT(_net_inet_ip, OID_AUTO, mbuf_frag_size, CTLFLAG_RW,
	&mbuf_frag_size, 0, "Fragment outgoing mbufs to this size");
#endif

static void	ip_mloopback(struct ifnet *, const struct mbuf *, int);


extern int in_mcast_loop;
extern	struct protosw inetsw[];

static inline int
ip_output_pfil(struct mbuf **mp, struct ifnet *ifp, struct inpcb *inp,
    struct sockaddr_in *dst, int *fibnum, int *error)
{
	struct m_tag *fwd_tag = NULL;
	struct mbuf *m;
	struct in_addr odst;
	struct ip *ip;

	m = *mp;
	ip = mtod(m, struct ip *);

	/* Run through list of hooks for output packets. */
	odst.s_addr = ip->ip_dst.s_addr;
	switch (pfil_run_hooks(V_inet_pfil_head, mp, ifp, PFIL_OUT, inp)) {
	case PFIL_DROPPED:
		*error = EPERM;
		/* FALLTHROUGH */
	case PFIL_CONSUMED:
		return 1; /* Finished */
	case PFIL_PASS:
		*error = 0;
	}
	m = *mp;
	ip = mtod(m, struct ip *);

	/* See if destination IP address was changed by packet filter. */
	if (odst.s_addr != ip->ip_dst.s_addr) {
		m->m_flags |= M_SKIP_FIREWALL;
		/* If destination is now ourself drop to ip_input(). */
		if (in_localip(ip->ip_dst)) {
			m->m_flags |= M_FASTFWD_OURS;
			if (m->m_pkthdr.rcvif == NULL)
				m->m_pkthdr.rcvif = V_loif;
			if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
				m->m_pkthdr.csum_flags |=
					CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
				m->m_pkthdr.csum_data = 0xffff;
			}
			m->m_pkthdr.csum_flags |=
				CSUM_IP_CHECKED | CSUM_IP_VALID;
#ifdef SCTP
			if (m->m_pkthdr.csum_flags & CSUM_SCTP)
				m->m_pkthdr.csum_flags |= CSUM_SCTP_VALID;
#endif
			*error = netisr_queue(NETISR_IP, m);
			return 1; /* Finished */
		}

		bzero(dst, sizeof(*dst));
		dst->sin_family = AF_INET;
		dst->sin_len = sizeof(*dst);
		dst->sin_addr = ip->ip_dst;

		return -1; /* Reloop */
	}
	/* See if fib was changed by packet filter. */
	if ((*fibnum) != M_GETFIB(m)) {
		m->m_flags |= M_SKIP_FIREWALL;
		*fibnum = M_GETFIB(m);
		return -1; /* Reloop for FIB change */
	}

	/* See if local, if yes, send it to netisr with IP_FASTFWD_OURS. */
	if (m->m_flags & M_FASTFWD_OURS) {
		if (m->m_pkthdr.rcvif == NULL)
			m->m_pkthdr.rcvif = V_loif;
		if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
			m->m_pkthdr.csum_flags |=
				CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
			m->m_pkthdr.csum_data = 0xffff;
		}
#ifdef SCTP
		if (m->m_pkthdr.csum_flags & CSUM_SCTP)
			m->m_pkthdr.csum_flags |= CSUM_SCTP_VALID;
#endif
		m->m_pkthdr.csum_flags |=
			CSUM_IP_CHECKED | CSUM_IP_VALID;

		*error = netisr_queue(NETISR_IP, m);
		return 1; /* Finished */
	}
	/* Or forward to some other address? */
	if ((m->m_flags & M_IP_NEXTHOP) &&
	    ((fwd_tag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL)) != NULL)) {
		bcopy((fwd_tag+1), dst, sizeof(struct sockaddr_in));
		m->m_flags |= M_SKIP_FIREWALL;
		m->m_flags &= ~M_IP_NEXTHOP;
		m_tag_delete(m, fwd_tag);

		return -1; /* Reloop for CHANGE of dst */
	}

	return 0;
}

/*
 * IP output.  The packet in mbuf chain m contains a skeletal IP
 * header (with len, off, ttl, proto, tos, src, dst).
 * The mbuf chain containing the packet will be freed.
 * The mbuf opt, if present, will not be freed.
 * If route ro is present and has ro_rt initialized, route lookup would be
 * skipped and ro->ro_rt would be used. If ro is present but ro->ro_rt is NULL,
 * then result of route lookup is stored in ro->ro_rt.
 *
 * In the IP forwarding case, the packet will arrive with options already
 * inserted, so must have a NULL opt pointer.
 */
int
ip_output(struct mbuf *m, struct mbuf *opt, struct route *ro, int flags,
    struct ip_moptions *imo, struct inpcb *inp)
{
	struct rm_priotracker in_ifa_tracker;
	struct epoch_tracker et;
	struct ip *ip;
	struct ifnet *ifp = NULL;	/* keep compiler happy */
	struct mbuf *m0;
	int hlen = sizeof (struct ip);
	int mtu;
	int error = 0;
	struct sockaddr_in *dst;
	const struct sockaddr_in *gw;
	struct in_ifaddr *ia;
	int isbroadcast;
	uint16_t ip_len, ip_off;
	struct route iproute;
	struct rtentry *rte;	/* cache for ro->ro_rt */
	uint32_t fibnum;
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	int no_route_but_check_spd = 0;
#endif
	M_ASSERTPKTHDR(m);

	if (inp != NULL) {
		INP_LOCK_ASSERT(inp);
		M_SETFIB(m, inp->inp_inc.inc_fibnum);
		if ((flags & IP_NODEFAULTFLOWID) == 0) {
			m->m_pkthdr.flowid = inp->inp_flowid;
			M_HASHTYPE_SET(m, inp->inp_flowtype);
		}
	}

	if (ro == NULL) {
		ro = &iproute;
		bzero(ro, sizeof (*ro));
	}

	if (opt) {
		int len = 0;
		m = ip_insertoptions(m, opt, &len);
		if (len != 0)
			hlen = len; /* ip->ip_hl is updated above */
	}
	ip = mtod(m, struct ip *);
	ip_len = ntohs(ip->ip_len);
	ip_off = ntohs(ip->ip_off);

	if ((flags & (IP_FORWARDING|IP_RAWOUTPUT)) == 0) {
		ip->ip_v = IPVERSION;
		ip->ip_hl = hlen >> 2;
		ip_fillid(ip);
	} else {
		/* Header already set, fetch hlen from there */
		hlen = ip->ip_hl << 2;
	}
	if ((flags & IP_FORWARDING) == 0)
		IPSTAT_INC(ips_localout);

	/*
	 * dst/gw handling:
	 *
	 * dst can be rewritten but always points to &ro->ro_dst.
	 * gw is readonly but can point either to dst OR rt_gateway,
	 * therefore we need restore gw if we're redoing lookup.
	 */
	gw = dst = (struct sockaddr_in *)&ro->ro_dst;
	fibnum = (inp != NULL) ? inp->inp_inc.inc_fibnum : M_GETFIB(m);
	rte = ro->ro_rt;
	if (rte == NULL) {
		bzero(dst, sizeof(*dst));
		dst->sin_family = AF_INET;
		dst->sin_len = sizeof(*dst);
		dst->sin_addr = ip->ip_dst;
	}
	NET_EPOCH_ENTER(et);
again:
	/*
	 * Validate route against routing table additions;
	 * a better/more specific route might have been added.
	 */
	if (inp)
		RT_VALIDATE(ro, &inp->inp_rt_cookie, fibnum);
	/*
	 * If there is a cached route,
	 * check that it is to the same destination
	 * and is still up.  If not, free it and try again.
	 * The address family should also be checked in case of sharing the
	 * cache with IPv6.
	 * Also check whether routing cache needs invalidation.
	 */
	rte = ro->ro_rt;
	if (rte && ((rte->rt_flags & RTF_UP) == 0 ||
		    rte->rt_ifp == NULL ||
		    !RT_LINK_IS_UP(rte->rt_ifp) ||
			  dst->sin_family != AF_INET ||
			  dst->sin_addr.s_addr != ip->ip_dst.s_addr)) {
		RO_INVALIDATE_CACHE(ro);
		rte = NULL;
	}
	ia = NULL;
	/*
	 * If routing to interface only, short circuit routing lookup.
	 * The use of an all-ones broadcast address implies this; an
	 * interface is specified by the broadcast address of an interface,
	 * or the destination address of a ptp interface.
	 */
	if (flags & IP_SENDONES) {
		if ((ia = ifatoia(ifa_ifwithbroadaddr(sintosa(dst),
						      M_GETFIB(m)))) == NULL &&
		    (ia = ifatoia(ifa_ifwithdstaddr(sintosa(dst),
						    M_GETFIB(m)))) == NULL) {
			IPSTAT_INC(ips_noroute);
			error = ENETUNREACH;
			goto bad;
		}
		ip->ip_dst.s_addr = INADDR_BROADCAST;
		dst->sin_addr = ip->ip_dst;
		ifp = ia->ia_ifp;
		ip->ip_ttl = 1;
		isbroadcast = 1;
	} else if (flags & IP_ROUTETOIF) {
		if ((ia = ifatoia(ifa_ifwithdstaddr(sintosa(dst),
						    M_GETFIB(m)))) == NULL &&
		    (ia = ifatoia(ifa_ifwithnet(sintosa(dst), 0,
						M_GETFIB(m)))) == NULL) {
			IPSTAT_INC(ips_noroute);
			error = ENETUNREACH;
			goto bad;
		}
		ifp = ia->ia_ifp;
		ip->ip_ttl = 1;
		isbroadcast = ifp->if_flags & IFF_BROADCAST ?
		    in_ifaddr_broadcast(dst->sin_addr, ia) : 0;
	} else if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) &&
	    imo != NULL && imo->imo_multicast_ifp != NULL) {
		/*
		 * Bypass the normal routing lookup for multicast
		 * packets if the interface is specified.
		 */
		ifp = imo->imo_multicast_ifp;
		IFP_TO_IA(ifp, ia, &in_ifa_tracker);
		isbroadcast = 0;	/* fool gcc */
	} else {
		/*
		 * We want to do any cloning requested by the link layer,
		 * as this is probably required in all cases for correct
		 * operation (as it is for ARP).
		 */
		if (rte == NULL) {
#ifdef RADIX_MPATH
			rtalloc_mpath_fib(ro,
			    ntohl(ip->ip_src.s_addr ^ ip->ip_dst.s_addr),
			    fibnum);
#else
			in_rtalloc_ign(ro, 0, fibnum);
#endif
			rte = ro->ro_rt;
		}
		if (rte == NULL ||
		    (rte->rt_flags & RTF_UP) == 0 ||
		    rte->rt_ifp == NULL ||
		    !RT_LINK_IS_UP(rte->rt_ifp)) {
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
			/*
			 * There is no route for this packet, but it is
			 * possible that a matching SPD entry exists.
			 */
			no_route_but_check_spd = 1;
			mtu = 0; /* Silence GCC warning. */
			goto sendit;
#endif
			IPSTAT_INC(ips_noroute);
			error = EHOSTUNREACH;
			goto bad;
		}
		ia = ifatoia(rte->rt_ifa);
		ifp = rte->rt_ifp;
		counter_u64_add(rte->rt_pksent, 1);
		rt_update_ro_flags(ro);
		if (rte->rt_flags & RTF_GATEWAY)
			gw = (struct sockaddr_in *)rte->rt_gateway;
		if (rte->rt_flags & RTF_HOST)
			isbroadcast = (rte->rt_flags & RTF_BROADCAST);
		else if (ifp->if_flags & IFF_BROADCAST)
			isbroadcast = in_ifaddr_broadcast(gw->sin_addr, ia);
		else
			isbroadcast = 0;
	}

	/*
	 * Calculate MTU.  If we have a route that is up, use that,
	 * otherwise use the interface's MTU.
	 */
	if (rte != NULL && (rte->rt_flags & (RTF_UP|RTF_HOST)))
		mtu = rte->rt_mtu;
	else
		mtu = ifp->if_mtu;
	/* Catch a possible divide by zero later. */
	KASSERT(mtu > 0, ("%s: mtu %d <= 0, rte=%p (rt_flags=0x%08x) ifp=%p",
	    __func__, mtu, rte, (rte != NULL) ? rte->rt_flags : 0, ifp));

	if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr))) {
		m->m_flags |= M_MCAST;
		/*
		 * IP destination address is multicast.  Make sure "gw"
		 * still points to the address in "ro".  (It may have been
		 * changed to point to a gateway address, above.)
		 */
		gw = dst;
		/*
		 * See if the caller provided any multicast options
		 */
		if (imo != NULL) {
			ip->ip_ttl = imo->imo_multicast_ttl;
			if (imo->imo_multicast_vif != -1)
				ip->ip_src.s_addr =
				    ip_mcast_src ?
				    ip_mcast_src(imo->imo_multicast_vif) :
				    INADDR_ANY;
		} else
			ip->ip_ttl = IP_DEFAULT_MULTICAST_TTL;
		/*
		 * Confirm that the outgoing interface supports multicast.
		 */
		if ((imo == NULL) || (imo->imo_multicast_vif == -1)) {
			if ((ifp->if_flags & IFF_MULTICAST) == 0) {
				IPSTAT_INC(ips_noroute);
				error = ENETUNREACH;
				goto bad;
			}
		}
		/*
		 * If source address not specified yet, use address
		 * of outgoing interface.
		 */
		if (ip->ip_src.s_addr == INADDR_ANY) {
			/* Interface may have no addresses. */
			if (ia != NULL)
				ip->ip_src = IA_SIN(ia)->sin_addr;
		}

		if ((imo == NULL && in_mcast_loop) ||
		    (imo && imo->imo_multicast_loop)) {
			/*
			 * Loop back multicast datagram if not expressly
			 * forbidden to do so, even if we are not a member
			 * of the group; ip_input() will filter it later,
			 * thus deferring a hash lookup and mutex acquisition
			 * at the expense of a cheap copy using m_copym().
			 */
			ip_mloopback(ifp, m, hlen);
		} else {
			/*
			 * If we are acting as a multicast router, perform
			 * multicast forwarding as if the packet had just
			 * arrived on the interface to which we are about
			 * to send.  The multicast forwarding function
			 * recursively calls this function, using the
			 * IP_FORWARDING flag to prevent infinite recursion.
			 *
			 * Multicasts that are looped back by ip_mloopback(),
			 * above, will be forwarded by the ip_input() routine,
			 * if necessary.
			 */
			if (V_ip_mrouter && (flags & IP_FORWARDING) == 0) {
				/*
				 * If rsvp daemon is not running, do not
				 * set ip_moptions. This ensures that the packet
				 * is multicast and not just sent down one link
				 * as prescribed by rsvpd.
				 */
				if (!V_rsvp_on)
					imo = NULL;
				if (ip_mforward &&
				    ip_mforward(ip, ifp, m, imo) != 0) {
					m_freem(m);
					goto done;
				}
			}
		}

		/*
		 * Multicasts with a time-to-live of zero may be looped-
		 * back, above, but must not be transmitted on a network.
		 * Also, multicasts addressed to the loopback interface
		 * are not sent -- the above call to ip_mloopback() will
		 * loop back a copy. ip_input() will drop the copy if
		 * this host does not belong to the destination group on
		 * the loopback interface.
		 */
		if (ip->ip_ttl == 0 || ifp->if_flags & IFF_LOOPBACK) {
			m_freem(m);
			goto done;
		}

		goto sendit;
	}

	/*
	 * If the source address is not specified yet, use the address
	 * of the outoing interface.
	 */
	if (ip->ip_src.s_addr == INADDR_ANY) {
		/* Interface may have no addresses. */
		if (ia != NULL) {
			ip->ip_src = IA_SIN(ia)->sin_addr;
		}
	}

	/*
	 * Look for broadcast address and
	 * verify user is allowed to send
	 * such a packet.
	 */
	if (isbroadcast) {
		if ((ifp->if_flags & IFF_BROADCAST) == 0) {
			error = EADDRNOTAVAIL;
			goto bad;
		}
		if ((flags & IP_ALLOWBROADCAST) == 0) {
			error = EACCES;
			goto bad;
		}
		/* don't allow broadcast messages to be fragmented */
		if (ip_len > mtu) {
			error = EMSGSIZE;
			goto bad;
		}
		m->m_flags |= M_BCAST;
	} else {
		m->m_flags &= ~M_BCAST;
	}

sendit:
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	if (IPSEC_ENABLED(ipv4)) {
		if ((error = IPSEC_OUTPUT(ipv4, m, inp)) != 0) {
			if (error == EINPROGRESS)
				error = 0;
			goto done;
		}
	}
	/*
	 * Check if there was a route for this packet; return error if not.
	 */
	if (no_route_but_check_spd) {
		IPSTAT_INC(ips_noroute);
		error = EHOSTUNREACH;
		goto bad;
	}
	/* Update variables that are affected by ipsec4_output(). */
	ip = mtod(m, struct ip *);
	hlen = ip->ip_hl << 2;
#endif /* IPSEC */

	/* Jump over all PFIL processing if hooks are not active. */
	if (PFIL_HOOKED_OUT(V_inet_pfil_head)) {
		switch (ip_output_pfil(&m, ifp, inp, dst, &fibnum, &error)) {
		case 1: /* Finished */
			goto done;

		case 0: /* Continue normally */
			ip = mtod(m, struct ip *);
			break;

		case -1: /* Need to try again */
			/* Reset everything for a new round */
			RO_RTFREE(ro);
			ro->ro_prepend = NULL;
			rte = NULL;
			gw = dst;
			ip = mtod(m, struct ip *);
			goto again;

		}
	}

	/* 127/8 must not appear on wire - RFC1122. */
	if ((ntohl(ip->ip_dst.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET ||
	    (ntohl(ip->ip_src.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET) {
		if ((ifp->if_flags & IFF_LOOPBACK) == 0) {
			IPSTAT_INC(ips_badaddr);
			error = EADDRNOTAVAIL;
			goto bad;
		}
	}

	m->m_pkthdr.csum_flags |= CSUM_IP;
	if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA & ~ifp->if_hwassist) {
		in_delayed_cksum(m);
		m->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}
#ifdef SCTP
	if (m->m_pkthdr.csum_flags & CSUM_SCTP & ~ifp->if_hwassist) {
		sctp_delayed_cksum(m, (uint32_t)(ip->ip_hl << 2));
		m->m_pkthdr.csum_flags &= ~CSUM_SCTP;
	}
#endif

	/*
	 * If small enough for interface, or the interface will take
	 * care of the fragmentation for us, we can just send directly.
	 */
	if (ip_len <= mtu ||
	    (m->m_pkthdr.csum_flags & ifp->if_hwassist & CSUM_TSO) != 0) {
		ip->ip_sum = 0;
		if (m->m_pkthdr.csum_flags & CSUM_IP & ~ifp->if_hwassist) {
			ip->ip_sum = in_cksum(m, hlen);
			m->m_pkthdr.csum_flags &= ~CSUM_IP;
		}

		/*
		 * Record statistics for this interface address.
		 * With CSUM_TSO the byte/packet count will be slightly
		 * incorrect because we count the IP+TCP headers only
		 * once instead of for every generated packet.
		 */
		if (!(flags & IP_FORWARDING) && ia) {
			if (m->m_pkthdr.csum_flags & CSUM_TSO)
				counter_u64_add(ia->ia_ifa.ifa_opackets,
				    m->m_pkthdr.len / m->m_pkthdr.tso_segsz);
			else
				counter_u64_add(ia->ia_ifa.ifa_opackets, 1);

			counter_u64_add(ia->ia_ifa.ifa_obytes, m->m_pkthdr.len);
		}
#ifdef MBUF_STRESS_TEST
		if (mbuf_frag_size && m->m_pkthdr.len > mbuf_frag_size)
			m = m_fragment(m, M_NOWAIT, mbuf_frag_size);
#endif
		/*
		 * Reset layer specific mbuf flags
		 * to avoid confusing lower layers.
		 */
		m_clrprotoflags(m);
		IP_PROBE(send, NULL, NULL, ip, ifp, ip, NULL);
#ifdef RATELIMIT
		if (inp != NULL) {
			if (inp->inp_flags2 & INP_RATE_LIMIT_CHANGED)
				in_pcboutput_txrtlmt(inp, ifp, m);
			/* stamp send tag on mbuf */
			m->m_pkthdr.snd_tag = inp->inp_snd_tag;
		} else {
			m->m_pkthdr.snd_tag = NULL;
		}
#endif
		error = (*ifp->if_output)(ifp, m,
		    (const struct sockaddr *)gw, ro);
#ifdef RATELIMIT
		/* check for route change */
		if (error == EAGAIN)
			in_pcboutput_eagain(inp);
#endif
		goto done;
	}

	/* Balk when DF bit is set or the interface didn't support TSO. */
	if ((ip_off & IP_DF) || (m->m_pkthdr.csum_flags & CSUM_TSO)) {
		error = EMSGSIZE;
		IPSTAT_INC(ips_cantfrag);
		goto bad;
	}

	/*
	 * Too large for interface; fragment if possible. If successful,
	 * on return, m will point to a list of packets to be sent.
	 */
	error = ip_fragment(ip, &m, mtu, ifp->if_hwassist);
	if (error)
		goto bad;
	for (; m; m = m0) {
		m0 = m->m_nextpkt;
		m->m_nextpkt = 0;
		if (error == 0) {
			/* Record statistics for this interface address. */
			if (ia != NULL) {
				counter_u64_add(ia->ia_ifa.ifa_opackets, 1);
				counter_u64_add(ia->ia_ifa.ifa_obytes,
				    m->m_pkthdr.len);
			}
			/*
			 * Reset layer specific mbuf flags
			 * to avoid confusing upper layers.
			 */
			m_clrprotoflags(m);

			IP_PROBE(send, NULL, NULL, mtod(m, struct ip *), ifp,
			    mtod(m, struct ip *), NULL);
#ifdef RATELIMIT
			if (inp != NULL) {
				if (inp->inp_flags2 & INP_RATE_LIMIT_CHANGED)
					in_pcboutput_txrtlmt(inp, ifp, m);
				/* stamp send tag on mbuf */
				m->m_pkthdr.snd_tag = inp->inp_snd_tag;
			} else {
				m->m_pkthdr.snd_tag = NULL;
			}
#endif
			error = (*ifp->if_output)(ifp, m,
			    (const struct sockaddr *)gw, ro);
#ifdef RATELIMIT
			/* check for route change */
			if (error == EAGAIN)
				in_pcboutput_eagain(inp);
#endif
		} else
			m_freem(m);
	}

	if (error == 0)
		IPSTAT_INC(ips_fragmented);

done:
	if (ro == &iproute)
		RO_RTFREE(ro);
	else if (rte == NULL)
		/*
		 * If the caller supplied a route but somehow the reference
		 * to it has been released need to prevent the caller
		 * calling RTFREE on it again.
		 */
		ro->ro_rt = NULL;
	NET_EPOCH_EXIT(et);
	return (error);
 bad:
	m_freem(m);
	goto done;
}

/*
 * Create a chain of fragments which fit the given mtu. m_frag points to the
 * mbuf to be fragmented; on return it points to the chain with the fragments.
 * Return 0 if no error. If error, m_frag may contain a partially built
 * chain of fragments that should be freed by the caller.
 *
 * if_hwassist_flags is the hw offload capabilities (see if_data.ifi_hwassist)
 */
int
ip_fragment(struct ip *ip, struct mbuf **m_frag, int mtu,
    u_long if_hwassist_flags)
{
	int error = 0;
	int hlen = ip->ip_hl << 2;
	int len = (mtu - hlen) & ~7;	/* size of payload in each fragment */
	int off;
	struct mbuf *m0 = *m_frag;	/* the original packet		*/
	int firstlen;
	struct mbuf **mnext;
	int nfrags;
	uint16_t ip_len, ip_off;

	ip_len = ntohs(ip->ip_len);
	ip_off = ntohs(ip->ip_off);

	if (ip_off & IP_DF) {	/* Fragmentation not allowed */
		IPSTAT_INC(ips_cantfrag);
		return EMSGSIZE;
	}

	/*
	 * Must be able to put at least 8 bytes per fragment.
	 */
	if (len < 8)
		return EMSGSIZE;

	/*
	 * If the interface will not calculate checksums on
	 * fragmented packets, then do it here.
	 */
	if (m0->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
		in_delayed_cksum(m0);
		m0->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}
#ifdef SCTP
	if (m0->m_pkthdr.csum_flags & CSUM_SCTP) {
		sctp_delayed_cksum(m0, hlen);
		m0->m_pkthdr.csum_flags &= ~CSUM_SCTP;
	}
#endif
	if (len > PAGE_SIZE) {
		/*
		 * Fragment large datagrams such that each segment
		 * contains a multiple of PAGE_SIZE amount of data,
		 * plus headers. This enables a receiver to perform
		 * page-flipping zero-copy optimizations.
		 *
		 * XXX When does this help given that sender and receiver
		 * could have different page sizes, and also mtu could
		 * be less than the receiver's page size ?
		 */
		int newlen;

		off = MIN(mtu, m0->m_pkthdr.len);

		/*
		 * firstlen (off - hlen) must be aligned on an
		 * 8-byte boundary
		 */
		if (off < hlen)
			goto smart_frag_failure;
		off = ((off - hlen) & ~7) + hlen;
		newlen = (~PAGE_MASK) & mtu;
		if ((newlen + sizeof (struct ip)) > mtu) {
			/* we failed, go back the default */
smart_frag_failure:
			newlen = len;
			off = hlen + len;
		}
		len = newlen;

	} else {
		off = hlen + len;
	}

	firstlen = off - hlen;
	mnext = &m0->m_nextpkt;		/* pointer to next packet */

	/*
	 * Loop through length of segment after first fragment,
	 * make new header and copy data of each part and link onto chain.
	 * Here, m0 is the original packet, m is the fragment being created.
	 * The fragments are linked off the m_nextpkt of the original
	 * packet, which after processing serves as the first fragment.
	 */
	for (nfrags = 1; off < ip_len; off += len, nfrags++) {
		struct ip *mhip;	/* ip header on the fragment */
		struct mbuf *m;
		int mhlen = sizeof (struct ip);

		m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m == NULL) {
			error = ENOBUFS;
			IPSTAT_INC(ips_odropped);
			goto done;
		}
		/*
		 * Make sure the complete packet header gets copied
		 * from the originating mbuf to the newly created
		 * mbuf. This also ensures that existing firewall
		 * classification(s), VLAN tags and so on get copied
		 * to the resulting fragmented packet(s):
		 */
		if (m_dup_pkthdr(m, m0, M_NOWAIT) == 0) {
			m_free(m);
			error = ENOBUFS;
			IPSTAT_INC(ips_odropped);
			goto done;
		}
		/*
		 * In the first mbuf, leave room for the link header, then
		 * copy the original IP header including options. The payload
		 * goes into an additional mbuf chain returned by m_copym().
		 */
		m->m_data += max_linkhdr;
		mhip = mtod(m, struct ip *);
		*mhip = *ip;
		if (hlen > sizeof (struct ip)) {
			mhlen = ip_optcopy(ip, mhip) + sizeof (struct ip);
			mhip->ip_v = IPVERSION;
			mhip->ip_hl = mhlen >> 2;
		}
		m->m_len = mhlen;
		/* XXX do we need to add ip_off below ? */
		mhip->ip_off = ((off - hlen) >> 3) + ip_off;
		if (off + len >= ip_len)
			len = ip_len - off;
		else
			mhip->ip_off |= IP_MF;
		mhip->ip_len = htons((u_short)(len + mhlen));
		m->m_next = m_copym(m0, off, len, M_NOWAIT);
		if (m->m_next == NULL) {	/* copy failed */
			m_free(m);
			error = ENOBUFS;	/* ??? */
			IPSTAT_INC(ips_odropped);
			goto done;
		}
		m->m_pkthdr.len = mhlen + len;
#ifdef MAC
		mac_netinet_fragment(m0, m);
#endif
		mhip->ip_off = htons(mhip->ip_off);
		mhip->ip_sum = 0;
		if (m->m_pkthdr.csum_flags & CSUM_IP & ~if_hwassist_flags) {
			mhip->ip_sum = in_cksum(m, mhlen);
			m->m_pkthdr.csum_flags &= ~CSUM_IP;
		}
		*mnext = m;
		mnext = &m->m_nextpkt;
	}
	IPSTAT_ADD(ips_ofragments, nfrags);

	/*
	 * Update first fragment by trimming what's been copied out
	 * and updating header.
	 */
	m_adj(m0, hlen + firstlen - ip_len);
	m0->m_pkthdr.len = hlen + firstlen;
	ip->ip_len = htons((u_short)m0->m_pkthdr.len);
	ip->ip_off = htons(ip_off | IP_MF);
	ip->ip_sum = 0;
	if (m0->m_pkthdr.csum_flags & CSUM_IP & ~if_hwassist_flags) {
		ip->ip_sum = in_cksum(m0, hlen);
		m0->m_pkthdr.csum_flags &= ~CSUM_IP;
	}

done:
	*m_frag = m0;
	return error;
}

void
in_delayed_cksum(struct mbuf *m)
{
	struct ip *ip;
	struct udphdr *uh;
	uint16_t cklen, csum, offset;

	ip = mtod(m, struct ip *);
	offset = ip->ip_hl << 2 ;

	if (m->m_pkthdr.csum_flags & CSUM_UDP) {
		/* if udp header is not in the first mbuf copy udplen */
		if (offset + sizeof(struct udphdr) > m->m_len) {
			m_copydata(m, offset + offsetof(struct udphdr,
			    uh_ulen), sizeof(cklen), (caddr_t)&cklen);
			cklen = ntohs(cklen);
		} else {
			uh = (struct udphdr *)mtodo(m, offset);
			cklen = ntohs(uh->uh_ulen);
		}
		csum = in_cksum_skip(m, cklen + offset, offset);
		if (csum == 0)
			csum = 0xffff;
	} else {
		cklen = ntohs(ip->ip_len);
		csum = in_cksum_skip(m, cklen, offset);
	}
	offset += m->m_pkthdr.csum_data;	/* checksum offset */

	if (offset + sizeof(csum) > m->m_len)
		m_copyback(m, offset, sizeof(csum), (caddr_t)&csum);
	else
		*(u_short *)mtodo(m, offset) = csum;
}

/*
 * IP socket option processing.
 */
int
ip_ctloutput(struct socket *so, struct sockopt *sopt)
{
	struct	inpcb *inp = sotoinpcb(so);
	int	error, optval;
#ifdef	RSS
	uint32_t rss_bucket;
	int retval;
#endif

	error = optval = 0;
	if (sopt->sopt_level != IPPROTO_IP) {
		error = EINVAL;

		if (sopt->sopt_level == SOL_SOCKET &&
		    sopt->sopt_dir == SOPT_SET) {
			switch (sopt->sopt_name) {
			case SO_REUSEADDR:
				INP_WLOCK(inp);
				if ((so->so_options & SO_REUSEADDR) != 0)
					inp->inp_flags2 |= INP_REUSEADDR;
				else
					inp->inp_flags2 &= ~INP_REUSEADDR;
				INP_WUNLOCK(inp);
				error = 0;
				break;
			case SO_REUSEPORT:
				INP_WLOCK(inp);
				if ((so->so_options & SO_REUSEPORT) != 0)
					inp->inp_flags2 |= INP_REUSEPORT;
				else
					inp->inp_flags2 &= ~INP_REUSEPORT;
				INP_WUNLOCK(inp);
				error = 0;
				break;
			case SO_REUSEPORT_LB:
				INP_WLOCK(inp);
				if ((so->so_options & SO_REUSEPORT_LB) != 0)
					inp->inp_flags2 |= INP_REUSEPORT_LB;
				else
					inp->inp_flags2 &= ~INP_REUSEPORT_LB;
				INP_WUNLOCK(inp);
				error = 0;
				break;
			case SO_SETFIB:
				INP_WLOCK(inp);
				inp->inp_inc.inc_fibnum = so->so_fibnum;
				INP_WUNLOCK(inp);
				error = 0;
				break;
			case SO_MAX_PACING_RATE:
#ifdef RATELIMIT
				INP_WLOCK(inp);
				inp->inp_flags2 |= INP_RATE_LIMIT_CHANGED;
				INP_WUNLOCK(inp);
				error = 0;
#else
				error = EOPNOTSUPP;
#endif
				break;
			default:
				break;
			}
		}
		return (error);
	}

	switch (sopt->sopt_dir) {
	case SOPT_SET:
		switch (sopt->sopt_name) {
		case IP_OPTIONS:
#ifdef notyet
		case IP_RETOPTS:
#endif
		{
			struct mbuf *m;
			if (sopt->sopt_valsize > MLEN) {
				error = EMSGSIZE;
				break;
			}
			m = m_get(sopt->sopt_td ? M_WAITOK : M_NOWAIT, MT_DATA);
			if (m == NULL) {
				error = ENOBUFS;
				break;
			}
			m->m_len = sopt->sopt_valsize;
			error = sooptcopyin(sopt, mtod(m, char *), m->m_len,
					    m->m_len);
			if (error) {
				m_free(m);
				break;
			}
			INP_WLOCK(inp);
			error = ip_pcbopts(inp, sopt->sopt_name, m);
			INP_WUNLOCK(inp);
			return (error);
		}

		case IP_BINDANY:
			if (sopt->sopt_td != NULL) {
				error = priv_check(sopt->sopt_td,
				    PRIV_NETINET_BINDANY);
				if (error)
					break;
			}
			/* FALLTHROUGH */
		case IP_BINDMULTI:
#ifdef	RSS
		case IP_RSS_LISTEN_BUCKET:
#endif
		case IP_TOS:
		case IP_TTL:
		case IP_MINTTL:
		case IP_RECVOPTS:
		case IP_RECVRETOPTS:
		case IP_ORIGDSTADDR:
		case IP_RECVDSTADDR:
		case IP_RECVTTL:
		case IP_RECVIF:
		case IP_ONESBCAST:
		case IP_DONTFRAG:
		case IP_RECVTOS:
		case IP_RECVFLOWID:
#ifdef	RSS
		case IP_RECVRSSBUCKETID:
#endif
			error = sooptcopyin(sopt, &optval, sizeof optval,
					    sizeof optval);
			if (error)
				break;

			switch (sopt->sopt_name) {
			case IP_TOS:
				inp->inp_ip_tos = optval;
				break;

			case IP_TTL:
				inp->inp_ip_ttl = optval;
				break;

			case IP_MINTTL:
				if (optval >= 0 && optval <= MAXTTL)
					inp->inp_ip_minttl = optval;
				else
					error = EINVAL;
				break;

#define	OPTSET(bit) do {						\
	INP_WLOCK(inp);							\
	if (optval)							\
		inp->inp_flags |= bit;					\
	else								\
		inp->inp_flags &= ~bit;					\
	INP_WUNLOCK(inp);						\
} while (0)

#define	OPTSET2(bit, val) do {						\
	INP_WLOCK(inp);							\
	if (val)							\
		inp->inp_flags2 |= bit;					\
	else								\
		inp->inp_flags2 &= ~bit;				\
	INP_WUNLOCK(inp);						\
} while (0)

			case IP_RECVOPTS:
				OPTSET(INP_RECVOPTS);
				break;

			case IP_RECVRETOPTS:
				OPTSET(INP_RECVRETOPTS);
				break;

			case IP_RECVDSTADDR:
				OPTSET(INP_RECVDSTADDR);
				break;

			case IP_ORIGDSTADDR:
				OPTSET2(INP_ORIGDSTADDR, optval);
				break;

			case IP_RECVTTL:
				OPTSET(INP_RECVTTL);
				break;

			case IP_RECVIF:
				OPTSET(INP_RECVIF);
				break;

			case IP_ONESBCAST:
				OPTSET(INP_ONESBCAST);
				break;
			case IP_DONTFRAG:
				OPTSET(INP_DONTFRAG);
				break;
			case IP_BINDANY:
				OPTSET(INP_BINDANY);
				break;
			case IP_RECVTOS:
				OPTSET(INP_RECVTOS);
				break;
			case IP_BINDMULTI:
				OPTSET2(INP_BINDMULTI, optval);
				break;
			case IP_RECVFLOWID:
				OPTSET2(INP_RECVFLOWID, optval);
				break;
#ifdef	RSS
			case IP_RSS_LISTEN_BUCKET:
				if ((optval >= 0) &&
				    (optval < rss_getnumbuckets())) {
					inp->inp_rss_listen_bucket = optval;
					OPTSET2(INP_RSS_BUCKET_SET, 1);
				} else {
					error = EINVAL;
				}
				break;
			case IP_RECVRSSBUCKETID:
				OPTSET2(INP_RECVRSSBUCKETID, optval);
				break;
#endif
			}
			break;
#undef OPTSET
#undef OPTSET2

		/*
		 * Multicast socket options are processed by the in_mcast
		 * module.
		 */
		case IP_MULTICAST_IF:
		case IP_MULTICAST_VIF:
		case IP_MULTICAST_TTL:
		case IP_MULTICAST_LOOP:
		case IP_ADD_MEMBERSHIP:
		case IP_DROP_MEMBERSHIP:
		case IP_ADD_SOURCE_MEMBERSHIP:
		case IP_DROP_SOURCE_MEMBERSHIP:
		case IP_BLOCK_SOURCE:
		case IP_UNBLOCK_SOURCE:
		case IP_MSFILTER:
		case MCAST_JOIN_GROUP:
		case MCAST_LEAVE_GROUP:
		case MCAST_JOIN_SOURCE_GROUP:
		case MCAST_LEAVE_SOURCE_GROUP:
		case MCAST_BLOCK_SOURCE:
		case MCAST_UNBLOCK_SOURCE:
			error = inp_setmoptions(inp, sopt);
			break;

		case IP_PORTRANGE:
			error = sooptcopyin(sopt, &optval, sizeof optval,
					    sizeof optval);
			if (error)
				break;

			INP_WLOCK(inp);
			switch (optval) {
			case IP_PORTRANGE_DEFAULT:
				inp->inp_flags &= ~(INP_LOWPORT);
				inp->inp_flags &= ~(INP_HIGHPORT);
				break;

			case IP_PORTRANGE_HIGH:
				inp->inp_flags &= ~(INP_LOWPORT);
				inp->inp_flags |= INP_HIGHPORT;
				break;

			case IP_PORTRANGE_LOW:
				inp->inp_flags &= ~(INP_HIGHPORT);
				inp->inp_flags |= INP_LOWPORT;
				break;

			default:
				error = EINVAL;
				break;
			}
			INP_WUNLOCK(inp);
			break;

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
		case IP_IPSEC_POLICY:
			if (IPSEC_ENABLED(ipv4)) {
				error = IPSEC_PCBCTL(ipv4, inp, sopt);
				break;
			}
			/* FALLTHROUGH */
#endif /* IPSEC */

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	case SOPT_GET:
		switch (sopt->sopt_name) {
		case IP_OPTIONS:
		case IP_RETOPTS:
			INP_RLOCK(inp);
			if (inp->inp_options) {
				struct mbuf *options;

				options = m_copym(inp->inp_options, 0,
				    M_COPYALL, M_NOWAIT);
				INP_RUNLOCK(inp);
				if (options != NULL) {
					error = sooptcopyout(sopt,
							     mtod(options, char *),
							     options->m_len);
					m_freem(options);
				} else
					error = ENOMEM;
			} else {
				INP_RUNLOCK(inp);
				sopt->sopt_valsize = 0;
			}
			break;

		case IP_TOS:
		case IP_TTL:
		case IP_MINTTL:
		case IP_RECVOPTS:
		case IP_RECVRETOPTS:
		case IP_ORIGDSTADDR:
		case IP_RECVDSTADDR:
		case IP_RECVTTL:
		case IP_RECVIF:
		case IP_PORTRANGE:
		case IP_ONESBCAST:
		case IP_DONTFRAG:
		case IP_BINDANY:
		case IP_RECVTOS:
		case IP_BINDMULTI:
		case IP_FLOWID:
		case IP_FLOWTYPE:
		case IP_RECVFLOWID:
#ifdef	RSS
		case IP_RSSBUCKETID:
		case IP_RECVRSSBUCKETID:
#endif
			switch (sopt->sopt_name) {

			case IP_TOS:
				optval = inp->inp_ip_tos;
				break;

			case IP_TTL:
				optval = inp->inp_ip_ttl;
				break;

			case IP_MINTTL:
				optval = inp->inp_ip_minttl;
				break;

#define	OPTBIT(bit)	(inp->inp_flags & bit ? 1 : 0)
#define	OPTBIT2(bit)	(inp->inp_flags2 & bit ? 1 : 0)

			case IP_RECVOPTS:
				optval = OPTBIT(INP_RECVOPTS);
				break;

			case IP_RECVRETOPTS:
				optval = OPTBIT(INP_RECVRETOPTS);
				break;

			case IP_RECVDSTADDR:
				optval = OPTBIT(INP_RECVDSTADDR);
				break;

			case IP_ORIGDSTADDR:
				optval = OPTBIT2(INP_ORIGDSTADDR);
				break;

			case IP_RECVTTL:
				optval = OPTBIT(INP_RECVTTL);
				break;

			case IP_RECVIF:
				optval = OPTBIT(INP_RECVIF);
				break;

			case IP_PORTRANGE:
				if (inp->inp_flags & INP_HIGHPORT)
					optval = IP_PORTRANGE_HIGH;
				else if (inp->inp_flags & INP_LOWPORT)
					optval = IP_PORTRANGE_LOW;
				else
					optval = 0;
				break;

			case IP_ONESBCAST:
				optval = OPTBIT(INP_ONESBCAST);
				break;
			case IP_DONTFRAG:
				optval = OPTBIT(INP_DONTFRAG);
				break;
			case IP_BINDANY:
				optval = OPTBIT(INP_BINDANY);
				break;
			case IP_RECVTOS:
				optval = OPTBIT(INP_RECVTOS);
				break;
			case IP_FLOWID:
				optval = inp->inp_flowid;
				break;
			case IP_FLOWTYPE:
				optval = inp->inp_flowtype;
				break;
			case IP_RECVFLOWID:
				optval = OPTBIT2(INP_RECVFLOWID);
				break;
#ifdef	RSS
			case IP_RSSBUCKETID:
				retval = rss_hash2bucket(inp->inp_flowid,
				    inp->inp_flowtype,
				    &rss_bucket);
				if (retval == 0)
					optval = rss_bucket;
				else
					error = EINVAL;
				break;
			case IP_RECVRSSBUCKETID:
				optval = OPTBIT2(INP_RECVRSSBUCKETID);
				break;
#endif
			case IP_BINDMULTI:
				optval = OPTBIT2(INP_BINDMULTI);
				break;
			}
			error = sooptcopyout(sopt, &optval, sizeof optval);
			break;

		/*
		 * Multicast socket options are processed by the in_mcast
		 * module.
		 */
		case IP_MULTICAST_IF:
		case IP_MULTICAST_VIF:
		case IP_MULTICAST_TTL:
		case IP_MULTICAST_LOOP:
		case IP_MSFILTER:
			error = inp_getmoptions(inp, sopt);
			break;

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
		case IP_IPSEC_POLICY:
			if (IPSEC_ENABLED(ipv4)) {
				error = IPSEC_PCBCTL(ipv4, inp, sopt);
				break;
			}
			/* FALLTHROUGH */
#endif /* IPSEC */

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;
	}
	return (error);
}

/*
 * Routine called from ip_output() to loop back a copy of an IP multicast
 * packet to the input queue of a specified interface.  Note that this
 * calls the output routine of the loopback "driver", but with an interface
 * pointer that might NOT be a loopback interface -- evil, but easier than
 * replicating that code here.
 */
static void
ip_mloopback(struct ifnet *ifp, const struct mbuf *m, int hlen)
{
	struct ip *ip;
	struct mbuf *copym;

	/*
	 * Make a deep copy of the packet because we're going to
	 * modify the pack in order to generate checksums.
	 */
	copym = m_dup(m, M_NOWAIT);
	if (copym != NULL && (!M_WRITABLE(copym) || copym->m_len < hlen))
		copym = m_pullup(copym, hlen);
	if (copym != NULL) {
		/* If needed, compute the checksum and mark it as valid. */
		if (copym->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
			in_delayed_cksum(copym);
			copym->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
			copym->m_pkthdr.csum_flags |=
			    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
			copym->m_pkthdr.csum_data = 0xffff;
		}
		/*
		 * We don't bother to fragment if the IP length is greater
		 * than the interface's MTU.  Can this possibly matter?
		 */
		ip = mtod(copym, struct ip *);
		ip->ip_sum = 0;
		ip->ip_sum = in_cksum(copym, hlen);
		if_simloop(ifp, copym, AF_INET, 0);
	}
}

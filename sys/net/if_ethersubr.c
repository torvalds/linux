/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1989, 1993
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
 *	@(#)if_ethersubr.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_netgraph.h"
#include "opt_mbuf_profiling.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/uuid.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/if_llc.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if_bridgevar.h>
#include <net/if_vlan_var.h>
#include <net/if_llatbl.h>
#include <net/pfil.h>
#include <net/rss_config.h>
#include <net/vnet.h>

#include <netpfil/pf/pf_mtag.h>

#if defined(INET) || defined(INET6)
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip_carp.h>
#include <netinet/ip_var.h>
#endif
#ifdef INET6
#include <netinet6/nd6.h>
#endif
#include <security/mac/mac_framework.h>

#ifdef CTASSERT
CTASSERT(sizeof (struct ether_header) == ETHER_ADDR_LEN * 2 + 2);
CTASSERT(sizeof (struct ether_addr) == ETHER_ADDR_LEN);
#endif

VNET_DEFINE(pfil_head_t, link_pfil_head);	/* Packet filter hooks */

/* netgraph node hooks for ng_ether(4) */
void	(*ng_ether_input_p)(struct ifnet *ifp, struct mbuf **mp);
void	(*ng_ether_input_orphan_p)(struct ifnet *ifp, struct mbuf *m);
int	(*ng_ether_output_p)(struct ifnet *ifp, struct mbuf **mp);
void	(*ng_ether_attach_p)(struct ifnet *ifp);
void	(*ng_ether_detach_p)(struct ifnet *ifp);

void	(*vlan_input_p)(struct ifnet *, struct mbuf *);

/* if_bridge(4) support */
void	(*bridge_dn_p)(struct mbuf *, struct ifnet *);

/* if_lagg(4) support */
struct mbuf *(*lagg_input_p)(struct ifnet *, struct mbuf *); 

static const u_char etherbroadcastaddr[ETHER_ADDR_LEN] =
			{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static	int ether_resolvemulti(struct ifnet *, struct sockaddr **,
		struct sockaddr *);
#ifdef VIMAGE
static	void ether_reassign(struct ifnet *, struct vnet *, char *);
#endif
static	int ether_requestencap(struct ifnet *, struct if_encap_req *);


#define senderr(e) do { error = (e); goto bad;} while (0)

static void
update_mbuf_csumflags(struct mbuf *src, struct mbuf *dst)
{
	int csum_flags = 0;

	if (src->m_pkthdr.csum_flags & CSUM_IP)
		csum_flags |= (CSUM_IP_CHECKED|CSUM_IP_VALID);
	if (src->m_pkthdr.csum_flags & CSUM_DELAY_DATA)
		csum_flags |= (CSUM_DATA_VALID|CSUM_PSEUDO_HDR);
	if (src->m_pkthdr.csum_flags & CSUM_SCTP)
		csum_flags |= CSUM_SCTP_VALID;
	dst->m_pkthdr.csum_flags |= csum_flags;
	if (csum_flags & CSUM_DATA_VALID)
		dst->m_pkthdr.csum_data = 0xffff;
}

/*
 * Handle link-layer encapsulation requests.
 */
static int
ether_requestencap(struct ifnet *ifp, struct if_encap_req *req)
{
	struct ether_header *eh;
	struct arphdr *ah;
	uint16_t etype;
	const u_char *lladdr;

	if (req->rtype != IFENCAP_LL)
		return (EOPNOTSUPP);

	if (req->bufsize < ETHER_HDR_LEN)
		return (ENOMEM);

	eh = (struct ether_header *)req->buf;
	lladdr = req->lladdr;
	req->lladdr_off = 0;

	switch (req->family) {
	case AF_INET:
		etype = htons(ETHERTYPE_IP);
		break;
	case AF_INET6:
		etype = htons(ETHERTYPE_IPV6);
		break;
	case AF_ARP:
		ah = (struct arphdr *)req->hdata;
		ah->ar_hrd = htons(ARPHRD_ETHER);

		switch(ntohs(ah->ar_op)) {
		case ARPOP_REVREQUEST:
		case ARPOP_REVREPLY:
			etype = htons(ETHERTYPE_REVARP);
			break;
		case ARPOP_REQUEST:
		case ARPOP_REPLY:
		default:
			etype = htons(ETHERTYPE_ARP);
			break;
		}

		if (req->flags & IFENCAP_FLAG_BROADCAST)
			lladdr = ifp->if_broadcastaddr;
		break;
	default:
		return (EAFNOSUPPORT);
	}

	memcpy(&eh->ether_type, &etype, sizeof(eh->ether_type));
	memcpy(eh->ether_dhost, lladdr, ETHER_ADDR_LEN);
	memcpy(eh->ether_shost, IF_LLADDR(ifp), ETHER_ADDR_LEN);
	req->bufsize = sizeof(struct ether_header);

	return (0);
}


static int
ether_resolve_addr(struct ifnet *ifp, struct mbuf *m,
	const struct sockaddr *dst, struct route *ro, u_char *phdr,
	uint32_t *pflags, struct llentry **plle)
{
	struct ether_header *eh;
	uint32_t lleflags = 0;
	int error = 0;
#if defined(INET) || defined(INET6)
	uint16_t etype;
#endif

	if (plle)
		*plle = NULL;
	eh = (struct ether_header *)phdr;

	switch (dst->sa_family) {
#ifdef INET
	case AF_INET:
		if ((m->m_flags & (M_BCAST | M_MCAST)) == 0)
			error = arpresolve(ifp, 0, m, dst, phdr, &lleflags,
			    plle);
		else {
			if (m->m_flags & M_BCAST)
				memcpy(eh->ether_dhost, ifp->if_broadcastaddr,
				    ETHER_ADDR_LEN);
			else {
				const struct in_addr *a;
				a = &(((const struct sockaddr_in *)dst)->sin_addr);
				ETHER_MAP_IP_MULTICAST(a, eh->ether_dhost);
			}
			etype = htons(ETHERTYPE_IP);
			memcpy(&eh->ether_type, &etype, sizeof(etype));
			memcpy(eh->ether_shost, IF_LLADDR(ifp), ETHER_ADDR_LEN);
		}
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if ((m->m_flags & M_MCAST) == 0)
			error = nd6_resolve(ifp, 0, m, dst, phdr, &lleflags,
			    plle);
		else {
			const struct in6_addr *a6;
			a6 = &(((const struct sockaddr_in6 *)dst)->sin6_addr);
			ETHER_MAP_IPV6_MULTICAST(a6, eh->ether_dhost);
			etype = htons(ETHERTYPE_IPV6);
			memcpy(&eh->ether_type, &etype, sizeof(etype));
			memcpy(eh->ether_shost, IF_LLADDR(ifp), ETHER_ADDR_LEN);
		}
		break;
#endif
	default:
		if_printf(ifp, "can't handle af%d\n", dst->sa_family);
		if (m != NULL)
			m_freem(m);
		return (EAFNOSUPPORT);
	}

	if (error == EHOSTDOWN) {
		if (ro != NULL && (ro->ro_flags & RT_HAS_GW) != 0)
			error = EHOSTUNREACH;
	}

	if (error != 0)
		return (error);

	*pflags = RT_MAY_LOOP;
	if (lleflags & LLE_IFADDR)
		*pflags |= RT_L2_ME;

	return (0);
}

/*
 * Ethernet output routine.
 * Encapsulate a packet of type family for the local net.
 * Use trailer local net encapsulation if enough data in first
 * packet leaves a multiple of 512 bytes of data in remainder.
 */
int
ether_output(struct ifnet *ifp, struct mbuf *m,
	const struct sockaddr *dst, struct route *ro)
{
	int error = 0;
	char linkhdr[ETHER_HDR_LEN], *phdr;
	struct ether_header *eh;
	struct pf_mtag *t;
	int loop_copy = 1;
	int hlen;	/* link layer header length */
	uint32_t pflags;
	struct llentry *lle = NULL;
	int addref = 0;

	phdr = NULL;
	pflags = 0;
	if (ro != NULL) {
		/* XXX BPF uses ro_prepend */
		if (ro->ro_prepend != NULL) {
			phdr = ro->ro_prepend;
			hlen = ro->ro_plen;
		} else if (!(m->m_flags & (M_BCAST | M_MCAST))) {
			if ((ro->ro_flags & RT_LLE_CACHE) != 0) {
				lle = ro->ro_lle;
				if (lle != NULL &&
				    (lle->la_flags & LLE_VALID) == 0) {
					LLE_FREE(lle);
					lle = NULL;	/* redundant */
					ro->ro_lle = NULL;
				}
				if (lle == NULL) {
					/* if we lookup, keep cache */
					addref = 1;
				} else
					/*
					 * Notify LLE code that
					 * the entry was used
					 * by datapath.
					 */
					llentry_mark_used(lle);
			}
			if (lle != NULL) {
				phdr = lle->r_linkdata;
				hlen = lle->r_hdrlen;
				pflags = lle->r_flags;
			}
		}
	}

#ifdef MAC
	error = mac_ifnet_check_transmit(ifp, m);
	if (error)
		senderr(error);
#endif

	M_PROFILE(m);
	if (ifp->if_flags & IFF_MONITOR)
		senderr(ENETDOWN);
	if (!((ifp->if_flags & IFF_UP) &&
	    (ifp->if_drv_flags & IFF_DRV_RUNNING)))
		senderr(ENETDOWN);

	if (phdr == NULL) {
		/* No prepend data supplied. Try to calculate ourselves. */
		phdr = linkhdr;
		hlen = ETHER_HDR_LEN;
		error = ether_resolve_addr(ifp, m, dst, ro, phdr, &pflags,
		    addref ? &lle : NULL);
		if (addref && lle != NULL)
			ro->ro_lle = lle;
		if (error != 0)
			return (error == EWOULDBLOCK ? 0 : error);
	}

	if ((pflags & RT_L2_ME) != 0) {
		update_mbuf_csumflags(m, m);
		return (if_simloop(ifp, m, dst->sa_family, 0));
	}
	loop_copy = pflags & RT_MAY_LOOP;

	/*
	 * Add local net header.  If no space in first mbuf,
	 * allocate another.
	 *
	 * Note that we do prepend regardless of RT_HAS_HEADER flag.
	 * This is done because BPF code shifts m_data pointer
	 * to the end of ethernet header prior to calling if_output().
	 */
	M_PREPEND(m, hlen, M_NOWAIT);
	if (m == NULL)
		senderr(ENOBUFS);
	if ((pflags & RT_HAS_HEADER) == 0) {
		eh = mtod(m, struct ether_header *);
		memcpy(eh, phdr, hlen);
	}

	/*
	 * If a simplex interface, and the packet is being sent to our
	 * Ethernet address or a broadcast address, loopback a copy.
	 * XXX To make a simplex device behave exactly like a duplex
	 * device, we should copy in the case of sending to our own
	 * ethernet address (thus letting the original actually appear
	 * on the wire). However, we don't do that here for security
	 * reasons and compatibility with the original behavior.
	 */
	if ((m->m_flags & M_BCAST) && loop_copy && (ifp->if_flags & IFF_SIMPLEX) &&
	    ((t = pf_find_mtag(m)) == NULL || !t->routed)) {
		struct mbuf *n;

		/*
		 * Because if_simloop() modifies the packet, we need a
		 * writable copy through m_dup() instead of a readonly
		 * one as m_copy[m] would give us. The alternative would
		 * be to modify if_simloop() to handle the readonly mbuf,
		 * but performancewise it is mostly equivalent (trading
		 * extra data copying vs. extra locking).
		 *
		 * XXX This is a local workaround.  A number of less
		 * often used kernel parts suffer from the same bug.
		 * See PR kern/105943 for a proposed general solution.
		 */
		if ((n = m_dup(m, M_NOWAIT)) != NULL) {
			update_mbuf_csumflags(m, n);
			(void)if_simloop(ifp, n, dst->sa_family, hlen);
		} else
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
	}

       /*
	* Bridges require special output handling.
	*/
	if (ifp->if_bridge) {
		BRIDGE_OUTPUT(ifp, m, error);
		return (error);
	}

#if defined(INET) || defined(INET6)
	if (ifp->if_carp &&
	    (error = (*carp_output_p)(ifp, m, dst)))
		goto bad;
#endif

	/* Handle ng_ether(4) processing, if any */
	if (ifp->if_l2com != NULL) {
		KASSERT(ng_ether_output_p != NULL,
		    ("ng_ether_output_p is NULL"));
		if ((error = (*ng_ether_output_p)(ifp, &m)) != 0) {
bad:			if (m != NULL)
				m_freem(m);
			return (error);
		}
		if (m == NULL)
			return (0);
	}

	/* Continue with link-layer output */
	return ether_output_frame(ifp, m);
}

static bool
ether_set_pcp(struct mbuf **mp, struct ifnet *ifp, uint8_t pcp)
{
	struct ether_header *eh;

	eh = mtod(*mp, struct ether_header *);
	if (ntohs(eh->ether_type) == ETHERTYPE_VLAN ||
	    ether_8021q_frame(mp, ifp, ifp, 0, pcp))
		return (true);
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	return (false);
}

/*
 * Ethernet link layer output routine to send a raw frame to the device.
 *
 * This assumes that the 14 byte Ethernet header is present and contiguous
 * in the first mbuf (if BRIDGE'ing).
 */
int
ether_output_frame(struct ifnet *ifp, struct mbuf *m)
{
	uint8_t pcp;

	pcp = ifp->if_pcp;
	if (pcp != IFNET_PCP_NONE && ifp->if_type != IFT_L2VLAN &&
	    !ether_set_pcp(&m, ifp, pcp))
		return (0);

	if (PFIL_HOOKED_OUT(V_link_pfil_head))
		switch (pfil_run_hooks(V_link_pfil_head, &m, ifp, PFIL_OUT,
		    NULL)) {
		case PFIL_DROPPED:
			return (EACCES);
		case PFIL_CONSUMED:
			return (0);
		}

#ifdef EXPERIMENTAL
#if defined(INET6) && defined(INET)
	/* draft-ietf-6man-ipv6only-flag */
	/* Catch ETHERTYPE_IP, and ETHERTYPE_[REV]ARP if we are v6-only. */
	if ((ND_IFINFO(ifp)->flags & ND6_IFF_IPV6_ONLY_MASK) != 0) {
		struct ether_header *eh;

		eh = mtod(m, struct ether_header *);
		switch (ntohs(eh->ether_type)) {
		case ETHERTYPE_IP:
		case ETHERTYPE_ARP:
		case ETHERTYPE_REVARP:
			m_freem(m);
			return (EAFNOSUPPORT);
			/* NOTREACHED */
			break;
		};
	}
#endif
#endif

	/*
	 * Queue message on interface, update output statistics if
	 * successful, and start output if interface not yet active.
	 */
	return ((ifp->if_transmit)(ifp, m));
}

/*
 * Process a received Ethernet packet; the packet is in the
 * mbuf chain m with the ethernet header at the front.
 */
static void
ether_input_internal(struct ifnet *ifp, struct mbuf *m)
{
	struct ether_header *eh;
	u_short etype;

	if ((ifp->if_flags & IFF_UP) == 0) {
		m_freem(m);
		return;
	}
#ifdef DIAGNOSTIC
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		if_printf(ifp, "discard frame at !IFF_DRV_RUNNING\n");
		m_freem(m);
		return;
	}
#endif
	if (m->m_len < ETHER_HDR_LEN) {
		/* XXX maybe should pullup? */
		if_printf(ifp, "discard frame w/o leading ethernet "
				"header (len %u pkt len %u)\n",
				m->m_len, m->m_pkthdr.len);
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		m_freem(m);
		return;
	}
	eh = mtod(m, struct ether_header *);
	etype = ntohs(eh->ether_type);
	random_harvest_queue_ether(m, sizeof(*m));

#ifdef EXPERIMENTAL
#if defined(INET6) && defined(INET)
	/* draft-ietf-6man-ipv6only-flag */
	/* Catch ETHERTYPE_IP, and ETHERTYPE_[REV]ARP if we are v6-only. */
	if ((ND_IFINFO(ifp)->flags & ND6_IFF_IPV6_ONLY_MASK) != 0) {

		switch (etype) {
		case ETHERTYPE_IP:
		case ETHERTYPE_ARP:
		case ETHERTYPE_REVARP:
			m_freem(m);
			return;
			/* NOTREACHED */
			break;
		};
	}
#endif
#endif

	CURVNET_SET_QUIET(ifp->if_vnet);

	if (ETHER_IS_MULTICAST(eh->ether_dhost)) {
		if (ETHER_IS_BROADCAST(eh->ether_dhost))
			m->m_flags |= M_BCAST;
		else
			m->m_flags |= M_MCAST;
		if_inc_counter(ifp, IFCOUNTER_IMCASTS, 1);
	}

#ifdef MAC
	/*
	 * Tag the mbuf with an appropriate MAC label before any other
	 * consumers can get to it.
	 */
	mac_ifnet_create_mbuf(ifp, m);
#endif

	/*
	 * Give bpf a chance at the packet.
	 */
	ETHER_BPF_MTAP(ifp, m);

	/*
	 * If the CRC is still on the packet, trim it off. We do this once
	 * and once only in case we are re-entered. Nothing else on the
	 * Ethernet receive path expects to see the FCS.
	 */
	if (m->m_flags & M_HASFCS) {
		m_adj(m, -ETHER_CRC_LEN);
		m->m_flags &= ~M_HASFCS;
	}

	if (!(ifp->if_capenable & IFCAP_HWSTATS))
		if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);

	/* Allow monitor mode to claim this frame, after stats are updated. */
	if (ifp->if_flags & IFF_MONITOR) {
		m_freem(m);
		CURVNET_RESTORE();
		return;
	}

	/* Handle input from a lagg(4) port */
	if (ifp->if_type == IFT_IEEE8023ADLAG) {
		KASSERT(lagg_input_p != NULL,
		    ("%s: if_lagg not loaded!", __func__));
		m = (*lagg_input_p)(ifp, m);
		if (m != NULL)
			ifp = m->m_pkthdr.rcvif;
		else {
			CURVNET_RESTORE();
			return;
		}
	}

	/*
	 * If the hardware did not process an 802.1Q tag, do this now,
	 * to allow 802.1P priority frames to be passed to the main input
	 * path correctly.
	 * TODO: Deal with Q-in-Q frames, but not arbitrary nesting levels.
	 */
	if ((m->m_flags & M_VLANTAG) == 0 && etype == ETHERTYPE_VLAN) {
		struct ether_vlan_header *evl;

		if (m->m_len < sizeof(*evl) &&
		    (m = m_pullup(m, sizeof(*evl))) == NULL) {
#ifdef DIAGNOSTIC
			if_printf(ifp, "cannot pullup VLAN header\n");
#endif
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			CURVNET_RESTORE();
			return;
		}

		evl = mtod(m, struct ether_vlan_header *);
		m->m_pkthdr.ether_vtag = ntohs(evl->evl_tag);
		m->m_flags |= M_VLANTAG;

		bcopy((char *)evl, (char *)evl + ETHER_VLAN_ENCAP_LEN,
		    ETHER_HDR_LEN - ETHER_TYPE_LEN);
		m_adj(m, ETHER_VLAN_ENCAP_LEN);
		eh = mtod(m, struct ether_header *);
	}

	M_SETFIB(m, ifp->if_fib);

	/* Allow ng_ether(4) to claim this frame. */
	if (ifp->if_l2com != NULL) {
		KASSERT(ng_ether_input_p != NULL,
		    ("%s: ng_ether_input_p is NULL", __func__));
		m->m_flags &= ~M_PROMISC;
		(*ng_ether_input_p)(ifp, &m);
		if (m == NULL) {
			CURVNET_RESTORE();
			return;
		}
		eh = mtod(m, struct ether_header *);
	}

	/*
	 * Allow if_bridge(4) to claim this frame.
	 * The BRIDGE_INPUT() macro will update ifp if the bridge changed it
	 * and the frame should be delivered locally.
	 */
	if (ifp->if_bridge != NULL) {
		m->m_flags &= ~M_PROMISC;
		BRIDGE_INPUT(ifp, m);
		if (m == NULL) {
			CURVNET_RESTORE();
			return;
		}
		eh = mtod(m, struct ether_header *);
	}

#if defined(INET) || defined(INET6)
	/*
	 * Clear M_PROMISC on frame so that carp(4) will see it when the
	 * mbuf flows up to Layer 3.
	 * FreeBSD's implementation of carp(4) uses the inprotosw
	 * to dispatch IPPROTO_CARP. carp(4) also allocates its own
	 * Ethernet addresses of the form 00:00:5e:00:01:xx, which
	 * is outside the scope of the M_PROMISC test below.
	 * TODO: Maintain a hash table of ethernet addresses other than
	 * ether_dhost which may be active on this ifp.
	 */
	if (ifp->if_carp && (*carp_forus_p)(ifp, eh->ether_dhost)) {
		m->m_flags &= ~M_PROMISC;
	} else
#endif
	{
		/*
		 * If the frame received was not for our MAC address, set the
		 * M_PROMISC flag on the mbuf chain. The frame may need to
		 * be seen by the rest of the Ethernet input path in case of
		 * re-entry (e.g. bridge, vlan, netgraph) but should not be
		 * seen by upper protocol layers.
		 */
		if (!ETHER_IS_MULTICAST(eh->ether_dhost) &&
		    bcmp(IF_LLADDR(ifp), eh->ether_dhost, ETHER_ADDR_LEN) != 0)
			m->m_flags |= M_PROMISC;
	}

	ether_demux(ifp, m);
	CURVNET_RESTORE();
}

/*
 * Ethernet input dispatch; by default, direct dispatch here regardless of
 * global configuration.  However, if RSS is enabled, hook up RSS affinity
 * so that when deferred or hybrid dispatch is enabled, we can redistribute
 * load based on RSS.
 *
 * XXXRW: Would be nice if the ifnet passed up a flag indicating whether or
 * not it had already done work distribution via multi-queue.  Then we could
 * direct dispatch in the event load balancing was already complete and
 * handle the case of interfaces with different capabilities better.
 *
 * XXXRW: Sort of want an M_DISTRIBUTED flag to avoid multiple distributions
 * at multiple layers?
 *
 * XXXRW: For now, enable all this only if RSS is compiled in, although it
 * works fine without RSS.  Need to characterise the performance overhead
 * of the detour through the netisr code in the event the result is always
 * direct dispatch.
 */
static void
ether_nh_input(struct mbuf *m)
{

	M_ASSERTPKTHDR(m);
	KASSERT(m->m_pkthdr.rcvif != NULL,
	    ("%s: NULL interface pointer", __func__));
	ether_input_internal(m->m_pkthdr.rcvif, m);
}

static struct netisr_handler	ether_nh = {
	.nh_name = "ether",
	.nh_handler = ether_nh_input,
	.nh_proto = NETISR_ETHER,
#ifdef RSS
	.nh_policy = NETISR_POLICY_CPU,
	.nh_dispatch = NETISR_DISPATCH_DIRECT,
	.nh_m2cpuid = rss_m2cpuid,
#else
	.nh_policy = NETISR_POLICY_SOURCE,
	.nh_dispatch = NETISR_DISPATCH_DIRECT,
#endif
};

static void
ether_init(__unused void *arg)
{

	netisr_register(&ether_nh);
}
SYSINIT(ether, SI_SUB_INIT_IF, SI_ORDER_ANY, ether_init, NULL);

static void
vnet_ether_init(__unused void *arg)
{
	struct pfil_head_args args;

	args.pa_version = PFIL_VERSION;
	args.pa_flags = PFIL_IN | PFIL_OUT;
	args.pa_type = PFIL_TYPE_ETHERNET;
	args.pa_headname = PFIL_ETHER_NAME;
	V_link_pfil_head = pfil_head_register(&args);

#ifdef VIMAGE
	netisr_register_vnet(&ether_nh);
#endif
}
VNET_SYSINIT(vnet_ether_init, SI_SUB_PROTO_IF, SI_ORDER_ANY,
    vnet_ether_init, NULL);
 
#ifdef VIMAGE
static void
vnet_ether_pfil_destroy(__unused void *arg)
{

	pfil_head_unregister(V_link_pfil_head);
}
VNET_SYSUNINIT(vnet_ether_pfil_uninit, SI_SUB_PROTO_PFIL, SI_ORDER_ANY,
    vnet_ether_pfil_destroy, NULL);

static void
vnet_ether_destroy(__unused void *arg)
{

	netisr_unregister_vnet(&ether_nh);
}
VNET_SYSUNINIT(vnet_ether_uninit, SI_SUB_PROTO_IF, SI_ORDER_ANY,
    vnet_ether_destroy, NULL);
#endif



static void
ether_input(struct ifnet *ifp, struct mbuf *m)
{

	struct mbuf *mn;

	/*
	 * The drivers are allowed to pass in a chain of packets linked with
	 * m_nextpkt. We split them up into separate packets here and pass
	 * them up. This allows the drivers to amortize the receive lock.
	 */
	while (m) {
		mn = m->m_nextpkt;
		m->m_nextpkt = NULL;

		/*
		 * We will rely on rcvif being set properly in the deferred context,
		 * so assert it is correct here.
		 */
		KASSERT(m->m_pkthdr.rcvif == ifp, ("%s: ifnet mismatch m %p "
		    "rcvif %p ifp %p", __func__, m, m->m_pkthdr.rcvif, ifp));
		CURVNET_SET_QUIET(ifp->if_vnet);
		netisr_dispatch(NETISR_ETHER, m);
		CURVNET_RESTORE();
		m = mn;
	}
}

/*
 * Upper layer processing for a received Ethernet packet.
 */
void
ether_demux(struct ifnet *ifp, struct mbuf *m)
{
	struct ether_header *eh;
	int i, isr;
	u_short ether_type;

	KASSERT(ifp != NULL, ("%s: NULL interface pointer", __func__));

	/* Do not grab PROMISC frames in case we are re-entered. */
	if (PFIL_HOOKED_IN(V_link_pfil_head) && !(m->m_flags & M_PROMISC)) {
		i = pfil_run_hooks(V_link_pfil_head, &m, ifp, PFIL_IN, NULL);
		if (i != 0 || m == NULL)
			return;
	}

	eh = mtod(m, struct ether_header *);
	ether_type = ntohs(eh->ether_type);

	/*
	 * If this frame has a VLAN tag other than 0, call vlan_input()
	 * if its module is loaded. Otherwise, drop.
	 */
	if ((m->m_flags & M_VLANTAG) &&
	    EVL_VLANOFTAG(m->m_pkthdr.ether_vtag) != 0) {
		if (ifp->if_vlantrunk == NULL) {
			if_inc_counter(ifp, IFCOUNTER_NOPROTO, 1);
			m_freem(m);
			return;
		}
		KASSERT(vlan_input_p != NULL,("%s: VLAN not loaded!",
		    __func__));
		/* Clear before possibly re-entering ether_input(). */
		m->m_flags &= ~M_PROMISC;
		(*vlan_input_p)(ifp, m);
		return;
	}

	/*
	 * Pass promiscuously received frames to the upper layer if the user
	 * requested this by setting IFF_PPROMISC. Otherwise, drop them.
	 */
	if ((ifp->if_flags & IFF_PPROMISC) == 0 && (m->m_flags & M_PROMISC)) {
		m_freem(m);
		return;
	}

	/*
	 * Reset layer specific mbuf flags to avoid confusing upper layers.
	 * Strip off Ethernet header.
	 */
	m->m_flags &= ~M_VLANTAG;
	m_clrprotoflags(m);
	m_adj(m, ETHER_HDR_LEN);

	/*
	 * Dispatch frame to upper layer.
	 */
	switch (ether_type) {
#ifdef INET
	case ETHERTYPE_IP:
		isr = NETISR_IP;
		break;

	case ETHERTYPE_ARP:
		if (ifp->if_flags & IFF_NOARP) {
			/* Discard packet if ARP is disabled on interface */
			m_freem(m);
			return;
		}
		isr = NETISR_ARP;
		break;
#endif
#ifdef INET6
	case ETHERTYPE_IPV6:
		isr = NETISR_IPV6;
		break;
#endif
	default:
		goto discard;
	}
	netisr_dispatch(isr, m);
	return;

discard:
	/*
	 * Packet is to be discarded.  If netgraph is present,
	 * hand the packet to it for last chance processing;
	 * otherwise dispose of it.
	 */
	if (ifp->if_l2com != NULL) {
		KASSERT(ng_ether_input_orphan_p != NULL,
		    ("ng_ether_input_orphan_p is NULL"));
		/*
		 * Put back the ethernet header so netgraph has a
		 * consistent view of inbound packets.
		 */
		M_PREPEND(m, ETHER_HDR_LEN, M_NOWAIT);
		(*ng_ether_input_orphan_p)(ifp, m);
		return;
	}
	m_freem(m);
}

/*
 * Convert Ethernet address to printable (loggable) representation.
 * This routine is for compatibility; it's better to just use
 *
 *	printf("%6D", <pointer to address>, ":");
 *
 * since there's no static buffer involved.
 */
char *
ether_sprintf(const u_char *ap)
{
	static char etherbuf[18];
	snprintf(etherbuf, sizeof (etherbuf), "%6D", ap, ":");
	return (etherbuf);
}

/*
 * Perform common duties while attaching to interface list
 */
void
ether_ifattach(struct ifnet *ifp, const u_int8_t *lla)
{
	int i;
	struct ifaddr *ifa;
	struct sockaddr_dl *sdl;

	ifp->if_addrlen = ETHER_ADDR_LEN;
	ifp->if_hdrlen = ETHER_HDR_LEN;
	if_attach(ifp);
	ifp->if_mtu = ETHERMTU;
	ifp->if_output = ether_output;
	ifp->if_input = ether_input;
	ifp->if_resolvemulti = ether_resolvemulti;
	ifp->if_requestencap = ether_requestencap;
#ifdef VIMAGE
	ifp->if_reassign = ether_reassign;
#endif
	if (ifp->if_baudrate == 0)
		ifp->if_baudrate = IF_Mbps(10);		/* just a default */
	ifp->if_broadcastaddr = etherbroadcastaddr;

	ifa = ifp->if_addr;
	KASSERT(ifa != NULL, ("%s: no lladdr!\n", __func__));
	sdl = (struct sockaddr_dl *)ifa->ifa_addr;
	sdl->sdl_type = IFT_ETHER;
	sdl->sdl_alen = ifp->if_addrlen;
	bcopy(lla, LLADDR(sdl), ifp->if_addrlen);

	if (ifp->if_hw_addr != NULL)
		bcopy(lla, ifp->if_hw_addr, ifp->if_addrlen);

	bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN);
	if (ng_ether_attach_p != NULL)
		(*ng_ether_attach_p)(ifp);

	/* Announce Ethernet MAC address if non-zero. */
	for (i = 0; i < ifp->if_addrlen; i++)
		if (lla[i] != 0)
			break; 
	if (i != ifp->if_addrlen)
		if_printf(ifp, "Ethernet address: %6D\n", lla, ":");

	uuid_ether_add(LLADDR(sdl));

	/* Add necessary bits are setup; announce it now. */
	EVENTHANDLER_INVOKE(ether_ifattach_event, ifp);
	if (IS_DEFAULT_VNET(curvnet))
		devctl_notify("ETHERNET", ifp->if_xname, "IFATTACH", NULL);
}

/*
 * Perform common duties while detaching an Ethernet interface
 */
void
ether_ifdetach(struct ifnet *ifp)
{
	struct sockaddr_dl *sdl;

	sdl = (struct sockaddr_dl *)(ifp->if_addr->ifa_addr);
	uuid_ether_del(LLADDR(sdl));

	if (ifp->if_l2com != NULL) {
		KASSERT(ng_ether_detach_p != NULL,
		    ("ng_ether_detach_p is NULL"));
		(*ng_ether_detach_p)(ifp);
	}

	bpfdetach(ifp);
	if_detach(ifp);
}

#ifdef VIMAGE
void
ether_reassign(struct ifnet *ifp, struct vnet *new_vnet, char *unused __unused)
{

	if (ifp->if_l2com != NULL) {
		KASSERT(ng_ether_detach_p != NULL,
		    ("ng_ether_detach_p is NULL"));
		(*ng_ether_detach_p)(ifp);
	}

	if (ng_ether_attach_p != NULL) {
		CURVNET_SET_QUIET(new_vnet);
		(*ng_ether_attach_p)(ifp);
		CURVNET_RESTORE();
	}
}
#endif

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_ETHER, ether, CTLFLAG_RW, 0, "Ethernet");

#if 0
/*
 * This is for reference.  We have a table-driven version
 * of the little-endian crc32 generator, which is faster
 * than the double-loop.
 */
uint32_t
ether_crc32_le(const uint8_t *buf, size_t len)
{
	size_t i;
	uint32_t crc;
	int bit;
	uint8_t data;

	crc = 0xffffffff;	/* initial value */

	for (i = 0; i < len; i++) {
		for (data = *buf++, bit = 0; bit < 8; bit++, data >>= 1) {
			carry = (crc ^ data) & 1;
			crc >>= 1;
			if (carry)
				crc = (crc ^ ETHER_CRC_POLY_LE);
		}
	}

	return (crc);
}
#else
uint32_t
ether_crc32_le(const uint8_t *buf, size_t len)
{
	static const uint32_t crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
	};
	size_t i;
	uint32_t crc;

	crc = 0xffffffff;	/* initial value */

	for (i = 0; i < len; i++) {
		crc ^= buf[i];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
	}

	return (crc);
}
#endif

uint32_t
ether_crc32_be(const uint8_t *buf, size_t len)
{
	size_t i;
	uint32_t crc, carry;
	int bit;
	uint8_t data;

	crc = 0xffffffff;	/* initial value */

	for (i = 0; i < len; i++) {
		for (data = *buf++, bit = 0; bit < 8; bit++, data >>= 1) {
			carry = ((crc & 0x80000000) ? 1 : 0) ^ (data & 0x01);
			crc <<= 1;
			if (carry)
				crc = (crc ^ ETHER_CRC_POLY_BE) | carry;
		}
	}

	return (crc);
}

int
ether_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ifaddr *ifa = (struct ifaddr *) data;
	struct ifreq *ifr = (struct ifreq *) data;
	int error = 0;

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;

		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			ifp->if_init(ifp->if_softc);	/* before arpwhohas */
			arp_ifinit(ifp, ifa);
			break;
#endif
		default:
			ifp->if_init(ifp->if_softc);
			break;
		}
		break;

	case SIOCGIFADDR:
		bcopy(IF_LLADDR(ifp), &ifr->ifr_addr.sa_data[0],
		    ETHER_ADDR_LEN);
		break;

	case SIOCSIFMTU:
		/*
		 * Set the interface MTU.
		 */
		if (ifr->ifr_mtu > ETHERMTU) {
			error = EINVAL;
		} else {
			ifp->if_mtu = ifr->ifr_mtu;
		}
		break;

	case SIOCSLANPCP:
		error = priv_check(curthread, PRIV_NET_SETLANPCP);
		if (error != 0)
			break;
		if (ifr->ifr_lan_pcp > 7 &&
		    ifr->ifr_lan_pcp != IFNET_PCP_NONE) {
			error = EINVAL;
		} else {
			ifp->if_pcp = ifr->ifr_lan_pcp;
			/* broadcast event about PCP change */
			EVENTHANDLER_INVOKE(ifnet_event, ifp, IFNET_EVENT_PCP);
		}
		break;

	case SIOCGLANPCP:
		ifr->ifr_lan_pcp = ifp->if_pcp;
		break;

	default:
		error = EINVAL;			/* XXX netbsd has ENOTTY??? */
		break;
	}
	return (error);
}

static int
ether_resolvemulti(struct ifnet *ifp, struct sockaddr **llsa,
	struct sockaddr *sa)
{
	struct sockaddr_dl *sdl;
#ifdef INET
	struct sockaddr_in *sin;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif
	u_char *e_addr;

	switch(sa->sa_family) {
	case AF_LINK:
		/*
		 * No mapping needed. Just check that it's a valid MC address.
		 */
		sdl = (struct sockaddr_dl *)sa;
		e_addr = LLADDR(sdl);
		if (!ETHER_IS_MULTICAST(e_addr))
			return EADDRNOTAVAIL;
		*llsa = NULL;
		return 0;

#ifdef INET
	case AF_INET:
		sin = (struct sockaddr_in *)sa;
		if (!IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
			return EADDRNOTAVAIL;
		sdl = link_init_sdl(ifp, *llsa, IFT_ETHER);
		sdl->sdl_alen = ETHER_ADDR_LEN;
		e_addr = LLADDR(sdl);
		ETHER_MAP_IP_MULTICAST(&sin->sin_addr, e_addr);
		*llsa = (struct sockaddr *)sdl;
		return 0;
#endif
#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)sa;
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
			/*
			 * An IP6 address of 0 means listen to all
			 * of the Ethernet multicast address used for IP6.
			 * (This is used for multicast routers.)
			 */
			ifp->if_flags |= IFF_ALLMULTI;
			*llsa = NULL;
			return 0;
		}
		if (!IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			return EADDRNOTAVAIL;
		sdl = link_init_sdl(ifp, *llsa, IFT_ETHER);
		sdl->sdl_alen = ETHER_ADDR_LEN;
		e_addr = LLADDR(sdl);
		ETHER_MAP_IPV6_MULTICAST(&sin6->sin6_addr, e_addr);
		*llsa = (struct sockaddr *)sdl;
		return 0;
#endif

	default:
		/*
		 * Well, the text isn't quite right, but it's the name
		 * that counts...
		 */
		return EAFNOSUPPORT;
	}
}

static moduledata_t ether_mod = {
	.name = "ether",
};

void
ether_vlan_mtap(struct bpf_if *bp, struct mbuf *m, void *data, u_int dlen)
{
	struct ether_vlan_header vlan;
	struct mbuf mv, mb;

	KASSERT((m->m_flags & M_VLANTAG) != 0,
	    ("%s: vlan information not present", __func__));
	KASSERT(m->m_len >= sizeof(struct ether_header),
	    ("%s: mbuf not large enough for header", __func__));
	bcopy(mtod(m, char *), &vlan, sizeof(struct ether_header));
	vlan.evl_proto = vlan.evl_encap_proto;
	vlan.evl_encap_proto = htons(ETHERTYPE_VLAN);
	vlan.evl_tag = htons(m->m_pkthdr.ether_vtag);
	m->m_len -= sizeof(struct ether_header);
	m->m_data += sizeof(struct ether_header);
	/*
	 * If a data link has been supplied by the caller, then we will need to
	 * re-create a stack allocated mbuf chain with the following structure:
	 *
	 * (1) mbuf #1 will contain the supplied data link
	 * (2) mbuf #2 will contain the vlan header
	 * (3) mbuf #3 will contain the original mbuf's packet data
	 *
	 * Otherwise, submit the packet and vlan header via bpf_mtap2().
	 */
	if (data != NULL) {
		mv.m_next = m;
		mv.m_data = (caddr_t)&vlan;
		mv.m_len = sizeof(vlan);
		mb.m_next = &mv;
		mb.m_data = data;
		mb.m_len = dlen;
		bpf_mtap(bp, &mb);
	} else
		bpf_mtap2(bp, &vlan, sizeof(vlan), m);
	m->m_len += sizeof(struct ether_header);
	m->m_data -= sizeof(struct ether_header);
}

struct mbuf *
ether_vlanencap(struct mbuf *m, uint16_t tag)
{
	struct ether_vlan_header *evl;

	M_PREPEND(m, ETHER_VLAN_ENCAP_LEN, M_NOWAIT);
	if (m == NULL)
		return (NULL);
	/* M_PREPEND takes care of m_len, m_pkthdr.len for us */

	if (m->m_len < sizeof(*evl)) {
		m = m_pullup(m, sizeof(*evl));
		if (m == NULL)
			return (NULL);
	}

	/*
	 * Transform the Ethernet header into an Ethernet header
	 * with 802.1Q encapsulation.
	 */
	evl = mtod(m, struct ether_vlan_header *);
	bcopy((char *)evl + ETHER_VLAN_ENCAP_LEN,
	    (char *)evl, ETHER_HDR_LEN - ETHER_TYPE_LEN);
	evl->evl_encap_proto = htons(ETHERTYPE_VLAN);
	evl->evl_tag = htons(tag);
	return (m);
}

static SYSCTL_NODE(_net_link, IFT_L2VLAN, vlan, CTLFLAG_RW, 0,
    "IEEE 802.1Q VLAN");
static SYSCTL_NODE(_net_link_vlan, PF_LINK, link, CTLFLAG_RW, 0,
    "for consistency");

VNET_DEFINE_STATIC(int, soft_pad);
#define	V_soft_pad	VNET(soft_pad)
SYSCTL_INT(_net_link_vlan, OID_AUTO, soft_pad, CTLFLAG_RW | CTLFLAG_VNET,
    &VNET_NAME(soft_pad), 0,
    "pad short frames before tagging");

/*
 * For now, make preserving PCP via an mbuf tag optional, as it increases
 * per-packet memory allocations and frees.  In the future, it would be
 * preferable to reuse ether_vtag for this, or similar.
 */
int vlan_mtag_pcp = 0;
SYSCTL_INT(_net_link_vlan, OID_AUTO, mtag_pcp, CTLFLAG_RW,
    &vlan_mtag_pcp, 0,
    "Retain VLAN PCP information as packets are passed up the stack");

bool
ether_8021q_frame(struct mbuf **mp, struct ifnet *ife, struct ifnet *p,
    uint16_t vid, uint8_t pcp)
{
	struct m_tag *mtag;
	int n;
	uint16_t tag;
	static const char pad[8];	/* just zeros */

	/*
	 * Pad the frame to the minimum size allowed if told to.
	 * This option is in accord with IEEE Std 802.1Q, 2003 Ed.,
	 * paragraph C.4.4.3.b.  It can help to work around buggy
	 * bridges that violate paragraph C.4.4.3.a from the same
	 * document, i.e., fail to pad short frames after untagging.
	 * E.g., a tagged frame 66 bytes long (incl. FCS) is OK, but
	 * untagging it will produce a 62-byte frame, which is a runt
	 * and requires padding.  There are VLAN-enabled network
	 * devices that just discard such runts instead or mishandle
	 * them somehow.
	 */
	if (V_soft_pad && p->if_type == IFT_ETHER) {
		for (n = ETHERMIN + ETHER_HDR_LEN - (*mp)->m_pkthdr.len;
		     n > 0; n -= sizeof(pad)) {
			if (!m_append(*mp, min(n, sizeof(pad)), pad))
				break;
		}
		if (n > 0) {
			m_freem(*mp);
			*mp = NULL;
			if_printf(ife, "cannot pad short frame");
			return (false);
		}
	}

	/*
	 * If underlying interface can do VLAN tag insertion itself,
	 * just pass the packet along. However, we need some way to
	 * tell the interface where the packet came from so that it
	 * knows how to find the VLAN tag to use, so we attach a
	 * packet tag that holds it.
	 */
	if (vlan_mtag_pcp && (mtag = m_tag_locate(*mp, MTAG_8021Q,
	    MTAG_8021Q_PCP_OUT, NULL)) != NULL)
		tag = EVL_MAKETAG(vid, *(uint8_t *)(mtag + 1), 0);
	else
		tag = EVL_MAKETAG(vid, pcp, 0);
	if (p->if_capenable & IFCAP_VLAN_HWTAGGING) {
		(*mp)->m_pkthdr.ether_vtag = tag;
		(*mp)->m_flags |= M_VLANTAG;
	} else {
		*mp = ether_vlanencap(*mp, tag);
		if (*mp == NULL) {
			if_printf(ife, "unable to prepend 802.1Q header");
			return (false);
		}
	}
	return (true);
}

void
ether_fakeaddr(struct ether_addr *hwaddr)
{

	/*
	 * Generate a convenient locally administered address,
	 * 'bsd' + random 24 low-order bits.  'b' is 0x62, which has the locally
	 * assigned bit set, and the broadcast/multicast bit clear.
	 */
	arc4rand(hwaddr->octet, ETHER_ADDR_LEN, 1);
	hwaddr->octet[0] = 'b';
	hwaddr->octet[1] = 's';
	hwaddr->octet[2] = 'd';
}

DECLARE_MODULE(ether, ether_mod, SI_SUB_INIT_IF, SI_ORDER_ANY);
MODULE_VERSION(ether, 1);

/*	$OpenBSD: if_ethersubr.c,v 1.303 2025/07/07 02:28:50 jsg Exp $	*/
/*	$NetBSD: if_ethersubr.c,v 1.19 1996/05/07 02:40:30 thorpej Exp $	*/

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
 */

/*
%%% portions-copyright-nrl-95
Portions of this software are Copyright 1995-1998 by Randall Atkinson,
Ronald Lee, Daniel McDonald, Bao Phan, and Chris Winters. All Rights
Reserved. All rights under this copyright have been assigned to the US
Naval Research Laboratory (NRL). The NRL Copyright Notice and License
Agreement Version 1.1 (January 17, 1995) applies to these portions of the
software.
You should have received a copy of the license with this software. If you
didn't get a copy, you may request one from <license@ipv6.nrl.navy.mil>.
*/

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/smr.h>

#include <net/if.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include "vlan.h"
#if NVLAN > 0
#include <net/if_vlan_var.h>
#endif

#include "carp.h"
#if NCARP > 0
#include <netinet/ip_carp.h>
#endif

#include "pppoe.h"
#if NPPPOE > 0
#include <net/if_pppoe.h>
#endif

#include "bpe.h"
#if NBPE > 0
#include <net/if_bpe.h>
#endif

#ifdef INET6
#include <netinet6/nd6.h>
#endif

#ifdef PIPEX
#include <net/pipex.h>
#endif

#ifdef MPLS
#include <netmpls/mpls.h>
#endif /* MPLS */

#include "af_frame.h"
#if NAF_FRAME > 0
#include <net/frame.h>

static struct mbuf *
	ether_frm_input(struct ifnet *, struct mbuf *, uint64_t, uint16_t);
#endif

/* #define ETHERDEBUG 1 */
#ifdef ETHERDEBUG
int etherdebug = ETHERDEBUG;
#define DNPRINTF(level, fmt, args...)					\
	do {								\
		if (etherdebug >= level)				\
			printf("%s: " fmt "\n", __func__, ## args);	\
	} while (0)
#else
#define DNPRINTF(level, fmt, args...)					\
	do { } while (0)
#endif
#define DPRINTF(fmt, args...)	DNPRINTF(1, fmt, args)

u_int8_t etherbroadcastaddr[ETHER_ADDR_LEN] =
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
u_int8_t etheranyaddr[ETHER_ADDR_LEN] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
#define senderr(e) { error = (e); goto bad;}

int
ether_ioctl(struct ifnet *ifp, struct arpcom *arp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		break;

	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > ifp->if_hardmtu)
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_MULTICAST) {
			error = (cmd == SIOCADDMULTI) ?
			    ether_addmulti(ifr, arp) :
			    ether_delmulti(ifr, arp);
		} else
			error = ENOTTY;
		break;

	default:
		error = ENOTTY;
	}

	return (error);
}


void
ether_rtrequest(struct ifnet *ifp, int req, struct rtentry *rt)
{
	if (rt == NULL)
		return;

	switch (rt_key(rt)->sa_family) {
	case AF_INET:
		arp_rtrequest(ifp, req, rt);
		break;
#ifdef INET6
	case AF_INET6:
		nd6_rtrequest(ifp, req, rt);
		break;
#endif
	default:
		break;
	}
}

int
ether_resolve(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt, struct ether_header *eh)
{
	struct arpcom *ac = (struct arpcom *)ifp;
	sa_family_t af = dst->sa_family;
	int error = 0;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		senderr(ENETDOWN);

	KASSERT(rt != NULL || ISSET(m->m_flags, M_MCAST|M_BCAST) ||
		af == AF_UNSPEC || af == pseudo_AF_HDRCMPLT);

#ifdef DIAGNOSTIC
	if (ifp->if_rdomain != rtable_l2(m->m_pkthdr.ph_rtableid)) {
		printf("%s: trying to send packet on wrong domain. "
		    "if %d vs. mbuf %d\n", ifp->if_xname,
		    ifp->if_rdomain, rtable_l2(m->m_pkthdr.ph_rtableid));
	}
#endif

	switch (af) {
	case AF_INET:
		error = arpresolve(ifp, rt, m, dst, eh->ether_dhost);
		if (error)
			return (error);
		eh->ether_type = htons(ETHERTYPE_IP);

		/*
		 * If broadcasting on a simplex interface, loopback a copy.
		 * The checksum must be calculated in software.  Keep the
		 * condition in sync with in_ifcap_cksum().
		 */
		if (ISSET(m->m_flags, M_BCAST) &&
		    ISSET(ifp->if_flags, IFF_SIMPLEX) &&
		    !m->m_pkthdr.pf.routed) {
			struct mbuf *mcopy;

			/* XXX Should we input an unencrypted IPsec packet? */
			mcopy = m_copym(m, 0, M_COPYALL, M_NOWAIT);
			if (mcopy != NULL)
				if_input_local(ifp, mcopy, af, NULL);
		}
		break;
#ifdef INET6
	case AF_INET6:
		error = nd6_resolve(ifp, rt, m, dst, eh->ether_dhost);
		if (error)
			return (error);
		eh->ether_type = htons(ETHERTYPE_IPV6);
		break;
#endif
#ifdef MPLS
	case AF_MPLS:
		if (rt == NULL)
			senderr(EHOSTUNREACH);

		if (!ISSET(ifp->if_xflags, IFXF_MPLS))
			senderr(ENETUNREACH);

		dst = ISSET(rt->rt_flags, RTF_GATEWAY) ?
		    rt->rt_gateway : rt_key(rt);

		switch (dst->sa_family) {
		case AF_LINK:
			if (satosdl(dst)->sdl_alen < sizeof(eh->ether_dhost))
				senderr(EHOSTUNREACH);
			memcpy(eh->ether_dhost, LLADDR(satosdl(dst)),
			    sizeof(eh->ether_dhost));
			break;
#ifdef INET6
		case AF_INET6:
			error = nd6_resolve(ifp, rt, m, dst, eh->ether_dhost);
			if (error)
				return (error);
			break;
#endif
		case AF_INET:
			error = arpresolve(ifp, rt, m, dst, eh->ether_dhost);
			if (error)
				return (error);
			break;
		default:
			senderr(EHOSTUNREACH);
		}
		/* XXX handling for simplex devices in case of M/BCAST ?? */
		if (m->m_flags & (M_BCAST | M_MCAST))
			eh->ether_type = htons(ETHERTYPE_MPLS_MCAST);
		else
			eh->ether_type = htons(ETHERTYPE_MPLS);
		break;
#endif /* MPLS */
	case pseudo_AF_HDRCMPLT:
		/* take the whole header from the sa */
		memcpy(eh, dst->sa_data, sizeof(*eh));
		return (0);

	case AF_UNSPEC:
		/* take the dst and type from the sa, but get src below */
		memcpy(eh, dst->sa_data, sizeof(*eh));
		break;

	default:
		printf("%s: can't handle af%d\n", ifp->if_xname, af);
		senderr(EAFNOSUPPORT);
	}

	memcpy(eh->ether_shost, ac->ac_enaddr, sizeof(eh->ether_shost));

	return (0);

bad:
	m_freem(m);
	return (error);
}

struct mbuf*
ether_encap(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt, int *errorp)
{
	struct ether_header eh;
	int error;

	error = ether_resolve(ifp, m, dst, rt, &eh);
	switch (error) {
	case 0:
		break;
	case EAGAIN:
		error = 0;
	default:
		*errorp = error;
		return (NULL);
	}

	m = m_prepend(m, ETHER_ALIGN + sizeof(eh), M_DONTWAIT);
	if (m == NULL) {
		*errorp = ENOBUFS;
		return (NULL);
	}

	m_adj(m, ETHER_ALIGN);
	memcpy(mtod(m, struct ether_header *), &eh, sizeof(eh));

	return (m);
}

int
ether_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	int error;

	m = ether_encap(ifp, m, dst, rt, &error);
	if (m == NULL)
		return (error);

	return (if_enqueue(ifp, m));
}

/*
 * Process a received Ethernet packet.
 *
 * Ethernet input has several "phases" of filtering packets to
 * support virtual/pseudo interfaces before actual layer 3 protocol
 * handling.
 *
 * First phase:
 *
 * The first phase supports drivers that aggregate multiple Ethernet
 * ports into a single logical interface, ie, aggr(4) and trunk(4).
 * These drivers intercept packets by swapping out the if_input handler
 * on the "port" interfaces to steal the packets before they get here
 * to ether_input().
 */
void
ether_input(struct ifnet *ifp, struct mbuf *m, struct netstack *ns)
{
	struct ether_header *eh;
	void (*input)(struct ifnet *, struct mbuf *, struct netstack *);
	u_int16_t etype;
	struct arpcom *ac;
	const struct ether_brport *eb;
	unsigned int sdelim = 0;
	uint64_t dst, self;

	/* Drop short frames */
	if (m->m_len < ETHER_HDR_LEN)
		goto dropanyway;

	/*
	 * Second phase: service delimited packet filtering.
	 *
	 * Let vlan(4) and svlan(4) look at "service delimited"
	 * packets. If a virtual interface does not exist to take
	 * those packets, they're returned to ether_input() so a
	 * bridge can have a go at forwarding them.
	 */

	eh = mtod(m, struct ether_header *);
	dst = ether_addr_to_e64((struct ether_addr *)eh->ether_dhost);
	etype = ntohs(eh->ether_type);

	if (ISSET(m->m_flags, M_VLANTAG) ||
	    etype == ETHERTYPE_VLAN || etype == ETHERTYPE_QINQ) {
#if NVLAN > 0
		m = vlan_input(ifp, m, &sdelim, ns);
		if (m == NULL)
			return;
#else
		sdelim = 1;
#endif
	}

	/*
	 * Third phase: bridge processing.
	 *
	 * Give the packet to a bridge interface, ie, bridge(4),
	 * veb(4), or tpmr(4), if it is configured. A bridge
	 * may take the packet and forward it to another port, or it
	 * may return it here to ether_input() to support local
	 * delivery to this port.
	 */

	ac = (struct arpcom *)ifp;

	smr_read_enter();
	eb = SMR_PTR_GET(&ac->ac_brport);
	if (eb != NULL)
		eb->eb_port_take(eb->eb_port);
	smr_read_leave();
	if (eb != NULL) {
		m = (*eb->eb_input)(ifp, m, dst, eb->eb_port, ns);
		eb->eb_port_rele(eb->eb_port);
		if (m == NULL) {
			return;
		}
	}

	/*
	 * Fourth phase: drop service delimited packets.
	 *
	 * If the packet has a tag, and a bridge didn't want it,
	 * it's not for this port.
	 */

	if (sdelim)
		goto dropanyway;

	/*
	 * Fifth phase: destination address check.
	 *
	 * Is the packet specifically addressed to this port?
	 */

	eh = mtod(m, struct ether_header *);
	self = ether_addr_to_e64((struct ether_addr *)ac->ac_enaddr);
	if (dst != self) {
#if NCARP > 0
		/*
		 * If it's not for this port, it could be for carp(4).
		 */
		if (ifp->if_type != IFT_CARP &&
		    !SRPL_EMPTY_LOCKED(&ifp->if_carp)) {
			m = carp_input(ifp, m, dst, ns);
			if (m == NULL)
				return;

			eh = mtod(m, struct ether_header *);
		}
#endif

		/*
		 * If not, it must be multicast or broadcast to go further.
		 */
		if (!ETH64_IS_MULTICAST(dst))
			goto dropanyway;

		/*
		 * If this is not a simplex interface, drop the packet
		 * if it came from us.
		 */
		if ((ifp->if_flags & IFF_SIMPLEX) == 0) {
			uint64_t src = ether_addr_to_e64(
			    (struct ether_addr *)eh->ether_shost);
			if (self == src)
				goto dropanyway;
		}

		SET(m->m_flags, ETH64_IS_BROADCAST(dst) ? M_BCAST : M_MCAST);
		ifp->if_imcasts++;
	}

	/*
	 * Sixth phase: protocol demux.
	 *
	 * At this point it is known that the packet is destined
	 * for layer 3 protocol handling on the local port.
	 */
	etype = ntohs(eh->ether_type);

	switch (etype) {
	case ETHERTYPE_IP:
		input = ipv4_input;
		break;

	case ETHERTYPE_ARP:
		if (ifp->if_flags & IFF_NOARP)
			goto dropanyway;
		input = arpinput;
		break;

	case ETHERTYPE_REVARP:
		if (ifp->if_flags & IFF_NOARP)
			goto dropanyway;
		input = revarpinput;
		break;

#ifdef INET6
	/*
	 * Schedule IPv6 software interrupt for incoming IPv6 packet.
	 */
	case ETHERTYPE_IPV6:
		input = ipv6_input;
		break;
#endif /* INET6 */
#if NPPPOE > 0 || defined(PIPEX)
	case ETHERTYPE_PPPOEDISC:
	case ETHERTYPE_PPPOE:
		if (m->m_flags & (M_MCAST | M_BCAST))
			goto dropanyway;
#ifdef PIPEX
		if (pipex_enable) {
			struct pipex_session *session;

			if ((session = pipex_pppoe_lookup_session(m)) != NULL) {
				pipex_pppoe_input(m, session, ns);
				pipex_rele_session(session);
				return;
			}
		}
#endif
		if (etype == ETHERTYPE_PPPOEDISC) {
			if (mq_enqueue(&pppoediscinq, m) == 0)
				schednetisr(NETISR_PPPOE);
		} else {
			m = pppoe_vinput(ifp, m, ns);
			if (m != NULL && mq_enqueue(&pppoeinq, m) == 0)
				schednetisr(NETISR_PPPOE);
		}
		return;
#endif
#ifdef MPLS
	case ETHERTYPE_MPLS:
	case ETHERTYPE_MPLS_MCAST:
		input = mpls_input;
		break;
#endif
#if NBPE > 0
	case ETHERTYPE_PBB:
		bpe_input(ifp, m, ns);
		return;
#endif
	default:
#if NAF_FRAME > 0
		m = ether_frm_input(ifp, m, dst, etype);
#endif
		goto dropanyway;
	}

	m_adj(m, sizeof(*eh));
	(*input)(ifp, m, ns);
	return;
dropanyway:
	m_freem(m);
	return;
}

int
ether_brport_isset(struct ifnet *ifp)
{
	struct arpcom *ac = (struct arpcom *)ifp;

	KERNEL_ASSERT_LOCKED();
	if (SMR_PTR_GET_LOCKED(&ac->ac_brport) != NULL)
		return (EBUSY);

	return (0);
}

void
ether_brport_set(struct ifnet *ifp, const struct ether_brport *eb)
{
	struct arpcom *ac = (struct arpcom *)ifp;

	KERNEL_ASSERT_LOCKED();
	KASSERTMSG(SMR_PTR_GET_LOCKED(&ac->ac_brport) == NULL,
	    "%s setting an already set brport", ifp->if_xname);

	SMR_PTR_SET_LOCKED(&ac->ac_brport, eb);
}

void
ether_brport_clr(struct ifnet *ifp)
{
	struct arpcom *ac = (struct arpcom *)ifp;

	KERNEL_ASSERT_LOCKED();
	KASSERTMSG(SMR_PTR_GET_LOCKED(&ac->ac_brport) != NULL,
	    "%s clearing an already clear brport", ifp->if_xname);

	SMR_PTR_SET_LOCKED(&ac->ac_brport, NULL);
}

const struct ether_brport *
ether_brport_get(struct ifnet *ifp)
{
	struct arpcom *ac = (struct arpcom *)ifp;
	SMR_ASSERT_CRITICAL();
	return (SMR_PTR_GET(&ac->ac_brport));
}

const struct ether_brport *
ether_brport_get_locked(struct ifnet *ifp)
{
	struct arpcom *ac = (struct arpcom *)ifp;
	KERNEL_ASSERT_LOCKED();
	return (SMR_PTR_GET_LOCKED(&ac->ac_brport));
}

/*
 * Convert Ethernet address to printable (loggable) representation.
 */
static char digits[] = "0123456789abcdef";
char *
ether_sprintf(u_char *ap)
{
	int i;
	static char etherbuf[ETHER_ADDR_LEN * 3];
	char *cp = etherbuf;

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		*cp++ = digits[*ap >> 4];
		*cp++ = digits[*ap++ & 0xf];
		*cp++ = ':';
	}
	*--cp = 0;
	return (etherbuf);
}

/*
 * Generate a (hopefully) acceptable MAC address, if asked.
 */
void
ether_fakeaddr(struct ifnet *ifp)
{
	static int unit;
	int rng = arc4random();

	/* Non-multicast; locally administered address */
	((struct arpcom *)ifp)->ac_enaddr[0] = 0xfe;
	((struct arpcom *)ifp)->ac_enaddr[1] = 0xe1;
	((struct arpcom *)ifp)->ac_enaddr[2] = 0xba;
	((struct arpcom *)ifp)->ac_enaddr[3] = 0xd0 | (unit++ & 0xf);
	((struct arpcom *)ifp)->ac_enaddr[4] = rng;
	((struct arpcom *)ifp)->ac_enaddr[5] = rng >> 8;
}

/*
 * Perform common duties while attaching to interface list
 */
void
ether_ifattach(struct ifnet *ifp)
{
	struct arpcom *ac = (struct arpcom *)ifp;

	/*
	 * Any interface which provides a MAC address which is obviously
	 * invalid gets whacked, so that users will notice.
	 */
	if (ETHER_IS_MULTICAST(((struct arpcom *)ifp)->ac_enaddr))
		ether_fakeaddr(ifp);

	ifp->if_type = IFT_ETHER;
	ifp->if_addrlen = ETHER_ADDR_LEN;
	ifp->if_hdrlen = ETHER_HDR_LEN;
	ifp->if_mtu = ETHERMTU;
	ifp->if_input = ether_input;
	if (ifp->if_output == NULL)
		ifp->if_output = ether_output;
	ifp->if_rtrequest = ether_rtrequest;

	if (ifp->if_hardmtu == 0)
		ifp->if_hardmtu = ETHERMTU;

	if_alloc_sadl(ifp);
	memcpy(LLADDR(ifp->if_sadl), ac->ac_enaddr, ifp->if_addrlen);
	LIST_INIT(&ac->ac_multiaddrs);
#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_EN10MB, ETHER_HDR_LEN);
#endif
}

void
ether_ifdetach(struct ifnet *ifp)
{
	struct arpcom *ac = (struct arpcom *)ifp;
	struct ether_multi *enm;

	/* Undo pseudo-driver changes. */
	if_deactivate(ifp);

	while (!LIST_EMPTY(&ac->ac_multiaddrs)) {
		enm = LIST_FIRST(&ac->ac_multiaddrs);
		LIST_REMOVE(enm, enm_list);
		free(enm, M_IFMADDR, sizeof *enm);
	}
}

#if 0
/*
 * This is for reference.  We have table-driven versions of the
 * crc32 generators, which are faster than the double-loop.
 */
u_int32_t __pure
ether_crc32_le_update(u_int_32_t crc, const u_int8_t *buf, size_t len)
{
	u_int32_t c, carry;
	size_t i, j;

	for (i = 0; i < len; i++) {
		c = buf[i];
		for (j = 0; j < 8; j++) {
			carry = ((crc & 0x01) ? 1 : 0) ^ (c & 0x01);
			crc >>= 1;
			c >>= 1;
			if (carry)
				crc = (crc ^ ETHER_CRC_POLY_LE);
		}
	}

	return (crc);
}

u_int32_t __pure
ether_crc32_be_update(u_int_32_t crc, const u_int8_t *buf, size_t len)
{
	u_int32_t c, carry;
	size_t i, j;

	for (i = 0; i < len; i++) {
		c = buf[i];
		for (j = 0; j < 8; j++) {
			carry = ((crc & 0x80000000U) ? 1 : 0) ^ (c & 0x01);
			crc <<= 1;
			c >>= 1;
			if (carry)
				crc = (crc ^ ETHER_CRC_POLY_BE) | carry;
		}
	}

	return (crc);
}
#else
u_int32_t __pure
ether_crc32_le_update(u_int32_t crc, const u_int8_t *buf, size_t len)
{
	static const u_int32_t crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
	};
	size_t i;

	for (i = 0; i < len; i++) {
		crc ^= buf[i];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
	}

	return (crc);
}

u_int32_t __pure
ether_crc32_be_update(u_int32_t crc, const u_int8_t *buf, size_t len)
{
	static const u_int8_t rev[] = {
		0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
		0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf
	};
	static const u_int32_t crctab[] = {
		0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
		0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
		0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
		0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd
	};
	size_t i;
	u_int8_t data;

	for (i = 0; i < len; i++) {
		data = buf[i];
		crc = (crc << 4) ^ crctab[(crc >> 28) ^ rev[data & 0xf]];
		crc = (crc << 4) ^ crctab[(crc >> 28) ^ rev[data >> 4]];
	}

	return (crc);
}
#endif

u_int32_t
ether_crc32_le(const u_int8_t *buf, size_t len)
{
	return ether_crc32_le_update(0xffffffff, buf, len);
}

u_int32_t
ether_crc32_be(const u_int8_t *buf, size_t len)
{
	return ether_crc32_be_update(0xffffffff, buf, len);
}

u_char	ether_ipmulticast_min[ETHER_ADDR_LEN] =
    { 0x01, 0x00, 0x5e, 0x00, 0x00, 0x00 };
u_char	ether_ipmulticast_max[ETHER_ADDR_LEN] =
    { 0x01, 0x00, 0x5e, 0x7f, 0xff, 0xff };

#ifdef INET6
u_char	ether_ip6multicast_min[ETHER_ADDR_LEN] =
    { 0x33, 0x33, 0x00, 0x00, 0x00, 0x00 };
u_char	ether_ip6multicast_max[ETHER_ADDR_LEN] =
    { 0x33, 0x33, 0xff, 0xff, 0xff, 0xff };
#endif

/*
 * Convert a sockaddr into an Ethernet address or range of Ethernet
 * addresses.
 */
int
ether_multiaddr(struct sockaddr *sa, u_int8_t addrlo[ETHER_ADDR_LEN],
    u_int8_t addrhi[ETHER_ADDR_LEN])
{
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif /* INET6 */

	switch (sa->sa_family) {

	case AF_UNSPEC:
		memcpy(addrlo, sa->sa_data, ETHER_ADDR_LEN);
		memcpy(addrhi, addrlo, ETHER_ADDR_LEN);
		break;

	case AF_INET:
		sin = satosin(sa);
		if (sin->sin_addr.s_addr == INADDR_ANY) {
			/*
			 * An IP address of INADDR_ANY means listen to
			 * or stop listening to all of the Ethernet
			 * multicast addresses used for IP.
			 * (This is for the sake of IP multicast routers.)
			 */
			memcpy(addrlo, ether_ipmulticast_min, ETHER_ADDR_LEN);
			memcpy(addrhi, ether_ipmulticast_max, ETHER_ADDR_LEN);
		} else {
			ETHER_MAP_IP_MULTICAST(&sin->sin_addr, addrlo);
			memcpy(addrhi, addrlo, ETHER_ADDR_LEN);
		}
		break;
#ifdef INET6
	case AF_INET6:
		sin6 = satosin6(sa);
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
			/*
			 * An IP6 address of 0 means listen to or stop
			 * listening to all of the Ethernet multicast
			 * address used for IP6.
			 *
			 * (This might not be healthy, given IPv6's reliance on
			 * multicast for things like neighbor discovery.
			 * Perhaps initializing all-nodes, solicited nodes, and
			 * possibly all-routers for this interface afterwards
			 * is not a bad idea.)
			 */

			memcpy(addrlo, ether_ip6multicast_min, ETHER_ADDR_LEN);
			memcpy(addrhi, ether_ip6multicast_max, ETHER_ADDR_LEN);
		} else {
			ETHER_MAP_IPV6_MULTICAST(&sin6->sin6_addr, addrlo);
			memcpy(addrhi, addrlo, ETHER_ADDR_LEN);
		}
		break;
#endif

	default:
		return (EAFNOSUPPORT);
	}
	return (0);
}

/*
 * Add an Ethernet multicast address or range of addresses to the list for a
 * given interface.
 */
int
ether_addmulti(struct ifreq *ifr, struct arpcom *ac)
{
	struct ether_multi *enm;
	u_char addrlo[ETHER_ADDR_LEN];
	u_char addrhi[ETHER_ADDR_LEN];
	int s = splnet(), error;

	error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
	if (error != 0) {
		splx(s);
		return (error);
	}

	/*
	 * Verify that we have valid Ethernet multicast addresses.
	 */
	if ((addrlo[0] & 0x01) != 1 || (addrhi[0] & 0x01) != 1) {
		splx(s);
		return (EINVAL);
	}
	/*
	 * See if the address range is already in the list.
	 */
	ETHER_LOOKUP_MULTI(addrlo, addrhi, ac, enm);
	if (enm != NULL) {
		/*
		 * Found it; just increment the reference count.
		 */
		refcnt_take(&enm->enm_refcnt);
		splx(s);
		return (0);
	}
	/*
	 * New address or range; malloc a new multicast record
	 * and link it into the interface's multicast list.
	 */
	enm = malloc(sizeof(*enm), M_IFMADDR, M_NOWAIT);
	if (enm == NULL) {
		splx(s);
		return (ENOBUFS);
	}
	memcpy(enm->enm_addrlo, addrlo, ETHER_ADDR_LEN);
	memcpy(enm->enm_addrhi, addrhi, ETHER_ADDR_LEN);
	refcnt_init_trace(&enm->enm_refcnt, DT_REFCNT_IDX_ETHMULTI);
	LIST_INSERT_HEAD(&ac->ac_multiaddrs, enm, enm_list);
	ac->ac_multicnt++;
	if (memcmp(addrlo, addrhi, ETHER_ADDR_LEN) != 0)
		ac->ac_multirangecnt++;
	splx(s);
	/*
	 * Return ENETRESET to inform the driver that the list has changed
	 * and its reception filter should be adjusted accordingly.
	 */
	return (ENETRESET);
}

/*
 * Delete a multicast address record.
 */
int
ether_delmulti(struct ifreq *ifr, struct arpcom *ac)
{
	struct ether_multi *enm;
	u_char addrlo[ETHER_ADDR_LEN];
	u_char addrhi[ETHER_ADDR_LEN];
	int s = splnet(), error;

	error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
	if (error != 0) {
		splx(s);
		return (error);
	}

	/*
	 * Look up the address in our list.
	 */
	ETHER_LOOKUP_MULTI(addrlo, addrhi, ac, enm);
	if (enm == NULL) {
		splx(s);
		return (ENXIO);
	}
	if (refcnt_rele(&enm->enm_refcnt) == 0) {
		/*
		 * Still some claims to this record.
		 */
		splx(s);
		return (0);
	}
	/*
	 * No remaining claims to this record; unlink and free it.
	 */
	LIST_REMOVE(enm, enm_list);
	free(enm, M_IFMADDR, sizeof *enm);
	ac->ac_multicnt--;
	if (memcmp(addrlo, addrhi, ETHER_ADDR_LEN) != 0)
		ac->ac_multirangecnt--;
	splx(s);
	/*
	 * Return ENETRESET to inform the driver that the list has changed
	 * and its reception filter should be adjusted accordingly.
	 */
	return (ENETRESET);
}

uint64_t
ether_addr_to_e64(const struct ether_addr *ea)
{
	uint64_t e64 = 0;
	size_t i;

	for (i = 0; i < nitems(ea->ether_addr_octet); i++) {
		e64 <<= 8;
		e64 |= ea->ether_addr_octet[i];
	}

	return (e64);
}

void
ether_e64_to_addr(struct ether_addr *ea, uint64_t e64)
{
	size_t i = nitems(ea->ether_addr_octet);

	do {
		ea->ether_addr_octet[--i] = e64;
		e64 >>= 8;
	} while (i > 0);
}

/* Parse different TCP/IP protocol headers for a quick view inside an mbuf. */
void
ether_extract_headers(struct mbuf *m0, struct ether_extracted *ext)
{
	struct mbuf	*m;
	size_t		 hlen, iplen;
	int		 hoff;
	uint8_t		 ipproto;
	uint16_t	 ether_type;
	/* gcc 4.2.1 on sparc64 may create 32 bit loads on unaligned mbuf */
	union {
		u_char	hc_data;
#if _BYTE_ORDER == _LITTLE_ENDIAN
		struct {
			u_int	hl:4,	/* header length */
				v:4;	/* version */
		} hc_ip;
		struct {
			u_int	x2:4,	/* (unused) */
				off:4;	/* data offset */
		} hc_th;
#endif
#if _BYTE_ORDER == _BIG_ENDIAN
		struct {
			u_int	v:4,	/* version */
				hl:4;	/* header length */
		} hc_ip;
		struct {
			u_int	off:4,	/* data offset */
				x2:4;	/* (unused) */
		} hc_th;
#endif
	} hdrcpy;

	/* Return NULL if header was not recognized. */
	memset(ext, 0, sizeof(*ext));

	KASSERT(ISSET(m0->m_flags, M_PKTHDR));
	ext->paylen = m0->m_pkthdr.len;

	if (m0->m_len < sizeof(*ext->eh)) {
		DPRINTF("m_len %d, eh %zu", m0->m_len, sizeof(*ext->eh));
		return;
	}
	ext->eh = mtod(m0, struct ether_header *);
	hlen = sizeof(*ext->eh);
	if (ext->paylen < hlen) {
		DPRINTF("paylen %u, ehlen %zu", ext->paylen, hlen);
		ext->eh = NULL;
		return;
	}
	ext->paylen -= hlen;
	ether_type = ntohs(ext->eh->ether_type);

#if NVLAN > 0
	if (ether_type == ETHERTYPE_VLAN) {
		if (m0->m_len < sizeof(*ext->evh)) {
			DPRINTF("m_len %d, evh %zu",
			    m0->m_len, sizeof(*ext->evh));
			return;
		}
		ext->evh = mtod(m0, struct ether_vlan_header *);
		hlen = sizeof(*ext->evh);
		if (sizeof(*ext->eh) + ext->paylen < hlen) {
			DPRINTF("paylen %zu, evhlen %zu",
			    sizeof(*ext->eh) + ext->paylen, hlen);
			ext->evh = NULL;
			return;
		}
		ext->paylen = sizeof(*ext->eh) + ext->paylen - hlen;
		ether_type = ntohs(ext->evh->evl_proto);
	}
#endif

	switch (ether_type) {
	case ETHERTYPE_IP:
		m = m_getptr(m0, hlen, &hoff);
		if (m == NULL || m->m_len - hoff < sizeof(*ext->ip4)) {
			DPRINTF("m_len %d, hoff %d, ip4 %zu",
			    m ? m->m_len : -1, hoff, sizeof(*ext->ip4));
			return;
		}
		ext->ip4 = (struct ip *)(mtod(m, caddr_t) + hoff);

		memcpy(&hdrcpy.hc_data, ext->ip4, 1);
		hlen = hdrcpy.hc_ip.hl << 2;
		if (m->m_len - hoff < hlen) {
			DPRINTF("m_len %d, hoff %d, iphl %zu",
			    m ? m->m_len : -1, hoff, hlen);
			ext->ip4 = NULL;
			return;
		}
		if (ext->paylen < hlen) {
			DPRINTF("paylen %u, ip4hlen %zu", ext->paylen, hlen);
			ext->ip4 = NULL;
			return;
		}
		iplen = ntohs(ext->ip4->ip_len);
		if (ext->paylen < iplen) {
			DPRINTF("paylen %u, ip4len %zu", ext->paylen, iplen);
			ext->ip4 = NULL;
			return;
		}
		if (iplen < hlen) {
			DPRINTF("ip4len %zu, ip4hlen %zu", iplen, hlen);
			ext->ip4 = NULL;
			return;
		}
		ext->iplen = iplen;
		ext->iphlen = hlen;
		ext->paylen -= hlen;
		ipproto = ext->ip4->ip_p;

		if (ISSET(ntohs(ext->ip4->ip_off), IP_MF|IP_OFFMASK))
			return;
		break;
#ifdef INET6
	case ETHERTYPE_IPV6:
		m = m_getptr(m0, hlen, &hoff);
		if (m == NULL || m->m_len - hoff < sizeof(*ext->ip6)) {
			DPRINTF("m_len %d, hoff %d, ip6 %zu",
			    m ? m->m_len : -1, hoff, sizeof(*ext->ip6));
			return;
		}
		ext->ip6 = (struct ip6_hdr *)(mtod(m, caddr_t) + hoff);

		hlen = sizeof(*ext->ip6);
		if (ext->paylen < hlen) {
			DPRINTF("paylen %u, ip6hlen %zu", ext->paylen, hlen);
			ext->ip6 = NULL;
			return;
		}
		iplen = hlen + ntohs(ext->ip6->ip6_plen);
		if (ext->paylen < iplen) {
			DPRINTF("paylen %u, ip6len %zu", ext->paylen, iplen);
			ext->ip6 = NULL;
			return;
		}
		ext->iplen = iplen;
		ext->iphlen = hlen;
		ext->paylen -= hlen;
		ipproto = ext->ip6->ip6_nxt;
		break;
#endif
	default:
		return;
	}

	switch (ipproto) {
	case IPPROTO_TCP:
		m = m_getptr(m, hoff + hlen, &hoff);
		if (m == NULL || m->m_len - hoff < sizeof(*ext->tcp)) {
			DPRINTF("m_len %d, hoff %d, tcp %zu",
			    m ? m->m_len : -1, hoff, sizeof(*ext->tcp));
			return;
		}
		ext->tcp = (struct tcphdr *)(mtod(m, caddr_t) + hoff);

		memcpy(&hdrcpy.hc_data, &ext->tcp->th_flags - 1, 1);
		hlen = hdrcpy.hc_th.off << 2;
		if (m->m_len - hoff < hlen) {
			DPRINTF("m_len %d, hoff %d, thoff %zu",
			    m ? m->m_len : -1, hoff, hlen);
			ext->tcp = NULL;
			return;
		}
		if (ext->iplen - ext->iphlen < hlen) {
			DPRINTF("iplen %u, iphlen %u, tcphlen %zu",
			    ext->iplen, ext->iphlen, hlen);
			ext->tcp = NULL;
			return;
		}
		ext->tcphlen = hlen;
		ext->paylen -= hlen;
		break;

	case IPPROTO_UDP:
		m = m_getptr(m, hoff + hlen, &hoff);
		if (m == NULL || m->m_len - hoff < sizeof(*ext->udp)) {
			DPRINTF("m_len %d, hoff %d, tcp %zu",
			    m ? m->m_len : -1, hoff, sizeof(*ext->tcp));
			return;
		}
		ext->udp = (struct udphdr *)(mtod(m, caddr_t) + hoff);

		hlen = sizeof(*ext->udp);
		if (ext->iplen - ext->iphlen < hlen) {
			DPRINTF("iplen %u, iphlen %u, udphlen %zu",
			    ext->iplen, ext->iphlen, hlen);
			ext->udp = NULL;
			return;
		}
		break;
	}

	DNPRINTF(2, "%s%s%s%s%s%s ip %u, iph %u, tcph %u, payl %u",
	    ext->eh ? "eh," : "", ext->evh ? "evh," : "",
	    ext->ip4 ? "ip4," : "", ext->ip6 ? "ip6," : "",
	    ext->tcp ? "tcp," : "", ext->udp ? "udp," : "",
	    ext->iplen, ext->iphlen, ext->tcphlen, ext->paylen);
}

#if NAF_FRAME > 0

#include <sys/socket.h>
#include <sys/protosw.h>

/*
 * lock order is:
 *
 * - socket lock
 * - ether_pcb_lock
 * - socket buffer mtx
 */

struct ether_pcb;

struct ether_pcb_group {
	TAILQ_ENTRY(ether_pcb_group)
			 epg_entry;
	struct ether_pcb *
			 epg_pcb;
	unsigned int	 epg_ifindex;
	uint8_t		 epg_addr[ETHER_ADDR_LEN];
	struct task	 epg_hook;
};

TAILQ_HEAD(ether_pcb_groups, ether_pcb_group);

struct ether_pcb {
	TAILQ_ENTRY(ether_pcb)
			 ep_entry;
	struct rwlock	 ep_lock;

	struct socket	*ep_socket;

	uint64_t	 ep_laddr;
	uint64_t	 ep_faddr;
	unsigned int	 ep_ifindex;
	uint16_t	 ep_etype;

	uint64_t	 ep_options;
	int		 ep_txprio;

	struct ether_pcb_groups
			 ep_groups;
};

TAILQ_HEAD(ether_pcb_list, ether_pcb);

static int	ether_frm_attach(struct socket *, int, int);
static int	ether_frm_detach(struct socket *);
static int	ether_frm_bind(struct socket *, struct mbuf *, struct proc *);
static int	ether_frm_connect(struct socket *, struct mbuf *);
static int	ether_frm_disconnect(struct socket *);
static int	ether_frm_shutdown(struct socket *);
static int	ether_frm_send(struct socket *, struct mbuf *, struct mbuf *,
		    struct mbuf *);
static int	ether_frm_sockaddr(struct socket *, struct mbuf *);
static int	ether_frm_peeraddr(struct socket *, struct mbuf *);

const struct pr_usrreqs ether_frm_usrreqs = {
	.pru_attach	= ether_frm_attach,
	.pru_detach	= ether_frm_detach,
	.pru_bind	= ether_frm_bind,
	.pru_connect	= ether_frm_connect,
	.pru_disconnect	= ether_frm_disconnect,
	.pru_shutdown	= ether_frm_shutdown,
	.pru_send	= ether_frm_send,
	.pru_sockaddr	= ether_frm_sockaddr,
	.pru_peeraddr	= ether_frm_peeraddr,
};

static struct rwlock ether_pcb_lock = RWLOCK_INITIALIZER("ethsocks");
static struct ether_pcb_list ether_pcbs = TAILQ_HEAD_INITIALIZER(ether_pcbs);

static int
ether_frm_valid_etype(uint16_t etype)
{
	switch (etype) {
	case ETHERTYPE_LLDP:
	case ETHERTYPE_EAPOL:
	case ETHERTYPE_PTP:
	case ETHERTYPE_CFM:
		return (1);
	}

	return (0);
}

static int
ether_frm_nam2sfrm(struct sockaddr_frame **sfrmp, const struct mbuf *nam)
{
	struct sockaddr_frame *sfrm;

	if (nam->m_len != sizeof(*sfrm))
		return (EINVAL);

	sfrm = mtod(nam, struct sockaddr_frame *);
	if (sfrm->sfrm_family != AF_FRAME)
		return (EAFNOSUPPORT);
	*sfrmp = sfrm;
	return (0);
}

static int
ether_frm_ifp(struct ifnet **ifpp, const struct sockaddr_frame *sfrm)
{
	struct ifnet *ifp;

	if (sfrm->sfrm_ifindex != 0)
		ifp = if_get(sfrm->sfrm_ifindex);
	else if (sfrm->sfrm_ifname[0] != '\0') {
		KERNEL_LOCK();
		ifp = if_unit(sfrm->sfrm_ifname);
		KERNEL_UNLOCK();
	} else {
		*ifpp = NULL;
		return (0);
	}

	if (ifp == NULL)
		return (ENXIO);

	if (ifp->if_type != IFT_ETHER) {
		if_put(ifp);
		return (EAFNOSUPPORT);
	}

	*ifpp = ifp;
	return (0);
}

static int
ether_frm_attach(struct socket *so, int proto, int wait)
{
	struct ether_pcb *ep;
	int error;

	if (so->so_pcb != NULL)
		return (EINVAL);

	error = suser(curproc);
	if (error != 0)
		return (error);

	error = soreserve(so, MCLBYTES, MCLBYTES);
	if (error != 0)
		return (error);

	ep = malloc(sizeof(*ep), M_PCB, (wait ? M_WAITOK : M_NOWAIT) | M_ZERO);
	if (ep == NULL)
		return (ENOMEM);

	rw_init(&ep->ep_lock, "ethsock");

	so->so_pcb = ep;
	ep->ep_socket = so; /* shares a ref with the list */

	ep->ep_txprio = IF_HDRPRIO_PACKET;
	TAILQ_INIT(&ep->ep_groups);

	/* give the ref to the list */
	rw_enter_write(&ether_pcb_lock);
	TAILQ_INSERT_TAIL(&ether_pcbs, ep, ep_entry);
	rw_exit_write(&ether_pcb_lock);

	return (0);
}

static int
ether_frm_detach(struct socket *so)
{
	struct ether_pcb *ep;
	struct ether_pcb_group *epg, *nepg;
	struct ifnet *ifp;

	soassertlocked(so);

	ep = so->so_pcb;

	/* take the ref from the list */
	rw_enter_write(&ether_pcb_lock);
	TAILQ_REMOVE(&ether_pcbs, ep, ep_entry);
	rw_exit_write(&ether_pcb_lock);

	so->so_pcb = NULL; /* shares a ref with the list */

	/* XXX locking */
	TAILQ_FOREACH_SAFE(epg, &ep->ep_groups, epg_entry, nepg) {
		ifp = if_get(epg->epg_ifindex);
		if (ifp != NULL) {
			struct ifreq ifr;
			struct sockaddr *sa;

			if_detachhook_del(ifp, &epg->epg_hook);

			memset(&ifr, 0, sizeof(ifr));
			strlcpy(ifr.ifr_name, ifp->if_xname,
			    sizeof(ifr.ifr_name));
			sa = &ifr.ifr_addr;
			sa->sa_family = AF_UNSPEC;
			memcpy(sa->sa_data, &epg->epg_addr, ETHER_ADDR_LEN);
		
			(*ifp->if_ioctl)(ifp, SIOCDELMULTI, (caddr_t)&ifr);
		}
		if_put(ifp);

		TAILQ_REMOVE(&ep->ep_groups, epg, epg_entry);
		free(epg, M_PCB, sizeof(*epg));
	}

	free(ep, M_PCB, sizeof(*ep));

	return (0);
}

static int
ether_frm_bind(struct socket *so, struct mbuf *nam, struct proc *p)
{
	struct sockaddr_frame *sfrm;
	struct ether_pcb *ep;
	struct ether_pcb *epe;
	struct ifnet *ifp = NULL;
	unsigned int ifindex = 0;
	uint16_t etype;
	uint64_t laddr;
	int error;

	soassertlocked(so);

	error = ether_frm_nam2sfrm(&sfrm, nam);
	if (error != 0)
		return (error);

	etype = ntohs(sfrm->sfrm_proto);
	if (!ether_frm_valid_etype(etype))
		return (EADDRNOTAVAIL);

	ep = so->so_pcb;
	if (ep->ep_etype != 0)
		return (EINVAL);

	error = ether_frm_ifp(&ifp, sfrm);
	if (error != 0)
		return (error);
	if (ifp != NULL)
		ifindex = ifp->if_index;

	laddr = ether_addr_to_e64((struct ether_addr *)sfrm->sfrm_addr);

	rw_enter_write(&ether_pcb_lock);
	TAILQ_FOREACH(epe, &ether_pcbs, ep_entry) {
		if (ep == epe)
			continue;

		/* XXX check stuff */
	}

	if (error == 0) {
		/* serialised by the socket lock */
		ep->ep_etype = etype;
		ep->ep_ifindex = ifindex;
		ep->ep_laddr = laddr;
	}
	rw_exit_write(&ether_pcb_lock);

	if_put(ifp);
	return (error);
}

static int
ether_frm_connect(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_frame *sfrm;
	struct ether_pcb *ep;
	struct ether_pcb *epe;
	struct ifnet *ifp = NULL;
	uint64_t faddr;
	uint16_t etype;
	int error;

	soassertlocked(so);

	error = ether_frm_nam2sfrm(&sfrm, nam);
	if (error != 0)
		return (error);

	etype = ntohs(sfrm->sfrm_proto);
	if (!ether_frm_valid_etype(etype))
		return (EADDRNOTAVAIL);

	faddr = ether_addr_to_e64((struct ether_addr *)sfrm->sfrm_addr);
	if (faddr == 0)
		return (EADDRNOTAVAIL);

	error = ether_frm_ifp(&ifp, sfrm);
	if (error != 0)
		return (error);
	if (ifp == NULL)
		return (EADDRNOTAVAIL);

	ep = so->so_pcb;
	if (ep->ep_etype != 0) {
		if (ep->ep_faddr != 0 ||
		    ep->ep_etype != etype) {
			error = EISCONN;
			goto put;
		}
	}
	if (ep->ep_ifindex != 0) {
		if (ep->ep_ifindex != ifp->if_index) {
			error = EADDRNOTAVAIL;
			goto put;
		}
	}

	rw_enter_write(&ether_pcb_lock);
	TAILQ_FOREACH(epe, &ether_pcbs, ep_entry) {
		if (ep == epe)
			continue;
		/* XXX check stuff */
	}

	if (error == 0) {
		/* serialised by the socket lock */
		ep->ep_etype = etype;
		ep->ep_ifindex = ifp->if_index;
		ep->ep_faddr = faddr;
	}
	rw_exit_write(&ether_pcb_lock);

put:
	if_put(ifp);
	return (error);
}

static int
ether_frm_disconnect(struct socket *so)
{
	struct ether_pcb *ep;

	soassertlocked(so);

	ep = so->so_pcb;
	if (ep->ep_faddr == 0)
		return (ENOTCONN);

	rw_enter_write(&ether_pcb_lock);
	ep->ep_ifindex = 0;
	ep->ep_etype = 0;
	ep->ep_laddr = 0;
	ep->ep_faddr = 0;
	rw_exit_write(&ether_pcb_lock);

	return (0);
}

static int
ether_frm_shutdown(struct socket *so)
{
	soassertlocked(so);
	socantsendmore(so);
	return (0);
}

static int
ether_frm_send(struct socket *so, struct mbuf *m, struct mbuf *nam,
    struct mbuf *control)
{
	struct ether_pcb *ep;
	int error;
	uint16_t etype;
	uint64_t laddr;
	uint64_t faddr;
	struct ifnet *ifp = NULL;
	struct arpcom *ac;
	struct ether_header *eh;
	int txprio;

	soassertlocked_readonly(so);

	ep = so->so_pcb;
	KASSERTMSG(ep != NULL, "%s: NULL pcb on socket %p", __func__, so);
	txprio = ep->ep_txprio;

	/* XXX get prio out of a cmsg */
	m_freem(control);

	if (nam != NULL) {
		struct sockaddr_frame *sfrm;

		error = ether_frm_nam2sfrm(&sfrm, nam);
		if (error != 0)
			goto drop;

		etype = ntohs(sfrm->sfrm_proto);
		if (!ether_frm_valid_etype(etype)) {
			error = EADDRNOTAVAIL;
			goto drop;
		}

		if (ep->ep_faddr != 0) {
			error = EISCONN;
			goto drop;
		}
		faddr = ether_addr_to_e64((struct ether_addr *)sfrm->sfrm_addr);
		if (faddr == 0) {
			error = EADDRNOTAVAIL;
			goto drop;
		}

		error = ether_frm_ifp(&ifp, sfrm);
		if (error != 0)
			goto drop;
		if (ifp == NULL) {
			ifp = if_get(ep->ep_ifindex);
			if (ifp == NULL) {
				error = EADDRNOTAVAIL;
				goto drop;
			}
		} else {
			if (ep->ep_ifindex != 0 &&
			    ep->ep_ifindex != ifp->if_index) {
				error = EADDRNOTAVAIL;
				goto drop;
			}
		}

		if (ep->ep_etype != etype) {
			if (ep->ep_etype == 0) {
				/* this is cheeky */
				rw_enter_write(&ether_pcb_lock);
				ep->ep_etype = etype;
				rw_exit_write(&ether_pcb_lock);
			} else {
				error = EADDRNOTAVAIL;
				goto drop;
			}
		}
	} else {
		faddr = ep->ep_faddr;
		if (faddr == 0) {
			error = ENOTCONN;
			goto drop;
		}

		ifp = if_get(ep->ep_ifindex);
		if (ifp == NULL) {
			error = ENXIO;
			goto drop;
		}

		etype = ep->ep_etype;
	}

	if (ifp->if_type != IFT_ETHER) {
		error = EAFNOSUPPORT;
		goto drop;
	}

	ac = (struct arpcom *)ifp;

	laddr = ether_addr_to_e64((struct ether_addr *)ac->ac_enaddr);
	if (ep->ep_laddr != laddr) {
		if (ep->ep_laddr != 0) {
			error = EADDRNOTAVAIL;
			goto drop;
		}
	}

	m = m_prepend(m, ETHER_ALIGN + sizeof(*eh), M_NOWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto drop;
	}
	m_adj(m, ETHER_ALIGN);

	if (txprio != IF_HDRPRIO_PACKET)
		m->m_pkthdr.pf.prio = txprio;

	eh = mtod(m, struct ether_header *);
	ether_e64_to_addr((struct ether_addr *)eh->ether_dhost, faddr);
	ether_e64_to_addr((struct ether_addr *)eh->ether_shost, laddr);
	eh->ether_type = htons(etype);

	error = if_enqueue(ifp, m);
	m = NULL;

drop:
	if_put(ifp);
	m_freem(m);
	return (error);
}

static int
ether_frm_sockaddr_frame(struct ether_pcb *ep, struct mbuf *nam, uint64_t addr)
{
	struct sockaddr_frame *sfrm;
	struct ifnet *ifp;

	nam->m_len = sizeof(*sfrm);
	sfrm = mtod(nam, struct sockaddr_frame *);
	memset(sfrm, 0, sizeof(*sfrm));
	sfrm->sfrm_len = sizeof(*sfrm);
	sfrm->sfrm_family = AF_FRAME;

	ether_e64_to_addr((struct ether_addr *)sfrm->sfrm_addr, addr);

	if (ep->ep_etype) {
		sfrm->sfrm_proto = htons(ep->ep_etype);
		sfrm->sfrm_ifindex = ep->ep_ifindex;

		ifp = if_get(ep->ep_ifindex);
		if (ifp != NULL) {
			strlcpy(sfrm->sfrm_ifname, ifp->if_xname,
			    sizeof(sfrm->sfrm_ifname));
		}
		if_put(ifp);
	}

	return (0);
}

static int
ether_frm_sockaddr(struct socket *so, struct mbuf *nam)
{
	struct ether_pcb *ep = so->so_pcb;

	return (ether_frm_sockaddr_frame(ep, nam, ep->ep_laddr));
}

static int
ether_frm_peeraddr(struct socket *so, struct mbuf *nam)
{
	struct ether_pcb *ep = so->so_pcb;

	return (ether_frm_sockaddr_frame(ep, nam, ep->ep_faddr));
}

static void
ether_frm_group_detach(void *arg)
{
	struct ether_pcb_group *epg = arg;
	struct ether_pcb *ep = epg->epg_pcb;
	struct socket *so = ep->ep_socket;
	struct ifnet *ifp;

	ifp = if_get(epg->epg_ifindex);

	/* XXX locking^Wreference counts */
	solock(so);
	if (ifp != NULL)
		if_detachhook_del(ifp, &epg->epg_hook);
	TAILQ_REMOVE(&ep->ep_groups, epg, epg_entry);
	sounlock(so);

	if_put(ifp);
	free(epg, M_PCB, sizeof(*epg));
}

static int
ether_frm_group(struct socket *so, int optname, struct mbuf *m)
{
	struct frame_mreq *fmr;
	struct ifreq ifr;
	struct sockaddr *sa;
	struct ifnet *ifp;
	struct ether_pcb *ep;
	struct ether_pcb_group *epg;
	u_long cmd;
	int error;

	soassertlocked(so);

	if (m == NULL || m->m_len != sizeof(*fmr))
		return (EINVAL);

	fmr = mtod(m, struct frame_mreq *);
	if (!ETHER_IS_MULTICAST(fmr->fmr_addr))
		return (EADDRNOTAVAIL);

	if (fmr->fmr_ifindex == 0) {
		KERNEL_LOCK();
		ifp = if_unit(fmr->fmr_ifname);
		KERNEL_UNLOCK();
	} else
		ifp = if_get(fmr->fmr_ifindex);
	if (ifp == NULL)
		return (ENXIO);

	if (ifp->if_type != IFT_ETHER) {
		error = EADDRNOTAVAIL;
		goto put;
	}

	if (ETHER_IS_BROADCAST(fmr->fmr_addr)) {
		error = 0;
		goto put;
	}

	ep = so->so_pcb;
	TAILQ_FOREACH(epg, &ep->ep_groups, epg_entry) {
		if (epg->epg_ifindex != ifp->if_index)
			continue;
		if (!ETHER_IS_EQ(epg->epg_addr, fmr->fmr_addr))
			continue;

		break;
	}

	switch (optname) {
	case FRAME_ADD_MEMBERSHIP:
		if (epg != NULL) {
			error = EISCONN;
			goto put;
		}
		epg = malloc(sizeof(*epg), M_PCB, M_DONTWAIT);
		if (epg == NULL) {
			error = ENOMEM;
			goto put;
		}

		epg->epg_pcb = ep;
		epg->epg_ifindex = ifp->if_index;
		memcpy(&epg->epg_addr, fmr->fmr_addr, sizeof(epg->epg_addr));
		task_set(&epg->epg_hook, ether_frm_group_detach, epg);

		cmd = SIOCADDMULTI;
		break;
	case FRAME_DEL_MEMBERSHIP:
		if (epg == NULL) {
			error = ENOTCONN;
			goto put;
		}
		cmd = SIOCDELMULTI;
		break;
	default:
		panic("%s: unexpected optname %d", __func__, optname);
		/* NOTREACHED */
	}

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifp->if_xname, sizeof(ifr.ifr_name));
	sa = &ifr.ifr_addr;
	sa->sa_family = AF_UNSPEC;
	memcpy(sa->sa_data, fmr->fmr_addr, ETHER_ADDR_LEN);

	/* XXX soref? */
	/* this could lead to multiple epgs for the same if/group */
	sounlock(so);
	KERNEL_LOCK();
	NET_LOCK();
	error = (*ifp->if_ioctl)(ifp, cmd, (caddr_t)&ifr);
	NET_UNLOCK();
	KERNEL_UNLOCK();
	solock(so);

	switch (optname) {
	case FRAME_ADD_MEMBERSHIP:
		if (error != 0) {
			free(epg, M_PCB, sizeof(*epg));
			break;
		}

		TAILQ_INSERT_TAIL(&ep->ep_groups, epg, epg_entry);
		if_detachhook_add(ifp, &epg->epg_hook);
		break;
	case FRAME_DEL_MEMBERSHIP:
		if (error != 0)
			break;

		if_detachhook_del(ifp, &epg->epg_hook);
		TAILQ_REMOVE(&ep->ep_groups, epg, epg_entry);
		free(epg, M_PCB, sizeof(*epg));
		break;
	}
put:
	if_put(ifp);

	return (error);
}

#define ETHER_PCB_OPTM(_v)	(1ULL << (_v))

#define ETHER_PCB_OPTS				\
	ETHER_PCB_OPTM(FRAME_RECVDSTADDR) |	\
	ETHER_PCB_OPTM(FRAME_RECVPRIO)

static int
ether_frm_setopt(struct ether_pcb *ep, int optname, struct mbuf *m)
{
	uint64_t optm = ETHER_PCB_OPTM(optname);
	int opt;

	if (!ISSET(ETHER_PCB_OPTS, optm))
		return (ENOPROTOOPT);

	if (m == NULL || m->m_len != sizeof(opt))
		return (EINVAL);

	opt = *mtod(m, int *);
	if (opt)
		SET(ep->ep_options, optm);
	else
		CLR(ep->ep_options, optm);

	return (0);
}

static int
ether_frm_setsockopt(struct socket *so, int optname, struct mbuf *m)
{
	struct ether_pcb *ep = so->so_pcb;
	int error = ENOPROTOOPT;
	int v;

	if (optname >= 0 && optname < 64)
		return (ether_frm_setopt(ep, optname, m));

	switch (optname) {
	case FRAME_ADD_MEMBERSHIP:
	case FRAME_DEL_MEMBERSHIP:
		error = ether_frm_group(so, optname, m);
		break;
	case FRAME_SENDPRIO:
		if (m == NULL || m->m_len != sizeof(v)) {
			error = EINVAL;
			break;
		}
		v = *mtod(m, int *);
		error = if_txhprio_l2_check(v);
		if (error != 0)
			break;
		ep->ep_txprio = v;
		break;

	default:
		break;
	}

	return (error);
}

static int
ether_frm_getopt(struct ether_pcb *ep, int optname, struct mbuf *m)
{
	uint64_t optm = ETHER_PCB_OPTM(optname);
	int opt;

	if (!ISSET(ETHER_PCB_OPTS, optm))
		return (ENOPROTOOPT);

	opt = !!ISSET(ep->ep_options, optm);

	m->m_len = sizeof(opt);
	*mtod(m, int *) = opt;

	return (0);
}

static int
ether_frm_getsockopt(struct socket *so, int optname, struct mbuf *m)
{
	struct ether_pcb *ep = so->so_pcb;
	int error = ENOPROTOOPT;

	if (optname >= 0 && optname < 64)
		return (ether_frm_getopt(ep, optname, m));

	switch (optname) {
	default:
		break;
	}

	return (error);
}

int
ether_frm_ctloutput(int op, struct socket *so, int level, int optname,
    struct mbuf *m)
{
	int error = 0;

	if (level != IFT_ETHER)
		return (EINVAL);

	switch (op) {
	case PRCO_SETOPT:
		error = ether_frm_setsockopt(so, optname, m);
		break;
	case PRCO_GETOPT:
		error = ether_frm_getsockopt(so, optname, m);
		break;
	}

	return (error);
}

static struct mbuf *
ether_frm_cmsg(struct mbuf *cmsgs, const void *data, size_t datalen,
    int type, int level)
{
	struct mbuf *cm;

	cm = sbcreatecontrol(data, datalen, type, level);
	if (cm != NULL) {
		cm->m_next = cmsgs;
		cmsgs = cm;
	}

	return (cmsgs);
}

static void
ether_frm_recv(struct socket *so, struct mbuf *m0,
    const struct sockaddr_frame *sfrm)
{
	struct ether_pcb *ep = so->so_pcb;
	struct mbuf *m;
	struct mbuf *cmsgs = NULL;
	int ok;

	/* offset 0 and m_adj cos sbappendaddr needs m_pkthdr.len */
	m = m_copym(m0, 0, M_COPYALL, M_DONTWAIT);
	if (m == NULL)
		return;
	m_adj(m, sizeof(struct ether_header));

	if (ISSET(ep->ep_options, ETHER_PCB_OPTM(FRAME_RECVPRIO))) {
		int rxprio = m0->m_pkthdr.pf.prio;
		cmsgs = ether_frm_cmsg(cmsgs, &rxprio, sizeof(rxprio),
		    FRAME_RECVPRIO, IFT_ETHER);
	}

	if (ISSET(ep->ep_options, ETHER_PCB_OPTM(FRAME_RECVDSTADDR))) {
		struct ether_header *eh = mtod(m0, struct ether_header *);
		cmsgs = ether_frm_cmsg(cmsgs, eh->ether_dhost, ETHER_ADDR_LEN,
		    FRAME_RECVDSTADDR, IFT_ETHER);
	}

	if (ISSET(so->so_options, SO_TIMESTAMP)) {
		struct timeval tv;
		m_microtime(m0, &tv);
		cmsgs = ether_frm_cmsg(cmsgs, &tv, sizeof(tv),
		    SCM_TIMESTAMP, SOL_SOCKET);
	}

	mtx_enter(&so->so_rcv.sb_mtx);
	ok = sbappendaddr(&so->so_rcv, (struct sockaddr *)sfrm, m, cmsgs);
	mtx_leave(&so->so_rcv.sb_mtx);

	if (!ok) {
		m_freem(m);
		m_freem(cmsgs);
		return;
	}

	sorwakeup(so);
}

static struct mbuf *
ether_frm_input(struct ifnet *ifp, struct mbuf *m, uint64_t dst, uint16_t etype)
{
	struct sockaddr_frame sfrm = { .sfrm_family = AF_UNSPEC };
	struct ether_pcb *ep;
	struct ether_header *eh;
	uint64_t src;

	if (TAILQ_EMPTY(&ether_pcbs))
		return (m);

	eh = mtod(m, struct ether_header *);
	src = ether_addr_to_e64((struct ether_addr *)eh->ether_shost);
	if (src == 0)
		return (m);

	rw_enter_read(&ether_pcb_lock);
	TAILQ_FOREACH(ep, &ether_pcbs, ep_entry) {
		if (ep->ep_etype == 0) /* bound? */
			continue;
		if (ep->ep_etype != etype)
			continue;
		if (ep->ep_ifindex != 0) {
			if (ep->ep_ifindex != ifp->if_index)
				continue;
		}

		if (ep->ep_laddr != 0) {
			if (ep->ep_laddr != dst)
				continue;
		}
		/* ether_input says dst is valid for local delivery */

		if (ep->ep_faddr != 0) { /* connected? */
			if (ep->ep_faddr != src)
				continue;
		}

		if (sfrm.sfrm_family == AF_UNSPEC) {
			sfrm.sfrm_len = sizeof(sfrm);
			sfrm.sfrm_family = AF_FRAME;
			sfrm.sfrm_proto = htons(etype);
			sfrm.sfrm_ifindex = ifp->if_index;
			ether_e64_to_addr((struct ether_addr *)sfrm.sfrm_addr,
			    src);
			strlcpy(sfrm.sfrm_ifname, ifp->if_xname,
			    sizeof(sfrm.sfrm_ifname));
		}

		ether_frm_recv(ep->ep_socket, m, &sfrm);
	}
	rw_exit_read(&ether_pcb_lock);

	return (m);
}

#endif /* NAF_FRAME */

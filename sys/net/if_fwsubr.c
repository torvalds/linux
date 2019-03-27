/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2004 Doug Rabson
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
 * $FreeBSD$
 */

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/if_llc.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/bpf.h>
#include <net/firewire.h>
#include <net/if_llatbl.h>

#if defined(INET) || defined(INET6)
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#endif
#ifdef INET6
#include <netinet6/nd6.h>
#endif

#include <security/mac/mac_framework.h>

static MALLOC_DEFINE(M_FWCOM, "fw_com", "firewire interface internals");

struct fw_hwaddr firewire_broadcastaddr = {
	0xffffffff,
	0xffffffff,
	0xff,
	0xff,
	0xffff,
	0xffffffff
};

static int
firewire_output(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
    struct route *ro)
{
	struct fw_com *fc = IFP2FWC(ifp);
	int error, type;
	struct m_tag *mtag;
	union fw_encap *enc;
	struct fw_hwaddr *destfw;
	uint8_t speed;
	uint16_t psize, fsize, dsize;
	struct mbuf *mtail;
	int unicast, dgl, foff;
	static int next_dgl;
#if defined(INET) || defined(INET6)
	int is_gw = 0;
#endif

#ifdef MAC
	error = mac_ifnet_check_transmit(ifp, m);
	if (error)
		goto bad;
#endif

	if (!((ifp->if_flags & IFF_UP) &&
	   (ifp->if_drv_flags & IFF_DRV_RUNNING))) {
		error = ENETDOWN;
		goto bad;
	}

#if defined(INET) || defined(INET6)
	if (ro != NULL)
		is_gw = (ro->ro_flags & RT_HAS_GW) != 0;
#endif
	/*
	 * For unicast, we make a tag to store the lladdr of the
	 * destination. This might not be the first time we have seen
	 * the packet (for instance, the arp code might be trying to
	 * re-send it after receiving an arp reply) so we only
	 * allocate a tag if there isn't one there already. For
	 * multicast, we will eventually use a different tag to store
	 * the channel number.
	 */
	unicast = !(m->m_flags & (M_BCAST | M_MCAST));
	if (unicast) {
		mtag = m_tag_locate(m, MTAG_FIREWIRE, MTAG_FIREWIRE_HWADDR, NULL);
		if (!mtag) {
			mtag = m_tag_alloc(MTAG_FIREWIRE, MTAG_FIREWIRE_HWADDR,
			    sizeof (struct fw_hwaddr), M_NOWAIT);
			if (!mtag) {
				error = ENOMEM;
				goto bad;
			}
			m_tag_prepend(m, mtag);
		}
		destfw = (struct fw_hwaddr *)(mtag + 1);
	} else {
		destfw = NULL;
	}

	switch (dst->sa_family) {
#ifdef INET
	case AF_INET:
		/*
		 * Only bother with arp for unicast. Allocation of
		 * channels etc. for firewire is quite different and
		 * doesn't fit into the arp model.
		 */
		if (unicast) {
			error = arpresolve(ifp, is_gw, m, dst,
			    (u_char *) destfw, NULL, NULL);
			if (error)
				return (error == EWOULDBLOCK ? 0 : error);
		}
		type = ETHERTYPE_IP;
		break;

	case AF_ARP:
	{
		struct arphdr *ah;
		ah = mtod(m, struct arphdr *);
		ah->ar_hrd = htons(ARPHRD_IEEE1394);
		type = ETHERTYPE_ARP;
		if (unicast)
			*destfw = *(struct fw_hwaddr *) ar_tha(ah);

		/*
		 * The standard arp code leaves a hole for the target
		 * hardware address which we need to close up.
		 */
		bcopy(ar_tpa(ah), ar_tha(ah), ah->ar_pln);
		m_adj(m, -ah->ar_hln);
		break;
	}
#endif

#ifdef INET6
	case AF_INET6:
		if (unicast) {
			error = nd6_resolve(fc->fc_ifp, is_gw, m, dst,
			    (u_char *) destfw, NULL, NULL);
			if (error)
				return (error == EWOULDBLOCK ? 0 : error);
		}
		type = ETHERTYPE_IPV6;
		break;
#endif

	default:
		if_printf(ifp, "can't handle af%d\n", dst->sa_family);
		error = EAFNOSUPPORT;
		goto bad;
	}

	/*
	 * Let BPF tap off a copy before we encapsulate.
	 */
	if (bpf_peers_present(ifp->if_bpf)) {
		struct fw_bpfhdr h;
		if (unicast)
			bcopy(destfw, h.firewire_dhost, 8);
		else
			bcopy(&firewire_broadcastaddr, h.firewire_dhost, 8);
		bcopy(&fc->fc_hwaddr, h.firewire_shost, 8);
		h.firewire_type = htons(type);
		bpf_mtap2(ifp->if_bpf, &h, sizeof(h), m);
	}

	/*
	 * Punt on MCAP for now and send all multicast packets on the
	 * broadcast channel.
	 */
	if (m->m_flags & M_MCAST)
		m->m_flags |= M_BCAST;

	/*
	 * Figure out what speed to use and what the largest supported
	 * packet size is. For unicast, this is the minimum of what we
	 * can speak and what they can hear. For broadcast, lets be
	 * conservative and use S100. We could possibly improve that
	 * by examining the bus manager's speed map or similar. We
	 * also reduce the packet size for broadcast to account for
	 * the GASP header.
	 */
	if (unicast) {
		speed = min(fc->fc_speed, destfw->sspd);
		psize = min(512 << speed, 2 << destfw->sender_max_rec);
	} else {
		speed = 0;
		psize = 512 - 2*sizeof(uint32_t);
	}

	/*
	 * Next, we encapsulate, possibly fragmenting the original
	 * datagram if it won't fit into a single packet.
	 */
	if (m->m_pkthdr.len <= psize - sizeof(uint32_t)) {
		/*
		 * No fragmentation is necessary.
		 */
		M_PREPEND(m, sizeof(uint32_t), M_NOWAIT);
		if (!m) {
			error = ENOBUFS;
			goto bad;
		}
		enc = mtod(m, union fw_encap *);
		enc->unfrag.ether_type = type;
		enc->unfrag.lf = FW_ENCAP_UNFRAG;
		enc->unfrag.reserved = 0;

		/*
		 * Byte swap the encapsulation header manually.
		 */
		enc->ul[0] = htonl(enc->ul[0]);

		error = (ifp->if_transmit)(ifp, m);
		return (error);
	} else {
		/*
		 * Fragment the datagram, making sure to leave enough
		 * space for the encapsulation header in each packet.
		 */
		fsize = psize - 2*sizeof(uint32_t);
		dgl = next_dgl++;
		dsize = m->m_pkthdr.len;
		foff = 0;
		while (m) {
			if (m->m_pkthdr.len > fsize) {
				/*
				 * Split off the tail segment from the
				 * datagram, copying our tags over.
				 */
				mtail = m_split(m, fsize, M_NOWAIT);
				m_tag_copy_chain(mtail, m, M_NOWAIT);
			} else {
				mtail = NULL;
			}

			/*
			 * Add our encapsulation header to this
			 * fragment and hand it off to the link.
			 */
			M_PREPEND(m, 2*sizeof(uint32_t), M_NOWAIT);
			if (!m) {
				error = ENOBUFS;
				goto bad;
			}
			enc = mtod(m, union fw_encap *);
			if (foff == 0) {
				enc->firstfrag.lf = FW_ENCAP_FIRST;
				enc->firstfrag.reserved1 = 0;
				enc->firstfrag.reserved2 = 0;
				enc->firstfrag.datagram_size = dsize - 1;
				enc->firstfrag.ether_type = type;
				enc->firstfrag.dgl = dgl;
			} else {
				if (mtail)
					enc->nextfrag.lf = FW_ENCAP_NEXT;
				else
					enc->nextfrag.lf = FW_ENCAP_LAST;
				enc->nextfrag.reserved1 = 0;
				enc->nextfrag.reserved2 = 0;
				enc->nextfrag.reserved3 = 0;
				enc->nextfrag.datagram_size = dsize - 1;
				enc->nextfrag.fragment_offset = foff;
				enc->nextfrag.dgl = dgl;
			}
			foff += m->m_pkthdr.len - 2*sizeof(uint32_t);

			/*
			 * Byte swap the encapsulation header manually.
			 */
			enc->ul[0] = htonl(enc->ul[0]);
			enc->ul[1] = htonl(enc->ul[1]);

			error = (ifp->if_transmit)(ifp, m);
			if (error) {
				if (mtail)
					m_freem(mtail);
				return (ENOBUFS);
			}

			m = mtail;
		}

		return (0);
	}

bad:
	if (m)
		m_freem(m);
	return (error);
}

static struct mbuf *
firewire_input_fragment(struct fw_com *fc, struct mbuf *m, int src)
{
	union fw_encap *enc;
	struct fw_reass *r;
	struct mbuf *mf, *mprev;
	int dsize;
	int fstart, fend, start, end, islast;
	uint32_t id;

	/*
	 * Find an existing reassembly buffer or create a new one.
	 */
	enc = mtod(m, union fw_encap *);
	id = enc->firstfrag.dgl | (src << 16);
	STAILQ_FOREACH(r, &fc->fc_frags, fr_link)
		if (r->fr_id == id)
			break;
	if (!r) {
		r = malloc(sizeof(struct fw_reass), M_TEMP, M_NOWAIT);
		if (!r) {
			m_freem(m);
			return 0;
		}
		r->fr_id = id;
		r->fr_frags = 0;
		STAILQ_INSERT_HEAD(&fc->fc_frags, r, fr_link);
	}

	/*
	 * If this fragment overlaps any other fragment, we must discard
	 * the partial reassembly and start again.
	 */
	if (enc->firstfrag.lf == FW_ENCAP_FIRST)
		fstart = 0;
	else
		fstart = enc->nextfrag.fragment_offset;
	fend = fstart + m->m_pkthdr.len - 2*sizeof(uint32_t);
	dsize = enc->nextfrag.datagram_size;
	islast = (enc->nextfrag.lf == FW_ENCAP_LAST);

	for (mf = r->fr_frags; mf; mf = mf->m_nextpkt) {
		enc = mtod(mf, union fw_encap *);
		if (enc->nextfrag.datagram_size != dsize) {
			/*
			 * This fragment must be from a different
			 * packet.
			 */
			goto bad;
		}
		if (enc->firstfrag.lf == FW_ENCAP_FIRST)
			start = 0;
		else
			start = enc->nextfrag.fragment_offset;
		end = start + mf->m_pkthdr.len - 2*sizeof(uint32_t);
		if ((fstart < end && fend > start) ||
		    (islast && enc->nextfrag.lf == FW_ENCAP_LAST)) {
			/*
			 * Overlap - discard reassembly buffer and start
			 * again with this fragment.
			 */
			goto bad;
		}
	}

	/*
	 * Find where to put this fragment in the list.
	 */
	for (mf = r->fr_frags, mprev = NULL; mf;
	    mprev = mf, mf = mf->m_nextpkt) {
		enc = mtod(mf, union fw_encap *);
		if (enc->firstfrag.lf == FW_ENCAP_FIRST)
			start = 0;
		else
			start = enc->nextfrag.fragment_offset;
		if (start >= fend)
			break;
	}

	/*
	 * If this is a last fragment and we are not adding at the end
	 * of the list, discard the buffer.
	 */
	if (islast && mprev && mprev->m_nextpkt)
		goto bad;

	if (mprev) {
		m->m_nextpkt = mprev->m_nextpkt;
		mprev->m_nextpkt = m;

		/*
		 * Coalesce forwards and see if we can make a whole
		 * datagram.
		 */
		enc = mtod(mprev, union fw_encap *);
		if (enc->firstfrag.lf == FW_ENCAP_FIRST)
			start = 0;
		else
			start = enc->nextfrag.fragment_offset;
		end = start + mprev->m_pkthdr.len - 2*sizeof(uint32_t);
		while (end == fstart) {
			/*
			 * Strip off the encap header from m and
			 * append it to mprev, freeing m.
			 */
			m_adj(m, 2*sizeof(uint32_t));
			mprev->m_nextpkt = m->m_nextpkt;
			mprev->m_pkthdr.len += m->m_pkthdr.len;
			m_cat(mprev, m);

			if (mprev->m_pkthdr.len == dsize + 1 + 2*sizeof(uint32_t)) {
				/*
				 * We have assembled a complete packet
				 * we must be finished. Make sure we have
				 * merged the whole chain.
				 */
				STAILQ_REMOVE(&fc->fc_frags, r, fw_reass, fr_link);
				free(r, M_TEMP);
				m = mprev->m_nextpkt;
				while (m) {
					mf = m->m_nextpkt;
					m_freem(m);
					m = mf;
				}
				mprev->m_nextpkt = NULL;

				return (mprev);
			}

			/*
			 * See if we can continue merging forwards.
			 */
			end = fend;
			m = mprev->m_nextpkt;
			if (m) {
				enc = mtod(m, union fw_encap *);
				if (enc->firstfrag.lf == FW_ENCAP_FIRST)
					fstart = 0;
				else
					fstart = enc->nextfrag.fragment_offset;
				fend = fstart + m->m_pkthdr.len
				    - 2*sizeof(uint32_t);
			} else {
				break;
			}
		}
	} else {
		m->m_nextpkt = 0;
		r->fr_frags = m;
	}

	return (0);

bad:
	while (r->fr_frags) {
		mf = r->fr_frags;
		r->fr_frags = mf->m_nextpkt;
		m_freem(mf);
	}
	m->m_nextpkt = 0;
	r->fr_frags = m;

	return (0);
}

void
firewire_input(struct ifnet *ifp, struct mbuf *m, uint16_t src)
{
	struct fw_com *fc = IFP2FWC(ifp);
	union fw_encap *enc;
	int type, isr;

	/*
	 * The caller has already stripped off the packet header
	 * (stream or wreqb) and marked the mbuf's M_BCAST flag
	 * appropriately. We de-encapsulate the IP packet and pass it
	 * up the line after handling link-level fragmentation.
	 */
	if (m->m_pkthdr.len < sizeof(uint32_t)) {
		if_printf(ifp, "discarding frame without "
		    "encapsulation header (len %u pkt len %u)\n",
		    m->m_len, m->m_pkthdr.len);
	}

	m = m_pullup(m, sizeof(uint32_t));
	if (m == NULL)
		return;
	enc = mtod(m, union fw_encap *);

	/*
	 * Byte swap the encapsulation header manually.
	 */
	enc->ul[0] = ntohl(enc->ul[0]);

	if (enc->unfrag.lf != 0) {
		m = m_pullup(m, 2*sizeof(uint32_t));
		if (!m)
			return;
		enc = mtod(m, union fw_encap *);
		enc->ul[1] = ntohl(enc->ul[1]);
		m = firewire_input_fragment(fc, m, src);
		if (!m)
			return;
		enc = mtod(m, union fw_encap *);
		type = enc->firstfrag.ether_type;
		m_adj(m, 2*sizeof(uint32_t));
	} else {
		type = enc->unfrag.ether_type;
		m_adj(m, sizeof(uint32_t));
	}

	if (m->m_pkthdr.rcvif == NULL) {
		if_printf(ifp, "discard frame w/o interface pointer\n");
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		m_freem(m);
		return;
	}
#ifdef DIAGNOSTIC
	if (m->m_pkthdr.rcvif != ifp) {
		if_printf(ifp, "Warning, frame marked as received on %s\n",
			m->m_pkthdr.rcvif->if_xname);
	}
#endif

#ifdef MAC
	/*
	 * Tag the mbuf with an appropriate MAC label before any other
	 * consumers can get to it.
	 */
	mac_ifnet_create_mbuf(ifp, m);
#endif

	/*
	 * Give bpf a chance at the packet. The link-level driver
	 * should have left us a tag with the EUID of the sender.
	 */
	if (bpf_peers_present(ifp->if_bpf)) {
		struct fw_bpfhdr h;
		struct m_tag *mtag;

		mtag = m_tag_locate(m, MTAG_FIREWIRE, MTAG_FIREWIRE_SENDER_EUID, 0);
		if (mtag)
			bcopy(mtag + 1, h.firewire_shost, 8);
		else
			bcopy(&firewire_broadcastaddr, h.firewire_dhost, 8);
		bcopy(&fc->fc_hwaddr, h.firewire_dhost, 8);
		h.firewire_type = htons(type);
		bpf_mtap2(ifp->if_bpf, &h, sizeof(h), m);
	}

	if (ifp->if_flags & IFF_MONITOR) {
		/*
		 * Interface marked for monitoring; discard packet.
		 */
		m_freem(m);
		return;
	}

	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);

	/* Discard packet if interface is not up */
	if ((ifp->if_flags & IFF_UP) == 0) {
		m_freem(m);
		return;
	}

	if (m->m_flags & (M_BCAST|M_MCAST))
		if_inc_counter(ifp, IFCOUNTER_IMCASTS, 1);

	switch (type) {
#ifdef INET
	case ETHERTYPE_IP:
		isr = NETISR_IP;
		break;

	case ETHERTYPE_ARP:
	{
		struct arphdr *ah;
		ah = mtod(m, struct arphdr *);

		/*
		 * Adjust the arp packet to insert an empty tha slot.
		 */
		m->m_len += ah->ar_hln;
		m->m_pkthdr.len += ah->ar_hln;
		bcopy(ar_tha(ah), ar_tpa(ah), ah->ar_pln);
		isr = NETISR_ARP;
		break;
	}
#endif

#ifdef INET6
	case ETHERTYPE_IPV6:
		isr = NETISR_IPV6;
		break;
#endif

	default:
		m_freem(m);
		return;
	}

	M_SETFIB(m, ifp->if_fib);
	netisr_dispatch(isr, m);
}

int
firewire_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
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
		bcopy(&IFP2FWC(ifp)->fc_hwaddr, &ifr->ifr_addr.sa_data[0],
		    sizeof(struct fw_hwaddr));
		break;

	case SIOCSIFMTU:
		/*
		 * Set the interface MTU.
		 */
		if (ifr->ifr_mtu > 1500) {
			error = EINVAL;
		} else {
			ifp->if_mtu = ifr->ifr_mtu;
		}
		break;
	default:
		error = EINVAL;			/* XXX netbsd has ENOTTY??? */
		break;
	}
	return (error);
}

static int
firewire_resolvemulti(struct ifnet *ifp, struct sockaddr **llsa,
    struct sockaddr *sa)
{
#ifdef INET
	struct sockaddr_in *sin;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif

	switch(sa->sa_family) {
	case AF_LINK:
		/*
		 * No mapping needed.
		 */
		*llsa = NULL;
		return 0;

#ifdef INET
	case AF_INET:
		sin = (struct sockaddr_in *)sa;
		if (!IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
			return EADDRNOTAVAIL;
		*llsa = NULL;
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
		*llsa = NULL;
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

void
firewire_ifattach(struct ifnet *ifp, struct fw_hwaddr *llc)
{
	struct fw_com *fc = IFP2FWC(ifp);
	struct ifaddr *ifa;
	struct sockaddr_dl *sdl;
	static const char* speeds[] = {
		"S100", "S200", "S400", "S800",
		"S1600", "S3200"
	};

	fc->fc_speed = llc->sspd;
	STAILQ_INIT(&fc->fc_frags);

	ifp->if_addrlen = sizeof(struct fw_hwaddr);
	ifp->if_hdrlen = 0;
	if_attach(ifp);
	ifp->if_mtu = 1500;	/* XXX */
	ifp->if_output = firewire_output;
	ifp->if_resolvemulti = firewire_resolvemulti;
	ifp->if_broadcastaddr = (u_char *) &firewire_broadcastaddr;

	ifa = ifp->if_addr;
	KASSERT(ifa != NULL, ("%s: no lladdr!\n", __func__));
	sdl = (struct sockaddr_dl *)ifa->ifa_addr;
	sdl->sdl_type = IFT_IEEE1394;
	sdl->sdl_alen = ifp->if_addrlen;
	bcopy(llc, LLADDR(sdl), ifp->if_addrlen);

	bpfattach(ifp, DLT_APPLE_IP_OVER_IEEE1394,
	    sizeof(struct fw_hwaddr));

	if_printf(ifp, "Firewire address: %8D @ 0x%04x%08x, %s, maxrec %d\n",
	    (uint8_t *) &llc->sender_unique_ID_hi, ":",
	    ntohs(llc->sender_unicast_FIFO_hi),
	    ntohl(llc->sender_unicast_FIFO_lo),
	    speeds[llc->sspd],
	    (2 << llc->sender_max_rec));
}

void
firewire_ifdetach(struct ifnet *ifp)
{
	bpfdetach(ifp);
	if_detach(ifp);
}

void
firewire_busreset(struct ifnet *ifp)
{
	struct fw_com *fc = IFP2FWC(ifp);
	struct fw_reass *r;
	struct mbuf *m;

	/*
	 * Discard any partial datagrams since the host ids may have changed.
	 */
	while ((r = STAILQ_FIRST(&fc->fc_frags))) {
		STAILQ_REMOVE_HEAD(&fc->fc_frags, fr_link);
		while (r->fr_frags) {
			m = r->fr_frags;
			r->fr_frags = m->m_nextpkt;
			m_freem(m);
		}
		free(r, M_TEMP);
	}
}

static void *
firewire_alloc(u_char type, struct ifnet *ifp)
{
	struct fw_com	*fc;

	fc = malloc(sizeof(struct fw_com), M_FWCOM, M_WAITOK | M_ZERO);
	fc->fc_ifp = ifp;

	return (fc);
}

static void
firewire_free(void *com, u_char type)
{

	free(com, M_FWCOM);
}

static int
firewire_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		if_register_com_alloc(IFT_IEEE1394,
		    firewire_alloc, firewire_free);
		break;
	case MOD_UNLOAD:
		if_deregister_com_alloc(IFT_IEEE1394);
		break;
	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t firewire_mod = {
	"if_firewire",
	firewire_modevent,
	0
};

DECLARE_MODULE(if_firewire, firewire_mod, SI_SUB_INIT_IF, SI_ORDER_ANY);
MODULE_VERSION(if_firewire, 1);

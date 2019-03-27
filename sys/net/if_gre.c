/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * Copyright (c) 2014, 2018 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Heiko W.Rupp <hwr@pilhuhn.de>
 *
 * IPv6-over-GRE contributed by Gert Doering <gert@greenie.muc.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $NetBSD: if_gre.c,v 1.49 2003/12/11 00:22:29 itojun Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/vnet.h>
#include <net/route.h>

#include <netinet/in.h>
#ifdef INET
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#endif

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#endif

#include <netinet/ip_encap.h>
#include <net/bpf.h>
#include <net/if_gre.h>

#include <machine/in_cksum.h>
#include <security/mac/mac_framework.h>

#define	GREMTU			1476

static const char grename[] = "gre";
MALLOC_DEFINE(M_GRE, grename, "Generic Routing Encapsulation");

static struct sx gre_ioctl_sx;
SX_SYSINIT(gre_ioctl_sx, &gre_ioctl_sx, "gre_ioctl");

static int	gre_clone_create(struct if_clone *, int, caddr_t);
static void	gre_clone_destroy(struct ifnet *);
VNET_DEFINE_STATIC(struct if_clone *, gre_cloner);
#define	V_gre_cloner	VNET(gre_cloner)

static void	gre_qflush(struct ifnet *);
static int	gre_transmit(struct ifnet *, struct mbuf *);
static int	gre_ioctl(struct ifnet *, u_long, caddr_t);
static int	gre_output(struct ifnet *, struct mbuf *,
		    const struct sockaddr *, struct route *);
static void	gre_delete_tunnel(struct gre_softc *);

SYSCTL_DECL(_net_link);
static SYSCTL_NODE(_net_link, IFT_TUNNEL, gre, CTLFLAG_RW, 0,
    "Generic Routing Encapsulation");
#ifndef MAX_GRE_NEST
/*
 * This macro controls the default upper limitation on nesting of gre tunnels.
 * Since, setting a large value to this macro with a careless configuration
 * may introduce system crash, we don't allow any nestings by default.
 * If you need to configure nested gre tunnels, you can define this macro
 * in your kernel configuration file.  However, if you do so, please be
 * careful to configure the tunnels so that it won't make a loop.
 */
#define MAX_GRE_NEST 1
#endif

VNET_DEFINE_STATIC(int, max_gre_nesting) = MAX_GRE_NEST;
#define	V_max_gre_nesting	VNET(max_gre_nesting)
SYSCTL_INT(_net_link_gre, OID_AUTO, max_nesting, CTLFLAG_RW | CTLFLAG_VNET,
    &VNET_NAME(max_gre_nesting), 0, "Max nested tunnels");

static void
vnet_gre_init(const void *unused __unused)
{

	V_gre_cloner = if_clone_simple(grename, gre_clone_create,
	    gre_clone_destroy, 0);
#ifdef INET
	in_gre_init();
#endif
#ifdef INET6
	in6_gre_init();
#endif
}
VNET_SYSINIT(vnet_gre_init, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_gre_init, NULL);

static void
vnet_gre_uninit(const void *unused __unused)
{

	if_clone_detach(V_gre_cloner);
#ifdef INET
	in_gre_uninit();
#endif
#ifdef INET6
	in6_gre_uninit();
#endif
}
VNET_SYSUNINIT(vnet_gre_uninit, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_gre_uninit, NULL);

static int
gre_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct gre_softc *sc;

	sc = malloc(sizeof(struct gre_softc), M_GRE, M_WAITOK | M_ZERO);
	sc->gre_fibnum = curthread->td_proc->p_fibnum;
	GRE2IFP(sc) = if_alloc(IFT_TUNNEL);
	GRE2IFP(sc)->if_softc = sc;
	if_initname(GRE2IFP(sc), grename, unit);

	GRE2IFP(sc)->if_mtu = GREMTU;
	GRE2IFP(sc)->if_flags = IFF_POINTOPOINT|IFF_MULTICAST;
	GRE2IFP(sc)->if_output = gre_output;
	GRE2IFP(sc)->if_ioctl = gre_ioctl;
	GRE2IFP(sc)->if_transmit = gre_transmit;
	GRE2IFP(sc)->if_qflush = gre_qflush;
	GRE2IFP(sc)->if_capabilities |= IFCAP_LINKSTATE;
	GRE2IFP(sc)->if_capenable |= IFCAP_LINKSTATE;
	if_attach(GRE2IFP(sc));
	bpfattach(GRE2IFP(sc), DLT_NULL, sizeof(u_int32_t));
	return (0);
}

static void
gre_clone_destroy(struct ifnet *ifp)
{
	struct gre_softc *sc;

	sx_xlock(&gre_ioctl_sx);
	sc = ifp->if_softc;
	gre_delete_tunnel(sc);
	bpfdetach(ifp);
	if_detach(ifp);
	ifp->if_softc = NULL;
	sx_xunlock(&gre_ioctl_sx);

	GRE_WAIT();
	if_free(ifp);
	free(sc, M_GRE);
}

static int
gre_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	struct gre_softc *sc;
	uint32_t opt;
	int error;

	switch (cmd) {
	case SIOCSIFMTU:
		 /* XXX: */
		if (ifr->ifr_mtu < 576)
			return (EINVAL);
		ifp->if_mtu = ifr->ifr_mtu;
		return (0);
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
	case SIOCSIFFLAGS:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		return (0);
	case GRESADDRS:
	case GRESADDRD:
	case GREGADDRS:
	case GREGADDRD:
	case GRESPROTO:
	case GREGPROTO:
		return (EOPNOTSUPP);
	}
	sx_xlock(&gre_ioctl_sx);
	sc = ifp->if_softc;
	if (sc == NULL) {
		error = ENXIO;
		goto end;
	}
	error = 0;
	switch (cmd) {
	case SIOCDIFPHYADDR:
		if (sc->gre_family == 0)
			break;
		gre_delete_tunnel(sc);
		break;
#ifdef INET
	case SIOCSIFPHYADDR:
	case SIOCGIFPSRCADDR:
	case SIOCGIFPDSTADDR:
		error = in_gre_ioctl(sc, cmd, data);
		break;
#endif
#ifdef INET6
	case SIOCSIFPHYADDR_IN6:
	case SIOCGIFPSRCADDR_IN6:
	case SIOCGIFPDSTADDR_IN6:
		error = in6_gre_ioctl(sc, cmd, data);
		break;
#endif
	case SIOCGTUNFIB:
		ifr->ifr_fib = sc->gre_fibnum;
		break;
	case SIOCSTUNFIB:
		if ((error = priv_check(curthread, PRIV_NET_GRE)) != 0)
			break;
		if (ifr->ifr_fib >= rt_numfibs)
			error = EINVAL;
		else
			sc->gre_fibnum = ifr->ifr_fib;
		break;
	case GRESKEY:
	case GRESOPTS:
		if ((error = priv_check(curthread, PRIV_NET_GRE)) != 0)
			break;
		if ((error = copyin(ifr_data_get_ptr(ifr), &opt,
		    sizeof(opt))) != 0)
			break;
		if (cmd == GRESKEY) {
			if (sc->gre_key == opt)
				break;
		} else if (cmd == GRESOPTS) {
			if (opt & ~GRE_OPTMASK) {
				error = EINVAL;
				break;
			}
			if (sc->gre_options == opt)
				break;
		}
		switch (sc->gre_family) {
#ifdef INET
		case AF_INET:
			in_gre_setopts(sc, cmd, opt);
			break;
#endif
#ifdef INET6
		case AF_INET6:
			in6_gre_setopts(sc, cmd, opt);
			break;
#endif
		default:
			if (cmd == GRESKEY)
				sc->gre_key = opt;
			else
				sc->gre_options = opt;
			break;
		}
		/*
		 * XXX: Do we need to initiate change of interface
		 * state here?
		 */
		break;
	case GREGKEY:
		error = copyout(&sc->gre_key, ifr_data_get_ptr(ifr),
		    sizeof(sc->gre_key));
		break;
	case GREGOPTS:
		error = copyout(&sc->gre_options, ifr_data_get_ptr(ifr),
		    sizeof(sc->gre_options));
		break;
	default:
		error = EINVAL;
		break;
	}
	if (error == 0 && sc->gre_family != 0) {
		if (
#ifdef INET
		    cmd == SIOCSIFPHYADDR ||
#endif
#ifdef INET6
		    cmd == SIOCSIFPHYADDR_IN6 ||
#endif
		    0) {
			if_link_state_change(ifp, LINK_STATE_UP);
		}
	}
end:
	sx_xunlock(&gre_ioctl_sx);
	return (error);
}

static void
gre_delete_tunnel(struct gre_softc *sc)
{

	sx_assert(&gre_ioctl_sx, SA_XLOCKED);
	if (sc->gre_family != 0) {
		CK_LIST_REMOVE(sc, chain);
		CK_LIST_REMOVE(sc, srchash);
		GRE_WAIT();
		free(sc->gre_hdr, M_GRE);
		sc->gre_family = 0;
	}
	GRE2IFP(sc)->if_drv_flags &= ~IFF_DRV_RUNNING;
	if_link_state_change(GRE2IFP(sc), LINK_STATE_DOWN);
}

struct gre_list *
gre_hashinit(void)
{
	struct gre_list *hash;
	int i;

	hash = malloc(sizeof(struct gre_list) * GRE_HASH_SIZE,
	    M_GRE, M_WAITOK);
	for (i = 0; i < GRE_HASH_SIZE; i++)
		CK_LIST_INIT(&hash[i]);

	return (hash);
}

void
gre_hashdestroy(struct gre_list *hash)
{

	free(hash, M_GRE);
}

void
gre_updatehdr(struct gre_softc *sc, struct grehdr *gh)
{
	uint32_t *opts;
	uint16_t flags;

	sx_assert(&gre_ioctl_sx, SA_XLOCKED);

	flags = 0;
	opts = gh->gre_opts;
	if (sc->gre_options & GRE_ENABLE_CSUM) {
		flags |= GRE_FLAGS_CP;
		sc->gre_hlen += 2 * sizeof(uint16_t);
		*opts++ = 0;
	}
	if (sc->gre_key != 0) {
		flags |= GRE_FLAGS_KP;
		sc->gre_hlen += sizeof(uint32_t);
		*opts++ = htonl(sc->gre_key);
	}
	if (sc->gre_options & GRE_ENABLE_SEQ) {
		flags |= GRE_FLAGS_SP;
		sc->gre_hlen += sizeof(uint32_t);
		*opts++ = 0;
	} else
		sc->gre_oseq = 0;
	gh->gre_flags = htons(flags);
}

int
gre_input(struct mbuf *m, int off, int proto, void *arg)
{
	struct gre_softc *sc = arg;
	struct grehdr *gh;
	struct ifnet *ifp;
	uint32_t *opts;
#ifdef notyet
	uint32_t key;
#endif
	uint16_t flags;
	int hlen, isr, af;

	ifp = GRE2IFP(sc);
	hlen = off + sizeof(struct grehdr) + 4 * sizeof(uint32_t);
	if (m->m_pkthdr.len < hlen)
		goto drop;
	if (m->m_len < hlen) {
		m = m_pullup(m, hlen);
		if (m == NULL)
			goto drop;
	}
	gh = (struct grehdr *)mtodo(m, off);
	flags = ntohs(gh->gre_flags);
	if (flags & ~GRE_FLAGS_MASK)
		goto drop;
	opts = gh->gre_opts;
	hlen = 2 * sizeof(uint16_t);
	if (flags & GRE_FLAGS_CP) {
		/* reserved1 field must be zero */
		if (((uint16_t *)opts)[1] != 0)
			goto drop;
		if (in_cksum_skip(m, m->m_pkthdr.len, off) != 0)
			goto drop;
		hlen += 2 * sizeof(uint16_t);
		opts++;
	}
	if (flags & GRE_FLAGS_KP) {
#ifdef notyet
        /* 
         * XXX: The current implementation uses the key only for outgoing
         * packets. But we can check the key value here, or even in the
         * encapcheck function.
         */
		key = ntohl(*opts);
#endif
		hlen += sizeof(uint32_t);
		opts++;
    }
#ifdef notyet
	} else
		key = 0;

	if (sc->gre_key != 0 && (key != sc->gre_key || key != 0))
		goto drop;
#endif
	if (flags & GRE_FLAGS_SP) {
#ifdef notyet
		seq = ntohl(*opts);
#endif
		hlen += sizeof(uint32_t);
	}
	switch (ntohs(gh->gre_proto)) {
	case ETHERTYPE_WCCP:
		/*
		 * For WCCP skip an additional 4 bytes if after GRE header
		 * doesn't follow an IP header.
		 */
		if (flags == 0 && (*(uint8_t *)gh->gre_opts & 0xF0) != 0x40)
			hlen += sizeof(uint32_t);
		/* FALLTHROUGH */
	case ETHERTYPE_IP:
		isr = NETISR_IP;
		af = AF_INET;
		break;
	case ETHERTYPE_IPV6:
		isr = NETISR_IPV6;
		af = AF_INET6;
		break;
	default:
		goto drop;
	}
	m_adj(m, off + hlen);
	m_clrprotoflags(m);
	m->m_pkthdr.rcvif = ifp;
	M_SETFIB(m, ifp->if_fib);
#ifdef MAC
	mac_ifnet_create_mbuf(ifp, m);
#endif
	BPF_MTAP2(ifp, &af, sizeof(af), m);
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
	if ((ifp->if_flags & IFF_MONITOR) != 0)
		m_freem(m);
	else
		netisr_dispatch(isr, m);
	return (IPPROTO_DONE);
drop:
	if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
	m_freem(m);
	return (IPPROTO_DONE);
}

static int
gre_output(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
   struct route *ro)
{
	uint32_t af;

	if (dst->sa_family == AF_UNSPEC)
		bcopy(dst->sa_data, &af, sizeof(af));
	else
		af = dst->sa_family;
	/*
	 * Now save the af in the inbound pkt csum data, this is a cheat since
	 * we are using the inbound csum_data field to carry the af over to
	 * the gre_transmit() routine, avoiding using yet another mtag.
	 */
	m->m_pkthdr.csum_data = af;
	return (ifp->if_transmit(ifp, m));
}

static void
gre_setseqn(struct grehdr *gh, uint32_t seq)
{
	uint32_t *opts;
	uint16_t flags;

	opts = gh->gre_opts;
	flags = ntohs(gh->gre_flags);
	KASSERT((flags & GRE_FLAGS_SP) != 0,
	    ("gre_setseqn called, but GRE_FLAGS_SP isn't set "));
	if (flags & GRE_FLAGS_CP)
		opts++;
	if (flags & GRE_FLAGS_KP)
		opts++;
	*opts = htonl(seq);
}

#define	MTAG_GRE	1307983903
static int
gre_transmit(struct ifnet *ifp, struct mbuf *m)
{
	GRE_RLOCK_TRACKER;
	struct gre_softc *sc;
	struct grehdr *gh;
	uint32_t af;
	int error, len;
	uint16_t proto;

	len = 0;
	GRE_RLOCK();
#ifdef MAC
	error = mac_ifnet_check_transmit(ifp, m);
	if (error) {
		m_freem(m);
		goto drop;
	}
#endif
	error = ENETDOWN;
	sc = ifp->if_softc;
	if ((ifp->if_flags & IFF_MONITOR) != 0 ||
	    (ifp->if_flags & IFF_UP) == 0 ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    sc->gre_family == 0 ||
	    (error = if_tunnel_check_nesting(ifp, m, MTAG_GRE,
		V_max_gre_nesting)) != 0) {
		m_freem(m);
		goto drop;
	}
	af = m->m_pkthdr.csum_data;
	BPF_MTAP2(ifp, &af, sizeof(af), m);
	m->m_flags &= ~(M_BCAST|M_MCAST);
	M_SETFIB(m, sc->gre_fibnum);
	M_PREPEND(m, sc->gre_hlen, M_NOWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto drop;
	}
	bcopy(sc->gre_hdr, mtod(m, void *), sc->gre_hlen);
	/* Determine GRE proto */
	switch (af) {
#ifdef INET
	case AF_INET:
		proto = htons(ETHERTYPE_IP);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		proto = htons(ETHERTYPE_IPV6);
		break;
#endif
	default:
		m_freem(m);
		error = ENETDOWN;
		goto drop;
	}
	/* Determine offset of GRE header */
	switch (sc->gre_family) {
#ifdef INET
	case AF_INET:
		len = sizeof(struct ip);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		len = sizeof(struct ip6_hdr);
		break;
#endif
	default:
		m_freem(m);
		error = ENETDOWN;
		goto drop;
	}
	gh = (struct grehdr *)mtodo(m, len);
	gh->gre_proto = proto;
	if (sc->gre_options & GRE_ENABLE_SEQ)
		gre_setseqn(gh, sc->gre_oseq++);
	if (sc->gre_options & GRE_ENABLE_CSUM) {
		*(uint16_t *)gh->gre_opts = in_cksum_skip(m,
		    m->m_pkthdr.len, len);
	}
	len = m->m_pkthdr.len - len;
	switch (sc->gre_family) {
#ifdef INET
	case AF_INET:
		error = in_gre_output(m, af, sc->gre_hlen);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		error = in6_gre_output(m, af, sc->gre_hlen);
		break;
#endif
	default:
		m_freem(m);
		error = ENETDOWN;
	}
drop:
	if (error)
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	else {
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
	}
	GRE_RUNLOCK();
	return (error);
}

static void
gre_qflush(struct ifnet *ifp __unused)
{

}

static int
gremodevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
	case MOD_UNLOAD:
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t gre_mod = {
	"if_gre",
	gremodevent,
	0
};

DECLARE_MODULE(if_gre, gre_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(if_gre, 1);

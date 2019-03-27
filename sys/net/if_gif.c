/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * Copyright (c) 2018 Andrey V. Elsukov <ae@FreeBSD.org>
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
 *
 *	$KAME: if_gif.c,v 1.87 2001/10/19 08:50:27 itojun Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sx.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <machine/cpu.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/bpf.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_ecn.h>
#ifdef	INET
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#endif	/* INET */

#ifdef INET6
#ifndef INET
#include <netinet/in.h>
#endif
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_ecn.h>
#include <netinet6/ip6_var.h>
#endif /* INET6 */

#include <netinet/ip_encap.h>
#include <net/ethernet.h>
#include <net/if_bridgevar.h>
#include <net/if_gif.h>

#include <security/mac/mac_framework.h>

static const char gifname[] = "gif";

MALLOC_DEFINE(M_GIF, "gif", "Generic Tunnel Interface");
static struct sx gif_ioctl_sx;
SX_SYSINIT(gif_ioctl_sx, &gif_ioctl_sx, "gif_ioctl");

void	(*ng_gif_input_p)(struct ifnet *ifp, struct mbuf **mp, int af);
void	(*ng_gif_input_orphan_p)(struct ifnet *ifp, struct mbuf *m, int af);
void	(*ng_gif_attach_p)(struct ifnet *ifp);
void	(*ng_gif_detach_p)(struct ifnet *ifp);

static void	gif_delete_tunnel(struct gif_softc *);
static int	gif_ioctl(struct ifnet *, u_long, caddr_t);
static int	gif_transmit(struct ifnet *, struct mbuf *);
static void	gif_qflush(struct ifnet *);
static int	gif_clone_create(struct if_clone *, int, caddr_t);
static void	gif_clone_destroy(struct ifnet *);
VNET_DEFINE_STATIC(struct if_clone *, gif_cloner);
#define	V_gif_cloner	VNET(gif_cloner)

SYSCTL_DECL(_net_link);
static SYSCTL_NODE(_net_link, IFT_GIF, gif, CTLFLAG_RW, 0,
    "Generic Tunnel Interface");
#ifndef MAX_GIF_NEST
/*
 * This macro controls the default upper limitation on nesting of gif tunnels.
 * Since, setting a large value to this macro with a careless configuration
 * may introduce system crash, we don't allow any nestings by default.
 * If you need to configure nested gif tunnels, you can define this macro
 * in your kernel configuration file.  However, if you do so, please be
 * careful to configure the tunnels so that it won't make a loop.
 */
#define MAX_GIF_NEST 1
#endif
VNET_DEFINE_STATIC(int, max_gif_nesting) = MAX_GIF_NEST;
#define	V_max_gif_nesting	VNET(max_gif_nesting)
SYSCTL_INT(_net_link_gif, OID_AUTO, max_nesting, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(max_gif_nesting), 0, "Max nested tunnels");

static int
gif_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct gif_softc *sc;

	sc = malloc(sizeof(struct gif_softc), M_GIF, M_WAITOK | M_ZERO);
	sc->gif_fibnum = curthread->td_proc->p_fibnum;
	GIF2IFP(sc) = if_alloc(IFT_GIF);
	GIF2IFP(sc)->if_softc = sc;
	if_initname(GIF2IFP(sc), gifname, unit);

	GIF2IFP(sc)->if_addrlen = 0;
	GIF2IFP(sc)->if_mtu    = GIF_MTU;
	GIF2IFP(sc)->if_flags  = IFF_POINTOPOINT | IFF_MULTICAST;
	GIF2IFP(sc)->if_ioctl  = gif_ioctl;
	GIF2IFP(sc)->if_transmit = gif_transmit;
	GIF2IFP(sc)->if_qflush = gif_qflush;
	GIF2IFP(sc)->if_output = gif_output;
	GIF2IFP(sc)->if_capabilities |= IFCAP_LINKSTATE;
	GIF2IFP(sc)->if_capenable |= IFCAP_LINKSTATE;
	if_attach(GIF2IFP(sc));
	bpfattach(GIF2IFP(sc), DLT_NULL, sizeof(u_int32_t));
	if (ng_gif_attach_p != NULL)
		(*ng_gif_attach_p)(GIF2IFP(sc));

	return (0);
}

static void
gif_clone_destroy(struct ifnet *ifp)
{
	struct gif_softc *sc;

	sx_xlock(&gif_ioctl_sx);
	sc = ifp->if_softc;
	gif_delete_tunnel(sc);
	if (ng_gif_detach_p != NULL)
		(*ng_gif_detach_p)(ifp);
	bpfdetach(ifp);
	if_detach(ifp);
	ifp->if_softc = NULL;
	sx_xunlock(&gif_ioctl_sx);

	GIF_WAIT();
	if_free(ifp);
	free(sc, M_GIF);
}

static void
vnet_gif_init(const void *unused __unused)
{

	V_gif_cloner = if_clone_simple(gifname, gif_clone_create,
	    gif_clone_destroy, 0);
#ifdef INET
	in_gif_init();
#endif
#ifdef INET6
	in6_gif_init();
#endif
}
VNET_SYSINIT(vnet_gif_init, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_gif_init, NULL);

static void
vnet_gif_uninit(const void *unused __unused)
{

	if_clone_detach(V_gif_cloner);
#ifdef INET
	in_gif_uninit();
#endif
#ifdef INET6
	in6_gif_uninit();
#endif
}
VNET_SYSUNINIT(vnet_gif_uninit, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_gif_uninit, NULL);

static int
gifmodevent(module_t mod, int type, void *data)
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

static moduledata_t gif_mod = {
	"if_gif",
	gifmodevent,
	0
};

DECLARE_MODULE(if_gif, gif_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(if_gif, 1);

struct gif_list *
gif_hashinit(void)
{
	struct gif_list *hash;
	int i;

	hash = malloc(sizeof(struct gif_list) * GIF_HASH_SIZE,
	    M_GIF, M_WAITOK);
	for (i = 0; i < GIF_HASH_SIZE; i++)
		CK_LIST_INIT(&hash[i]);

	return (hash);
}

void
gif_hashdestroy(struct gif_list *hash)
{

	free(hash, M_GIF);
}

#define	MTAG_GIF	1080679712
static int
gif_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct gif_softc *sc;
	struct etherip_header *eth;
#ifdef INET
	struct ip *ip;
#endif
#ifdef INET6
	struct ip6_hdr *ip6;
	uint32_t t;
#endif
	uint32_t af;
	uint8_t proto, ecn;
	int error;

	GIF_RLOCK();
#ifdef MAC
	error = mac_ifnet_check_transmit(ifp, m);
	if (error) {
		m_freem(m);
		goto err;
	}
#endif
	error = ENETDOWN;
	sc = ifp->if_softc;
	if ((ifp->if_flags & IFF_MONITOR) != 0 ||
	    (ifp->if_flags & IFF_UP) == 0 ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    sc->gif_family == 0 ||
	    (error = if_tunnel_check_nesting(ifp, m, MTAG_GIF,
		V_max_gif_nesting)) != 0) {
		m_freem(m);
		goto err;
	}
	/* Now pull back the af that we stashed in the csum_data. */
	if (ifp->if_bridge)
		af = AF_LINK;
	else
		af = m->m_pkthdr.csum_data;
	m->m_flags &= ~(M_BCAST|M_MCAST);
	M_SETFIB(m, sc->gif_fibnum);
	BPF_MTAP2(ifp, &af, sizeof(af), m);
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_OBYTES, m->m_pkthdr.len);
	/* inner AF-specific encapsulation */
	ecn = 0;
	switch (af) {
#ifdef INET
	case AF_INET:
		proto = IPPROTO_IPV4;
		if (m->m_len < sizeof(struct ip))
			m = m_pullup(m, sizeof(struct ip));
		if (m == NULL) {
			error = ENOBUFS;
			goto err;
		}
		ip = mtod(m, struct ip *);
		ip_ecn_ingress((ifp->if_flags & IFF_LINK1) ? ECN_ALLOWED:
		    ECN_NOCARE, &ecn, &ip->ip_tos);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		proto = IPPROTO_IPV6;
		if (m->m_len < sizeof(struct ip6_hdr))
			m = m_pullup(m, sizeof(struct ip6_hdr));
		if (m == NULL) {
			error = ENOBUFS;
			goto err;
		}
		t = 0;
		ip6 = mtod(m, struct ip6_hdr *);
		ip6_ecn_ingress((ifp->if_flags & IFF_LINK1) ? ECN_ALLOWED:
		    ECN_NOCARE, &t, &ip6->ip6_flow);
		ecn = (ntohl(t) >> 20) & 0xff;
		break;
#endif
	case AF_LINK:
		proto = IPPROTO_ETHERIP;
		M_PREPEND(m, sizeof(struct etherip_header), M_NOWAIT);
		if (m == NULL) {
			error = ENOBUFS;
			goto err;
		}
		eth = mtod(m, struct etherip_header *);
		eth->eip_resvh = 0;
		eth->eip_ver = ETHERIP_VERSION;
		eth->eip_resvl = 0;
		break;
	default:
		error = EAFNOSUPPORT;
		m_freem(m);
		goto err;
	}
	/* XXX should we check if our outer source is legal? */
	/* dispatch to output logic based on outer AF */
	switch (sc->gif_family) {
#ifdef INET
	case AF_INET:
		error = in_gif_output(ifp, m, proto, ecn);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		error = in6_gif_output(ifp, m, proto, ecn);
		break;
#endif
	default:
		m_freem(m);
	}
err:
	if (error)
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	GIF_RUNLOCK();
	return (error);
}

static void
gif_qflush(struct ifnet *ifp __unused)
{

}


int
gif_output(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
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
	 * the gif_transmit() routine, avoiding using yet another mtag.
	 */
	m->m_pkthdr.csum_data = af;
	return (ifp->if_transmit(ifp, m));
}

void
gif_input(struct mbuf *m, struct ifnet *ifp, int proto, uint8_t ecn)
{
	struct etherip_header *eip;
#ifdef INET
	struct ip *ip;
#endif
#ifdef INET6
	struct ip6_hdr *ip6;
	uint32_t t;
#endif
	struct ether_header *eh;
	struct ifnet *oldifp;
	int isr, n, af;

	if (ifp == NULL) {
		/* just in case */
		m_freem(m);
		return;
	}
	m->m_pkthdr.rcvif = ifp;
	m_clrprotoflags(m);
	switch (proto) {
#ifdef INET
	case IPPROTO_IPV4:
		af = AF_INET;
		if (m->m_len < sizeof(struct ip))
			m = m_pullup(m, sizeof(struct ip));
		if (m == NULL)
			goto drop;
		ip = mtod(m, struct ip *);
		if (ip_ecn_egress((ifp->if_flags & IFF_LINK1) ? ECN_ALLOWED:
		    ECN_NOCARE, &ecn, &ip->ip_tos) == 0) {
			m_freem(m);
			goto drop;
		}
		break;
#endif
#ifdef INET6
	case IPPROTO_IPV6:
		af = AF_INET6;
		if (m->m_len < sizeof(struct ip6_hdr))
			m = m_pullup(m, sizeof(struct ip6_hdr));
		if (m == NULL)
			goto drop;
		t = htonl((uint32_t)ecn << 20);
		ip6 = mtod(m, struct ip6_hdr *);
		if (ip6_ecn_egress((ifp->if_flags & IFF_LINK1) ? ECN_ALLOWED:
		    ECN_NOCARE, &t, &ip6->ip6_flow) == 0) {
			m_freem(m);
			goto drop;
		}
		break;
#endif
	case IPPROTO_ETHERIP:
		af = AF_LINK;
		break;
	default:
		m_freem(m);
		goto drop;
	}

#ifdef MAC
	mac_ifnet_create_mbuf(ifp, m);
#endif

	if (bpf_peers_present(ifp->if_bpf)) {
		uint32_t af1 = af;
		bpf_mtap2(ifp->if_bpf, &af1, sizeof(af1), m);
	}

	if ((ifp->if_flags & IFF_MONITOR) != 0) {
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
		m_freem(m);
		return;
	}

	if (ng_gif_input_p != NULL) {
		(*ng_gif_input_p)(ifp, &m, af);
		if (m == NULL)
			goto drop;
	}

	/*
	 * Put the packet to the network layer input queue according to the
	 * specified address family.
	 * Note: older versions of gif_input directly called network layer
	 * input functions, e.g. ip6_input, here.  We changed the policy to
	 * prevent too many recursive calls of such input functions, which
	 * might cause kernel panic.  But the change may introduce another
	 * problem; if the input queue is full, packets are discarded.
	 * The kernel stack overflow really happened, and we believed
	 * queue-full rarely occurs, so we changed the policy.
	 */
	switch (af) {
#ifdef INET
	case AF_INET:
		isr = NETISR_IP;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		isr = NETISR_IPV6;
		break;
#endif
	case AF_LINK:
		n = sizeof(struct etherip_header) +
		    sizeof(struct ether_header);
		if (n > m->m_len)
			m = m_pullup(m, n);
		if (m == NULL)
			goto drop;
		eip = mtod(m, struct etherip_header *);
		if (eip->eip_ver != ETHERIP_VERSION) {
			/* discard unknown versions */
			m_freem(m);
			goto drop;
		}
		m_adj(m, sizeof(struct etherip_header));

		m->m_flags &= ~(M_BCAST|M_MCAST);
		m->m_pkthdr.rcvif = ifp;

		if (ifp->if_bridge) {
			oldifp = ifp;
			eh = mtod(m, struct ether_header *);
			if (ETHER_IS_MULTICAST(eh->ether_dhost)) {
				if (ETHER_IS_BROADCAST(eh->ether_dhost))
					m->m_flags |= M_BCAST;
				else
					m->m_flags |= M_MCAST;
				if_inc_counter(ifp, IFCOUNTER_IMCASTS, 1);
			}
			BRIDGE_INPUT(ifp, m);

			if (m != NULL && ifp != oldifp) {
				/*
				 * The bridge gave us back itself or one of the
				 * members for which the frame is addressed.
				 */
				ether_demux(ifp, m);
				return;
			}
		}
		if (m != NULL)
			m_freem(m);
		return;

	default:
		if (ng_gif_input_orphan_p != NULL)
			(*ng_gif_input_orphan_p)(ifp, m, af);
		else
			m_freem(m);
		return;
	}

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
	M_SETFIB(m, ifp->if_fib);
	netisr_dispatch(isr, m);
	return;
drop:
	if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
}

static int
gif_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq*)data;
	struct gif_softc *sc;
	u_int options;
	int error;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
	case SIOCGIFMTU:
	case SIOCSIFFLAGS:
		return (0);
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < GIF_MTU_MIN ||
		    ifr->ifr_mtu > GIF_MTU_MAX)
			return (EINVAL);
		else
			ifp->if_mtu = ifr->ifr_mtu;
		return (0);
	}
	sx_xlock(&gif_ioctl_sx);
	sc = ifp->if_softc;
	if (sc == NULL) {
		error = ENXIO;
		goto bad;
	}
	error = 0;
	switch (cmd) {
	case SIOCDIFPHYADDR:
		if (sc->gif_family == 0)
			break;
		gif_delete_tunnel(sc);
		break;
#ifdef INET
	case SIOCSIFPHYADDR:
	case SIOCGIFPSRCADDR:
	case SIOCGIFPDSTADDR:
		error = in_gif_ioctl(sc, cmd, data);
		break;
#endif
#ifdef INET6
	case SIOCSIFPHYADDR_IN6:
	case SIOCGIFPSRCADDR_IN6:
	case SIOCGIFPDSTADDR_IN6:
		error = in6_gif_ioctl(sc, cmd, data);
		break;
#endif
	case SIOCGTUNFIB:
		ifr->ifr_fib = sc->gif_fibnum;
		break;
	case SIOCSTUNFIB:
		if ((error = priv_check(curthread, PRIV_NET_GIF)) != 0)
			break;
		if (ifr->ifr_fib >= rt_numfibs)
			error = EINVAL;
		else
			sc->gif_fibnum = ifr->ifr_fib;
		break;
	case GIFGOPTS:
		options = sc->gif_options;
		error = copyout(&options, ifr_data_get_ptr(ifr),
		    sizeof(options));
		break;
	case GIFSOPTS:
		if ((error = priv_check(curthread, PRIV_NET_GIF)) != 0)
			break;
		error = copyin(ifr_data_get_ptr(ifr), &options,
		    sizeof(options));
		if (error)
			break;
		if (options & ~GIF_OPTMASK) {
			error = EINVAL;
			break;
		}
		if (sc->gif_options != options) {
			switch (sc->gif_family) {
#ifdef INET
			case AF_INET:
				error = in_gif_setopts(sc, options);
				break;
#endif
#ifdef INET6
			case AF_INET6:
				error = in6_gif_setopts(sc, options);
				break;
#endif
			default:
				/* No need to invoke AF-handler */
				sc->gif_options = options;
			}
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	if (error == 0 && sc->gif_family != 0) {
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
bad:
	sx_xunlock(&gif_ioctl_sx);
	return (error);
}

static void
gif_delete_tunnel(struct gif_softc *sc)
{

	sx_assert(&gif_ioctl_sx, SA_XLOCKED);
	if (sc->gif_family != 0) {
		CK_LIST_REMOVE(sc, srchash);
		CK_LIST_REMOVE(sc, chain);
		/* Wait until it become safe to free gif_hdr */
		GIF_WAIT();
		free(sc->gif_hdr, M_GIF);
	}
	sc->gif_family = 0;
	GIF2IFP(sc)->if_drv_flags &= ~IFF_DRV_RUNNING;
	if_link_state_change(GIF2IFP(sc), LINK_STATE_DOWN);
}


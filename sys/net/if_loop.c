/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)if_loop.c	8.2 (Berkeley) 1/9/95
 * $FreeBSD$
 */

/*
 * Loopback interface driver for protocol testing and timing.
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/bpf.h>
#include <net/vnet.h>

#ifdef	INET
#include <netinet/in.h>
#include <netinet/in_var.h>
#endif

#ifdef INET6
#ifndef INET
#include <netinet/in.h>
#endif
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#endif

#include <security/mac/mac_framework.h>

#ifdef TINY_LOMTU
#define	LOMTU	(1024+512)
#elif defined(LARGE_LOMTU)
#define LOMTU	131072
#else
#define LOMTU	16384
#endif

#define	LO_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP | CSUM_SCTP)
#define	LO_CSUM_FEATURES6	(CSUM_TCP_IPV6 | CSUM_UDP_IPV6 | CSUM_SCTP_IPV6)
#define	LO_CSUM_SET		(CSUM_DATA_VALID | CSUM_DATA_VALID_IPV6 | \
				    CSUM_PSEUDO_HDR | \
				    CSUM_IP_CHECKED | CSUM_IP_VALID | \
				    CSUM_SCTP_VALID)

int		loioctl(struct ifnet *, u_long, caddr_t);
int		looutput(struct ifnet *ifp, struct mbuf *m,
		    const struct sockaddr *dst, struct route *ro);
static int	lo_clone_create(struct if_clone *, int, caddr_t);
static void	lo_clone_destroy(struct ifnet *);

VNET_DEFINE(struct ifnet *, loif);	/* Used externally */

#ifdef VIMAGE
VNET_DEFINE_STATIC(struct if_clone *, lo_cloner);
#define	V_lo_cloner		VNET(lo_cloner)
#endif

static struct if_clone *lo_cloner;
static const char loname[] = "lo";

static void
lo_clone_destroy(struct ifnet *ifp)
{

#ifndef VIMAGE
	/* XXX: destroying lo0 will lead to panics. */
	KASSERT(V_loif != ifp, ("%s: destroying lo0", __func__));
#endif

	bpfdetach(ifp);
	if_detach(ifp);
	if_free(ifp);
}

static int
lo_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct ifnet *ifp;

	ifp = if_alloc(IFT_LOOP);
	if (ifp == NULL)
		return (ENOSPC);

	if_initname(ifp, loname, unit);
	ifp->if_mtu = LOMTU;
	ifp->if_flags = IFF_LOOPBACK | IFF_MULTICAST;
	ifp->if_ioctl = loioctl;
	ifp->if_output = looutput;
	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	ifp->if_capabilities = ifp->if_capenable =
	    IFCAP_HWCSUM | IFCAP_HWCSUM_IPV6 | IFCAP_LINKSTATE;
	ifp->if_hwassist = LO_CSUM_FEATURES | LO_CSUM_FEATURES6;
	if_attach(ifp);
	bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));
	if (V_loif == NULL)
		V_loif = ifp;

	return (0);
}

static void
vnet_loif_init(const void *unused __unused)
{

#ifdef VIMAGE
	lo_cloner = if_clone_simple(loname, lo_clone_create, lo_clone_destroy,
	    1);
	V_lo_cloner = lo_cloner;
#else
	lo_cloner = if_clone_simple(loname, lo_clone_create, lo_clone_destroy,
	    1);
#endif
}
VNET_SYSINIT(vnet_loif_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    vnet_loif_init, NULL);

#ifdef VIMAGE
static void
vnet_loif_uninit(const void *unused __unused)
{

	if_clone_detach(V_lo_cloner);
	V_loif = NULL;
}
VNET_SYSUNINIT(vnet_loif_uninit, SI_SUB_INIT_IF, SI_ORDER_SECOND,
    vnet_loif_uninit, NULL);
#endif

static int
loop_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		break;

	case MOD_UNLOAD:
		printf("loop module unload - not possible for this module type\n");
		return (EINVAL);

	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t loop_mod = {
	"if_lo",
	loop_modevent,
	0
};

DECLARE_MODULE(if_lo, loop_mod, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY);

int
looutput(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
    struct route *ro)
{
	u_int32_t af;
#ifdef MAC
	int error;
#endif

	M_ASSERTPKTHDR(m); /* check if we have the packet header */

#ifdef MAC
	error = mac_ifnet_check_transmit(ifp, m);
	if (error) {
		m_freem(m);
		return (error);
	}
#endif

	if (ro != NULL && ro->ro_flags & (RT_REJECT|RT_BLACKHOLE)) {
		m_freem(m);
		return (ro->ro_flags & RT_BLACKHOLE ? 0 : EHOSTUNREACH);
	}

	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_OBYTES, m->m_pkthdr.len);

#ifdef RSS
	M_HASHTYPE_CLEAR(m);
#endif

	/* BPF writes need to be handled specially. */
	if (dst->sa_family == AF_UNSPEC || dst->sa_family == pseudo_AF_HDRCMPLT)
		bcopy(dst->sa_data, &af, sizeof(af));
	else
		af = dst->sa_family;

#if 1	/* XXX */
	switch (af) {
	case AF_INET:
		if (ifp->if_capenable & IFCAP_RXCSUM) {
			m->m_pkthdr.csum_data = 0xffff;
			m->m_pkthdr.csum_flags = LO_CSUM_SET;
		}
		m->m_pkthdr.csum_flags &= ~LO_CSUM_FEATURES;
		break;
	case AF_INET6:
#if 0
		/*
		 * XXX-BZ for now always claim the checksum is good despite
		 * any interface flags.   This is a workaround for 9.1-R and
		 * a proper solution ought to be sought later.
		 */
		if (ifp->if_capenable & IFCAP_RXCSUM_IPV6) {
			m->m_pkthdr.csum_data = 0xffff;
			m->m_pkthdr.csum_flags = LO_CSUM_SET;
		}
#else
		m->m_pkthdr.csum_data = 0xffff;
		m->m_pkthdr.csum_flags = LO_CSUM_SET;
#endif
		m->m_pkthdr.csum_flags &= ~LO_CSUM_FEATURES6;
		break;
	default:
		printf("looutput: af=%d unexpected\n", af);
		m_freem(m);
		return (EAFNOSUPPORT);
	}
#endif
	return (if_simloop(ifp, m, af, 0));
}

/*
 * if_simloop()
 *
 * This function is to support software emulation of hardware loopback,
 * i.e., for interfaces with the IFF_SIMPLEX attribute. Since they can't
 * hear their own broadcasts, we create a copy of the packet that we
 * would normally receive via a hardware loopback.
 *
 * This function expects the packet to include the media header of length hlen.
 */
int
if_simloop(struct ifnet *ifp, struct mbuf *m, int af, int hlen)
{
	int isr;

	M_ASSERTPKTHDR(m);
	m_tag_delete_nonpersistent(m);
	m->m_pkthdr.rcvif = ifp;

#ifdef MAC
	mac_ifnet_create_mbuf(ifp, m);
#endif

	/*
	 * Let BPF see incoming packet in the following manner:
	 *  - Emulated packet loopback for a simplex interface
	 *    (net/if_ethersubr.c)
	 *	-> passes it to ifp's BPF
	 *  - IPv4/v6 multicast packet loopback (netinet(6)/ip(6)_output.c)
	 *	-> not passes it to any BPF
	 *  - Normal packet loopback from myself to myself (net/if_loop.c)
	 *	-> passes to lo0's BPF (even in case of IPv6, where ifp!=lo0)
	 */
	if (hlen > 0) {
		if (bpf_peers_present(ifp->if_bpf)) {
			bpf_mtap(ifp->if_bpf, m);
		}
	} else {
		if (bpf_peers_present(V_loif->if_bpf)) {
			if ((m->m_flags & M_MCAST) == 0 || V_loif == ifp) {
				/* XXX beware sizeof(af) != 4 */
				u_int32_t af1 = af;

				/*
				 * We need to prepend the address family.
				 */
				bpf_mtap2(V_loif->if_bpf, &af1, sizeof(af1), m);
			}
		}
	}

	/* Strip away media header */
	if (hlen > 0) {
		m_adj(m, hlen);
#ifndef __NO_STRICT_ALIGNMENT
		/*
		 * Some archs do not like unaligned data, so
		 * we move data down in the first mbuf.
		 */
		if (mtod(m, vm_offset_t) & 3) {
			KASSERT(hlen >= 3, ("if_simloop: hlen too small"));
			bcopy(m->m_data,
			    (char *)(mtod(m, vm_offset_t)
				- (mtod(m, vm_offset_t) & 3)),
			    m->m_len);
			m->m_data -= (mtod(m,vm_offset_t) & 3);
		}
#endif
	}

	/* Deliver to upper layer protocol */
	switch (af) {
#ifdef INET
	case AF_INET:
		isr = NETISR_IP;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		m->m_flags |= M_LOOP;
		isr = NETISR_IPV6;
		break;
#endif
	default:
		printf("if_simloop: can't handle af=%d\n", af);
		m_freem(m);
		return (EAFNOSUPPORT);
	}
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
	netisr_queue(isr, m);	/* mbuf is free'd on failure. */
	return (0);
}

/*
 * Process an ioctl request.
 */
/* ARGSUSED */
int
loioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0, mask;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		ifp->if_drv_flags |= IFF_DRV_RUNNING;
		if_link_state_change(ifp, LINK_STATE_UP);
		/*
		 * Everything else is done at a higher level.
		 */
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifr == NULL) {
			error = EAFNOSUPPORT;		/* XXX */
			break;
		}
		switch (ifr->ifr_addr.sa_family) {

#ifdef INET
		case AF_INET:
			break;
#endif
#ifdef INET6
		case AF_INET6:
			break;
#endif

		default:
			error = EAFNOSUPPORT;
			break;
		}
		break;

	case SIOCSIFMTU:
		ifp->if_mtu = ifr->ifr_mtu;
		break;

	case SIOCSIFFLAGS:
		if_link_state_change(ifp, (ifp->if_flags & IFF_UP) ?
		    LINK_STATE_UP: LINK_STATE_DOWN);
		break;

	case SIOCSIFCAP:
		mask = ifp->if_capenable ^ ifr->ifr_reqcap;
		if ((mask & IFCAP_RXCSUM) != 0)
			ifp->if_capenable ^= IFCAP_RXCSUM;
		if ((mask & IFCAP_TXCSUM) != 0)
			ifp->if_capenable ^= IFCAP_TXCSUM;
		if ((mask & IFCAP_RXCSUM_IPV6) != 0) {
#if 0
			ifp->if_capenable ^= IFCAP_RXCSUM_IPV6;
#else
			error = EOPNOTSUPP;
			break;
#endif
		}
		if ((mask & IFCAP_TXCSUM_IPV6) != 0) {
#if 0
			ifp->if_capenable ^= IFCAP_TXCSUM_IPV6;
#else
			error = EOPNOTSUPP;
			break;
#endif
		}
		ifp->if_hwassist = 0;
		if (ifp->if_capenable & IFCAP_TXCSUM)
			ifp->if_hwassist = LO_CSUM_FEATURES;
#if 0
		if (ifp->if_capenable & IFCAP_TXCSUM_IPV6)
			ifp->if_hwassist |= LO_CSUM_FEATURES6;
#endif
		break;

	default:
		error = EINVAL;
	}
	return (error);
}

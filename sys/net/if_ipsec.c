/*-
 * Copyright (c) 2016-2018 Yandex LLC
 * Copyright (c) 2016-2018 Andrey V. Elsukov <ae@FreeBSD.org>
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

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fnv_hash.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sx.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/conf.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/if_types.h>
#include <net/bpf.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_encap.h>

#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/scope6_var.h>

#include <netipsec/ipsec.h>
#ifdef INET6
#include <netipsec/ipsec6.h>
#endif

#include <net/if_ipsec.h>
#include <netipsec/key.h>

#include <security/mac/mac_framework.h>

static MALLOC_DEFINE(M_IPSEC, "ipsec", "IPsec Virtual Tunnel Interface");
static const char ipsecname[] = "ipsec";

#if defined(INET) && defined(INET6)
#define	IPSEC_SPCOUNT		4
#else
#define	IPSEC_SPCOUNT		2
#endif

struct ipsec_softc {
	struct ifnet		*ifp;
	struct secpolicy	*sp[IPSEC_SPCOUNT];
	uint32_t		reqid;
	u_int			family;
	u_int			fibnum;

	CK_LIST_ENTRY(ipsec_softc) idhash;
	CK_LIST_ENTRY(ipsec_softc) srchash;
};

#define	IPSEC_RLOCK_TRACKER	struct epoch_tracker ipsec_et
#define	IPSEC_RLOCK()	epoch_enter_preempt(net_epoch_preempt, &ipsec_et)
#define	IPSEC_RUNLOCK()	epoch_exit_preempt(net_epoch_preempt, &ipsec_et)
#define	IPSEC_WAIT()	epoch_wait_preempt(net_epoch_preempt)

#ifndef IPSEC_HASH_SIZE
#define	IPSEC_HASH_SIZE	(1 << 5)
#endif

CK_LIST_HEAD(ipsec_iflist, ipsec_softc);
VNET_DEFINE_STATIC(struct ipsec_iflist *, ipsec_idhtbl) = NULL;
#define	V_ipsec_idhtbl		VNET(ipsec_idhtbl)

#ifdef INET
VNET_DEFINE_STATIC(struct ipsec_iflist *, ipsec4_srchtbl) = NULL;
#define	V_ipsec4_srchtbl	VNET(ipsec4_srchtbl)
static const struct srcaddrtab *ipsec4_srctab = NULL;
#endif

#ifdef INET6
VNET_DEFINE_STATIC(struct ipsec_iflist *, ipsec6_srchtbl) = NULL;
#define	V_ipsec6_srchtbl	VNET(ipsec6_srchtbl)
static const struct srcaddrtab *ipsec6_srctab = NULL;
#endif

static struct ipsec_iflist *
ipsec_idhash(uint32_t id)
{

	return (&V_ipsec_idhtbl[fnv_32_buf(&id, sizeof(id),
	    FNV1_32_INIT) & (IPSEC_HASH_SIZE - 1)]);
}

static struct ipsec_iflist *
ipsec_srchash(const struct sockaddr *sa)
{
	uint32_t hval;

	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		hval = fnv_32_buf(
		    &((const struct sockaddr_in *)sa)->sin_addr.s_addr,
		    sizeof(in_addr_t), FNV1_32_INIT);
		return (&V_ipsec4_srchtbl[hval & (IPSEC_HASH_SIZE - 1)]);
#endif
#ifdef INET6
	case AF_INET6:
		hval = fnv_32_buf(
		    &((const struct sockaddr_in6 *)sa)->sin6_addr,
		    sizeof(struct in6_addr), FNV1_32_INIT);
		return (&V_ipsec6_srchtbl[hval & (IPSEC_HASH_SIZE - 1)]);
#endif
	}
	return (NULL);
}

/*
 * ipsec_ioctl_sx protects from concurrent ioctls.
 */
static struct sx ipsec_ioctl_sx;
SX_SYSINIT(ipsec_ioctl_sx, &ipsec_ioctl_sx, "ipsec_ioctl");

static int	ipsec_init_reqid(struct ipsec_softc *);
static int	ipsec_set_tunnel(struct ipsec_softc *, struct sockaddr *,
    struct sockaddr *, uint32_t);
static void	ipsec_delete_tunnel(struct ipsec_softc *);

static int	ipsec_set_addresses(struct ifnet *, struct sockaddr *,
    struct sockaddr *);
static int	ipsec_set_reqid(struct ipsec_softc *, uint32_t);
static void	ipsec_set_running(struct ipsec_softc *);

static void	ipsec_srcaddr(void *, const struct sockaddr *, int);
static int	ipsec_ioctl(struct ifnet *, u_long, caddr_t);
static int	ipsec_transmit(struct ifnet *, struct mbuf *);
static int	ipsec_output(struct ifnet *, struct mbuf *,
    const struct sockaddr *, struct route *);
static void	ipsec_qflush(struct ifnet *);
static int	ipsec_clone_create(struct if_clone *, int, caddr_t);
static void	ipsec_clone_destroy(struct ifnet *);

VNET_DEFINE_STATIC(struct if_clone *, ipsec_cloner);
#define	V_ipsec_cloner		VNET(ipsec_cloner)

static int
ipsec_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct ipsec_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_IPSEC, M_WAITOK | M_ZERO);
	sc->fibnum = curthread->td_proc->p_fibnum;
	sc->ifp = ifp = if_alloc(IFT_TUNNEL);
	ifp->if_softc = sc;
	if_initname(ifp, ipsecname, unit);

	ifp->if_addrlen = 0;
	ifp->if_mtu = IPSEC_MTU;
	ifp->if_flags  = IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_ioctl  = ipsec_ioctl;
	ifp->if_transmit  = ipsec_transmit;
	ifp->if_qflush  = ipsec_qflush;
	ifp->if_output = ipsec_output;
	if_attach(ifp);
	bpfattach(ifp, DLT_NULL, sizeof(uint32_t));

	return (0);
}

static void
ipsec_clone_destroy(struct ifnet *ifp)
{
	struct ipsec_softc *sc;

	sx_xlock(&ipsec_ioctl_sx);
	sc = ifp->if_softc;
	ipsec_delete_tunnel(sc);
	/*
	 * Delete softc from idhash on interface destroy, since
	 * ipsec_delete_tunnel() keeps reqid unchanged.
	 */
	if (sc->reqid != 0)
		CK_LIST_REMOVE(sc, idhash);
	bpfdetach(ifp);
	if_detach(ifp);
	ifp->if_softc = NULL;
	sx_xunlock(&ipsec_ioctl_sx);

	IPSEC_WAIT();
	if_free(ifp);
	free(sc, M_IPSEC);
}

static struct ipsec_iflist *
ipsec_hashinit(void)
{
	struct ipsec_iflist *hash;
	int i;

	hash = malloc(sizeof(struct ipsec_iflist) * IPSEC_HASH_SIZE,
	    M_IPSEC, M_WAITOK);
	for (i = 0; i < IPSEC_HASH_SIZE; i++)
		CK_LIST_INIT(&hash[i]);

	return (hash);
}

static void
vnet_ipsec_init(const void *unused __unused)
{

	V_ipsec_idhtbl = ipsec_hashinit();
#ifdef INET
	V_ipsec4_srchtbl = ipsec_hashinit();
	if (IS_DEFAULT_VNET(curvnet))
		ipsec4_srctab = ip_encap_register_srcaddr(ipsec_srcaddr,
		    NULL, M_WAITOK);
#endif
#ifdef INET6
	V_ipsec6_srchtbl = ipsec_hashinit();
	if (IS_DEFAULT_VNET(curvnet))
		ipsec6_srctab = ip6_encap_register_srcaddr(ipsec_srcaddr,
		    NULL, M_WAITOK);
#endif
	V_ipsec_cloner = if_clone_simple(ipsecname, ipsec_clone_create,
	    ipsec_clone_destroy, 0);
}
VNET_SYSINIT(vnet_ipsec_init, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_ipsec_init, NULL);

static void
vnet_ipsec_uninit(const void *unused __unused)
{

	if_clone_detach(V_ipsec_cloner);
	free(V_ipsec_idhtbl, M_IPSEC);
	/*
	 * Use V_ipsec_idhtbl pointer as indicator that VNET is going to be
	 * destroyed, it is used by ipsec_srcaddr() callback.
	 */
	V_ipsec_idhtbl = NULL;
	IPSEC_WAIT();

#ifdef INET
	if (IS_DEFAULT_VNET(curvnet))
		ip_encap_unregister_srcaddr(ipsec4_srctab);
	free(V_ipsec4_srchtbl, M_IPSEC);
#endif
#ifdef INET6
	if (IS_DEFAULT_VNET(curvnet))
		ip6_encap_unregister_srcaddr(ipsec6_srctab);
	free(V_ipsec6_srchtbl, M_IPSEC);
#endif
}
VNET_SYSUNINIT(vnet_ipsec_uninit, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_ipsec_uninit, NULL);

static struct secpolicy *
ipsec_getpolicy(struct ipsec_softc *sc, int dir, sa_family_t af)
{

	switch (af) {
#ifdef INET
	case AF_INET:
		return (sc->sp[(dir == IPSEC_DIR_INBOUND ? 0: 1)]);
#endif
#ifdef INET6
	case AF_INET6:
		return (sc->sp[(dir == IPSEC_DIR_INBOUND ? 0: 1)
#ifdef INET
			+ 2
#endif
		]);
#endif
	}
	return (NULL);
}

static struct secasindex *
ipsec_getsaidx(struct ipsec_softc *sc, int dir, sa_family_t af)
{
	struct secpolicy *sp;

	sp = ipsec_getpolicy(sc, dir, af);
	if (sp == NULL)
		return (NULL);
	return (&sp->req[0]->saidx);
}

static int
ipsec_transmit(struct ifnet *ifp, struct mbuf *m)
{
	IPSEC_RLOCK_TRACKER;
	struct ipsec_softc *sc;
	struct secpolicy *sp;
	struct ip *ip;
	uint32_t af;
	int error;

	IPSEC_RLOCK();
#ifdef MAC
	error = mac_ifnet_check_transmit(ifp, m);
	if (error) {
		m_freem(m);
		goto err;
	}
#endif
	error = ENETDOWN;
	sc = ifp->if_softc;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    (ifp->if_flags & IFF_MONITOR) != 0 ||
	    (ifp->if_flags & IFF_UP) == 0 || sc->family == 0) {
		m_freem(m);
		goto err;
	}

	/* Determine address family to correctly handle packet in BPF */
	ip = mtod(m, struct ip *);
	switch (ip->ip_v) {
#ifdef INET
	case IPVERSION:
		af = AF_INET;
		break;
#endif
#ifdef INET6
	case (IPV6_VERSION >> 4):
		af = AF_INET6;
		break;
#endif
	default:
		error = EAFNOSUPPORT;
		m_freem(m);
		goto err;
	}

	/*
	 * Loop prevention.
	 * XXX: for now just check presence of IPSEC_OUT_DONE mbuf tag.
	 *      We can read full chain and compare destination address,
	 *      proto and mode from xform_history with values from softc.
	 */
	if (m_tag_find(m, PACKET_TAG_IPSEC_OUT_DONE, NULL) != NULL) {
		m_freem(m);
		goto err;
	}

	sp = ipsec_getpolicy(sc, IPSEC_DIR_OUTBOUND, af);
	key_addref(sp);
	M_SETFIB(m, sc->fibnum);

	BPF_MTAP2(ifp, &af, sizeof(af), m);
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_OBYTES, m->m_pkthdr.len);

	switch (af) {
#ifdef INET
	case AF_INET:
		error = ipsec4_process_packet(m, sp, NULL);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		error = ipsec6_process_packet(m, sp, NULL);
		break;
#endif
	default:
		panic("%s: unknown address family\n", __func__);
	}
err:
	if (error != 0)
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	IPSEC_RUNLOCK();
	return (error);
}

static void
ipsec_qflush(struct ifnet *ifp __unused)
{

}

static int
ipsec_output(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
	struct route *ro)
{

	return (ifp->if_transmit(ifp, m));
}

int
ipsec_if_input(struct mbuf *m, struct secasvar *sav, uint32_t af)
{
	IPSEC_RLOCK_TRACKER;
	struct secasindex *saidx;
	struct ipsec_softc *sc;
	struct ifnet *ifp;

	if (sav->state != SADB_SASTATE_MATURE &&
	    sav->state != SADB_SASTATE_DYING) {
		m_freem(m);
		return (ENETDOWN);
	}

	if (sav->sah->saidx.mode != IPSEC_MODE_TUNNEL ||
	    sav->sah->saidx.proto != IPPROTO_ESP)
		return (0);

	IPSEC_RLOCK();
	CK_LIST_FOREACH(sc, ipsec_idhash(sav->sah->saidx.reqid), idhash) {
		if (sc->family == 0)
			continue;
		saidx = ipsec_getsaidx(sc, IPSEC_DIR_INBOUND,
		    sav->sah->saidx.src.sa.sa_family);
		/* SA's reqid should match reqid in SP */
		if (saidx == NULL ||
		    sav->sah->saidx.reqid != saidx->reqid)
			continue;
		/* SAH's addresses should match tunnel endpoints. */
		if (key_sockaddrcmp(&sav->sah->saidx.dst.sa,
		    &saidx->dst.sa, 0) != 0)
			continue;
		if (key_sockaddrcmp(&sav->sah->saidx.src.sa,
		    &saidx->src.sa, 0) == 0)
			break;
	}
	if (sc == NULL) {
		IPSEC_RUNLOCK();
		/* Tunnel was not found. Nothing to do. */
		return (0);
	}
	ifp = sc->ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    (ifp->if_flags & IFF_UP) == 0) {
		IPSEC_RUNLOCK();
		m_freem(m);
		return (ENETDOWN);
	}
	/*
	 * We found matching and working tunnel.
	 * Set its ifnet as receiving interface.
	 */
	m->m_pkthdr.rcvif = ifp;

	m_clrprotoflags(m);
	M_SETFIB(m, ifp->if_fib);
	BPF_MTAP2(ifp, &af, sizeof(af), m);
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
	if ((ifp->if_flags & IFF_MONITOR) != 0) {
		IPSEC_RUNLOCK();
		m_freem(m);
		return (ENETDOWN);
	}
	IPSEC_RUNLOCK();
	return (0);
}

static int
ipsec_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq*)data;
	struct sockaddr *dst, *src;
	struct ipsec_softc *sc;
	struct secasindex *saidx;
#ifdef INET
	struct sockaddr_in *sin = NULL;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6 = NULL;
#endif
	uint32_t reqid;
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
		if (ifr->ifr_mtu < IPSEC_MTU_MIN ||
		    ifr->ifr_mtu > IPSEC_MTU_MAX)
			return (EINVAL);
		else
			ifp->if_mtu = ifr->ifr_mtu;
		return (0);
	}
	sx_xlock(&ipsec_ioctl_sx);
	sc = ifp->if_softc;
	/* Check that softc is still here */
	if (sc == NULL) {
		error = ENXIO;
		goto bad;
	}
	error = 0;
	switch (cmd) {
	case SIOCSIFPHYADDR:
#ifdef INET6
	case SIOCSIFPHYADDR_IN6:
#endif
		error = EINVAL;
		switch (cmd) {
#ifdef INET
		case SIOCSIFPHYADDR:
			src = (struct sockaddr *)
				&(((struct in_aliasreq *)data)->ifra_addr);
			dst = (struct sockaddr *)
				&(((struct in_aliasreq *)data)->ifra_dstaddr);
			break;
#endif
#ifdef INET6
		case SIOCSIFPHYADDR_IN6:
			src = (struct sockaddr *)
				&(((struct in6_aliasreq *)data)->ifra_addr);
			dst = (struct sockaddr *)
				&(((struct in6_aliasreq *)data)->ifra_dstaddr);
			break;
#endif
		default:
			goto bad;
		}
		/* sa_family must be equal */
		if (src->sa_family != dst->sa_family ||
		    src->sa_len != dst->sa_len)
			goto bad;

		/* validate sa_len */
		switch (src->sa_family) {
#ifdef INET
		case AF_INET:
			if (src->sa_len != sizeof(struct sockaddr_in))
				goto bad;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			if (src->sa_len != sizeof(struct sockaddr_in6))
				goto bad;
			break;
#endif
		default:
			error = EAFNOSUPPORT;
			goto bad;
		}
		/* check sa_family looks sane for the cmd */
		error = EAFNOSUPPORT;
		switch (cmd) {
#ifdef INET
		case SIOCSIFPHYADDR:
			if (src->sa_family == AF_INET)
				break;
			goto bad;
#endif
#ifdef INET6
		case SIOCSIFPHYADDR_IN6:
			if (src->sa_family == AF_INET6)
				break;
			goto bad;
#endif
		}
		error = EADDRNOTAVAIL;
		switch (src->sa_family) {
#ifdef INET
		case AF_INET:
			if (satosin(src)->sin_addr.s_addr == INADDR_ANY ||
			    satosin(dst)->sin_addr.s_addr == INADDR_ANY)
				goto bad;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			if (IN6_IS_ADDR_UNSPECIFIED(
			    &satosin6(src)->sin6_addr) ||
			    IN6_IS_ADDR_UNSPECIFIED(
			    &satosin6(dst)->sin6_addr))
				goto bad;
			/*
			 * Check validity of the scope zone ID of the
			 * addresses, and convert it into the kernel
			 * internal form if necessary.
			 */
			error = sa6_embedscope(satosin6(src), 0);
			if (error != 0)
				goto bad;
			error = sa6_embedscope(satosin6(dst), 0);
			if (error != 0)
				goto bad;
#endif
		};
		error = ipsec_set_addresses(ifp, src, dst);
		break;
	case SIOCDIFPHYADDR:
		ipsec_delete_tunnel(sc);
		break;
	case SIOCGIFPSRCADDR:
	case SIOCGIFPDSTADDR:
#ifdef INET6
	case SIOCGIFPSRCADDR_IN6:
	case SIOCGIFPDSTADDR_IN6:
#endif
		if (sc->family == 0) {
			error = EADDRNOTAVAIL;
			break;
		}
		saidx = ipsec_getsaidx(sc, IPSEC_DIR_OUTBOUND, sc->family);
		switch (cmd) {
#ifdef INET
		case SIOCGIFPSRCADDR:
		case SIOCGIFPDSTADDR:
			if (saidx->src.sa.sa_family != AF_INET) {
				error = EADDRNOTAVAIL;
				break;
			}
			sin = (struct sockaddr_in *)&ifr->ifr_addr;
			memset(sin, 0, sizeof(*sin));
			sin->sin_family = AF_INET;
			sin->sin_len = sizeof(*sin);
			break;
#endif
#ifdef INET6
		case SIOCGIFPSRCADDR_IN6:
		case SIOCGIFPDSTADDR_IN6:
			if (saidx->src.sa.sa_family != AF_INET6) {
				error = EADDRNOTAVAIL;
				break;
			}
			sin6 = (struct sockaddr_in6 *)
				&(((struct in6_ifreq *)data)->ifr_addr);
			memset(sin6, 0, sizeof(*sin6));
			sin6->sin6_family = AF_INET6;
			sin6->sin6_len = sizeof(*sin6);
			break;
#endif
		default:
			error = EAFNOSUPPORT;
		}
		if (error == 0) {
			switch (cmd) {
#ifdef INET
			case SIOCGIFPSRCADDR:
				sin->sin_addr = saidx->src.sin.sin_addr;
				break;
			case SIOCGIFPDSTADDR:
				sin->sin_addr = saidx->dst.sin.sin_addr;
				break;
#endif
#ifdef INET6
			case SIOCGIFPSRCADDR_IN6:
				sin6->sin6_addr = saidx->src.sin6.sin6_addr;
				break;
			case SIOCGIFPDSTADDR_IN6:
				sin6->sin6_addr = saidx->dst.sin6.sin6_addr;
				break;
#endif
			}
		}
		if (error != 0)
			break;
		switch (cmd) {
#ifdef INET
		case SIOCGIFPSRCADDR:
		case SIOCGIFPDSTADDR:
			error = prison_if(curthread->td_ucred,
			    (struct sockaddr *)sin);
			if (error != 0)
				memset(sin, 0, sizeof(*sin));
			break;
#endif
#ifdef INET6
		case SIOCGIFPSRCADDR_IN6:
		case SIOCGIFPDSTADDR_IN6:
			error = prison_if(curthread->td_ucred,
			    (struct sockaddr *)sin6);
			if (error == 0)
				error = sa6_recoverscope(sin6);
			if (error != 0)
				memset(sin6, 0, sizeof(*sin6));
#endif
		}
		break;
	case SIOCGTUNFIB:
		ifr->ifr_fib = sc->fibnum;
		break;
	case SIOCSTUNFIB:
		if ((error = priv_check(curthread, PRIV_NET_SETIFFIB)) != 0)
			break;
		if (ifr->ifr_fib >= rt_numfibs)
			error = EINVAL;
		else
			sc->fibnum = ifr->ifr_fib;
		break;
	case IPSECGREQID:
		reqid = sc->reqid;
		error = copyout(&reqid, ifr_data_get_ptr(ifr), sizeof(reqid));
		break;
	case IPSECSREQID:
		if ((error = priv_check(curthread, PRIV_NET_SETIFCAP)) != 0)
			break;
		error = copyin(ifr_data_get_ptr(ifr), &reqid, sizeof(reqid));
		if (error != 0)
			break;
		error = ipsec_set_reqid(sc, reqid);
		break;
	default:
		error = EINVAL;
		break;
	}
bad:
	sx_xunlock(&ipsec_ioctl_sx);
	return (error);
}

/*
 * Check that ingress address belongs to local host.
 */
static void
ipsec_set_running(struct ipsec_softc *sc)
{
	struct secasindex *saidx;
	int localip;

	saidx = ipsec_getsaidx(sc, IPSEC_DIR_OUTBOUND, sc->family);
	localip = 0;
	switch (sc->family) {
#ifdef INET
	case AF_INET:
		localip = in_localip(saidx->src.sin.sin_addr);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		localip = in6_localip(&saidx->src.sin6.sin6_addr);
		break;
#endif
	}
	if (localip != 0)
		sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;
	else
		sc->ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
}

/*
 * ifaddr_event handler.
 * Clear IFF_DRV_RUNNING flag when ingress address disappears to prevent
 * source address spoofing.
 */
static void
ipsec_srcaddr(void *arg __unused, const struct sockaddr *sa,
    int event __unused)
{
	struct ipsec_softc *sc;
	struct secasindex *saidx;

	/* Check that VNET is ready */
	if (V_ipsec_idhtbl == NULL)
		return;

	MPASS(in_epoch(net_epoch_preempt));
	CK_LIST_FOREACH(sc, ipsec_srchash(sa), srchash) {
		if (sc->family == 0)
			continue;
		saidx = ipsec_getsaidx(sc, IPSEC_DIR_OUTBOUND, sa->sa_family);
		if (saidx == NULL ||
		    key_sockaddrcmp(&saidx->src.sa, sa, 0) != 0)
			continue;
		ipsec_set_running(sc);
	}
}

/*
 * Allocate new private security policies for tunneling interface.
 * Each tunneling interface has following security policies for
 * both AF:
 *   0.0.0.0/0[any] 0.0.0.0/0[any] -P in \
 *	ipsec esp/tunnel/RemoteIP-LocalIP/unique:reqid
 *   0.0.0.0/0[any] 0.0.0.0/0[any] -P out \
 *	ipsec esp/tunnel/LocalIP-RemoteIP/unique:reqid
 */
static int
ipsec_newpolicies(struct ipsec_softc *sc, struct secpolicy *sp[IPSEC_SPCOUNT],
    const struct sockaddr *src, const struct sockaddr *dst, uint32_t reqid)
{
	struct ipsecrequest *isr;
	int i;

	memset(sp, 0, sizeof(struct secpolicy *) * IPSEC_SPCOUNT);
	for (i = 0; i < IPSEC_SPCOUNT; i++) {
		if ((sp[i] = key_newsp()) == NULL)
			goto fail;
		if ((isr = ipsec_newisr()) == NULL)
			goto fail;

		sp[i]->policy = IPSEC_POLICY_IPSEC;
		sp[i]->state = IPSEC_SPSTATE_DEAD;
		sp[i]->req[sp[i]->tcount++] = isr;
		sp[i]->created = time_second;
		/* Use priority field to store if_index */
		sp[i]->priority = sc->ifp->if_index;
		isr->level = IPSEC_LEVEL_UNIQUE;
		isr->saidx.proto = IPPROTO_ESP;
		isr->saidx.mode = IPSEC_MODE_TUNNEL;
		isr->saidx.reqid = reqid;
		if (i % 2 == 0) {
			sp[i]->spidx.dir = IPSEC_DIR_INBOUND;
			bcopy(src, &isr->saidx.dst, src->sa_len);
			bcopy(dst, &isr->saidx.src, dst->sa_len);
		} else {
			sp[i]->spidx.dir = IPSEC_DIR_OUTBOUND;
			bcopy(src, &isr->saidx.src, src->sa_len);
			bcopy(dst, &isr->saidx.dst, dst->sa_len);
		}
		sp[i]->spidx.ul_proto = IPSEC_ULPROTO_ANY;
#ifdef INET
		if (i < 2) {
			sp[i]->spidx.src.sa.sa_family =
			    sp[i]->spidx.dst.sa.sa_family = AF_INET;
			sp[i]->spidx.src.sa.sa_len =
			    sp[i]->spidx.dst.sa.sa_len =
			    sizeof(struct sockaddr_in);
			continue;
		}
#endif
#ifdef INET6
		sp[i]->spidx.src.sa.sa_family =
		    sp[i]->spidx.dst.sa.sa_family = AF_INET6;
		sp[i]->spidx.src.sa.sa_len =
		    sp[i]->spidx.dst.sa.sa_len = sizeof(struct sockaddr_in6);
#endif
	}
	return (0);
fail:
	for (i = 0; i < IPSEC_SPCOUNT; i++)
		key_freesp(&sp[i]);
	return (ENOMEM);
}

static int
ipsec_check_reqid(uint32_t reqid)
{
	struct ipsec_softc *sc;

	sx_assert(&ipsec_ioctl_sx, SA_XLOCKED);
	CK_LIST_FOREACH(sc, ipsec_idhash(reqid), idhash) {
		if (sc->reqid == reqid)
			return (EEXIST);
	}
	return (0);
}

/*
 * We use key_newreqid() to automatically obtain unique reqid.
 * Then we check that given id is unique, i.e. it is not used by
 * another if_ipsec(4) interface. This macro limits the number of
 * tries to get unique id.
 */
#define	IPSEC_REQID_TRYCNT	64
static int
ipsec_init_reqid(struct ipsec_softc *sc)
{
	uint32_t reqid;
	int trycount;

	sx_assert(&ipsec_ioctl_sx, SA_XLOCKED);
	if (sc->reqid != 0) /* already initialized */
		return (0);

	trycount = IPSEC_REQID_TRYCNT;
	while (--trycount > 0) {
		reqid = key_newreqid();
		if (ipsec_check_reqid(reqid) == 0)
			break;
	}
	if (trycount == 0)
		return (EEXIST);
	sc->reqid = reqid;
	CK_LIST_INSERT_HEAD(ipsec_idhash(reqid), sc, idhash);
	return (0);
}

/*
 * Set or update reqid for given tunneling interface.
 * When specified reqid is zero, generate new one.
 * We are protected by ioctl_sx lock from concurrent id generation.
 * Also softc would not disappear while we hold ioctl_sx lock.
 */
static int
ipsec_set_reqid(struct ipsec_softc *sc, uint32_t reqid)
{
	struct secasindex *saidx;

	sx_assert(&ipsec_ioctl_sx, SA_XLOCKED);

	if (sc->reqid == reqid && reqid != 0)
		return (0);

	if (reqid != 0) {
		/* Check that specified reqid doesn't exist */
		if (ipsec_check_reqid(reqid) != 0)
			return (EEXIST);
		if (sc->reqid != 0) {
			CK_LIST_REMOVE(sc, idhash);
			IPSEC_WAIT();
		}
		sc->reqid = reqid;
		CK_LIST_INSERT_HEAD(ipsec_idhash(reqid), sc, idhash);
	} else {
		/* Generate new reqid */
		if (ipsec_init_reqid(sc) != 0)
			return (EEXIST);
	}

	/* Tunnel isn't fully configured, just return. */
	if (sc->family == 0)
		return (0);

	saidx = ipsec_getsaidx(sc, IPSEC_DIR_OUTBOUND, sc->family);
	KASSERT(saidx != NULL,
	    ("saidx is NULL, but family is %d", sc->family));
	return (ipsec_set_tunnel(sc, &saidx->src.sa, &saidx->dst.sa,
	    sc->reqid));
}

/*
 * Set tunnel endpoints addresses.
 */
static int
ipsec_set_addresses(struct ifnet *ifp, struct sockaddr *src,
    struct sockaddr *dst)
{
	struct ipsec_softc *sc;
	struct secasindex *saidx;

	sx_assert(&ipsec_ioctl_sx, SA_XLOCKED);

	sc = ifp->if_softc;
	if (sc->family != 0) {
		saidx = ipsec_getsaidx(sc, IPSEC_DIR_OUTBOUND,
		    src->sa_family);
		if (saidx != NULL && saidx->reqid == sc->reqid &&
		    key_sockaddrcmp(&saidx->src.sa, src, 0) == 0 &&
		    key_sockaddrcmp(&saidx->dst.sa, dst, 0) == 0)
			return (0); /* Nothing has been changed. */

	}
	/* If reqid is not set, generate new one. */
	if (ipsec_init_reqid(sc) != 0)
		return (EEXIST);
	return (ipsec_set_tunnel(sc, src, dst, sc->reqid));
}

static int
ipsec_set_tunnel(struct ipsec_softc *sc, struct sockaddr *src,
    struct sockaddr *dst, uint32_t reqid)
{
	struct secpolicy *sp[IPSEC_SPCOUNT];
	int i;

	sx_assert(&ipsec_ioctl_sx, SA_XLOCKED);

	/* Allocate SP with new addresses. */
	if (ipsec_newpolicies(sc, sp, src, dst, reqid) == 0) {
		/* Add new policies to SPDB */
		if (key_register_ifnet(sp, IPSEC_SPCOUNT) != 0) {
			for (i = 0; i < IPSEC_SPCOUNT; i++)
				key_freesp(&sp[i]);
			return (EAGAIN);
		}
		if (sc->family != 0)
			ipsec_delete_tunnel(sc);
		for (i = 0; i < IPSEC_SPCOUNT; i++)
			sc->sp[i] = sp[i];
		sc->family = src->sa_family;
		CK_LIST_INSERT_HEAD(ipsec_srchash(src), sc, srchash);
	} else {
		sc->ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		return (ENOMEM);
	}
	ipsec_set_running(sc);
	return (0);
}

static void
ipsec_delete_tunnel(struct ipsec_softc *sc)
{
	int i;

	sx_assert(&ipsec_ioctl_sx, SA_XLOCKED);

	sc->ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	if (sc->family != 0) {
		CK_LIST_REMOVE(sc, srchash);
		sc->family = 0;
		/*
		 * Make sure that ipsec_if_input() will not do access
		 * to softc's policies.
		 */
		IPSEC_WAIT();

		key_unregister_ifnet(sc->sp, IPSEC_SPCOUNT);
		for (i = 0; i < IPSEC_SPCOUNT; i++)
			key_freesp(&sc->sp[i]);
	}
}

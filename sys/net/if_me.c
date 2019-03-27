/*-
 * Copyright (c) 2014, 2018 Andrey V. Elsukov <ae@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/jail.h>
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

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/vnet.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_encap.h>

#include <machine/in_cksum.h>
#include <security/mac/mac_framework.h>

#define	MEMTU			(1500 - sizeof(struct mobhdr))
static const char mename[] = "me";
static MALLOC_DEFINE(M_IFME, mename, "Minimal Encapsulation for IP");
/* Minimal forwarding header RFC 2004 */
struct mobhdr {
	uint8_t		mob_proto;	/* protocol */
	uint8_t		mob_flags;	/* flags */
#define	MOB_FLAGS_SP	0x80		/* source present */
	uint16_t	mob_csum;	/* header checksum */
	struct in_addr	mob_dst;	/* original destination address */
	struct in_addr	mob_src;	/* original source addr (optional) */
} __packed;

struct me_softc {
	struct ifnet		*me_ifp;
	u_int			me_fibnum;
	struct in_addr		me_src;
	struct in_addr		me_dst;

	CK_LIST_ENTRY(me_softc) chain;
	CK_LIST_ENTRY(me_softc) srchash;
};
CK_LIST_HEAD(me_list, me_softc);
#define	ME2IFP(sc)		((sc)->me_ifp)
#define	ME_READY(sc)		((sc)->me_src.s_addr != 0)
#define	ME_RLOCK_TRACKER	struct epoch_tracker me_et
#define	ME_RLOCK()		epoch_enter_preempt(net_epoch_preempt, &me_et)
#define	ME_RUNLOCK()		epoch_exit_preempt(net_epoch_preempt, &me_et)
#define	ME_WAIT()		epoch_wait_preempt(net_epoch_preempt)

#ifndef ME_HASH_SIZE
#define	ME_HASH_SIZE	(1 << 4)
#endif
VNET_DEFINE_STATIC(struct me_list *, me_hashtbl) = NULL;
VNET_DEFINE_STATIC(struct me_list *, me_srchashtbl) = NULL;
#define	V_me_hashtbl		VNET(me_hashtbl)
#define	V_me_srchashtbl		VNET(me_srchashtbl)
#define	ME_HASH(src, dst)	(V_me_hashtbl[\
    me_hashval((src), (dst)) & (ME_HASH_SIZE - 1)])
#define	ME_SRCHASH(src)		(V_me_srchashtbl[\
    fnv_32_buf(&(src), sizeof(src), FNV1_32_INIT) & (ME_HASH_SIZE - 1)])

static struct sx me_ioctl_sx;
SX_SYSINIT(me_ioctl_sx, &me_ioctl_sx, "me_ioctl");

static int	me_clone_create(struct if_clone *, int, caddr_t);
static void	me_clone_destroy(struct ifnet *);
VNET_DEFINE_STATIC(struct if_clone *, me_cloner);
#define	V_me_cloner	VNET(me_cloner)

static void	me_qflush(struct ifnet *);
static int	me_transmit(struct ifnet *, struct mbuf *);
static int	me_ioctl(struct ifnet *, u_long, caddr_t);
static int	me_output(struct ifnet *, struct mbuf *,
		    const struct sockaddr *, struct route *);
static int	me_input(struct mbuf *, int, int, void *);

static int	me_set_tunnel(struct me_softc *, in_addr_t, in_addr_t);
static void	me_delete_tunnel(struct me_softc *);

SYSCTL_DECL(_net_link);
static SYSCTL_NODE(_net_link, IFT_TUNNEL, me, CTLFLAG_RW, 0,
    "Minimal Encapsulation for IP (RFC 2004)");
#ifndef MAX_ME_NEST
#define MAX_ME_NEST 1
#endif

VNET_DEFINE_STATIC(int, max_me_nesting) = MAX_ME_NEST;
#define	V_max_me_nesting	VNET(max_me_nesting)
SYSCTL_INT(_net_link_me, OID_AUTO, max_nesting, CTLFLAG_RW | CTLFLAG_VNET,
    &VNET_NAME(max_me_nesting), 0, "Max nested tunnels");

static uint32_t
me_hashval(in_addr_t src, in_addr_t dst)
{
	uint32_t ret;

	ret = fnv_32_buf(&src, sizeof(src), FNV1_32_INIT);
	return (fnv_32_buf(&dst, sizeof(dst), ret));
}

static struct me_list *
me_hashinit(void)
{
	struct me_list *hash;
	int i;

	hash = malloc(sizeof(struct me_list) * ME_HASH_SIZE,
	    M_IFME, M_WAITOK);
	for (i = 0; i < ME_HASH_SIZE; i++)
		CK_LIST_INIT(&hash[i]);

	return (hash);
}

static void
vnet_me_init(const void *unused __unused)
{

	V_me_cloner = if_clone_simple(mename, me_clone_create,
	    me_clone_destroy, 0);
}
VNET_SYSINIT(vnet_me_init, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_me_init, NULL);

static void
vnet_me_uninit(const void *unused __unused)
{

	if (V_me_hashtbl != NULL) {
		free(V_me_hashtbl, M_IFME);
		V_me_hashtbl = NULL;
		ME_WAIT();
		free(V_me_srchashtbl, M_IFME);
	}
	if_clone_detach(V_me_cloner);
}
VNET_SYSUNINIT(vnet_me_uninit, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_me_uninit, NULL);

static int
me_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct me_softc *sc;

	sc = malloc(sizeof(struct me_softc), M_IFME, M_WAITOK | M_ZERO);
	sc->me_fibnum = curthread->td_proc->p_fibnum;
	ME2IFP(sc) = if_alloc(IFT_TUNNEL);
	ME2IFP(sc)->if_softc = sc;
	if_initname(ME2IFP(sc), mename, unit);

	ME2IFP(sc)->if_mtu = MEMTU;;
	ME2IFP(sc)->if_flags = IFF_POINTOPOINT|IFF_MULTICAST;
	ME2IFP(sc)->if_output = me_output;
	ME2IFP(sc)->if_ioctl = me_ioctl;
	ME2IFP(sc)->if_transmit = me_transmit;
	ME2IFP(sc)->if_qflush = me_qflush;
	ME2IFP(sc)->if_capabilities |= IFCAP_LINKSTATE;
	ME2IFP(sc)->if_capenable |= IFCAP_LINKSTATE;
	if_attach(ME2IFP(sc));
	bpfattach(ME2IFP(sc), DLT_NULL, sizeof(u_int32_t));
	return (0);
}

static void
me_clone_destroy(struct ifnet *ifp)
{
	struct me_softc *sc;

	sx_xlock(&me_ioctl_sx);
	sc = ifp->if_softc;
	me_delete_tunnel(sc);
	bpfdetach(ifp);
	if_detach(ifp);
	ifp->if_softc = NULL;
	sx_xunlock(&me_ioctl_sx);

	ME_WAIT();
	if_free(ifp);
	free(sc, M_IFME);
}

static int
me_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	struct sockaddr_in *src, *dst;
	struct me_softc *sc;
	int error;

	switch (cmd) {
	case SIOCSIFMTU:
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
	}
	sx_xlock(&me_ioctl_sx);
	sc = ifp->if_softc;
	if (sc == NULL) {
		error = ENXIO;
		goto end;
	}
	error = 0;
	switch (cmd) {
	case SIOCSIFPHYADDR:
		src = &((struct in_aliasreq *)data)->ifra_addr;
		dst = &((struct in_aliasreq *)data)->ifra_dstaddr;
		if (src->sin_family != dst->sin_family ||
		    src->sin_family != AF_INET ||
		    src->sin_len != dst->sin_len ||
		    src->sin_len != sizeof(struct sockaddr_in)) {
			error = EINVAL;
			break;
		}
		if (src->sin_addr.s_addr == INADDR_ANY ||
		    dst->sin_addr.s_addr == INADDR_ANY) {
			error = EADDRNOTAVAIL;
			break;
		}
		error = me_set_tunnel(sc, src->sin_addr.s_addr,
		    dst->sin_addr.s_addr);
		break;
	case SIOCDIFPHYADDR:
		me_delete_tunnel(sc);
		break;
	case SIOCGIFPSRCADDR:
	case SIOCGIFPDSTADDR:
		if (!ME_READY(sc)) {
			error = EADDRNOTAVAIL;
			break;
		}
		src = (struct sockaddr_in *)&ifr->ifr_addr;
		memset(src, 0, sizeof(*src));
		src->sin_family = AF_INET;
		src->sin_len = sizeof(*src);
		switch (cmd) {
		case SIOCGIFPSRCADDR:
			src->sin_addr = sc->me_src;
			break;
		case SIOCGIFPDSTADDR:
			src->sin_addr = sc->me_dst;
			break;
		}
		error = prison_if(curthread->td_ucred, sintosa(src));
		if (error != 0)
			memset(src, 0, sizeof(*src));
		break;
	case SIOCGTUNFIB:
		ifr->ifr_fib = sc->me_fibnum;
		break;
	case SIOCSTUNFIB:
		if ((error = priv_check(curthread, PRIV_NET_GRE)) != 0)
			break;
		if (ifr->ifr_fib >= rt_numfibs)
			error = EINVAL;
		else
			sc->me_fibnum = ifr->ifr_fib;
		break;
	default:
		error = EINVAL;
		break;
	}
end:
	sx_xunlock(&me_ioctl_sx);
	return (error);
}

static int
me_lookup(const struct mbuf *m, int off, int proto, void **arg)
{
	const struct ip *ip;
	struct me_softc *sc;

	if (V_me_hashtbl == NULL)
		return (0);

	MPASS(in_epoch(net_epoch_preempt));
	ip = mtod(m, const struct ip *);
	CK_LIST_FOREACH(sc, &ME_HASH(ip->ip_dst.s_addr,
	    ip->ip_src.s_addr), chain) {
		if (sc->me_src.s_addr == ip->ip_dst.s_addr &&
		    sc->me_dst.s_addr == ip->ip_src.s_addr) {
			if ((ME2IFP(sc)->if_flags & IFF_UP) == 0)
				return (0);
			*arg = sc;
			return (ENCAP_DRV_LOOKUP);
		}
	}
	return (0);
}

/*
 * Check that ingress address belongs to local host.
 */
static void
me_set_running(struct me_softc *sc)
{

	if (in_localip(sc->me_src))
		ME2IFP(sc)->if_drv_flags |= IFF_DRV_RUNNING;
	else
		ME2IFP(sc)->if_drv_flags &= ~IFF_DRV_RUNNING;
}

/*
 * ifaddr_event handler.
 * Clear IFF_DRV_RUNNING flag when ingress address disappears to prevent
 * source address spoofing.
 */
static void
me_srcaddr(void *arg __unused, const struct sockaddr *sa,
    int event __unused)
{
	const struct sockaddr_in *sin;
	struct me_softc *sc;

	/* Check that VNET is ready */
	if (V_me_hashtbl == NULL)
		return;

	MPASS(in_epoch(net_epoch_preempt));
	sin = (const struct sockaddr_in *)sa;
	CK_LIST_FOREACH(sc, &ME_SRCHASH(sin->sin_addr.s_addr), srchash) {
		if (sc->me_src.s_addr != sin->sin_addr.s_addr)
			continue;
		me_set_running(sc);
	}
}

static int
me_set_tunnel(struct me_softc *sc, in_addr_t src, in_addr_t dst)
{
	struct me_softc *tmp;

	sx_assert(&me_ioctl_sx, SA_XLOCKED);

	if (V_me_hashtbl == NULL) {
		V_me_hashtbl = me_hashinit();
		V_me_srchashtbl = me_hashinit();
	}

	if (sc->me_src.s_addr == src && sc->me_dst.s_addr == dst)
		return (0);

	CK_LIST_FOREACH(tmp, &ME_HASH(src, dst), chain) {
		if (tmp == sc)
			continue;
		if (tmp->me_src.s_addr == src &&
		    tmp->me_dst.s_addr == dst)
			return (EADDRNOTAVAIL);
	}

	me_delete_tunnel(sc);
	sc->me_dst.s_addr = dst;
	sc->me_src.s_addr = src;
	CK_LIST_INSERT_HEAD(&ME_HASH(src, dst), sc, chain);
	CK_LIST_INSERT_HEAD(&ME_SRCHASH(src), sc, srchash);

	me_set_running(sc);
	if_link_state_change(ME2IFP(sc), LINK_STATE_UP);
	return (0);
}

static void
me_delete_tunnel(struct me_softc *sc)
{

	sx_assert(&me_ioctl_sx, SA_XLOCKED);
	if (ME_READY(sc)) {
		CK_LIST_REMOVE(sc, chain);
		CK_LIST_REMOVE(sc, srchash);
		ME_WAIT();

		sc->me_src.s_addr = 0;
		sc->me_dst.s_addr = 0;
		ME2IFP(sc)->if_drv_flags &= ~IFF_DRV_RUNNING;
		if_link_state_change(ME2IFP(sc), LINK_STATE_DOWN);
	}
}

static uint16_t
me_in_cksum(uint16_t *p, int nwords)
{
	uint32_t sum = 0;

	while (nwords-- > 0)
		sum += *p++;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return (~sum);
}

static int
me_input(struct mbuf *m, int off, int proto, void *arg)
{
	struct me_softc *sc = arg;
	struct mobhdr *mh;
	struct ifnet *ifp;
	struct ip *ip;
	int hlen;

	ifp = ME2IFP(sc);
	/* checks for short packets */
	hlen = sizeof(struct mobhdr);
	if (m->m_pkthdr.len < sizeof(struct ip) + hlen)
		hlen -= sizeof(struct in_addr);
	if (m->m_len < sizeof(struct ip) + hlen)
		m = m_pullup(m, sizeof(struct ip) + hlen);
	if (m == NULL)
		goto drop;
	mh = (struct mobhdr *)mtodo(m, sizeof(struct ip));
	/* check for wrong flags */
	if (mh->mob_flags & (~MOB_FLAGS_SP)) {
		m_freem(m);
		goto drop;
	}
	if (mh->mob_flags) {
	       if (hlen != sizeof(struct mobhdr)) {
			m_freem(m);
			goto drop;
	       }
	} else
		hlen = sizeof(struct mobhdr) - sizeof(struct in_addr);
	/* check mobile header checksum */
	if (me_in_cksum((uint16_t *)mh, hlen / sizeof(uint16_t)) != 0) {
		m_freem(m);
		goto drop;
	}
#ifdef MAC
	mac_ifnet_create_mbuf(ifp, m);
#endif
	ip = mtod(m, struct ip *);
	ip->ip_dst = mh->mob_dst;
	ip->ip_p = mh->mob_proto;
	ip->ip_sum = 0;
	ip->ip_len = htons(m->m_pkthdr.len - hlen);
	if (mh->mob_flags)
		ip->ip_src = mh->mob_src;
	memmove(mtodo(m, hlen), ip, sizeof(struct ip));
	m_adj(m, hlen);
	m_clrprotoflags(m);
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.csum_flags |= (CSUM_IP_CHECKED | CSUM_IP_VALID);
	M_SETFIB(m, ifp->if_fib);
	hlen = AF_INET;
	BPF_MTAP2(ifp, &hlen, sizeof(hlen), m);
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
	if ((ifp->if_flags & IFF_MONITOR) != 0)
		m_freem(m);
	else
		netisr_dispatch(NETISR_IP, m);
	return (IPPROTO_DONE);
drop:
	if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
	return (IPPROTO_DONE);
}

static int
me_output(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
   struct route *ro __unused)
{
	uint32_t af;

	if (dst->sa_family == AF_UNSPEC)
		bcopy(dst->sa_data, &af, sizeof(af));
	else
		af = dst->sa_family;
	m->m_pkthdr.csum_data = af;
	return (ifp->if_transmit(ifp, m));
}

#define	MTAG_ME	1414491977
static int
me_transmit(struct ifnet *ifp, struct mbuf *m)
{
	ME_RLOCK_TRACKER;
	struct mobhdr mh;
	struct me_softc *sc;
	struct ip *ip;
	uint32_t af;
	int error, hlen, plen;

	ME_RLOCK();
#ifdef MAC
	error = mac_ifnet_check_transmit(ifp, m);
	if (error != 0)
		goto drop;
#endif
	error = ENETDOWN;
	sc = ifp->if_softc;
	if (sc == NULL || !ME_READY(sc) ||
	    (ifp->if_flags & IFF_MONITOR) != 0 ||
	    (ifp->if_flags & IFF_UP) == 0 ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    (error = if_tunnel_check_nesting(ifp, m, MTAG_ME,
		V_max_me_nesting)) != 0) {
		m_freem(m);
		goto drop;
	}
	af = m->m_pkthdr.csum_data;
	if (af != AF_INET) {
		error = EAFNOSUPPORT;
		m_freem(m);
		goto drop;
	}
	if (m->m_len < sizeof(struct ip))
		m = m_pullup(m, sizeof(struct ip));
	if (m == NULL) {
		error = ENOBUFS;
		goto drop;
	}
	ip = mtod(m, struct ip *);
	/* Fragmented datagramms shouldn't be encapsulated */
	if (ip->ip_off & htons(IP_MF | IP_OFFMASK)) {
		error = EINVAL;
		m_freem(m);
		goto drop;
	}
	mh.mob_proto = ip->ip_p;
	mh.mob_src = ip->ip_src;
	mh.mob_dst = ip->ip_dst;
	if (in_hosteq(sc->me_src, ip->ip_src)) {
		hlen = sizeof(struct mobhdr) - sizeof(struct in_addr);
		mh.mob_flags = 0;
	} else {
		hlen = sizeof(struct mobhdr);
		mh.mob_flags = MOB_FLAGS_SP;
	}
	BPF_MTAP2(ifp, &af, sizeof(af), m);
	plen = m->m_pkthdr.len;
	ip->ip_src = sc->me_src;
	ip->ip_dst = sc->me_dst;
	m->m_flags &= ~(M_BCAST|M_MCAST);
	M_SETFIB(m, sc->me_fibnum);
	M_PREPEND(m, hlen, M_NOWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto drop;
	}
	if (m->m_len < sizeof(struct ip) + hlen)
		m = m_pullup(m, sizeof(struct ip) + hlen);
	if (m == NULL) {
		error = ENOBUFS;
		goto drop;
	}
	memmove(mtod(m, void *), mtodo(m, hlen), sizeof(struct ip));
	ip = mtod(m, struct ip *);
	ip->ip_len = htons(m->m_pkthdr.len);
	ip->ip_p = IPPROTO_MOBILE;
	ip->ip_sum = 0;
	mh.mob_csum = 0;
	mh.mob_csum = me_in_cksum((uint16_t *)&mh, hlen / sizeof(uint16_t));
	bcopy(&mh, mtodo(m, sizeof(struct ip)), hlen);
	error = ip_output(m, NULL, NULL, IP_FORWARDING, NULL, NULL);
drop:
	if (error)
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	else {
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_OBYTES, plen);
	}
	ME_RUNLOCK();
	return (error);
}

static void
me_qflush(struct ifnet *ifp __unused)
{

}

static const struct srcaddrtab *me_srcaddrtab = NULL;
static const struct encaptab *ecookie = NULL;
static const struct encap_config me_encap_cfg = {
	.proto = IPPROTO_MOBILE,
	.min_length = sizeof(struct ip) + sizeof(struct mobhdr) -
	    sizeof(in_addr_t),
	.exact_match = ENCAP_DRV_LOOKUP,
	.lookup = me_lookup,
	.input = me_input
};

static int
memodevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		me_srcaddrtab = ip_encap_register_srcaddr(me_srcaddr,
		    NULL, M_WAITOK);
		ecookie = ip_encap_attach(&me_encap_cfg, NULL, M_WAITOK);
		break;
	case MOD_UNLOAD:
		ip_encap_detach(ecookie);
		ip_encap_unregister_srcaddr(me_srcaddrtab);
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t me_mod = {
	"if_me",
	memodevent,
	0
};

DECLARE_MODULE(if_me, me_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(if_me, 1);

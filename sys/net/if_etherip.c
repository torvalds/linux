/*	$OpenBSD: if_etherip.c,v 1.59 2025/07/07 02:28:50 jsg Exp $	*/
/*
 * Copyright (c) 2015 Kazuya GODA <goda@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bpfilter.h"
#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/rtable.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip_ether.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#endif

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NPF > 0
#include <net/pfvar.h>
#endif

#include <net/if_etherip.h>

/*
 * Locks used to protect data:
 *	a	atomic
 */
 
union etherip_addr {
	struct in_addr	in4;
	struct in6_addr	in6;
};

struct etherip_tunnel {
	union etherip_addr
			_t_src;
#define t_src4	_t_src.in4
#define t_src6	_t_src.in6
	union etherip_addr
			_t_dst;
#define t_dst4	_t_dst.in4
#define t_dst6	_t_dst.in6

	unsigned int	t_rtableid;
	sa_family_t	t_af;
	uint8_t		t_tos;

	TAILQ_ENTRY(etherip_tunnel)
			t_entry;
};

TAILQ_HEAD(etherip_list, etherip_tunnel);

static inline int etherip_cmp(const struct etherip_tunnel *,
    const struct etherip_tunnel *);

struct etherip_softc {
	struct etherip_tunnel	sc_tunnel; /* must be first */
	struct arpcom		sc_ac;
	struct ifmedia		sc_media;
	int			sc_txhprio;
	int			sc_rxhprio;
	uint16_t		sc_df;
	uint8_t			sc_ttl;
};

/*
 * We can control the acceptance of EtherIP packets by altering the sysctl
 * net.inet.etherip.allow value. Zero means drop them, all else is acceptance.
 */
int etherip_allow = 0;	/* [a] */

struct cpumem *etheripcounters;

void etheripattach(int);
int etherip_clone_create(struct if_clone *, int);
int etherip_clone_destroy(struct ifnet *);
int etherip_ioctl(struct ifnet *, u_long, caddr_t);
int etherip_output(struct ifnet *, struct mbuf *, struct sockaddr *,
    struct rtentry *);
void etherip_start(struct ifnet *);
int etherip_media_change(struct ifnet *);
void etherip_media_status(struct ifnet *, struct ifmediareq *);
int etherip_set_tunnel(struct etherip_softc *, struct if_laddrreq *);
int etherip_get_tunnel(struct etherip_softc *, struct if_laddrreq *);
int etherip_del_tunnel(struct etherip_softc *);
int etherip_up(struct etherip_softc *);
int etherip_down(struct etherip_softc *);
struct etherip_softc *etherip_find(const struct etherip_tunnel *);
int etherip_input(struct etherip_tunnel *, struct mbuf *, uint8_t, int,
    struct netstack *);

struct if_clone	etherip_cloner = IF_CLONE_INITIALIZER("etherip",
    etherip_clone_create, etherip_clone_destroy);

struct etherip_list etherip_list = TAILQ_HEAD_INITIALIZER(etherip_list);

void
etheripattach(int count)
{
	if_clone_attach(&etherip_cloner);
	etheripcounters = counters_alloc(etherips_ncounters);
}

int
etherip_clone_create(struct if_clone *ifc, int unit)
{
	struct ifnet *ifp;
	struct etherip_softc *sc;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = &sc->sc_ac.ac_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	sc->sc_ttl = atomic_load_int(&ip_defttl);
	sc->sc_txhprio = IFQ_TOS2PRIO(IPTOS_PREC_ROUTINE); /* 0 */
	sc->sc_rxhprio = IF_HDRPRIO_PACKET;
	sc->sc_df = htons(0);

	ifp->if_softc = sc;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_ioctl = etherip_ioctl;
	ifp->if_output = etherip_output;
	ifp->if_start = etherip_start;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ether_fakeaddr(ifp);

	ifmedia_init(&sc->sc_media, 0, etherip_media_change,
	    etherip_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

	NET_LOCK();
	TAILQ_INSERT_TAIL(&etherip_list, &sc->sc_tunnel, t_entry);
	NET_UNLOCK();

	return (0);
}

int
etherip_clone_destroy(struct ifnet *ifp)
{
	struct etherip_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		etherip_down(sc);

	TAILQ_REMOVE(&etherip_list, &sc->sc_tunnel, t_entry);
	NET_UNLOCK();

	ifmedia_delete_instance(&sc->sc_media, IFM_INST_ANY);
	ether_ifdetach(ifp);
	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

int
etherip_media_change(struct ifnet *ifp)
{
	return 0;
}

void
etherip_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	imr->ifm_active = IFM_ETHER | IFM_AUTO;
	imr->ifm_status = IFM_AVALID | IFM_ACTIVE;
}

int
etherip_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
 	struct m_tag *mtag;

	mtag = NULL;
	while ((mtag = m_tag_find(m, PACKET_TAG_GRE, mtag)) != NULL) {
		if (*(int *)(mtag + 1) == ifp->if_index) {
			m_freem(m);
			return (EIO);
		}
	}
 
	return (ether_output(ifp, m, dst, rt));
}

void
etherip_start(struct ifnet *ifp)
{
	struct etherip_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int error;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	while ((m = ifq_dequeue(&ifp->if_snd)) != NULL) {
#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_ether(if_bpf, m, BPF_DIRECTION_OUT);
#endif

		switch (sc->sc_tunnel.t_af) {
		case AF_INET:
			error = ip_etherip_output(ifp, m);
			break;
#ifdef INET6
		case AF_INET6:
			error = ip6_etherip_output(ifp, m);
			break;
#endif
		default:
			/* unhandled_af(sc->sc_tunnel.t_af); */
			m_freem(m);
			continue;
		}

		if (error)
			ifp->if_oerrors++;
	}
}

int
etherip_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct etherip_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = etherip_up(sc);
			else
				error = 0;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = etherip_down(sc);
		}
		break;

	case SIOCSLIFPHYRTABLE:
		if (ifr->ifr_rdomainid < 0 ||
		    ifr->ifr_rdomainid > RT_TABLEID_MAX ||
		    !rtable_exists(ifr->ifr_rdomainid)) {
			error = EINVAL;
			break;
		}
		sc->sc_tunnel.t_rtableid = ifr->ifr_rdomainid;
		break;

	case SIOCGLIFPHYRTABLE:
		ifr->ifr_rdomainid = sc->sc_tunnel.t_rtableid;
		break;

	case SIOCSLIFPHYADDR:
		error = etherip_set_tunnel(sc, (struct if_laddrreq *)data);
		break;
	case SIOCGLIFPHYADDR:
		error = etherip_get_tunnel(sc, (struct if_laddrreq *)data);
		break;
	case SIOCDIFPHYADDR:
		error = etherip_del_tunnel(sc);
		break;

	case SIOCSTXHPRIO:
		error = if_txhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_txhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGTXHPRIO:
		ifr->ifr_hdrprio = sc->sc_txhprio;
                break;

	case SIOCSRXHPRIO:
		error = if_rxhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_rxhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGRXHPRIO:
		ifr->ifr_hdrprio = sc->sc_rxhprio;
                break;

	case SIOCSLIFPHYTTL:
		if (ifr->ifr_ttl < 1 || ifr->ifr_ttl > 0xff) {
			error = EINVAL;
			break;
		}

		/* commit */
		sc->sc_ttl = (uint8_t)ifr->ifr_ttl;
		break;
	case SIOCGLIFPHYTTL:
		ifr->ifr_ttl = (int)sc->sc_ttl;
		break;

	case SIOCSLIFPHYDF:
		/* commit */
		sc->sc_df = ifr->ifr_df ? htons(IP_DF) : htons(0);
		break;
	case SIOCGLIFPHYDF:
		ifr->ifr_df = sc->sc_df ? 1 : 0;
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		/* no hardware to program */
		error = 0;
	}

	return (error);
}

int
etherip_set_tunnel(struct etherip_softc *sc, struct if_laddrreq *req)
{
	struct sockaddr *src = (struct sockaddr *)&req->addr;
	struct sockaddr *dst = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *src4, *dst4;
#ifdef INET6
	struct sockaddr_in6 *src6, *dst6;
	int error;
#endif

	/* sa_family and sa_len must be equal */
	if (src->sa_family != dst->sa_family || src->sa_len != dst->sa_len)
		return (EINVAL);

	/* validate */
	switch (dst->sa_family) {
	case AF_INET:
		if (dst->sa_len != sizeof(*dst4))
			return (EINVAL);

		src4 = (struct sockaddr_in *)src;
		if (in_nullhost(src4->sin_addr) ||
		    IN_MULTICAST(src4->sin_addr.s_addr))
			return (EINVAL);

		dst4 = (struct sockaddr_in *)dst;
		if (in_nullhost(dst4->sin_addr) ||
		    IN_MULTICAST(dst4->sin_addr.s_addr))
			return (EINVAL);

		sc->sc_tunnel.t_src4 = src4->sin_addr;
		sc->sc_tunnel.t_dst4 = dst4->sin_addr;
		break;
#ifdef INET6
	case AF_INET6:
		if (dst->sa_len != sizeof(*dst6))
			return (EINVAL);

		src6 = (struct sockaddr_in6 *)src;
		if (IN6_IS_ADDR_UNSPECIFIED(&src6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&src6->sin6_addr))
			return (EINVAL);

		dst6 = (struct sockaddr_in6 *)dst;
		if (IN6_IS_ADDR_UNSPECIFIED(&dst6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&dst6->sin6_addr))
			return (EINVAL);

		error = in6_embedscope(&sc->sc_tunnel.t_src6, src6, NULL, NULL);
		if (error != 0)
			return (error);

		error = in6_embedscope(&sc->sc_tunnel.t_dst6, dst6, NULL, NULL);
		if (error != 0)
			return (error);

		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}

	/* commit */
	sc->sc_tunnel.t_af = dst->sa_family;

	return (0);
}

int
etherip_get_tunnel(struct etherip_softc *sc, struct if_laddrreq *req)
{
	struct sockaddr *src = (struct sockaddr *)&req->addr;
	struct sockaddr *dst = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *sin;
#ifdef INET6 /* ifconfig already embeds the scopeid */
	struct sockaddr_in6 *sin6;
#endif

	switch (sc->sc_tunnel.t_af) {
	case AF_UNSPEC:
		return (EADDRNOTAVAIL);
	case AF_INET:
		sin = (struct sockaddr_in *)src;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = sc->sc_tunnel.t_src4;

		sin = (struct sockaddr_in *)dst;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = sc->sc_tunnel.t_dst4;

		break;
#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)src;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(*sin6);
		in6_recoverscope(sin6, &sc->sc_tunnel.t_src6);

		sin6 = (struct sockaddr_in6 *)dst;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(*sin6);
		in6_recoverscope(sin6, &sc->sc_tunnel.t_dst6);

		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}

	return (0);
}

int
etherip_del_tunnel(struct etherip_softc *sc)
{
	/* commit */
	sc->sc_tunnel.t_af = AF_UNSPEC;

	return (0);
}

int
etherip_up(struct etherip_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	NET_ASSERT_LOCKED();

	SET(ifp->if_flags, IFF_RUNNING);

	return (0);
}

int
etherip_down(struct etherip_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	NET_ASSERT_LOCKED();

	CLR(ifp->if_flags, IFF_RUNNING);

	return (0);
}

int
ip_etherip_output(struct ifnet *ifp, struct mbuf *m)
{
	struct etherip_softc *sc = (struct etherip_softc *)ifp->if_softc;
	struct m_tag *mtag;
	struct etherip_header *eip;
	struct ip *ip;

	M_PREPEND(m, sizeof(*ip) + sizeof(*eip), M_DONTWAIT);
	if (m == NULL) {
		etheripstat_inc(etherips_adrops);
		return ENOBUFS;
	}

	ip = mtod(m, struct ip *);
	memset(ip, 0, sizeof(struct ip));

	ip->ip_v = IPVERSION;
	ip->ip_hl = sizeof(*ip) >> 2;
	ip->ip_tos = IFQ_PRIO2TOS(sc->sc_txhprio == IF_HDRPRIO_PACKET ?
	    m->m_pkthdr.pf.prio : sc->sc_txhprio);
	ip->ip_len = htons(m->m_pkthdr.len);
	ip->ip_id = htons(ip_randomid());
	ip->ip_off = sc->sc_df;
	ip->ip_ttl = sc->sc_ttl;
	ip->ip_p = IPPROTO_ETHERIP;
	ip->ip_src = sc->sc_tunnel.t_src4;
	ip->ip_dst = sc->sc_tunnel.t_dst4;

	eip = (struct etherip_header *)(ip + 1);
	eip->eip_ver = ETHERIP_VERSION;
	eip->eip_res = 0;
	eip->eip_pad = 0;

	mtag = m_tag_get(PACKET_TAG_GRE, sizeof(ifp->if_index), M_NOWAIT);
	if (mtag == NULL) {
		m_freem(m);
		return (ENOMEM);
	}

	*(int *)(mtag + 1) = ifp->if_index;
	m_tag_prepend(m, mtag);

	m->m_flags &= ~(M_BCAST|M_MCAST);
	m->m_pkthdr.ph_rtableid = sc->sc_tunnel.t_rtableid;

#if NPF > 0
	pf_pkt_addr_changed(m);
#endif
	etheripstat_pkt(etherips_opackets, etherips_obytes, m->m_pkthdr.len -
	    (sizeof(struct ip) + sizeof(struct etherip_header)));

	ip_send(m);

	return (0);
}

int
ip_etherip_input(struct mbuf **mp, int *offp, int type, int af,
    struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct etherip_tunnel key;
	struct ip *ip;

	ip = mtod(m, struct ip *);

	key.t_af = AF_INET;
	key.t_src4 = ip->ip_dst;
	key.t_dst4 = ip->ip_src;

	return (etherip_input(&key, m, ip->ip_tos, *offp, ns));
}

struct etherip_softc *
etherip_find(const struct etherip_tunnel *key)
{
	struct etherip_tunnel *t;
	struct etherip_softc *sc;

	TAILQ_FOREACH(t, &etherip_list, t_entry) {
		if (etherip_cmp(key, t) != 0)
			continue;

		sc = (struct etherip_softc *)t;
		if (!ISSET(sc->sc_ac.ac_if.if_flags, IFF_RUNNING))
			continue;

		return (sc);
	}

	return (NULL);
}

int
etherip_input(struct etherip_tunnel *key, struct mbuf *m, uint8_t tos,
    int hlen, struct netstack *ns)
{
	struct etherip_softc *sc;
	struct ifnet *ifp;
	struct etherip_header *eip;
	int rxprio;

	if (atomic_load_int(&etherip_allow) == 0 &&
	    (m->m_flags & (M_AUTH|M_CONF)) == 0) {
		etheripstat_inc(etherips_pdrops);
		goto drop;
	}

	key->t_rtableid = m->m_pkthdr.ph_rtableid;

	NET_ASSERT_LOCKED();
	sc = etherip_find(key);
	if (sc == NULL) {
		etheripstat_inc(etherips_noifdrops);
		goto drop;
	}

	m_adj(m, hlen);
	m = m_pullup(m, sizeof(*eip));
	if (m == NULL) {
		etheripstat_inc(etherips_adrops);
		return IPPROTO_DONE;
	}

	eip = mtod(m, struct etherip_header *);
	if (eip->eip_ver != ETHERIP_VERSION || eip->eip_pad) {
		etheripstat_inc(etherips_adrops);
		goto drop;
	}

	m_adj(m, sizeof(struct etherip_header));

	etheripstat_pkt(etherips_ipackets, etherips_ibytes, m->m_pkthdr.len);

	m = m_pullup(m, sizeof(struct ether_header));
	if (m == NULL) {
		etheripstat_inc(etherips_adrops);
		return IPPROTO_DONE;
	}

	rxprio = sc->sc_rxhprio;
	switch (rxprio) {
	case IF_HDRPRIO_PACKET:
		break;
	case IF_HDRPRIO_OUTER:
		m->m_pkthdr.pf.prio = IFQ_TOS2PRIO(tos);
		break;
	default:
		m->m_pkthdr.pf.prio = rxprio;
		break;
	}

	ifp = &sc->sc_ac.ac_if;

	m->m_flags &= ~(M_BCAST|M_MCAST);
	m->m_pkthdr.ph_ifidx = ifp->if_index;
	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;

#if NPF > 0
	pf_pkt_addr_changed(m);
#endif

	if_vinput(ifp, m, ns);
	return IPPROTO_DONE;

drop:
	m_freem(m);
	return (IPPROTO_DONE);
}

#ifdef INET6
int
ip6_etherip_output(struct ifnet *ifp, struct mbuf *m)
{
	struct etherip_softc *sc = ifp->if_softc;
	struct m_tag *mtag;
	struct ip6_hdr *ip6;
	struct etherip_header *eip;
	uint16_t len;
	uint32_t flow;

	if (IN6_IS_ADDR_UNSPECIFIED(&sc->sc_tunnel.t_dst6)) {
		m_freem(m);
		return (ENETUNREACH);
	}

	len = m->m_pkthdr.len;

	M_PREPEND(m, sizeof(*ip6) + sizeof(*eip), M_DONTWAIT);
	if (m == NULL) {
		etheripstat_inc(etherips_adrops);
		return ENOBUFS;
	}

	flow = IPV6_VERSION << 24;
	flow |= IFQ_PRIO2TOS(sc->sc_txhprio == IF_HDRPRIO_PACKET ?
	     m->m_pkthdr.pf.prio : sc->sc_txhprio) << 20;

	ip6 = mtod(m, struct ip6_hdr *);
	htobem32(&ip6->ip6_flow, flow);
	ip6->ip6_nxt  = IPPROTO_ETHERIP;
	ip6->ip6_hlim = sc->sc_ttl;
	ip6->ip6_plen = htons(len);
	memcpy(&ip6->ip6_src, &sc->sc_tunnel.t_src6, sizeof(ip6->ip6_src));
	memcpy(&ip6->ip6_dst, &sc->sc_tunnel.t_dst6, sizeof(ip6->ip6_dst));

	eip = (struct etherip_header *)(ip6 + 1);
	eip->eip_ver = ETHERIP_VERSION;
	eip->eip_res = 0;
	eip->eip_pad = 0;

	mtag = m_tag_get(PACKET_TAG_GRE, sizeof(ifp->if_index), M_NOWAIT);
	if (mtag == NULL) {
		m_freem(m);
		return (ENOMEM);
	}

	*(int *)(mtag + 1) = ifp->if_index;
	m_tag_prepend(m, mtag);

	if (sc->sc_df)
		SET(m->m_pkthdr.csum_flags, M_IPV6_DF_OUT);

	m->m_flags &= ~(M_BCAST|M_MCAST);
	m->m_pkthdr.ph_rtableid = sc->sc_tunnel.t_rtableid;

#if NPF > 0
	pf_pkt_addr_changed(m);
#endif

	etheripstat_pkt(etherips_opackets, etherips_obytes, len);

	ip6_send(m);
	return (0);
}

int
ip6_etherip_input(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct etherip_tunnel key;
	const struct ip6_hdr *ip6;
	uint32_t flow;

	ip6 = mtod(m, const struct ip6_hdr *);

	key.t_af = AF_INET6;
	key.t_src6 = ip6->ip6_dst;
	key.t_dst6 = ip6->ip6_src;

	flow = bemtoh32(&ip6->ip6_flow);

	return (etherip_input(&key, m, flow >> 20, *offp, ns));
}
#endif /* INET6 */

int
etherip_sysctl_etheripstat(void *oldp, size_t *oldlenp, void *newp)
{
	struct etheripstat etheripstat;

	CTASSERT(sizeof(etheripstat) == (etherips_ncounters *
	    sizeof(uint64_t)));
	memset(&etheripstat, 0, sizeof etheripstat);
	counters_read(etheripcounters, (uint64_t *)&etheripstat,
	    etherips_ncounters, NULL);
	return (sysctl_rdstruct(oldp, oldlenp, newp, &etheripstat,
	    sizeof(etheripstat)));
}

int
etherip_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return ENOTDIR;

	switch (name[0]) {
	case ETHERIPCTL_ALLOW:
		return (sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &etherip_allow, 0, 1));
	case ETHERIPCTL_STATS:
		return (etherip_sysctl_etheripstat(oldp, oldlenp, newp));
	default:
		break;
	}

	return ENOPROTOOPT;
}

static inline int
etherip_ip_cmp(int af, const union etherip_addr *a, const union etherip_addr *b)
{
	switch (af) {
#ifdef INET6
	case AF_INET6:
		return (memcmp(&a->in6, &b->in6, sizeof(a->in6)));
		/* FALLTHROUGH */
#endif /* INET6 */
	case AF_INET:
		return (memcmp(&a->in4, &b->in4, sizeof(a->in4)));
		break;
	default:
		panic("%s: unsupported af %d", __func__, af);
	}

	return (0);
}

static inline int
etherip_cmp(const struct etherip_tunnel *a, const struct etherip_tunnel *b)
{
	int rv;

	if (a->t_rtableid > b->t_rtableid)
		return (1);
	if (a->t_rtableid < b->t_rtableid)
		return (-1);

	/* sort by address */
	if (a->t_af > b->t_af)
		return (1);
	if (a->t_af < b->t_af)
		return (-1);

	rv = etherip_ip_cmp(a->t_af, &a->_t_dst, &b->_t_dst);
	if (rv != 0)
		return (rv);

	rv = etherip_ip_cmp(a->t_af, &a->_t_src, &b->_t_src);
	if (rv != 0)
		return (rv);

	return (0);
}

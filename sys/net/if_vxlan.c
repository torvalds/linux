/*	$OpenBSD: if_vxlan.c,v 1.104 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2021 David Gwynne <dlg@openbsd.org>
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
#include <sys/pool.h>
#include <sys/tree.h>
#include <sys/refcnt.h>
#include <sys/smr.h>

#include <sys/socketvar.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/rtable.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_var.h>
#endif

/* for bridge stuff */
#include <net/if_bridge.h>
#include <net/if_etherbridge.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

/*
 * The protocol.
 */

#define VXLAN_PORT		4789

struct vxlan_header {
	uint32_t		vxlan_flags;
#define VXLAN_F_I			(1U << 27)
	uint32_t		vxlan_id;
#define VXLAN_VNI_SHIFT			8
#define VXLAN_VNI_MASK			(0xffffffU << VXLAN_VNI_SHIFT)
};

#define VXLAN_VNI_MAX			0x00ffffffU
#define VXLAN_VNI_MIN			0x00000000U

/*
 * The driver.
 */

union vxlan_addr {
	struct in_addr		in4;
	struct in6_addr		in6;
};

struct vxlan_softc;

struct vxlan_peer {
	RBT_ENTRY(vxlan_peer)	 p_entry;

	struct vxlan_header	 p_header;
	union vxlan_addr	 p_addr;

	struct vxlan_softc	*p_sc;
};

RBT_HEAD(vxlan_peers, vxlan_peer);

struct vxlan_tep {
	TAILQ_ENTRY(vxlan_tep)	 vt_entry;

	sa_family_t		 vt_af;
	unsigned int		 vt_rdomain;
	union vxlan_addr	 vt_addr;
#define vt_addr4 vt_addr.in4
#define vt_addr6 vt_addr.in6
	in_port_t		 vt_port;

	struct socket		*vt_so;

	struct mutex		 vt_mtx;
	struct vxlan_peers	 vt_peers;
};

TAILQ_HEAD(vxlan_teps, vxlan_tep);

enum vxlan_tunnel_mode {
	VXLAN_TMODE_UNSET,
	VXLAN_TMODE_P2P,	 /* unicast destination, no learning */
	VXLAN_TMODE_LEARNING,	 /* multicast destination, learning */
	VXLAN_TMODE_ENDPOINT,	 /* unset destination, no learning */
};

struct vxlan_softc {
	struct arpcom		 sc_ac;
	struct etherbridge	 sc_eb;

	unsigned int		 sc_rdomain;
	sa_family_t		 sc_af;
	union vxlan_addr	 sc_src;
	union vxlan_addr	 sc_dst;
	in_port_t		 sc_port;
	struct vxlan_header	 sc_header;
	unsigned int		 sc_if_index0;

	struct task		 sc_dtask;
	void			*sc_inmulti;

	enum vxlan_tunnel_mode	 sc_mode;
	struct vxlan_peer	*sc_ucast_peer;
	struct vxlan_peer	*sc_mcast_peer;
	struct refcnt		 sc_refs;

	uint16_t		 sc_df;
	int			 sc_ttl;
	int			 sc_txhprio;
	int			 sc_rxhprio;

	struct task		 sc_send_task;
};

void		vxlanattach(int);

static int	vxlan_clone_create(struct if_clone *, int);
static int	vxlan_clone_destroy(struct ifnet *);

static int	vxlan_output(struct ifnet *, struct mbuf *,
		    struct sockaddr *, struct rtentry *);
static int	vxlan_enqueue(struct ifnet *, struct mbuf *);
static void	vxlan_start(struct ifqueue *);
static void	vxlan_send(void *);

static int	vxlan_ioctl(struct ifnet *, u_long, caddr_t);
static int	vxlan_up(struct vxlan_softc *);
static int	vxlan_down(struct vxlan_softc *);
static int	vxlan_addmulti(struct vxlan_softc *, struct ifnet *);
static void	vxlan_delmulti(struct vxlan_softc *);

static struct mbuf *
		vxlan_input(void *, struct mbuf *, struct ip *,
		    struct ip6_hdr *, void *, int, struct netstack *);

static int	vxlan_set_rdomain(struct vxlan_softc *, const struct ifreq *);
static int	vxlan_get_rdomain(struct vxlan_softc *, struct ifreq *);
static int	vxlan_set_tunnel(struct vxlan_softc *,
		    const struct if_laddrreq *);
static int	vxlan_get_tunnel(struct vxlan_softc *, struct if_laddrreq *);
static int	vxlan_del_tunnel(struct vxlan_softc *);
static int	vxlan_set_vnetid(struct vxlan_softc *, const struct ifreq *);
static int	vxlan_get_vnetid(struct vxlan_softc *, struct ifreq *);
static int	vxlan_del_vnetid(struct vxlan_softc *);
static int	vxlan_set_parent(struct vxlan_softc *,
		    const struct if_parent *);
static int	vxlan_get_parent(struct vxlan_softc *, struct if_parent *);
static int	vxlan_del_parent(struct vxlan_softc *);

static int	vxlan_add_addr(struct vxlan_softc *, const struct ifbareq *);
static int	vxlan_del_addr(struct vxlan_softc *, const struct ifbareq *);

static void	vxlan_detach_hook(void *);

static struct if_clone vxlan_cloner =
    IF_CLONE_INITIALIZER("vxlan", vxlan_clone_create, vxlan_clone_destroy);

static int	 vxlan_eb_port_eq(void *, void *, void *);
static void	*vxlan_eb_port_take(void *, void *);
static void	 vxlan_eb_port_rele(void *, void *);
static size_t	 vxlan_eb_port_ifname(void *, char *, size_t, void *);
static void	 vxlan_eb_port_sa(void *, struct sockaddr_storage *, void *);

static const struct etherbridge_ops vxlan_etherbridge_ops = {
	vxlan_eb_port_eq,
	vxlan_eb_port_take,
	vxlan_eb_port_rele,
	vxlan_eb_port_ifname,
	vxlan_eb_port_sa,
};

static struct rwlock vxlan_lock = RWLOCK_INITIALIZER("vteps");
static struct vxlan_teps vxlan_teps = TAILQ_HEAD_INITIALIZER(vxlan_teps);
static struct pool vxlan_endpoint_pool;

static inline int	vxlan_peer_cmp(const struct vxlan_peer *,
			    const struct vxlan_peer *);

RBT_PROTOTYPE(vxlan_peers, vxlan_peer, p_entry, vxlan_peer_cmp);

void
vxlanattach(int count)
{
	if_clone_attach(&vxlan_cloner);
}

static int
vxlan_clone_create(struct if_clone *ifc, int unit)
{
	struct vxlan_softc *sc;
	struct ifnet *ifp;
	int error;

	if (vxlan_endpoint_pool.pr_size == 0) {
		pool_init(&vxlan_endpoint_pool, sizeof(union vxlan_addr),
		    0, IPL_SOFTNET, 0, "vxlanep", NULL);
	}

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO|M_CANFAIL);
	if (sc == NULL)
		return (ENOMEM);

	ifp = &sc->sc_ac.ac_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	error = etherbridge_init(&sc->sc_eb, ifp->if_xname,
	    &vxlan_etherbridge_ops, sc);
	if (error == -1) {
		free(sc, M_DEVBUF, sizeof(*sc));
		return (error);
	}

	sc->sc_af = AF_UNSPEC;
	sc->sc_txhprio = 0;
	sc->sc_rxhprio = IF_HDRPRIO_OUTER;
	sc->sc_df = 0;
	sc->sc_ttl = IP_DEFAULT_MULTICAST_TTL;

	task_set(&sc->sc_dtask, vxlan_detach_hook, sc);
	refcnt_init(&sc->sc_refs);
	task_set(&sc->sc_send_task, vxlan_send, sc);

	ifp->if_softc = sc;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_ioctl = vxlan_ioctl;
	ifp->if_output = vxlan_output;
	ifp->if_enqueue = vxlan_enqueue;
	ifp->if_qstart = vxlan_start;
	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST | IFF_SIMPLEX;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;
	ether_fakeaddr(ifp);

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

	return (0);
}

static int
vxlan_clone_destroy(struct ifnet *ifp)
{
	struct vxlan_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		vxlan_down(sc);
	NET_UNLOCK();

	ether_ifdetach(ifp);
	if_detach(ifp);

	etherbridge_destroy(&sc->sc_eb);

	refcnt_finalize(&sc->sc_refs, "vxlanfini");

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static struct vxlan_softc *
vxlan_take(struct vxlan_softc *sc)
{
	refcnt_take(&sc->sc_refs);
	return (sc);
}

static void
vxlan_rele(struct vxlan_softc *sc)
{
	refcnt_rele_wake(&sc->sc_refs);
}

static struct mbuf *
vxlan_encap(struct vxlan_softc *sc, struct mbuf *m,
    struct mbuf *(ip_encap)(struct vxlan_softc *sc, struct mbuf *,
    const union vxlan_addr *, uint8_t))
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct m_tag *mtag;
	struct mbuf *m0;
	union vxlan_addr gateway;
	const union vxlan_addr *endpoint;
	struct vxlan_header *vh;
	struct udphdr *uh;
	int prio;
	uint8_t tos;

	if (sc->sc_mode == VXLAN_TMODE_UNSET)
		goto drop;

	if (sc->sc_mode == VXLAN_TMODE_P2P)
		endpoint = &sc->sc_dst;
	else { /* VXLAN_TMODE_LEARNING || VXLAN_TMODE_ENDPOINT */
		struct ether_header *eh = mtod(m, struct ether_header *);

		smr_read_enter();
		endpoint = etherbridge_resolve_ea(&sc->sc_eb,
		    (struct ether_addr *)eh->ether_dhost);
		if (endpoint != NULL) {
			gateway = *endpoint;
			endpoint = &gateway;
		}
		smr_read_leave();

		if (endpoint == NULL) {
			if (sc->sc_mode == VXLAN_TMODE_ENDPOINT)
				goto drop;

			/* "flood" to unknown destinations */
			endpoint = &sc->sc_dst;
		}
	}

	/* force prepend mbuf because of payload alignment */
	m0 = m_get(M_DONTWAIT, m->m_type);
	if (m0 == NULL)
		goto drop;

	m_align(m0, 0);
	m0->m_len = 0;

	M_MOVE_PKTHDR(m0, m);
	m0->m_next = m;

	m = m_prepend(m0, sizeof(*vh), M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	vh = mtod(m, struct vxlan_header *);
	*vh = sc->sc_header;

	m = m_prepend(m, sizeof(*uh), M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	uh = mtod(m, struct udphdr *);
	uh->uh_sport = sc->sc_port; /* XXX */
	uh->uh_dport = sc->sc_port;
	htobem16(&uh->uh_ulen, m->m_pkthdr.len);
	uh->uh_sum = htons(0);

	SET(m->m_pkthdr.csum_flags, M_UDP_CSUM_OUT);

	mtag = m_tag_get(PACKET_TAG_GRE, sizeof(ifp->if_index), M_NOWAIT);
	if (mtag == NULL)
		goto drop;

	*(int *)(mtag + 1) = ifp->if_index;
	m_tag_prepend(m, mtag);

	prio = sc->sc_txhprio;
	if (prio == IF_HDRPRIO_PACKET)
		prio = m->m_pkthdr.pf.prio;
	tos = IFQ_PRIO2TOS(prio);

	CLR(m->m_flags, M_BCAST|M_MCAST);
	m->m_pkthdr.ph_rtableid = sc->sc_rdomain;

#if NPF > 0
	pf_pkt_addr_changed(m);
#endif

	return ((*ip_encap)(sc, m, endpoint, tos));
drop:
	m_freem(m);
	return (NULL);
}

static struct mbuf *
vxlan_encap_ipv4(struct vxlan_softc *sc, struct mbuf *m,
    const union vxlan_addr *endpoint, uint8_t tos)
{
	struct ip *ip;

	m = m_prepend(m, sizeof(*ip), M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	ip = mtod(m, struct ip *);
	ip->ip_v = IPVERSION;
	ip->ip_hl = sizeof(*ip) >> 2;
	ip->ip_off = sc->sc_df;
	ip->ip_tos = tos;
	ip->ip_len = htons(m->m_pkthdr.len);
	ip->ip_ttl = sc->sc_ttl;
	ip->ip_p = IPPROTO_UDP;
	ip->ip_src = sc->sc_src.in4;
	ip->ip_dst = endpoint->in4;

	return (m);
}

#ifdef INET6
static struct mbuf *
vxlan_encap_ipv6(struct vxlan_softc *sc, struct mbuf *m,
    const union vxlan_addr *endpoint, uint8_t tos)
{
	struct ip6_hdr *ip6;
	int len = m->m_pkthdr.len;

	m = m_prepend(m, sizeof(*ip6), M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow = ISSET(m->m_pkthdr.csum_flags, M_FLOWID) ?
	    htonl(m->m_pkthdr.ph_flowid) : 0;
	ip6->ip6_vfc |= IPV6_VERSION;
	ip6->ip6_flow |= htonl((uint32_t)tos << 20);
	ip6->ip6_plen = htons(len);
	ip6->ip6_nxt = IPPROTO_UDP;
	ip6->ip6_hlim = sc->sc_ttl;
	ip6->ip6_src = sc->sc_src.in6;
	ip6->ip6_dst = endpoint->in6;

	if (sc->sc_df)
		SET(m->m_pkthdr.csum_flags, M_IPV6_DF_OUT);

	return (m);
}
#endif /* INET6 */

static int
vxlan_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
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

static int
vxlan_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	struct vxlan_softc *sc = ifp->if_softc;
	struct ifqueue *ifq = &ifp->if_snd;

	if (ifq_enqueue(ifq, m) != 0)
		return (ENOBUFS);

	task_add(ifq->ifq_softnet, &sc->sc_send_task);

	return (0);
}

static void
vxlan_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct vxlan_softc *sc = ifp->if_softc;

	task_add(ifq->ifq_softnet, &sc->sc_send_task);
}

static uint64_t
vxlan_send_ipv4(struct vxlan_softc *sc, struct mbuf_list *ml)
{
	struct ip_moptions imo;
	struct mbuf *m;
	uint64_t oerrors = 0;

	if (ml_empty(ml))
		return (0);

	memset(&imo, 0, sizeof(struct ip_moptions));
	imo.imo_ifidx = sc->sc_if_index0;
	imo.imo_ttl = sc->sc_ttl;

	NET_LOCK_SHARED();
	while ((m = ml_dequeue(ml)) != NULL) {
		if (ip_output(m, NULL, NULL, IP_RAWOUTPUT, &imo, NULL, 0) != 0)
			oerrors++;
	}
	NET_UNLOCK_SHARED();

	return (oerrors);
}

#ifdef INET6
static uint64_t
vxlan_send_ipv6(struct vxlan_softc *sc, struct mbuf_list *ml)
{
	struct ip6_moptions im6o;
	struct mbuf *m;
	uint64_t oerrors = 0;

	if (ml_empty(ml))
		return (0);

	memset(&im6o, 0, sizeof(struct ip6_moptions));
	im6o.im6o_ifidx = sc->sc_if_index0;
	im6o.im6o_hlim = sc->sc_ttl;

	NET_LOCK_SHARED();
	while ((m = ml_dequeue(ml)) != NULL) {
		if (ip6_output(m, NULL, NULL, 0, &im6o, NULL) != 0)
			oerrors++;
	}
	NET_UNLOCK_SHARED();

	return (oerrors);
}
#endif /* INET6 */

static void
vxlan_send(void *arg)
{
	struct vxlan_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mbuf *(*ip_encap)(struct vxlan_softc *, struct mbuf *,
	    const union vxlan_addr *, uint8_t);
	uint64_t (*ip_send)(struct vxlan_softc *, struct mbuf_list *);
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	uint64_t oerrors;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return;

	switch (sc->sc_af) {
	case AF_INET:
		ip_encap = vxlan_encap_ipv4;
		ip_send = vxlan_send_ipv4;
		break;
#ifdef INET6
	case AF_INET6:
		ip_encap = vxlan_encap_ipv6;
		ip_send = vxlan_send_ipv6;
		break;
#endif
	default:
		unhandled_af(sc->sc_af);
		/* NOTREACHED */
	}

	while ((m = ifq_dequeue(&ifp->if_snd)) != NULL) {
#if NBPFILTER > 0
		caddr_t if_bpf = READ_ONCE(ifp->if_bpf);
		if (if_bpf != NULL)
			bpf_mtap_ether(if_bpf, m, BPF_DIRECTION_OUT);
#endif
		m = vxlan_encap(sc, m, ip_encap);
		if (m == NULL)
			continue;

		ml_enqueue(&ml, m);
	}

	oerrors = (*ip_send)(sc, &ml);

	counters_add(ifp->if_counters, ifc_oerrors, oerrors);
}

static struct mbuf *
vxlan_input(void *arg, struct mbuf *m, struct ip *ip, struct ip6_hdr *ip6,
    void *uhp, int hlen, struct netstack *ns)
{
	struct vxlan_tep *vt = arg;
	union vxlan_addr addr;
	struct vxlan_peer key, *p;
	struct udphdr *uh;
	struct vxlan_header *vh;
	struct ether_header *eh;
	int vhlen = hlen + sizeof(*vh);
	struct mbuf *n;
	int off;
	in_port_t port;
	struct vxlan_softc *sc = NULL;
	struct ifnet *ifp;
	int rxhprio;
	uint8_t tos;

	if (m->m_pkthdr.len < vhlen)
		goto drop;

	uh = uhp;
	port = uh->uh_sport;

	if (ip != NULL) {
		memset(&addr, 0, sizeof(addr));
		addr.in4 = ip->ip_src;
		tos = ip->ip_tos;
	}
#ifdef INET6
	else {
		addr.in6 = ip6->ip6_src;
		tos = bemtoh32(&ip6->ip6_flow) >> 20;
	}
#endif

	if (m->m_len < vhlen) {
		m = m_pullup(m, vhlen);
		if (m == NULL)
			return (NULL);
	}

	/* can't use ip/ip6/uh after this */

	vh = (struct vxlan_header *)(mtod(m, caddr_t) + hlen);

	memset(&key, 0, sizeof(key));
	key.p_addr = addr;
	key.p_header.vxlan_flags = vh->vxlan_flags & htonl(VXLAN_F_I);
	key.p_header.vxlan_id = vh->vxlan_id & htonl(VXLAN_VNI_MASK);

	mtx_enter(&vt->vt_mtx);
	p = RBT_FIND(vxlan_peers, &vt->vt_peers, &key);
	if (p == NULL) {
		memset(&key.p_addr, 0, sizeof(key.p_addr));
		p = RBT_FIND(vxlan_peers, &vt->vt_peers, &key);
	}
	if (p != NULL)
		sc = vxlan_take(p->p_sc);
	mtx_leave(&vt->vt_mtx);

	if (sc == NULL)
		goto drop;

	ifp = &sc->sc_ac.ac_if;
	if (ISSET(ifp->if_flags, IFF_LINK0) && port != sc->sc_port)
		goto rele_drop;

	m_adj(m, vhlen);

	if (m->m_pkthdr.len < sizeof(*eh))
		goto rele_drop;

	if (m->m_len < sizeof(*eh)) {
		m = m_pullup(m, sizeof(*eh));
		if (m == NULL)
			goto rele;
	}

	n = m_getptr(m, sizeof(*eh), &off);
	if (n == NULL)
		goto rele_drop;

	if (!ALIGNED_POINTER(mtod(n, caddr_t) + off, uint32_t)) {
		n = m_dup_pkt(m, ETHER_ALIGN, M_NOWAIT);
		m_freem(m);
		if (n == NULL)
			goto rele;
		m = n;
	}

	if (sc->sc_mode == VXLAN_TMODE_LEARNING) {
		eh = mtod(m, struct ether_header *);
		etherbridge_map_ea(&sc->sc_eb, &addr,
		    (struct ether_addr *)eh->ether_shost);
	}

	rxhprio = sc->sc_rxhprio;
	switch (rxhprio) {
	case IF_HDRPRIO_PACKET:
		/* nop */
		break;
	case IF_HDRPRIO_OUTER:
		m->m_pkthdr.pf.prio = IFQ_TOS2PRIO(tos);
		break;
	default:
		m->m_pkthdr.pf.prio = rxhprio;
		break;
        }

	if_vinput(ifp, m, ns);
rele:
	vxlan_rele(sc);
	return (NULL);

rele_drop:
	vxlan_rele(sc);
drop:
	m_freem(m);
	return (NULL);
}

static int
vxlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct vxlan_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct ifbrparam *bparam = (struct ifbrparam *)data;
	struct ifnet *ifp0;
	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		break;
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = vxlan_up(sc);
			else
				error = 0;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = vxlan_down(sc);
		}
		break;

	case SIOCSIFXFLAGS:
		if ((ifp0 = if_get(sc->sc_if_index0)) != NULL) {
			ifsetlro(ifp0, ISSET(ifr->ifr_flags, IFXF_LRO));
			if_put(ifp0);
		}
		break;

	case SIOCSLIFPHYRTABLE:
		error = vxlan_set_rdomain(sc, ifr);
		break;
	case SIOCGLIFPHYRTABLE:
		error = vxlan_get_rdomain(sc, ifr);
		break;

	case SIOCSLIFPHYADDR:
		error = vxlan_set_tunnel(sc, (const struct if_laddrreq *)data);
		break;
	case SIOCGLIFPHYADDR:
		error = vxlan_get_tunnel(sc, (struct if_laddrreq *)data);
		break;
	case SIOCDIFPHYADDR:
		error = vxlan_del_tunnel(sc);
		break;

	case SIOCSVNETID:
		error = vxlan_set_vnetid(sc, ifr);
		break;
	case SIOCGVNETID:
		error = vxlan_get_vnetid(sc, ifr);
		break;
	case SIOCDVNETID:
		error = vxlan_del_vnetid(sc);
		break;

	case SIOCSIFPARENT:
		error = vxlan_set_parent(sc, (struct if_parent *)data);
		break;
	case SIOCGIFPARENT:
		error = vxlan_get_parent(sc, (struct if_parent *)data);
		break;
	case SIOCDIFPARENT:
		error = vxlan_del_parent(sc);
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

	case SIOCSLIFPHYDF:
		/* commit */
		sc->sc_df = ifr->ifr_df ? htons(IP_DF) : htons(0);
		break;
	case SIOCGLIFPHYDF:
		ifr->ifr_df = sc->sc_df ? 1 : 0;
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

	case SIOCBRDGSCACHE:
		error = etherbridge_set_max(&sc->sc_eb, bparam);
		break;
	case SIOCBRDGGCACHE:
		error = etherbridge_get_max(&sc->sc_eb, bparam);
		break;
	case SIOCBRDGSTO:
		error = etherbridge_set_tmo(&sc->sc_eb, bparam);
		break;
	case SIOCBRDGGTO:
		error = etherbridge_get_tmo(&sc->sc_eb, bparam);
		break;

	case SIOCBRDGRTS:
		error = etherbridge_rtfind(&sc->sc_eb,
		    (struct ifbaconf *)data);
		break;
	case SIOCBRDGFLUSH:
		etherbridge_flush(&sc->sc_eb,
		    ((struct ifbreq *)data)->ifbr_ifsflags);
		break;
	case SIOCBRDGSADDR:
		error = vxlan_add_addr(sc, (struct ifbareq *)data);
		break;
	case SIOCBRDGDADDR:
		error = vxlan_del_addr(sc, (struct ifbareq *)data);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* no hardware to program */
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

static struct vxlan_tep *
vxlan_tep_get(struct vxlan_softc *sc, const union vxlan_addr *addr)
{
	struct vxlan_tep *vt;

	TAILQ_FOREACH(vt, &vxlan_teps, vt_entry) {
		if (sc->sc_af == vt->vt_af &&
		    sc->sc_rdomain == vt->vt_rdomain &&
		    memcmp(addr, &vt->vt_addr, sizeof(*addr)) == 0 &&
		    sc->sc_port == vt->vt_port)
			return (vt);
	}

	return (NULL);
}

static int
vxlan_tep_add_addr(struct vxlan_softc *sc, const union vxlan_addr *addr,
    struct vxlan_peer *p)
{
	struct mbuf m;
	struct vxlan_tep *vt;
	struct socket *so;
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif
	int error;

	vt = vxlan_tep_get(sc, addr);
	if (vt != NULL) {
		struct vxlan_peer *op;

		mtx_enter(&vt->vt_mtx);
		op = RBT_INSERT(vxlan_peers, &vt->vt_peers, p);
		mtx_leave(&vt->vt_mtx);

		if (op != NULL)
			return (EADDRINUSE);

		return (0);
	}

	vt = malloc(sizeof(*vt), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (vt == NULL)
		return (ENOMEM);

	vt->vt_af = sc->sc_af;
	vt->vt_rdomain = sc->sc_rdomain;
	vt->vt_addr = *addr;
	vt->vt_port = sc->sc_port;

	mtx_init(&vt->vt_mtx, IPL_SOFTNET);
	RBT_INIT(vxlan_peers, &vt->vt_peers);
	RBT_INSERT(vxlan_peers, &vt->vt_peers, p);

	error = socreate(vt->vt_af, &so, SOCK_DGRAM, IPPROTO_UDP);
	if (error != 0)
		goto free;

	solock_shared(so);
	sotoinpcb(so)->inp_upcall = vxlan_input;
	sotoinpcb(so)->inp_upcall_arg = vt;
	sounlock_shared(so);

	m_inithdr(&m);
	m.m_len = sizeof(vt->vt_rdomain);
	*mtod(&m, unsigned int *) = vt->vt_rdomain;
	error = sosetopt(so, SOL_SOCKET, SO_RTABLE, &m);
	if (error != 0)
		goto close;

	m_inithdr(&m);
	switch (vt->vt_af) {
	case AF_INET:
		sin = mtod(&m, struct sockaddr_in *);
		memset(sin, 0, sizeof(*sin));
		sin->sin_len = sizeof(*sin);
		sin->sin_family = AF_INET;
		sin->sin_addr = addr->in4;
		sin->sin_port = vt->vt_port;

		m.m_len = sizeof(*sin);
		break;

#ifdef INET6
	case AF_INET6:
		sin6 = mtod(&m, struct sockaddr_in6 *);
		sin6->sin6_len = sizeof(*sin6);
		sin6->sin6_family = AF_INET6;
		in6_recoverscope(sin6, &addr->in6);
		sin6->sin6_port = sc->sc_port;

		m.m_len = sizeof(*sin6);
		break;
#endif
	default:
		unhandled_af(vt->vt_af);
	}

	solock_shared(so);
	error = sobind(so, &m, curproc);
	sounlock_shared(so);
	if (error != 0)
		goto close;

	rw_assert_wrlock(&vxlan_lock);
	TAILQ_INSERT_TAIL(&vxlan_teps, vt, vt_entry);

	vt->vt_so = so;

	return (0);

close:
	soclose(so, MSG_DONTWAIT);
free:
	free(vt, M_DEVBUF, sizeof(*vt));
	return (error);
}

static void
vxlan_tep_del_addr(struct vxlan_softc *sc, const union vxlan_addr *addr,
    struct vxlan_peer *p)
{
	struct vxlan_tep *vt;
	int empty;

	vt = vxlan_tep_get(sc, addr);
	if (vt == NULL)
		panic("unable to find vxlan_tep for peer %p (sc %p)", p, sc);

	mtx_enter(&vt->vt_mtx);
	RBT_REMOVE(vxlan_peers, &vt->vt_peers, p);
	empty = RBT_EMPTY(vxlan_peers, &vt->vt_peers);
	mtx_leave(&vt->vt_mtx);

	if (!empty)
		return;

	rw_assert_wrlock(&vxlan_lock);
	TAILQ_REMOVE(&vxlan_teps, vt, vt_entry);

	soclose(vt->vt_so, MSG_DONTWAIT);
	free(vt, M_DEVBUF, sizeof(*vt));
}

static int
vxlan_tep_up(struct vxlan_softc *sc)
{
	struct vxlan_peer *up, *mp;
	int error;

	up = malloc(sizeof(*up), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (up == NULL)
		return (ENOMEM);

	if (sc->sc_mode == VXLAN_TMODE_P2P)
		up->p_addr = sc->sc_dst;
	up->p_header = sc->sc_header;
	up->p_sc = vxlan_take(sc);

	error = vxlan_tep_add_addr(sc, &sc->sc_src, up);
	if (error != 0)
		goto freeup;

	sc->sc_ucast_peer = up;

	if (sc->sc_mode != VXLAN_TMODE_LEARNING)
		return (0);

	mp = malloc(sizeof(*mp), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (mp == NULL) {
		error = ENOMEM;
		goto delup;
	}

	/* addr is multicast, leave it as 0s */
	mp->p_header = sc->sc_header;
	mp->p_sc = vxlan_take(sc);

	/* destination address is a multicast group we want to join */
	error = vxlan_tep_add_addr(sc, &sc->sc_dst, up);
	if (error != 0)
		goto freemp;

	sc->sc_mcast_peer = mp;

	return (0);

freemp:
	vxlan_rele(mp->p_sc);
	free(mp, M_DEVBUF, sizeof(*mp));
delup:
	vxlan_tep_del_addr(sc, &sc->sc_src, up);
freeup:
	vxlan_rele(up->p_sc);
	free(up, M_DEVBUF, sizeof(*up));
	return (error);
}

static void
vxlan_tep_down(struct vxlan_softc *sc)
{
	struct vxlan_peer *up = sc->sc_ucast_peer;

	if (sc->sc_mode == VXLAN_TMODE_LEARNING) {
		struct vxlan_peer *mp = sc->sc_mcast_peer;
		vxlan_tep_del_addr(sc, &sc->sc_dst, mp);
		vxlan_rele(mp->p_sc);
		free(mp, M_DEVBUF, sizeof(*mp));
	}

	vxlan_tep_del_addr(sc, &sc->sc_src, up);
	vxlan_rele(up->p_sc);
	free(up, M_DEVBUF, sizeof(*up));
}

static int
vxlan_up(struct vxlan_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ifnet *ifp0 = NULL;
	int error;

	KASSERT(!ISSET(ifp->if_flags, IFF_RUNNING));
	NET_ASSERT_LOCKED();

	if (sc->sc_af == AF_UNSPEC)
		return (EDESTADDRREQ);
	KASSERT(sc->sc_mode != VXLAN_TMODE_UNSET);

	NET_UNLOCK();

	error = rw_enter(&vxlan_lock, RW_WRITE|RW_INTR);
	if (error != 0)
		goto netlock;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		/* something else beat us */
		rw_exit(&vxlan_lock);
		return (0);
	}
	NET_UNLOCK();

	if (sc->sc_mode != VXLAN_TMODE_P2P) {
		error = etherbridge_up(&sc->sc_eb);
		if (error != 0)
			goto unlock;
	}

	if (sc->sc_mode == VXLAN_TMODE_LEARNING) {
		ifp0 = if_get(sc->sc_if_index0);
		if (ifp0 == NULL) {
			error = ENXIO;
			goto down;
		}

		/* check again if multicast will work on top of the parent */
		if (!ISSET(ifp0->if_flags, IFF_MULTICAST)) {
			error = EPROTONOSUPPORT;
			goto put;
		}

		error = vxlan_addmulti(sc, ifp0);
		if (error != 0)
			goto put;

		/* Register callback if parent wants to unregister */
		if_detachhook_add(ifp0, &sc->sc_dtask);
	} else {
		if (sc->sc_if_index0 != 0) {
			error = EPROTONOSUPPORT;
			goto down;
		}
	}

	error = vxlan_tep_up(sc);
	if (error != 0)
		goto del;

	if_put(ifp0);

	NET_LOCK();
	SET(ifp->if_flags, IFF_RUNNING);
	rw_exit(&vxlan_lock);

	return (0);

del:
	if (sc->sc_mode == VXLAN_TMODE_LEARNING) {
		if (ifp0 != NULL)
			if_detachhook_del(ifp0, &sc->sc_dtask);
		vxlan_delmulti(sc);
	}
put:
	if_put(ifp0);
down:
	if (sc->sc_mode != VXLAN_TMODE_P2P)
		etherbridge_down(&sc->sc_eb);
unlock:
	rw_exit(&vxlan_lock);
netlock:
	NET_LOCK();

	return (error);
}

static int
vxlan_down(struct vxlan_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ifnet *ifp0;
	int error;

	KASSERT(ISSET(ifp->if_flags, IFF_RUNNING));
	NET_UNLOCK();

	error = rw_enter(&vxlan_lock, RW_WRITE|RW_INTR);
	if (error != 0) {
		NET_LOCK();
		return (error);
	}

	NET_LOCK();
	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		/* something else beat us */
		rw_exit(&vxlan_lock);
		return (0);
	}
	NET_UNLOCK();

	vxlan_tep_down(sc);

	if (sc->sc_mode == VXLAN_TMODE_LEARNING) {
		vxlan_delmulti(sc);
		ifp0 = if_get(sc->sc_if_index0);
		if (ifp0 != NULL) {
			if_detachhook_del(ifp0, &sc->sc_dtask);
		}
		if_put(ifp0);
	}

	if (sc->sc_mode != VXLAN_TMODE_P2P)
		etherbridge_down(&sc->sc_eb);

	taskq_del_barrier(ifp->if_snd.ifq_softnet, &sc->sc_send_task);
	NET_LOCK();
	CLR(ifp->if_flags, IFF_RUNNING);
	rw_exit(&vxlan_lock);

	return (0);
}

static int
vxlan_addmulti(struct vxlan_softc *sc, struct ifnet *ifp0)
{
	int error = 0;

	NET_LOCK();

	switch (sc->sc_af) {
	case AF_INET:
		sc->sc_inmulti = in_addmulti(&sc->sc_dst.in4, ifp0);
		if (sc->sc_inmulti == NULL)
			error = EADDRNOTAVAIL;
		break;
#ifdef INET6
	case AF_INET6:
		sc->sc_inmulti = in6_addmulti(&sc->sc_dst.in6, ifp0, &error);
		break;
#endif
	default:
		unhandled_af(sc->sc_af);
	}

	NET_UNLOCK();

	return (error);
}

static void
vxlan_delmulti(struct vxlan_softc *sc)
{
	NET_LOCK();

	switch (sc->sc_af) {
	case AF_INET:
		in_delmulti(sc->sc_inmulti);
		break;
#ifdef INET6
	case AF_INET6:
		in6_delmulti(sc->sc_inmulti);
		break;
#endif
	default:
		unhandled_af(sc->sc_af);
	}

	sc->sc_inmulti = NULL; /* keep it tidy */

	NET_UNLOCK();
}

static int
vxlan_set_rdomain(struct vxlan_softc *sc, const struct ifreq *ifr)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (ifr->ifr_rdomainid < 0 ||
	    ifr->ifr_rdomainid > RT_TABLEID_MAX)
		return (EINVAL);
	if (!rtable_exists(ifr->ifr_rdomainid))
		return (EADDRNOTAVAIL);

	if (sc->sc_rdomain == ifr->ifr_rdomainid)
		return (0);

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	sc->sc_rdomain = ifr->ifr_rdomainid;
	etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);

	return (0);
}

static int
vxlan_get_rdomain(struct vxlan_softc *sc, struct ifreq *ifr)
{
	ifr->ifr_rdomainid = sc->sc_rdomain;

	return (0);
}

static int
vxlan_set_tunnel(struct vxlan_softc *sc, const struct if_laddrreq *req)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct sockaddr *src = (struct sockaddr *)&req->addr;
	struct sockaddr *dst = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *src4, *dst4;
#ifdef INET6
	struct sockaddr_in6 *src6, *dst6;
	int error;
#endif
	union vxlan_addr saddr, daddr;
	unsigned int mode = VXLAN_TMODE_ENDPOINT;
	in_port_t port = htons(VXLAN_PORT);

	memset(&saddr, 0, sizeof(saddr));
	memset(&daddr, 0, sizeof(daddr));

	/* validate */
	switch (src->sa_family) {
	case AF_INET:
		src4 = (struct sockaddr_in *)src;
		if (in_nullhost(src4->sin_addr) ||
		    IN_MULTICAST(src4->sin_addr.s_addr))
			return (EINVAL);

		if (src4->sin_port != htons(0))
			port = src4->sin_port;

		if (dst->sa_family != AF_UNSPEC) {
			if (dst->sa_family != AF_INET)
				return (EINVAL);

			dst4 = (struct sockaddr_in *)dst;
			if (in_nullhost(dst4->sin_addr))
				return (EINVAL);

			if (dst4->sin_port != htons(0))
				return (EINVAL);

			/* all good */
			mode = IN_MULTICAST(dst4->sin_addr.s_addr) ?
			    VXLAN_TMODE_LEARNING : VXLAN_TMODE_P2P;
			daddr.in4 = dst4->sin_addr;
		}

		saddr.in4 = src4->sin_addr;
		break;

#ifdef INET6
	case AF_INET6:
		src6 = (struct sockaddr_in6 *)src;
		if (IN6_IS_ADDR_UNSPECIFIED(&src6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&src6->sin6_addr))
			return (EINVAL);

		if (src6->sin6_port != htons(0))
			port = src6->sin6_port;

		if (dst->sa_family != AF_UNSPEC) {
			if (dst->sa_family != AF_INET6)
				return (EINVAL);

			dst6 = (struct sockaddr_in6 *)dst;
			if (IN6_IS_ADDR_UNSPECIFIED(&dst6->sin6_addr))
				return (EINVAL);

			if (src6->sin6_scope_id != dst6->sin6_scope_id)
				return (EINVAL);

			if (dst6->sin6_port != htons(0))
				return (EINVAL);

			/* all good */
			mode = IN6_IS_ADDR_MULTICAST(&dst6->sin6_addr) ?
			    VXLAN_TMODE_LEARNING : VXLAN_TMODE_P2P;
			error = in6_embedscope(&daddr.in6, dst6, NULL, NULL);
			if (error != 0)
				return (error);
		}

		error = in6_embedscope(&saddr.in6, src6, NULL, NULL);
		if (error != 0)
			return (error);

		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}

	if (memcmp(&sc->sc_src, &saddr, sizeof(sc->sc_src)) == 0 &&
	    memcmp(&sc->sc_dst, &daddr, sizeof(sc->sc_dst)) == 0 &&
	    sc->sc_port == port)
		return (0);

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	sc->sc_af = src->sa_family;
	sc->sc_src = saddr;
	sc->sc_dst = daddr;
	sc->sc_port = port;
	sc->sc_mode = mode;
	etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);

	return (0);
}

static int
vxlan_get_tunnel(struct vxlan_softc *sc, struct if_laddrreq *req)
{
	struct sockaddr *dstaddr = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif

	if (sc->sc_af == AF_UNSPEC)
		return (EADDRNOTAVAIL);
	KASSERT(sc->sc_mode != VXLAN_TMODE_UNSET);

	memset(&req->addr, 0, sizeof(req->addr));
	memset(&req->dstaddr, 0, sizeof(req->dstaddr));

	/* default to endpoint */
	dstaddr->sa_len = 2;
	dstaddr->sa_family = AF_UNSPEC;

	switch (sc->sc_af) {
	case AF_INET:
		sin = (struct sockaddr_in *)&req->addr;
		sin->sin_len = sizeof(*sin);
		sin->sin_family = AF_INET;
		sin->sin_addr = sc->sc_src.in4;
		sin->sin_port = sc->sc_port;

		if (sc->sc_mode == VXLAN_TMODE_ENDPOINT)
			break;

		sin = (struct sockaddr_in *)&req->dstaddr;
		sin->sin_len = sizeof(*sin);
		sin->sin_family = AF_INET;
		sin->sin_addr = sc->sc_dst.in4;
		break;

#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&req->addr;
		sin6->sin6_len = sizeof(*sin6);
		sin6->sin6_family = AF_INET6;
		in6_recoverscope(sin6, &sc->sc_src.in6);
		sin6->sin6_port = sc->sc_port;

		if (sc->sc_mode == VXLAN_TMODE_ENDPOINT)
			break;

		sin6 = (struct sockaddr_in6 *)&req->dstaddr;
		sin6->sin6_len = sizeof(*sin6);
		sin6->sin6_family = AF_INET6;
		in6_recoverscope(sin6, &sc->sc_dst.in6);
		break;
#endif
	default:
		unhandled_af(sc->sc_af);
	}

	return (0);
}

static int
vxlan_del_tunnel(struct vxlan_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (sc->sc_af == AF_UNSPEC)
		return (0);

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	sc->sc_af = AF_UNSPEC;
	memset(&sc->sc_src, 0, sizeof(sc->sc_src));
	memset(&sc->sc_dst, 0, sizeof(sc->sc_dst));
	sc->sc_port = htons(0);
	sc->sc_mode = VXLAN_TMODE_UNSET;
	etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);

	return (0);
}

static int
vxlan_set_vnetid(struct vxlan_softc *sc, const struct ifreq *ifr)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t vni;

	if (ifr->ifr_vnetid < VXLAN_VNI_MIN ||
	    ifr->ifr_vnetid > VXLAN_VNI_MAX)
		return (EINVAL);

	vni = htonl(ifr->ifr_vnetid << VXLAN_VNI_SHIFT);
	if (ISSET(sc->sc_header.vxlan_flags, htonl(VXLAN_F_I)) &&
	    sc->sc_header.vxlan_id == vni)
		return (0);

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	SET(sc->sc_header.vxlan_flags, htonl(VXLAN_F_I));
	sc->sc_header.vxlan_id = vni;
	etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);

	return (0);
}

static int
vxlan_get_vnetid(struct vxlan_softc *sc, struct ifreq *ifr)
{
	uint32_t vni;

	if (!ISSET(sc->sc_header.vxlan_flags, htonl(VXLAN_F_I)))
		return (EADDRNOTAVAIL);

	vni = ntohl(sc->sc_header.vxlan_id);
	vni &= VXLAN_VNI_MASK;
	vni >>= VXLAN_VNI_SHIFT;

	ifr->ifr_vnetid = vni;

	return (0);
}

static int
vxlan_del_vnetid(struct vxlan_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (!ISSET(sc->sc_header.vxlan_flags, htonl(VXLAN_F_I)))
		return (0);

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	CLR(sc->sc_header.vxlan_flags, htonl(VXLAN_F_I));
	sc->sc_header.vxlan_id = htonl(0 << VXLAN_VNI_SHIFT);
	etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);

	return (0);
}

static int
vxlan_set_parent(struct vxlan_softc *sc, const struct if_parent *p)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ifnet *ifp0;
	int error = 0;

	ifp0 = if_unit(p->ifp_parent);
	if (ifp0 == NULL)
		return (ENXIO);

	if (!ISSET(ifp0->if_flags, IFF_MULTICAST)) {
		error = ENXIO;
		goto put;
	}

	if (sc->sc_if_index0 == ifp0->if_index)
		goto put;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		error = EBUSY;
		goto put;
	}

	ifsetlro(ifp0, 0);

	/* commit */
	sc->sc_if_index0 = ifp0->if_index;
	etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);

put:
	if_put(ifp0);
	return (error);
}

static int
vxlan_get_parent(struct vxlan_softc *sc, struct if_parent *p)
{
	struct ifnet *ifp0;
	int error = 0;

	ifp0 = if_get(sc->sc_if_index0);
	if (ifp0 == NULL)
		error = EADDRNOTAVAIL;
	else
		strlcpy(p->ifp_parent, ifp0->if_xname, sizeof(p->ifp_parent));
	if_put(ifp0);

	return (error);
}

static int
vxlan_del_parent(struct vxlan_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (sc->sc_if_index0 == 0)
		return (0);

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	sc->sc_if_index0 = 0;
	etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);

	return (0);
}

static int
vxlan_add_addr(struct vxlan_softc *sc, const struct ifbareq *ifba)
{
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct sockaddr_in6 src6 = {
		.sin6_len = sizeof(src6),
		.sin6_family = AF_UNSPEC,
	};
	int error;
#endif
	union vxlan_addr endpoint;
	unsigned int type;

	switch (sc->sc_mode) {
	case VXLAN_TMODE_UNSET:
		return (ENOPROTOOPT);
	case VXLAN_TMODE_P2P:
		return (EPROTONOSUPPORT);
	default:
		break;
	}

	/* ignore ifba_ifsname */

	if (ISSET(ifba->ifba_flags, ~IFBAF_TYPEMASK))
		return (EINVAL);
	switch (ifba->ifba_flags & IFBAF_TYPEMASK) {
	case IFBAF_DYNAMIC:
		type = EBE_DYNAMIC;
		break;
	case IFBAF_STATIC:
		type = EBE_STATIC;
		break;
	default:
		return (EINVAL);
	}

	memset(&endpoint, 0, sizeof(endpoint));

	if (ifba->ifba_dstsa.ss_family != sc->sc_af)
		return (EAFNOSUPPORT);
	switch (ifba->ifba_dstsa.ss_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)&ifba->ifba_dstsa;
		if (in_nullhost(sin->sin_addr) ||
		    IN_MULTICAST(sin->sin_addr.s_addr))
			return (EADDRNOTAVAIL);

		if (sin->sin_port != htons(0))
			return (EADDRNOTAVAIL);

		endpoint.in4 = sin->sin_addr;
		break;

#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&ifba->ifba_dstsa;
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			return (EADDRNOTAVAIL);

		in6_recoverscope(&src6, &sc->sc_src.in6);
		if (src6.sin6_scope_id != sin6->sin6_scope_id)
			return (EADDRNOTAVAIL);

		if (sin6->sin6_port != htons(0))
			return (EADDRNOTAVAIL);

		error = in6_embedscope(&endpoint.in6, sin6, NULL, NULL);
		if (error != 0)
			return (error);

		break;
#endif
	default: /* AF_UNSPEC */
		return (EADDRNOTAVAIL);
	}

	return (etherbridge_add_addr(&sc->sc_eb, &endpoint,
	    &ifba->ifba_dst, type));
}

static int
vxlan_del_addr(struct vxlan_softc *sc, const struct ifbareq *ifba)
{
	return (etherbridge_del_addr(&sc->sc_eb, &ifba->ifba_dst));
}

void
vxlan_detach_hook(void *arg)
{
	struct vxlan_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		vxlan_down(sc);
		CLR(ifp->if_flags, IFF_UP);
	}

	sc->sc_if_index0 = 0;
}

static int
vxlan_eb_port_eq(void *arg, void *a, void *b)
{
	const union vxlan_addr *va = a, *vb = b;
	size_t i;

	for (i = 0; i < nitems(va->in6.s6_addr32); i++) {
		if (va->in6.s6_addr32[i] != vb->in6.s6_addr32[i])
			return (0);
	}

	return (1);
}

static void *
vxlan_eb_port_take(void *arg, void *port)
{
	union vxlan_addr *endpoint;

	endpoint = pool_get(&vxlan_endpoint_pool, PR_NOWAIT);
	if (endpoint == NULL)
		return (NULL);

	*endpoint = *(union vxlan_addr *)port;

	return (endpoint);
}

static void
vxlan_eb_port_rele(void *arg, void *port)
{
	union vxlan_addr *endpoint = port;

	pool_put(&vxlan_endpoint_pool, endpoint);
}

static size_t
vxlan_eb_port_ifname(void *arg, char *dst, size_t len, void *port)
{
	struct vxlan_softc *sc = arg;

	return (strlcpy(dst, sc->sc_ac.ac_if.if_xname, len));
}

static void
vxlan_eb_port_sa(void *arg, struct sockaddr_storage *ss, void *port)
{
	struct vxlan_softc *sc = arg;
	union vxlan_addr *endpoint = port;

	switch (sc->sc_af) {
	case AF_INET: {
		struct sockaddr_in *sin = (struct sockaddr_in *)ss;

		sin->sin_len = sizeof(*sin);
		sin->sin_family = AF_INET;
		sin->sin_addr = endpoint->in4;
		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;

		sin6->sin6_len = sizeof(*sin6);
		sin6->sin6_family = AF_INET6;
		in6_recoverscope(sin6, &endpoint->in6);
		break;
	}
#endif /* INET6 */
	default:
		unhandled_af(sc->sc_af);
	}
}

static inline int
vxlan_peer_cmp(const struct vxlan_peer *ap, const struct vxlan_peer *bp)
{
	size_t i;

	if (ap->p_header.vxlan_id > bp->p_header.vxlan_id)
		return (1);
	if (ap->p_header.vxlan_id < bp->p_header.vxlan_id)
		return (-1);
	if (ap->p_header.vxlan_flags > bp->p_header.vxlan_flags)
		return (1);
	if (ap->p_header.vxlan_flags < bp->p_header.vxlan_flags)
		return (-1);

	for (i = 0; i < nitems(ap->p_addr.in6.s6_addr32); i++) {
		if (ap->p_addr.in6.s6_addr32[i] >
		    bp->p_addr.in6.s6_addr32[i])
			return (1);
		if (ap->p_addr.in6.s6_addr32[i] <
		    bp->p_addr.in6.s6_addr32[i])
			return (-1);
	}

	return (0);
}

RBT_GENERATE(vxlan_peers, vxlan_peer, p_entry, vxlan_peer_cmp);

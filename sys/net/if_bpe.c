/*	$OpenBSD: if_bpe.c,v 1.25 2025/07/07 02:28:50 jsg Exp $ */
/*
 * Copyright (c) 2018 David Gwynne <dlg@openbsd.org>
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
#include <sys/smr.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

/* for bridge stuff */
#include <net/if_bridge.h>
#include <net/if_etherbridge.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <net/if_bpe.h>

#define PBB_ITAG_ISID		0x00ffffff
#define PBB_ITAG_ISID_MIN	0x00000000
#define PBB_ITAG_ISID_MAX	0x00ffffff
#define PBB_ITAG_RES2		0x03000000	/* must be zero on input */
#define PBB_ITAG_RES1		0x04000000	/* ignore on input */
#define PBB_ITAG_UCA		0x08000000
#define PBB_ITAG_DEI		0x10000000
#define PBB_ITAG_PCP_SHIFT	29
#define PBB_ITAG_PCP_MASK	(0x7U << PBB_ITAG_PCP_SHIFT)

#define BPE_BRIDGE_AGE_TMO	100 /* seconds */

struct bpe_key {
	int			k_if;
	uint32_t		k_isid;

	RBT_ENTRY(bpe_tunnel)	k_entry;
};

RBT_HEAD(bpe_tree, bpe_key);

static inline int bpe_cmp(const struct bpe_key *, const struct bpe_key *);

RBT_PROTOTYPE(bpe_tree, bpe_key, k_entry, bpe_cmp);
RBT_GENERATE(bpe_tree, bpe_key, k_entry, bpe_cmp);

struct bpe_softc {
	struct bpe_key		sc_key; /* must be first */
	struct arpcom		sc_ac;
	int			sc_txhprio;
	int			sc_rxhprio;
	struct ether_addr	sc_group;

	struct task		sc_ltask;
	struct task		sc_dtask;

	struct etherbridge	sc_eb;
};

void		bpeattach(int);

static int	bpe_clone_create(struct if_clone *, int);
static int	bpe_clone_destroy(struct ifnet *);

static void	bpe_start(struct ifnet *);
static int	bpe_ioctl(struct ifnet *, u_long, caddr_t);
static int	bpe_media_get(struct bpe_softc *, struct ifreq *);
static int	bpe_up(struct bpe_softc *);
static int	bpe_down(struct bpe_softc *);
static int	bpe_multi(struct bpe_softc *, struct ifnet *, u_long);
static int	bpe_set_vnetid(struct bpe_softc *, const struct ifreq *);
static void	bpe_set_group(struct bpe_softc *, uint32_t);
static int	bpe_set_parent(struct bpe_softc *, const struct if_parent *);
static int	bpe_get_parent(struct bpe_softc *, struct if_parent *);
static int	bpe_del_parent(struct bpe_softc *);
static int	bpe_add_addr(struct bpe_softc *, const struct ifbareq *);
static int	bpe_del_addr(struct bpe_softc *, const struct ifbareq *);

static void	bpe_link_hook(void *);
static void	bpe_link_state(struct bpe_softc *, u_char, uint64_t);
static void	bpe_detach_hook(void *);

static struct if_clone bpe_cloner =
    IF_CLONE_INITIALIZER("bpe", bpe_clone_create, bpe_clone_destroy);

static int	 bpe_eb_port_eq(void *, void *, void *);
static void	*bpe_eb_port_take(void *, void *);
static void	 bpe_eb_port_rele(void *, void *);
static size_t	 bpe_eb_port_ifname(void *, char *, size_t, void *);
static void	 bpe_eb_port_sa(void *, struct sockaddr_storage *, void *);

static const struct etherbridge_ops bpe_etherbridge_ops = {
	bpe_eb_port_eq,
	bpe_eb_port_take,
	bpe_eb_port_rele,
	bpe_eb_port_ifname,
	bpe_eb_port_sa,
};

static struct bpe_tree bpe_interfaces = RBT_INITIALIZER();
static struct rwlock bpe_lock = RWLOCK_INITIALIZER("bpeifs");
static struct pool bpe_endpoint_pool;

void
bpeattach(int count)
{
	if_clone_attach(&bpe_cloner);
}

static int
bpe_clone_create(struct if_clone *ifc, int unit)
{
	struct bpe_softc *sc;
	struct ifnet *ifp;
	int error;

	if (bpe_endpoint_pool.pr_size == 0) {
		pool_init(&bpe_endpoint_pool, sizeof(struct ether_addr), 0,
		    IPL_NONE, 0, "bpepl", NULL);
	}

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);

	ifp = &sc->sc_ac.ac_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	error = etherbridge_init(&sc->sc_eb, ifp->if_xname,
	    &bpe_etherbridge_ops, sc);
	if (error == -1) {
		free(sc, M_DEVBUF, sizeof(*sc));
		return (error);
	}

	sc->sc_key.k_if = 0;
	sc->sc_key.k_isid = 0;
	bpe_set_group(sc, 0);

	sc->sc_txhprio = IF_HDRPRIO_PACKET;
	sc->sc_rxhprio = IF_HDRPRIO_OUTER;

	task_set(&sc->sc_ltask, bpe_link_hook, sc);
	task_set(&sc->sc_dtask, bpe_detach_hook, sc);

	ifp->if_softc = sc;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_ioctl = bpe_ioctl;
	ifp->if_start = bpe_start;
	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED;
	ether_fakeaddr(ifp);

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

	return (0);
}

static int
bpe_clone_destroy(struct ifnet *ifp)
{
	struct bpe_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		bpe_down(sc);
	NET_UNLOCK();

	ether_ifdetach(ifp);
	if_detach(ifp);

	etherbridge_destroy(&sc->sc_eb);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static void
bpe_start(struct ifnet *ifp)
{
	struct bpe_softc *sc = ifp->if_softc;
	struct ifnet *ifp0;
	struct mbuf *m0, *m;
	struct ether_header *ceh;
	struct ether_header *beh;
	uint32_t itag, *itagp;
	int hlen = sizeof(*beh) + sizeof(*itagp);
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif
	int txprio;
	uint8_t prio;

	ifp0 = if_get(sc->sc_key.k_if);
	if (ifp0 == NULL || !ISSET(ifp0->if_flags, IFF_RUNNING)) {
		ifq_purge(&ifp->if_snd);
		goto done;
	}

	txprio = sc->sc_txhprio;

	while ((m0 = ifq_dequeue(&ifp->if_snd)) != NULL) {
#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_ether(if_bpf, m0, BPF_DIRECTION_OUT);
#endif

		ceh = mtod(m0, struct ether_header *);

		/* force prepend of a whole mbuf because of alignment */
		m = m_get(M_DONTWAIT, m0->m_type);
		if (m == NULL) {
			m_freem(m0);
			continue;
		}

		M_MOVE_PKTHDR(m, m0);
		m->m_next = m0;

		m_align(m, 0);
		m->m_len = 0;

		m = m_prepend(m, hlen, M_DONTWAIT);
		if (m == NULL)
			continue;

		beh = mtod(m, struct ether_header *);

		if (ETHER_IS_BROADCAST(ceh->ether_dhost)) {
			memcpy(beh->ether_dhost, &sc->sc_group,
			    sizeof(beh->ether_dhost));
		} else {
			struct ether_addr *endpoint;

			smr_read_enter();
			endpoint = etherbridge_resolve_ea(&sc->sc_eb,
			    (struct ether_addr *)ceh->ether_dhost);
			if (endpoint == NULL) {
				/* "flood" to unknown hosts */
				endpoint = &sc->sc_group;
			}
			memcpy(beh->ether_dhost, endpoint,
			    sizeof(beh->ether_dhost));
			smr_read_leave();
		}

		memcpy(beh->ether_shost, ((struct arpcom *)ifp0)->ac_enaddr,
		    sizeof(beh->ether_shost));
		beh->ether_type = htons(ETHERTYPE_PBB);

		prio = (txprio == IF_HDRPRIO_PACKET) ?
		    m->m_pkthdr.pf.prio : txprio;

		itag = sc->sc_key.k_isid;
		itag |= prio << PBB_ITAG_PCP_SHIFT;
		itagp = (uint32_t *)(beh + 1);

		htobem32(itagp, itag);

		if_enqueue(ifp0, m);
	}

done:
	if_put(ifp0);
}

static int
bpe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct bpe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct ifbrparam *bparam = (struct ifbrparam *)data;
	struct ifnet *ifp0;
	int error = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = bpe_up(sc);
			else
				error = 0;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = bpe_down(sc);
		}
		break;

	case SIOCSIFXFLAGS:
		if ((ifp0 = if_get(sc->sc_key.k_if)) != NULL) {
			ifsetlro(ifp0, ISSET(ifr->ifr_flags, IFXF_LRO));
			if_put(ifp0);
		}
		break;

	case SIOCSVNETID:
		error = bpe_set_vnetid(sc, ifr);
		break;
	case SIOCGVNETID:
		ifr->ifr_vnetid = sc->sc_key.k_isid;
		break;

	case SIOCSIFPARENT:
		error = bpe_set_parent(sc, (struct if_parent *)data);
		break;
	case SIOCGIFPARENT:
		error = bpe_get_parent(sc, (struct if_parent *)data);
		break;
	case SIOCDIFPARENT:
		error = bpe_del_parent(sc);
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

	case SIOCGIFMEDIA:
		error = bpe_media_get(sc, ifr);
		break;

	case SIOCBRDGSCACHE:
		error = suser(curproc);
		if (error != 0)
			break;

		error = etherbridge_set_max(&sc->sc_eb, bparam);
		break;
	case SIOCBRDGGCACHE:
		error = etherbridge_get_max(&sc->sc_eb, bparam);
		break;

	case SIOCBRDGSTO:
		error = suser(curproc);
		if (error != 0)
			break;

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
		error = suser(curproc);
		if (error != 0)
			break;

		etherbridge_flush(&sc->sc_eb,
		    ((struct ifbreq *)data)->ifbr_ifsflags);
		break;
	case SIOCBRDGSADDR:
		error = bpe_add_addr(sc, (struct ifbareq *)data);
		break;
	case SIOCBRDGDADDR:
		error = bpe_del_addr(sc, (struct ifbareq *)data);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	return (error);
}

static int
bpe_media_get(struct bpe_softc *sc, struct ifreq *ifr)
{
	struct ifnet *ifp0;
	int error;

	ifp0 = if_get(sc->sc_key.k_if);
	if (ifp0 != NULL)
		error = (*ifp0->if_ioctl)(ifp0, SIOCGIFMEDIA, (caddr_t)ifr);
	else
		error = ENOTTY;
	if_put(ifp0);

	return (error);
}

static int
bpe_up(struct bpe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ifnet *ifp0;
	struct bpe_softc *osc;
	int error;
	u_int hardmtu;
	u_int hlen = sizeof(struct ether_header) + sizeof(uint32_t);

	KASSERT(!ISSET(ifp->if_flags, IFF_RUNNING));
	NET_ASSERT_LOCKED();

	error = etherbridge_up(&sc->sc_eb);
	if (error != 0)
		return (error);

	ifp0 = if_get(sc->sc_key.k_if);
	if (ifp0 == NULL) {
		error = ENXIO;
		goto down;
	}

	/* check again if bpe will work on top of the parent */
	if (ifp0->if_type != IFT_ETHER) {
		error = EPROTONOSUPPORT;
		goto put;
	}

	hardmtu = ifp0->if_hardmtu;
	if (hardmtu < hlen) {
		error = ENOBUFS;
		goto put;
	}
	hardmtu -= hlen;
	if (ifp->if_mtu > hardmtu) {
		error = ENOBUFS;
		goto put;
	}

	/* parent is fine, let's prepare the bpe to handle packets */
	ifp->if_hardmtu = hardmtu;
	SET(ifp->if_flags, ifp0->if_flags & IFF_SIMPLEX);

	/* commit the interface */
	error = rw_enter(&bpe_lock, RW_WRITE | RW_INTR);
	if (error != 0)
		goto scrub;

	osc = (struct bpe_softc *)RBT_INSERT(bpe_tree, &bpe_interfaces,
	    (struct bpe_key *)sc);
	rw_exit(&bpe_lock);

	if (osc != NULL) {
		error = EADDRINUSE;
		goto scrub;
	}

	if (bpe_multi(sc, ifp0, SIOCADDMULTI) != 0) {
		error = ENOTCONN;
		goto remove;
	}

	/* Register callback for physical link state changes */
	if_linkstatehook_add(ifp0, &sc->sc_ltask);

	/* Register callback if parent wants to unregister */
	if_detachhook_add(ifp0, &sc->sc_dtask);

	/* we're running now */
	SET(ifp->if_flags, IFF_RUNNING);
	bpe_link_state(sc, ifp0->if_link_state, ifp0->if_baudrate);

	if_put(ifp0);

	return (0);

remove:
	rw_enter(&bpe_lock, RW_WRITE);
	RBT_REMOVE(bpe_tree, &bpe_interfaces, (struct bpe_key *)sc);
	rw_exit(&bpe_lock);
scrub:
	CLR(ifp->if_flags, IFF_SIMPLEX);
	ifp->if_hardmtu = 0xffff;
put:
	if_put(ifp0);
down:
	etherbridge_down(&sc->sc_eb);

	return (error);
}

static int
bpe_down(struct bpe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ifnet *ifp0;

	NET_ASSERT_LOCKED();

	CLR(ifp->if_flags, IFF_RUNNING);

	ifp0 = if_get(sc->sc_key.k_if);
	if (ifp0 != NULL) {
		if_detachhook_del(ifp0, &sc->sc_dtask);
		if_linkstatehook_del(ifp0, &sc->sc_ltask);
		bpe_multi(sc, ifp0, SIOCDELMULTI);
	}
	if_put(ifp0);

	rw_enter(&bpe_lock, RW_WRITE);
	RBT_REMOVE(bpe_tree, &bpe_interfaces, (struct bpe_key *)sc);
	rw_exit(&bpe_lock);

	CLR(ifp->if_flags, IFF_SIMPLEX);
	ifp->if_hardmtu = 0xffff;

	etherbridge_down(&sc->sc_eb);

	return (0);
}

static int
bpe_multi(struct bpe_softc *sc, struct ifnet *ifp0, u_long cmd)
{
	struct ifreq ifr;
	struct sockaddr *sa;

	/* make it convincing */
	CTASSERT(sizeof(ifr.ifr_name) == sizeof(ifp0->if_xname));
	memcpy(ifr.ifr_name, ifp0->if_xname, sizeof(ifr.ifr_name));

	sa = &ifr.ifr_addr;
	CTASSERT(sizeof(sa->sa_data) >= sizeof(sc->sc_group));

	sa->sa_family = AF_UNSPEC;
	memcpy(sa->sa_data, &sc->sc_group, sizeof(sc->sc_group));

	return ((*ifp0->if_ioctl)(ifp0, cmd, (caddr_t)&ifr));
}

static void
bpe_set_group(struct bpe_softc *sc, uint32_t isid)
{
	uint8_t *group = sc->sc_group.ether_addr_octet;

	group[0] = 0x01;
	group[1] = 0x1e;
	group[2] = 0x83;
	group[3] = isid >> 16;
	group[4] = isid >> 8;
	group[5] = isid >> 0;
}

static int
bpe_set_vnetid(struct bpe_softc *sc, const struct ifreq *ifr)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t isid;

	if (ifr->ifr_vnetid < PBB_ITAG_ISID_MIN ||
	    ifr->ifr_vnetid > PBB_ITAG_ISID_MAX)
		return (EINVAL);

	isid = ifr->ifr_vnetid;
	if (isid == sc->sc_key.k_isid)
		return (0);

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	sc->sc_key.k_isid = isid;
	bpe_set_group(sc, isid);
	etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);

	return (0);
}

static int
bpe_set_parent(struct bpe_softc *sc, const struct if_parent *p)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ifnet *ifp0;
	int error = 0;

	ifp0 = if_unit(p->ifp_parent);
	if (ifp0 == NULL)
		return (ENXIO);

	if (ifp0->if_type != IFT_ETHER) {
		error = ENXIO;
		goto put;
	}

	if (ifp0->if_index == sc->sc_key.k_if)
		goto put;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		error = EBUSY;
		goto put;
	}

	ifsetlro(ifp0, 0);

	/* commit */
	sc->sc_key.k_if = ifp0->if_index;
	etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);

put:
	if_put(ifp0);
	return (error);
}

static int
bpe_get_parent(struct bpe_softc *sc, struct if_parent *p)
{
	struct ifnet *ifp0;
	int error = 0;

	ifp0 = if_get(sc->sc_key.k_if);
	if (ifp0 == NULL)
		error = EADDRNOTAVAIL;
	else
		memcpy(p->ifp_parent, ifp0->if_xname, sizeof(p->ifp_parent));
	if_put(ifp0);

	return (error);
}

static int
bpe_del_parent(struct bpe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	sc->sc_key.k_if = 0;
	etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);

	return (0);
}

static int
bpe_add_addr(struct bpe_softc *sc, const struct ifbareq *ifba)
{
	const struct sockaddr_dl *sdl;
	const struct ether_addr *endpoint;
	unsigned int type;

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

	if (ifba->ifba_dstsa.ss_family != AF_LINK)
		return (EAFNOSUPPORT);
	sdl = (struct sockaddr_dl *)&ifba->ifba_dstsa;
	if (sdl->sdl_type != IFT_ETHER)
		return (EAFNOSUPPORT);
	if (sdl->sdl_alen != ETHER_ADDR_LEN)
		return (EINVAL);
	endpoint = (struct ether_addr *)LLADDR(sdl);
	/* check endpoint for multicast or broadcast? */

	return (etherbridge_add_addr(&sc->sc_eb, (void *)endpoint,
	    &ifba->ifba_dst, type));
}

static int
bpe_del_addr(struct bpe_softc *sc, const struct ifbareq *ifba)
{
	return (etherbridge_del_addr(&sc->sc_eb, &ifba->ifba_dst));
}

static inline struct bpe_softc *
bpe_find(struct ifnet *ifp0, uint32_t isid)
{
	struct bpe_key k = { .k_if = ifp0->if_index, .k_isid = isid };
	struct bpe_softc *sc;

	rw_enter_read(&bpe_lock);
	sc = (struct bpe_softc *)RBT_FIND(bpe_tree, &bpe_interfaces, &k);
	rw_exit_read(&bpe_lock);

	return (sc);
}

void
bpe_input(struct ifnet *ifp0, struct mbuf *m, struct netstack *ns)
{
	struct bpe_softc *sc;
	struct ifnet *ifp;
	struct ether_header *beh, *ceh;
	uint32_t *itagp, itag;
	unsigned int hlen = sizeof(*beh) + sizeof(*itagp) + sizeof(*ceh);
	struct mbuf *n;
	int off;
	int prio;

	if (m->m_len < hlen) {
		m = m_pullup(m, hlen);
		if (m == NULL) {
			/* pbb short ++ */
			return;
		}
	}

	beh = mtod(m, struct ether_header *);
	itagp = (uint32_t *)(beh + 1);
	itag = bemtoh32(itagp);

	if (itag & PBB_ITAG_RES2) {
		/* dropped by res2 ++ */
		goto drop;
	}

	sc = bpe_find(ifp0, itag & PBB_ITAG_ISID);
	if (sc == NULL) {
		/* no interface found */
		goto drop;
	}

	ceh = (struct ether_header *)(itagp + 1);

	etherbridge_map_ea(&sc->sc_eb, ceh->ether_shost,
	    (struct ether_addr *)beh->ether_shost);

	m_adj(m, sizeof(*beh) + sizeof(*itagp));

	n = m_getptr(m, sizeof(*ceh), &off);
	if (n == NULL) {
		/* no data ++ */
		goto drop;
	}

	if (!ALIGNED_POINTER(mtod(n, caddr_t) + off, uint32_t)) {
		/* unaligned ++ */
		n = m_dup_pkt(m, ETHER_ALIGN, M_NOWAIT);
		m_freem(m);
		if (n == NULL)
			return;

		m = n;
	}

	ifp = &sc->sc_ac.ac_if;

	prio = sc->sc_rxhprio;
	switch (prio) {
	case IF_HDRPRIO_PACKET:
		break;
	case IF_HDRPRIO_OUTER:
		m->m_pkthdr.pf.prio = (itag & PBB_ITAG_PCP_MASK) >>
		    PBB_ITAG_PCP_SHIFT;
		break;
	default:
		m->m_pkthdr.pf.prio = prio;
		break;
	}

	m->m_flags &= ~(M_BCAST|M_MCAST);
	m->m_pkthdr.ph_ifidx = ifp->if_index;
	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;

#if NPF > 0
	pf_pkt_addr_changed(m);
#endif

	if_vinput(ifp, m, ns);
	return;

drop:
	m_freem(m);
}

void
bpe_detach_hook(void *arg)
{
	struct bpe_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		bpe_down(sc);
		CLR(ifp->if_flags, IFF_UP);
	}

	sc->sc_key.k_if = 0;
}

static void
bpe_link_hook(void *arg)
{
	struct bpe_softc *sc = arg;
	struct ifnet *ifp0;
	u_char link = LINK_STATE_DOWN;
	uint64_t baud = 0;

	ifp0 = if_get(sc->sc_key.k_if);
	if (ifp0 != NULL) {
		link = ifp0->if_link_state;
		baud = ifp0->if_baudrate;
	}
	if_put(ifp0);

	bpe_link_state(sc, link, baud);
}

void
bpe_link_state(struct bpe_softc *sc, u_char link, uint64_t baud)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (ifp->if_link_state == link)
		return;

	ifp->if_link_state = link;
	ifp->if_baudrate = baud;

	if_link_state_change(ifp);
}

static inline int
bpe_cmp(const struct bpe_key *a, const struct bpe_key *b)
{
	if (a->k_if > b->k_if)
		return (1);
	if (a->k_if < b->k_if)
		return (-1);
	if (a->k_isid > b->k_isid)
		return (1);
	if (a->k_isid < b->k_isid)
		return (-1);

	return (0);
}

static int
bpe_eb_port_eq(void *arg, void *a, void *b)
{
	struct ether_addr *ea = a, *eb = b;

	return (memcmp(ea, eb, sizeof(*ea)) == 0);
}

static void *
bpe_eb_port_take(void *arg, void *port)
{
	struct ether_addr *ea = port;
	struct ether_addr *endpoint;

	endpoint = pool_get(&bpe_endpoint_pool, PR_NOWAIT);
	if (endpoint == NULL)
		return (NULL);

	memcpy(endpoint, ea, sizeof(*endpoint));

	return (endpoint);
}

static void
bpe_eb_port_rele(void *arg, void *port)
{
	struct ether_addr *endpoint = port;

	pool_put(&bpe_endpoint_pool, endpoint);
}

static size_t
bpe_eb_port_ifname(void *arg, char *dst, size_t len, void *port)
{
	struct bpe_softc *sc = arg;

	return (strlcpy(dst, sc->sc_ac.ac_if.if_xname, len));
}

static void
bpe_eb_port_sa(void *arg, struct sockaddr_storage *ss, void *port)
{
	struct ether_addr *endpoint = port;
	struct sockaddr_dl *sdl;

	sdl = (struct sockaddr_dl *)ss;
	sdl->sdl_len = sizeof(sdl);
	sdl->sdl_family = AF_LINK;
	sdl->sdl_index = 0;
	sdl->sdl_type = IFT_ETHER;
	sdl->sdl_nlen = 0;
	sdl->sdl_alen = sizeof(*endpoint);
	CTASSERT(sizeof(sdl->sdl_data) >= sizeof(*endpoint));
	memcpy(sdl->sdl_data, endpoint, sizeof(*endpoint));
}

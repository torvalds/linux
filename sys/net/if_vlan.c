/*	$OpenBSD: if_vlan.c,v 1.222 2025/07/07 02:28:50 jsg Exp $	*/

/*
 * Copyright 1998 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/net/if_vlan.c,v 1.16 2000/03/26 15:21:40 charnier Exp $
 */

/*
 * if_vlan.c - pseudo-device driver for IEEE 802.1Q virtual LANs.
 * This is sort of sneaky in the implementation, since
 * we need to pretend to be enough of an Ethernet implementation
 * to make arp work.  The way we do this is by telling everyone
 * that we are an Ethernet, and then catch the packets that
 * ether_output() left on our output queue when it calls
 * if_start(), rewrite them for use by the real outgoing interface,
 * and ask it to send them.
 *
 * Some devices support 802.1Q tag insertion in firmware.  The
 * vlan interface behavior changes when the IFCAP_VLAN_HWTAGGING
 * capability is set on the parent.  In this case, vlan_start()
 * will not modify the ethernet header.
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/rwlock.h>
#include <sys/percpu.h>
#include <sys/refcnt.h>
#include <sys/smr.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_vlan_var.h>

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

struct vlan_mc_entry {
	LIST_ENTRY(vlan_mc_entry)	mc_entries;
	union {
		struct ether_multi	*mcu_enm;
	} mc_u;
#define mc_enm	mc_u.mcu_enm
	struct sockaddr_storage		mc_addr;
};

struct vlan_softc {
	struct arpcom		 sc_ac;
#define	sc_if			 sc_ac.ac_if
	unsigned int		 sc_dead;
	unsigned int		 sc_ifidx0;	/* parent interface */
	int			 sc_txprio;
	int			 sc_rxprio;
	uint16_t		 sc_proto; /* encapsulation ethertype */
	uint16_t		 sc_tag;
	uint16_t		 sc_type; /* non-standard ethertype or 0x8100 */
	LIST_HEAD(__vlan_mchead, vlan_mc_entry)
				 sc_mc_listhead;
	SMR_SLIST_ENTRY(vlan_softc) sc_list;
	int			 sc_flags;
	struct refcnt		 sc_refcnt;
	struct task		 sc_ltask;
	struct task		 sc_dtask;
};

SMR_SLIST_HEAD(vlan_list, vlan_softc);

#define	IFVF_PROMISC	0x01	/* the parent should be made promisc */
#define	IFVF_LLADDR	0x02	/* don't inherit the parents mac */

#define TAG_HASH_BITS		5
#define TAG_HASH_SIZE		(1 << TAG_HASH_BITS)
#define TAG_HASH_MASK		(TAG_HASH_SIZE - 1)
#define TAG_HASH(tag)		(tag & TAG_HASH_MASK)
struct vlan_list *vlan_tagh, *svlan_tagh;
struct rwlock vlan_tagh_lk = RWLOCK_INITIALIZER("vlantag");

void	vlanattach(int count);
int	vlan_clone_create(struct if_clone *, int);
int	vlan_clone_destroy(struct ifnet *);

int	vlan_enqueue(struct ifnet *, struct mbuf *);
void	vlan_start(struct ifqueue *ifq);
int	vlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr);

int	vlan_up(struct vlan_softc *);
int	vlan_down(struct vlan_softc *);

void	vlan_ifdetach(void *);
void	vlan_link_hook(void *);
void	vlan_link_state(struct vlan_softc *, u_char, uint64_t);

int	vlan_set_vnetid(struct vlan_softc *, uint16_t);
int	vlan_set_parent(struct vlan_softc *, const char *);
int	vlan_del_parent(struct vlan_softc *);
int	vlan_inuse(uint16_t, unsigned int, uint16_t);
int	vlan_inuse_locked(uint16_t, unsigned int, uint16_t);

int	vlan_multi_add(struct vlan_softc *, struct ifreq *);
int	vlan_multi_del(struct vlan_softc *, struct ifreq *);
void	vlan_multi_apply(struct vlan_softc *, struct ifnet *, u_long);
void	vlan_multi_free(struct vlan_softc *);

int	vlan_media_get(struct vlan_softc *, struct ifreq *);

int	vlan_iff(struct vlan_softc *);
int	vlan_setlladdr(struct vlan_softc *, struct ifreq *);

int	vlan_set_compat(struct ifnet *, struct ifreq *);
int	vlan_get_compat(struct ifnet *, struct ifreq *);

struct if_clone vlan_cloner =
    IF_CLONE_INITIALIZER("vlan", vlan_clone_create, vlan_clone_destroy);
struct if_clone svlan_cloner =
    IF_CLONE_INITIALIZER("svlan", vlan_clone_create, vlan_clone_destroy);

void
vlanattach(int count)
{
	unsigned int i;

	/* Normal VLAN */
	vlan_tagh = mallocarray(TAG_HASH_SIZE, sizeof(*vlan_tagh),
	    M_DEVBUF, M_NOWAIT);
	if (vlan_tagh == NULL)
		panic("vlanattach: hashinit");

	/* Service-VLAN for QinQ/802.1ad provider bridges */
	svlan_tagh = mallocarray(TAG_HASH_SIZE, sizeof(*svlan_tagh),
	    M_DEVBUF, M_NOWAIT);
	if (svlan_tagh == NULL)
		panic("vlanattach: hashinit");

	for (i = 0; i < TAG_HASH_SIZE; i++) {
		SMR_SLIST_INIT(&vlan_tagh[i]);
		SMR_SLIST_INIT(&svlan_tagh[i]);
	}

	if_clone_attach(&vlan_cloner);
	if_clone_attach(&svlan_cloner);
}

int
vlan_clone_create(struct if_clone *ifc, int unit)
{
	struct vlan_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	sc->sc_dead = 0;
	LIST_INIT(&sc->sc_mc_listhead);
	task_set(&sc->sc_ltask, vlan_link_hook, sc);
	task_set(&sc->sc_dtask, vlan_ifdetach, sc);
	ifp = &sc->sc_if;
	ifp->if_softc = sc;
	snprintf(ifp->if_xname, sizeof ifp->if_xname, "%s%d", ifc->ifc_name,
	    unit);
	/* NB: flags are not set here */
	/* NB: mtu is not set here */

	/* Special handling for the IEEE 802.1ad QinQ variant */
	if (strcmp("svlan", ifc->ifc_name) == 0)
		sc->sc_type = ETHERTYPE_QINQ;
	else
		sc->sc_type = ETHERTYPE_VLAN;

	refcnt_init(&sc->sc_refcnt);
	sc->sc_txprio = IF_HDRPRIO_PACKET;
	sc->sc_rxprio = IF_HDRPRIO_OUTER;

	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED|IFXF_MPSAFE;
	ifp->if_qstart = vlan_start;
	ifp->if_enqueue = vlan_enqueue;
	ifp->if_ioctl = vlan_ioctl;
	ifp->if_hardmtu = 0xffff;
	ifp->if_link_state = LINK_STATE_DOWN;

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);
	ifp->if_hdrlen = EVL_ENCAPLEN;

	return (0);
}

int
vlan_clone_destroy(struct ifnet *ifp)
{
	struct vlan_softc *sc = ifp->if_softc;

	NET_LOCK();
	sc->sc_dead = 1;

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		vlan_down(sc);
	NET_UNLOCK();

	ether_ifdetach(ifp);
	if_detach(ifp);
	smr_barrier();
	refcnt_finalize(&sc->sc_refcnt, "vlanrefs");
	vlan_multi_free(sc);
	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

void
vlan_transmit(struct vlan_softc *sc, struct ifnet *ifp0, struct mbuf *m)
{
	struct ifnet *ifp = &sc->sc_if;
	int txprio = sc->sc_txprio;
	uint8_t prio;

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif /* NBPFILTER > 0 */

	prio = (txprio == IF_HDRPRIO_PACKET) ?
	    m->m_pkthdr.pf.prio : txprio;

	/* IEEE 802.1p has prio 0 and 1 swapped */
	if (prio <= 1)
		prio = !prio;

	/*
	 * If the underlying interface cannot do VLAN tag insertion
	 * itself, create an encapsulation header.
	 */
	if ((ifp0->if_capabilities & IFCAP_VLAN_HWTAGGING) &&
	    (sc->sc_type == ETHERTYPE_VLAN)) {
		m->m_pkthdr.ether_vtag = sc->sc_tag |
		    (prio << EVL_PRIO_BITS);
		m->m_flags |= M_VLANTAG;
	} else {
		m = vlan_inject(m, sc->sc_type, sc->sc_tag |
		    (prio << EVL_PRIO_BITS));
		if (m == NULL) {
			counters_inc(ifp->if_counters, ifc_oerrors);
			return;
		}
	}

	if (if_enqueue(ifp0, m))
		counters_inc(ifp->if_counters, ifc_oerrors);
}

int
vlan_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	struct ifnet *ifp0;
	struct vlan_softc *sc;
	int error = 0;

	if (!ifq_is_priq(&ifp->if_snd))
		return (if_enqueue_ifq(ifp, m));

	sc = ifp->if_softc;
	ifp0 = if_get(sc->sc_ifidx0);

	if (ifp0 == NULL || !ISSET(ifp0->if_flags, IFF_RUNNING)) {
		m_freem(m);
		error = ENETDOWN;
	} else {
		counters_pkt(ifp->if_counters,
		    ifc_opackets, ifc_obytes, m->m_pkthdr.len);
		vlan_transmit(sc, ifp0, m);
	}

	if_put(ifp0);

	return (error);
}

void
vlan_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct vlan_softc *sc = ifp->if_softc;
	struct ifnet *ifp0;
	struct mbuf *m;

	ifp0 = if_get(sc->sc_ifidx0);
	if (ifp0 == NULL || !ISSET(ifp0->if_flags, IFF_RUNNING)) {
		ifq_purge(ifq);
		goto leave;
	}

	while ((m = ifq_dequeue(ifq)) != NULL)
		vlan_transmit(sc, ifp0, m);

leave:
	if_put(ifp0);
}

struct mbuf *
vlan_strip(struct mbuf *m)
{
	if (ISSET(m->m_flags, M_VLANTAG)) {
		CLR(m->m_flags, M_VLANTAG);
	} else {
		struct ether_vlan_header *evl;

		evl = mtod(m, struct ether_vlan_header *);
		memmove((caddr_t)evl + EVL_ENCAPLEN, evl,
		    offsetof(struct ether_vlan_header, evl_encap_proto));
		m_adj(m, EVL_ENCAPLEN);
	}

	return (m);
}

struct mbuf *
vlan_inject(struct mbuf *m, uint16_t type, uint16_t tag)
{
	struct ether_vlan_header evh;

	m_copydata(m, 0, ETHER_HDR_LEN, &evh);
	evh.evl_proto = evh.evl_encap_proto;
	evh.evl_encap_proto = htons(type);
	evh.evl_tag = htons(tag);
	m_adj(m, ETHER_HDR_LEN);
	M_PREPEND(m, sizeof(evh) + ETHER_ALIGN, M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	m_adj(m, ETHER_ALIGN);

	m_copyback(m, 0, sizeof(evh), &evh, M_NOWAIT);
	CLR(m->m_flags, M_VLANTAG);

	return (m);
}

struct mbuf *
vlan_input(struct ifnet *ifp0, struct mbuf *m, unsigned int *sdelim,
    struct netstack *ns)
{
	struct vlan_softc *sc;
	struct ifnet *ifp;
	struct ether_vlan_header *evl;
	struct vlan_list *tagh, *list;
	uint16_t vtag, tag;
	uint16_t etype;
	int rxprio;

	if (m->m_flags & M_VLANTAG) {
		vtag = m->m_pkthdr.ether_vtag;
		etype = ETHERTYPE_VLAN;
		tagh = vlan_tagh;
	} else {
		if (m->m_len < sizeof(*evl)) {
			m = m_pullup(m, sizeof(*evl));
			if (m == NULL)
				return (NULL);
		}

		evl = mtod(m, struct ether_vlan_header *);
		vtag = bemtoh16(&evl->evl_tag);
		etype = bemtoh16(&evl->evl_encap_proto);
		switch (etype) {
		case ETHERTYPE_VLAN:
			tagh = vlan_tagh;
			break;
		case ETHERTYPE_QINQ:
			tagh = svlan_tagh;
			break;
		default:
			panic("%s: unexpected etype 0x%04x", __func__, etype);
			/* NOTREACHED */
		}
	}

	tag = EVL_VLANOFTAG(vtag);
	list = &tagh[TAG_HASH(tag)];
	smr_read_enter();
	SMR_SLIST_FOREACH(sc, list, sc_list) {
		if (ifp0->if_index == sc->sc_ifidx0 && tag == sc->sc_tag &&
		    etype == sc->sc_type) {
			refcnt_take(&sc->sc_refcnt);
			break;
		}
	}
	smr_read_leave();

	if (sc == NULL) {
		/* VLAN 0 Priority Tagging */
		if (tag == 0 && etype == ETHERTYPE_VLAN) {
			struct ether_header *eh;

			/* XXX we should actually use the prio value? */
			m = vlan_strip(m);

			eh = mtod(m, struct ether_header *);
			if (eh->ether_type == htons(ETHERTYPE_VLAN) ||
			    eh->ether_type == htons(ETHERTYPE_QINQ)) {
				m_freem(m);
				return (NULL);
			}
		} else
			*sdelim = 1;

		return (m); /* decline */
	}

	ifp = &sc->sc_if;
	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		m_freem(m);
		goto leave;
	}

	/*
	 * Having found a valid vlan interface corresponding to
	 * the given source interface and vlan tag, remove the
	 * encapsulation.
	 */
	m = vlan_strip(m);

	rxprio = sc->sc_rxprio;
	switch (rxprio) {
	case IF_HDRPRIO_PACKET:
		break;
	case IF_HDRPRIO_OUTER:
		m->m_pkthdr.pf.prio = EVL_PRIOFTAG(m->m_pkthdr.ether_vtag);
		/* IEEE 802.1p has prio 0 and 1 swapped */
		if (m->m_pkthdr.pf.prio <= 1)
			m->m_pkthdr.pf.prio = !m->m_pkthdr.pf.prio;
		break;
	default:
		m->m_pkthdr.pf.prio = rxprio;
		break;
	}

	if_vinput(ifp, m, ns);
leave:
	refcnt_rele_wake(&sc->sc_refcnt);
	return (NULL);
}

int
vlan_up(struct vlan_softc *sc)
{
	struct vlan_list *tagh, *list;
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	int error = 0;
	unsigned int hardmtu;

	KASSERT(!ISSET(ifp->if_flags, IFF_RUNNING));

	tagh = sc->sc_type == ETHERTYPE_QINQ ? svlan_tagh : vlan_tagh;
	list = &tagh[TAG_HASH(sc->sc_tag)];

	ifp0 = if_get(sc->sc_ifidx0);
	if (ifp0 == NULL)
		return (ENXIO);

	/* check vlan will work on top of the parent */
	if (ifp0->if_type != IFT_ETHER) {
		error = EPROTONOSUPPORT;
		goto put;
	}

	hardmtu = ifp0->if_hardmtu;
	if (!ISSET(ifp0->if_capabilities, IFCAP_VLAN_MTU))
		hardmtu -= EVL_ENCAPLEN;

	if (ifp->if_mtu > hardmtu) {
		error = ENOBUFS;
		goto put;
	}

	/* parent is fine, let's prepare the sc to handle packets */
	ifp->if_hardmtu = hardmtu;
	SET(ifp->if_flags, ifp0->if_flags & IFF_SIMPLEX);

	if (ISSET(sc->sc_flags, IFVF_PROMISC)) {
		error = ifpromisc(ifp0, 1);
		if (error != 0)
			goto scrub;
	}

	/*
	 * Note: In cases like vio(4) and em(4) where the offsets of the
	 * csum can be freely defined, we could actually do csum offload
	 * for QINQ packets.
	 */
	if (sc->sc_type != ETHERTYPE_VLAN) {
		/*
		 * Hardware offload only works with the default VLAN
		 * ethernet type (0x8100).
		 */
		ifp->if_capabilities = 0;
	} else if (ISSET(ifp0->if_capabilities, IFCAP_VLAN_HWTAGGING) ||
	    ISSET(ifp0->if_capabilities, IFCAP_VLAN_HWOFFLOAD)) {
		/*
		 * Chips that can do hardware-assisted VLAN encapsulation, can
		 * calculate the correct checksum for VLAN tagged packets.
		 *
		 * Hardware which does checksum offloading, but not VLAN tag
		 * injection, have to set IFCAP_VLAN_HWOFFLOAD.
		 */
		ifp->if_capabilities = ifp0->if_capabilities &
		    (IFCAP_CSUM_MASK | IFCAP_TSOv4 | IFCAP_TSOv6);
	}

	/* commit the sc */
	error = rw_enter(&vlan_tagh_lk, RW_WRITE | RW_INTR);
	if (error != 0)
		goto unpromisc;

	error = vlan_inuse_locked(sc->sc_type, sc->sc_ifidx0, sc->sc_tag);
	if (error != 0)
		goto leave;

	SMR_SLIST_INSERT_HEAD_LOCKED(list, sc, sc_list);
	rw_exit(&vlan_tagh_lk);

	/* Register callback for physical link state changes */
	if_linkstatehook_add(ifp0, &sc->sc_ltask);

	/* Register callback if parent wants to unregister */
	if_detachhook_add(ifp0, &sc->sc_dtask);

	/* configure the parent to handle packets for this vlan */
	vlan_multi_apply(sc, ifp0, SIOCADDMULTI);

	/* we're running now */
	SET(ifp->if_flags, IFF_RUNNING);
	vlan_link_state(sc, ifp0->if_link_state, ifp0->if_baudrate);

	if_put(ifp0);

	return (ENETRESET);

leave:
	rw_exit(&vlan_tagh_lk);
unpromisc:
	if (ISSET(sc->sc_flags, IFVF_PROMISC))
		(void)ifpromisc(ifp0, 0); /* XXX */
scrub:
	ifp->if_capabilities = 0;
	CLR(ifp->if_flags, IFF_SIMPLEX);
	ifp->if_hardmtu = 0xffff;
put:
	if_put(ifp0);

	return (error);
}

int
vlan_down(struct vlan_softc *sc)
{
	struct vlan_list *tagh, *list;
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;

	tagh = sc->sc_type == ETHERTYPE_QINQ ? svlan_tagh : vlan_tagh;
	list = &tagh[TAG_HASH(sc->sc_tag)];

	KASSERT(ISSET(ifp->if_flags, IFF_RUNNING));

	vlan_link_state(sc, LINK_STATE_DOWN, 0);
	CLR(ifp->if_flags, IFF_RUNNING);

	ifq_barrier(&ifp->if_snd);

	ifp0 = if_get(sc->sc_ifidx0);
	if (ifp0 != NULL) {
		if (ISSET(sc->sc_flags, IFVF_PROMISC))
			ifpromisc(ifp0, 0);
		vlan_multi_apply(sc, ifp0, SIOCDELMULTI);
		if_detachhook_del(ifp0, &sc->sc_dtask);
		if_linkstatehook_del(ifp0, &sc->sc_ltask);
	}
	if_put(ifp0);

	rw_enter_write(&vlan_tagh_lk);
	SMR_SLIST_REMOVE_LOCKED(list, sc, vlan_softc, sc_list);
	rw_exit_write(&vlan_tagh_lk);

	ifp->if_capabilities = 0;
	CLR(ifp->if_flags, IFF_SIMPLEX);
	ifp->if_hardmtu = 0xffff;

	return (0);
}

void
vlan_ifdetach(void *v)
{
	struct vlan_softc *sc = v;
	struct ifnet *ifp = &sc->sc_if;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		vlan_down(sc);
		CLR(ifp->if_flags, IFF_UP);
	}

	sc->sc_ifidx0 = 0;
}

void
vlan_link_hook(void *v)
{
	struct vlan_softc *sc = v;
	struct ifnet *ifp0;

	u_char link = LINK_STATE_DOWN;
	uint64_t baud = 0;

	ifp0 = if_get(sc->sc_ifidx0);
	if (ifp0 != NULL) {
		link = ifp0->if_link_state;
		baud = ifp0->if_baudrate;
	}
	if_put(ifp0);

	vlan_link_state(sc, link, baud);
}

void
vlan_link_state(struct vlan_softc *sc, u_char link, uint64_t baud)
{
	if (sc->sc_if.if_link_state == link)
		return;

	sc->sc_if.if_link_state = link;
	sc->sc_if.if_baudrate = baud;

	if_link_state_change(&sc->sc_if);
}

int
vlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct vlan_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct if_parent *parent = (struct if_parent *)data;
	struct ifnet *ifp0;
	uint16_t tag;
	int error = 0;

	if (sc->sc_dead)
		return (ENXIO);

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = vlan_up(sc);
			else
				error = ENETRESET;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = vlan_down(sc);
		}
		break;

	case SIOCSIFXFLAGS:
		if ((ifp0 = if_get(sc->sc_ifidx0)) != NULL) {
			ifsetlro(ifp0, ISSET(ifr->ifr_flags, IFXF_LRO));
			if_put(ifp0);
		}
		break;

	case SIOCSVNETID:
		if (ifr->ifr_vnetid < EVL_VLID_MIN ||
		    ifr->ifr_vnetid > EVL_VLID_MAX) {
			error = EINVAL;
			break;
		}

		tag = ifr->ifr_vnetid;
		if (tag == sc->sc_tag)
			break;

		error = vlan_set_vnetid(sc, tag);
		break;

	case SIOCGVNETID:
		if (sc->sc_tag == EVL_VLID_NULL)
			error = EADDRNOTAVAIL;
		else
			ifr->ifr_vnetid = (int64_t)sc->sc_tag;
		break;

	case SIOCDVNETID:
		error = vlan_set_vnetid(sc, 0);
		break;

	case SIOCSIFPARENT:
		error = vlan_set_parent(sc, parent->ifp_parent);
		break;

	case SIOCGIFPARENT:
		ifp0 = if_get(sc->sc_ifidx0);
		if (ifp0 == NULL)
			error = EADDRNOTAVAIL;
		else {
			memcpy(parent->ifp_parent, ifp0->if_xname,
			    sizeof(parent->ifp_parent));
		}
		if_put(ifp0);
		break;

	case SIOCDIFPARENT:
		error = vlan_del_parent(sc);
		break;

	case SIOCADDMULTI:
		error = vlan_multi_add(sc, ifr);
		break;

	case SIOCDELMULTI:
		error = vlan_multi_del(sc, ifr);
		break;

	case SIOCGIFMEDIA:
		error = vlan_media_get(sc, ifr);
		break;

	case SIOCSIFMEDIA:
		error = ENOTTY;
		break;

	case SIOCSIFLLADDR:
		error = vlan_setlladdr(sc, ifr);
		break;

	case SIOCSETVLAN:
		error = vlan_set_compat(ifp, ifr);
		break;
	case SIOCGETVLAN:
		error = vlan_get_compat(ifp, ifr);
		break;

	case SIOCSTXHPRIO:
		error = if_txhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_txprio = ifr->ifr_hdrprio;
		break;
	case SIOCGTXHPRIO:
		ifr->ifr_hdrprio = sc->sc_txprio;
		break;

	case SIOCSRXHPRIO:
		error = if_rxhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_rxprio = ifr->ifr_hdrprio;
		break;
	case SIOCGRXHPRIO:
		ifr->ifr_hdrprio = sc->sc_rxprio;
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET)
		error = vlan_iff(sc);

	return error;
}

int
vlan_iff(struct vlan_softc *sc)
{
	struct ifnet *ifp0;
	int promisc = 0;
	int error = 0;

	if (ISSET(sc->sc_if.if_flags, IFF_PROMISC) ||
	    ISSET(sc->sc_flags, IFVF_LLADDR))
		promisc = IFVF_PROMISC;

	if (ISSET(sc->sc_flags, IFVF_PROMISC) == promisc)
		return (0);

	if (ISSET(sc->sc_if.if_flags, IFF_RUNNING)) {
		ifp0 = if_get(sc->sc_ifidx0);
		if (ifp0 != NULL)
			error = ifpromisc(ifp0, promisc);
		if_put(ifp0);
	}

	if (error == 0) {
		CLR(sc->sc_flags, IFVF_PROMISC);
		SET(sc->sc_flags, promisc);
	}

	return (error);
}

int
vlan_setlladdr(struct vlan_softc *sc, struct ifreq *ifr)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	uint8_t lladdr[ETHER_ADDR_LEN];
	int flag;

	memcpy(lladdr, ifr->ifr_addr.sa_data, sizeof(lladdr));

	/* setting the mac addr to 00:00:00:00:00:00 means reset lladdr */
	if (memcmp(lladdr, etheranyaddr, sizeof(lladdr)) == 0) {
		ifp0 = if_get(sc->sc_ifidx0);
		if (ifp0 != NULL)
			memcpy(lladdr, LLADDR(ifp0->if_sadl), sizeof(lladdr));
		if_put(ifp0);

		flag = 0;
	} else
		flag = IFVF_LLADDR;

	if (memcmp(lladdr, LLADDR(ifp->if_sadl), sizeof(lladdr)) == 0 &&
	    ISSET(sc->sc_flags, IFVF_LLADDR) == flag) {
		/* nop */
		return (0);
	}

	/* commit */
	if_setlladdr(ifp, lladdr);
	CLR(sc->sc_flags, IFVF_LLADDR);
	SET(sc->sc_flags, flag);

	return (ENETRESET);
}

int
vlan_set_vnetid(struct vlan_softc *sc, uint16_t tag)
{
	struct ifnet *ifp = &sc->sc_if;
	struct vlan_list *tagh, *list;
	u_char link = ifp->if_link_state;
	uint64_t baud = ifp->if_baudrate;
	int error;

	tagh = sc->sc_type == ETHERTYPE_QINQ ? svlan_tagh : vlan_tagh;

	if (ISSET(ifp->if_flags, IFF_RUNNING) && LINK_STATE_IS_UP(link))
		vlan_link_state(sc, LINK_STATE_DOWN, 0);

	error = rw_enter(&vlan_tagh_lk, RW_WRITE);
	if (error != 0)
		return (error);

	error = vlan_inuse_locked(sc->sc_type, sc->sc_ifidx0, tag);
	if (error != 0)
		goto unlock;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		list = &tagh[TAG_HASH(sc->sc_tag)];
		SMR_SLIST_REMOVE_LOCKED(list, sc, vlan_softc, sc_list);

		sc->sc_tag = tag;

		list = &tagh[TAG_HASH(sc->sc_tag)];
		SMR_SLIST_INSERT_HEAD_LOCKED(list, sc, sc_list);
	} else
		sc->sc_tag = tag;

unlock:
	rw_exit(&vlan_tagh_lk);

	if (ISSET(ifp->if_flags, IFF_RUNNING) && LINK_STATE_IS_UP(link))
		vlan_link_state(sc, link, baud);

	return (error);
}

int
vlan_set_parent(struct vlan_softc *sc, const char *parent)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	int error = 0;

	ifp0 = if_unit(parent);
	if (ifp0 == NULL)
		return (EINVAL);

	if (ifp0->if_type != IFT_ETHER) {
		error = EPROTONOSUPPORT;
		goto put;
	}

	if (sc->sc_ifidx0 == ifp0->if_index) {
		/* nop */
		goto put;
	}

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		error = EBUSY;
		goto put;
	}

	error = vlan_inuse(sc->sc_type, ifp0->if_index, sc->sc_tag);
	if (error != 0)
		goto put;

	if (ether_brport_isset(ifp))
		ifsetlro(ifp0, 0);

	/* commit */
	sc->sc_ifidx0 = ifp0->if_index;
	if (!ISSET(sc->sc_flags, IFVF_LLADDR))
		if_setlladdr(ifp, LLADDR(ifp0->if_sadl));

put:
	if_put(ifp0);
	return (error);
}

int
vlan_del_parent(struct vlan_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	sc->sc_ifidx0 = 0;
	if (!ISSET(sc->sc_flags, IFVF_LLADDR))
		if_setlladdr(ifp, etheranyaddr);

	return (0);
}

int
vlan_set_compat(struct ifnet *ifp, struct ifreq *ifr)
{
	struct vlanreq vlr;
	struct ifreq req;
	struct if_parent parent;

	int error;

	error = suser(curproc);
	if (error != 0)
		return (error);

	error = copyin(ifr->ifr_data, &vlr, sizeof(vlr));
	if (error != 0)
		return (error);

	if (vlr.vlr_parent[0] == '\0')
		return (vlan_ioctl(ifp, SIOCDIFPARENT, (caddr_t)ifr));

	memset(&req, 0, sizeof(req));
	memcpy(req.ifr_name, ifp->if_xname, sizeof(req.ifr_name));
	req.ifr_vnetid = vlr.vlr_tag;

	error = vlan_ioctl(ifp, SIOCSVNETID, (caddr_t)&req);
	if (error != 0)
		return (error);

	memset(&parent, 0, sizeof(parent));
	memcpy(parent.ifp_name, ifp->if_xname, sizeof(parent.ifp_name));
	memcpy(parent.ifp_parent, vlr.vlr_parent, sizeof(parent.ifp_parent));
	error = vlan_ioctl(ifp, SIOCSIFPARENT, (caddr_t)&parent);
	if (error != 0)
		return (error);

	memset(&req, 0, sizeof(req));
	memcpy(req.ifr_name, ifp->if_xname, sizeof(req.ifr_name));
	SET(ifp->if_flags, IFF_UP);
	return (vlan_ioctl(ifp, SIOCSIFFLAGS, (caddr_t)&req));
}

int
vlan_get_compat(struct ifnet *ifp, struct ifreq *ifr)
{
	struct vlan_softc *sc = ifp->if_softc;
	struct vlanreq vlr;
	struct ifnet *p;

	memset(&vlr, 0, sizeof(vlr));
	p = if_get(sc->sc_ifidx0);
	if (p != NULL)
		memcpy(vlr.vlr_parent, p->if_xname, sizeof(vlr.vlr_parent));
	if_put(p);

	vlr.vlr_tag = sc->sc_tag;

	return (copyout(&vlr, ifr->ifr_data, sizeof(vlr)));
}

/*
 * do a quick check of up and running vlans for existing configurations.
 *
 * NOTE: this does allow the same config on down vlans, but vlan_up()
 * will catch them.
 */
int
vlan_inuse(uint16_t type, unsigned int ifidx, uint16_t tag)
{
	int error = 0;

	error = rw_enter(&vlan_tagh_lk, RW_READ | RW_INTR);
	if (error != 0)
		return (error);

	error = vlan_inuse_locked(type, ifidx, tag);

	rw_exit(&vlan_tagh_lk);

	return (error);
}

int
vlan_inuse_locked(uint16_t type, unsigned int ifidx, uint16_t tag)
{
	struct vlan_list *tagh, *list;
	struct vlan_softc *sc;

	tagh = type == ETHERTYPE_QINQ ? svlan_tagh : vlan_tagh;
	list = &tagh[TAG_HASH(tag)];

	SMR_SLIST_FOREACH_LOCKED(sc, list, sc_list) {
		if (sc->sc_tag == tag &&
		    sc->sc_type == type && /* wat */
		    sc->sc_ifidx0 == ifidx)
			return (EADDRINUSE);
	}

	return (0);
}

int
vlan_multi_add(struct vlan_softc *sc, struct ifreq *ifr)
{
	struct ifnet *ifp0;
	struct vlan_mc_entry *mc;
	uint8_t addrlo[ETHER_ADDR_LEN], addrhi[ETHER_ADDR_LEN];
	int error;

	error = ether_addmulti(ifr, &sc->sc_ac);
	if (error != ENETRESET)
		return (error);

	/*
	 * This is new multicast address.  We have to tell parent
	 * about it.  Also, remember this multicast address so that
	 * we can delete them on unconfigure.
	 */
	if ((mc = malloc(sizeof(*mc), M_DEVBUF, M_NOWAIT)) == NULL) {
		error = ENOMEM;
		goto alloc_failed;
	}

	/*
	 * As ether_addmulti() returns ENETRESET, following two
	 * statement shouldn't fail.
	 */
	(void)ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
	ETHER_LOOKUP_MULTI(addrlo, addrhi, &sc->sc_ac, mc->mc_enm);
	memcpy(&mc->mc_addr, &ifr->ifr_addr, ifr->ifr_addr.sa_len);
	LIST_INSERT_HEAD(&sc->sc_mc_listhead, mc, mc_entries);

	ifp0 = if_get(sc->sc_ifidx0);
	error = (ifp0 == NULL) ? 0 :
	    (*ifp0->if_ioctl)(ifp0, SIOCADDMULTI, (caddr_t)ifr);
	if_put(ifp0);

	if (error != 0)
		goto ioctl_failed;

	return (error);

 ioctl_failed:
	LIST_REMOVE(mc, mc_entries);
	free(mc, M_DEVBUF, sizeof(*mc));
 alloc_failed:
	(void)ether_delmulti(ifr, &sc->sc_ac);

	return (error);
}

int
vlan_multi_del(struct vlan_softc *sc, struct ifreq *ifr)
{
	struct ifnet *ifp0;
	struct ether_multi *enm;
	struct vlan_mc_entry *mc;
	uint8_t addrlo[ETHER_ADDR_LEN], addrhi[ETHER_ADDR_LEN];
	int error;

	/*
	 * Find a key to lookup vlan_mc_entry.  We have to do this
	 * before calling ether_delmulti for obvious reason.
	 */
	if ((error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi)) != 0)
		return (error);
	ETHER_LOOKUP_MULTI(addrlo, addrhi, &sc->sc_ac, enm);
	if (enm == NULL)
		return (EINVAL);

	LIST_FOREACH(mc, &sc->sc_mc_listhead, mc_entries) {
		if (mc->mc_enm == enm)
			break;
	}

	/* We won't delete entries we didn't add */
	if (mc == NULL)
		return (EINVAL);

	error = ether_delmulti(ifr, &sc->sc_ac);
	if (error != ENETRESET)
		return (error);

	if (!ISSET(sc->sc_if.if_flags, IFF_RUNNING))
		goto forget;

	ifp0 = if_get(sc->sc_ifidx0);
	error = (ifp0 == NULL) ? 0 :
	    (*ifp0->if_ioctl)(ifp0, SIOCDELMULTI, (caddr_t)ifr);
	if_put(ifp0);

	if (error != 0) {
		(void)ether_addmulti(ifr, &sc->sc_ac);
		return (error);
	}

forget:
	/* forget about this address */
	LIST_REMOVE(mc, mc_entries);
	free(mc, M_DEVBUF, sizeof(*mc));

	return (0);
}

int
vlan_media_get(struct vlan_softc *sc, struct ifreq *ifr)
{
	struct ifnet *ifp0;
	int error;

	ifp0 = if_get(sc->sc_ifidx0);
	error = (ifp0 == NULL) ? ENOTTY :
	    (*ifp0->if_ioctl)(ifp0, SIOCGIFMEDIA, (caddr_t)ifr);
	if_put(ifp0);

	return (error);
}

void
vlan_multi_apply(struct vlan_softc *sc, struct ifnet *ifp0, u_long cmd)
{
	struct vlan_mc_entry *mc;
	union {
		struct ifreq ifreq;
		struct {
			char			ifr_name[IFNAMSIZ];
			struct sockaddr_storage	ifr_ss;
		} ifreq_storage;
	} ifreq;
	struct ifreq *ifr = &ifreq.ifreq;

	memcpy(ifr->ifr_name, ifp0->if_xname, IFNAMSIZ);
	LIST_FOREACH(mc, &sc->sc_mc_listhead, mc_entries) {
		memcpy(&ifr->ifr_addr, &mc->mc_addr, mc->mc_addr.ss_len);

		(void)(*ifp0->if_ioctl)(ifp0, cmd, (caddr_t)ifr);
	}
}

void
vlan_multi_free(struct vlan_softc *sc)
{
	struct vlan_mc_entry *mc;

	while ((mc = LIST_FIRST(&sc->sc_mc_listhead)) != NULL) {
		LIST_REMOVE(mc, mc_entries);
		free(mc, M_DEVBUF, sizeof(*mc));
	}
}

/*	$OpenBSD: if_sec.c,v 1.14 2025/03/04 15:11:30 bluhm Exp $ */

/*
 * Copyright (c) 2022 The University of Queensland
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

/*
 * This code was written by David Gwynne <dlg@uq.edu.au> as part
 * of the Information Technology Infrastructure Group (ITIG) in the
 * Faculty of Engineering, Architecture and Information Technology
 * (EAIT).
 */

#ifndef IPSEC
#error sec enabled without IPSEC defined
#endif

#include "bpfilter.h"
#include "pf.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/smr.h>
#include <sys/refcnt.h>
#include <sys/task.h>
#include <sys/mutex.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_sec.h>
#include <net/if_types.h>
#include <net/toeplitz.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_ipsp.h>

#ifdef INET6
#include <netinet/ip6.h>
#endif

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NPF > 0
#include <net/pfvar.h>
#endif

#define SEC_MTU		1280
#define SEC_MTU_MIN	1280
#define SEC_MTU_MAX	32768	/* could get closer to 64k... */

struct sec_softc {
	struct ifnet			sc_if;
	unsigned int			sc_dead;
	unsigned int			sc_up;

	struct task			sc_send;
	int				sc_txprio;
	int				sc_tunneldf;

	unsigned int			sc_unit;
	SMR_SLIST_ENTRY(sec_softc)	sc_entry;
	struct refcnt			sc_refs;
};

SMR_SLIST_HEAD(sec_bucket, sec_softc);

static int	sec_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
static int	sec_enqueue(struct ifnet *, struct mbuf *);
static void	sec_send(void *);
static void	sec_start(struct ifqueue *);

static int	sec_ioctl(struct ifnet *, u_long, caddr_t);
static int	sec_up(struct sec_softc *);
static int	sec_down(struct sec_softc *);

static int	sec_clone_create(struct if_clone *, int);
static int	sec_clone_destroy(struct ifnet *);

static struct tdb *
		sec_tdb_get(unsigned int);
static void	sec_tdb_gc(void *);

static struct if_clone sec_cloner =
    IF_CLONE_INITIALIZER("sec", sec_clone_create, sec_clone_destroy);

static unsigned int		 sec_mix;
static struct sec_bucket	 sec_map[256] __aligned(CACHELINESIZE);
static struct tdb		*sec_tdbh[256] __aligned(CACHELINESIZE);

static struct tdb		*sec_tdb_gc_list;
static struct task		 sec_tdb_gc_task =
				     TASK_INITIALIZER(sec_tdb_gc, NULL);
static struct mutex		 sec_tdb_gc_mtx =
				     MUTEX_INITIALIZER(IPL_MPFLOOR);

void
secattach(int n)
{
	sec_mix = arc4random();
	if_clone_attach(&sec_cloner);
}

static int
sec_clone_create(struct if_clone *ifc, int unit)
{
	struct sec_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);

	sc->sc_unit = unit;
	sc->sc_tunneldf = IPSP_DF_OFF;

	task_set(&sc->sc_send, sec_send, sc);

	snprintf(sc->sc_if.if_xname, sizeof sc->sc_if.if_xname, "%s%d",
	    ifc->ifc_name, unit);

	ifp = &sc->sc_if;
	ifp->if_softc = sc;
	ifp->if_type = IFT_TUNNEL;
	ifp->if_mtu = SEC_MTU;
	ifp->if_flags = IFF_POINTOPOINT|IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;
	ifp->if_bpf_mtap = p2p_bpf_mtap;
	ifp->if_input = p2p_input;
	ifp->if_output = sec_output;
	ifp->if_enqueue = sec_enqueue;
	ifp->if_qstart = sec_start;
	ifp->if_ioctl = sec_ioctl;
	ifp->if_rtrequest = p2p_rtrequest;

	if_counters_alloc(ifp);
	if_attach(ifp);
	if_alloc_sadl(ifp);

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(uint32_t));
#endif

	return (0);
}

static int
sec_clone_destroy(struct ifnet *ifp)
{
	struct sec_softc *sc = ifp->if_softc;

	NET_LOCK();
	sc->sc_dead = 1;
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		sec_down(sc);
	NET_UNLOCK();

	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
sec_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct sec_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		break;

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = sec_up(sc);
			else
				error = 0;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = sec_down(sc);
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCSIFMTU:
		if (ifr->ifr_mtu < SEC_MTU_MIN ||
		    ifr->ifr_mtu > SEC_MTU_MAX) {
			error = EINVAL;
			break;
		}

		ifp->if_mtu = ifr->ifr_mtu;
		break;

	case SIOCSLIFPHYDF:
		sc->sc_tunneldf = (ifr->ifr_df ? IPSP_DF_ON : IPSP_DF_OFF);
		break;
	case SIOCGLIFPHYDF:
		ifr->ifr_df = (sc->sc_tunneldf == IPSP_DF_ON);
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

static int
sec_up(struct sec_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	unsigned int idx = stoeplitz_h32(sc->sc_unit) % nitems(sec_map);

	NET_ASSERT_LOCKED();
	KASSERT(!ISSET(ifp->if_flags, IFF_RUNNING));

	if (sc->sc_dead)
		return (ENXIO);

	/*
	 * coordinate with sec_down(). if sc_up is still up and
	 * we're here then something else is running sec_down.
	 */
	if (sc->sc_up)
		return (EBUSY);

	sc->sc_up = 1;

	refcnt_init(&sc->sc_refs);
	SET(ifp->if_flags, IFF_RUNNING);
	SMR_SLIST_INSERT_HEAD_LOCKED(&sec_map[idx], sc, sc_entry);

	return (0);
}

static int
sec_down(struct sec_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	unsigned int idx = stoeplitz_h32(sc->sc_unit) % nitems(sec_map);

	NET_ASSERT_LOCKED();
	KASSERT(ISSET(ifp->if_flags, IFF_RUNNING));

	/*
	 * taking sec down involves waiting for it to stop running
	 * in various contexts. this thread cannot hold netlock
	 * while waiting for a barrier for a task that could be trying
	 * to take netlock itself. so give up netlock, but don't clear
	 * sc_up to prevent sec_up from running.
	 */

	CLR(ifp->if_flags, IFF_RUNNING);
	NET_UNLOCK();

	smr_barrier();
	taskq_del_barrier(systq, &sc->sc_send);

	refcnt_finalize(&sc->sc_refs, "secdown");

	NET_LOCK();
	SMR_SLIST_REMOVE_LOCKED(&sec_map[idx], sc, sec_softc, sc_entry);
	sc->sc_up = 0;

	return (0);
}

static int
sec_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	struct m_tag *mtag;
	int error = 0;

	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		error = ENETDOWN;
		goto drop;
	}

	switch (dst->sa_family) {
	case AF_INET:
#ifdef INET6
	case AF_INET6:
#endif
#ifdef MPLS
	case AF_MPLS:
#endif
		break;
	default:
		error = EAFNOSUPPORT;
		goto drop;
	}

	mtag = NULL;
	while ((mtag = m_tag_find(m, PACKET_TAG_GRE, mtag)) != NULL) {
		if (ifp->if_index == *(int *)(mtag + 1)) {
			error = EIO;
			goto drop;
		}
	}

	mtag = m_tag_get(PACKET_TAG_GRE, sizeof(ifp->if_index), M_NOWAIT);
	if (mtag == NULL) {
		error = ENOBUFS;
		goto drop;
	}
	*(int *)(mtag + 1) = ifp->if_index;
	m_tag_prepend(m, mtag);

	m->m_pkthdr.ph_family = dst->sa_family;

	error = if_enqueue(ifp, m);
	if (error != 0)
		counters_inc(ifp->if_counters, ifc_oqdrops);

	return (error);

drop:
	m_freem(m);
	return (error);
}

static int
sec_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	struct sec_softc *sc = ifp->if_softc;
	struct ifqueue *ifq = &ifp->if_snd;
	int error;

	error = ifq_enqueue(ifq, m);
	if (error)
		return (error);

	task_add(systq, &sc->sc_send);

	return (0);
}

static void
sec_send(void *arg)
{
	struct sec_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_if;
	struct ifqueue *ifq = &ifp->if_snd;
	struct tdb *tdb;
	struct mbuf *m;
	int error;
	unsigned int flowid;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return;

	tdb = sec_tdb_get(sc->sc_unit);
	if (tdb == NULL)
		goto purge;

	flowid = sc->sc_unit ^ sec_mix;

	NET_LOCK();
	while ((m = ifq_dequeue(ifq)) != NULL) {
		CLR(m->m_flags, M_BCAST|M_MCAST);

#if NPF > 0
		pf_pkt_addr_changed(m);
#endif

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_af(ifp->if_bpf, m->m_pkthdr.ph_family, m,
			    BPF_DIRECTION_OUT);
#endif

		m->m_pkthdr.pf.prio = sc->sc_txprio;
		SET(m->m_pkthdr.csum_flags, M_FLOWID);
		m->m_pkthdr.ph_flowid = flowid;

		error = ipsp_process_packet(m, tdb,
		    m->m_pkthdr.ph_family, /* already tunnelled? */ 0,
		    sc->sc_tunneldf);
		if (error != 0)
			counters_inc(ifp->if_counters, ifc_oerrors);
	}
	NET_UNLOCK();

	tdb_unref(tdb);
	return;

purge:
	counters_add(ifp->if_counters, ifc_oerrors, ifq_purge(ifq));
}

static void
sec_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct sec_softc *sc = ifp->if_softc;

	/* move this back to systq for KERNEL_LOCK */
	task_add(systq, &sc->sc_send);
}

/*
 * ipsec_input handling
 */

struct sec_softc *
sec_get(unsigned int unit)
{
	unsigned int idx = stoeplitz_h32(unit) % nitems(sec_map);
	struct sec_bucket *sb = &sec_map[idx];
	struct sec_softc *sc;

	smr_read_enter();
	SMR_SLIST_FOREACH(sc, sb, sc_entry) {
		if (sc->sc_unit == unit) {
			refcnt_take(&sc->sc_refs);
			break;
		}
	}
	smr_read_leave();

	return (sc);
}

void
sec_input(struct sec_softc *sc, int af, int proto, struct mbuf *m,
    struct netstack *ns)
{
	struct ip *iph;
	int hlen;

	switch (af) {
	case AF_INET:
		iph = mtod(m, struct ip *);
		hlen = iph->ip_hl << 2;
		break;
#ifdef INET6
	case AF_INET6:
		hlen = sizeof(struct ip6_hdr);
		break;
#endif
	default:
		unhandled_af(af);
	}

	m_adj(m, hlen);

	switch (proto) {
	case IPPROTO_IPV4:
		af = AF_INET;
		break;
	case IPPROTO_IPV6:
		af = AF_INET6;
		break;
	case IPPROTO_MPLS:
		af = AF_MPLS;
		break;
	default:
		af = AF_UNSPEC;
		break;
	}

	m->m_pkthdr.ph_family = af;

	if_vinput(&sc->sc_if, m, ns);
}

void
sec_put(struct sec_softc *sc)
{
	refcnt_rele_wake(&sc->sc_refs);
}

/*
 * tdb handling
 */

static int
sec_tdb_valid(struct tdb *tdb)
{
	KASSERT(ISSET(tdb->tdb_flags, TDBF_IFACE));

	if (!ISSET(tdb->tdb_flags, TDBF_TUNNELING))
		return (0);
	if (ISSET(tdb->tdb_flags, TDBF_INVALID))
		return (0);

	if (tdb->tdb_iface_dir != IPSP_DIRECTION_OUT)
		return (0);

	return (1);
}

/*
 * these are called from netinet/ip_ipsp.c with tdb_sadb_mtx held,
 * which we rely on to serialise modifications to the sec_tdbh.
 */

void
sec_tdb_insert(struct tdb *tdb)
{
	unsigned int idx;
	struct tdb **tdbp;
	struct tdb *ltdb;

	if (!sec_tdb_valid(tdb))
		return;

	idx = stoeplitz_h32(tdb->tdb_iface) % nitems(sec_tdbh);
	tdbp = &sec_tdbh[idx];

	tdb_ref(tdb); /* take a ref for the SMR pointer */

	/* wire the tdb into the head of the list */
	ltdb = SMR_PTR_GET_LOCKED(tdbp);
	SMR_PTR_SET_LOCKED(&tdb->tdb_dnext, ltdb);
	SMR_PTR_SET_LOCKED(tdbp, tdb);
}

void
sec_tdb_remove(struct tdb *tdb)
{
	struct tdb **tdbp;
	struct tdb *ltdb;
	unsigned int idx;

	if (!sec_tdb_valid(tdb))
		return;

	idx = stoeplitz_h32(tdb->tdb_iface) % nitems(sec_tdbh);
	tdbp = &sec_tdbh[idx];

	while ((ltdb = SMR_PTR_GET_LOCKED(tdbp)) != NULL) {
		if (ltdb == tdb) {
			/* take the tdb out of the list */
			ltdb = SMR_PTR_GET_LOCKED(&tdb->tdb_dnext);
			SMR_PTR_SET_LOCKED(tdbp, ltdb);

			/* move the ref to the gc */

			mtx_enter(&sec_tdb_gc_mtx);
			tdb->tdb_dnext = sec_tdb_gc_list;
			sec_tdb_gc_list = tdb;
			mtx_leave(&sec_tdb_gc_mtx);
			task_add(systq, &sec_tdb_gc_task);

			return;
		}

		tdbp = &ltdb->tdb_dnext;
	}

	panic("%s: unable to find tdb %p", __func__, tdb);
}

static void
sec_tdb_gc(void *null)
{
	struct tdb *tdb, *ntdb;

	mtx_enter(&sec_tdb_gc_mtx);
	tdb = sec_tdb_gc_list;
	sec_tdb_gc_list = NULL;
	mtx_leave(&sec_tdb_gc_mtx);

	if (tdb == NULL)
		return;

	smr_barrier();

	NET_LOCK();
	do {
		ntdb = tdb->tdb_dnext;
		tdb_unref(tdb);
		tdb = ntdb;
	} while (tdb != NULL);
	NET_UNLOCK();
}

struct tdb *
sec_tdb_get(unsigned int unit)
{
	unsigned int idx;
	struct tdb **tdbp;
	struct tdb *tdb;

	idx = stoeplitz_h32(unit) % nitems(sec_map);
	tdbp = &sec_tdbh[idx];

	smr_read_enter();
	while ((tdb = SMR_PTR_GET(tdbp)) != NULL) {
		KASSERT(ISSET(tdb->tdb_flags, TDBF_IFACE));
		if (!ISSET(tdb->tdb_flags, TDBF_DELETED) &&
		    tdb->tdb_iface == unit) {
			tdb_ref(tdb);
			break;
		}

		tdbp = &tdb->tdb_dnext;
	}
	smr_read_leave();

	return (tdb);
}

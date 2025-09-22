/*	$OpenBSD: if_tpmr.c,v 1.37 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2019 The University of Queensland
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

#include "bpfilter.h"
#include "pf.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/percpu.h>
#include <sys/smr.h>
#include <sys/task.h>

#include <net/if.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_bridge.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NPF > 0
#include <net/pfvar.h>
#endif

#if NVLAN > 0
#include <net/if_vlan_var.h>
#endif

/*
 * tpmr interface
 */

#define TPMR_NUM_PORTS		2

struct tpmr_softc;

struct tpmr_port {
	struct ifnet		*p_ifp0;

	int (*p_ioctl)(struct ifnet *, u_long, caddr_t);
	int (*p_output)(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);

	struct task		 p_ltask;
	struct task		 p_dtask;

	struct tpmr_softc	*p_tpmr;
	unsigned int		 p_slot;

	int		 	 p_refcnt;

	struct ether_brport	 p_brport;
};

struct tpmr_softc {
	struct ifnet		 sc_if;
	unsigned int		 sc_dead;

	struct tpmr_port	*sc_ports[TPMR_NUM_PORTS];
	unsigned int		 sc_nports;
};

#define DPRINTF(_sc, fmt...)	do { \
	if (ISSET((_sc)->sc_if.if_flags, IFF_DEBUG)) \
		printf(fmt); \
} while (0)

static int	tpmr_clone_create(struct if_clone *, int);
static int	tpmr_clone_destroy(struct ifnet *);

static int	tpmr_ioctl(struct ifnet *, u_long, caddr_t);
static int	tpmr_enqueue(struct ifnet *, struct mbuf *);
static int	tpmr_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
static void	tpmr_start(struct ifqueue *);

static int	tpmr_up(struct tpmr_softc *);
static int	tpmr_down(struct tpmr_softc *);
static int	tpmr_iff(struct tpmr_softc *);

static void	tpmr_p_linkch(void *);
static void	tpmr_p_detach(void *);
static int	tpmr_p_ioctl(struct ifnet *, u_long, caddr_t);
static int	tpmr_p_output(struct ifnet *, struct mbuf *,
		    struct sockaddr *, struct rtentry *);

static void	tpmr_p_dtor(struct tpmr_softc *, struct tpmr_port *,
		    const char *);
static int	tpmr_add_port(struct tpmr_softc *,
		    const struct ifbreq *);
static int	tpmr_del_port(struct tpmr_softc *,
		    const struct ifbreq *);
static int	tpmr_port_list(struct tpmr_softc *, struct ifbifconf *);
static void	tpmr_p_take(void *);
static void	tpmr_p_rele(void *);

static struct if_clone tpmr_cloner =
    IF_CLONE_INITIALIZER("tpmr", tpmr_clone_create, tpmr_clone_destroy);

void
tpmrattach(int count)
{
	if_clone_attach(&tpmr_cloner);
}

static int
tpmr_clone_create(struct if_clone *ifc, int unit)
{
	struct tpmr_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO|M_CANFAIL);
	if (sc == NULL)
		return (ENOMEM);

	ifp = &sc->sc_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	ifp->if_softc = sc;
	ifp->if_type = IFT_BRIDGE;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_mtu = 0;
	ifp->if_addrlen = ETHER_ADDR_LEN;
	ifp->if_hdrlen = ETHER_HDR_LEN;
	ifp->if_ioctl = tpmr_ioctl;
	ifp->if_output = tpmr_output;
	ifp->if_enqueue = tpmr_enqueue;
	ifp->if_qstart = tpmr_start;
	ifp->if_flags = IFF_POINTOPOINT;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;
	ifp->if_link_state = LINK_STATE_DOWN;

	if_counters_alloc(ifp);
	if_attach(ifp);
	if_alloc_sadl(ifp);

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_EN10MB, ETHER_HDR_LEN);
#endif

	ifp->if_llprio = IFQ_MAXPRIO;

	return (0);
}

static int
tpmr_clone_destroy(struct ifnet *ifp)
{
	struct tpmr_softc *sc = ifp->if_softc;
	unsigned int i;

	NET_LOCK();
	sc->sc_dead = 1;

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		tpmr_down(sc);
	NET_UNLOCK();

	if_detach(ifp);

	NET_LOCK();
	for (i = 0; i < nitems(sc->sc_ports); i++) {
		struct tpmr_port *p = SMR_PTR_GET_LOCKED(&sc->sc_ports[i]);
		if (p == NULL)
			continue;
		tpmr_p_dtor(sc, p, "destroy");
	}
	NET_UNLOCK();

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
tpmr_vlan_filter(const struct mbuf *m)
{
	const struct ether_header *eh;

	eh = mtod(m, struct ether_header *);
	switch (ntohs(eh->ether_type)) {
	case ETHERTYPE_VLAN:
	case ETHERTYPE_QINQ:
		return (1);
	default:
		break;
	}

	return (0);
}

static int
tpmr_8021q_filter(const struct mbuf *m, uint64_t dst)
{
	if (ETH64_IS_8021_RSVD(dst)) {
		switch (dst & 0xf) {
		case 0x01: /* IEEE MAC-specific Control Protocols */
		case 0x02: /* IEEE 802.3 Slow Protocols */
		case 0x04: /* IEEE MAC-specific Control Protocols */
		case 0x0e: /* Individual LAN Scope, Nearest Bridge */
			return (1);
		default:
			break;
		}
	}

	return (0);
}

#if NPF > 0
struct tpmr_pf_ip_family {
	sa_family_t	   af;
	struct mbuf	*(*ip_check)(struct ifnet *, struct mbuf *);
	void		 (*ip_input)(struct ifnet *, struct mbuf *,
			    struct netstack *);
};

static const struct tpmr_pf_ip_family tpmr_pf_ipv4 = {
	.af		= AF_INET,
	.ip_check	= ipv4_check,
	.ip_input	= ipv4_input,
};

#ifdef INET6
static const struct tpmr_pf_ip_family tpmr_pf_ipv6 = {
	.af		= AF_INET6,
	.ip_check	= ipv6_check,
	.ip_input	= ipv6_input,
};
#endif

static struct mbuf *
tpmr_pf(struct ifnet *ifp0, int dir, struct mbuf *m, struct netstack *ns)
{
	struct ether_header *eh, copy;
	const struct tpmr_pf_ip_family *fam;

	eh = mtod(m, struct ether_header *);
	switch (ntohs(eh->ether_type)) {
	case ETHERTYPE_IP:
		fam = &tpmr_pf_ipv4;
		break;
#ifdef INET6
	case ETHERTYPE_IPV6:
		fam = &tpmr_pf_ipv6;
		break;
#endif
	default:
		return (m);
	}

	copy = *eh;
	m_adj(m, sizeof(*eh));

	if (dir == PF_IN) {
		m = (*fam->ip_check)(ifp0, m);
		if (m == NULL)
			return (NULL);
	}

	if (pf_test(fam->af, dir, ifp0, &m) != PF_PASS) {
		m_freem(m);
		return (NULL);
	}
	if (m == NULL)
		return (NULL);

	if (dir == PF_IN && ISSET(m->m_pkthdr.pf.flags, PF_TAG_DIVERTED)) {
		pf_mbuf_unlink_state_key(m);
		pf_mbuf_unlink_inpcb(m);
		(*fam->ip_input)(ifp0, m, ns);
		return (NULL);
	}

	m = m_prepend(m, sizeof(*eh), M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	/* checksum? */

	eh = mtod(m, struct ether_header *);
	*eh = copy;

	return (m);
}
#endif /* NPF > 0 */

static struct mbuf *
tpmr_input(struct ifnet *ifp0, struct mbuf *m, uint64_t dst, void *brport,
 struct netstack *ns)
{
	struct tpmr_port *p = brport;
	struct tpmr_softc *sc = p->p_tpmr;
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifpn;
	unsigned int iff;
	struct tpmr_port *pn;
	int len;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	iff = READ_ONCE(ifp->if_flags);
	if (!ISSET(iff, IFF_RUNNING))
		goto drop;

#if NVLAN > 0
	/*
	 * If the underlying interface removed the VLAN header itself,
	 * add it back.
	 */
	if (ISSET(m->m_flags, M_VLANTAG)) {
		m = vlan_inject(m, ETHERTYPE_VLAN, m->m_pkthdr.ether_vtag);
		if (m == NULL) {
			counters_inc(ifp->if_counters, ifc_ierrors);
			goto drop;
		}
	}
#endif

	if (!ISSET(iff, IFF_LINK2) &&
	    tpmr_vlan_filter(m))
		goto drop;

	if (!ISSET(iff, IFF_LINK0) &&
	    tpmr_8021q_filter(m, dst))
		goto drop;

#if NPF > 0
	if (!ISSET(iff, IFF_LINK1) &&
	    (m = tpmr_pf(ifp0, PF_IN, m, ns)) == NULL)
		return (NULL);
#endif

	len = m->m_pkthdr.len;
	counters_pkt(ifp->if_counters, ifc_ipackets, ifc_ibytes, len);

#if NBPFILTER > 0
	if_bpf = READ_ONCE(ifp->if_bpf);
	if (if_bpf) {
		if (bpf_mtap(if_bpf, m, 0))
			goto drop;
	}
#endif

	smr_read_enter();
	pn = SMR_PTR_GET(&sc->sc_ports[!p->p_slot]);
	if (pn != NULL)
		tpmr_p_take(pn);
	smr_read_leave();
	if (pn == NULL)
		goto drop;

	ifpn = pn->p_ifp0;
#if NPF > 0
	if (!ISSET(iff, IFF_LINK1) &&
	    (m = tpmr_pf(ifpn, PF_OUT, m, ns)) == NULL) {
		tpmr_p_rele(pn);
		return (NULL);
	}
#endif

	if (if_enqueue(ifpn, m))
		counters_inc(ifp->if_counters, ifc_oerrors);
	else {
		counters_pkt(ifp->if_counters,
		    ifc_opackets, ifc_obytes, len);
	}

	tpmr_p_rele(pn);

	return (NULL);

drop:
	m_freem(m);
	return (NULL);
}

static int
tpmr_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	m_freem(m);
	return (ENODEV);
}

static int
tpmr_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	m_freem(m);
	return (ENODEV);
}

static void
tpmr_start(struct ifqueue *ifq)
{
	ifq_purge(ifq);
}

static int
tpmr_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct tpmr_softc *sc = ifp->if_softc;
	int error = 0;

	if (sc->sc_dead)
		return (ENXIO);

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = tpmr_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = tpmr_down(sc);
		}
		break;

	case SIOCBRDGADD:
		error = suser(curproc);
		if (error != 0)
			break;

		error = tpmr_add_port(sc, (struct ifbreq *)data);
		break;
	case SIOCBRDGDEL:
		error = suser(curproc);
		if (error != 0)
			break;

		error = tpmr_del_port(sc, (struct ifbreq *)data);
		break;
	case SIOCBRDGIFS:
		error = tpmr_port_list(sc, (struct ifbifconf *)data);
		break;
	/* stub for ifconfig(8) brconfig.c:bridge_rules() */
	case SIOCBRDGGRL:
		((struct ifbrlconf *)data)->ifbrl_len = 0;
		break;

	default:
		error = ENOTTY;
		break;
	}

	if (error == ENETRESET)
		error = tpmr_iff(sc);

	return (error);
}

static int
tpmr_add_port(struct tpmr_softc *sc, const struct ifbreq *req)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	struct tpmr_port **pp;
	struct tpmr_port *p;
	int i;
	int error;

	NET_ASSERT_LOCKED();
	if (sc->sc_nports >= nitems(sc->sc_ports))
		return (ENOSPC);

	ifp0 = if_unit(req->ifbr_ifsname);
	if (ifp0 == NULL)
		return (EINVAL);

	if (ifp0->if_type != IFT_ETHER) {
		error = EPROTONOSUPPORT;
		goto put;
	}

	error = ether_brport_isset(ifp0);
	if (error != 0)
		goto put;

	/* let's try */

	p = malloc(sizeof(*p), M_DEVBUF, M_WAITOK|M_ZERO|M_CANFAIL);
	if (p == NULL) {
		error = ENOMEM;
		goto put;
	}

	ifsetlro(ifp0, 0);

	p->p_ifp0 = ifp0;
	p->p_tpmr = sc;

	p->p_ioctl = ifp0->if_ioctl;
	p->p_output = ifp0->if_output;

	error = ifpromisc(ifp0, 1);
	if (error != 0)
		goto free;

	/* this might have changed if we slept for malloc or ifpromisc */
	error = ether_brport_isset(ifp0);
	if (error != 0)
		goto unpromisc;

	task_set(&p->p_ltask, tpmr_p_linkch, p);
	if_linkstatehook_add(ifp0, &p->p_ltask);

	task_set(&p->p_dtask, tpmr_p_detach, p);
	if_detachhook_add(ifp0, &p->p_dtask);

	p->p_brport.eb_input = tpmr_input;
	p->p_brport.eb_port_take = tpmr_p_take;
	p->p_brport.eb_port_rele = tpmr_p_rele;
	p->p_brport.eb_port = p;

	/* commit */
	DPRINTF(sc, "%s %s trunkport: creating port\n",
	    ifp->if_xname, ifp0->if_xname);

	for (i = 0; i < nitems(sc->sc_ports); i++) {
		pp = &sc->sc_ports[i];
		if (SMR_PTR_GET_LOCKED(pp) == NULL)
			break;
	}
	sc->sc_nports++;

	p->p_slot = i;

	tpmr_p_take(p);
	ether_brport_set(ifp0, &p->p_brport);
	ifp0->if_ioctl = tpmr_p_ioctl;
	ifp0->if_output = tpmr_p_output;

	SMR_PTR_SET_LOCKED(pp, p);

	tpmr_p_linkch(p);

	return (0);

unpromisc:
	ifpromisc(ifp0, 0);
free:
	free(p, M_DEVBUF, sizeof(*p));
put:
	if_put(ifp0);
	return (error);
}

static struct tpmr_port *
tpmr_trunkport(struct tpmr_softc *sc, const char *name)
{
	unsigned int i;

	for (i = 0; i < nitems(sc->sc_ports); i++) {
		struct tpmr_port *p = SMR_PTR_GET_LOCKED(&sc->sc_ports[i]);
		if (p == NULL)
			continue;

		if (strcmp(p->p_ifp0->if_xname, name) == 0)
			return (p);
	}

	return (NULL);
}

static int
tpmr_del_port(struct tpmr_softc *sc, const struct ifbreq *req)
{
	struct tpmr_port *p;

	NET_ASSERT_LOCKED();
	p = tpmr_trunkport(sc, req->ifbr_ifsname);
	if (p == NULL)
		return (EINVAL);

	tpmr_p_dtor(sc, p, "del");

	return (0);
}


static int
tpmr_port_list(struct tpmr_softc *sc, struct ifbifconf *bifc)
{
	struct tpmr_port *p;
	struct ifbreq breq;
	int i = 0, total = nitems(sc->sc_ports), n = 0, error = 0;

	NET_ASSERT_LOCKED();

	if (bifc->ifbic_len == 0) {
		n = total;
		goto done;
	}

	for (i = 0; i < total; i++) {
		memset(&breq, 0, sizeof(breq));

		if (bifc->ifbic_len < sizeof(breq))
			break;

		p = SMR_PTR_GET_LOCKED(&sc->sc_ports[i]);
		if (p == NULL)
			continue;
		strlcpy(breq.ifbr_ifsname, p->p_ifp0->if_xname, IFNAMSIZ);

		/* flag as span port so ifconfig(8)'s brconfig.c:bridge_list()
		 * stays quiet wrt. STP */
		breq.ifbr_ifsflags = IFBIF_SPAN;
		strlcpy(breq.ifbr_name, sc->sc_if.if_xname, IFNAMSIZ);
		if ((error = copyout(&breq, bifc->ifbic_req + n,
		    sizeof(breq))) != 0)
			goto done;

		bifc->ifbic_len -= sizeof(breq);
		n++;
	}

done:
	bifc->ifbic_len = n * sizeof(breq);
	return (error);
}

static int
tpmr_p_ioctl(struct ifnet *ifp0, u_long cmd, caddr_t data)
{
	const struct ether_brport *eb = ether_brport_get_locked(ifp0);
	struct tpmr_port *p;
	int error = 0;

	KASSERTMSG(eb != NULL,
	    "%s: %s called without an ether_brport set",
	    ifp0->if_xname, __func__);
	KASSERTMSG(eb->eb_input == tpmr_input,
	    "%s: %s called, but eb_input seems wrong (%p != tpmr_input())",
	    ifp0->if_xname, __func__, eb->eb_input);

	p = eb->eb_port;

	switch (cmd) {
	case SIOCSIFADDR:
		error = EBUSY;
		break;

	default:
		error = (*p->p_ioctl)(ifp0, cmd, data);
		break;
	}

	return (error);
}

static int
tpmr_p_output(struct ifnet *ifp0, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	int (*p_output)(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *) = NULL;
	const struct ether_brport *eb;

	/* restrict transmission to bpf only */
	if ((m_tag_find(m, PACKET_TAG_DLT, NULL) == NULL)) {
		m_freem(m);
		return (EBUSY);
	}

	smr_read_enter();
	eb = ether_brport_get(ifp0);
	if (eb != NULL && eb->eb_input == tpmr_input) {
		struct tpmr_port *p = eb->eb_port;
		p_output = p->p_output; /* code doesn't go away */
	}
	smr_read_leave();

	if (p_output == NULL) {
		m_freem(m);
		return (ENXIO);
	}

	return ((*p_output)(ifp0, m, dst, rt));
}

static void
tpmr_p_take(void *p)
{
	struct tpmr_port *port = p;

	atomic_inc_int(&port->p_refcnt);
}

static void
tpmr_p_rele(void *p)
{
	struct tpmr_port *port = p;
	struct ifnet *ifp0 = port->p_ifp0;

	if (atomic_dec_int_nv(&port->p_refcnt) == 0) {
		if_put(ifp0);
		free(port, M_DEVBUF, sizeof(*port));
	}
}

static void
tpmr_p_dtor(struct tpmr_softc *sc, struct tpmr_port *p, const char *op)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0 = p->p_ifp0;

	DPRINTF(sc, "%s %s: destroying port\n",
	    ifp->if_xname, ifp0->if_xname);

	ifp0->if_ioctl = p->p_ioctl;
	ifp0->if_output = p->p_output;

	ether_brport_clr(ifp0);

	sc->sc_nports--;
	SMR_PTR_SET_LOCKED(&sc->sc_ports[p->p_slot], NULL);

	if (ifpromisc(ifp0, 0) != 0) {
		log(LOG_WARNING, "%s %s: unable to disable promisc\n",
		    ifp->if_xname, ifp0->if_xname);
	}

	if_detachhook_del(ifp0, &p->p_dtask);
	if_linkstatehook_del(ifp0, &p->p_ltask);

	tpmr_p_rele(p);

	smr_barrier();

	if (ifp->if_link_state != LINK_STATE_DOWN) {
		ifp->if_link_state = LINK_STATE_DOWN;
		if_link_state_change(ifp);
	}
}

static void
tpmr_p_detach(void *arg)
{
	struct tpmr_port *p = arg;
	struct tpmr_softc *sc = p->p_tpmr;

	tpmr_p_dtor(sc, p, "detach");

	NET_ASSERT_LOCKED();
}

static int
tpmr_p_active(struct tpmr_port *p)
{
	struct ifnet *ifp0 = p->p_ifp0;

	return (ISSET(ifp0->if_flags, IFF_RUNNING) &&
	    LINK_STATE_IS_UP(ifp0->if_link_state));
}

static void
tpmr_p_linkch(void *arg)
{
	struct tpmr_port *p = arg;
	struct tpmr_softc *sc = p->p_tpmr;
	struct ifnet *ifp = &sc->sc_if;
	struct tpmr_port *np;
	u_char link_state = LINK_STATE_FULL_DUPLEX;

	NET_ASSERT_LOCKED();

	if (!tpmr_p_active(p))
		link_state = LINK_STATE_DOWN;

	np = SMR_PTR_GET_LOCKED(&sc->sc_ports[!p->p_slot]);
	if (np == NULL || !tpmr_p_active(np))
		link_state = LINK_STATE_DOWN;

	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}

static int
tpmr_up(struct tpmr_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;

	NET_ASSERT_LOCKED();
	SET(ifp->if_flags, IFF_RUNNING);

	return (0);
}

static int
tpmr_iff(struct tpmr_softc *sc)
{
	return (0);
}

static int
tpmr_down(struct tpmr_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;

	NET_ASSERT_LOCKED();
	CLR(ifp->if_flags, IFF_RUNNING);

	return (0);
}

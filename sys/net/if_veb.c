/*	$OpenBSD: if_veb.c,v 1.43 2025/08/04 13:27:55 jan Exp $ */

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
#include "vlan.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/rwlock.h>
#include <sys/percpu.h>
#include <sys/smr.h>
#include <sys/task.h>
#include <sys/pool.h>

#include <net/if.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if 0 && defined(IPSEC)
/*
 * IPsec handling is disabled in veb until getting and using tdbs is mpsafe.
 */
#include <netinet/ip_ipsp.h>
#include <net/if_enc.h>
#endif

#include <net/if_bridge.h>
#include <net/if_etherbridge.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NPF > 0
#include <net/pfvar.h>
#endif

#if NVLAN > 0
#include <net/if_vlan_var.h>
#endif

/* SIOCBRDGIFFLGS, SIOCBRDGIFFLGS */
#define VEB_IFBIF_FLAGS	(IFBIF_LEARNING|IFBIF_DISCOVER|IFBIF_BLOCKNONIP)

struct veb_rule {
	TAILQ_ENTRY(veb_rule)		vr_entry;
	SMR_TAILQ_ENTRY(veb_rule)	vr_lentry[2];

	uint16_t			vr_flags;
#define VEB_R_F_IN				(1U << 0)
#define VEB_R_F_OUT				(1U << 1)
#define VEB_R_F_SRC				(1U << 2)
#define VEB_R_F_DST				(1U << 3)

#define VEB_R_F_ARP				(1U << 4)
#define VEB_R_F_RARP				(1U << 5)
#define VEB_R_F_SHA				(1U << 6)
#define VEB_R_F_SPA				(1U << 7)
#define VEB_R_F_THA				(1U << 8)
#define VEB_R_F_TPA				(1U << 9)
	uint16_t			 vr_arp_op;

	uint64_t			 vr_src;
	uint64_t			 vr_dst;
	struct ether_addr		 vr_arp_sha;
	struct ether_addr		 vr_arp_tha;
	struct in_addr			 vr_arp_spa;
	struct in_addr			 vr_arp_tpa;

	unsigned int			 vr_action;
#define VEB_R_MATCH				0
#define VEB_R_PASS				1
#define VEB_R_BLOCK				2

	int				 vr_pftag;
};

TAILQ_HEAD(veb_rules, veb_rule);
SMR_TAILQ_HEAD(veb_rule_list, veb_rule);

struct veb_softc;

struct veb_port {
	struct ifnet			*p_ifp0;
	struct refcnt			 p_refs;

	int (*p_enqueue)(struct ifnet *, struct mbuf *);

	int (*p_ioctl)(struct ifnet *, u_long, caddr_t);
	int (*p_output)(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);

	struct task			 p_ltask;
	struct task			 p_dtask;

	struct veb_softc		*p_veb;

	struct ether_brport		 p_brport;

	unsigned int			 p_link_state;
	unsigned int			 p_bif_flags;
	uint32_t			 p_protected;

	struct veb_rules		 p_vrl;
	unsigned int			 p_nvrl;
	struct veb_rule_list		 p_vr_list[2];
#define VEB_RULE_LIST_OUT			0
#define VEB_RULE_LIST_IN			1
};

struct veb_ports {
	struct refcnt			 m_refs;
	unsigned int			 m_count;

	/* followed by an array of veb_port pointers */
};

struct veb_softc {
	struct ifnet			 sc_if;
	unsigned int			 sc_dead;

	struct etherbridge		 sc_eb;

	struct rwlock			 sc_rule_lock;
	struct veb_ports		*sc_ports;
	struct veb_ports		*sc_spans;
};

#define DPRINTF(_sc, fmt...)    do { \
	if (ISSET((_sc)->sc_if.if_flags, IFF_DEBUG)) \
		printf(fmt); \
} while (0)

static int	veb_clone_create(struct if_clone *, int);
static int	veb_clone_destroy(struct ifnet *);

static int	veb_ioctl(struct ifnet *, u_long, caddr_t);
static void	veb_input(struct ifnet *, struct mbuf *, struct netstack *);
static int	veb_enqueue(struct ifnet *, struct mbuf *);
static int	veb_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
static void	veb_start(struct ifqueue *);

static int	veb_up(struct veb_softc *);
static int	veb_down(struct veb_softc *);
static int	veb_iff(struct veb_softc *);

static void	veb_p_linkch(void *);
static void	veb_p_detach(void *);
static int	veb_p_ioctl(struct ifnet *, u_long, caddr_t);
static int	veb_p_output(struct ifnet *, struct mbuf *,
		    struct sockaddr *, struct rtentry *);

static inline size_t
veb_ports_size(unsigned int n)
{
	/* use of _ALIGN is inspired by CMSGs */
	return _ALIGN(sizeof(struct veb_ports)) +
	    n * sizeof(struct veb_port *);
}

static inline struct veb_port **
veb_ports_array(struct veb_ports *m)
{
	return (struct veb_port **)((caddr_t)m + _ALIGN(sizeof(*m)));
}

static inline void veb_ports_free(struct veb_ports *);

static void	veb_p_unlink(struct veb_softc *, struct veb_port *);
static void	veb_p_fini(struct veb_port *);
static void	veb_p_dtor(struct veb_softc *, struct veb_port *);
static int	veb_add_port(struct veb_softc *,
		    const struct ifbreq *, unsigned int);
static int	veb_del_port(struct veb_softc *,
		    const struct ifbreq *, unsigned int);
static int	veb_port_list(struct veb_softc *, struct ifbifconf *);
static int	veb_port_set_flags(struct veb_softc *, struct ifbreq *);
static int	veb_port_get_flags(struct veb_softc *, struct ifbreq *);
static int	veb_port_set_protected(struct veb_softc *,
		    const struct ifbreq *);
static int	veb_add_addr(struct veb_softc *, const struct ifbareq *);
static int	veb_del_addr(struct veb_softc *, const struct ifbareq *);

static int	veb_rule_add(struct veb_softc *, const struct ifbrlreq *);
static int	veb_rule_list_flush(struct veb_softc *,
		    const struct ifbrlreq *);
static void	veb_rule_list_free(struct veb_rule *);
static int	veb_rule_list_get(struct veb_softc *, struct ifbrlconf *);

static int	 veb_eb_port_cmp(void *, void *, void *);
static void	*veb_eb_port_take(void *, void *);
static void	 veb_eb_port_rele(void *, void *);
static size_t	 veb_eb_port_ifname(void *, char *, size_t, void *);
static void	 veb_eb_port_sa(void *, struct sockaddr_storage *, void *);

static void	 veb_eb_brport_take(void *);
static void	 veb_eb_brport_rele(void *);

static const struct etherbridge_ops veb_etherbridge_ops = {
	veb_eb_port_cmp,
	veb_eb_port_take,
	veb_eb_port_rele,
	veb_eb_port_ifname,
	veb_eb_port_sa,
};

static struct if_clone veb_cloner =
    IF_CLONE_INITIALIZER("veb", veb_clone_create, veb_clone_destroy);

static struct pool veb_rule_pool;

static int	vport_clone_create(struct if_clone *, int);
static int	vport_clone_destroy(struct ifnet *);

struct vport_softc {
	struct arpcom		 sc_ac;
	unsigned int		 sc_dead;
};

static int	vport_if_enqueue(struct ifnet *, struct mbuf *);

static int	vport_ioctl(struct ifnet *, u_long, caddr_t);
static int	vport_enqueue(struct ifnet *, struct mbuf *);
static void	vport_start(struct ifqueue *);

static int	vport_up(struct vport_softc *);
static int	vport_down(struct vport_softc *);
static int	vport_iff(struct vport_softc *);

static struct if_clone vport_cloner =
    IF_CLONE_INITIALIZER("vport", vport_clone_create, vport_clone_destroy);

void
vebattach(int count)
{
	if_clone_attach(&veb_cloner);
	if_clone_attach(&vport_cloner);
}

static int
veb_clone_create(struct if_clone *ifc, int unit)
{
	struct veb_softc *sc;
	struct ifnet *ifp;
	int error;

	if (veb_rule_pool.pr_size == 0) {
		pool_init(&veb_rule_pool, sizeof(struct veb_rule),
		    0, IPL_SOFTNET, 0, "vebrpl", NULL);
	}

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO|M_CANFAIL);
	if (sc == NULL)
		return (ENOMEM);

	rw_init(&sc->sc_rule_lock, "vebrlk");
	sc->sc_ports = NULL;
	sc->sc_spans = NULL;

	ifp = &sc->sc_if;

	snprintf(ifp->if_xname, IFNAMSIZ, "%s%d", ifc->ifc_name, unit);

	error = etherbridge_init(&sc->sc_eb, ifp->if_xname,
	    &veb_etherbridge_ops, sc);
	if (error != 0) {
		free(sc, M_DEVBUF, sizeof(*sc));
		return (error);
	}

	ifp->if_softc = sc;
	ifp->if_type = IFT_BRIDGE;
	ifp->if_hdrlen = ETHER_HDR_LEN;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_ioctl = veb_ioctl;
	ifp->if_input = veb_input;
	ifp->if_output = veb_output;
	ifp->if_enqueue = veb_enqueue;
	ifp->if_qstart = veb_start;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;

	if_counters_alloc(ifp);
	if_attach(ifp);

	if_alloc_sadl(ifp);

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_EN10MB, ETHER_HDR_LEN);
#endif

	return (0);
}

static int
veb_clone_destroy(struct ifnet *ifp)
{
	struct veb_softc *sc = ifp->if_softc;
	struct veb_ports *mp, *ms;
	struct veb_port **ps;
	struct veb_port *p;
	unsigned int i;

	NET_LOCK();
	sc->sc_dead = 1;

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		veb_down(sc);
	NET_UNLOCK();

	if_detach(ifp);

	NET_LOCK();

	/*
	 * this is an upside down version of veb_p_dtor() and
	 * veb_ports_destroy() to avoid a lot of malloc/free and
	 * smr_barrier calls if we remove ports one by one.
	 */

	mp = SMR_PTR_GET_LOCKED(&sc->sc_ports);
	SMR_PTR_SET_LOCKED(&sc->sc_ports, NULL);
	if (mp != NULL) {
		ps = veb_ports_array(mp);
		for (i = 0; i < mp->m_count; i++)
			veb_p_unlink(sc, ps[i]);
	}

	ms = SMR_PTR_GET_LOCKED(&sc->sc_spans);
	SMR_PTR_SET_LOCKED(&sc->sc_spans, NULL);
	if (ms != NULL) {
		ps = veb_ports_array(ms);
		for (i = 0; i < ms->m_count; i++)
			veb_p_unlink(sc, ps[i]);
	}

	if (mp != NULL || ms != NULL) {
		smr_barrier(); /* everything everywhere all at once */

		if (mp != NULL) {
			refcnt_finalize(&mp->m_refs, "vebdtor");

			ps = veb_ports_array(mp);
			for (i = 0; i < mp->m_count; i++) {
				p = ps[i];
				/* the ports map holds a port ref */
				refcnt_rele(&p->p_refs);
				/* now we can finalize the port */
				veb_p_fini(p);
			}

			veb_ports_free(mp);
		}
		if (ms != NULL) {
			refcnt_finalize(&ms->m_refs, "vebdtor");

			ps = veb_ports_array(ms);
			for (i = 0; i < ms->m_count; i++) {
				p = ps[i];
				/* the ports map holds a port ref */
				refcnt_rele(&p->p_refs);
				/* now we can finalize the port */
				veb_p_fini(p);
			}

			veb_ports_free(ms);
		}
	}
	NET_UNLOCK();

	etherbridge_destroy(&sc->sc_eb);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static struct mbuf *
veb_span_input(struct ifnet *ifp0, struct mbuf *m, uint64_t dst, void *brport,
    struct netstack *ns)
{
	m_freem(m);
	return (NULL);
}

static void
veb_span(struct veb_softc *sc, struct mbuf *m0)
{
	struct veb_ports *sm;
	struct veb_port **ps;
	struct veb_port *p;
	struct ifnet *ifp0;
	struct mbuf *m;
	unsigned int i;

	smr_read_enter();
	sm = SMR_PTR_GET(&sc->sc_spans);
	if (sm != NULL)
		refcnt_take(&sm->m_refs);
	smr_read_leave();
	if (sm == NULL)
		return;

	ps = veb_ports_array(sm);
	for (i = 0; i < sm->m_count; i++) {
		p = ps[i];

		ifp0 = p->p_ifp0;
		if (!ISSET(ifp0->if_flags, IFF_RUNNING))
			continue;

		m = m_dup_pkt(m0, max_linkhdr + ETHER_ALIGN, M_NOWAIT);
		if (m == NULL) {
			/* XXX count error */
			continue;
		}

		if_enqueue(ifp0, m); /* XXX count error */
	}
	refcnt_rele_wake(&sm->m_refs);
}

static int
veb_ip_filter(const struct mbuf *m)
{
	const struct ether_header *eh;

	if (ISSET(m->m_flags, M_VLANTAG))
		return (1);

	eh = mtod(m, struct ether_header *);
	switch (ntohs(eh->ether_type)) {
	case ETHERTYPE_IP:
	case ETHERTYPE_ARP:
	case ETHERTYPE_REVARP:
	case ETHERTYPE_IPV6:
		return (0);
	default:
		break;
	}

	return (1);
}

static int
veb_vlan_filter(const struct mbuf *m)
{
	const struct ether_header *eh;

	if (ISSET(m->m_flags, M_VLANTAG))
		return (1);

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
veb_rule_arp_match(const struct veb_rule *vr, struct mbuf *m)
{
	struct ether_header *eh;
	struct ether_arp ea;

	if (ISSET(m->m_flags, M_VLANTAG))
		return (0);

	eh = mtod(m, struct ether_header *);

	if (eh->ether_type != htons(ETHERTYPE_ARP))
		return (0);
	if (m->m_pkthdr.len < sizeof(*eh) + sizeof(ea))
		return (0);

	m_copydata(m, sizeof(*eh), sizeof(ea), (caddr_t)&ea);

	if (ea.arp_hrd != htons(ARPHRD_ETHER) ||
	    ea.arp_pro != htons(ETHERTYPE_IP) ||
	    ea.arp_hln != ETHER_ADDR_LEN ||
	    ea.arp_pln != sizeof(struct in_addr))
		return (0);

	if (ISSET(vr->vr_flags, VEB_R_F_ARP)) {
		if (ea.arp_op != htons(ARPOP_REQUEST) &&
		    ea.arp_op != htons(ARPOP_REPLY))
			return (0);
	}
	if (ISSET(vr->vr_flags, VEB_R_F_RARP)) {
		if (ea.arp_op != htons(ARPOP_REVREQUEST) &&
		    ea.arp_op != htons(ARPOP_REVREPLY))
			return (0);
	}

	if (vr->vr_arp_op != htons(0) && vr->vr_arp_op != ea.arp_op)
		return (0);

	if (ISSET(vr->vr_flags, VEB_R_F_SHA) &&
	    !ETHER_IS_EQ(&vr->vr_arp_sha, ea.arp_sha))
		return (0);
	if (ISSET(vr->vr_flags, VEB_R_F_THA) &&
	    !ETHER_IS_EQ(&vr->vr_arp_tha, ea.arp_tha))
		return (0);
	if (ISSET(vr->vr_flags, VEB_R_F_SPA) &&
	    memcmp(&vr->vr_arp_spa, ea.arp_spa, sizeof(vr->vr_arp_spa)) != 0)
		return (0);
	if (ISSET(vr->vr_flags, VEB_R_F_TPA) &&
	    memcmp(&vr->vr_arp_tpa, ea.arp_tpa, sizeof(vr->vr_arp_tpa)) != 0)
		return (0);

	return (1);
}

static int
veb_rule_list_test(struct veb_rule *vr, int dir, struct mbuf *m,
    uint64_t src, uint64_t dst)
{
	SMR_ASSERT_CRITICAL();

	do {
		if (ISSET(vr->vr_flags, VEB_R_F_ARP|VEB_R_F_RARP) &&
		    !veb_rule_arp_match(vr, m))
			continue;

		if (ISSET(vr->vr_flags, VEB_R_F_SRC) &&
		    vr->vr_src != src)
			continue;
		if (ISSET(vr->vr_flags, VEB_R_F_DST) &&
		    vr->vr_dst != dst)
			continue;

		if (vr->vr_action == VEB_R_BLOCK)
			return (VEB_R_BLOCK);
#if NPF > 0
		pf_tag_packet(m, vr->vr_pftag, -1);
#endif
		if (vr->vr_action == VEB_R_PASS)
			return (VEB_R_PASS);
	} while ((vr = SMR_TAILQ_NEXT(vr, vr_lentry[dir])) != NULL);

	return (VEB_R_PASS);
}

static inline int
veb_rule_filter(struct veb_port *p, int dir, struct mbuf *m,
    uint64_t src, uint64_t dst)
{
	struct veb_rule *vr;
	int filter = VEB_R_PASS;

	smr_read_enter();
	vr = SMR_TAILQ_FIRST(&p->p_vr_list[dir]);
	if (vr != NULL)
		filter = veb_rule_list_test(vr, dir, m, src, dst);
	smr_read_leave();

	return (filter == VEB_R_BLOCK);
}

#if NPF > 0
struct veb_pf_ip_family {
	sa_family_t	   af;
	struct mbuf	*(*ip_check)(struct ifnet *, struct mbuf *);
	void		 (*ip_input)(struct ifnet *, struct mbuf *,
			    struct netstack *);
};

static const struct veb_pf_ip_family veb_pf_ipv4 = {
	.af		= AF_INET,
	.ip_check	= ipv4_check,
	.ip_input	= ipv4_input,
};

#ifdef INET6
static const struct veb_pf_ip_family veb_pf_ipv6 = {
	.af		= AF_INET6,
	.ip_check	= ipv6_check,
	.ip_input	= ipv6_input,
};
#endif

static struct mbuf *
veb_pf(struct ifnet *ifp0, int dir, struct mbuf *m, struct netstack *ns)
{
	struct ether_header *eh, copy;
	const struct veb_pf_ip_family *fam;
	int hlen;

	/*
	 * pf runs on vport interfaces when they enter or leave the
	 * l3 stack, so don't confuse things (even more) by running
	 * pf again here. note that because of this exception the
	 * pf direction on vport interfaces is reversed compared to
	 * other veb ports.
	 */
	if (ifp0->if_enqueue == vport_enqueue)
		return (m);

	if (ISSET(m->m_flags, M_VLANTAG))
		return (m);

	eh = mtod(m, struct ether_header *);
	switch (ntohs(eh->ether_type)) {
	case ETHERTYPE_IP:
		fam = &veb_pf_ipv4;
		break;
#ifdef INET6
	case ETHERTYPE_IPV6:
		fam = &veb_pf_ipv6;
		break;
#endif
	default:
		return (m);
	}

	copy = *eh;
	m_adj(m, sizeof(*eh));

	m = (*fam->ip_check)(ifp0, m);
	if (m == NULL)
		return (NULL);

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

	hlen = roundup(sizeof(*eh), sizeof(long));
	m = m_prepend(m, hlen, M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	/* checksum? */

	m_adj(m, hlen - sizeof(*eh));
	eh = mtod(m, struct ether_header *);
	*eh = copy;

	return (m);
}
#endif /* NPF > 0 */

#if 0 && defined(IPSEC)
static struct mbuf *
veb_ipsec_proto_in(struct ifnet *ifp0, struct mbuf *m, int iphlen,
    /* const */ union sockaddr_union *dst, int poff)
{
	struct tdb *tdb;
	uint16_t cpi;
	uint32_t spi;
	uint8_t proto;

	/* ipsec_common_input checks for 8 bytes of input, so we do too */
	if (m->m_pkthdr.len < iphlen + 2 * sizeof(u_int32_t))
		return (m); /* decline */

	proto = *(mtod(m, uint8_t *) + poff);
	/* i'm not a huge fan of how these headers get picked at */
	switch (proto) {
	case IPPROTO_ESP:
		m_copydata(m, iphlen, sizeof(spi), &spi);
		break;
	case IPPROTO_AH:
		m_copydata(m, iphlen + sizeof(uint32_t), sizeof(spi), &spi);
		break;
	case IPPROTO_IPCOMP:
		m_copydata(m, iphlen + sizeof(uint16_t), sizeof(cpi), &cpi);
		spi = htonl(ntohs(cpi));
		break;
	default:
		return (m); /* decline */
	}

	tdb = gettdb(m->m_pkthdr.ph_rtableid, spi, dst, proto);
	if (tdb != NULL && !ISSET(tdb->tdb_flags, TDBF_INVALID) &&
	    tdb->tdb_xform != NULL) {
		if (tdb->tdb_first_use == 0) {
			tdb->tdb_first_use = gettime();
			if (ISSET(tdb->tdb_flags, TDBF_FIRSTUSE)) {
				timeout_add_sec(&tdb->tdb_first_tmo,
				    tdb->tdb_exp_first_use);
			}
			if (ISSET(tdb->tdb_flags, TDBF_SOFT_FIRSTUSE)) {
				timeout_add_sec(&tdb->tdb_sfirst_tmo,
				    tdb->tdb_soft_first_use);
			}
		}

		(*(tdb->tdb_xform->xf_input))(m, tdb, iphlen, poff, NULL);
		return (NULL);
	}

	return (m);
}

static struct mbuf *
veb_ipsec_ipv4_in(struct ifnet *ifp0, struct mbuf *m)
{
	union sockaddr_union su = {
		.sin.sin_len = sizeof(su.sin),
		.sin.sin_family = AF_INET,
	};
	struct ip *ip;
	int iphlen;

	if (m->m_len < sizeof(*ip)) {
		m = m_pullup(m, sizeof(*ip));
		if (m == NULL)
			return (NULL);
	}

	ip = mtod(m, struct ip *);
	iphlen = ip->ip_hl << 2;
	if (iphlen < sizeof(*ip)) {
		/* this is a weird packet, decline */
		return (m);
	}

	su.sin.sin_addr = ip->ip_dst;

	return (veb_ipsec_proto_in(ifp0, m, iphlen, &su,
	    offsetof(struct ip, ip_p)));
}

#ifdef INET6
static struct mbuf *
veb_ipsec_ipv6_in(struct ifnet *ifp0, struct mbuf *m)
{
	union sockaddr_union su = {
		.sin6.sin6_len = sizeof(su.sin6),
		.sin6.sin6_family = AF_INET6,
	};
	struct ip6_hdr *ip6;

	if (m->m_len < sizeof(*ip6)) {
		m = m_pullup(m, sizeof(*ip6));
		if (m == NULL)
			return (NULL);
	}

	ip6 = mtod(m, struct ip6_hdr *);

	su.sin6.sin6_addr = ip6->ip6_dst;

	/* XXX scope? */

	return (veb_ipsec_proto_in(ifp0, m, sizeof(*ip6), &su,
	    offsetof(struct ip6_hdr, ip6_nxt)));
}
#endif /* INET6 */

static struct mbuf *
veb_ipsec_in(struct ifnet *ifp0, struct mbuf *m)
{
	struct mbuf *(*ipsec_ip_in)(struct ifnet *, struct mbuf *);
	struct ether_header *eh, copy;

	if (ifp0->if_enqueue == vport_enqueue)
		return (m);

	if (ISSET(m->m_flags, M_VLANTAG))
		return (m);

	eh = mtod(m, struct ether_header *);
	switch (ntohs(eh->ether_type)) {
	case ETHERTYPE_IP:
		ipsec_ip_in = veb_ipsec_ipv4_in;
		break;
#ifdef INET6
	case ETHERTYPE_IPV6:
		ipsec_ip_in = veb_ipsec_ipv6_in;
		break;
#endif /* INET6 */
	default:
		return (m);
	}

	copy = *eh;
	m_adj(m, sizeof(*eh));

	m = (*ipsec_ip_in)(ifp0, m);
	if (m == NULL)
		return (NULL);

	m = m_prepend(m, sizeof(*eh), M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	eh = mtod(m, struct ether_header *);
	*eh = copy;

	return (m);
}

static struct mbuf *
veb_ipsec_proto_out(struct mbuf *m, sa_family_t af, int iphlen)
{
	struct tdb *tdb;
	int error;
#if NPF > 0
	struct ifnet *encifp;
#endif

	tdb = ipsp_spd_lookup(m, af, iphlen, &error, IPSP_DIRECTION_OUT,
	    NULL, NULL, NULL);
	if (tdb == NULL)
		return (m);

#if NPF > 0
	encifp = enc_getif(tdb->tdb_rdomain, tdb->tdb_tap);
	if (encifp != NULL) {
		if (pf_test(af, PF_OUT, encifp, &m) != PF_PASS) {
			m_freem(m);
			return (NULL);
		}
		if (m == NULL)
			return (NULL);
	}
#endif /* NPF > 0 */

	/* XXX mtu checks */

	(void)ipsp_process_packet(m, tdb, af, 0);
	return (NULL);
}

static struct mbuf *
veb_ipsec_ipv4_out(struct mbuf *m)
{
	struct ip *ip;
	int iphlen;

	if (m->m_len < sizeof(*ip)) {
		m = m_pullup(m, sizeof(*ip));
		if (m == NULL)
			return (NULL);
	}

	ip = mtod(m, struct ip *);
	iphlen = ip->ip_hl << 2;
	if (iphlen < sizeof(*ip)) {
		/* this is a weird packet, decline */
		return (m);
	}

	return (veb_ipsec_proto_out(m, AF_INET, iphlen));
}

#ifdef INET6
static struct mbuf *
veb_ipsec_ipv6_out(struct mbuf *m)
{
	return (veb_ipsec_proto_out(m, AF_INET6, sizeof(struct ip6_hdr)));
}
#endif /* INET6 */

static struct mbuf *
veb_ipsec_out(struct ifnet *ifp0, struct mbuf *m)
{
	struct mbuf *(*ipsec_ip_out)(struct mbuf *);
	struct ether_header *eh, copy;

	if (ifp0->if_enqueue == vport_enqueue)
		return (m);

	if (ISSET(m->m_flags, M_VLANTAG))
		return (m);

	eh = mtod(m, struct ether_header *);
	switch (ntohs(eh->ether_type)) {
	case ETHERTYPE_IP:
		ipsec_ip_out = veb_ipsec_ipv4_out;
		break;
#ifdef INET6
	case ETHERTYPE_IPV6:
		ipsec_ip_out = veb_ipsec_ipv6_out;
		break;
#endif /* INET6 */
	default:
		return (m);
	}

	copy = *eh;
	m_adj(m, sizeof(*eh));

	m = (*ipsec_ip_out)(m);
	if (m == NULL)
		return (NULL);

	m = m_prepend(m, sizeof(*eh), M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	eh = mtod(m, struct ether_header *);
	*eh = copy;

	return (m);
}
#endif /* IPSEC */

static struct mbuf *
veb_offload(struct ifnet *ifp, struct ifnet *ifp0, struct mbuf *m)
{
	struct ether_extracted ext;
	int csum = 0;

#if NVLAN > 0
	if (ISSET(m->m_flags, M_VLANTAG) &&
	    !ISSET(ifp0->if_capabilities, IFCAP_VLAN_HWTAGGING)) {
		/*
		 * If the underlying interface has no VLAN hardware tagging
		 * support, inject one in software.
		 */
		m = vlan_inject(m, ETHERTYPE_VLAN, m->m_pkthdr.ether_vtag);
		if (m == NULL)
			goto err;
	}
#endif

	if (ISSET(m->m_pkthdr.csum_flags, M_IPV4_CSUM_OUT) &&
	    !ISSET(ifp0->if_capabilities, IFCAP_CSUM_IPv4))
		csum = 1;

	if (ISSET(m->m_pkthdr.csum_flags, M_TCP_CSUM_OUT) &&
	    (!ISSET(ifp0->if_capabilities, IFCAP_CSUM_TCPv4) ||
	     !ISSET(ifp0->if_capabilities, IFCAP_CSUM_TCPv6)))
		csum = 1;

	if (ISSET(m->m_pkthdr.csum_flags, M_UDP_CSUM_OUT) &&
	    (!ISSET(ifp0->if_capabilities, IFCAP_CSUM_UDPv4) ||
	     !ISSET(ifp0->if_capabilities, IFCAP_CSUM_UDPv6)))
		csum = 1;

	if (csum) {
		int ethlen;
		int hlen;

		ether_extract_headers(m, &ext);

		ethlen = sizeof *ext.eh;
		if (ext.evh)
			ethlen = sizeof *ext.evh;

		hlen = m->m_pkthdr.len - ext.paylen;

		if (m->m_len < hlen) {
			m = m_pullup(m, hlen);
			if (m == NULL)
				goto err;
		}

		/* hide ethernet header */
		m->m_data += ethlen;
		m->m_len -= ethlen;
		m->m_pkthdr.len -= ethlen;

		if (ext.ip4) {
			in_hdr_cksum_out(m, ifp0);
			in_proto_cksum_out(m, ifp0);
#ifdef INET6
		} else if (ext.ip6) {
			in6_proto_cksum_out(m, ifp0);
#endif
		}

		/* show ethernet header again */
		m->m_data -= ethlen;
		m->m_len += ethlen;
		m->m_pkthdr.len += ethlen;
	}

	return m;
 err:
	counters_inc(ifp->if_counters, ifc_ierrors);
	return NULL;
}

static void
veb_broadcast(struct veb_softc *sc, struct veb_port *rp, struct mbuf *m0,
    uint64_t src, uint64_t dst, struct netstack *ns)
{
	struct ifnet *ifp = &sc->sc_if;
	struct veb_ports *pm;
	struct veb_port **ps;
	struct veb_port *tp;
	struct ifnet *ifp0;
	struct mbuf *m;
	unsigned int i;

#if NPF > 0
	/*
	 * we couldn't find a specific port to send this packet to,
	 * but pf should still have a chance to apply policy to it.
	 * let pf look at it, but use the veb interface as a proxy.
	 */
	if (ISSET(ifp->if_flags, IFF_LINK1) &&
	    (m0 = veb_pf(ifp, PF_FWD, m0, ns)) == NULL)
		return;
#endif

#if 0 && defined(IPSEC)
	/* same goes for ipsec */
	if (ISSET(ifp->if_flags, IFF_LINK2) &&
	    (m0 = veb_ipsec_out(ifp, m0)) == NULL)
		return;
#endif

	counters_pkt(ifp->if_counters, ifc_opackets, ifc_obytes,
	    m0->m_pkthdr.len);

	smr_read_enter();
	pm = SMR_PTR_GET(&sc->sc_ports);
	if (__predict_true(pm != NULL))
		refcnt_take(&pm->m_refs);
	smr_read_leave();
	if (__predict_false(pm == NULL))
		goto done;

	ps = veb_ports_array(pm);
	for (i = 0; i < pm->m_count; i++) {
		tp = ps[i];

		if (rp == tp || (rp->p_protected & tp->p_protected)) {
			/*
			 * don't let Ethernet packets hairpin or
			 * move between ports in the same protected
			 * domain(s).
			 */
			continue;
		}

		ifp0 = tp->p_ifp0;
		if (!ISSET(ifp0->if_flags, IFF_RUNNING)) {
			/* don't waste time */
			continue;
		}

		if (!ISSET(tp->p_bif_flags, IFBIF_DISCOVER) &&
		    !ISSET(m0->m_flags, M_BCAST | M_MCAST)) {
			/* don't flood unknown unicast */
			continue;
		}

		if (veb_rule_filter(tp, VEB_RULE_LIST_OUT, m0, src, dst))
			continue;

		if ((m0 = veb_offload(ifp, ifp0, m0)) == NULL)
			goto rele;

		m = m_dup_pkt(m0, max_linkhdr + ETHER_ALIGN, M_NOWAIT);
		if (m == NULL) {
			/* XXX count error? */
			continue;
		}

		(*tp->p_enqueue)(ifp0, m); /* XXX count error */
	}
 rele:
	refcnt_rele_wake(&pm->m_refs);

done:
	m_freem(m0);
}

static struct mbuf *
veb_transmit(struct veb_softc *sc, struct veb_port *rp, struct veb_port *tp,
    struct mbuf *m, uint64_t src, uint64_t dst, struct netstack *ns)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;

	if (tp == NULL)
		return (m);

	if (rp == tp || (rp->p_protected & tp->p_protected)) {
		/*
		 * don't let Ethernet packets hairpin or move between
		 * ports in the same protected domain(s).
		 */
		goto drop;
	}

	if (veb_rule_filter(tp, VEB_RULE_LIST_OUT, m, src, dst))
		goto drop;

	ifp0 = tp->p_ifp0;

#if 0 && defined(IPSEC)
	if (ISSET(ifp->if_flags, IFF_LINK2) &&
	    (m = veb_ipsec_out(ifp0, m0)) == NULL)
		return;
#endif

#if NPF > 0
	if (ISSET(ifp->if_flags, IFF_LINK1) &&
	    (m = veb_pf(ifp0, PF_FWD, m, ns)) == NULL)
		return (NULL);
#endif

	counters_pkt(ifp->if_counters, ifc_opackets, ifc_obytes,
	    m->m_pkthdr.len);

	if ((m = veb_offload(ifp, ifp0, m)) == NULL)
		goto drop;

	(*tp->p_enqueue)(ifp0, m); /* XXX count error */

	return (NULL);
drop:
	m_freem(m);
	return (NULL);
}

static struct mbuf *
veb_vport_input(struct ifnet *ifp0, struct mbuf *m, uint64_t dst, void *brport,
    struct netstack *ns)
{
	return (m);
}

static struct mbuf *
veb_port_input(struct ifnet *ifp0, struct mbuf *m, uint64_t dst, void *brport,
    struct netstack *ns)
{
	struct veb_port *p = brport;
	struct veb_softc *sc = p->p_veb;
	struct ifnet *ifp = &sc->sc_if;
	struct ether_header *eh;
	uint64_t src;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return (m);

	eh = mtod(m, struct ether_header *);
	src = ether_addr_to_e64((struct ether_addr *)eh->ether_shost);

	/* Is this a MAC Bridge component Reserved address? */
	if (ETH64_IS_8021_RSVD(dst)) {
		if (!ISSET(ifp->if_flags, IFF_LINK0)) {
			/*
			 * letting vlans through implies this is
			 * an s-vlan component.
			 */
			goto drop;
		}

		 /* look at the last nibble of the 802.1 reserved address */
		switch (dst & 0xf) {
		case 0x0: /* Nearest Customer Bridge Group Address */
		case 0xb: /* EDE-SS PEP (IEEE Std 802.1AEcg) */
		case 0xc: /* reserved */
		case 0xd: /* Provider Bridge MVRP Address */
		case 0xf: /* reserved */
			break;
		default:
			goto drop;
		}
	}

	counters_pkt(ifp->if_counters, ifc_ipackets, ifc_ibytes,
	    m->m_pkthdr.len);

	/* force packets into the one routing domain for pf */
	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;

#if NBPFILTER > 0
	if_bpf = READ_ONCE(ifp->if_bpf);
	if (if_bpf != NULL) {
		if (bpf_mtap_ether(if_bpf, m, 0) != 0)
			goto drop;
	}
#endif

	veb_span(sc, m);

	if (ISSET(p->p_bif_flags, IFBIF_BLOCKNONIP) &&
	    veb_ip_filter(m))
		goto drop;

	if (!ISSET(ifp->if_flags, IFF_LINK0) &&
	    veb_vlan_filter(m))
		goto drop;

	if (veb_rule_filter(p, VEB_RULE_LIST_IN, m, src, dst))
		goto drop;

#if NPF > 0
	if (ISSET(ifp->if_flags, IFF_LINK1) &&
	    (m = veb_pf(ifp0, PF_IN, m, ns)) == NULL)
		return (NULL);
#endif

#if 0 && defined(IPSEC)
	if (ISSET(ifp->if_flags, IFF_LINK2) &&
	    (m = veb_ipsec_in(ifp0, m)) == NULL)
		return (NULL);
#endif

	eh = mtod(m, struct ether_header *);

	if (ISSET(p->p_bif_flags, IFBIF_LEARNING))
		etherbridge_map(&sc->sc_eb, p, src);

	CLR(m->m_flags, M_BCAST|M_MCAST);

	if (!ETH64_IS_MULTICAST(dst)) {
		struct veb_port *tp = NULL;

		smr_read_enter();
		tp = etherbridge_resolve(&sc->sc_eb, dst);
		if (tp != NULL)
			veb_eb_port_take(NULL, tp);
		smr_read_leave();
		if (tp != NULL) {
			m = veb_transmit(sc, p, tp, m, src, dst, ns);
			veb_eb_port_rele(NULL, tp);
		}

		if (m == NULL)
			return (NULL);

		/* unknown unicast address */
	} else {
		SET(m->m_flags, ETH64_IS_BROADCAST(dst) ? M_BCAST : M_MCAST);
	}

	veb_broadcast(sc, p, m, src, dst, ns);
	return (NULL);

drop:
	m_freem(m);
	return (NULL);
}

static void
veb_input(struct ifnet *ifp, struct mbuf *m, struct netstack *ns)
{
	m_freem(m);
}

static int
veb_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	m_freem(m);
	return (ENODEV);
}

static int
veb_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	m_freem(m);
	return (ENODEV);
}

static void
veb_start(struct ifqueue *ifq)
{
	ifq_purge(ifq);
}

static int
veb_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct veb_softc *sc = ifp->if_softc;
	struct ifbrparam *bparam = (struct ifbrparam *)data;
	int error = 0;

	if (sc->sc_dead)
		return (ENXIO);

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = veb_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = veb_down(sc);
		}
		break;

	case SIOCBRDGADD:
		error = suser(curproc);
		if (error != 0)
			break;

		error = veb_add_port(sc, (struct ifbreq *)data, 0);
		break;
	case SIOCBRDGADDS:
		error = suser(curproc);
		if (error != 0)
			break;

		error = veb_add_port(sc, (struct ifbreq *)data, 1);
		break;
	case SIOCBRDGDEL:
		error = suser(curproc);
		if (error != 0)
			break;

		error = veb_del_port(sc, (struct ifbreq *)data, 0);
		break;
	case SIOCBRDGDELS:
		error = suser(curproc);
		if (error != 0)
			break;

		error = veb_del_port(sc, (struct ifbreq *)data, 1);
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
		error = etherbridge_rtfind(&sc->sc_eb, (struct ifbaconf *)data);
		break;
	case SIOCBRDGIFS:
		error = veb_port_list(sc, (struct ifbifconf *)data);
		break;
	case SIOCBRDGFLUSH:
		etherbridge_flush(&sc->sc_eb,
		    ((struct ifbreq *)data)->ifbr_ifsflags);
		break;
	case SIOCBRDGSADDR:
		error = veb_add_addr(sc, (struct ifbareq *)data);
		break;
	case SIOCBRDGDADDR:
		error = veb_del_addr(sc, (struct ifbareq *)data);
		break;

	case SIOCBRDGSIFPROT:
		error = veb_port_set_protected(sc, (struct ifbreq *)data);
		break;

	case SIOCBRDGSIFFLGS:
		error = veb_port_set_flags(sc, (struct ifbreq *)data);
		break;
	case SIOCBRDGGIFFLGS:
		error = veb_port_get_flags(sc, (struct ifbreq *)data);
		break;

	case SIOCBRDGARL:
		error = veb_rule_add(sc, (struct ifbrlreq *)data);
		break;
	case SIOCBRDGFRL:
		error = veb_rule_list_flush(sc, (struct ifbrlreq *)data);
		break;
	case SIOCBRDGGRL:
		error = veb_rule_list_get(sc, (struct ifbrlconf *)data);
		break;

	default:
		error = ENOTTY;
		break;
	}

	if (error == ENETRESET)
		error = veb_iff(sc);

	return (error);
}

static struct veb_ports *
veb_ports_insert(struct veb_ports *om, struct veb_port *p)
{
	struct veb_ports *nm;
	struct veb_port **nps, **ops;
	unsigned int ocount = om != NULL ? om->m_count : 0;
	unsigned int ncount = ocount + 1;
	unsigned int i;

	nm = malloc(veb_ports_size(ncount), M_DEVBUF, M_WAITOK|M_ZERO);

	refcnt_init(&nm->m_refs);
	nm->m_count = ncount;

	nps = veb_ports_array(nm);

	if (om != NULL) {
		ops = veb_ports_array(om);
		for (i = 0; i < ocount; i++) {
			struct veb_port *op = ops[i];
			refcnt_take(&op->p_refs);
			nps[i] = op;
		}
	} else
		i = 0;

	refcnt_take(&p->p_refs);
	nps[i] = p;

	return (nm);
}

static struct veb_ports *
veb_ports_remove(struct veb_ports *om, struct veb_port *p)
{
	struct veb_ports *nm;
	struct veb_port **nps, **ops;
	unsigned int ocount = om->m_count;
	unsigned int ncount = ocount - 1;
	unsigned int i, j;

	if (ncount == 0)
		return (NULL);

	nm = malloc(veb_ports_size(ncount), M_DEVBUF, M_WAITOK|M_ZERO);

	refcnt_init(&nm->m_refs);
	nm->m_count = ncount;

	nps = veb_ports_array(nm);
	j = 0;

	ops = veb_ports_array(om);
	for (i = 0; i < ocount; i++) {
		struct veb_port *op = ops[i];
		if (op == p)
			continue;

		refcnt_take(&op->p_refs);
		nps[j++] = op;
	}
	KASSERT(j == ncount);

	return (nm);
}

static inline void
veb_ports_free(struct veb_ports *m)
{
	free(m, M_DEVBUF, veb_ports_size(m->m_count));
}

static void
veb_ports_destroy(struct veb_ports *m)
{
	struct veb_port **ps = veb_ports_array(m);
	unsigned int i;

	for (i = 0; i < m->m_count; i++) {
		struct veb_port *p = ps[i];
		refcnt_rele_wake(&p->p_refs);
	}

	veb_ports_free(m);
}

static int
veb_add_port(struct veb_softc *sc, const struct ifbreq *req, unsigned int span)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	struct veb_ports **ports_ptr;
	struct veb_ports *om, *nm;
	struct veb_port *p;
	int isvport;
	int error;

	NET_ASSERT_LOCKED();

	ifp0 = if_unit(req->ifbr_ifsname);
	if (ifp0 == NULL)
		return (EINVAL);

	if (ifp0->if_type != IFT_ETHER) {
		error = EPROTONOSUPPORT;
		goto put;
	}

	if (ifp0 == ifp) {
		error = EPROTONOSUPPORT;
		goto put;
	}

	isvport = (ifp0->if_enqueue == vport_enqueue);

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
	p->p_veb = sc;

	refcnt_init(&p->p_refs);
	TAILQ_INIT(&p->p_vrl);
	SMR_TAILQ_INIT(&p->p_vr_list[0]);
	SMR_TAILQ_INIT(&p->p_vr_list[1]);

	p->p_enqueue = isvport ? vport_if_enqueue : if_enqueue;
	p->p_ioctl = ifp0->if_ioctl;
	p->p_output = ifp0->if_output;

	if (span) {
		ports_ptr = &sc->sc_spans;

		if (isvport) {
			error = EPROTONOSUPPORT;
			goto free;
		}

		p->p_brport.eb_input = veb_span_input;
		p->p_bif_flags = IFBIF_SPAN;
	} else {
		ports_ptr = &sc->sc_ports;

		error = ifpromisc(ifp0, 1);
		if (error != 0)
			goto free;

		p->p_bif_flags = IFBIF_LEARNING | IFBIF_DISCOVER;
		p->p_brport.eb_input = isvport ?
		    veb_vport_input : veb_port_input;
	}

	p->p_brport.eb_port_take = veb_eb_brport_take;
	p->p_brport.eb_port_rele = veb_eb_brport_rele;

	om = SMR_PTR_GET_LOCKED(ports_ptr);
	nm = veb_ports_insert(om, p);

	/* this might have changed if we slept for malloc or ifpromisc */
	error = ether_brport_isset(ifp0);
	if (error != 0)
		goto unpromisc;

	task_set(&p->p_ltask, veb_p_linkch, p);
	if_linkstatehook_add(ifp0, &p->p_ltask);

	task_set(&p->p_dtask, veb_p_detach, p);
	if_detachhook_add(ifp0, &p->p_dtask);

	p->p_brport.eb_port = p;

	/* commit */
	SMR_PTR_SET_LOCKED(ports_ptr, nm);

	ether_brport_set(ifp0, &p->p_brport);
	if (!isvport) { /* vport is special */
		ifp0->if_ioctl = veb_p_ioctl;
		ifp0->if_output = veb_p_output;
	}

	veb_p_linkch(p);

	/* clean up the old veb_ports map */
	smr_barrier();
	if (om != NULL) {
		refcnt_finalize(&om->m_refs, "vebports");
		veb_ports_destroy(om);
	}

	return (0);

unpromisc:
	if (!span)
		ifpromisc(ifp0, 0);
free:
	free(p, M_DEVBUF, sizeof(*p));
put:
	if_put(ifp0);
	return (error);
}

static struct veb_port *
veb_trunkport(struct veb_softc *sc, const char *name, unsigned int span)
{
	struct veb_ports *m;
	struct veb_port **ps;
	struct veb_port *p;
	unsigned int i;

	m = SMR_PTR_GET_LOCKED(span ? &sc->sc_spans : &sc->sc_ports);
	if (m == NULL)
		return (NULL);

	ps = veb_ports_array(m);
	for (i = 0; i < m->m_count; i++) {
		p = ps[i];

		if (strncmp(p->p_ifp0->if_xname, name, IFNAMSIZ) == 0)
			return (p);
	}

	return (NULL);
}

static int
veb_del_port(struct veb_softc *sc, const struct ifbreq *req, unsigned int span)
{
	struct veb_port *p;

	NET_ASSERT_LOCKED();
	p = veb_trunkport(sc, req->ifbr_ifsname, span);
	if (p == NULL)
		return (EINVAL);

	veb_p_dtor(sc, p);

	return (0);
}

static struct veb_port *
veb_port_get(struct veb_softc *sc, const char *name)
{
	struct veb_ports *m;
	struct veb_port **ps;
	struct veb_port *p;
	unsigned int i;

	NET_ASSERT_LOCKED();

	m = SMR_PTR_GET_LOCKED(&sc->sc_ports);
	if (m == NULL)
		return (NULL);

	ps = veb_ports_array(m);
	for (i = 0; i < m->m_count; i++) {
		p = ps[i];

		if (strncmp(p->p_ifp0->if_xname, name, IFNAMSIZ) == 0) {
			refcnt_take(&p->p_refs);
			return (p);
		}
	}

	return (NULL);
}

static void
veb_port_put(struct veb_softc *sc, struct veb_port *p)
{
	refcnt_rele_wake(&p->p_refs);
}

static int
veb_port_set_protected(struct veb_softc *sc, const struct ifbreq *ifbr)
{
	struct veb_port *p;

	p = veb_port_get(sc, ifbr->ifbr_ifsname);
	if (p == NULL)
		return (ESRCH);

	p->p_protected = ifbr->ifbr_protected;
	veb_port_put(sc, p);

	return (0);
}

static int
veb_rule_add(struct veb_softc *sc, const struct ifbrlreq *ifbr)
{
	const struct ifbrarpf *brla = &ifbr->ifbr_arpf;
	struct veb_rule vr, *vrp;
	struct veb_port *p;
	int error;

	memset(&vr, 0, sizeof(vr));

	switch (ifbr->ifbr_action) {
	case BRL_ACTION_BLOCK:
		vr.vr_action = VEB_R_BLOCK;
		break;
	case BRL_ACTION_PASS:
		vr.vr_action = VEB_R_PASS;
		break;
	/* XXX VEB_R_MATCH */
	default:
		return (EINVAL);
	}

	if (!ISSET(ifbr->ifbr_flags, BRL_FLAG_IN|BRL_FLAG_OUT))
		return (EINVAL);
	if (ISSET(ifbr->ifbr_flags, BRL_FLAG_IN))
		SET(vr.vr_flags, VEB_R_F_IN);
	if (ISSET(ifbr->ifbr_flags, BRL_FLAG_OUT))
		SET(vr.vr_flags, VEB_R_F_OUT);

	if (ISSET(ifbr->ifbr_flags, BRL_FLAG_SRCVALID)) {
		SET(vr.vr_flags, VEB_R_F_SRC);
		vr.vr_src = ether_addr_to_e64(&ifbr->ifbr_src);
	}
	if (ISSET(ifbr->ifbr_flags, BRL_FLAG_DSTVALID)) {
		SET(vr.vr_flags, VEB_R_F_DST);
		vr.vr_dst = ether_addr_to_e64(&ifbr->ifbr_dst);
	}

	/* ARP rule */
	if (ISSET(brla->brla_flags, BRLA_ARP|BRLA_RARP)) {
		if (ISSET(brla->brla_flags, BRLA_ARP))
			SET(vr.vr_flags, VEB_R_F_ARP);
		if (ISSET(brla->brla_flags, BRLA_RARP))
			SET(vr.vr_flags, VEB_R_F_RARP);

		if (ISSET(brla->brla_flags, BRLA_SHA)) {
			SET(vr.vr_flags, VEB_R_F_SHA);
			vr.vr_arp_sha = brla->brla_sha;
		}
		if (ISSET(brla->brla_flags, BRLA_THA)) {
			SET(vr.vr_flags, VEB_R_F_THA);
			vr.vr_arp_tha = brla->brla_tha;
		}
		if (ISSET(brla->brla_flags, BRLA_SPA)) {
			SET(vr.vr_flags, VEB_R_F_SPA);
			vr.vr_arp_spa = brla->brla_spa;
		}
		if (ISSET(brla->brla_flags, BRLA_TPA)) {
			SET(vr.vr_flags, VEB_R_F_TPA);
			vr.vr_arp_tpa = brla->brla_tpa;
		}
		vr.vr_arp_op = htons(brla->brla_op);
	}

	if (ifbr->ifbr_tagname[0] != '\0') {
#if NPF > 0
		vr.vr_pftag = pf_tagname2tag((char *)ifbr->ifbr_tagname, 1);
		if (vr.vr_pftag == 0)
			return (ENOMEM);
#else
		return (EINVAL);
#endif
	}

	p = veb_port_get(sc, ifbr->ifbr_ifsname);
	if (p == NULL) {
		error = ESRCH;
		goto error;
	}

	vrp = pool_get(&veb_rule_pool, PR_WAITOK|PR_LIMITFAIL|PR_ZERO);
	if (vrp == NULL) {
		error = ENOMEM;
		goto port_put;
	}

	*vrp = vr;

	/* there's one big lock on a veb for all ports */
	error = rw_enter(&sc->sc_rule_lock, RW_WRITE|RW_INTR);
	if (error != 0)
		goto rule_put;

	TAILQ_INSERT_TAIL(&p->p_vrl, vrp, vr_entry);
	p->p_nvrl++;
	if (ISSET(vr.vr_flags, VEB_R_F_OUT)) {
		SMR_TAILQ_INSERT_TAIL_LOCKED(&p->p_vr_list[0],
		    vrp, vr_lentry[0]);
	}
	if (ISSET(vr.vr_flags, VEB_R_F_IN)) {
		SMR_TAILQ_INSERT_TAIL_LOCKED(&p->p_vr_list[1],
		    vrp, vr_lentry[1]);
	}

	rw_exit(&sc->sc_rule_lock);
	veb_port_put(sc, p);

	return (0);

rule_put:
	pool_put(&veb_rule_pool, vrp);
port_put:
	veb_port_put(sc, p);
error:
#if NPF > 0
	pf_tag_unref(vr.vr_pftag);
#endif
	return (error);
}

static void
veb_rule_list_free(struct veb_rule *nvr)
{
	struct veb_rule *vr;

	while ((vr = nvr) != NULL) {
		nvr = TAILQ_NEXT(vr, vr_entry);
		pool_put(&veb_rule_pool, vr);
	}
}

static int
veb_rule_list_flush(struct veb_softc *sc, const struct ifbrlreq *ifbr)
{
	struct veb_port *p;
	struct veb_rule *vr;
	int error;

	p = veb_port_get(sc, ifbr->ifbr_ifsname);
	if (p == NULL)
		return (ESRCH);

	error = rw_enter(&sc->sc_rule_lock, RW_WRITE|RW_INTR);
	if (error != 0) {
		veb_port_put(sc, p);
		return (error);
	}

	/* take all the rules away */
	vr = TAILQ_FIRST(&p->p_vrl);

	/* reset the lists and counts of rules */
	TAILQ_INIT(&p->p_vrl);
	p->p_nvrl = 0;
	SMR_TAILQ_INIT(&p->p_vr_list[0]);
	SMR_TAILQ_INIT(&p->p_vr_list[1]);

	rw_exit(&sc->sc_rule_lock);
	veb_port_put(sc, p);

	smr_barrier();
	veb_rule_list_free(vr);

	return (0);
}

static void
veb_rule2ifbr(struct ifbrlreq *ifbr, const struct veb_rule *vr)
{
	switch (vr->vr_action) {
	case VEB_R_PASS:
		ifbr->ifbr_action = BRL_ACTION_PASS;
		break;
	case VEB_R_BLOCK:
		ifbr->ifbr_action = BRL_ACTION_BLOCK;
		break;
	}

	if (ISSET(vr->vr_flags, VEB_R_F_IN))
		SET(ifbr->ifbr_flags, BRL_FLAG_IN);
	if (ISSET(vr->vr_flags, VEB_R_F_OUT))
		SET(ifbr->ifbr_flags, BRL_FLAG_OUT);

	if (ISSET(vr->vr_flags, VEB_R_F_SRC)) {
		SET(ifbr->ifbr_flags, BRL_FLAG_SRCVALID);
		ether_e64_to_addr(&ifbr->ifbr_src, vr->vr_src);
	}
	if (ISSET(vr->vr_flags, VEB_R_F_DST)) {
		SET(ifbr->ifbr_flags, BRL_FLAG_DSTVALID);
		ether_e64_to_addr(&ifbr->ifbr_dst, vr->vr_dst);
	}

	/* ARP rule */
	if (ISSET(vr->vr_flags, VEB_R_F_ARP|VEB_R_F_RARP)) {
		struct ifbrarpf *brla = &ifbr->ifbr_arpf;

		if (ISSET(vr->vr_flags, VEB_R_F_ARP))
			SET(brla->brla_flags, BRLA_ARP);
		if (ISSET(vr->vr_flags, VEB_R_F_RARP))
			SET(brla->brla_flags, BRLA_RARP);

		if (ISSET(vr->vr_flags, VEB_R_F_SHA)) {
			SET(brla->brla_flags, BRLA_SHA);
			brla->brla_sha = vr->vr_arp_sha;
		}
		if (ISSET(vr->vr_flags, VEB_R_F_THA)) {
			SET(brla->brla_flags, BRLA_THA);
			brla->brla_tha = vr->vr_arp_tha;
		}

		if (ISSET(vr->vr_flags, VEB_R_F_SPA)) {
			SET(brla->brla_flags, BRLA_SPA);
			brla->brla_spa = vr->vr_arp_spa;
		}
		if (ISSET(vr->vr_flags, VEB_R_F_TPA)) {
			SET(brla->brla_flags, BRLA_TPA);
			brla->brla_tpa = vr->vr_arp_tpa;
		}

		brla->brla_op = ntohs(vr->vr_arp_op);
	}

#if NPF > 0
	if (vr->vr_pftag != 0)
		pf_tag2tagname(vr->vr_pftag, ifbr->ifbr_tagname);
#endif
}

static int
veb_rule_list_get(struct veb_softc *sc, struct ifbrlconf *ifbrl)
{
	struct veb_port *p;
	struct veb_rule *vr;
	struct ifbrlreq *ifbr, *ifbrs;
	int error = 0;
	size_t len;

	p = veb_port_get(sc, ifbrl->ifbrl_ifsname);
	if (p == NULL)
		return (ESRCH);

	len = p->p_nvrl; /* estimate */
	if (ifbrl->ifbrl_len == 0 || len == 0) {
		ifbrl->ifbrl_len = len * sizeof(*ifbrs);
		goto port_put;
	}

	error = rw_enter(&sc->sc_rule_lock, RW_READ|RW_INTR);
	if (error != 0)
		goto port_put;

	ifbrs = mallocarray(p->p_nvrl, sizeof(*ifbrs), M_TEMP,
	    M_WAITOK|M_CANFAIL|M_ZERO);
	if (ifbrs == NULL) {
		rw_exit(&sc->sc_rule_lock);
		goto port_put;
	}
	len = p->p_nvrl * sizeof(*ifbrs);

	ifbr = ifbrs;
	TAILQ_FOREACH(vr, &p->p_vrl, vr_entry) {
		strlcpy(ifbr->ifbr_name, sc->sc_if.if_xname, IFNAMSIZ);
		strlcpy(ifbr->ifbr_ifsname, p->p_ifp0->if_xname, IFNAMSIZ);
		veb_rule2ifbr(ifbr, vr);

		ifbr++;
	}

	rw_exit(&sc->sc_rule_lock);

	error = copyout(ifbrs, ifbrl->ifbrl_buf, min(len, ifbrl->ifbrl_len));
	if (error == 0)
		ifbrl->ifbrl_len = len;
	free(ifbrs, M_TEMP, len);

port_put:
	veb_port_put(sc, p);
	return (error);
}

static int
veb_port_list(struct veb_softc *sc, struct ifbifconf *bifc)
{
	struct ifnet *ifp = &sc->sc_if;
	struct veb_ports *m;
	struct veb_port **ps;
	struct veb_port *p;
	struct ifnet *ifp0;
	struct ifbreq breq;
	int n = 0, error = 0;
	unsigned int i;

	NET_ASSERT_LOCKED();

	if (bifc->ifbic_len == 0) {
		m = SMR_PTR_GET_LOCKED(&sc->sc_ports);
		if (m != NULL)
			n += m->m_count;
		m = SMR_PTR_GET_LOCKED(&sc->sc_spans);
		if (m != NULL)
			n += m->m_count;
		goto done;
	}

	m = SMR_PTR_GET_LOCKED(&sc->sc_ports);
	if (m != NULL) {
		ps = veb_ports_array(m);
		for (i = 0; i < m->m_count; i++) {
			if (bifc->ifbic_len < sizeof(breq))
				break;

			p = ps[i];

			memset(&breq, 0, sizeof(breq));

			ifp0 = p->p_ifp0;

			strlcpy(breq.ifbr_name, ifp->if_xname, IFNAMSIZ);
			strlcpy(breq.ifbr_ifsname, ifp0->if_xname, IFNAMSIZ);

			breq.ifbr_ifsflags = p->p_bif_flags;
			breq.ifbr_portno = ifp0->if_index;
			breq.ifbr_protected = p->p_protected;
			if ((error = copyout(&breq, bifc->ifbic_req + n,
			    sizeof(breq))) != 0)
				goto done;

			bifc->ifbic_len -= sizeof(breq);
			n++;
		}
	}

	m = SMR_PTR_GET_LOCKED(&sc->sc_spans);
	if (m != NULL) {
		ps = veb_ports_array(m);
		for (i = 0; i < m->m_count; i++) {
			if (bifc->ifbic_len < sizeof(breq))
				break;

			p = ps[i];

			memset(&breq, 0, sizeof(breq));

			strlcpy(breq.ifbr_name, ifp->if_xname, IFNAMSIZ);
			strlcpy(breq.ifbr_ifsname, p->p_ifp0->if_xname,
			    IFNAMSIZ);

			breq.ifbr_ifsflags = p->p_bif_flags;
			if ((error = copyout(&breq, bifc->ifbic_req + n,
			    sizeof(breq))) != 0)
				goto done;

			bifc->ifbic_len -= sizeof(breq);
			n++;
		}
	}

done:
	bifc->ifbic_len = n * sizeof(breq);
	return (error);
}

static int
veb_port_set_flags(struct veb_softc *sc, struct ifbreq *ifbr)
{
	struct veb_port *p;

	if (ISSET(ifbr->ifbr_ifsflags, ~VEB_IFBIF_FLAGS))
		return (EINVAL);

	p = veb_port_get(sc, ifbr->ifbr_ifsname);
	if (p == NULL)
		return (ESRCH);

	p->p_bif_flags = ifbr->ifbr_ifsflags;

	veb_port_put(sc, p);
	return (0);
}

static int
veb_port_get_flags(struct veb_softc *sc, struct ifbreq *ifbr)
{
	struct veb_port *p;

	p = veb_port_get(sc, ifbr->ifbr_ifsname);
	if (p == NULL)
		return (ESRCH);

	ifbr->ifbr_ifsflags = p->p_bif_flags;
	ifbr->ifbr_portno = p->p_ifp0->if_index;
	ifbr->ifbr_protected = p->p_protected;

	veb_port_put(sc, p);
	return (0);
}

static int
veb_add_addr(struct veb_softc *sc, const struct ifbareq *ifba)
{
	struct veb_port *p;
	int error = 0;
	unsigned int type;

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

	if (ifba->ifba_dstsa.ss_family != AF_UNSPEC)
		return (EAFNOSUPPORT);

	p = veb_port_get(sc, ifba->ifba_ifsname);
	if (p == NULL)
		return (ESRCH);

	error = etherbridge_add_addr(&sc->sc_eb, p, &ifba->ifba_dst, type);

	veb_port_put(sc, p);

	return (error);
}

static int
veb_del_addr(struct veb_softc *sc, const struct ifbareq *ifba)
{
	return (etherbridge_del_addr(&sc->sc_eb, &ifba->ifba_dst));
}

static int
veb_p_ioctl(struct ifnet *ifp0, u_long cmd, caddr_t data)
{
	const struct ether_brport *eb = ether_brport_get_locked(ifp0);
	struct veb_port *p;
	int error = 0;

	KASSERTMSG(eb != NULL,
	    "%s: %s called without an ether_brport set",
	    ifp0->if_xname, __func__);
	KASSERTMSG((eb->eb_input == veb_port_input) ||
	    (eb->eb_input == veb_span_input),
	    "%s called %s, but eb_input (%p) seems wrong",
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
veb_p_output(struct ifnet *ifp0, struct mbuf *m, struct sockaddr *dst,
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
	if (eb != NULL && eb->eb_input == veb_port_input) {
		struct veb_port *p = eb->eb_port;
		p_output = p->p_output; /* code doesn't go away */
	}
	smr_read_leave();

	if (p_output == NULL) {
		m_freem(m);
		return (ENXIO);
	}

	return ((*p_output)(ifp0, m, dst, rt));
}

/*
 * there must be an smr_barrier after ether_brport_clr() and before
 * veb_port is freed in veb_p_fini()
 */

static void
veb_p_unlink(struct veb_softc *sc, struct veb_port *p)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0 = p->p_ifp0;

	ifp0->if_ioctl = p->p_ioctl;
	ifp0->if_output = p->p_output;

	ether_brport_clr(ifp0); /* needs an smr_barrier */

	if_detachhook_del(ifp0, &p->p_dtask);
	if_linkstatehook_del(ifp0, &p->p_ltask);

	if (!ISSET(p->p_bif_flags, IFBIF_SPAN)) {
		if (ifpromisc(ifp0, 0) != 0) {
			log(LOG_WARNING, "%s %s: unable to disable promisc\n",
			    ifp->if_xname, ifp0->if_xname);
		}

		etherbridge_detach_port(&sc->sc_eb, p);
	}
}

static void
veb_p_fini(struct veb_port *p)
{
	struct ifnet *ifp0 = p->p_ifp0;

	refcnt_finalize(&p->p_refs, "vebpdtor");
	veb_rule_list_free(TAILQ_FIRST(&p->p_vrl));

	if_put(ifp0);
	free(p, M_DEVBUF, sizeof(*p)); /* hope you didn't forget smr_barrier */
}

static void
veb_p_dtor(struct veb_softc *sc, struct veb_port *p)
{
	struct veb_ports **ports_ptr;
	struct veb_ports *om, *nm;

	ports_ptr = ISSET(p->p_bif_flags, IFBIF_SPAN) ?
	    &sc->sc_spans : &sc->sc_ports;

	om = SMR_PTR_GET_LOCKED(ports_ptr);
	nm = veb_ports_remove(om, p);
	SMR_PTR_SET_LOCKED(ports_ptr, nm);

	veb_p_unlink(sc, p);

	smr_barrier();
	refcnt_finalize(&om->m_refs, "vebports");
	veb_ports_destroy(om);

	veb_p_fini(p);
}

static void
veb_p_detach(void *arg)
{
	struct veb_port *p = arg;
	struct veb_softc *sc = p->p_veb;

	NET_ASSERT_LOCKED();

	veb_p_dtor(sc, p);
}

static int
veb_p_active(struct veb_port *p)
{
	struct ifnet *ifp0 = p->p_ifp0;

	return (ISSET(ifp0->if_flags, IFF_RUNNING) &&
	    LINK_STATE_IS_UP(ifp0->if_link_state));
}

static void
veb_p_linkch(void *arg)
{
	struct veb_port *p = arg;
	u_char link_state = LINK_STATE_FULL_DUPLEX;

	NET_ASSERT_LOCKED();

	if (!veb_p_active(p))
		link_state = LINK_STATE_DOWN;

	p->p_link_state = link_state;
}

static int
veb_up(struct veb_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	int error;

	error = etherbridge_up(&sc->sc_eb);
	if (error != 0)
		return (error);

	NET_ASSERT_LOCKED();
	SET(ifp->if_flags, IFF_RUNNING);

	return (0);
}

static int
veb_iff(struct veb_softc *sc)
{
	return (0);
}

static int
veb_down(struct veb_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	int error;

	error = etherbridge_down(&sc->sc_eb);
	if (error != 0)
		return (0);

	NET_ASSERT_LOCKED();
	CLR(ifp->if_flags, IFF_RUNNING);

	return (0);
}

static int
veb_eb_port_cmp(void *arg, void *a, void *b)
{
	struct veb_port *pa = a, *pb = b;
	return (pa == pb);
}

static void *
veb_eb_port_take(void *arg, void *port)
{
	struct veb_port *p = port;

	refcnt_take(&p->p_refs);

	return (p);
}

static void
veb_eb_port_rele(void *arg, void *port)
{
	struct veb_port *p = port;

	refcnt_rele_wake(&p->p_refs);
}

static void
veb_eb_brport_take(void *port)
{
	veb_eb_port_take(NULL, port);
}

static void
veb_eb_brport_rele(void *port)
{
	veb_eb_port_rele(NULL, port);
}

static size_t
veb_eb_port_ifname(void *arg, char *dst, size_t len, void *port)
{
	struct veb_port *p = port;

	return (strlcpy(dst, p->p_ifp0->if_xname, len));
}

static void
veb_eb_port_sa(void *arg, struct sockaddr_storage *ss, void *port)
{
	ss->ss_family = AF_UNSPEC;
}

/*
 * virtual ethernet bridge port
 */

static int
vport_clone_create(struct if_clone *ifc, int unit)
{
	struct vport_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO|M_CANFAIL);
	if (sc == NULL)
		return (ENOMEM);

	ifp = &sc->sc_ac.ac_if;

	snprintf(ifp->if_xname, IFNAMSIZ, "%s%d", ifc->ifc_name, unit);

	ifp->if_softc = sc;
	ifp->if_type = IFT_ETHER;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_ioctl = vport_ioctl;
	ifp->if_enqueue = vport_enqueue;
	ifp->if_qstart = vport_start;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;

	ifp->if_capabilities = 0;
#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif
	ifp->if_capabilities |= IFCAP_CSUM_IPv4;
	ifp->if_capabilities |= IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4;
	ifp->if_capabilities |= IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;

	ether_fakeaddr(ifp);

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

	return (0);
}

static int
vport_clone_destroy(struct ifnet *ifp)
{
	struct vport_softc *sc = ifp->if_softc;

	NET_LOCK();
	sc->sc_dead = 1;

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		vport_down(sc);
	NET_UNLOCK();

	ether_ifdetach(ifp);
	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
vport_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct vport_softc *sc = ifp->if_softc;
	int error = 0;

	if (sc->sc_dead)
		return (ENXIO);

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = vport_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = vport_down(sc);
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET)
		error = vport_iff(sc);

	return (error);
}

static int
vport_up(struct vport_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	NET_ASSERT_LOCKED();
	SET(ifp->if_flags, IFF_RUNNING);

	return (0);
}

static int
vport_iff(struct vport_softc *sc)
{
	return (0);
}

static int
vport_down(struct vport_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	NET_ASSERT_LOCKED();
	CLR(ifp->if_flags, IFF_RUNNING);

	return (0);
}

static int
vport_if_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	/*
	 * switching an l2 packet toward a vport means pushing it
	 * into the network stack. this function exists to make
	 * if_vinput compat with veb calling if_enqueue.
	 */

	if_vinput(ifp, m, NULL);

	return (0);
}

static int
vport_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	struct arpcom *ac;
	const struct ether_brport *eb;
	int error = ENETDOWN;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	/*
	 * a packet sent from the l3 stack out a vport goes into
	 * veb for switching out another port.
	 */

#if NPF > 0
	/*
	 * there's no relationship between pf states in the l3 stack
	 * and the l2 bridge.
	 */
	pf_pkt_addr_changed(m);
#endif

	ac = (struct arpcom *)ifp;

	smr_read_enter();
	eb = SMR_PTR_GET(&ac->ac_brport);
	if (eb != NULL)
		eb->eb_port_take(eb->eb_port);
	smr_read_leave();
	if (eb != NULL) {
		struct mbuf *(*input)(struct ifnet *, struct mbuf *,
		    uint64_t, void *, struct netstack *) = eb->eb_input;
		struct ether_header *eh;
		uint64_t dst;

		counters_pkt(ifp->if_counters, ifc_opackets, ifc_obytes,
		    m->m_pkthdr.len);

#if NBPFILTER > 0
		if_bpf = READ_ONCE(ifp->if_bpf);
		if (if_bpf != NULL)
			bpf_mtap_ether(if_bpf, m, BPF_DIRECTION_OUT);
#endif

		eh = mtod(m, struct ether_header *);
		dst = ether_addr_to_e64((struct ether_addr *)eh->ether_dhost);

		if (input == veb_vport_input)
			input = veb_port_input;
		m = (*input)(ifp, m, dst, eb->eb_port, NULL);

		error = 0;

		eb->eb_port_rele(eb->eb_port);
	}

	m_freem(m);

	return (error);
}

static void
vport_start(struct ifqueue *ifq)
{
	ifq_purge(ifq);
}

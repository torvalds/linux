/*	$OpenBSD: if_trunk.c,v 1.30 2007/01/31 06:20:19 reyk Exp $	*/

/*
 * Copyright (c) 2005, 2006 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2007 Andrew Thompson <thompsa@FreeBSD.org>
 * Copyright (c) 2014, 2016 Marcelo Araujo <araujo@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ratelimit.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/rmlock.h>
#include <sys/sx.h>
#include <sys/taskqueue.h>
#include <sys/eventhandler.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_clone.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/bpf.h>
#include <net/vnet.h>

#if defined(INET) || defined(INET6)
#include <netinet/in.h>
#include <netinet/ip.h>
#endif
#ifdef INET
#include <netinet/in_systm.h>
#include <netinet/if_ether.h>
#endif

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/in6_ifattach.h>
#endif

#include <net/if_vlan_var.h>
#include <net/if_lagg.h>
#include <net/ieee8023ad_lacp.h>

#define	LAGG_RLOCK()	struct epoch_tracker lagg_et; epoch_enter_preempt(net_epoch_preempt, &lagg_et)
#define	LAGG_RUNLOCK()	epoch_exit_preempt(net_epoch_preempt, &lagg_et)
#define	LAGG_RLOCK_ASSERT()	MPASS(in_epoch(net_epoch_preempt))
#define	LAGG_UNLOCK_ASSERT()	MPASS(!in_epoch(net_epoch_preempt))

#define	LAGG_SX_INIT(_sc)	sx_init(&(_sc)->sc_sx, "if_lagg sx")
#define	LAGG_SX_DESTROY(_sc)	sx_destroy(&(_sc)->sc_sx)
#define	LAGG_XLOCK(_sc)		sx_xlock(&(_sc)->sc_sx)
#define	LAGG_XUNLOCK(_sc)	sx_xunlock(&(_sc)->sc_sx)
#define	LAGG_SXLOCK_ASSERT(_sc)	sx_assert(&(_sc)->sc_sx, SA_LOCKED)
#define	LAGG_XLOCK_ASSERT(_sc)	sx_assert(&(_sc)->sc_sx, SA_XLOCKED)

/* Special flags we should propagate to the lagg ports. */
static struct {
	int flag;
	int (*func)(struct ifnet *, int);
} lagg_pflags[] = {
	{IFF_PROMISC, ifpromisc},
	{IFF_ALLMULTI, if_allmulti},
	{0, NULL}
};

VNET_DEFINE(SLIST_HEAD(__trhead, lagg_softc), lagg_list); /* list of laggs */
#define	V_lagg_list	VNET(lagg_list)
VNET_DEFINE_STATIC(struct mtx, lagg_list_mtx);
#define	V_lagg_list_mtx	VNET(lagg_list_mtx)
#define	LAGG_LIST_LOCK_INIT(x)		mtx_init(&V_lagg_list_mtx, \
					"if_lagg list", NULL, MTX_DEF)
#define	LAGG_LIST_LOCK_DESTROY(x)	mtx_destroy(&V_lagg_list_mtx)
#define	LAGG_LIST_LOCK(x)		mtx_lock(&V_lagg_list_mtx)
#define	LAGG_LIST_UNLOCK(x)		mtx_unlock(&V_lagg_list_mtx)
eventhandler_tag	lagg_detach_cookie = NULL;

static int	lagg_clone_create(struct if_clone *, int, caddr_t);
static void	lagg_clone_destroy(struct ifnet *);
VNET_DEFINE_STATIC(struct if_clone *, lagg_cloner);
#define	V_lagg_cloner	VNET(lagg_cloner)
static const char laggname[] = "lagg";

static void	lagg_capabilities(struct lagg_softc *);
static int	lagg_port_create(struct lagg_softc *, struct ifnet *);
static int	lagg_port_destroy(struct lagg_port *, int);
static struct mbuf *lagg_input(struct ifnet *, struct mbuf *);
static void	lagg_linkstate(struct lagg_softc *);
static void	lagg_port_state(struct ifnet *, int);
static int	lagg_port_ioctl(struct ifnet *, u_long, caddr_t);
static int	lagg_port_output(struct ifnet *, struct mbuf *,
		    const struct sockaddr *, struct route *);
static void	lagg_port_ifdetach(void *arg __unused, struct ifnet *);
#ifdef LAGG_PORT_STACKING
static int	lagg_port_checkstacking(struct lagg_softc *);
#endif
static void	lagg_port2req(struct lagg_port *, struct lagg_reqport *);
static void	lagg_init(void *);
static void	lagg_stop(struct lagg_softc *);
static int	lagg_ioctl(struct ifnet *, u_long, caddr_t);
#ifdef RATELIMIT
static int	lagg_snd_tag_alloc(struct ifnet *,
		    union if_snd_tag_alloc_params *,
		    struct m_snd_tag **);
static void	lagg_snd_tag_free(struct m_snd_tag *);
#endif
static int	lagg_setmulti(struct lagg_port *);
static int	lagg_clrmulti(struct lagg_port *);
static	int	lagg_setcaps(struct lagg_port *, int cap);
static	int	lagg_setflag(struct lagg_port *, int, int,
		    int (*func)(struct ifnet *, int));
static	int	lagg_setflags(struct lagg_port *, int status);
static uint64_t lagg_get_counter(struct ifnet *ifp, ift_counter cnt);
static int	lagg_transmit(struct ifnet *, struct mbuf *);
static void	lagg_qflush(struct ifnet *);
static int	lagg_media_change(struct ifnet *);
static void	lagg_media_status(struct ifnet *, struct ifmediareq *);
static struct lagg_port *lagg_link_active(struct lagg_softc *,
	    struct lagg_port *);

/* Simple round robin */
static void	lagg_rr_attach(struct lagg_softc *);
static int	lagg_rr_start(struct lagg_softc *, struct mbuf *);
static struct mbuf *lagg_rr_input(struct lagg_softc *, struct lagg_port *,
		    struct mbuf *);

/* Active failover */
static int	lagg_fail_start(struct lagg_softc *, struct mbuf *);
static struct mbuf *lagg_fail_input(struct lagg_softc *, struct lagg_port *,
		    struct mbuf *);

/* Loadbalancing */
static void	lagg_lb_attach(struct lagg_softc *);
static void	lagg_lb_detach(struct lagg_softc *);
static int	lagg_lb_port_create(struct lagg_port *);
static void	lagg_lb_port_destroy(struct lagg_port *);
static int	lagg_lb_start(struct lagg_softc *, struct mbuf *);
static struct mbuf *lagg_lb_input(struct lagg_softc *, struct lagg_port *,
		    struct mbuf *);
static int	lagg_lb_porttable(struct lagg_softc *, struct lagg_port *);

/* Broadcast */
static int    lagg_bcast_start(struct lagg_softc *, struct mbuf *);
static struct mbuf *lagg_bcast_input(struct lagg_softc *, struct lagg_port *,
		    struct mbuf *);

/* 802.3ad LACP */
static void	lagg_lacp_attach(struct lagg_softc *);
static void	lagg_lacp_detach(struct lagg_softc *);
static int	lagg_lacp_start(struct lagg_softc *, struct mbuf *);
static struct mbuf *lagg_lacp_input(struct lagg_softc *, struct lagg_port *,
		    struct mbuf *);
static void	lagg_lacp_lladdr(struct lagg_softc *);

/* lagg protocol table */
static const struct lagg_proto {
	lagg_proto	pr_num;
	void		(*pr_attach)(struct lagg_softc *);
	void		(*pr_detach)(struct lagg_softc *);
	int		(*pr_start)(struct lagg_softc *, struct mbuf *);
	struct mbuf *	(*pr_input)(struct lagg_softc *, struct lagg_port *,
			    struct mbuf *);
	int		(*pr_addport)(struct lagg_port *);
	void		(*pr_delport)(struct lagg_port *);
	void		(*pr_linkstate)(struct lagg_port *);
	void 		(*pr_init)(struct lagg_softc *);
	void 		(*pr_stop)(struct lagg_softc *);
	void 		(*pr_lladdr)(struct lagg_softc *);
	void		(*pr_request)(struct lagg_softc *, void *);
	void		(*pr_portreq)(struct lagg_port *, void *);
} lagg_protos[] = {
    {
	.pr_num = LAGG_PROTO_NONE
    },
    {
	.pr_num = LAGG_PROTO_ROUNDROBIN,
	.pr_attach = lagg_rr_attach,
	.pr_start = lagg_rr_start,
	.pr_input = lagg_rr_input,
    },
    {
	.pr_num = LAGG_PROTO_FAILOVER,
	.pr_start = lagg_fail_start,
	.pr_input = lagg_fail_input,
    },
    {
	.pr_num = LAGG_PROTO_LOADBALANCE,
	.pr_attach = lagg_lb_attach,
	.pr_detach = lagg_lb_detach,
	.pr_start = lagg_lb_start,
	.pr_input = lagg_lb_input,
	.pr_addport = lagg_lb_port_create,
	.pr_delport = lagg_lb_port_destroy,
    },
    {
	.pr_num = LAGG_PROTO_LACP,
	.pr_attach = lagg_lacp_attach,
	.pr_detach = lagg_lacp_detach,
	.pr_start = lagg_lacp_start,
	.pr_input = lagg_lacp_input,
	.pr_addport = lacp_port_create,
	.pr_delport = lacp_port_destroy,
	.pr_linkstate = lacp_linkstate,
	.pr_init = lacp_init,
	.pr_stop = lacp_stop,
	.pr_lladdr = lagg_lacp_lladdr,
	.pr_request = lacp_req,
	.pr_portreq = lacp_portreq,
    },
    {
	.pr_num = LAGG_PROTO_BROADCAST,
	.pr_start = lagg_bcast_start,
	.pr_input = lagg_bcast_input,
    },
};

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, OID_AUTO, lagg, CTLFLAG_RW, 0,
    "Link Aggregation");

/* Allow input on any failover links */
VNET_DEFINE_STATIC(int, lagg_failover_rx_all);
#define	V_lagg_failover_rx_all	VNET(lagg_failover_rx_all)
SYSCTL_INT(_net_link_lagg, OID_AUTO, failover_rx_all, CTLFLAG_RW | CTLFLAG_VNET,
    &VNET_NAME(lagg_failover_rx_all), 0,
    "Accept input from any interface in a failover lagg");

/* Default value for using flowid */
VNET_DEFINE_STATIC(int, def_use_flowid) = 0;
#define	V_def_use_flowid	VNET(def_use_flowid)
SYSCTL_INT(_net_link_lagg, OID_AUTO, default_use_flowid, CTLFLAG_RWTUN,
    &VNET_NAME(def_use_flowid), 0,
    "Default setting for using flow id for load sharing");

/* Default value for flowid shift */
VNET_DEFINE_STATIC(int, def_flowid_shift) = 16;
#define	V_def_flowid_shift	VNET(def_flowid_shift)
SYSCTL_INT(_net_link_lagg, OID_AUTO, default_flowid_shift, CTLFLAG_RWTUN,
    &VNET_NAME(def_flowid_shift), 0,
    "Default setting for flowid shift for load sharing");

static void
vnet_lagg_init(const void *unused __unused)
{

	LAGG_LIST_LOCK_INIT();
	SLIST_INIT(&V_lagg_list);
	V_lagg_cloner = if_clone_simple(laggname, lagg_clone_create,
	    lagg_clone_destroy, 0);
}
VNET_SYSINIT(vnet_lagg_init, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_lagg_init, NULL);

static void
vnet_lagg_uninit(const void *unused __unused)
{

	if_clone_detach(V_lagg_cloner);
	LAGG_LIST_LOCK_DESTROY();
}
VNET_SYSUNINIT(vnet_lagg_uninit, SI_SUB_INIT_IF, SI_ORDER_ANY,
    vnet_lagg_uninit, NULL);

static int
lagg_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		lagg_input_p = lagg_input;
		lagg_linkstate_p = lagg_port_state;
		lagg_detach_cookie = EVENTHANDLER_REGISTER(
		    ifnet_departure_event, lagg_port_ifdetach, NULL,
		    EVENTHANDLER_PRI_ANY);
		break;
	case MOD_UNLOAD:
		EVENTHANDLER_DEREGISTER(ifnet_departure_event,
		    lagg_detach_cookie);
		lagg_input_p = NULL;
		lagg_linkstate_p = NULL;
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t lagg_mod = {
	"if_lagg",
	lagg_modevent,
	0
};

DECLARE_MODULE(if_lagg, lagg_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(if_lagg, 1);

static void
lagg_proto_attach(struct lagg_softc *sc, lagg_proto pr)
{

	LAGG_XLOCK_ASSERT(sc);
	KASSERT(sc->sc_proto == LAGG_PROTO_NONE, ("%s: sc %p has proto",
	    __func__, sc));

	if (sc->sc_ifflags & IFF_DEBUG)
		if_printf(sc->sc_ifp, "using proto %u\n", pr);

	if (lagg_protos[pr].pr_attach != NULL)
		lagg_protos[pr].pr_attach(sc);
	sc->sc_proto = pr;
}

static void
lagg_proto_detach(struct lagg_softc *sc)
{
	lagg_proto pr;

	LAGG_XLOCK_ASSERT(sc);
	pr = sc->sc_proto;
	sc->sc_proto = LAGG_PROTO_NONE;

	if (lagg_protos[pr].pr_detach != NULL)
		lagg_protos[pr].pr_detach(sc);
}

static int
lagg_proto_start(struct lagg_softc *sc, struct mbuf *m)
{

	return (lagg_protos[sc->sc_proto].pr_start(sc, m));
}

static struct mbuf *
lagg_proto_input(struct lagg_softc *sc, struct lagg_port *lp, struct mbuf *m)
{

	return (lagg_protos[sc->sc_proto].pr_input(sc, lp, m));
}

static int
lagg_proto_addport(struct lagg_softc *sc, struct lagg_port *lp)
{

	if (lagg_protos[sc->sc_proto].pr_addport == NULL)
		return (0);
	else
		return (lagg_protos[sc->sc_proto].pr_addport(lp));
}

static void
lagg_proto_delport(struct lagg_softc *sc, struct lagg_port *lp)
{

	if (lagg_protos[sc->sc_proto].pr_delport != NULL)
		lagg_protos[sc->sc_proto].pr_delport(lp);
}

static void
lagg_proto_linkstate(struct lagg_softc *sc, struct lagg_port *lp)
{

	if (lagg_protos[sc->sc_proto].pr_linkstate != NULL)
		lagg_protos[sc->sc_proto].pr_linkstate(lp);
}

static void
lagg_proto_init(struct lagg_softc *sc)
{

	if (lagg_protos[sc->sc_proto].pr_init != NULL)
		lagg_protos[sc->sc_proto].pr_init(sc);
}

static void
lagg_proto_stop(struct lagg_softc *sc)
{

	if (lagg_protos[sc->sc_proto].pr_stop != NULL)
		lagg_protos[sc->sc_proto].pr_stop(sc);
}

static void
lagg_proto_lladdr(struct lagg_softc *sc)
{

	if (lagg_protos[sc->sc_proto].pr_lladdr != NULL)
		lagg_protos[sc->sc_proto].pr_lladdr(sc);
}

static void
lagg_proto_request(struct lagg_softc *sc, void *v)
{

	if (lagg_protos[sc->sc_proto].pr_request != NULL)
		lagg_protos[sc->sc_proto].pr_request(sc, v);
}

static void
lagg_proto_portreq(struct lagg_softc *sc, struct lagg_port *lp, void *v)
{

	if (lagg_protos[sc->sc_proto].pr_portreq != NULL)
		lagg_protos[sc->sc_proto].pr_portreq(lp, v);
}

/*
 * This routine is run via an vlan
 * config EVENT
 */
static void
lagg_register_vlan(void *arg, struct ifnet *ifp, u_int16_t vtag)
{
	struct lagg_softc *sc = ifp->if_softc;
	struct lagg_port *lp;

	if (ifp->if_softc !=  arg)   /* Not our event */
		return;

	LAGG_RLOCK();
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries)
		EVENTHANDLER_INVOKE(vlan_config, lp->lp_ifp, vtag);
	LAGG_RUNLOCK();
}

/*
 * This routine is run via an vlan
 * unconfig EVENT
 */
static void
lagg_unregister_vlan(void *arg, struct ifnet *ifp, u_int16_t vtag)
{
	struct lagg_softc *sc = ifp->if_softc;
	struct lagg_port *lp;

	if (ifp->if_softc !=  arg)   /* Not our event */
		return;

	LAGG_RLOCK();
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries)
		EVENTHANDLER_INVOKE(vlan_unconfig, lp->lp_ifp, vtag);
	LAGG_RUNLOCK();
}

static int
lagg_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct lagg_softc *sc;
	struct ifnet *ifp;
	static const u_char eaddr[6];	/* 00:00:00:00:00:00 */

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = sc->sc_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		free(sc, M_DEVBUF);
		return (ENOSPC);
	}
	LAGG_SX_INIT(sc);

	LAGG_XLOCK(sc);
	if (V_def_use_flowid)
		sc->sc_opts |= LAGG_OPT_USE_FLOWID;
	sc->flowid_shift = V_def_flowid_shift;

	/* Hash all layers by default */
	sc->sc_flags = MBUF_HASHFLAG_L2|MBUF_HASHFLAG_L3|MBUF_HASHFLAG_L4;

	lagg_proto_attach(sc, LAGG_PROTO_DEFAULT);

	CK_SLIST_INIT(&sc->sc_ports);

	/* Initialise pseudo media types */
	ifmedia_init(&sc->sc_media, 0, lagg_media_change,
	    lagg_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_initname(ifp, laggname, unit);
	ifp->if_softc = sc;
	ifp->if_transmit = lagg_transmit;
	ifp->if_qflush = lagg_qflush;
	ifp->if_init = lagg_init;
	ifp->if_ioctl = lagg_ioctl;
	ifp->if_get_counter = lagg_get_counter;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
#ifdef RATELIMIT
	ifp->if_snd_tag_alloc = lagg_snd_tag_alloc;
	ifp->if_snd_tag_free = lagg_snd_tag_free;
#endif
	ifp->if_capenable = ifp->if_capabilities = IFCAP_HWSTATS;

	/*
	 * Attach as an ordinary ethernet device, children will be attached
	 * as special device IFT_IEEE8023ADLAG.
	 */
	ether_ifattach(ifp, eaddr);

	sc->vlan_attach = EVENTHANDLER_REGISTER(vlan_config,
		lagg_register_vlan, sc, EVENTHANDLER_PRI_FIRST);
	sc->vlan_detach = EVENTHANDLER_REGISTER(vlan_unconfig,
		lagg_unregister_vlan, sc, EVENTHANDLER_PRI_FIRST);

	/* Insert into the global list of laggs */
	LAGG_LIST_LOCK();
	SLIST_INSERT_HEAD(&V_lagg_list, sc, sc_entries);
	LAGG_LIST_UNLOCK();
	LAGG_XUNLOCK(sc);

	return (0);
}

static void
lagg_clone_destroy(struct ifnet *ifp)
{
	struct lagg_softc *sc = (struct lagg_softc *)ifp->if_softc;
	struct lagg_port *lp;

	LAGG_XLOCK(sc);
	sc->sc_destroying = 1;
	lagg_stop(sc);
	ifp->if_flags &= ~IFF_UP;

	EVENTHANDLER_DEREGISTER(vlan_config, sc->vlan_attach);
	EVENTHANDLER_DEREGISTER(vlan_unconfig, sc->vlan_detach);

	/* Shutdown and remove lagg ports */
	while ((lp = CK_SLIST_FIRST(&sc->sc_ports)) != NULL)
		lagg_port_destroy(lp, 1);

	/* Unhook the aggregation protocol */
	lagg_proto_detach(sc);
	LAGG_XUNLOCK(sc);

	ifmedia_removeall(&sc->sc_media);
	ether_ifdetach(ifp);
	if_free(ifp);

	LAGG_LIST_LOCK();
	SLIST_REMOVE(&V_lagg_list, sc, lagg_softc, sc_entries);
	LAGG_LIST_UNLOCK();

	LAGG_SX_DESTROY(sc);
	free(sc, M_DEVBUF);
}

static void
lagg_capabilities(struct lagg_softc *sc)
{
	struct lagg_port *lp;
	int cap, ena, pena;
	uint64_t hwa;
	struct ifnet_hw_tsomax hw_tsomax;

	LAGG_XLOCK_ASSERT(sc);

	/* Get common enabled capabilities for the lagg ports */
	ena = ~0;
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries)
		ena &= lp->lp_ifp->if_capenable;
	ena = (ena == ~0 ? 0 : ena);

	/*
	 * Apply common enabled capabilities back to the lagg ports.
	 * May require several iterations if they are dependent.
	 */
	do {
		pena = ena;
		CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
			lagg_setcaps(lp, ena);
			ena &= lp->lp_ifp->if_capenable;
		}
	} while (pena != ena);

	/* Get other capabilities from the lagg ports */
	cap = ~0;
	hwa = ~(uint64_t)0;
	memset(&hw_tsomax, 0, sizeof(hw_tsomax));
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
		cap &= lp->lp_ifp->if_capabilities;
		hwa &= lp->lp_ifp->if_hwassist;
		if_hw_tsomax_common(lp->lp_ifp, &hw_tsomax);
	}
	cap = (cap == ~0 ? 0 : cap);
	hwa = (hwa == ~(uint64_t)0 ? 0 : hwa);

	if (sc->sc_ifp->if_capabilities != cap ||
	    sc->sc_ifp->if_capenable != ena ||
	    sc->sc_ifp->if_hwassist != hwa ||
	    if_hw_tsomax_update(sc->sc_ifp, &hw_tsomax) != 0) {
		sc->sc_ifp->if_capabilities = cap;
		sc->sc_ifp->if_capenable = ena;
		sc->sc_ifp->if_hwassist = hwa;
		getmicrotime(&sc->sc_ifp->if_lastchange);

		if (sc->sc_ifflags & IFF_DEBUG)
			if_printf(sc->sc_ifp,
			    "capabilities 0x%08x enabled 0x%08x\n", cap, ena);
	}
}

static int
lagg_port_create(struct lagg_softc *sc, struct ifnet *ifp)
{
	struct lagg_softc *sc_ptr;
	struct lagg_port *lp, *tlp;
	struct ifreq ifr;
	int error, i, oldmtu;
	uint64_t *pval;

	LAGG_XLOCK_ASSERT(sc);

	if (sc->sc_ifp == ifp) {
		if_printf(sc->sc_ifp,
		    "cannot add a lagg to itself as a port\n");
		return (EINVAL);
	}

	/* Limit the maximal number of lagg ports */
	if (sc->sc_count >= LAGG_MAX_PORTS)
		return (ENOSPC);

	/* Check if port has already been associated to a lagg */
	if (ifp->if_lagg != NULL) {
		/* Port is already in the current lagg? */
		lp = (struct lagg_port *)ifp->if_lagg;
		if (lp->lp_softc == sc)
			return (EEXIST);
		return (EBUSY);
	}

	/* XXX Disallow non-ethernet interfaces (this should be any of 802) */
	if (ifp->if_type != IFT_ETHER && ifp->if_type != IFT_L2VLAN)
		return (EPROTONOSUPPORT);

	/* Allow the first Ethernet member to define the MTU */
	oldmtu = -1;
	if (CK_SLIST_EMPTY(&sc->sc_ports)) {
		sc->sc_ifp->if_mtu = ifp->if_mtu;
	} else if (sc->sc_ifp->if_mtu != ifp->if_mtu) {
		if (ifp->if_ioctl == NULL) {
			if_printf(sc->sc_ifp, "cannot change MTU for %s\n",
			    ifp->if_xname);
			return (EINVAL);
		}
		oldmtu = ifp->if_mtu;
		strlcpy(ifr.ifr_name, ifp->if_xname, sizeof(ifr.ifr_name));
		ifr.ifr_mtu = sc->sc_ifp->if_mtu;
		error = (*ifp->if_ioctl)(ifp, SIOCSIFMTU, (caddr_t)&ifr);
		if (error != 0) {
			if_printf(sc->sc_ifp, "invalid MTU for %s\n",
			    ifp->if_xname);
			return (error);
		}
		ifr.ifr_mtu = oldmtu;
	}

	lp = malloc(sizeof(struct lagg_port), M_DEVBUF, M_WAITOK|M_ZERO);
	lp->lp_softc = sc;

	/* Check if port is a stacked lagg */
	LAGG_LIST_LOCK();
	SLIST_FOREACH(sc_ptr, &V_lagg_list, sc_entries) {
		if (ifp == sc_ptr->sc_ifp) {
			LAGG_LIST_UNLOCK();
			free(lp, M_DEVBUF);
			if (oldmtu != -1)
				(*ifp->if_ioctl)(ifp, SIOCSIFMTU,
				    (caddr_t)&ifr);
			return (EINVAL);
			/* XXX disable stacking for the moment, its untested */
#ifdef LAGG_PORT_STACKING
			lp->lp_flags |= LAGG_PORT_STACK;
			if (lagg_port_checkstacking(sc_ptr) >=
			    LAGG_MAX_STACKING) {
				LAGG_LIST_UNLOCK();
				free(lp, M_DEVBUF);
				if (oldmtu != -1)
					(*ifp->if_ioctl)(ifp, SIOCSIFMTU,
					    (caddr_t)&ifr);
				return (E2BIG);
			}
#endif
		}
	}
	LAGG_LIST_UNLOCK();

	if_ref(ifp);
	lp->lp_ifp = ifp;

	bcopy(IF_LLADDR(ifp), lp->lp_lladdr, ETHER_ADDR_LEN);
	lp->lp_ifcapenable = ifp->if_capenable;
	if (CK_SLIST_EMPTY(&sc->sc_ports)) {
		bcopy(IF_LLADDR(ifp), IF_LLADDR(sc->sc_ifp), ETHER_ADDR_LEN);
		lagg_proto_lladdr(sc);
		EVENTHANDLER_INVOKE(iflladdr_event, sc->sc_ifp);
	} else {
		if_setlladdr(ifp, IF_LLADDR(sc->sc_ifp), ETHER_ADDR_LEN);
	}
	lagg_setflags(lp, 1);

	if (CK_SLIST_EMPTY(&sc->sc_ports))
		sc->sc_primary = lp;

	/* Change the interface type */
	lp->lp_iftype = ifp->if_type;
	ifp->if_type = IFT_IEEE8023ADLAG;
	ifp->if_lagg = lp;
	lp->lp_ioctl = ifp->if_ioctl;
	ifp->if_ioctl = lagg_port_ioctl;
	lp->lp_output = ifp->if_output;
	ifp->if_output = lagg_port_output;

	/* Read port counters */
	pval = lp->port_counters.val;
	for (i = 0; i < IFCOUNTERS; i++, pval++)
		*pval = ifp->if_get_counter(ifp, i);

	/*
	 * Insert into the list of ports.
	 * Keep ports sorted by if_index. It is handy, when configuration
	 * is predictable and `ifconfig laggN create ...` command
	 * will lead to the same result each time.
	 */
	LAGG_RLOCK();
	CK_SLIST_FOREACH(tlp, &sc->sc_ports, lp_entries) {
		if (tlp->lp_ifp->if_index < ifp->if_index && (
		    CK_SLIST_NEXT(tlp, lp_entries) == NULL ||
		    ((struct  lagg_port*)CK_SLIST_NEXT(tlp, lp_entries))->lp_ifp->if_index >
		    ifp->if_index))
			break;
	}
	LAGG_RUNLOCK();
	if (tlp != NULL)
		CK_SLIST_INSERT_AFTER(tlp, lp, lp_entries);
	else
		CK_SLIST_INSERT_HEAD(&sc->sc_ports, lp, lp_entries);
	sc->sc_count++;

	lagg_setmulti(lp);


	if ((error = lagg_proto_addport(sc, lp)) != 0) {
		/* Remove the port, without calling pr_delport. */
		lagg_port_destroy(lp, 0);
		if (oldmtu != -1)
			(*ifp->if_ioctl)(ifp, SIOCSIFMTU, (caddr_t)&ifr);
		return (error);
	}

	/* Update lagg capabilities */
	lagg_capabilities(sc);
	lagg_linkstate(sc);

	return (0);
}

#ifdef LAGG_PORT_STACKING
static int
lagg_port_checkstacking(struct lagg_softc *sc)
{
	struct lagg_softc *sc_ptr;
	struct lagg_port *lp;
	int m = 0;

	LAGG_SXLOCK_ASSERT(sc);
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
		if (lp->lp_flags & LAGG_PORT_STACK) {
			sc_ptr = (struct lagg_softc *)lp->lp_ifp->if_softc;
			m = MAX(m, lagg_port_checkstacking(sc_ptr));
		}
	}

	return (m + 1);
}
#endif

static void
lagg_port_destroy_cb(epoch_context_t ec)
{
	struct lagg_port *lp;
	struct ifnet *ifp;

	lp = __containerof(ec, struct lagg_port, lp_epoch_ctx);
	ifp = lp->lp_ifp;

	if_rele(ifp);
	free(lp, M_DEVBUF);
}

static int
lagg_port_destroy(struct lagg_port *lp, int rundelport)
{
	struct lagg_softc *sc = lp->lp_softc;
	struct lagg_port *lp_ptr, *lp0;
	struct ifnet *ifp = lp->lp_ifp;
	uint64_t *pval, vdiff;
	int i;

	LAGG_XLOCK_ASSERT(sc);

	if (rundelport)
		lagg_proto_delport(sc, lp);

	if (lp->lp_detaching == 0)
		lagg_clrmulti(lp);

	/* Restore interface */
	ifp->if_type = lp->lp_iftype;
	ifp->if_ioctl = lp->lp_ioctl;
	ifp->if_output = lp->lp_output;
	ifp->if_lagg = NULL;

	/* Update detached port counters */
	pval = lp->port_counters.val;
	for (i = 0; i < IFCOUNTERS; i++, pval++) {
		vdiff = ifp->if_get_counter(ifp, i) - *pval;
		sc->detached_counters.val[i] += vdiff;
	}

	/* Finally, remove the port from the lagg */
	CK_SLIST_REMOVE(&sc->sc_ports, lp, lagg_port, lp_entries);
	sc->sc_count--;

	/* Update the primary interface */
	if (lp == sc->sc_primary) {
		uint8_t lladdr[ETHER_ADDR_LEN];

		if ((lp0 = CK_SLIST_FIRST(&sc->sc_ports)) == NULL)
			bzero(&lladdr, ETHER_ADDR_LEN);
		else
			bcopy(lp0->lp_lladdr, lladdr, ETHER_ADDR_LEN);
		sc->sc_primary = lp0;
		if (sc->sc_destroying == 0) {
			bcopy(lladdr, IF_LLADDR(sc->sc_ifp), ETHER_ADDR_LEN);
			lagg_proto_lladdr(sc);
			EVENTHANDLER_INVOKE(iflladdr_event, sc->sc_ifp);
		}

		/*
		 * Update lladdr for each port (new primary needs update
		 * as well, to switch from old lladdr to its 'real' one)
		 */
		CK_SLIST_FOREACH(lp_ptr, &sc->sc_ports, lp_entries)
			if_setlladdr(lp_ptr->lp_ifp, lladdr, ETHER_ADDR_LEN);
	}

	if (lp->lp_ifflags)
		if_printf(ifp, "%s: lp_ifflags unclean\n", __func__);

	if (lp->lp_detaching == 0) {
		lagg_setflags(lp, 0);
		lagg_setcaps(lp, lp->lp_ifcapenable);
		if_setlladdr(ifp, lp->lp_lladdr, ETHER_ADDR_LEN);
	}

	/*
	 * free port and release it's ifnet reference after a grace period has
	 * elapsed.
	 */
	epoch_call(net_epoch_preempt, &lp->lp_epoch_ctx, lagg_port_destroy_cb);
	/* Update lagg capabilities */
	lagg_capabilities(sc);
	lagg_linkstate(sc);

	return (0);
}

static int
lagg_port_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct lagg_reqport *rp = (struct lagg_reqport *)data;
	struct lagg_softc *sc;
	struct lagg_port *lp = NULL;
	int error = 0;

	/* Should be checked by the caller */
	if (ifp->if_type != IFT_IEEE8023ADLAG ||
	    (lp = ifp->if_lagg) == NULL || (sc = lp->lp_softc) == NULL)
		goto fallback;

	switch (cmd) {
	case SIOCGLAGGPORT:
		if (rp->rp_portname[0] == '\0' ||
		    ifunit(rp->rp_portname) != ifp) {
			error = EINVAL;
			break;
		}

		LAGG_RLOCK();
		if ((lp = ifp->if_lagg) == NULL || lp->lp_softc != sc) {
			error = ENOENT;
			LAGG_RUNLOCK();
			break;
		}

		lagg_port2req(lp, rp);
		LAGG_RUNLOCK();
		break;

	case SIOCSIFCAP:
		if (lp->lp_ioctl == NULL) {
			error = EINVAL;
			break;
		}
		error = (*lp->lp_ioctl)(ifp, cmd, data);
		if (error)
			break;

		/* Update lagg interface capabilities */
		LAGG_XLOCK(sc);
		lagg_capabilities(sc);
		LAGG_XUNLOCK(sc);
		VLAN_CAPABILITIES(sc->sc_ifp);
		break;

	case SIOCSIFMTU:
		/* Do not allow the MTU to be changed once joined */
		error = EINVAL;
		break;

	default:
		goto fallback;
	}

	return (error);

fallback:
	if (lp != NULL && lp->lp_ioctl != NULL)
		return ((*lp->lp_ioctl)(ifp, cmd, data));

	return (EINVAL);
}

/*
 * Requests counter @cnt data. 
 *
 * Counter value is calculated the following way:
 * 1) for each port, sum  difference between current and "initial" measurements.
 * 2) add lagg logical interface counters.
 * 3) add data from detached_counters array.
 *
 * We also do the following things on ports attach/detach:
 * 1) On port attach we store all counters it has into port_counter array. 
 * 2) On port detach we add the different between "initial" and
 *   current counters data to detached_counters array.
 */
static uint64_t
lagg_get_counter(struct ifnet *ifp, ift_counter cnt)
{
	struct lagg_softc *sc;
	struct lagg_port *lp;
	struct ifnet *lpifp;
	uint64_t newval, oldval, vsum;

	/* Revise this when we've got non-generic counters. */
	KASSERT(cnt < IFCOUNTERS, ("%s: invalid cnt %d", __func__, cnt));

	sc = (struct lagg_softc *)ifp->if_softc;

	vsum = 0;
	LAGG_RLOCK();
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
		/* Saved attached value */
		oldval = lp->port_counters.val[cnt];
		/* current value */
		lpifp = lp->lp_ifp;
		newval = lpifp->if_get_counter(lpifp, cnt);
		/* Calculate diff and save new */
		vsum += newval - oldval;
	}
	LAGG_RUNLOCK();

	/*
	 * Add counter data which might be added by upper
	 * layer protocols operating on logical interface.
	 */
	vsum += if_get_counter_default(ifp, cnt);

	/*
	 * Add counter data from detached ports counters
	 */
	vsum += sc->detached_counters.val[cnt];


	return (vsum);
}

/*
 * For direct output to child ports.
 */
static int
lagg_port_output(struct ifnet *ifp, struct mbuf *m,
	const struct sockaddr *dst, struct route *ro)
{
	struct lagg_port *lp = ifp->if_lagg;

	switch (dst->sa_family) {
		case pseudo_AF_HDRCMPLT:
		case AF_UNSPEC:
			return ((*lp->lp_output)(ifp, m, dst, ro));
	}

	/* drop any other frames */
	m_freem(m);
	return (ENETDOWN);
}

static void
lagg_port_ifdetach(void *arg __unused, struct ifnet *ifp)
{
	struct lagg_port *lp;
	struct lagg_softc *sc;

	if ((lp = ifp->if_lagg) == NULL)
		return;
	/* If the ifnet is just being renamed, don't do anything. */
	if (ifp->if_flags & IFF_RENAMING)
		return;

	sc = lp->lp_softc;

	LAGG_XLOCK(sc);
	lp->lp_detaching = 1;
	lagg_port_destroy(lp, 1);
	LAGG_XUNLOCK(sc);
	VLAN_CAPABILITIES(sc->sc_ifp);
}

static void
lagg_port2req(struct lagg_port *lp, struct lagg_reqport *rp)
{
	struct lagg_softc *sc = lp->lp_softc;

	strlcpy(rp->rp_ifname, sc->sc_ifname, sizeof(rp->rp_ifname));
	strlcpy(rp->rp_portname, lp->lp_ifp->if_xname, sizeof(rp->rp_portname));
	rp->rp_prio = lp->lp_prio;
	rp->rp_flags = lp->lp_flags;
	lagg_proto_portreq(sc, lp, &rp->rp_psc);

	/* Add protocol specific flags */
	switch (sc->sc_proto) {
		case LAGG_PROTO_FAILOVER:
			if (lp == sc->sc_primary)
				rp->rp_flags |= LAGG_PORT_MASTER;
			if (lp == lagg_link_active(sc, sc->sc_primary))
				rp->rp_flags |= LAGG_PORT_ACTIVE;
			break;

		case LAGG_PROTO_ROUNDROBIN:
		case LAGG_PROTO_LOADBALANCE:
		case LAGG_PROTO_BROADCAST:
			if (LAGG_PORTACTIVE(lp))
				rp->rp_flags |= LAGG_PORT_ACTIVE;
			break;

		case LAGG_PROTO_LACP:
			/* LACP has a different definition of active */
			if (lacp_isactive(lp))
				rp->rp_flags |= LAGG_PORT_ACTIVE;
			if (lacp_iscollecting(lp))
				rp->rp_flags |= LAGG_PORT_COLLECTING;
			if (lacp_isdistributing(lp))
				rp->rp_flags |= LAGG_PORT_DISTRIBUTING;
			break;
	}

}

static void
lagg_init(void *xsc)
{
	struct lagg_softc *sc = (struct lagg_softc *)xsc;
	struct ifnet *ifp = sc->sc_ifp;
	struct lagg_port *lp;

	LAGG_XLOCK(sc);
	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		LAGG_XUNLOCK(sc);
		return;
	}

	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	/*
	 * Update the port lladdrs if needed.
	 * This might be if_setlladdr() notification
	 * that lladdr has been changed.
	 */
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
		if (memcmp(IF_LLADDR(ifp), IF_LLADDR(lp->lp_ifp),
		    ETHER_ADDR_LEN) != 0)
			if_setlladdr(lp->lp_ifp, IF_LLADDR(ifp), ETHER_ADDR_LEN);
	}

	lagg_proto_init(sc);

	LAGG_XUNLOCK(sc);
}

static void
lagg_stop(struct lagg_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;

	LAGG_XLOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	lagg_proto_stop(sc);
}

static int
lagg_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct lagg_softc *sc = (struct lagg_softc *)ifp->if_softc;
	struct lagg_reqall *ra = (struct lagg_reqall *)data;
	struct lagg_reqopts *ro = (struct lagg_reqopts *)data;
	struct lagg_reqport *rp = (struct lagg_reqport *)data, rpbuf;
	struct lagg_reqflags *rf = (struct lagg_reqflags *)data;
	struct ifreq *ifr = (struct ifreq *)data;
	struct lagg_port *lp;
	struct ifnet *tpif;
	struct thread *td = curthread;
	char *buf, *outbuf;
	int count, buflen, len, error = 0;

	bzero(&rpbuf, sizeof(rpbuf));

	switch (cmd) {
	case SIOCGLAGG:
		LAGG_XLOCK(sc);
		buflen = sc->sc_count * sizeof(struct lagg_reqport);
		outbuf = malloc(buflen, M_TEMP, M_WAITOK | M_ZERO);
		ra->ra_proto = sc->sc_proto;
		lagg_proto_request(sc, &ra->ra_psc);
		count = 0;
		buf = outbuf;
		len = min(ra->ra_size, buflen);
		CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
			if (len < sizeof(rpbuf))
				break;

			lagg_port2req(lp, &rpbuf);
			memcpy(buf, &rpbuf, sizeof(rpbuf));
			count++;
			buf += sizeof(rpbuf);
			len -= sizeof(rpbuf);
		}
		LAGG_XUNLOCK(sc);
		ra->ra_ports = count;
		ra->ra_size = count * sizeof(rpbuf);
		error = copyout(outbuf, ra->ra_port, ra->ra_size);
		free(outbuf, M_TEMP);
		break;
	case SIOCSLAGG:
		error = priv_check(td, PRIV_NET_LAGG);
		if (error)
			break;
		if (ra->ra_proto >= LAGG_PROTO_MAX) {
			error = EPROTONOSUPPORT;
			break;
		}

		LAGG_XLOCK(sc);
		lagg_proto_detach(sc);
		LAGG_UNLOCK_ASSERT();
		lagg_proto_attach(sc, ra->ra_proto);
		LAGG_XUNLOCK(sc);
		break;
	case SIOCGLAGGOPTS:
		LAGG_XLOCK(sc);
		ro->ro_opts = sc->sc_opts;
		if (sc->sc_proto == LAGG_PROTO_LACP) {
			struct lacp_softc *lsc;

			lsc = (struct lacp_softc *)sc->sc_psc;
			if (lsc->lsc_debug.lsc_tx_test != 0)
				ro->ro_opts |= LAGG_OPT_LACP_TXTEST;
			if (lsc->lsc_debug.lsc_rx_test != 0)
				ro->ro_opts |= LAGG_OPT_LACP_RXTEST;
			if (lsc->lsc_strict_mode != 0)
				ro->ro_opts |= LAGG_OPT_LACP_STRICT;
			if (lsc->lsc_fast_timeout != 0)
				ro->ro_opts |= LAGG_OPT_LACP_TIMEOUT;

			ro->ro_active = sc->sc_active;
		} else {
			ro->ro_active = 0;
			CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries)
				ro->ro_active += LAGG_PORTACTIVE(lp);
		}
		ro->ro_bkt = sc->sc_bkt;
		ro->ro_flapping = sc->sc_flapping;
		ro->ro_flowid_shift = sc->flowid_shift;
		LAGG_XUNLOCK(sc);
		break;
	case SIOCSLAGGOPTS:
		if (sc->sc_proto == LAGG_PROTO_ROUNDROBIN) {
			if (ro->ro_bkt == 0)
				sc->sc_bkt = 1; // Minimum 1 packet per iface.
			else
				sc->sc_bkt = ro->ro_bkt;
		}
		error = priv_check(td, PRIV_NET_LAGG);
		if (error)
			break;
		if (ro->ro_opts == 0)
			break;
		/*
		 * Set options.  LACP options are stored in sc->sc_psc,
		 * not in sc_opts.
		 */
		int valid, lacp;

		switch (ro->ro_opts) {
		case LAGG_OPT_USE_FLOWID:
		case -LAGG_OPT_USE_FLOWID:
		case LAGG_OPT_FLOWIDSHIFT:
			valid = 1;
			lacp = 0;
			break;
		case LAGG_OPT_LACP_TXTEST:
		case -LAGG_OPT_LACP_TXTEST:
		case LAGG_OPT_LACP_RXTEST:
		case -LAGG_OPT_LACP_RXTEST:
		case LAGG_OPT_LACP_STRICT:
		case -LAGG_OPT_LACP_STRICT:
		case LAGG_OPT_LACP_TIMEOUT:
		case -LAGG_OPT_LACP_TIMEOUT:
			valid = lacp = 1;
			break;
		default:
			valid = lacp = 0;
			break;
		}

		LAGG_XLOCK(sc);

		if (valid == 0 ||
		    (lacp == 1 && sc->sc_proto != LAGG_PROTO_LACP)) {
			/* Invalid combination of options specified. */
			error = EINVAL;
			LAGG_XUNLOCK(sc);
			break;	/* Return from SIOCSLAGGOPTS. */ 
		}
		/*
		 * Store new options into sc->sc_opts except for
		 * FLOWIDSHIFT and LACP options.
		 */
		if (lacp == 0) {
			if (ro->ro_opts == LAGG_OPT_FLOWIDSHIFT)
				sc->flowid_shift = ro->ro_flowid_shift;
			else if (ro->ro_opts > 0)
				sc->sc_opts |= ro->ro_opts;
			else
				sc->sc_opts &= ~ro->ro_opts;
		} else {
			struct lacp_softc *lsc;
			struct lacp_port *lp;

			lsc = (struct lacp_softc *)sc->sc_psc;

			switch (ro->ro_opts) {
			case LAGG_OPT_LACP_TXTEST:
				lsc->lsc_debug.lsc_tx_test = 1;
				break;
			case -LAGG_OPT_LACP_TXTEST:
				lsc->lsc_debug.lsc_tx_test = 0;
				break;
			case LAGG_OPT_LACP_RXTEST:
				lsc->lsc_debug.lsc_rx_test = 1;
				break;
			case -LAGG_OPT_LACP_RXTEST:
				lsc->lsc_debug.lsc_rx_test = 0;
				break;
			case LAGG_OPT_LACP_STRICT:
				lsc->lsc_strict_mode = 1;
				break;
			case -LAGG_OPT_LACP_STRICT:
				lsc->lsc_strict_mode = 0;
				break;
			case LAGG_OPT_LACP_TIMEOUT:
				LACP_LOCK(lsc);
        			LIST_FOREACH(lp, &lsc->lsc_ports, lp_next)
                        		lp->lp_state |= LACP_STATE_TIMEOUT;
				LACP_UNLOCK(lsc);
				lsc->lsc_fast_timeout = 1;
				break;
			case -LAGG_OPT_LACP_TIMEOUT:
				LACP_LOCK(lsc);
        			LIST_FOREACH(lp, &lsc->lsc_ports, lp_next)
                        		lp->lp_state &= ~LACP_STATE_TIMEOUT;
				LACP_UNLOCK(lsc);
				lsc->lsc_fast_timeout = 0;
				break;
			}
		}
		LAGG_XUNLOCK(sc);
		break;
	case SIOCGLAGGFLAGS:
		rf->rf_flags = 0;
		LAGG_XLOCK(sc);
		if (sc->sc_flags & MBUF_HASHFLAG_L2)
			rf->rf_flags |= LAGG_F_HASHL2;
		if (sc->sc_flags & MBUF_HASHFLAG_L3)
			rf->rf_flags |= LAGG_F_HASHL3;
		if (sc->sc_flags & MBUF_HASHFLAG_L4)
			rf->rf_flags |= LAGG_F_HASHL4;
		LAGG_XUNLOCK(sc);
		break;
	case SIOCSLAGGHASH:
		error = priv_check(td, PRIV_NET_LAGG);
		if (error)
			break;
		if ((rf->rf_flags & LAGG_F_HASHMASK) == 0) {
			error = EINVAL;
			break;
		}
		LAGG_XLOCK(sc);
		sc->sc_flags = 0;
		if (rf->rf_flags & LAGG_F_HASHL2)
			sc->sc_flags |= MBUF_HASHFLAG_L2;
		if (rf->rf_flags & LAGG_F_HASHL3)
			sc->sc_flags |= MBUF_HASHFLAG_L3;
		if (rf->rf_flags & LAGG_F_HASHL4)
			sc->sc_flags |= MBUF_HASHFLAG_L4;
		LAGG_XUNLOCK(sc);
		break;
	case SIOCGLAGGPORT:
		if (rp->rp_portname[0] == '\0' ||
		    (tpif = ifunit_ref(rp->rp_portname)) == NULL) {
			error = EINVAL;
			break;
		}

		LAGG_RLOCK();
		if ((lp = (struct lagg_port *)tpif->if_lagg) == NULL ||
		    lp->lp_softc != sc) {
			error = ENOENT;
			LAGG_RUNLOCK();
			if_rele(tpif);
			break;
		}

		lagg_port2req(lp, rp);
		LAGG_RUNLOCK();
		if_rele(tpif);
		break;
	case SIOCSLAGGPORT:
		error = priv_check(td, PRIV_NET_LAGG);
		if (error)
			break;
		if (rp->rp_portname[0] == '\0' ||
		    (tpif = ifunit_ref(rp->rp_portname)) == NULL) {
			error = EINVAL;
			break;
		}
#ifdef INET6
		/*
		 * A laggport interface should not have inet6 address
		 * because two interfaces with a valid link-local
		 * scope zone must not be merged in any form.  This
		 * restriction is needed to prevent violation of
		 * link-local scope zone.  Attempts to add a laggport
		 * interface which has inet6 addresses triggers
		 * removal of all inet6 addresses on the member
		 * interface.
		 */
		if (in6ifa_llaonifp(tpif)) {
			in6_ifdetach(tpif);
				if_printf(sc->sc_ifp,
				    "IPv6 addresses on %s have been removed "
				    "before adding it as a member to prevent "
				    "IPv6 address scope violation.\n",
				    tpif->if_xname);
		}
#endif
		LAGG_XLOCK(sc);
		error = lagg_port_create(sc, tpif);
		LAGG_XUNLOCK(sc);
		if_rele(tpif);
		VLAN_CAPABILITIES(ifp);
		break;
	case SIOCSLAGGDELPORT:
		error = priv_check(td, PRIV_NET_LAGG);
		if (error)
			break;
		if (rp->rp_portname[0] == '\0' ||
		    (tpif = ifunit_ref(rp->rp_portname)) == NULL) {
			error = EINVAL;
			break;
		}

		LAGG_XLOCK(sc);
		if ((lp = (struct lagg_port *)tpif->if_lagg) == NULL ||
		    lp->lp_softc != sc) {
			error = ENOENT;
			LAGG_XUNLOCK(sc);
			if_rele(tpif);
			break;
		}

		error = lagg_port_destroy(lp, 1);
		LAGG_XUNLOCK(sc);
		if_rele(tpif);
		VLAN_CAPABILITIES(ifp);
		break;
	case SIOCSIFFLAGS:
		/* Set flags on ports too */
		LAGG_XLOCK(sc);
		CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
			lagg_setflags(lp, 1);
		}

		if (!(ifp->if_flags & IFF_UP) &&
		    (ifp->if_drv_flags & IFF_DRV_RUNNING)) {
			/*
			 * If interface is marked down and it is running,
			 * then stop and disable it.
			 */
			lagg_stop(sc);
			LAGG_XUNLOCK(sc);
		} else if ((ifp->if_flags & IFF_UP) &&
		    !(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
			/*
			 * If interface is marked up and it is stopped, then
			 * start it.
			 */
			LAGG_XUNLOCK(sc);
			(*ifp->if_init)(sc);
		} else
			LAGG_XUNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		LAGG_XLOCK(sc);
		CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
			lagg_clrmulti(lp);
			lagg_setmulti(lp);
		}
		LAGG_XUNLOCK(sc);
		error = 0;
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCSIFCAP:
		LAGG_XLOCK(sc);
		CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
			if (lp->lp_ioctl != NULL)
				(*lp->lp_ioctl)(lp->lp_ifp, cmd, data);
		}
		lagg_capabilities(sc);
		LAGG_XUNLOCK(sc);
		VLAN_CAPABILITIES(ifp);
		error = 0;
		break;

	case SIOCSIFMTU:
		LAGG_XLOCK(sc);
		CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
			if (lp->lp_ioctl != NULL)
				error = (*lp->lp_ioctl)(lp->lp_ifp, cmd, data);
			else
				error = EINVAL;
			if (error != 0) {
				if_printf(ifp,
				    "failed to change MTU to %d on port %s, "
				    "reverting all ports to original MTU (%d)\n",
				    ifr->ifr_mtu, lp->lp_ifp->if_xname, ifp->if_mtu);
				break;
			}
		}
		if (error == 0) {
			ifp->if_mtu = ifr->ifr_mtu;
		} else {
			/* set every port back to the original MTU */
			ifr->ifr_mtu = ifp->if_mtu;
			CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
				if (lp->lp_ioctl != NULL)
					(*lp->lp_ioctl)(lp->lp_ifp, cmd, data);
			}
		}
		LAGG_XUNLOCK(sc);
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return (error);
}

#ifdef RATELIMIT
static int
lagg_snd_tag_alloc(struct ifnet *ifp,
    union if_snd_tag_alloc_params *params,
    struct m_snd_tag **ppmt)
{
	struct lagg_softc *sc = (struct lagg_softc *)ifp->if_softc;
	struct lagg_port *lp;
	struct lagg_lb *lb;
	uint32_t p;

	switch (sc->sc_proto) {
	case LAGG_PROTO_FAILOVER:
		lp = lagg_link_active(sc, sc->sc_primary);
		break;
	case LAGG_PROTO_LOADBALANCE:
		if ((sc->sc_opts & LAGG_OPT_USE_FLOWID) == 0 ||
		    params->hdr.flowtype == M_HASHTYPE_NONE)
			return (EOPNOTSUPP);
		p = params->hdr.flowid >> sc->flowid_shift;
		p %= sc->sc_count;
		lb = (struct lagg_lb *)sc->sc_psc;
		lp = lb->lb_ports[p];
		lp = lagg_link_active(sc, lp);
		break;
	case LAGG_PROTO_LACP:
		if ((sc->sc_opts & LAGG_OPT_USE_FLOWID) == 0 ||
		    params->hdr.flowtype == M_HASHTYPE_NONE)
			return (EOPNOTSUPP);
		lp = lacp_select_tx_port_by_hash(sc, params->hdr.flowid);
		break;
	default:
		return (EOPNOTSUPP);
	}
	if (lp == NULL)
		return (EOPNOTSUPP);
	ifp = lp->lp_ifp;
	if (ifp == NULL || ifp->if_snd_tag_alloc == NULL ||
	    (ifp->if_capenable & IFCAP_TXRTLMT) == 0)
		return (EOPNOTSUPP);

	/* forward allocation request */
	return (ifp->if_snd_tag_alloc(ifp, params, ppmt));
}

static void
lagg_snd_tag_free(struct m_snd_tag *tag)
{
	tag->ifp->if_snd_tag_free(tag);
}

#endif

static int
lagg_setmulti(struct lagg_port *lp)
{
	struct lagg_softc *sc = lp->lp_softc;
	struct ifnet *ifp = lp->lp_ifp;
	struct ifnet *scifp = sc->sc_ifp;
	struct lagg_mc *mc;
	struct ifmultiaddr *ifma;
	int error;

	IF_ADDR_WLOCK(scifp);
	CK_STAILQ_FOREACH(ifma, &scifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		mc = malloc(sizeof(struct lagg_mc), M_DEVBUF, M_NOWAIT);
		if (mc == NULL) {
			IF_ADDR_WUNLOCK(scifp);
			return (ENOMEM);
		}
		bcopy(ifma->ifma_addr, &mc->mc_addr,
		    ifma->ifma_addr->sa_len);
		mc->mc_addr.sdl_index = ifp->if_index;
		mc->mc_ifma = NULL;
		SLIST_INSERT_HEAD(&lp->lp_mc_head, mc, mc_entries);
	}
	IF_ADDR_WUNLOCK(scifp);
	SLIST_FOREACH (mc, &lp->lp_mc_head, mc_entries) {
		error = if_addmulti(ifp,
		    (struct sockaddr *)&mc->mc_addr, &mc->mc_ifma);
		if (error)
			return (error);
	}
	return (0);
}

static int
lagg_clrmulti(struct lagg_port *lp)
{
	struct lagg_mc *mc;

	LAGG_XLOCK_ASSERT(lp->lp_softc);
	while ((mc = SLIST_FIRST(&lp->lp_mc_head)) != NULL) {
		SLIST_REMOVE(&lp->lp_mc_head, mc, lagg_mc, mc_entries);
		if (mc->mc_ifma && lp->lp_detaching == 0)
			if_delmulti_ifma(mc->mc_ifma);
		free(mc, M_DEVBUF);
	}
	return (0);
}

static int
lagg_setcaps(struct lagg_port *lp, int cap)
{
	struct ifreq ifr;

	if (lp->lp_ifp->if_capenable == cap)
		return (0);
	if (lp->lp_ioctl == NULL)
		return (ENXIO);
	ifr.ifr_reqcap = cap;
	return ((*lp->lp_ioctl)(lp->lp_ifp, SIOCSIFCAP, (caddr_t)&ifr));
}

/* Handle a ref counted flag that should be set on the lagg port as well */
static int
lagg_setflag(struct lagg_port *lp, int flag, int status,
    int (*func)(struct ifnet *, int))
{
	struct lagg_softc *sc = lp->lp_softc;
	struct ifnet *scifp = sc->sc_ifp;
	struct ifnet *ifp = lp->lp_ifp;
	int error;

	LAGG_XLOCK_ASSERT(sc);

	status = status ? (scifp->if_flags & flag) : 0;
	/* Now "status" contains the flag value or 0 */

	/*
	 * See if recorded ports status is different from what
	 * we want it to be.  If it is, flip it.  We record ports
	 * status in lp_ifflags so that we won't clear ports flag
	 * we haven't set.  In fact, we don't clear or set ports
	 * flags directly, but get or release references to them.
	 * That's why we can be sure that recorded flags still are
	 * in accord with actual ports flags.
	 */
	if (status != (lp->lp_ifflags & flag)) {
		error = (*func)(ifp, status);
		if (error)
			return (error);
		lp->lp_ifflags &= ~flag;
		lp->lp_ifflags |= status;
	}
	return (0);
}

/*
 * Handle IFF_* flags that require certain changes on the lagg port
 * if "status" is true, update ports flags respective to the lagg
 * if "status" is false, forcedly clear the flags set on port.
 */
static int
lagg_setflags(struct lagg_port *lp, int status)
{
	int error, i;

	for (i = 0; lagg_pflags[i].flag; i++) {
		error = lagg_setflag(lp, lagg_pflags[i].flag,
		    status, lagg_pflags[i].func);
		if (error)
			return (error);
	}
	return (0);
}

static int
lagg_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct lagg_softc *sc = (struct lagg_softc *)ifp->if_softc;
	int error;

	LAGG_RLOCK();
	/* We need a Tx algorithm and at least one port */
	if (sc->sc_proto == LAGG_PROTO_NONE || sc->sc_count == 0) {
		LAGG_RUNLOCK();
		m_freem(m);
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return (ENXIO);
	}

	ETHER_BPF_MTAP(ifp, m);

	error = lagg_proto_start(sc, m);
	LAGG_RUNLOCK();

	if (error != 0)
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	return (error);
}

/*
 * The ifp->if_qflush entry point for lagg(4) is no-op.
 */
static void
lagg_qflush(struct ifnet *ifp __unused)
{
}

static struct mbuf *
lagg_input(struct ifnet *ifp, struct mbuf *m)
{
	struct lagg_port *lp = ifp->if_lagg;
	struct lagg_softc *sc = lp->lp_softc;
	struct ifnet *scifp = sc->sc_ifp;

	LAGG_RLOCK();
	if ((scifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    lp->lp_detaching != 0 ||
	    sc->sc_proto == LAGG_PROTO_NONE) {
		LAGG_RUNLOCK();
		m_freem(m);
		return (NULL);
	}

	ETHER_BPF_MTAP(scifp, m);

	m = lagg_proto_input(sc, lp, m);
	if (m != NULL && (scifp->if_flags & IFF_MONITOR) != 0) {
		m_freem(m);
		m = NULL;
	}

	LAGG_RUNLOCK();
	return (m);
}

static int
lagg_media_change(struct ifnet *ifp)
{
	struct lagg_softc *sc = (struct lagg_softc *)ifp->if_softc;

	if (sc->sc_ifflags & IFF_DEBUG)
		printf("%s\n", __func__);

	/* Ignore */
	return (0);
}

static void
lagg_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct lagg_softc *sc = (struct lagg_softc *)ifp->if_softc;
	struct lagg_port *lp;

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_ETHER | IFM_AUTO;

	LAGG_RLOCK();
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
		if (LAGG_PORTACTIVE(lp))
			imr->ifm_status |= IFM_ACTIVE;
	}
	LAGG_RUNLOCK();
}

static void
lagg_linkstate(struct lagg_softc *sc)
{
	struct lagg_port *lp;
	int new_link = LINK_STATE_DOWN;
	uint64_t speed;

	LAGG_XLOCK_ASSERT(sc);

	/* LACP handles link state itself */
	if (sc->sc_proto == LAGG_PROTO_LACP)
		return;

	/* Our link is considered up if at least one of our ports is active */
	LAGG_RLOCK();
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
		if (lp->lp_ifp->if_link_state == LINK_STATE_UP) {
			new_link = LINK_STATE_UP;
			break;
		}
	}
	LAGG_RUNLOCK();
	if_link_state_change(sc->sc_ifp, new_link);

	/* Update if_baudrate to reflect the max possible speed */
	switch (sc->sc_proto) {
		case LAGG_PROTO_FAILOVER:
			sc->sc_ifp->if_baudrate = sc->sc_primary != NULL ?
			    sc->sc_primary->lp_ifp->if_baudrate : 0;
			break;
		case LAGG_PROTO_ROUNDROBIN:
		case LAGG_PROTO_LOADBALANCE:
		case LAGG_PROTO_BROADCAST:
			speed = 0;
			LAGG_RLOCK();
			CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries)
				speed += lp->lp_ifp->if_baudrate;
			LAGG_RUNLOCK();
			sc->sc_ifp->if_baudrate = speed;
			break;
		case LAGG_PROTO_LACP:
			/* LACP updates if_baudrate itself */
			break;
	}
}

static void
lagg_port_state(struct ifnet *ifp, int state)
{
	struct lagg_port *lp = (struct lagg_port *)ifp->if_lagg;
	struct lagg_softc *sc = NULL;

	if (lp != NULL)
		sc = lp->lp_softc;
	if (sc == NULL)
		return;

	LAGG_XLOCK(sc);
	lagg_linkstate(sc);
	lagg_proto_linkstate(sc, lp);
	LAGG_XUNLOCK(sc);
}

struct lagg_port *
lagg_link_active(struct lagg_softc *sc, struct lagg_port *lp)
{
	struct lagg_port *lp_next, *rval = NULL;
	struct epoch_tracker net_et;

	/*
	 * Search a port which reports an active link state.
	 */

	if (lp == NULL)
		goto search;
	if (LAGG_PORTACTIVE(lp)) {
		rval = lp;
		goto found;
	}
	if ((lp_next = CK_SLIST_NEXT(lp, lp_entries)) != NULL &&
	    LAGG_PORTACTIVE(lp_next)) {
		rval = lp_next;
		goto found;
	}

 search:
	epoch_enter_preempt(net_epoch_preempt, &net_et);
	CK_SLIST_FOREACH(lp_next, &sc->sc_ports, lp_entries) {
		if (LAGG_PORTACTIVE(lp_next)) {
			epoch_exit_preempt(net_epoch_preempt, &net_et);
			return (lp_next);
		}
	}
	epoch_exit_preempt(net_epoch_preempt, &net_et);
found:
	return (rval);
}

int
lagg_enqueue(struct ifnet *ifp, struct mbuf *m)
{

	return (ifp->if_transmit)(ifp, m);
}

/*
 * Simple round robin aggregation
 */
static void
lagg_rr_attach(struct lagg_softc *sc)
{
	sc->sc_seq = 0;
	sc->sc_bkt_count = sc->sc_bkt;
}

static int
lagg_rr_start(struct lagg_softc *sc, struct mbuf *m)
{
	struct lagg_port *lp;
	uint32_t p;

	if (sc->sc_bkt_count == 0 && sc->sc_bkt > 0)
		sc->sc_bkt_count = sc->sc_bkt;

	if (sc->sc_bkt > 0) {
		atomic_subtract_int(&sc->sc_bkt_count, 1);
	if (atomic_cmpset_int(&sc->sc_bkt_count, 0, sc->sc_bkt))
		p = atomic_fetchadd_32(&sc->sc_seq, 1);
	else
		p = sc->sc_seq; 
	} else
		p = atomic_fetchadd_32(&sc->sc_seq, 1);

	p %= sc->sc_count;
	lp = CK_SLIST_FIRST(&sc->sc_ports);

	while (p--)
		lp = CK_SLIST_NEXT(lp, lp_entries);

	/*
	 * Check the port's link state. This will return the next active
	 * port if the link is down or the port is NULL.
	 */
	if ((lp = lagg_link_active(sc, lp)) == NULL) {
		m_freem(m);
		return (ENETDOWN);
	}

	/* Send mbuf */
	return (lagg_enqueue(lp->lp_ifp, m));
}

static struct mbuf *
lagg_rr_input(struct lagg_softc *sc, struct lagg_port *lp, struct mbuf *m)
{
	struct ifnet *ifp = sc->sc_ifp;

	/* Just pass in the packet to our lagg device */
	m->m_pkthdr.rcvif = ifp;

	return (m);
}

/*
 * Broadcast mode
 */
static int
lagg_bcast_start(struct lagg_softc *sc, struct mbuf *m)
{
	int active_ports = 0;
	int errors = 0;
	int ret;
	struct lagg_port *lp, *last = NULL;
	struct mbuf *m0;

	LAGG_RLOCK();
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries) {
		if (!LAGG_PORTACTIVE(lp))
			continue;

		active_ports++;

		if (last != NULL) {
			m0 = m_copym(m, 0, M_COPYALL, M_NOWAIT);
			if (m0 == NULL) {
				ret = ENOBUFS;
				errors++;
				break;
			}

			ret = lagg_enqueue(last->lp_ifp, m0);
			if (ret != 0)
				errors++;
		}
		last = lp;
	}
	LAGG_RUNLOCK();

	if (last == NULL) {
		m_freem(m);
		return (ENOENT);
	}
	if ((last = lagg_link_active(sc, last)) == NULL) {
		m_freem(m);
		return (ENETDOWN);
	}

	ret = lagg_enqueue(last->lp_ifp, m);
	if (ret != 0)
		errors++;

	if (errors == 0)
		return (ret);

	return (0);
}

static struct mbuf*
lagg_bcast_input(struct lagg_softc *sc, struct lagg_port *lp, struct mbuf *m)
{
	struct ifnet *ifp = sc->sc_ifp;

	/* Just pass in the packet to our lagg device */
	m->m_pkthdr.rcvif = ifp;
	return (m);
}

/*
 * Active failover
 */
static int
lagg_fail_start(struct lagg_softc *sc, struct mbuf *m)
{
	struct lagg_port *lp;

	/* Use the master port if active or the next available port */
	if ((lp = lagg_link_active(sc, sc->sc_primary)) == NULL) {
		m_freem(m);
		return (ENETDOWN);
	}

	/* Send mbuf */
	return (lagg_enqueue(lp->lp_ifp, m));
}

static struct mbuf *
lagg_fail_input(struct lagg_softc *sc, struct lagg_port *lp, struct mbuf *m)
{
	struct ifnet *ifp = sc->sc_ifp;
	struct lagg_port *tmp_tp;

	if (lp == sc->sc_primary || V_lagg_failover_rx_all) {
		m->m_pkthdr.rcvif = ifp;
		return (m);
	}

	if (!LAGG_PORTACTIVE(sc->sc_primary)) {
		tmp_tp = lagg_link_active(sc, sc->sc_primary);
		/*
		 * If tmp_tp is null, we've received a packet when all
		 * our links are down. Weird, but process it anyways.
		 */
		if ((tmp_tp == NULL || tmp_tp == lp)) {
			m->m_pkthdr.rcvif = ifp;
			return (m);
		}
	}

	m_freem(m);
	return (NULL);
}

/*
 * Loadbalancing
 */
static void
lagg_lb_attach(struct lagg_softc *sc)
{
	struct lagg_port *lp;
	struct lagg_lb *lb;

	LAGG_XLOCK_ASSERT(sc);
	lb = malloc(sizeof(struct lagg_lb), M_DEVBUF, M_WAITOK | M_ZERO);
	lb->lb_key = m_ether_tcpip_hash_init();
	sc->sc_psc = lb;

	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries)
		lagg_lb_port_create(lp);
}

static void
lagg_lb_detach(struct lagg_softc *sc)
{
	struct lagg_lb *lb;

	lb = (struct lagg_lb *)sc->sc_psc;
	if (lb != NULL)
		free(lb, M_DEVBUF);
}

static int
lagg_lb_porttable(struct lagg_softc *sc, struct lagg_port *lp)
{
	struct lagg_lb *lb = (struct lagg_lb *)sc->sc_psc;
	struct lagg_port *lp_next;
	int i = 0, rv;

	rv = 0;
	bzero(&lb->lb_ports, sizeof(lb->lb_ports));
	LAGG_RLOCK();
	CK_SLIST_FOREACH(lp_next, &sc->sc_ports, lp_entries) {
		if (lp_next == lp)
			continue;
		if (i >= LAGG_MAX_PORTS) {
			rv = EINVAL;
			break;
		}
		if (sc->sc_ifflags & IFF_DEBUG)
			printf("%s: port %s at index %d\n",
			    sc->sc_ifname, lp_next->lp_ifp->if_xname, i);
		lb->lb_ports[i++] = lp_next;
	}
	LAGG_RUNLOCK();

	return (rv);
}

static int
lagg_lb_port_create(struct lagg_port *lp)
{
	struct lagg_softc *sc = lp->lp_softc;
	return (lagg_lb_porttable(sc, NULL));
}

static void
lagg_lb_port_destroy(struct lagg_port *lp)
{
	struct lagg_softc *sc = lp->lp_softc;
	lagg_lb_porttable(sc, lp);
}

static int
lagg_lb_start(struct lagg_softc *sc, struct mbuf *m)
{
	struct lagg_lb *lb = (struct lagg_lb *)sc->sc_psc;
	struct lagg_port *lp = NULL;
	uint32_t p = 0;

	if ((sc->sc_opts & LAGG_OPT_USE_FLOWID) &&
	    M_HASHTYPE_GET(m) != M_HASHTYPE_NONE)
		p = m->m_pkthdr.flowid >> sc->flowid_shift;
	else
		p = m_ether_tcpip_hash(sc->sc_flags, m, lb->lb_key);
	p %= sc->sc_count;
	lp = lb->lb_ports[p];

	/*
	 * Check the port's link state. This will return the next active
	 * port if the link is down or the port is NULL.
	 */
	if ((lp = lagg_link_active(sc, lp)) == NULL) {
		m_freem(m);
		return (ENETDOWN);
	}

	/* Send mbuf */
	return (lagg_enqueue(lp->lp_ifp, m));
}

static struct mbuf *
lagg_lb_input(struct lagg_softc *sc, struct lagg_port *lp, struct mbuf *m)
{
	struct ifnet *ifp = sc->sc_ifp;

	/* Just pass in the packet to our lagg device */
	m->m_pkthdr.rcvif = ifp;

	return (m);
}

/*
 * 802.3ad LACP
 */
static void
lagg_lacp_attach(struct lagg_softc *sc)
{
	struct lagg_port *lp;

	lacp_attach(sc);
	LAGG_XLOCK_ASSERT(sc);
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries)
		lacp_port_create(lp);
}

static void
lagg_lacp_detach(struct lagg_softc *sc)
{
	struct lagg_port *lp;
	void *psc;

	LAGG_XLOCK_ASSERT(sc);
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries)
		lacp_port_destroy(lp);

	psc = sc->sc_psc;
	sc->sc_psc = NULL;
	lacp_detach(psc);
}

static void
lagg_lacp_lladdr(struct lagg_softc *sc)
{
	struct lagg_port *lp;

	LAGG_SXLOCK_ASSERT(sc);

	/* purge all the lacp ports */
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries)
		lacp_port_destroy(lp);

	/* add them back in */
	CK_SLIST_FOREACH(lp, &sc->sc_ports, lp_entries)
		lacp_port_create(lp);
}

static int
lagg_lacp_start(struct lagg_softc *sc, struct mbuf *m)
{
	struct lagg_port *lp;

	lp = lacp_select_tx_port(sc, m);
	if (lp == NULL) {
		m_freem(m);
		return (ENETDOWN);
	}

	/* Send mbuf */
	return (lagg_enqueue(lp->lp_ifp, m));
}

static struct mbuf *
lagg_lacp_input(struct lagg_softc *sc, struct lagg_port *lp, struct mbuf *m)
{
	struct ifnet *ifp = sc->sc_ifp;
	struct ether_header *eh;
	u_short etype;

	eh = mtod(m, struct ether_header *);
	etype = ntohs(eh->ether_type);

	/* Tap off LACP control messages */
	if ((m->m_flags & M_VLANTAG) == 0 && etype == ETHERTYPE_SLOW) {
		m = lacp_input(lp, m);
		if (m == NULL)
			return (NULL);
	}

	/*
	 * If the port is not collecting or not in the active aggregator then
	 * free and return.
	 */
	if (lacp_iscollecting(lp) == 0 || lacp_isactive(lp) == 0) {
		m_freem(m);
		return (NULL);
	}

	m->m_pkthdr.rcvif = ifp;
	return (m);
}


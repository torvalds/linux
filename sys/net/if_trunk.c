/*	$OpenBSD: if_trunk.c,v 1.157 2025/07/07 02:28:50 jsg Exp $	*/

/*
 * Copyright (c) 2005, 2006, 2007 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/task.h>

#include <crypto/siphash.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#ifdef INET6
#include <netinet/ip6.h>
#endif

#include <net/if_trunk.h>
#include <net/trunklacp.h>

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

SLIST_HEAD(__trhead, trunk_softc) trunk_list;	/* list of trunks */

void	 trunkattach(int);
int	 trunk_clone_create(struct if_clone *, int);
int	 trunk_clone_destroy(struct ifnet *);
void	 trunk_lladdr(struct arpcom *, u_int8_t *);
int	 trunk_capabilities(struct trunk_softc *);
void	 trunk_port_lladdr(struct trunk_port *, u_int8_t *);
int	 trunk_port_create(struct trunk_softc *, struct ifnet *);
int	 trunk_port_destroy(struct trunk_port *);
void	 trunk_port_state(void *);
void	 trunk_port_ifdetach(void *);
int	 trunk_port_ioctl(struct ifnet *, u_long, caddr_t);
int	 trunk_port_output(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);
struct trunk_port *trunk_port_get(struct trunk_softc *, struct ifnet *);
int	 trunk_port_checkstacking(struct trunk_softc *);
void	 trunk_port2req(struct trunk_port *, struct trunk_reqport *);
int	 trunk_ioctl(struct ifnet *, u_long, caddr_t);
int	 trunk_ether_addmulti(struct trunk_softc *, struct ifreq *);
int	 trunk_ether_delmulti(struct trunk_softc *, struct ifreq *);
void	 trunk_ether_purgemulti(struct trunk_softc *);
int	 trunk_ether_cmdmulti(struct trunk_port *, u_long);
int	 trunk_ioctl_allports(struct trunk_softc *, u_long, caddr_t);
void	 trunk_input(struct ifnet *, struct mbuf *, struct netstack *);
void	 trunk_start(struct ifnet *);
void	 trunk_init(struct ifnet *);
void	 trunk_stop(struct ifnet *);
int	 trunk_media_change(struct ifnet *);
void	 trunk_media_status(struct ifnet *, struct ifmediareq *);
struct trunk_port *trunk_link_active(struct trunk_softc *,
	    struct trunk_port *);
const void *trunk_gethdr(struct mbuf *, u_int, u_int, void *);

struct if_clone trunk_cloner =
    IF_CLONE_INITIALIZER("trunk", trunk_clone_create, trunk_clone_destroy);

/* Simple round robin */
int	 trunk_rr_attach(struct trunk_softc *);
int	 trunk_rr_detach(struct trunk_softc *);
void	 trunk_rr_port_destroy(struct trunk_port *);
int	 trunk_rr_start(struct trunk_softc *, struct mbuf *);
int	 trunk_rr_input(struct trunk_softc *, struct trunk_port *,
	    struct mbuf *);

/* Active failover */
int	 trunk_fail_attach(struct trunk_softc *);
int	 trunk_fail_detach(struct trunk_softc *);
int	 trunk_fail_port_create(struct trunk_port *);
void	 trunk_fail_port_destroy(struct trunk_port *);
int	 trunk_fail_start(struct trunk_softc *, struct mbuf *);
int	 trunk_fail_input(struct trunk_softc *, struct trunk_port *,
	    struct mbuf *);
void	 trunk_fail_linkstate(struct trunk_port *);

/* Loadbalancing */
int	 trunk_lb_attach(struct trunk_softc *);
int	 trunk_lb_detach(struct trunk_softc *);
int	 trunk_lb_port_create(struct trunk_port *);
void	 trunk_lb_port_destroy(struct trunk_port *);
int	 trunk_lb_start(struct trunk_softc *, struct mbuf *);
int	 trunk_lb_input(struct trunk_softc *, struct trunk_port *,
	    struct mbuf *);
int	 trunk_lb_porttable(struct trunk_softc *, struct trunk_port *);

/* Broadcast mode */
int	 trunk_bcast_attach(struct trunk_softc *);
int	 trunk_bcast_detach(struct trunk_softc *);
int	 trunk_bcast_start(struct trunk_softc *, struct mbuf *);
int	 trunk_bcast_input(struct trunk_softc *, struct trunk_port *,
	    struct mbuf *);

/* 802.3ad LACP */
int	 trunk_lacp_attach(struct trunk_softc *);
int	 trunk_lacp_detach(struct trunk_softc *);
int	 trunk_lacp_start(struct trunk_softc *, struct mbuf *);
int	 trunk_lacp_input(struct trunk_softc *, struct trunk_port *,
	    struct mbuf *);

/* Trunk protocol table */
static const struct {
	enum trunk_proto	ti_proto;
	int			(*ti_attach)(struct trunk_softc *);
} trunk_protos[] = {
	{ TRUNK_PROTO_ROUNDROBIN,	trunk_rr_attach },
	{ TRUNK_PROTO_FAILOVER,		trunk_fail_attach },
	{ TRUNK_PROTO_LOADBALANCE,	trunk_lb_attach },
	{ TRUNK_PROTO_BROADCAST,	trunk_bcast_attach },
	{ TRUNK_PROTO_LACP,		trunk_lacp_attach },
	{ TRUNK_PROTO_NONE,		NULL }
};

void
trunkattach(int count)
{
	SLIST_INIT(&trunk_list);
	if_clone_attach(&trunk_cloner);
}

int
trunk_clone_create(struct if_clone *ifc, int unit)
{
	struct trunk_softc *tr;
	struct ifnet *ifp;
	int i, error = 0;

	tr = malloc(sizeof(*tr), M_DEVBUF, M_WAITOK|M_ZERO);
	tr->tr_proto = TRUNK_PROTO_NONE;
	for (i = 0; trunk_protos[i].ti_proto != TRUNK_PROTO_NONE; i++) {
		if (trunk_protos[i].ti_proto == TRUNK_PROTO_DEFAULT) {
			tr->tr_proto = trunk_protos[i].ti_proto;
			if ((error = trunk_protos[i].ti_attach(tr)) != 0) {
				free(tr, M_DEVBUF, sizeof *tr);
				return (error);
			}
			break;
		}
	}
	SLIST_INIT(&tr->tr_ports);

	/* Initialise pseudo media types */
	ifmedia_init(&tr->tr_media, 0, trunk_media_change,
	    trunk_media_status);
	ifmedia_add(&tr->tr_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&tr->tr_media, IFM_ETHER | IFM_AUTO);

	ifp = &tr->tr_ac.ac_if;
	ifp->if_softc = tr;
	ifp->if_start = trunk_start;
	ifp->if_ioctl = trunk_ioctl;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_capabilities = trunk_capabilities(tr);
	ifp->if_xflags = IFXF_CLONED;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	/*
	 * Attach as an ordinary ethernet device, children will be attached
	 * as special device IFT_IEEE8023ADLAG.
	 */
	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

	/* Insert into the global list of trunks */
	SLIST_INSERT_HEAD(&trunk_list, tr, tr_entries);

	return (0);
}

int
trunk_clone_destroy(struct ifnet *ifp)
{
	struct trunk_softc *tr = (struct trunk_softc *)ifp->if_softc;
	struct trunk_port *tp;
	int error;

	/* Remove any multicast groups that we may have joined. */
	trunk_ether_purgemulti(tr);

	/* Shutdown and remove trunk ports, return on error */
	NET_LOCK();
	while ((tp = SLIST_FIRST(&tr->tr_ports)) != NULL) {
		if ((error = trunk_port_destroy(tp)) != 0) {
			NET_UNLOCK();
			return (error);
		}
	}
	NET_UNLOCK();

	ifmedia_delete_instance(&tr->tr_media, IFM_INST_ANY);
	ether_ifdetach(ifp);
	if_detach(ifp);

	SLIST_REMOVE(&trunk_list, tr, trunk_softc, tr_entries);
	free(tr, M_DEVBUF, sizeof *tr);

	return (0);
}

void
trunk_lladdr(struct arpcom *ac, u_int8_t *lladdr)
{
	struct ifnet *ifp = &ac->ac_if;
	struct sockaddr_dl *sdl;

	sdl = ifp->if_sadl;
	sdl->sdl_type = IFT_ETHER;
	sdl->sdl_alen = ETHER_ADDR_LEN;
	bcopy(lladdr, LLADDR(sdl), ETHER_ADDR_LEN);
	bcopy(lladdr, ac->ac_enaddr, ETHER_ADDR_LEN);
}

int
trunk_capabilities(struct trunk_softc *tr)
{
	struct trunk_port *tp;
	int cap = ~0, priv;

	/* Preserve private capabilities */
	priv = tr->tr_capabilities & IFCAP_TRUNK_MASK;

	/* Get capabilities from the trunk ports */
	SLIST_FOREACH(tp, &tr->tr_ports, tp_entries)
		cap &= tp->tp_capabilities;

	if (tr->tr_ifflags & IFF_DEBUG) {
		printf("%s: capabilities 0x%08x\n",
		    tr->tr_ifname, cap == ~0 ? priv : (cap | priv));
	}

	return (cap == ~0 ? priv : (cap | priv));
}

void
trunk_port_lladdr(struct trunk_port *tp, u_int8_t *lladdr)
{
	struct ifnet *ifp = tp->tp_if;

	/* Set the link layer address */
	trunk_lladdr((struct arpcom *)ifp, lladdr);

	/* Reset the port to update the lladdr */
	ifnewlladdr(ifp);
}

int
trunk_port_create(struct trunk_softc *tr, struct ifnet *ifp)
{
	struct trunk_softc *tr_ptr;
	struct trunk_port *tp;
	struct arpcom *ac0;
	int error = 0;

	/* Limit the maximal number of trunk ports */
	if (tr->tr_count >= TRUNK_MAX_PORTS)
		return (ENOSPC);

	/* Check if port has already been associated to a trunk */
	if (trunk_port_get(NULL, ifp) != NULL)
		return (EBUSY);

	/* XXX Disallow non-ethernet interfaces (this should be any of 802) */
	if (ifp->if_type != IFT_ETHER)
		return (EPROTONOSUPPORT);

	ac0 = (struct arpcom *)ifp;
	if (ac0->ac_trunkport != NULL)
		return (EBUSY);

	/* Take MTU from the first member port */
	if (SLIST_EMPTY(&tr->tr_ports)) {
		if (tr->tr_ifflags & IFF_DEBUG)
			printf("%s: first port, setting trunk mtu %u\n",
			    tr->tr_ifname, ifp->if_mtu);
		tr->tr_ac.ac_if.if_mtu = ifp->if_mtu;
		tr->tr_ac.ac_if.if_hardmtu = ifp->if_hardmtu;
	} else if (tr->tr_ac.ac_if.if_mtu != ifp->if_mtu) {
		printf("%s: adding %s failed, MTU %u != %u\n", tr->tr_ifname,
		    ifp->if_xname, ifp->if_mtu, tr->tr_ac.ac_if.if_mtu);
		return (EINVAL);
	}

	if ((error = ifpromisc(ifp, 1)) != 0)
		return (error);

	if ((tp = malloc(sizeof *tp, M_DEVBUF, M_NOWAIT|M_ZERO)) == NULL)
		return (ENOMEM);

	/* Check if port is a stacked trunk */
	SLIST_FOREACH(tr_ptr, &trunk_list, tr_entries) {
		if (ifp == &tr_ptr->tr_ac.ac_if) {
			tp->tp_flags |= TRUNK_PORT_STACK;
			if (trunk_port_checkstacking(tr_ptr) >=
			    TRUNK_MAX_STACKING) {
				free(tp, M_DEVBUF, sizeof *tp);
				return (E2BIG);
			}
		}
	}

	/* Change the interface type */
	tp->tp_iftype = ifp->if_type;
	ifp->if_type = IFT_IEEE8023ADLAG;

	tp->tp_ioctl = ifp->if_ioctl;
	ifp->if_ioctl = trunk_port_ioctl;

	tp->tp_output = ifp->if_output;
	ifp->if_output = trunk_port_output;

	tp->tp_if = ifp;
	tp->tp_trunk = tr;

	/* Save port link layer address */
	bcopy(((struct arpcom *)ifp)->ac_enaddr, tp->tp_lladdr, ETHER_ADDR_LEN);

	if (SLIST_EMPTY(&tr->tr_ports)) {
		tr->tr_primary = tp;
		tp->tp_flags |= TRUNK_PORT_MASTER;
		trunk_lladdr(&tr->tr_ac, tp->tp_lladdr);
	}

	/* Insert into the list of ports */
	SLIST_INSERT_HEAD(&tr->tr_ports, tp, tp_entries);
	tr->tr_count++;

	/* Update link layer address for this port */
	trunk_port_lladdr(tp,
	    ((struct arpcom *)(tr->tr_primary->tp_if))->ac_enaddr);

	/* Update trunk capabilities */
	tr->tr_capabilities = trunk_capabilities(tr);

	/* Add multicast addresses to this port */
	trunk_ether_cmdmulti(tp, SIOCADDMULTI);

	/* Register callback for physical link state changes */
	task_set(&tp->tp_ltask, trunk_port_state, tp);
	if_linkstatehook_add(ifp, &tp->tp_ltask);

	/* Register callback if parent wants to unregister */
	task_set(&tp->tp_dtask, trunk_port_ifdetach, tp);
	if_detachhook_add(ifp, &tp->tp_dtask);

	if (tr->tr_port_create != NULL)
		error = (*tr->tr_port_create)(tp);

	/* Change input handler of the physical interface. */
	tp->tp_input = ifp->if_input;
	NET_ASSERT_LOCKED();
	ac0->ac_trunkport = tp;
	ifp->if_input = trunk_input;

	return (error);
}

int
trunk_port_checkstacking(struct trunk_softc *tr)
{
	struct trunk_softc *tr_ptr;
	struct trunk_port *tp;
	int m = 0;

	SLIST_FOREACH(tp, &tr->tr_ports, tp_entries) {
		if (tp->tp_flags & TRUNK_PORT_STACK) {
			tr_ptr = (struct trunk_softc *)tp->tp_if->if_softc;
			m = MAX(m, trunk_port_checkstacking(tr_ptr));
		}
	}

	return (m + 1);
}

int
trunk_port_destroy(struct trunk_port *tp)
{
	struct trunk_softc *tr = (struct trunk_softc *)tp->tp_trunk;
	struct trunk_port *tp_ptr;
	struct ifnet *ifp = tp->tp_if;
	struct arpcom *ac0 = (struct arpcom *)ifp;

	/* Restore previous input handler. */
	NET_ASSERT_LOCKED();
	ifp->if_input = tp->tp_input;
	ac0->ac_trunkport = NULL;

	/* Remove multicast addresses from this port */
	trunk_ether_cmdmulti(tp, SIOCDELMULTI);

	ifpromisc(ifp, 0);

	if (tr->tr_port_destroy != NULL)
		(*tr->tr_port_destroy)(tp);

	/* Restore interface type. */
	ifp->if_type = tp->tp_iftype;

	ifp->if_ioctl = tp->tp_ioctl;
	ifp->if_output = tp->tp_output;

	if_detachhook_del(ifp, &tp->tp_dtask);
	if_linkstatehook_del(ifp, &tp->tp_ltask);

	/* Finally, remove the port from the trunk */
	SLIST_REMOVE(&tr->tr_ports, tp, trunk_port, tp_entries);
	tr->tr_count--;

	/* Update the primary interface */
	if (tp == tr->tr_primary) {
		u_int8_t lladdr[ETHER_ADDR_LEN];

		if ((tp_ptr = SLIST_FIRST(&tr->tr_ports)) == NULL) {
			bzero(&lladdr, ETHER_ADDR_LEN);
		} else {
			bcopy(((struct arpcom *)tp_ptr->tp_if)->ac_enaddr,
			    lladdr, ETHER_ADDR_LEN);
			tp_ptr->tp_flags = TRUNK_PORT_MASTER;
		}
		trunk_lladdr(&tr->tr_ac, lladdr);
		tr->tr_primary = tp_ptr;

		/* Update link layer address for each port */
		SLIST_FOREACH(tp_ptr, &tr->tr_ports, tp_entries)
			trunk_port_lladdr(tp_ptr, lladdr);
	}

	/* Reset the port lladdr */
	trunk_port_lladdr(tp, tp->tp_lladdr);

	if_put(ifp);
	free(tp, M_DEVBUF, sizeof *tp);

	/* Update trunk capabilities */
	tr->tr_capabilities = trunk_capabilities(tr);

	return (0);
}

int
trunk_port_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct trunk_reqport *rp = (struct trunk_reqport *)data;
	struct trunk_softc *tr;
	struct trunk_port *tp = NULL;
	struct ifnet *ifp0 = NULL;
	int error = 0;

	/* Should be checked by the caller */
	if (ifp->if_type != IFT_IEEE8023ADLAG ||
	    (tp = trunk_port_get(NULL, ifp)) == NULL ||
	    (tr = (struct trunk_softc *)tp->tp_trunk) == NULL) {
		error = EINVAL;
		goto fallback;
	}

	switch (cmd) {
	case SIOCGTRUNKPORT:
		if (rp->rp_portname[0] == '\0' ||
		    (ifp0 = if_unit(rp->rp_portname)) != ifp) {
			if_put(ifp0);
			error = EINVAL;
			break;
		}
		if_put(ifp0);

		/* Search in all trunks if the global flag is set */
		if ((tp = trunk_port_get(rp->rp_flags & TRUNK_PORT_GLOBAL ?
		    NULL : tr, ifp)) == NULL) {
			error = ENOENT;
			break;
		}

		trunk_port2req(tp, rp);
		break;
	case SIOCSIFMTU:
		/* Do not allow the MTU to be changed once joined */
		error = EINVAL;
		break;
	default:
		error = ENOTTY;
		goto fallback;
	}

	return (error);

 fallback:
	if (tp != NULL)
		error = (*tp->tp_ioctl)(ifp, cmd, data);

	return (error);
}

int
trunk_port_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	/* restrict transmission on trunk members to bpf only */
	if (ifp->if_type == IFT_IEEE8023ADLAG &&
	    (m_tag_find(m, PACKET_TAG_DLT, NULL) == NULL)) {
		m_freem(m);
		return (EBUSY);
	}

	return (ether_output(ifp, m, dst, rt));
}

void
trunk_port_ifdetach(void *arg)
{
	struct trunk_port *tp = (struct trunk_port *)arg;

	trunk_port_destroy(tp);
}

struct trunk_port *
trunk_port_get(struct trunk_softc *tr, struct ifnet *ifp)
{
	struct trunk_port *tp;
	struct trunk_softc *tr_ptr;

	if (tr != NULL) {
		/* Search port in specified trunk */
		SLIST_FOREACH(tp, &tr->tr_ports, tp_entries) {
			if (tp->tp_if == ifp)
				return (tp);
		}
	} else {
		/* Search all trunks for the selected port */
		SLIST_FOREACH(tr_ptr, &trunk_list, tr_entries) {
			SLIST_FOREACH(tp, &tr_ptr->tr_ports, tp_entries) {
				if (tp->tp_if == ifp)
					return (tp);
			}
		}
	}

	return (NULL);
}

void
trunk_port2req(struct trunk_port *tp, struct trunk_reqport *rp)
{
	struct trunk_softc *tr = (struct trunk_softc *)tp->tp_trunk;

	strlcpy(rp->rp_ifname, tr->tr_ifname, sizeof(rp->rp_ifname));
	strlcpy(rp->rp_portname, tp->tp_if->if_xname, sizeof(rp->rp_portname));
	rp->rp_prio = tp->tp_prio;
	if (tr->tr_portreq != NULL)
		(*tr->tr_portreq)(tp, (caddr_t)&rp->rp_psc);

	/* Add protocol specific flags */
	switch (tr->tr_proto) {
	case TRUNK_PROTO_FAILOVER:
		rp->rp_flags = tp->tp_flags;
		if (tp == trunk_link_active(tr, tr->tr_primary))
			rp->rp_flags |= TRUNK_PORT_ACTIVE;
		break;

	case TRUNK_PROTO_ROUNDROBIN:
	case TRUNK_PROTO_LOADBALANCE:
	case TRUNK_PROTO_BROADCAST:
		rp->rp_flags = tp->tp_flags;
		if (TRUNK_PORTACTIVE(tp))
			rp->rp_flags |= TRUNK_PORT_ACTIVE;
		break;

	case TRUNK_PROTO_LACP:
		/* LACP has a different definition of active */
		rp->rp_flags = lacp_port_status(tp);
		break;
	default:
		break;
	}
}

int
trunk_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct trunk_softc *tr = (struct trunk_softc *)ifp->if_softc;
	struct trunk_reqall *ra = (struct trunk_reqall *)data;
	struct trunk_reqport *rp = (struct trunk_reqport *)data, rpbuf;
	struct trunk_opts *tro = (struct trunk_opts *)data;
	struct ifreq *ifr = (struct ifreq *)data;
	struct lacp_softc *lsc;
	struct trunk_port *tp;
	struct lacp_port *lp;
	struct ifnet *tpif;
	int i, error = 0;

	bzero(&rpbuf, sizeof(rpbuf));

	switch (cmd) {
	case SIOCGTRUNK:
		ra->ra_proto = tr->tr_proto;
		if (tr->tr_req != NULL)
			(*tr->tr_req)(tr, (caddr_t)&ra->ra_psc);
		ra->ra_ports = i = 0;
		tp = SLIST_FIRST(&tr->tr_ports);
		while (tp && ra->ra_size >=
		    i + sizeof(struct trunk_reqport)) {
			trunk_port2req(tp, &rpbuf);
			error = copyout(&rpbuf, (caddr_t)ra->ra_port + i,
			    sizeof(struct trunk_reqport));
			if (error)
				break;
			i += sizeof(struct trunk_reqport);
			ra->ra_ports++;
			tp = SLIST_NEXT(tp, tp_entries);
		}
		break;
	case SIOCSTRUNK:
		if ((error = suser(curproc)) != 0) {
			error = EPERM;
			break;
		}
		if (ra->ra_proto >= TRUNK_PROTO_MAX) {
			error = EPROTONOSUPPORT;
			break;
		}

		/*
		 * Use of ifp->if_input and ac->ac_trunkport is
		 * protected by NET_LOCK, but that may not be true
		 * in the future. The below comment and code flow is
		 * maintained to help in that future.
		 *
		 * Serialize modifications to the trunk and trunk
		 * ports via the ifih SRP: detaching trunk_input
		 * from the trunk port will require all currently
		 * running trunk_input's on this port to finish
		 * granting us an exclusive access to it.
		 */
		NET_ASSERT_LOCKED();
		SLIST_FOREACH(tp, &tr->tr_ports, tp_entries) {
			/* if_ih_remove(tp->tp_if, trunk_input, tp); */
			tp->tp_if->if_input = tp->tp_input;
		}
		if (tr->tr_proto != TRUNK_PROTO_NONE)
			error = tr->tr_detach(tr);
		if (error != 0)
			break;
		for (i = 0; i < nitems(trunk_protos); i++) {
			if (trunk_protos[i].ti_proto == ra->ra_proto) {
				if (tr->tr_ifflags & IFF_DEBUG)
					printf("%s: using proto %u\n",
					    tr->tr_ifname,
					    trunk_protos[i].ti_proto);
				tr->tr_proto = trunk_protos[i].ti_proto;
				if (tr->tr_proto != TRUNK_PROTO_NONE)
					error = trunk_protos[i].ti_attach(tr);
				SLIST_FOREACH(tp, &tr->tr_ports, tp_entries) {
					/* if_ih_insert(tp->tp_if,
					    trunk_input, tp); */
					tp->tp_if->if_input = trunk_input;
				}
				/* Update trunk capabilities */
				tr->tr_capabilities = trunk_capabilities(tr);
				goto out;
			}
		}
		error = EPROTONOSUPPORT;
		break;
	case SIOCGTRUNKOPTS:
		/* Only LACP trunks have options atm */
		if (tro->to_proto != TRUNK_PROTO_LACP) {
			error = EPROTONOSUPPORT;
			break;
		}
		lsc = LACP_SOFTC(tr);
		tro->to_lacpopts.lacp_mode = lsc->lsc_mode;
		tro->to_lacpopts.lacp_timeout = lsc->lsc_timeout;
		tro->to_lacpopts.lacp_prio = lsc->lsc_sys_prio;
		tro->to_lacpopts.lacp_portprio = lsc->lsc_port_prio;
		tro->to_lacpopts.lacp_ifqprio = lsc->lsc_ifq_prio;
		break;
	case SIOCSTRUNKOPTS:
		if ((error = suser(curproc)) != 0) {
			error = EPERM;
			break;
		}
		/* Only LACP trunks have options atm */
		if (tro->to_proto != TRUNK_PROTO_LACP) {
			error = EPROTONOSUPPORT;
			break;
		}
		lsc = LACP_SOFTC(tr);
		switch(tro->to_opts) {
			case TRUNK_OPT_LACP_MODE:
				/*
				 * Ensure mode changes occur immediately
				 * on all ports
				 */
				lsc->lsc_mode = tro->to_lacpopts.lacp_mode;
				if (lsc->lsc_mode == 0) {
					LIST_FOREACH(lp, &lsc->lsc_ports,
					    lp_next)
						lp->lp_state &=
						    ~LACP_STATE_ACTIVITY;
				} else {
					LIST_FOREACH(lp, &lsc->lsc_ports,
					    lp_next)
						lp->lp_state |=
						    LACP_STATE_ACTIVITY;
				}
				break;
			case TRUNK_OPT_LACP_TIMEOUT:
				/*
				 * Ensure timeout changes occur immediately
				 * on all ports
				 */
				lsc->lsc_timeout =
				    tro->to_lacpopts.lacp_timeout;
				if (lsc->lsc_timeout == 0) {
					LIST_FOREACH(lp, &lsc->lsc_ports,
					    lp_next)
						lp->lp_state &=
						    ~LACP_STATE_TIMEOUT;
				} else {
					LIST_FOREACH(lp, &lsc->lsc_ports,
					    lp_next)
						lp->lp_state |=
						    LACP_STATE_TIMEOUT;
				}
				break;
			case TRUNK_OPT_LACP_SYS_PRIO:
				if (tro->to_lacpopts.lacp_prio == 0) {
					error = EINVAL;	
					break;
				}
				lsc->lsc_sys_prio = tro->to_lacpopts.lacp_prio;
				break;
			case TRUNK_OPT_LACP_PORT_PRIO:
				if (tro->to_lacpopts.lacp_portprio == 0) {
					error = EINVAL;	
					break;
				}
				lsc->lsc_port_prio =
				    tro->to_lacpopts.lacp_portprio;
				break;
			case TRUNK_OPT_LACP_IFQ_PRIO:
				if (tro->to_lacpopts.lacp_ifqprio >
				    IFQ_MAXPRIO) {
					error = EINVAL;	
					break;
				}
				lsc->lsc_ifq_prio =
				    tro->to_lacpopts.lacp_ifqprio;
				break;
		}
		break;
	case SIOCGTRUNKPORT:
		if (rp->rp_portname[0] == '\0' ||
		    (tpif = if_unit(rp->rp_portname)) == NULL) {
			error = EINVAL;
			break;
		}

		/* Search in all trunks if the global flag is set */
		tp = trunk_port_get(rp->rp_flags & TRUNK_PORT_GLOBAL ?
		    NULL : tr, tpif);
		if_put(tpif);

		if(tp == NULL) {
			error = ENOENT;
			break;
		}

		trunk_port2req(tp, rp);
		break;
	case SIOCSTRUNKPORT:
		if ((error = suser(curproc)) != 0) {
			error = EPERM;
			break;
		}
		if (rp->rp_portname[0] == '\0' ||
		    (tpif = if_unit(rp->rp_portname)) == NULL) {
			error = EINVAL;
			break;
		}
		error = trunk_port_create(tr, tpif);
		if (error != 0)
			if_put(tpif);
		break;
	case SIOCSTRUNKDELPORT:
		if ((error = suser(curproc)) != 0) {
			error = EPERM;
			break;
		}
		if (rp->rp_portname[0] == '\0' ||
		    (tpif = if_unit(rp->rp_portname)) == NULL) {
			error = EINVAL;
			break;
		}

		/* Search in all trunks if the global flag is set */
		tp = trunk_port_get(rp->rp_flags & TRUNK_PORT_GLOBAL ?
		    NULL : tr, tpif);
		if_put(tpif);

		if(tp == NULL) {
			error = ENOENT;
			break;
		}

		error = trunk_port_destroy(tp);
		break;
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		error = ENETRESET;
		break;

	case SIOCSIFXFLAGS:
		SLIST_FOREACH(tp, &tr->tr_ports, tp_entries)
			ifsetlro(tp->tp_if, ISSET(ifr->ifr_flags, IFXF_LRO));
		break;

	case SIOCADDMULTI:
		error = trunk_ether_addmulti(tr, ifr);
		break;
	case SIOCDELMULTI:
		error = trunk_ether_delmulti(tr, ifr);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &tr->tr_media, cmd);
		break;
	case SIOCSIFLLADDR:
		/* Update the port lladdrs as well */
		SLIST_FOREACH(tp, &tr->tr_ports, tp_entries)
			trunk_port_lladdr(tp, ifr->ifr_addr.sa_data);
		error = ENETRESET;
		break;
	default:
		error = ether_ioctl(ifp, &tr->tr_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				trunk_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				trunk_stop(ifp);
		}
		error = 0;
	}

 out:
	return (error);
}

int
trunk_ether_addmulti(struct trunk_softc *tr, struct ifreq *ifr)
{
	struct trunk_mc *mc;
	u_int8_t addrlo[ETHER_ADDR_LEN], addrhi[ETHER_ADDR_LEN];
	int error;

	/* Ignore ENETRESET error code */
	if ((error = ether_addmulti(ifr, &tr->tr_ac)) != ENETRESET)
		return (error);

	if ((mc = malloc(sizeof(*mc), M_DEVBUF, M_NOWAIT)) == NULL) {
		error = ENOMEM;
		goto failed;
	}

	ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
	ETHER_LOOKUP_MULTI(addrlo, addrhi, &tr->tr_ac, mc->mc_enm);
	bcopy(&ifr->ifr_addr, &mc->mc_addr, ifr->ifr_addr.sa_len);
	SLIST_INSERT_HEAD(&tr->tr_mc_head, mc, mc_entries);

	if ((error = trunk_ioctl_allports(tr, SIOCADDMULTI,
	    (caddr_t)ifr)) != 0) {
		trunk_ether_delmulti(tr, ifr);
		return (error);
	}

	return (error);

 failed:
	ether_delmulti(ifr, &tr->tr_ac);

	return (error);
}

int
trunk_ether_delmulti(struct trunk_softc *tr, struct ifreq *ifr)
{
	struct ether_multi *enm;
	struct trunk_mc *mc;
	u_int8_t addrlo[ETHER_ADDR_LEN], addrhi[ETHER_ADDR_LEN];
	int error;

	if ((error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi)) != 0)
		return (error);
	ETHER_LOOKUP_MULTI(addrlo, addrhi, &tr->tr_ac, enm);
	if (enm == NULL)
		return (EINVAL);

	SLIST_FOREACH(mc, &tr->tr_mc_head, mc_entries)
		if (mc->mc_enm == enm)
			break;

	/* We won't delete entries we didn't add */
	if (mc == NULL)
		return (EINVAL);

	if ((error = ether_delmulti(ifr, &tr->tr_ac)) != ENETRESET)
		return (error);

	/* We no longer use this multicast address.  Tell parent so. */
	error = trunk_ioctl_allports(tr, SIOCDELMULTI, (caddr_t)ifr);
	if (error == 0) {
		SLIST_REMOVE(&tr->tr_mc_head, mc, trunk_mc, mc_entries);
		free(mc, M_DEVBUF, sizeof(*mc));
	} else {
		/* XXX At least one port failed to remove the address */
		if (tr->tr_ifflags & IFF_DEBUG) {
			printf("%s: failed to remove multicast address "
			    "on all ports (%d)\n", tr->tr_ifname, error);
		}
		(void)ether_addmulti(ifr, &tr->tr_ac);
	}

	return (0);
}

void
trunk_ether_purgemulti(struct trunk_softc *tr)
{
	struct trunk_mc *mc;
	struct trunk_ifreq ifs;
	struct ifreq *ifr = &ifs.ifreq.ifreq;

	while ((mc = SLIST_FIRST(&tr->tr_mc_head)) != NULL) {
		bcopy(&mc->mc_addr, &ifr->ifr_addr, mc->mc_addr.ss_len);

		/* Try to remove multicast address on all ports */
		trunk_ioctl_allports(tr, SIOCDELMULTI, (caddr_t)ifr);

		SLIST_REMOVE(&tr->tr_mc_head, mc, trunk_mc, mc_entries);
		free(mc, M_DEVBUF, sizeof(*mc));
	}
}

int
trunk_ether_cmdmulti(struct trunk_port *tp, u_long cmd)
{
	struct trunk_softc *tr = (struct trunk_softc *)tp->tp_trunk;
	struct trunk_mc *mc;
	struct trunk_ifreq ifs;
	struct ifreq *ifr = &ifs.ifreq.ifreq;
	int ret, error = 0;

	bcopy(tp->tp_ifname, ifr->ifr_name, IFNAMSIZ);
	SLIST_FOREACH(mc, &tr->tr_mc_head, mc_entries) {
		bcopy(&mc->mc_addr, &ifr->ifr_addr, mc->mc_addr.ss_len);

		if ((ret = tp->tp_ioctl(tp->tp_if, cmd, (caddr_t)ifr)) != 0) {
			if (tr->tr_ifflags & IFF_DEBUG) {
				printf("%s: ioctl %lu failed on %s: %d\n",
				    tr->tr_ifname, cmd, tp->tp_ifname, ret);
			}
			/* Store last known error and continue */
			error = ret;
		}
	}

	return (error);
}

int
trunk_ioctl_allports(struct trunk_softc *tr, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	struct trunk_port *tp;
	int ret, error = 0;

	SLIST_FOREACH(tp, &tr->tr_ports, tp_entries) {
		bcopy(tp->tp_ifname, ifr->ifr_name, IFNAMSIZ);
		if ((ret = tp->tp_ioctl(tp->tp_if, cmd, data)) != 0) {
			if (tr->tr_ifflags & IFF_DEBUG) {
				printf("%s: ioctl %lu failed on %s: %d\n",
				    tr->tr_ifname, cmd, tp->tp_ifname, ret);
			}
			/* Store last known error and continue */
			error = ret;
		}
	}

	return (error);
}

void
trunk_start(struct ifnet *ifp)
{
	struct trunk_softc *tr = (struct trunk_softc *)ifp->if_softc;
	struct mbuf *m;
	int error;

	for (;;) {
		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		if (tr->tr_proto != TRUNK_PROTO_NONE && tr->tr_count) {
			error = (*tr->tr_start)(tr, m);
			if (error != 0)
				ifp->if_oerrors++;
		} else {
			m_freem(m);
			if (tr->tr_proto != TRUNK_PROTO_NONE)
				ifp->if_oerrors++;
		}
	}
}

u_int32_t
trunk_hashmbuf(struct mbuf *m, SIPHASH_KEY *key)
{
	u_int16_t etype, ether_vtag;
	u_int32_t p = 0;
	u_int16_t *vlan, vlanbuf[2];
	int off;
	struct ether_header *eh;
	struct ip *ip, ipbuf;
#ifdef INET6
	u_int32_t flow;
	struct ip6_hdr *ip6, ip6buf;
#endif
	SIPHASH_CTX ctx;

	if (m->m_pkthdr.csum_flags & M_FLOWID)
		return (m->m_pkthdr.ph_flowid);

	SipHash24_Init(&ctx, key);
	off = sizeof(*eh);
	if (m->m_len < off)
		goto done;
	eh = mtod(m, struct ether_header *);
	etype = ntohs(eh->ether_type);
	SipHash24_Update(&ctx, &eh->ether_shost, ETHER_ADDR_LEN);
	SipHash24_Update(&ctx, &eh->ether_dhost, ETHER_ADDR_LEN);

	/* Special handling for encapsulating VLAN frames */
	if (m->m_flags & M_VLANTAG) {
		ether_vtag = EVL_VLANOFTAG(m->m_pkthdr.ether_vtag);
		SipHash24_Update(&ctx, &ether_vtag, sizeof(ether_vtag));
	} else if (etype == ETHERTYPE_VLAN) {
		if ((vlan = (u_int16_t *)
		    trunk_gethdr(m, off, EVL_ENCAPLEN, &vlanbuf)) == NULL)
			return (p);
		ether_vtag = EVL_VLANOFTAG(*vlan);
		SipHash24_Update(&ctx, &ether_vtag, sizeof(ether_vtag));
		etype = ntohs(vlan[1]);
		off += EVL_ENCAPLEN;
	}

	switch (etype) {
	case ETHERTYPE_IP:
		if ((ip = (struct ip *)
		    trunk_gethdr(m, off, sizeof(*ip), &ipbuf)) == NULL)
			return (p);
		SipHash24_Update(&ctx, &ip->ip_src, sizeof(struct in_addr));
		SipHash24_Update(&ctx, &ip->ip_dst, sizeof(struct in_addr));
		break;
#ifdef INET6
	case ETHERTYPE_IPV6:
		if ((ip6 = (struct ip6_hdr *)
		    trunk_gethdr(m, off, sizeof(*ip6), &ip6buf)) == NULL)
			return (p);
		SipHash24_Update(&ctx, &ip6->ip6_src, sizeof(struct in6_addr));
		SipHash24_Update(&ctx, &ip6->ip6_dst, sizeof(struct in6_addr));
		flow = ip6->ip6_flow & IPV6_FLOWLABEL_MASK;
		SipHash24_Update(&ctx, &flow, sizeof(flow)); /* IPv6 flow label */
		break;
#endif
	}

done:
	return SipHash24_End(&ctx);
}

void
trunk_init(struct ifnet *ifp)
{
	struct trunk_softc *tr = (struct trunk_softc *)ifp->if_softc;

	ifp->if_flags |= IFF_RUNNING;

	if (tr->tr_init != NULL)
		(*tr->tr_init)(tr);
}

void
trunk_stop(struct ifnet *ifp)
{
	struct trunk_softc *tr = (struct trunk_softc *)ifp->if_softc;

	ifp->if_flags &= ~IFF_RUNNING;

	if (tr->tr_stop != NULL)
		(*tr->tr_stop)(tr);
}

void
trunk_input(struct ifnet *ifp, struct mbuf *m, struct netstack *ns)
{
	struct arpcom *ac0 = (struct arpcom *)ifp;
	struct trunk_port *tp;
	struct trunk_softc *tr;
	struct ifnet *trifp = NULL;
	struct ether_header *eh;

	if (m->m_len < sizeof(*eh))
		goto bad;

	eh = mtod(m, struct ether_header *);
	if (ETHER_IS_MULTICAST(eh->ether_dhost))
		ifp->if_imcasts++;

	/* Should be checked by the caller */
	if (ifp->if_type != IFT_IEEE8023ADLAG)
		goto bad;

	tp = (struct trunk_port *)ac0->ac_trunkport;
	if ((tr = (struct trunk_softc *)tp->tp_trunk) == NULL)
		goto bad;

	trifp = &tr->tr_ac.ac_if;
	if (tr->tr_proto == TRUNK_PROTO_NONE)
		goto bad;

	if ((*tr->tr_input)(tr, tp, m)) {
		/*
		 * We stop here if the packet has been consumed
		 * by the protocol routine.
		 */
		return;
	}

	if ((trifp->if_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING))
		goto bad;

	/*
	 * Drop promiscuously received packets if we are not in
	 * promiscuous mode.
	 */
	if (!ETHER_IS_MULTICAST(eh->ether_dhost) &&
	    (ifp->if_flags & IFF_PROMISC) &&
	    (trifp->if_flags & IFF_PROMISC) == 0) {
		if (bcmp(&tr->tr_ac.ac_enaddr, eh->ether_dhost,
		    ETHER_ADDR_LEN)) {
			m_freem(m);
			return;
		}
	}


	if_vinput(trifp, m, ns);
	return;

 bad:
	if (trifp != NULL)
		trifp->if_ierrors++;
	m_freem(m);
}

int
trunk_media_change(struct ifnet *ifp)
{
	struct trunk_softc *tr = (struct trunk_softc *)ifp->if_softc;

	if (tr->tr_ifflags & IFF_DEBUG)
		printf("%s\n", __func__);

	/* Ignore */
	return (0);
}

void
trunk_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct trunk_softc *tr = (struct trunk_softc *)ifp->if_softc;
	struct trunk_port *tp;

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_ETHER | IFM_AUTO;

	SLIST_FOREACH(tp, &tr->tr_ports, tp_entries) {
		if (TRUNK_PORTACTIVE(tp))
			imr->ifm_status |= IFM_ACTIVE;
	}
}

void
trunk_port_state(void *arg)
{
	struct trunk_port *tp = (struct trunk_port *)arg;
	struct trunk_softc *tr = NULL;

	if (tp != NULL)
		tr = (struct trunk_softc *)tp->tp_trunk;
	if (tr == NULL)
		return;
	if (tr->tr_linkstate != NULL)
		(*tr->tr_linkstate)(tp);
	trunk_link_active(tr, tp);
}

struct trunk_port *
trunk_link_active(struct trunk_softc *tr, struct trunk_port *tp)
{
	struct trunk_port *tp_next, *rval = NULL;
	int new_link = LINK_STATE_DOWN;

	/*
	 * Search a port which reports an active link state.
	 */

	if (tp == NULL)
		goto search;
	if (TRUNK_PORTACTIVE(tp)) {
		rval = tp;
		goto found;
	}
	if ((tp_next = SLIST_NEXT(tp, tp_entries)) != NULL &&
	    TRUNK_PORTACTIVE(tp_next)) {
		rval = tp_next;
		goto found;
	}

 search:
	SLIST_FOREACH(tp_next, &tr->tr_ports, tp_entries) {
		if (TRUNK_PORTACTIVE(tp_next)) {
			rval = tp_next;
			goto found;
		}
	}

 found:
	if (rval != NULL) {
		/*
		 * The IEEE 802.1D standard assumes that a trunk with
		 * multiple ports is always full duplex. This is valid
		 * for load sharing trunks and if at least two links
		 * are active. Unfortunately, checking the latter would
		 * be too expensive at this point.
		 */
		if ((tr->tr_capabilities & IFCAP_TRUNK_FULLDUPLEX) &&
		    (tr->tr_count > 1))
			new_link = LINK_STATE_FULL_DUPLEX;
		else
			new_link = rval->tp_link_state;
	}

	if (tr->tr_ac.ac_if.if_link_state != new_link) {
		tr->tr_ac.ac_if.if_link_state = new_link;
		if_link_state_change(&tr->tr_ac.ac_if);
	}

	return (rval);
}

const void *
trunk_gethdr(struct mbuf *m, u_int off, u_int len, void *buf)
{
	if (m->m_pkthdr.len < (off + len))
		return (NULL);
	else if (m->m_len < (off + len)) {
		m_copydata(m, off, len, buf);
		return (buf);
	}
	return (mtod(m, caddr_t) + off);
}

/*
 * Simple round robin trunking
 */

int
trunk_rr_attach(struct trunk_softc *tr)
{
	struct trunk_port *tp;

	tr->tr_detach = trunk_rr_detach;
	tr->tr_start = trunk_rr_start;
	tr->tr_input = trunk_rr_input;
	tr->tr_init = NULL;
	tr->tr_stop = NULL;
	tr->tr_linkstate = NULL;
	tr->tr_port_create = NULL;
	tr->tr_port_destroy = trunk_rr_port_destroy;
	tr->tr_capabilities = IFCAP_TRUNK_FULLDUPLEX;
	tr->tr_req = NULL;
	tr->tr_portreq = NULL;

	tp = SLIST_FIRST(&tr->tr_ports);
	tr->tr_psc = (caddr_t)tp;

	return (0);
}

int
trunk_rr_detach(struct trunk_softc *tr)
{
	tr->tr_psc = NULL;
	return (0);
}

void
trunk_rr_port_destroy(struct trunk_port *tp)
{
	struct trunk_softc *tr = (struct trunk_softc *)tp->tp_trunk;

	if (tp == (struct trunk_port *)tr->tr_psc)
		tr->tr_psc = NULL;
}

int
trunk_rr_start(struct trunk_softc *tr, struct mbuf *m)
{
	struct trunk_port *tp = (struct trunk_port *)tr->tr_psc, *tp_next;
	int error = 0;

	if (tp == NULL && (tp = trunk_link_active(tr, NULL)) == NULL) {
		m_freem(m);
		return (ENOENT);
	}

	if ((error = if_enqueue(tp->tp_if, m)) != 0)
		return (error);

	/* Get next active port */
	tp_next = trunk_link_active(tr, SLIST_NEXT(tp, tp_entries));
	tr->tr_psc = (caddr_t)tp_next;

	return (0);
}

int
trunk_rr_input(struct trunk_softc *tr, struct trunk_port *tp, struct mbuf *m)
{
	/* Just pass in the packet to our trunk device */
	return (0);
}

/*
 * Active failover
 */

int
trunk_fail_attach(struct trunk_softc *tr)
{
	tr->tr_detach = trunk_fail_detach;
	tr->tr_start = trunk_fail_start;
	tr->tr_input = trunk_fail_input;
	tr->tr_init = NULL;
	tr->tr_stop = NULL;
	tr->tr_port_create = trunk_fail_port_create;
	tr->tr_port_destroy = trunk_fail_port_destroy;
	tr->tr_linkstate = trunk_fail_linkstate;
	tr->tr_req = NULL;
	tr->tr_portreq = NULL;

	/* Get primary or the next active port */
	tr->tr_psc = (caddr_t)trunk_link_active(tr, tr->tr_primary);

	return (0);
}

int
trunk_fail_detach(struct trunk_softc *tr)
{
	tr->tr_psc = NULL;
	return (0);
}

int
trunk_fail_port_create(struct trunk_port *tp)
{
	struct trunk_softc *tr = (struct trunk_softc *)tp->tp_trunk;

	/* Get primary or the next active port */
	tr->tr_psc = (caddr_t)trunk_link_active(tr, tr->tr_primary);
	return (0);
}

void
trunk_fail_port_destroy(struct trunk_port *tp)
{
	struct trunk_softc *tr = (struct trunk_softc *)tp->tp_trunk;
	struct trunk_port *tp_next;

	if ((caddr_t)tp == tr->tr_psc) {
		/* Get the next active port */
		tp_next = trunk_link_active(tr, SLIST_NEXT(tp, tp_entries));
		if (tp_next == tp)
			tr->tr_psc = NULL;
		else
			tr->tr_psc = (caddr_t)tp_next;
	} else {
		/* Get primary or the next active port */
		tr->tr_psc = (caddr_t)trunk_link_active(tr, tr->tr_primary);
	}
}

int
trunk_fail_start(struct trunk_softc *tr, struct mbuf *m)
{
	struct trunk_port *tp = (struct trunk_port *)tr->tr_psc;

	/* Use the master port if active or the next available port */
	if (tp == NULL) {
		m_freem(m);
		return (ENOENT);
	}

	return (if_enqueue(tp->tp_if, m));
}

int
trunk_fail_input(struct trunk_softc *tr, struct trunk_port *tp, struct mbuf *m)
{
	if ((caddr_t)tp == tr->tr_psc)
		return (0);
	m_freem(m);
	return (-1);
}

void
trunk_fail_linkstate(struct trunk_port *tp)
{
	struct trunk_softc *tr = (struct trunk_softc *)tp->tp_trunk;

	tr->tr_psc = (caddr_t)trunk_link_active(tr, tr->tr_primary);
}

/*
 * Loadbalancing
 */

int
trunk_lb_attach(struct trunk_softc *tr)
{
	struct trunk_lb *lb;

	if ((lb = malloc(sizeof(*lb), M_DEVBUF, M_NOWAIT|M_ZERO)) == NULL)
		return (ENOMEM);

	tr->tr_detach = trunk_lb_detach;
	tr->tr_start = trunk_lb_start;
	tr->tr_input = trunk_lb_input;
	tr->tr_port_create = trunk_lb_port_create;
	tr->tr_port_destroy = trunk_lb_port_destroy;
	tr->tr_linkstate = NULL;
	tr->tr_capabilities = IFCAP_TRUNK_FULLDUPLEX;
	tr->tr_req = NULL;
	tr->tr_portreq = NULL;
	tr->tr_init = NULL;
	tr->tr_stop = NULL;

	arc4random_buf(&lb->lb_key, sizeof(lb->lb_key));
	tr->tr_psc = (caddr_t)lb;

	return (0);
}

int
trunk_lb_detach(struct trunk_softc *tr)
{
	struct trunk_lb *lb = (struct trunk_lb *)tr->tr_psc;

	free(lb, M_DEVBUF, sizeof *lb);
	return (0);
}

int
trunk_lb_porttable(struct trunk_softc *tr, struct trunk_port *tp)
{
	struct trunk_lb *lb = (struct trunk_lb *)tr->tr_psc;
	struct trunk_port *tp_next;
	int i = 0;

	bzero(&lb->lb_ports, sizeof(lb->lb_ports));
	SLIST_FOREACH(tp_next, &tr->tr_ports, tp_entries) {
		if (tp_next == tp)
			continue;
		if (i >= TRUNK_MAX_PORTS)
			return (EINVAL);
		if (tr->tr_ifflags & IFF_DEBUG)
			printf("%s: port %s at index %d\n",
			    tr->tr_ifname, tp_next->tp_ifname, i);
		lb->lb_ports[i++] = tp_next;
	}

	return (0);
}

int
trunk_lb_port_create(struct trunk_port *tp)
{
	struct trunk_softc *tr = (struct trunk_softc *)tp->tp_trunk;
	return (trunk_lb_porttable(tr, NULL));
}

void
trunk_lb_port_destroy(struct trunk_port *tp)
{
	struct trunk_softc *tr = (struct trunk_softc *)tp->tp_trunk;
	trunk_lb_porttable(tr, tp);
}

int
trunk_lb_start(struct trunk_softc *tr, struct mbuf *m)
{
	struct trunk_lb *lb = (struct trunk_lb *)tr->tr_psc;
	struct trunk_port *tp = NULL;
	u_int32_t p = 0;

	p = trunk_hashmbuf(m, &lb->lb_key);
	p %= tr->tr_count;
	tp = lb->lb_ports[p];

	/*
	 * Check the port's link state. This will return the next active
	 * port if the link is down or the port is NULL.
	 */
	if ((tp = trunk_link_active(tr, tp)) == NULL) {
		m_freem(m);
		return (ENOENT);
	}

	return (if_enqueue(tp->tp_if, m));
}

int
trunk_lb_input(struct trunk_softc *tr, struct trunk_port *tp, struct mbuf *m)
{
	/* Just pass in the packet to our trunk device */
	return (0);
}

/*
 * Broadcast mode
 */

int
trunk_bcast_attach(struct trunk_softc *tr)
{
	tr->tr_detach = trunk_bcast_detach;
	tr->tr_start = trunk_bcast_start;
	tr->tr_input = trunk_bcast_input;
	tr->tr_init = NULL;
	tr->tr_stop = NULL;
	tr->tr_port_create = NULL;
	tr->tr_port_destroy = NULL;
	tr->tr_linkstate = NULL;
	tr->tr_req = NULL;
	tr->tr_portreq = NULL;

	return (0);
}

int
trunk_bcast_detach(struct trunk_softc *tr)
{
	return (0);
}

int
trunk_bcast_start(struct trunk_softc *tr, struct mbuf *m0)
{
	int			 active_ports = 0;
	int			 errors = 0;
	struct trunk_port	*tp, *last = NULL;
	struct mbuf		*m;

	SLIST_FOREACH(tp, &tr->tr_ports, tp_entries) {
		if (!TRUNK_PORTACTIVE(tp))
			continue;

		active_ports++;

		if (last != NULL) {
			m = m_copym(m0, 0, M_COPYALL, M_DONTWAIT);
			if (m == NULL) {
				errors++;
				break;
			}

			if (if_enqueue(last->tp_if, m) != 0)
				errors++;
		}
		last = tp;
	}
	if (last == NULL) {
		m_freem(m0);
		return (ENOENT);
	}

	if (if_enqueue(last->tp_if, m0) != 0)
		errors++;

	if (errors == active_ports)
		return (ENOBUFS);

	return (0);
}

int
trunk_bcast_input(struct trunk_softc *tr, struct trunk_port *tp, struct mbuf *m)
{
	return (0);
}

/*
 * 802.3ad LACP
 */

int
trunk_lacp_attach(struct trunk_softc *tr)
{
	struct trunk_port *tp;
	int error;

	tr->tr_detach = trunk_lacp_detach;
	tr->tr_port_create = lacp_port_create;
	tr->tr_port_destroy = lacp_port_destroy;
	tr->tr_linkstate = lacp_linkstate;
	tr->tr_start = trunk_lacp_start;
	tr->tr_input = trunk_lacp_input;
	tr->tr_init = lacp_init;
	tr->tr_stop = lacp_stop;
	tr->tr_req = lacp_req;
	tr->tr_portreq = lacp_portreq;

	error = lacp_attach(tr);
	if (error)
		return (error);

	SLIST_FOREACH(tp, &tr->tr_ports, tp_entries)
		lacp_port_create(tp);

	return (error);
}

int
trunk_lacp_detach(struct trunk_softc *tr)
{
	struct trunk_port *tp;
	int error;

	SLIST_FOREACH(tp, &tr->tr_ports, tp_entries)
		lacp_port_destroy(tp);

	/* unlocking is safe here */
	error = lacp_detach(tr);

	return (error);
}

int
trunk_lacp_start(struct trunk_softc *tr, struct mbuf *m)
{
	struct trunk_port *tp;

	tp = lacp_select_tx_port(tr, m);
	if (tp == NULL) {
		m_freem(m);
		return (EBUSY);
	}

	return (if_enqueue(tp->tp_if, m));
}

int
trunk_lacp_input(struct trunk_softc *tr, struct trunk_port *tp, struct mbuf *m)
{
	return (lacp_input(tp, m));
}

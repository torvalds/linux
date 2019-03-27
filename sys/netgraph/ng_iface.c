/*
 * ng_iface.c
 */

/*-
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_iface.c,v 1.33 1999/11/01 09:24:51 julian Exp $
 */

/*
 * This node is also a system networking interface. It has
 * a hook for each protocol (IP, AppleTalk, etc). Packets
 * are simply relayed between the interface and the hooks.
 *
 * Interfaces are named ng0, ng1, etc.  New nodes take the
 * first available interface name.
 *
 * This node also includes Berkeley packet filter support.
 */

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/random.h>
#include <sys/rmlock.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/libkern.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/bpf.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_iface.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_IFACE, "netgraph_iface", "netgraph iface node");
#else
#define M_NETGRAPH_IFACE M_NETGRAPH
#endif

static SYSCTL_NODE(_net_graph, OID_AUTO, iface, CTLFLAG_RW, 0,
    "Point to point netgraph interface");
VNET_DEFINE_STATIC(int, ng_iface_max_nest) = 2;
#define	V_ng_iface_max_nest	VNET(ng_iface_max_nest)
SYSCTL_INT(_net_graph_iface, OID_AUTO, max_nesting, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ng_iface_max_nest), 0, "Max nested tunnels");

/* This struct describes one address family */
struct iffam {
	sa_family_t	family;		/* Address family */
	const char	*hookname;	/* Name for hook */
};
typedef const struct iffam *iffam_p;

/* List of address families supported by our interface */
const static struct iffam gFamilies[] = {
	{ AF_INET,	NG_IFACE_HOOK_INET	},
	{ AF_INET6,	NG_IFACE_HOOK_INET6	},
	{ AF_ATM,	NG_IFACE_HOOK_ATM	},
	{ AF_NATM,	NG_IFACE_HOOK_NATM	},
};
#define	NUM_FAMILIES		nitems(gFamilies)

/* Node private data */
struct ng_iface_private {
	struct	ifnet *ifp;		/* Our interface */
	int	unit;			/* Interface unit number */
	node_p	node;			/* Our netgraph node */
	hook_p	hooks[NUM_FAMILIES];	/* Hook for each address family */
	struct rmlock	lock;		/* Protect private data changes */
};
typedef struct ng_iface_private *priv_p;

#define	PRIV_RLOCK(priv, t)	rm_rlock(&priv->lock, t)
#define	PRIV_RUNLOCK(priv, t)	rm_runlock(&priv->lock, t)
#define	PRIV_WLOCK(priv)	rm_wlock(&priv->lock)
#define	PRIV_WUNLOCK(priv)	rm_wunlock(&priv->lock)

/* Interface methods */
static void	ng_iface_start(struct ifnet *ifp);
static int	ng_iface_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
static int	ng_iface_output(struct ifnet *ifp, struct mbuf *m0,
    			const struct sockaddr *dst, struct route *ro);
static void	ng_iface_bpftap(struct ifnet *ifp,
			struct mbuf *m, sa_family_t family);
static int	ng_iface_send(struct ifnet *ifp, struct mbuf *m,
			sa_family_t sa);
#ifdef DEBUG
static void	ng_iface_print_ioctl(struct ifnet *ifp, int cmd, caddr_t data);
#endif

/* Netgraph methods */
static int		ng_iface_mod_event(module_t, int, void *);
static ng_constructor_t	ng_iface_constructor;
static ng_rcvmsg_t	ng_iface_rcvmsg;
static ng_shutdown_t	ng_iface_shutdown;
static ng_newhook_t	ng_iface_newhook;
static ng_rcvdata_t	ng_iface_rcvdata;
static ng_disconnect_t	ng_iface_disconnect;

/* Helper stuff */
static iffam_p	get_iffam_from_af(sa_family_t family);
static iffam_p	get_iffam_from_hook(priv_p priv, hook_p hook);
static iffam_p	get_iffam_from_name(const char *name);
static hook_p  *get_hook_from_iffam(priv_p priv, iffam_p iffam);

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_iface_cmds[] = {
	{
	  NGM_IFACE_COOKIE,
	  NGM_IFACE_GET_IFNAME,
	  "getifname",
	  NULL,
	  &ng_parse_string_type
	},
	{
	  NGM_IFACE_COOKIE,
	  NGM_IFACE_POINT2POINT,
	  "point2point",
	  NULL,
	  NULL
	},
	{
	  NGM_IFACE_COOKIE,
	  NGM_IFACE_BROADCAST,
	  "broadcast",
	  NULL,
	  NULL
	},
	{
	  NGM_IFACE_COOKIE,
	  NGM_IFACE_GET_IFINDEX,
	  "getifindex",
	  NULL,
	  &ng_parse_uint32_type
	},
	{ 0 }
};

/* Node type descriptor */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_IFACE_NODE_TYPE,
	.mod_event =	ng_iface_mod_event,
	.constructor =	ng_iface_constructor,
	.rcvmsg =	ng_iface_rcvmsg,
	.shutdown =	ng_iface_shutdown,
	.newhook =	ng_iface_newhook,
	.rcvdata =	ng_iface_rcvdata,
	.disconnect =	ng_iface_disconnect,
	.cmdlist =	ng_iface_cmds,
};
NETGRAPH_INIT(iface, &typestruct);

VNET_DEFINE_STATIC(struct unrhdr *, ng_iface_unit);
#define	V_ng_iface_unit			VNET(ng_iface_unit)

/************************************************************************
			HELPER STUFF
 ************************************************************************/

/*
 * Get the family descriptor from the family ID
 */
static __inline iffam_p
get_iffam_from_af(sa_family_t family)
{
	iffam_p iffam;
	int k;

	for (k = 0; k < NUM_FAMILIES; k++) {
		iffam = &gFamilies[k];
		if (iffam->family == family)
			return (iffam);
	}
	return (NULL);
}

/*
 * Get the family descriptor from the hook
 */
static __inline iffam_p
get_iffam_from_hook(priv_p priv, hook_p hook)
{
	int k;

	for (k = 0; k < NUM_FAMILIES; k++)
		if (priv->hooks[k] == hook)
			return (&gFamilies[k]);
	return (NULL);
}

/*
 * Get the hook from the iffam descriptor
 */

static __inline hook_p *
get_hook_from_iffam(priv_p priv, iffam_p iffam)
{
	return (&priv->hooks[iffam - gFamilies]);
}

/*
 * Get the iffam descriptor from the name
 */
static __inline iffam_p
get_iffam_from_name(const char *name)
{
	iffam_p iffam;
	int k;

	for (k = 0; k < NUM_FAMILIES; k++) {
		iffam = &gFamilies[k];
		if (!strcmp(iffam->hookname, name))
			return (iffam);
	}
	return (NULL);
}

/************************************************************************
			INTERFACE STUFF
 ************************************************************************/

/*
 * Process an ioctl for the virtual interface
 */
static int
ng_iface_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ifreq *const ifr = (struct ifreq *) data;
	int error = 0;

#ifdef DEBUG
	ng_iface_print_ioctl(ifp, command, data);
#endif
	switch (command) {

	/* These two are mostly handled at a higher layer */
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		ifp->if_drv_flags |= IFF_DRV_RUNNING;
		ifp->if_drv_flags &= ~(IFF_DRV_OACTIVE);
		break;
	case SIOCGIFADDR:
		break;

	/* Set flags */
	case SIOCSIFFLAGS:
		/*
		 * If the interface is marked up and stopped, then start it.
		 * If it is marked down and running, then stop it.
		 */
		if (ifr->ifr_flags & IFF_UP) {
			if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
				ifp->if_drv_flags &= ~(IFF_DRV_OACTIVE);
				ifp->if_drv_flags |= IFF_DRV_RUNNING;
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				ifp->if_drv_flags &= ~(IFF_DRV_RUNNING |
				    IFF_DRV_OACTIVE);
		}
		break;

	/* Set the interface MTU */
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > NG_IFACE_MTU_MAX
		    || ifr->ifr_mtu < NG_IFACE_MTU_MIN)
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;

	/* Stuff that's not supported */
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = 0;
		break;
	case SIOCSIFPHYS:
		error = EOPNOTSUPP;
		break;

	default:
		error = EINVAL;
		break;
	}
	return (error);
}

/*
 * This routine is called to deliver a packet out the interface.
 * We simply look at the address family and relay the packet to
 * the corresponding hook, if it exists and is connected.
 */

static int
ng_iface_output(struct ifnet *ifp, struct mbuf *m,
	const struct sockaddr *dst, struct route *ro)
{
	uint32_t af;
	int error;

	/* Check interface flags */
	if (!((ifp->if_flags & IFF_UP) &&
	    (ifp->if_drv_flags & IFF_DRV_RUNNING))) {
		m_freem(m);
		return (ENETDOWN);
	}

	/* Protect from deadly infinite recursion. */
	error = if_tunnel_check_nesting(ifp, m, NGM_IFACE_COOKIE,
	    V_ng_iface_max_nest);
	if (error) {
		m_freem(m);
		return (error);
	}

	/* BPF writes need to be handled specially. */
	if (dst->sa_family == AF_UNSPEC)
		bcopy(dst->sa_data, &af, sizeof(af));
	else
		af = dst->sa_family;

	/* Berkeley packet filter */
	ng_iface_bpftap(ifp, m, af);

	if (ALTQ_IS_ENABLED(&ifp->if_snd)) {
		M_PREPEND(m, sizeof(sa_family_t), M_NOWAIT);
		if (m == NULL) {
			if_inc_counter(ifp, IFCOUNTER_OQDROPS, 1);
			return (ENOBUFS);
		}
		*(sa_family_t *)m->m_data = af;
		error = (ifp->if_transmit)(ifp, m);
	} else
		error = ng_iface_send(ifp, m, af);

	return (error);
}

/*
 * Start method is used only when ALTQ is enabled.
 */
static void
ng_iface_start(struct ifnet *ifp)
{
	struct mbuf *m;
	sa_family_t sa;

	KASSERT(ALTQ_IS_ENABLED(&ifp->if_snd), ("%s without ALTQ", __func__));

	for(;;) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		sa = *mtod(m, sa_family_t *);
		m_adj(m, sizeof(sa_family_t));
		ng_iface_send(ifp, m, sa);
	}
}

/*
 * Flash a packet by the BPF (requires prepending 4 byte AF header)
 * Note the phoney mbuf; this is OK because BPF treats it read-only.
 */
static void
ng_iface_bpftap(struct ifnet *ifp, struct mbuf *m, sa_family_t family)
{
	KASSERT(family != AF_UNSPEC, ("%s: family=AF_UNSPEC", __func__));
	if (bpf_peers_present(ifp->if_bpf)) {
		int32_t family4 = (int32_t)family;
		bpf_mtap2(ifp->if_bpf, &family4, sizeof(family4), m);
	}
}

/*
 * This routine does actual delivery of the packet into the
 * netgraph(4). It is called from ng_iface_start() and
 * ng_iface_output().
 */
static int
ng_iface_send(struct ifnet *ifp, struct mbuf *m, sa_family_t sa)
{
	struct rm_priotracker priv_tracker;
	const priv_p priv = (priv_p) ifp->if_softc;
	const iffam_p iffam = get_iffam_from_af(sa);
	hook_p hook;
	int error;
	int len;

	/* Check address family to determine hook (if known) */
	if (iffam == NULL) {
		m_freem(m);
		log(LOG_WARNING, "%s: can't handle af%d\n", ifp->if_xname, sa);
		return (EAFNOSUPPORT);
	}

	/* Copy length before the mbuf gets invalidated. */
	len = m->m_pkthdr.len;

	PRIV_RLOCK(priv, &priv_tracker);
	hook = *get_hook_from_iffam(priv, iffam);
	if (hook == NULL) {
		NG_FREE_M(m);
		PRIV_RUNLOCK(priv, &priv_tracker);
		return ENETDOWN;
	}
	NG_HOOK_REF(hook);
	PRIV_RUNLOCK(priv, &priv_tracker);

	NG_OUTBOUND_THREAD_REF();
	NG_SEND_DATA_ONLY(error, hook, m);
	NG_OUTBOUND_THREAD_UNREF();
	NG_HOOK_UNREF(hook);

	/* Update stats. */
	if (error == 0) {
		if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	}

	return (error);
}

#ifdef DEBUG
/*
 * Display an ioctl to the virtual interface
 */

static void
ng_iface_print_ioctl(struct ifnet *ifp, int command, caddr_t data)
{
	char   *str;

	switch (command & IOC_DIRMASK) {
	case IOC_VOID:
		str = "IO";
		break;
	case IOC_OUT:
		str = "IOR";
		break;
	case IOC_IN:
		str = "IOW";
		break;
	case IOC_INOUT:
		str = "IORW";
		break;
	default:
		str = "IO??";
	}
	log(LOG_DEBUG, "%s: %s('%c', %d, char[%d])\n",
	       ifp->if_xname,
	       str,
	       IOCGROUP(command),
	       command & 0xff,
	       IOCPARM_LEN(command));
}
#endif /* DEBUG */

/************************************************************************
			NETGRAPH NODE STUFF
 ************************************************************************/

/*
 * Constructor for a node
 */
static int
ng_iface_constructor(node_p node)
{
	struct ifnet *ifp;
	priv_p priv;

	/* Allocate node and interface private structures */
	priv = malloc(sizeof(*priv), M_NETGRAPH_IFACE, M_WAITOK | M_ZERO);
	ifp = if_alloc(IFT_PROPVIRTUAL);
	if (ifp == NULL) {
		free(priv, M_NETGRAPH_IFACE);
		return (ENOMEM);
	}

	rm_init(&priv->lock, "ng_iface private rmlock");

	/* Link them together */
	ifp->if_softc = priv;
	priv->ifp = ifp;

	/* Get an interface unit number */
	priv->unit = alloc_unr(V_ng_iface_unit);

	/* Link together node and private info */
	NG_NODE_SET_PRIVATE(node, priv);
	priv->node = node;

	/* Initialize interface structure */
	if_initname(ifp, NG_IFACE_IFACE_NAME, priv->unit);
	ifp->if_output = ng_iface_output;
	ifp->if_start = ng_iface_start;
	ifp->if_ioctl = ng_iface_ioctl;
	ifp->if_mtu = NG_IFACE_MTU_DEFAULT;
	ifp->if_flags = (IFF_SIMPLEX|IFF_POINTOPOINT|IFF_NOARP|IFF_MULTICAST);
	ifp->if_type = IFT_PROPVIRTUAL;		/* XXX */
	ifp->if_addrlen = 0;			/* XXX */
	ifp->if_hdrlen = 0;			/* XXX */
	ifp->if_baudrate = 64000;		/* XXX */
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	/* Give this node the same name as the interface (if possible) */
	if (ng_name_node(node, ifp->if_xname) != 0)
		log(LOG_WARNING, "%s: can't acquire netgraph name\n",
		    ifp->if_xname);

	/* Attach the interface */
	if_attach(ifp);
	bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));

	/* Done */
	return (0);
}

/*
 * Give our ok for a hook to be added
 */
static int
ng_iface_newhook(node_p node, hook_p hook, const char *name)
{
	const iffam_p iffam = get_iffam_from_name(name);
	const priv_p priv = NG_NODE_PRIVATE(node);
	hook_p *hookptr;

	if (iffam == NULL)
		return (EPFNOSUPPORT);
	PRIV_WLOCK(priv);
	hookptr = get_hook_from_iffam(priv, iffam);
	if (*hookptr != NULL) {
		PRIV_WUNLOCK(priv);
		return (EISCONN);
	}
	*hookptr = hook;
	NG_HOOK_HI_STACK(hook);
	NG_HOOK_SET_TO_INBOUND(hook);
	PRIV_WUNLOCK(priv);
	return (0);
}

/*
 * Receive a control message
 */
static int
ng_iface_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ifnet *const ifp = priv->ifp;
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_IFACE_COOKIE:
		switch (msg->header.cmd) {
		case NGM_IFACE_GET_IFNAME:
			NG_MKRESPONSE(resp, msg, IFNAMSIZ, M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			strlcpy(resp->data, ifp->if_xname, IFNAMSIZ);
			break;

		case NGM_IFACE_POINT2POINT:
		case NGM_IFACE_BROADCAST:
		    {

			/* Deny request if interface is UP */
			if ((ifp->if_flags & IFF_UP) != 0)
				return (EBUSY);

			/* Change flags */
			switch (msg->header.cmd) {
			case NGM_IFACE_POINT2POINT:
				ifp->if_flags |= IFF_POINTOPOINT;
				ifp->if_flags &= ~IFF_BROADCAST;
				break;
			case NGM_IFACE_BROADCAST:
				ifp->if_flags &= ~IFF_POINTOPOINT;
				ifp->if_flags |= IFF_BROADCAST;
				break;
			}
			break;
		    }

		case NGM_IFACE_GET_IFINDEX:
			NG_MKRESPONSE(resp, msg, sizeof(uint32_t), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			*((uint32_t *)resp->data) = priv->ifp->if_index;
			break;

		default:
			error = EINVAL;
			break;
		}
		break;
	case NGM_FLOW_COOKIE:
		switch (msg->header.cmd) {
		case NGM_LINK_IS_UP:
			if_link_state_change(ifp, LINK_STATE_UP);
			break;
		case NGM_LINK_IS_DOWN:
			if_link_state_change(ifp, LINK_STATE_DOWN);
			break;
		default:
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/*
 * Recive data from a hook. Pass the packet to the correct input routine.
 */
static int
ng_iface_rcvdata(hook_p hook, item_p item)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	const iffam_p iffam = get_iffam_from_hook(priv, hook);
	struct ifnet *const ifp = priv->ifp;
	struct mbuf *m;
	int isr;

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);
	/* Sanity checks */
	KASSERT(iffam != NULL, ("%s: iffam", __func__));
	M_ASSERTPKTHDR(m);
	if ((ifp->if_flags & IFF_UP) == 0) {
		NG_FREE_M(m);
		return (ENETDOWN);
	}

	/* Update interface stats */
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);

	/* Note receiving interface */
	m->m_pkthdr.rcvif = ifp;

	/* Berkeley packet filter */
	ng_iface_bpftap(ifp, m, iffam->family);

	/* Send packet */
	switch (iffam->family) {
#ifdef INET
	case AF_INET:
		isr = NETISR_IP;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		isr = NETISR_IPV6;
		break;
#endif
	default:
		m_freem(m);
		return (EAFNOSUPPORT);
	}
	random_harvest_queue(m, sizeof(*m), RANDOM_NET_NG);
	M_SETFIB(m, ifp->if_fib);
	netisr_dispatch(isr, m);
	return (0);
}

/*
 * Shutdown and remove the node and its associated interface.
 */
static int
ng_iface_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	/*
	 * The ifnet may be in a different vnet than the netgraph node, 
	 * hence we have to change the current vnet context here.
	 */
	CURVNET_SET_QUIET(priv->ifp->if_vnet);
	bpfdetach(priv->ifp);
	if_detach(priv->ifp);
	if_free(priv->ifp);
	CURVNET_RESTORE();
	priv->ifp = NULL;
	free_unr(V_ng_iface_unit, priv->unit);
	rm_destroy(&priv->lock);
	free(priv, M_NETGRAPH_IFACE);
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);
	return (0);
}

/*
 * Hook disconnection. Note that we do *not* shutdown when all
 * hooks have been disconnected.
 */
static int
ng_iface_disconnect(hook_p hook)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	const iffam_p iffam = get_iffam_from_hook(priv, hook);

	if (iffam == NULL)
		panic("%s", __func__);
	PRIV_WLOCK(priv);
	*get_hook_from_iffam(priv, iffam) = NULL;
	PRIV_WUNLOCK(priv);
	return (0);
}

/*
 * Handle loading and unloading for this node type.
 */
static int
ng_iface_mod_event(module_t mod, int event, void *data)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
	case MOD_UNLOAD:
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static void
vnet_ng_iface_init(const void *unused)
{

	V_ng_iface_unit = new_unrhdr(0, 0xffff, NULL);
}
VNET_SYSINIT(vnet_ng_iface_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    vnet_ng_iface_init, NULL);

static void
vnet_ng_iface_uninit(const void *unused)
{

	delete_unrhdr(V_ng_iface_unit);
}
VNET_SYSUNINIT(vnet_ng_iface_uninit, SI_SUB_INIT_IF, SI_ORDER_ANY,
    vnet_ng_iface_uninit, NULL);

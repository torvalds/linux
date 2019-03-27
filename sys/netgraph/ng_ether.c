
/*
 * ng_ether.c
 */

/*-
 * Copyright (c) 1996-2000 Whistle Communications, Inc.
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
 * Authors: Archie Cobbs <archie@freebsd.org>
 *	    Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD$
 */

/*
 * ng_ether(4) netgraph node type
 */

#include <sys/param.h>
#include <sys/eventhandler.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/taskqueue.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_bridgevar.h>
#include <net/vnet.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_ether.h>

MODULE_VERSION(ng_ether, 1);

#define IFP2NG(ifp)  ((ifp)->if_l2com)

/* Per-node private data */
struct private {
	struct ifnet	*ifp;		/* associated interface */
	hook_p		upper;		/* upper hook connection */
	hook_p		lower;		/* lower hook connection */
	hook_p		orphan;		/* orphan hook connection */
	u_char		autoSrcAddr;	/* always overwrite source address */
	u_char		promisc;	/* promiscuous mode enabled */
	u_long		hwassist;	/* hardware checksum capabilities */
	u_int		flags;		/* flags e.g. really die */
};
typedef struct private *priv_p;

/* Hook pointers used by if_ethersubr.c to callback to netgraph */
extern	void	(*ng_ether_input_p)(struct ifnet *ifp, struct mbuf **mp);
extern	void	(*ng_ether_input_orphan_p)(struct ifnet *ifp, struct mbuf *m);
extern	int	(*ng_ether_output_p)(struct ifnet *ifp, struct mbuf **mp);
extern	void	(*ng_ether_attach_p)(struct ifnet *ifp);
extern	void	(*ng_ether_detach_p)(struct ifnet *ifp);
extern	void	(*ng_ether_link_state_p)(struct ifnet *ifp, int state);

/* Functional hooks called from if_ethersubr.c */
static void	ng_ether_input(struct ifnet *ifp, struct mbuf **mp);
static void	ng_ether_input_orphan(struct ifnet *ifp, struct mbuf *m);
static int	ng_ether_output(struct ifnet *ifp, struct mbuf **mp);
static void	ng_ether_attach(struct ifnet *ifp);
static void	ng_ether_detach(struct ifnet *ifp); 
static void	ng_ether_link_state(struct ifnet *ifp, int state); 

/* Other functions */
static int	ng_ether_rcv_lower(hook_p node, item_p item);
static int	ng_ether_rcv_upper(hook_p node, item_p item);

/* Netgraph node methods */
static ng_constructor_t	ng_ether_constructor;
static ng_rcvmsg_t	ng_ether_rcvmsg;
static ng_shutdown_t	ng_ether_shutdown;
static ng_newhook_t	ng_ether_newhook;
static ng_rcvdata_t	ng_ether_rcvdata;
static ng_disconnect_t	ng_ether_disconnect;
static int		ng_ether_mod_event(module_t mod, int event, void *data);

static eventhandler_tag	ng_ether_ifnet_arrival_cookie;

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_ether_cmdlist[] = {
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_GET_IFNAME,
	  "getifname",
	  NULL,
	  &ng_parse_string_type
	},
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_GET_IFINDEX,
	  "getifindex",
	  NULL,
	  &ng_parse_int32_type
	},
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_GET_ENADDR,
	  "getenaddr",
	  NULL,
	  &ng_parse_enaddr_type
	},
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_SET_ENADDR,
	  "setenaddr",
	  &ng_parse_enaddr_type,
	  NULL
	},
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_GET_PROMISC,
	  "getpromisc",
	  NULL,
	  &ng_parse_int32_type
	},
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_SET_PROMISC,
	  "setpromisc",
	  &ng_parse_int32_type,
	  NULL
	},
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_GET_AUTOSRC,
	  "getautosrc",
	  NULL,
	  &ng_parse_int32_type
	},
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_SET_AUTOSRC,
	  "setautosrc",
	  &ng_parse_int32_type,
	  NULL
	},
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_ADD_MULTI,
	  "addmulti",
	  &ng_parse_enaddr_type,
	  NULL
	},
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_DEL_MULTI,
	  "delmulti",
	  &ng_parse_enaddr_type,
	  NULL
	},
	{
	  NGM_ETHER_COOKIE,
	  NGM_ETHER_DETACH,
	  "detach",
	  NULL,
	  NULL
	},
	{ 0 }
};

static struct ng_type ng_ether_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_ETHER_NODE_TYPE,
	.mod_event =	ng_ether_mod_event,
	.constructor =	ng_ether_constructor,
	.rcvmsg =	ng_ether_rcvmsg,
	.shutdown =	ng_ether_shutdown,
	.newhook =	ng_ether_newhook,
	.rcvdata =	ng_ether_rcvdata,
	.disconnect =	ng_ether_disconnect,
	.cmdlist =	ng_ether_cmdlist,
};
NETGRAPH_INIT(ether, &ng_ether_typestruct);

/******************************************************************
		    UTILITY FUNCTIONS
******************************************************************/
static void
ng_ether_sanitize_ifname(const char *ifname, char *name)
{
	int i;

	for (i = 0; i < IFNAMSIZ; i++) {
		if (ifname[i] == '.' || ifname[i] == ':')
			name[i] = '_';
		else
			name[i] = ifname[i];
		if (name[i] == '\0')
			break;
	}
}

/******************************************************************
		    ETHERNET FUNCTION HOOKS
******************************************************************/

/*
 * Handle a packet that has come in on an interface. We get to
 * look at it here before any upper layer protocols do.
 */
static void
ng_ether_input(struct ifnet *ifp, struct mbuf **mp)
{
	const node_p node = IFP2NG(ifp);
	const priv_p priv = NG_NODE_PRIVATE(node);
	int error;

	/* If "lower" hook not connected, let packet continue */
	if (priv->lower == NULL)
		return;
	NG_SEND_DATA_ONLY(error, priv->lower, *mp);	/* sets *mp = NULL */
}

/*
 * Handle a packet that has come in on an interface, and which
 * does not match any of our known protocols (an ``orphan'').
 */
static void
ng_ether_input_orphan(struct ifnet *ifp, struct mbuf *m)
{
	const node_p node = IFP2NG(ifp);
	const priv_p priv = NG_NODE_PRIVATE(node);
	int error;

	/* If "orphan" hook not connected, discard packet */
	if (priv->orphan == NULL) {
		m_freem(m);
		return;
	}
	NG_SEND_DATA_ONLY(error, priv->orphan, m);
}

/*
 * Handle a packet that is going out on an interface.
 * The Ethernet header is already attached to the mbuf.
 */
static int
ng_ether_output(struct ifnet *ifp, struct mbuf **mp)
{
	const node_p node = IFP2NG(ifp);
	const priv_p priv = NG_NODE_PRIVATE(node);
	int error = 0;

	/* If "upper" hook not connected, let packet continue */
	if (priv->upper == NULL)
		return (0);

	/* Send it out "upper" hook */
	NG_OUTBOUND_THREAD_REF();
	NG_SEND_DATA_ONLY(error, priv->upper, *mp);
	NG_OUTBOUND_THREAD_UNREF();
	return (error);
}

/*
 * A new Ethernet interface has been attached.
 * Create a new node for it, etc.
 */
static void
ng_ether_attach(struct ifnet *ifp)
{
	char name[IFNAMSIZ];
	priv_p priv;
	node_p node;

	/*
	 * Do not create / attach an ether node to this ifnet if
	 * a netgraph node with the same name already exists.
	 * This should prevent ether nodes to become attached to
	 * eiface nodes, which may be problematic due to naming
	 * clashes.
	 */
	ng_ether_sanitize_ifname(ifp->if_xname, name);
	if ((node = ng_name2noderef(NULL, name)) != NULL) {
		NG_NODE_UNREF(node);
		return;
	}

	/* Create node */
	KASSERT(!IFP2NG(ifp), ("%s: node already exists?", __func__));
	if (ng_make_node_common(&ng_ether_typestruct, &node) != 0) {
		log(LOG_ERR, "%s: can't %s for %s\n",
		    __func__, "create node", ifp->if_xname);
		return;
	}

	/* Allocate private data */
	priv = malloc(sizeof(*priv), M_NETGRAPH, M_NOWAIT | M_ZERO);
	if (priv == NULL) {
		log(LOG_ERR, "%s: can't %s for %s\n",
		    __func__, "allocate memory", ifp->if_xname);
		NG_NODE_UNREF(node);
		return;
	}
	NG_NODE_SET_PRIVATE(node, priv);
	priv->ifp = ifp;
	IFP2NG(ifp) = node;
	priv->hwassist = ifp->if_hwassist;

	/* Try to give the node the same name as the interface */
	if (ng_name_node(node, name) != 0)
		log(LOG_WARNING, "%s: can't name node %s\n", __func__, name);
}

/*
 * An Ethernet interface is being detached.
 * REALLY Destroy its node.
 */
static void
ng_ether_detach(struct ifnet *ifp)
{
	const node_p node = IFP2NG(ifp);
	const priv_p priv = NG_NODE_PRIVATE(node);

	taskqueue_drain(taskqueue_swi, &ifp->if_linktask);
	NG_NODE_REALLY_DIE(node);	/* Force real removal of node */
	/*
	 * We can't assume the ifnet is still around when we run shutdown
	 * So zap it now. XXX We HOPE that anything running at this time
	 * handles it (as it should in the non netgraph case).
	 */
	IFP2NG(ifp) = NULL;
	priv->ifp = NULL;	/* XXX race if interrupted an output packet */
	ng_rmnode_self(node);		/* remove all netgraph parts */
}

/*
 * Notify graph about link event.
 * if_link_state_change() has already checked that the state has changed.
 */
static void
ng_ether_link_state(struct ifnet *ifp, int state)
{
	const node_p node = IFP2NG(ifp);
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *msg;
	int cmd, dummy_error = 0;

	if (state == LINK_STATE_UP)
		cmd = NGM_LINK_IS_UP;
	else if (state == LINK_STATE_DOWN)
		cmd = NGM_LINK_IS_DOWN;
	else
		return;

	if (priv->lower != NULL) {
		NG_MKMESSAGE(msg, NGM_FLOW_COOKIE, cmd, 0, M_NOWAIT);
		if (msg != NULL)
			NG_SEND_MSG_HOOK(dummy_error, node, msg, priv->lower, 0);
	}
	if (priv->orphan != NULL) {
		NG_MKMESSAGE(msg, NGM_FLOW_COOKIE, cmd, 0, M_NOWAIT);
		if (msg != NULL)
			NG_SEND_MSG_HOOK(dummy_error, node, msg, priv->orphan, 0);
	}
}

/*
 * Interface arrival notification handler.
 * The notification is produced in two cases:
 *  o a new interface arrives
 *  o an existing interface got renamed
 * Currently the first case is handled by ng_ether_attach via special
 * hook ng_ether_attach_p.
 */
static void
ng_ether_ifnet_arrival_event(void *arg __unused, struct ifnet *ifp)
{
	char name[IFNAMSIZ];
	node_p node;

	/* Only ethernet interfaces are of interest. */
	if (ifp->if_type != IFT_ETHER
	    && ifp->if_type != IFT_L2VLAN)
		return;

	/*
	 * Just return if it's a new interface without an ng_ether companion.
	 */
	node = IFP2NG(ifp);
	if (node == NULL)
		return;

	/* Try to give the node the same name as the new interface name */
	ng_ether_sanitize_ifname(ifp->if_xname, name);
	if (ng_name_node(node, name) != 0)
		log(LOG_WARNING, "%s: can't re-name node %s\n", __func__, name);
}

/******************************************************************
		    NETGRAPH NODE METHODS
******************************************************************/

/*
 * It is not possible or allowable to create a node of this type.
 * Nodes get created when the interface is attached (or, when
 * this node type's KLD is loaded).
 */
static int
ng_ether_constructor(node_p node)
{
	return (EINVAL);
}

/*
 * Check for attaching a new hook.
 */
static	int
ng_ether_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	hook_p *hookptr;

	/* Divert hook is an alias for lower */
	if (strcmp(name, NG_ETHER_HOOK_DIVERT) == 0)
		name = NG_ETHER_HOOK_LOWER;

	/* Which hook? */
	if (strcmp(name, NG_ETHER_HOOK_UPPER) == 0) {
		hookptr = &priv->upper;
		NG_HOOK_SET_RCVDATA(hook, ng_ether_rcv_upper);
		NG_HOOK_SET_TO_INBOUND(hook);
	} else if (strcmp(name, NG_ETHER_HOOK_LOWER) == 0) {
		hookptr = &priv->lower;
		NG_HOOK_SET_RCVDATA(hook, ng_ether_rcv_lower);
	} else if (strcmp(name, NG_ETHER_HOOK_ORPHAN) == 0) {
		hookptr = &priv->orphan;
		NG_HOOK_SET_RCVDATA(hook, ng_ether_rcv_lower);
	} else
		return (EINVAL);

	/* Check if already connected (shouldn't be, but doesn't hurt) */
	if (*hookptr != NULL)
		return (EISCONN);

	/* Disable hardware checksums while 'upper' hook is connected */
	if (hookptr == &priv->upper)
		priv->ifp->if_hwassist = 0;
	NG_HOOK_HI_STACK(hook);
	/* OK */
	*hookptr = hook;
	return (0);
}

/*
 * Receive an incoming control message.
 */
static int
ng_ether_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_ETHER_COOKIE:
		switch (msg->header.cmd) {
		case NGM_ETHER_GET_IFNAME:
			NG_MKRESPONSE(resp, msg, IFNAMSIZ, M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			strlcpy(resp->data, priv->ifp->if_xname, IFNAMSIZ);
			break;
		case NGM_ETHER_GET_IFINDEX:
			NG_MKRESPONSE(resp, msg, sizeof(u_int32_t), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			*((u_int32_t *)resp->data) = priv->ifp->if_index;
			break;
		case NGM_ETHER_GET_ENADDR:
			NG_MKRESPONSE(resp, msg, ETHER_ADDR_LEN, M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			bcopy(IF_LLADDR(priv->ifp),
			    resp->data, ETHER_ADDR_LEN);
			break;
		case NGM_ETHER_SET_ENADDR:
		    {
			if (msg->header.arglen != ETHER_ADDR_LEN) {
				error = EINVAL;
				break;
			}
			error = if_setlladdr(priv->ifp,
			    (u_char *)msg->data, ETHER_ADDR_LEN);
			break;
		    }
		case NGM_ETHER_GET_PROMISC:
			NG_MKRESPONSE(resp, msg, sizeof(u_int32_t), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			*((u_int32_t *)resp->data) = priv->promisc;
			break;
		case NGM_ETHER_SET_PROMISC:
		    {
			u_char want;

			if (msg->header.arglen != sizeof(u_int32_t)) {
				error = EINVAL;
				break;
			}
			want = !!*((u_int32_t *)msg->data);
			if (want ^ priv->promisc) {
				if ((error = ifpromisc(priv->ifp, want)) != 0)
					break;
				priv->promisc = want;
			}
			break;
		    }
		case NGM_ETHER_GET_AUTOSRC:
			NG_MKRESPONSE(resp, msg, sizeof(u_int32_t), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			*((u_int32_t *)resp->data) = priv->autoSrcAddr;
			break;
		case NGM_ETHER_SET_AUTOSRC:
			if (msg->header.arglen != sizeof(u_int32_t)) {
				error = EINVAL;
				break;
			}
			priv->autoSrcAddr = !!*((u_int32_t *)msg->data);
			break;
		case NGM_ETHER_ADD_MULTI:
		    {
			struct sockaddr_dl sa_dl;
			struct ifmultiaddr *ifma;

			if (msg->header.arglen != ETHER_ADDR_LEN) {
				error = EINVAL;
				break;
			}
			bzero(&sa_dl, sizeof(struct sockaddr_dl));
			sa_dl.sdl_len = sizeof(struct sockaddr_dl);
			sa_dl.sdl_family = AF_LINK;
			sa_dl.sdl_alen = ETHER_ADDR_LEN;
			bcopy((void *)msg->data, LLADDR(&sa_dl),
			    ETHER_ADDR_LEN);
			/*
			 * Netgraph is only permitted to join groups once
			 * via the if_addmulti() KPI, because it cannot hold
			 * struct ifmultiaddr * between calls. It may also
			 * lose a race while we check if the membership
			 * already exists.
			 */
			if_maddr_rlock(priv->ifp);
			ifma = if_findmulti(priv->ifp,
			    (struct sockaddr *)&sa_dl);
			if_maddr_runlock(priv->ifp);
			if (ifma != NULL) {
				error = EADDRINUSE;
			} else {
				error = if_addmulti(priv->ifp,
				    (struct sockaddr *)&sa_dl, &ifma);
			}
			break;
		    }
		case NGM_ETHER_DEL_MULTI:
		    {
			struct sockaddr_dl sa_dl;

			if (msg->header.arglen != ETHER_ADDR_LEN) {
				error = EINVAL;
				break;
			}
			bzero(&sa_dl, sizeof(struct sockaddr_dl));
			sa_dl.sdl_len = sizeof(struct sockaddr_dl);
			sa_dl.sdl_family = AF_LINK;
			sa_dl.sdl_alen = ETHER_ADDR_LEN;
			bcopy((void *)msg->data, LLADDR(&sa_dl),
			    ETHER_ADDR_LEN);
			error = if_delmulti(priv->ifp,
			    (struct sockaddr *)&sa_dl);
			break;
		    }
		case NGM_ETHER_DETACH:
			ng_ether_detach(priv->ifp);
			break;
		default:
			error = EINVAL;
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
 * Receive data on a hook.
 * Since we use per-hook recveive methods this should never be called.
 */
static int
ng_ether_rcvdata(hook_p hook, item_p item)
{
	NG_FREE_ITEM(item);

	panic("%s: weird hook", __func__);
}

/*
 * Handle an mbuf received on the "lower" or "orphan" hook.
 */
static int
ng_ether_rcv_lower(hook_p hook, item_p item)
{
	struct mbuf *m;
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
 	struct ifnet *const ifp = priv->ifp;

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	/* Check whether interface is ready for packets */

	if (!((ifp->if_flags & IFF_UP) &&
	    (ifp->if_drv_flags & IFF_DRV_RUNNING))) {
		NG_FREE_M(m);
		return (ENETDOWN);
	}

	/* Make sure header is fully pulled up */
	if (m->m_pkthdr.len < sizeof(struct ether_header)) {
		NG_FREE_M(m);
		return (EINVAL);
	}
	if (m->m_len < sizeof(struct ether_header)
	    && (m = m_pullup(m, sizeof(struct ether_header))) == NULL)
		return (ENOBUFS);

	/* Drop in the MAC address if desired */
	if (priv->autoSrcAddr) {

		/* Make the mbuf writable if it's not already */
		if (!M_WRITABLE(m)
		    && (m = m_pullup(m, sizeof(struct ether_header))) == NULL)
			return (ENOBUFS);

		/* Overwrite source MAC address */
		bcopy(IF_LLADDR(ifp),
		    mtod(m, struct ether_header *)->ether_shost,
		    ETHER_ADDR_LEN);
	}

	/* Send it on its way */
	return ether_output_frame(ifp, m);
}

/*
 * Handle an mbuf received on the "upper" hook.
 */
static int
ng_ether_rcv_upper(hook_p hook, item_p item)
{
	struct mbuf *m;
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ifnet *ifp = priv->ifp;

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	/* Check length and pull off header */
	if (m->m_pkthdr.len < sizeof(struct ether_header)) {
		NG_FREE_M(m);
		return (EINVAL);
	}
	if (m->m_len < sizeof(struct ether_header) &&
	    (m = m_pullup(m, sizeof(struct ether_header))) == NULL)
		return (ENOBUFS);

	m->m_pkthdr.rcvif = ifp;

	/* Pass the packet to the bridge, it may come back to us */
	if (ifp->if_bridge) {
		BRIDGE_INPUT(ifp, m);
		if (m == NULL)
			return (0);
	}

	/* Route packet back in */
	ether_demux(ifp, m);
	return (0);
}

/*
 * Shutdown node. This resets the node but does not remove it
 * unless the REALLY_DIE flag is set.
 */
static int
ng_ether_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (node->nd_flags & NGF_REALLY_DIE) {
		/*
		 * WE came here because the ethernet card is being unloaded,
		 * so stop being persistent.
		 * Actually undo all the things we did on creation.
		 * Assume the ifp has already been freed.
		 */
		NG_NODE_SET_PRIVATE(node, NULL);
		free(priv, M_NETGRAPH);		
		NG_NODE_UNREF(node);	/* free node itself */
		return (0);
	}
	if (priv->promisc) {		/* disable promiscuous mode */
		(void)ifpromisc(priv->ifp, 0);
		priv->promisc = 0;
	}
	NG_NODE_REVIVE(node);		/* Signal ng_rmnode we are persisant */

	return (0);
}

/*
 * Hook disconnection.
 */
static int
ng_ether_disconnect(hook_p hook)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (hook == priv->upper) {
		priv->upper = NULL;
		if (priv->ifp != NULL)		/* restore h/w csum */
			priv->ifp->if_hwassist = priv->hwassist;
	} else if (hook == priv->lower)
		priv->lower = NULL;
	else if (hook == priv->orphan)
		priv->orphan = NULL;
	else
		panic("%s: weird hook", __func__);
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
	&& (NG_NODE_IS_VALID(NG_HOOK_NODE(hook))))
		ng_rmnode_self(NG_HOOK_NODE(hook));	/* reset node */
	return (0);
}

/******************************************************************
		    	INITIALIZATION
******************************************************************/

/*
 * Handle loading and unloading for this node type.
 */
static int
ng_ether_mod_event(module_t mod, int event, void *data)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:

		/* Register function hooks */
		if (ng_ether_attach_p != NULL) {
			error = EEXIST;
			break;
		}
		ng_ether_attach_p = ng_ether_attach;
		ng_ether_detach_p = ng_ether_detach;
		ng_ether_output_p = ng_ether_output;
		ng_ether_input_p = ng_ether_input;
		ng_ether_input_orphan_p = ng_ether_input_orphan;
		ng_ether_link_state_p = ng_ether_link_state;

		ng_ether_ifnet_arrival_cookie =
		    EVENTHANDLER_REGISTER(ifnet_arrival_event,
		    ng_ether_ifnet_arrival_event, NULL, EVENTHANDLER_PRI_ANY);
		break;

	case MOD_UNLOAD:

		/*
		 * Note that the base code won't try to unload us until
		 * all nodes have been removed, and that can't happen
		 * until all Ethernet interfaces are removed. In any
		 * case, we know there are no nodes left if the action
		 * is MOD_UNLOAD, so there's no need to detach any nodes.
		 */

		EVENTHANDLER_DEREGISTER(ifnet_arrival_event,
		    ng_ether_ifnet_arrival_cookie);

		/* Unregister function hooks */
		ng_ether_attach_p = NULL;
		ng_ether_detach_p = NULL;
		ng_ether_output_p = NULL;
		ng_ether_input_p = NULL;
		ng_ether_input_orphan_p = NULL;
		ng_ether_link_state_p = NULL;
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static void
vnet_ng_ether_init(const void *unused)
{
	struct ifnet *ifp;

	/* If module load was rejected, don't attach to vnets. */
	if (ng_ether_attach_p != ng_ether_attach)
		return;

	/* Create nodes for any already-existing Ethernet interfaces. */
	IFNET_RLOCK();
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if (ifp->if_type == IFT_ETHER
		    || ifp->if_type == IFT_L2VLAN)
			ng_ether_attach(ifp);
	}
	IFNET_RUNLOCK();
}
VNET_SYSINIT(vnet_ng_ether_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    vnet_ng_ether_init, NULL);

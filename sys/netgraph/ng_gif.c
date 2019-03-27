/*
 * ng_gif.c
 */

/*-
 * SPDX-License-Identifier: BSD-3-Clause AND BSD-2-Clause
 *
 * Copyright 2001 The Aerospace Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions, and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of The Aerospace Corporation may not be used to endorse or
 *    promote products derived from this software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AEROSPACE CORPORATION ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AEROSPACE CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
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
 * $FreeBSD$
 */

/*
 * ng_gif(4) netgraph node type
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/if_gif.h>
#include <net/vnet.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_gif.h>

#define IFP2NG(ifp)  ((struct ng_node *)((struct gif_softc *)(ifp->if_softc))->gif_netgraph)
#define IFP2NG_SET(ifp, val)  (((struct gif_softc *)(ifp->if_softc))->gif_netgraph = (val))

/* Per-node private data */
struct private {
	struct ifnet	*ifp;		/* associated interface */
	hook_p		lower;		/* lower OR orphan hook connection */
	u_char		lowerOrphan;	/* whether lower is lower or orphan */
};
typedef struct private *priv_p;

/* Functional hooks called from if_gif.c */
static void	ng_gif_input(struct ifnet *ifp, struct mbuf **mp, int af);
static void	ng_gif_input_orphan(struct ifnet *ifp, struct mbuf *m, int af);
static void	ng_gif_attach(struct ifnet *ifp);
static void	ng_gif_detach(struct ifnet *ifp); 

/* Other functions */
static void	ng_gif_input2(node_p node, struct mbuf **mp, int af);
static int	ng_gif_glue_af(struct mbuf **mp, int af);
static int	ng_gif_rcv_lower(node_p node, struct mbuf *m);

/* Netgraph node methods */
static ng_constructor_t	ng_gif_constructor;
static ng_rcvmsg_t	ng_gif_rcvmsg;
static ng_shutdown_t	ng_gif_shutdown;
static ng_newhook_t	ng_gif_newhook;
static ng_connect_t	ng_gif_connect;
static ng_rcvdata_t	ng_gif_rcvdata;
static ng_disconnect_t	ng_gif_disconnect;
static int		ng_gif_mod_event(module_t mod, int event, void *data);

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_gif_cmdlist[] = {
	{
	  NGM_GIF_COOKIE,
	  NGM_GIF_GET_IFNAME,
	  "getifname",
	  NULL,
	  &ng_parse_string_type
	},
	{
	  NGM_GIF_COOKIE,
	  NGM_GIF_GET_IFINDEX,
	  "getifindex",
	  NULL,
	  &ng_parse_int32_type
	},
	{ 0 }
};

static struct ng_type ng_gif_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_GIF_NODE_TYPE,
	.mod_event =	ng_gif_mod_event,
	.constructor =	ng_gif_constructor,
	.rcvmsg =	ng_gif_rcvmsg,
	.shutdown =	ng_gif_shutdown,
	.newhook =	ng_gif_newhook,
	.connect =	ng_gif_connect,
	.rcvdata =	ng_gif_rcvdata,
	.disconnect =	ng_gif_disconnect,
	.cmdlist =	ng_gif_cmdlist,
};
MODULE_DEPEND(ng_gif, if_gif, 1,1,1);
NETGRAPH_INIT(gif, &ng_gif_typestruct);

/******************************************************************
		       GIF FUNCTION HOOKS
******************************************************************/

/*
 * Handle a packet that has come in on an interface. We get to
 * look at it here before any upper layer protocols do.
 */
static void
ng_gif_input(struct ifnet *ifp, struct mbuf **mp, int af)
{
	const node_p node = IFP2NG(ifp);
	const priv_p priv = NG_NODE_PRIVATE(node);

	/* If "lower" hook not connected, let packet continue */
	if (priv->lower == NULL || priv->lowerOrphan)
		return;
	ng_gif_input2(node, mp, af);
}

/*
 * Handle a packet that has come in on an interface, and which
 * does not match any of our known protocols (an ``orphan'').
 */
static void
ng_gif_input_orphan(struct ifnet *ifp, struct mbuf *m, int af)
{
	const node_p node = IFP2NG(ifp);
	const priv_p priv = NG_NODE_PRIVATE(node);

	/* If "orphan" hook not connected, let packet continue */
	if (priv->lower == NULL || !priv->lowerOrphan) {
		m_freem(m);
		return;
	}
	ng_gif_input2(node, &m, af);
	if (m != NULL)
		m_freem(m);
}

/*
 * Handle a packet that has come in on a gif interface.
 * Attach the address family to the mbuf for later use.
 */
static void
ng_gif_input2(node_p node, struct mbuf **mp, int af)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	int error;

	/* Glue address family on */
	if ((error = ng_gif_glue_af(mp, af)) != 0)
		return;

	/* Send out lower/orphan hook */
	NG_SEND_DATA_ONLY(error, priv->lower, *mp);
	*mp = NULL;
}

/*
 * A new gif interface has been attached.
 * Create a new node for it, etc.
 */
static void
ng_gif_attach(struct ifnet *ifp)
{
	priv_p priv;
	node_p node;

	/* Create node */
	KASSERT(!IFP2NG(ifp), ("%s: node already exists?", __func__));
	if (ng_make_node_common(&ng_gif_typestruct, &node) != 0) {
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
	IFP2NG_SET(ifp, node);

	/* Try to give the node the same name as the interface */
	if (ng_name_node(node, ifp->if_xname) != 0) {
		log(LOG_WARNING, "%s: can't name node %s\n",
		    __func__, ifp->if_xname);
	}
}

/*
 * An interface is being detached.
 * REALLY Destroy its node.
 */
static void
ng_gif_detach(struct ifnet *ifp)
{
	const node_p node = IFP2NG(ifp);
	priv_p priv;

	if (node == NULL)		/* no node (why not?), ignore */
		return;
	priv = NG_NODE_PRIVATE(node);
	NG_NODE_REALLY_DIE(node);	/* Force real removal of node */
	/*
	 * We can't assume the ifnet is still around when we run shutdown
	 * So zap it now. XXX We HOPE that anything running at this time
	 * handles it (as it should in the non netgraph case).
	 */
	IFP2NG_SET(ifp, NULL);
	priv->ifp = NULL;	/* XXX race if interrupted an output packet */
	ng_rmnode_self(node);		/* remove all netgraph parts */
}

/*
 * Optimization for gluing the address family onto
 * the front of an incoming packet.
 */
static int
ng_gif_glue_af(struct mbuf **mp, int af)
{
	struct mbuf *m = *mp;
	int error = 0;
	sa_family_t tmp_af;

	tmp_af = (sa_family_t) af;

	/*
	 * XXX: should try to bring back some of the optimizations from
	 * ng_ether.c
	 */

	/*
	 * Doing anything more is likely to get more
	 * expensive than it's worth..
	 * it's probable that everything else is in one
	 * big lump. The next node will do an m_pullup()
	 * for exactly the amount of data it needs and
	 * hopefully everything after that will not
	 * need one. So let's just use M_PREPEND.
	 */
	M_PREPEND(m, sizeof (tmp_af), M_NOWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto done;
	}

#if 0
copy:
#endif
	/* Copy header and return (possibly new) mbuf */
	*mtod(m, sa_family_t *) = tmp_af;
#if 0
	bcopy((caddr_t)&tmp_af, mtod(m, sa_family_t *), sizeof(tmp_af));
#endif
done:
	*mp = m;
	return error;
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
ng_gif_constructor(node_p node)
{
	return (EINVAL);
}

/*
 * Check for attaching a new hook.
 */
static	int
ng_gif_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	u_char orphan = priv->lowerOrphan;
	hook_p *hookptr;

	/* Divert hook is an alias for lower */
	if (strcmp(name, NG_GIF_HOOK_DIVERT) == 0)
		name = NG_GIF_HOOK_LOWER;

	/* Which hook? */
	if (strcmp(name, NG_GIF_HOOK_LOWER) == 0) {
		hookptr = &priv->lower;
		orphan = 0;
	} else if (strcmp(name, NG_GIF_HOOK_ORPHAN) == 0) {
		hookptr = &priv->lower;
		orphan = 1;
	} else
		return (EINVAL);

	/* Check if already connected (shouldn't be, but doesn't hurt) */
	if (*hookptr != NULL)
		return (EISCONN);

	/* OK */
	*hookptr = hook;
	priv->lowerOrphan = orphan;
	return (0);
}

/*
 * Hooks are attached, adjust to force queueing.
 * We don't really care which hook it is.
 * they should all be queuing for outgoing data.
 */
static	int
ng_gif_connect(hook_p hook)
{
	NG_HOOK_FORCE_QUEUE(NG_HOOK_PEER(hook));
	return (0);
}

/*
 * Receive an incoming control message.
 */
static int
ng_gif_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_GIF_COOKIE:
		switch (msg->header.cmd) {
		case NGM_GIF_GET_IFNAME:
			NG_MKRESPONSE(resp, msg, IFNAMSIZ, M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			strlcpy(resp->data, priv->ifp->if_xname, IFNAMSIZ);
			break;
		case NGM_GIF_GET_IFINDEX:
			NG_MKRESPONSE(resp, msg, sizeof(u_int32_t), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			*((u_int32_t *)resp->data) = priv->ifp->if_index;
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
 */
static int
ng_gif_rcvdata(hook_p hook, item_p item)
{
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct mbuf *m;

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if (hook == priv->lower)
		return ng_gif_rcv_lower(node, m);
	panic("%s: weird hook", __func__);
}

/*
 * Handle an mbuf received on the "lower" hook.
 */
static int
ng_gif_rcv_lower(node_p node, struct mbuf *m)
{
	struct sockaddr	dst;
	const priv_p priv = NG_NODE_PRIVATE(node);

	bzero(&dst, sizeof(dst));

	/* Make sure header is fully pulled up */
	if (m->m_pkthdr.len < sizeof(sa_family_t)) {
		NG_FREE_M(m);
		return (EINVAL);
	}
	if (m->m_len < sizeof(sa_family_t)
	    && (m = m_pullup(m, sizeof(sa_family_t))) == NULL) {
		return (ENOBUFS);
	}

	dst.sa_family = *mtod(m, sa_family_t *);
	m_adj(m, sizeof(sa_family_t));

	/* Send it on its way */
	/*
	 * XXX: gif_output only uses dst for the family and passes the
	 * fourth argument (rt) to in{,6}_gif_output which ignore it.
	 * If this changes ng_gif will probably break.
	 */
	return gif_output(priv->ifp, m, &dst, NULL);
}

/*
 * Shutdown node. This resets the node but does not remove it
 * unless the REALLY_DIE flag is set.
 */
static int
ng_gif_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (node->nd_flags & NGF_REALLY_DIE) {
		/*
		 * WE came here because the gif interface is being destroyed,
		 * so stop being persistent.
		 * Actually undo all the things we did on creation.
		 * Assume the ifp has already been freed.
		 */
		NG_NODE_SET_PRIVATE(node, NULL);
		free(priv, M_NETGRAPH);		
		NG_NODE_UNREF(node);	/* free node itself */
		return (0);
	}
	NG_NODE_REVIVE(node);		/* Signal ng_rmnode we are persisant */
	return (0);
}

/*
 * Hook disconnection.
 */
static int
ng_gif_disconnect(hook_p hook)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (hook == priv->lower) {
		priv->lower = NULL;
		priv->lowerOrphan = 0;
	} else 
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
ng_gif_mod_event(module_t mod, int event, void *data)
{
	VNET_ITERATOR_DECL(vnet_iter);
	struct ifnet *ifp;
	int error = 0;

	switch (event) {
	case MOD_LOAD:

		/* Register function hooks */
		if (ng_gif_attach_p != NULL) {
			error = EEXIST;
			break;
		}
		ng_gif_attach_p = ng_gif_attach;
		ng_gif_detach_p = ng_gif_detach;
		ng_gif_input_p = ng_gif_input;
		ng_gif_input_orphan_p = ng_gif_input_orphan;

		/* Create nodes for any already-existing gif interfaces */
		VNET_LIST_RLOCK();
		IFNET_RLOCK();
		VNET_FOREACH(vnet_iter) {
			CURVNET_SET_QUIET(vnet_iter); /* XXX revisit quiet */
			CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
				if (ifp->if_type == IFT_GIF)
					ng_gif_attach(ifp);
			}
			CURVNET_RESTORE();
		}
		IFNET_RUNLOCK();
		VNET_LIST_RUNLOCK();
		break;

	case MOD_UNLOAD:

		/*
		 * Note that the base code won't try to unload us until
		 * all nodes have been removed, and that can't happen
		 * until all gif interfaces are destroyed. In any
		 * case, we know there are no nodes left if the action
		 * is MOD_UNLOAD, so there's no need to detach any nodes.
		 *
		 * XXX: what about manual unloads?!?
		 */

		/* Unregister function hooks */
		ng_gif_attach_p = NULL;
		ng_gif_detach_p = NULL;
		ng_gif_input_p = NULL;
		ng_gif_input_orphan_p = NULL;
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}


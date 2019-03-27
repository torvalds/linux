/*
 * ng_sppp.c Netgraph to Sppp module.
 */

/*-
 * Copyright (C) 2002-2004 Cronyx Engineering.
 * Copyright (C) 2002-2004 Roman Kurakin <rik@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * Cronyx Id: ng_sppp.c,v 1.1.2.10 2004/03/01 15:17:21 rik Exp $
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/libkern.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/bpf.h>
#include <net/if_sppp.h>

#include <netinet/in.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_sppp.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_SPPP, "netgraph_sppp", "netgraph sppp node");
#else
#define M_NETGRAPH_SPPP M_NETGRAPH
#endif

/* Node private data */
struct ng_sppp_private {
	struct	ifnet *ifp;		/* Our interface */
	int	unit;			/* Interface unit number */
	node_p	node;			/* Our netgraph node */
	hook_p	hook;			/* Hook */
};
typedef struct ng_sppp_private *priv_p;

/* Interface methods */
static void	ng_sppp_start (struct ifnet *ifp);
static int	ng_sppp_ioctl (struct ifnet *ifp, u_long cmd, caddr_t data);

/* Netgraph methods */
static ng_constructor_t	ng_sppp_constructor;
static ng_rcvmsg_t	ng_sppp_rcvmsg;
static ng_shutdown_t	ng_sppp_shutdown;
static ng_newhook_t	ng_sppp_newhook;
static ng_rcvdata_t	ng_sppp_rcvdata;
static ng_disconnect_t	ng_sppp_disconnect;

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_sppp_cmds[] = {
	{
	  NGM_SPPP_COOKIE,
	  NGM_SPPP_GET_IFNAME,
	  "getifname",
	  NULL,
	  &ng_parse_string_type
	},
	{ 0 }
};

/* Node type descriptor */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_SPPP_NODE_TYPE,
	.constructor =	ng_sppp_constructor,
	.rcvmsg =	ng_sppp_rcvmsg,
	.shutdown =	ng_sppp_shutdown,
	.newhook =	ng_sppp_newhook,
	.rcvdata =	ng_sppp_rcvdata,
	.disconnect =	ng_sppp_disconnect,
	.cmdlist =	ng_sppp_cmds,
};
NETGRAPH_INIT(sppp, &typestruct);

MODULE_DEPEND (ng_sppp, sppp, 1, 1, 1);

/* We keep a bitmap indicating which unit numbers are free.
   Zero means the unit number is free, one means it's taken. */
static unsigned char	*ng_sppp_units = NULL;
static unsigned char	ng_sppp_units_len = 0;
static unsigned char	ng_units_in_use = 0;

/*
 * Find the first free unit number for a new interface.
 * Increase the size of the unit bitmap as necessary.
 */
static __inline void
ng_sppp_get_unit (int *unit)
{
	int index, bit;
	unsigned char mask;

	for (index = 0; index < ng_sppp_units_len
	    && ng_sppp_units[index] == 0xFF; index++);
	if (index == ng_sppp_units_len) {		/* extend array */
		unsigned char *newarray;
		int newlen;
		
		newlen = (2 * ng_sppp_units_len) + sizeof (*ng_sppp_units);
		newarray = malloc (newlen * sizeof (*ng_sppp_units),
		    M_NETGRAPH_SPPP, M_WAITOK);
		bcopy (ng_sppp_units, newarray,
		    ng_sppp_units_len * sizeof (*ng_sppp_units));
		bzero (newarray + ng_sppp_units_len,
		    newlen - ng_sppp_units_len);
		if (ng_sppp_units != NULL)
			free (ng_sppp_units, M_NETGRAPH_SPPP);
		ng_sppp_units = newarray;
		ng_sppp_units_len = newlen;
	}
	mask = ng_sppp_units[index];
	for (bit = 0; (mask & 1) != 0; bit++)
		mask >>= 1;
	KASSERT ((bit >= 0 && bit < NBBY),
	    ("%s: word=%d bit=%d", __func__, ng_sppp_units[index], bit));
	ng_sppp_units[index] |= (1 << bit);
	*unit = (index * NBBY) + bit;
	ng_units_in_use++;
}

/*
 * Free a no longer needed unit number.
 */
static __inline void
ng_sppp_free_unit (int unit)
{
	int index, bit;

	index = unit / NBBY;
	bit = unit % NBBY;
	KASSERT (index < ng_sppp_units_len,
	    ("%s: unit=%d len=%d", __func__, unit, ng_sppp_units_len));
	KASSERT ((ng_sppp_units[index] & (1 << bit)) != 0,
	    ("%s: unit=%d is free", __func__, unit));
	ng_sppp_units[index] &= ~(1 << bit);

	ng_units_in_use--;
	if (ng_units_in_use == 0) {
		free (ng_sppp_units, M_NETGRAPH_SPPP);
		ng_sppp_units_len = 0;
		ng_sppp_units = NULL;
	}
}

/************************************************************************
			INTERFACE STUFF
 ************************************************************************/

/*
 * Process an ioctl for the interface
 */
static int
ng_sppp_ioctl (struct ifnet *ifp, u_long command, caddr_t data)
{
	int error = 0;

	error = sppp_ioctl (ifp, command, data);
	if (error)
		return error;

	return error;
}

/*
 * This routine should never be called
 */

static void
ng_sppp_start (struct ifnet *ifp)
{
	struct mbuf *m;
	int len, error = 0;
	priv_p priv = ifp->if_softc;
	
	/* Check interface flags */
	/*
	 * This has side effects. It is not good idea to stop sending if we
	 * are not UP. If we are not running we still want to send LCP term
	 * packets.
	 */
/*	if (!((ifp->if_flags & IFF_UP) && */
/*	    (ifp->if_drv_flags & IFF_DRV_RUNNING))) { */
/*		return;*/
/*	}*/
	
	if (ifp->if_drv_flags & IFF_DRV_OACTIVE)
		return;
		
	if (!priv->hook)
		return;
		
	ifp->if_drv_flags |= IFF_DRV_OACTIVE;

	while ((m = sppp_dequeue (ifp)) != NULL) {
		BPF_MTAP (ifp, m);
		len = m->m_pkthdr.len;
		
		NG_SEND_DATA_ONLY (error, priv->hook, m);
		
		if (error) {
			ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
			return;
		}
	}
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

/************************************************************************
			NETGRAPH NODE STUFF
 ************************************************************************/

/*
 * Constructor for a node
 */
static int
ng_sppp_constructor (node_p node)
{
	struct sppp *pp;
	struct ifnet *ifp;
	priv_p priv;

	/* Allocate node and interface private structures */
	priv = malloc(sizeof(*priv), M_NETGRAPH_SPPP, M_WAITOK | M_ZERO);

	ifp = if_alloc(IFT_PPP);
	if (ifp == NULL) {
		free (priv, M_NETGRAPH_SPPP);
		return (ENOSPC);
	}
	pp = IFP2SP(ifp);

	/* Link them together */
	ifp->if_softc = priv;
	priv->ifp = ifp;

	/* Get an interface unit number */
	ng_sppp_get_unit(&priv->unit);

	/* Link together node and private info */
	NG_NODE_SET_PRIVATE (node, priv);
	priv->node = node;

	/* Initialize interface structure */
	if_initname (SP2IFP(pp), NG_SPPP_IFACE_NAME, priv->unit);
	ifp->if_start = ng_sppp_start;
	ifp->if_ioctl = ng_sppp_ioctl;
	ifp->if_flags = (IFF_POINTOPOINT|IFF_MULTICAST);

	/* Give this node the same name as the interface (if possible) */
	if (ng_name_node(node, SP2IFP(pp)->if_xname) != 0)
		log (LOG_WARNING, "%s: can't acquire netgraph name\n",
		    SP2IFP(pp)->if_xname);

	/* Attach the interface */
	sppp_attach (ifp);
	if_attach (ifp);
	bpfattach (ifp, DLT_NULL, sizeof(u_int32_t));

	/* Done */
	return (0);
}

/*
 * Give our ok for a hook to be added
 */
static int
ng_sppp_newhook (node_p node, hook_p hook, const char *name)
{
	priv_p priv = NG_NODE_PRIVATE (node);

	if (strcmp (name, NG_SPPP_HOOK_DOWNSTREAM) != 0)
		return (EINVAL);
	
	if (priv->hook)
		return (EISCONN);
		
	priv->hook = hook;
	NG_HOOK_SET_PRIVATE (hook, priv);
	
	return (0);
}

/*
 * Receive a control message
 */
static int
ng_sppp_rcvmsg (node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE (node);
	struct ng_mesg *msg = NULL;
	struct ng_mesg *resp = NULL;
	struct sppp *const pp = IFP2SP(priv->ifp);
	int error = 0;

	NGI_GET_MSG (item, msg);
	switch (msg->header.typecookie) {
	case NGM_SPPP_COOKIE:
		switch (msg->header.cmd) {
		case NGM_SPPP_GET_IFNAME:
			NG_MKRESPONSE (resp, msg, IFNAMSIZ, M_NOWAIT);
			if (!resp) {
				error = ENOMEM;
				break;
			}
			strlcpy(resp->data, SP2IFP(pp)->if_xname, IFNAMSIZ);
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
	NG_RESPOND_MSG (error, node, item, resp);
	NG_FREE_MSG (msg);
	return (error);
}

/*
 * Recive data from a hook. Pass the packet to the correct input routine.
 */
static int
ng_sppp_rcvdata (hook_p hook, item_p item)
{
	struct mbuf *m;
	const priv_p priv = NG_NODE_PRIVATE (NG_HOOK_NODE (hook));
	struct sppp *const pp = IFP2SP(priv->ifp);

	NGI_GET_M (item, m);
	NG_FREE_ITEM (item);
	/* Sanity checks */
	KASSERT (m->m_flags & M_PKTHDR, ("%s: not pkthdr", __func__));
	if ((SP2IFP(pp)->if_flags & IFF_UP) == 0) {
		NG_FREE_M (m);
		return (ENETDOWN);
	}

	/* Update interface stats */
	if_inc_counter(SP2IFP(pp), IFCOUNTER_IPACKETS, 1);

	/* Note receiving interface */
	m->m_pkthdr.rcvif = SP2IFP(pp);

	/* Berkeley packet filter */
	BPF_MTAP (SP2IFP(pp), m);

	/* Send packet */
	sppp_input (SP2IFP(pp), m);
	return 0;
}

/*
 * Shutdown and remove the node and its associated interface.
 */
static int
ng_sppp_shutdown (node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	/* Detach from the packet filter list of interfaces. */
	bpfdetach (priv->ifp);
	sppp_detach (priv->ifp);
	if_detach (priv->ifp);
	if_free(priv->ifp);
	ng_sppp_free_unit (priv->unit);
	free (priv, M_NETGRAPH_SPPP);
	NG_NODE_SET_PRIVATE (node, NULL);
	NG_NODE_UNREF (node);
	return (0);
}

/*
 * Hook disconnection.
 */
static int
ng_sppp_disconnect (hook_p hook)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (priv)
		priv->hook = NULL;

	return (0);
}

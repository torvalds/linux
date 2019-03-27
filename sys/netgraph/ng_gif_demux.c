/*
 * ng_gif_demux.c
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
 * $FreeBSD$
 */

/*
 * ng_gif_demux(4) netgraph node type
 *
 * Packets received on the "gif" hook have their type header removed
 * and are passed to the appropriate hook protocol hook.  Packets
 * received on a protocol hook have a type header added back and are
 * passed out the gif hook. The currently supported protocol hooks are:
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/socket.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_gif_demux.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_GIF_DEMUX, "netgraph_gif_demux",
    "netgraph gif demux node");
#else
#define M_NETGRAPH_GIF_DEMUX M_NETGRAPH
#endif

/* This struct describes one address family */
struct iffam {
	sa_family_t	family;		/* Address family */
	const char	*hookname;	/* Name for hook */
};
typedef const struct iffam *iffam_p;

/* List of address families supported by our interface */
const static struct iffam gFamilies[] = {
	{ AF_INET,	NG_GIF_DEMUX_HOOK_INET	},
	{ AF_INET6,	NG_GIF_DEMUX_HOOK_INET6	},
	{ AF_APPLETALK,	NG_GIF_DEMUX_HOOK_ATALK	},
	{ AF_IPX,	NG_GIF_DEMUX_HOOK_IPX	},
	{ AF_ATM,	NG_GIF_DEMUX_HOOK_ATM	},
	{ AF_NATM,	NG_GIF_DEMUX_HOOK_NATM	},
};
#define	NUM_FAMILIES		nitems(gFamilies)

/* Per-node private data */
struct ng_gif_demux_private {
	node_p	node;			/* Our netgraph node */
	hook_p	gif;			/* The gif hook */
	hook_p	hooks[NUM_FAMILIES];	/* The protocol hooks */
};
typedef struct ng_gif_demux_private *priv_p;

/* Netgraph node methods */
static ng_constructor_t	ng_gif_demux_constructor;
static ng_rcvmsg_t	ng_gif_demux_rcvmsg;
static ng_shutdown_t	ng_gif_demux_shutdown;
static ng_newhook_t	ng_gif_demux_newhook;
static ng_rcvdata_t	ng_gif_demux_rcvdata;
static ng_disconnect_t	ng_gif_demux_disconnect;

/* Helper stuff */
static iffam_p	get_iffam_from_af(sa_family_t family);
static iffam_p	get_iffam_from_hook(priv_p priv, hook_p hook);
static iffam_p	get_iffam_from_name(const char *name);
static hook_p	*get_hook_from_iffam(priv_p priv, iffam_p iffam);

/******************************************************************
		    NETGRAPH PARSE TYPES
******************************************************************/

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_gif_demux_cmdlist[] = {
	{ 0 }
};

/* Node type descriptor */
static struct ng_type ng_gif_demux_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_GIF_DEMUX_NODE_TYPE,
	.constructor =	ng_gif_demux_constructor,
	.rcvmsg =	ng_gif_demux_rcvmsg,
	.shutdown =	ng_gif_demux_shutdown,
	.newhook =	ng_gif_demux_newhook,
	.rcvdata =	ng_gif_demux_rcvdata,
	.disconnect =	ng_gif_demux_disconnect,
	.cmdlist =	ng_gif_demux_cmdlist,
};
NETGRAPH_INIT(gif_demux, &ng_gif_demux_typestruct);

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

/******************************************************************
		    NETGRAPH NODE METHODS
******************************************************************/

/*
 * Node constructor
 */
static int
ng_gif_demux_constructor(node_p node)
{
	priv_p priv;

	/* Allocate and initialize private info */
	priv = malloc(sizeof(*priv), M_NETGRAPH_GIF_DEMUX, M_WAITOK | M_ZERO);
	priv->node = node;

	NG_NODE_SET_PRIVATE(node, priv);

	/* Done */
	return (0);
}

/*
 * Method for attaching a new hook
 */
static	int
ng_gif_demux_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	iffam_p iffam;
	hook_p *hookptr;

	if (strcmp(NG_GIF_DEMUX_HOOK_GIF, name) == 0)
		hookptr = &priv->gif;
	else {
		iffam = get_iffam_from_name(name);
		if (iffam == NULL)
			return (EPFNOSUPPORT);
		hookptr = get_hook_from_iffam(NG_NODE_PRIVATE(node), iffam);
	}
	if (*hookptr != NULL)
		return (EISCONN);
	*hookptr = hook;
	return (0);
}

/*
 * Receive a control message
 */
static int
ng_gif_demux_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_GIF_DEMUX_COOKIE:
		switch (msg->header.cmd) {
		/* XXX: Add commands here. */
		default:
			error = EINVAL;
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}

	/* Done */
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/*
 * Receive data on a hook
 */
static int
ng_gif_demux_rcvdata(hook_p hook, item_p item)
{
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	iffam_p iffam;
	hook_p outhook;
	int error = 0;
	struct mbuf *m;

	/* Pull the mbuf out of the item for processing. */
	NGI_GET_M(item, m);

	if (hook == priv->gif) {
		/*
		 * Pull off the address family header and find the
		 * output hook.
		 */
		if (m->m_pkthdr.len < sizeof(sa_family_t)) {
			NG_FREE_M(m);
			NG_FREE_ITEM(item);
			return (EINVAL);
		}
		if (m->m_len < sizeof(sa_family_t)
		    && (m = m_pullup(m, sizeof(sa_family_t))) == NULL) {
			NG_FREE_ITEM(item);
			return (ENOBUFS);
		}
		iffam = get_iffam_from_af(*mtod(m, sa_family_t *));
		if (iffam == NULL) {
			NG_FREE_M(m);
			NG_FREE_ITEM(item);
			return (EINVAL);
		}
		outhook = *get_hook_from_iffam(priv, iffam);
		m_adj(m, sizeof(sa_family_t));
	} else {
		/*
		 * Add address family header and set the output hook.
		 */
		iffam = get_iffam_from_hook(priv, hook);
		M_PREPEND(m, sizeof (iffam->family), M_NOWAIT);
		if (m == NULL) {
			NG_FREE_M(m);
			NG_FREE_ITEM(item);
			return (ENOBUFS);
		}
		bcopy(&iffam->family, mtod(m, sa_family_t *),
		    sizeof(iffam->family));
		outhook = priv->gif;
	}

	/* Stuff the mbuf back in. */
	NGI_M(item) = m;

	/* Deliver packet */
	NG_FWD_ITEM_HOOK(error, item, outhook);
	return (error);
}

/*
 * Shutdown node
 */
static int
ng_gif_demux_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	free(priv, M_NETGRAPH_GIF_DEMUX);
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);
	return (0);
}

/*
 * Hook disconnection.
 */
static int
ng_gif_demux_disconnect(hook_p hook)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	iffam_p iffam;

	if (hook == priv->gif)
		priv->gif = NULL;
	else {
		iffam = get_iffam_from_hook(priv, hook);
		if (iffam == NULL)
			panic("%s", __func__);
		*get_hook_from_iffam(priv, iffam) = NULL;
	}

	return (0);
}

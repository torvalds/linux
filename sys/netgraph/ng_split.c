/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2000, Vitaly V Belekhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

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

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_split.h>

/* Netgraph methods */
static ng_constructor_t ng_split_constructor;
static ng_shutdown_t ng_split_shutdown;
static ng_newhook_t ng_split_newhook;
static ng_rcvdata_t ng_split_rcvdata;
static ng_disconnect_t ng_split_disconnect;

/* Node type descriptor */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_SPLIT_NODE_TYPE,
	.constructor =	ng_split_constructor,
	.shutdown =	ng_split_shutdown,
	.newhook =	ng_split_newhook,
	.rcvdata =	ng_split_rcvdata,
	.disconnect =	ng_split_disconnect,
};
NETGRAPH_INIT(ng_split, &typestruct);

/* Node private data */
struct ng_split_private {
	hook_p out;
	hook_p in;
	hook_p mixed;
	node_p	node;			/* Our netgraph node */
};
typedef struct ng_split_private *priv_p;

/************************************************************************
			NETGRAPH NODE STUFF
 ************************************************************************/

/*
 * Constructor for a node
 */
static int
ng_split_constructor(node_p node)
{
	priv_p		priv;

	/* Allocate node */
	priv = malloc(sizeof(*priv), M_NETGRAPH, M_ZERO | M_WAITOK);

	/* Link together node and private info */
	NG_NODE_SET_PRIVATE(node, priv);
	priv->node = node;

	/* Done */
	return (0);
}

/*
 * Give our ok for a hook to be added
 */
static int
ng_split_newhook(node_p node, hook_p hook, const char *name)
{
	priv_p		priv = NG_NODE_PRIVATE(node);
	hook_p		*localhook;

	if (strcmp(name, NG_SPLIT_HOOK_MIXED) == 0) {
		localhook = &priv->mixed;
	} else if (strcmp(name, NG_SPLIT_HOOK_IN) == 0) {
		localhook = &priv->in;
	} else if (strcmp(name, NG_SPLIT_HOOK_OUT) == 0) {
		localhook = &priv->out;
	} else
		return (EINVAL);

	if (*localhook != NULL)
		return (EISCONN);
	*localhook = hook;
	NG_HOOK_SET_PRIVATE(hook, localhook);

	return (0);
}

/*
 * Recive data from a hook.
 */
static int
ng_split_rcvdata(hook_p hook, item_p item)
{
	const priv_p	priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	int		error = 0;

	if (hook == priv->out) {
		printf("ng_split: got packet from out hook!\n");
		NG_FREE_ITEM(item);
		error = EINVAL;
	} else if ((hook == priv->in) && (priv->mixed != NULL)) {
		NG_FWD_ITEM_HOOK(error, item, priv->mixed);
	} else if ((hook == priv->mixed) && (priv->out != NULL)) {
		NG_FWD_ITEM_HOOK(error, item, priv->out);
	}

	if (item)
		NG_FREE_ITEM(item);

	return (error);
}

static int
ng_split_shutdown(node_p node)
{
	const priv_p	priv = NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);
	free(priv, M_NETGRAPH);

	return (0);
}

/*
 * Hook disconnection
 */
static int
ng_split_disconnect(hook_p hook)
{
	hook_p		*localhook = NG_HOOK_PRIVATE(hook);
	
	KASSERT(localhook != NULL, ("%s: null info", __func__));
	*localhook = NULL;
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
	    && (NG_NODE_IS_VALID(NG_HOOK_NODE(hook)))) {
		ng_rmnode_self(NG_HOOK_NODE(hook));
	}

	return (0);
}

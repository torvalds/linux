/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Ruslan Ermilov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/systm.h>

#include <netgraph/ng_message.h>
#include <netgraph/ng_hub.h>
#include <netgraph/netgraph.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_HUB, "netgraph_hub", "netgraph hub node");
#else
#define M_NETGRAPH_HUB M_NETGRAPH
#endif

/* Per-node private data */
struct ng_hub_private {
	int		persistent;	/* can exist w/o hooks */
};
typedef struct ng_hub_private *priv_p;

/* Netgraph node methods */
static ng_constructor_t	ng_hub_constructor;
static ng_rcvmsg_t	ng_hub_rcvmsg;
static ng_shutdown_t	ng_hub_shutdown;
static ng_rcvdata_t	ng_hub_rcvdata;
static ng_disconnect_t	ng_hub_disconnect;

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_hub_cmdlist[] = {
	{
		NGM_HUB_COOKIE,
		NGM_HUB_SET_PERSISTENT,
		"setpersistent",
		NULL,
		NULL
	},
	{ 0 }
};

static struct ng_type ng_hub_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_HUB_NODE_TYPE,
	.constructor =	ng_hub_constructor,
	.rcvmsg =	ng_hub_rcvmsg,
	.shutdown =	ng_hub_shutdown,
	.rcvdata =	ng_hub_rcvdata,
	.disconnect =	ng_hub_disconnect,
	.cmdlist =	ng_hub_cmdlist,
};
NETGRAPH_INIT(hub, &ng_hub_typestruct);


static int
ng_hub_constructor(node_p node)
{
	priv_p priv;

	/* Allocate and initialize private info */
	priv = malloc(sizeof(*priv), M_NETGRAPH_HUB, M_WAITOK | M_ZERO);

	NG_NODE_SET_PRIVATE(node, priv);
	return (0);
}

/*
 * Receive a control message
 */
static int
ng_hub_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	if (msg->header.typecookie == NGM_HUB_COOKIE &&
	    msg->header.cmd == NGM_HUB_SET_PERSISTENT) {
		priv->persistent = 1;
	} else {
		error = EINVAL;
	}

	NG_FREE_MSG(msg);
	return (error);
}

static int
ng_hub_rcvdata(hook_p hook, item_p item)
{
	const node_p node = NG_HOOK_NODE(hook);
	int error = 0;
	hook_p hook2;
	struct mbuf * const m = NGI_M(item), *m2;
	int nhooks;

	if ((nhooks = NG_NODE_NUMHOOKS(node)) == 1) {
		NG_FREE_ITEM(item);
		return (0);
	}
	LIST_FOREACH(hook2, &node->nd_hooks, hk_hooks) {
		if (hook2 == hook)
			continue;
		if (--nhooks == 1)
			NG_FWD_ITEM_HOOK(error, item, hook2);
		else {
			if ((m2 = m_dup(m, M_NOWAIT)) == NULL) {
				NG_FREE_ITEM(item);
				return (ENOBUFS);
			}
			NG_SEND_DATA_ONLY(error, hook2, m2);
			if (error)
				continue;	/* don't give up */
		}
	}

	return (error);
}

/*
 * Shutdown node
 */
static int
ng_hub_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	free(priv, M_NETGRAPH_HUB);
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);
	return (0);
}

static int
ng_hub_disconnect(hook_p hook)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0 &&
	    NG_NODE_IS_VALID(NG_HOOK_NODE(hook)) && !priv->persistent)
		ng_rmnode_self(NG_HOOK_NODE(hook));
	return (0);
}

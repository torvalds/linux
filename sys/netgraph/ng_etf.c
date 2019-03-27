/*-
 * ng_etf.c  Ethertype filter
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001, FreeBSD Incorporated 
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
 * Author: Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/errno.h>
#include <sys/queue.h>
#include <sys/syslog.h>

#include <net/ethernet.h>

#include <netgraph/ng_message.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_etf.h>
#include <netgraph/netgraph.h>

/* If you do complicated mallocs you may want to do this */
/* and use it for your mallocs */
#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_ETF, "netgraph_etf", "netgraph etf node ");
#else
#define M_NETGRAPH_ETF M_NETGRAPH
#endif

/*
 * This section contains the netgraph method declarations for the
 * etf node. These methods define the netgraph 'type'.
 */

static ng_constructor_t	ng_etf_constructor;
static ng_rcvmsg_t	ng_etf_rcvmsg;
static ng_shutdown_t	ng_etf_shutdown;
static ng_newhook_t	ng_etf_newhook;
static ng_rcvdata_t	ng_etf_rcvdata;	 /* note these are both ng_rcvdata_t */
static ng_disconnect_t	ng_etf_disconnect;

/* Parse type for struct ng_etfstat */
static const struct ng_parse_struct_field ng_etf_stat_type_fields[]
	= NG_ETF_STATS_TYPE_INFO;
static const struct ng_parse_type ng_etf_stat_type = {
	&ng_parse_struct_type,
	&ng_etf_stat_type_fields
};
/* Parse type for struct ng_setfilter */
static const struct ng_parse_struct_field ng_etf_filter_type_fields[]
	= NG_ETF_FILTER_TYPE_INFO;
static const struct ng_parse_type ng_etf_filter_type = {
	&ng_parse_struct_type,
	&ng_etf_filter_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_etf_cmdlist[] = {
	{
	  NGM_ETF_COOKIE,
	  NGM_ETF_GET_STATUS,
	  "getstatus",
	  NULL,
	  &ng_etf_stat_type,
	},
	{
	  NGM_ETF_COOKIE,
	  NGM_ETF_SET_FLAG,
	  "setflag",
	  &ng_parse_int32_type,
	  NULL
	},
	{
	  NGM_ETF_COOKIE,
	  NGM_ETF_SET_FILTER,
	  "setfilter",
	  &ng_etf_filter_type,
	  NULL
	},
	{ 0 }
};

/* Netgraph node type descriptor */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_ETF_NODE_TYPE,
	.constructor =	ng_etf_constructor,
	.rcvmsg =	ng_etf_rcvmsg,
	.shutdown =	ng_etf_shutdown,
	.newhook =	ng_etf_newhook,
	.rcvdata =	ng_etf_rcvdata,
	.disconnect =	ng_etf_disconnect,
	.cmdlist =	ng_etf_cmdlist,
};
NETGRAPH_INIT(etf, &typestruct);

/* Information we store for each hook on each node */
struct ETF_hookinfo {
	hook_p  hook;
};

struct filter {
	LIST_ENTRY(filter) next;
	u_int16_t	ethertype;	/* network order ethertype */
	hook_p		match_hook;	/* Hook to use on a match */
};

#define HASHSIZE 16 /* Dont change this without changing HASH() */
#define HASH(et) ((((et)>>12)+((et)>>8)+((et)>>4)+(et)) & 0x0f)
LIST_HEAD(filterhead, filter);

/* Information we store for each node */
struct ETF {
	struct ETF_hookinfo downstream_hook;
	struct ETF_hookinfo nomatch_hook;
	node_p		node;		/* back pointer to node */
	u_int   	packets_in;	/* packets in from downstream */
	u_int   	packets_out;	/* packets out towards downstream */
	u_int32_t	flags;
	struct filterhead hashtable[HASHSIZE];
};
typedef struct ETF *etf_p;

static struct filter *
ng_etf_findentry(etf_p etfp, u_int16_t ethertype)
{
	struct filterhead *chain = etfp->hashtable + HASH(ethertype);
	struct filter *fil;
	

	LIST_FOREACH(fil, chain, next) {
		if (fil->ethertype == ethertype) {
			return (fil);
		}
	}
	return (NULL);
}


/*
 * Allocate the private data structure. The generic node has already
 * been created. Link them together. We arrive with a reference to the node
 * i.e. the reference count is incremented for us already.
 */
static int
ng_etf_constructor(node_p node)
{
	etf_p privdata;
	int i;

	/* Initialize private descriptor */
	privdata = malloc(sizeof(*privdata), M_NETGRAPH_ETF, M_WAITOK | M_ZERO);
	for (i = 0; i < HASHSIZE; i++) {
		LIST_INIT((privdata->hashtable + i));
	}

	/* Link structs together; this counts as our one reference to node */
	NG_NODE_SET_PRIVATE(node, privdata);
	privdata->node = node;
	return (0);
}

/*
 * Give our ok for a hook to be added...
 * All names are ok. Two names are special.
 */
static int
ng_etf_newhook(node_p node, hook_p hook, const char *name)
{
	const etf_p etfp = NG_NODE_PRIVATE(node);
	struct ETF_hookinfo *hpriv;

	if (strcmp(name, NG_ETF_HOOK_DOWNSTREAM) == 0) {
		etfp->downstream_hook.hook = hook;
		NG_HOOK_SET_PRIVATE(hook, &etfp->downstream_hook);
		etfp->packets_in = 0;
		etfp->packets_out = 0;
	} else if (strcmp(name, NG_ETF_HOOK_NOMATCH) == 0) {
		etfp->nomatch_hook.hook = hook;
		NG_HOOK_SET_PRIVATE(hook, &etfp->nomatch_hook);
	} else {
		/*
		 * Any other hook name is valid and can
		 * later be associated with a filter rule.
		 */
		hpriv = malloc(sizeof(*hpriv),
			M_NETGRAPH_ETF, M_NOWAIT | M_ZERO);
		if (hpriv == NULL) {
			return (ENOMEM);
		}

		NG_HOOK_SET_PRIVATE(hook, hpriv);
		hpriv->hook = hook;
	}
	return(0);
}

/*
 * Get a netgraph control message.
 * We actually receive a queue item that has a pointer to the message.
 * If we free the item, the message will be freed too, unless we remove
 * it from the item using NGI_GET_MSG();
 * The return address is also stored in the item, as an ng_ID_t,
 * accessible as NGI_RETADDR(item);
 * Check it is one we understand. If needed, send a response.
 * We could save the address for an async action later, but don't here.
 * Always free the message.
 * The response should be in a malloc'd region that the caller can 'free'.
 * The NG_MKRESPONSE macro does all this for us.
 * A response is not required.
 * Theoretically you could respond defferently to old message types if
 * the cookie in the header didn't match what we consider to be current
 * (so that old userland programs could continue to work).
 */
static int
ng_etf_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const etf_p etfp = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	/* Deal with message according to cookie and command */
	switch (msg->header.typecookie) {
	case NGM_ETF_COOKIE: 
		switch (msg->header.cmd) {
		case NGM_ETF_GET_STATUS:
		    {
			struct ng_etfstat *stats;

			NG_MKRESPONSE(resp, msg, sizeof(*stats), M_NOWAIT);
			if (!resp) {
				error = ENOMEM;
				break;
			}
			stats = (struct ng_etfstat *) resp->data;
			stats->packets_in = etfp->packets_in;
			stats->packets_out = etfp->packets_out;
			break;
		    }
		case NGM_ETF_SET_FLAG:
			if (msg->header.arglen != sizeof(u_int32_t)) {
				error = EINVAL;
				break;
			}
			etfp->flags = *((u_int32_t *) msg->data);
			break;
		case NGM_ETF_SET_FILTER:
			{
				struct ng_etffilter *f;
				struct filter *fil;
				hook_p  hook;

				/* Check message long enough for this command */
				if (msg->header.arglen != sizeof(*f)) {
					error = EINVAL;
					break;
				}

				/* Make sure hook referenced exists */
				f = (struct ng_etffilter *)msg->data;
				hook = ng_findhook(node, f->matchhook);
				if (hook == NULL) {
					error = ENOENT;
					break;
				}

				/* and is not the downstream hook */
				if (hook == etfp->downstream_hook.hook) {
					error = EINVAL;
					break;
				}

				/* Check we don't already trap this ethertype */
				if (ng_etf_findentry(etfp,
						htons(f->ethertype))) {
					error = EEXIST;
					break;
				}

				/*
				 * Ok, make the filter and put it in the 
				 * hashtable ready for matching.
				 */
				fil = malloc(sizeof(*fil),
					M_NETGRAPH_ETF, M_NOWAIT | M_ZERO);
				if (fil == NULL) {
					error = ENOMEM;
					break;
				}

				fil->match_hook = hook;
				fil->ethertype = htons(f->ethertype);
				LIST_INSERT_HEAD( etfp->hashtable
					+ HASH(fil->ethertype),
						fil, next);
			}
			break;
		default:
			error = EINVAL;		/* unknown command */
			break;
		}
		break;
	default:
		error = EINVAL;			/* unknown cookie type */
		break;
	}

	/* Take care of synchronous response, if any */
	NG_RESPOND_MSG(error, node, item, resp);
	/* Free the message and return */
	NG_FREE_MSG(msg);
	return(error);
}

/*
 * Receive data, and do something with it.
 * Actually we receive a queue item which holds the data.
 * If we free the item it will also free the data unless we have previously
 * disassociated it using the NGI_GET_etf() macro.
 * Possibly send it out on another link after processing.
 * Possibly do something different if it comes from different
 * hooks. The caller will never free m , so if we use up this data
 * or abort we must free it.
 *
 * If we want, we may decide to force this data to be queued and reprocessed
 * at the netgraph NETISR time.
 * We would do that by setting the HK_QUEUE flag on our hook. We would do that
 * in the connect() method. 
 */
static int
ng_etf_rcvdata(hook_p hook, item_p item )
{
	const etf_p etfp = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct ether_header *eh;
	int error = 0;
	struct mbuf *m;
	u_int16_t ethertype;
	struct filter *fil;

	if (NG_HOOK_PRIVATE(hook) == NULL) { /* Shouldn't happen but.. */
		NG_FREE_ITEM(item);
	}

	/* 
	 * Everything not from the downstream hook goes to the
	 * downstream hook. But only if it matches the ethertype
	 * of the source hook. Un matching must go to/from 'nomatch'.
	 */

	/* Make sure we have an entire header */
	NGI_GET_M(item, m);
	if (m->m_len < sizeof(*eh) ) {
		m = m_pullup(m, sizeof(*eh));
		if (m == NULL) {
			NG_FREE_ITEM(item);
			return(EINVAL);
		}
	}

	eh = mtod(m, struct ether_header *);
	ethertype = eh->ether_type;
	fil = ng_etf_findentry(etfp, ethertype);

	/*
	 * if from downstream, select between a match hook or
	 * the nomatch hook
	 */
	if (hook == etfp->downstream_hook.hook) {
		etfp->packets_in++;
		if (fil && fil->match_hook) {
			NG_FWD_NEW_DATA(error, item, fil->match_hook, m);
		} else {
			NG_FWD_NEW_DATA(error, item,etfp->nomatch_hook.hook, m);
		}
	} else {
		/* 
		 * It must be heading towards the downstream.
		 * Check that it's ethertype matches 
		 * the filters for it's input hook.
		 * If it doesn't have one, check it's from nomatch.
		 */
		if ((fil && (fil->match_hook != hook))
		|| ((fil == NULL) && (hook != etfp->nomatch_hook.hook))) {
			NG_FREE_ITEM(item);
			NG_FREE_M(m);
			return (EPROTOTYPE);
		}
		NG_FWD_NEW_DATA( error, item, etfp->downstream_hook.hook, m);
		if (error == 0) {
			etfp->packets_out++;
		}
	}
	return (error);
}

/*
 * Do local shutdown processing..
 * All our links and the name have already been removed.
 */
static int
ng_etf_shutdown(node_p node)
{
	const etf_p privdata = NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(privdata->node);
	free(privdata, M_NETGRAPH_ETF);
	return (0);
}

/*
 * Hook disconnection
 *
 * For this type, removal of the last link destroys the node
 */
static int
ng_etf_disconnect(hook_p hook)
{
	const etf_p etfp = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	int i;
	struct filter *fil1, *fil2;

	/* purge any rules that refer to this filter */
	for (i = 0; i < HASHSIZE; i++) {
		fil1 = LIST_FIRST(&etfp->hashtable[i]);
		while (fil1 != NULL) {
			fil2 = LIST_NEXT(fil1, next);
			if (fil1->match_hook == hook) {
				LIST_REMOVE(fil1, next);
				free(fil1, M_NETGRAPH_ETF);
			}
			fil1 = fil2;
		}
	}
		
	/* If it's not one of the special hooks, then free it */
	if (hook == etfp->downstream_hook.hook) {
		etfp->downstream_hook.hook = NULL;
	} else if (hook == etfp->nomatch_hook.hook) {
		etfp->nomatch_hook.hook = NULL;
	} else {
		if (NG_HOOK_PRIVATE(hook)) /* Paranoia */
			free(NG_HOOK_PRIVATE(hook), M_NETGRAPH_ETF);
	}

	NG_HOOK_SET_PRIVATE(hook, NULL);

	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
	&& (NG_NODE_IS_VALID(NG_HOOK_NODE(hook)))) /* already shutting down? */
		ng_rmnode_self(NG_HOOK_NODE(hook));
	return (0);
}


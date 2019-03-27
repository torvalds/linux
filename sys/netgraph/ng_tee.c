
/*
 * ng_tee.c
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
 * Author: Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_tee.c,v 1.18 1999/11/01 09:24:52 julian Exp $
 */

/*
 * This node is like the tee(1) command and is useful for ``snooping.''
 * It has 4 hooks: left, right, left2right, and right2left. Data
 * entering from the right is passed to the left and duplicated on
 * right2left, and data entering from the left is passed to the right
 * and duplicated on left2right. Data entering from left2right is
 * sent to left, and data from right2left to right.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_tee.h>

/* Per hook info */
struct hookinfo {
	hook_p			hook;
	struct hookinfo		*dest, *dup;
	struct ng_tee_hookstat	stats;
};
typedef struct hookinfo *hi_p;

/* Per node info */
struct privdata {
	struct hookinfo		left;
	struct hookinfo		right;
	struct hookinfo		left2right;
	struct hookinfo		right2left;
};
typedef struct privdata *sc_p;

/* Netgraph methods */
static ng_constructor_t	ng_tee_constructor;
static ng_rcvmsg_t	ng_tee_rcvmsg;
static ng_close_t	ng_tee_close;
static ng_shutdown_t	ng_tee_shutdown;
static ng_newhook_t	ng_tee_newhook;
static ng_rcvdata_t	ng_tee_rcvdata;
static ng_disconnect_t	ng_tee_disconnect;

/* Parse type for struct ng_tee_hookstat */
static const struct ng_parse_struct_field ng_tee_hookstat_type_fields[]
	= NG_TEE_HOOKSTAT_INFO;
static const struct ng_parse_type ng_tee_hookstat_type = {
	&ng_parse_struct_type,
	&ng_tee_hookstat_type_fields
};

/* Parse type for struct ng_tee_stats */
static const struct ng_parse_struct_field ng_tee_stats_type_fields[]
	= NG_TEE_STATS_INFO(&ng_tee_hookstat_type);
static const struct ng_parse_type ng_tee_stats_type = {
	&ng_parse_struct_type,
	&ng_tee_stats_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_tee_cmds[] = {
	{
	  NGM_TEE_COOKIE,
	  NGM_TEE_GET_STATS,
	  "getstats",
	  NULL,
	  &ng_tee_stats_type
	},
	{
	  NGM_TEE_COOKIE,
	  NGM_TEE_CLR_STATS,
	  "clrstats",
	  NULL,
	  NULL
	},
	{
	  NGM_TEE_COOKIE,
	  NGM_TEE_GETCLR_STATS,
	  "getclrstats",
	  NULL,
	  &ng_tee_stats_type
	},
	{ 0 }
};

/* Netgraph type descriptor */
static struct ng_type ng_tee_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_TEE_NODE_TYPE,
	.constructor =  ng_tee_constructor,
	.rcvmsg =	ng_tee_rcvmsg,
	.close =	ng_tee_close,
	.shutdown =	ng_tee_shutdown,
	.newhook =	ng_tee_newhook,
	.rcvdata =	ng_tee_rcvdata,
	.disconnect =	ng_tee_disconnect,
	.cmdlist =	ng_tee_cmds,
};
NETGRAPH_INIT(tee, &ng_tee_typestruct);

/*
 * Node constructor
 */
static int
ng_tee_constructor(node_p node)
{
	sc_p privdata;

	privdata = malloc(sizeof(*privdata), M_NETGRAPH, M_WAITOK | M_ZERO);

	NG_NODE_SET_PRIVATE(node, privdata);
	return (0);
}

/*
 * Add a hook
 */
static int
ng_tee_newhook(node_p node, hook_p hook, const char *name)
{
	sc_p	privdata = NG_NODE_PRIVATE(node);
	hi_p	hinfo;

	/* Precalculate internal paths. */
	if (strcmp(name, NG_TEE_HOOK_RIGHT) == 0) {
		hinfo = &privdata->right;
		if (privdata->left.dest)
			privdata->left.dup = privdata->left.dest;
		privdata->left.dest = hinfo;
		privdata->right2left.dest = hinfo;
	} else if (strcmp(name, NG_TEE_HOOK_LEFT) == 0) {
		hinfo = &privdata->left;
		if (privdata->right.dest)
			privdata->right.dup = privdata->right.dest;
		privdata->right.dest = hinfo;
		privdata->left2right.dest = hinfo;
	} else if (strcmp(name, NG_TEE_HOOK_RIGHT2LEFT) == 0) {
		hinfo = &privdata->right2left;
		if (privdata->right.dest)
			privdata->right.dup = hinfo;
		else    
			privdata->right.dest = hinfo;
	} else if (strcmp(name, NG_TEE_HOOK_LEFT2RIGHT) == 0) {
		hinfo = &privdata->left2right;
		if (privdata->left.dest)
			privdata->left.dup = hinfo;
		else
			privdata->left.dest = hinfo;
	} else
		return (EINVAL);
	hinfo->hook = hook;
	bzero(&hinfo->stats, sizeof(hinfo->stats));
	NG_HOOK_SET_PRIVATE(hook, hinfo);
	return (0);
}

/*
 * Receive a control message
 */
static int
ng_tee_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const sc_p sc = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_TEE_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TEE_GET_STATS:
		case NGM_TEE_CLR_STATS:
		case NGM_TEE_GETCLR_STATS:
                    {
			struct ng_tee_stats *stats;

                        if (msg->header.cmd != NGM_TEE_CLR_STATS) {
                                NG_MKRESPONSE(resp, msg,
                                    sizeof(*stats), M_NOWAIT);
				if (resp == NULL) {
					error = ENOMEM;
					goto done;
				}
				stats = (struct ng_tee_stats *)resp->data;
				bcopy(&sc->right.stats, &stats->right,
				    sizeof(stats->right));
				bcopy(&sc->left.stats, &stats->left,
				    sizeof(stats->left));
				bcopy(&sc->right2left.stats, &stats->right2left,
				    sizeof(stats->right2left));
				bcopy(&sc->left2right.stats, &stats->left2right,
				    sizeof(stats->left2right));
                        }
                        if (msg->header.cmd != NGM_TEE_GET_STATS) {
				bzero(&sc->right.stats,
				    sizeof(sc->right.stats));
				bzero(&sc->left.stats,
				    sizeof(sc->left.stats));
				bzero(&sc->right2left.stats,
				    sizeof(sc->right2left.stats));
				bzero(&sc->left2right.stats,
				    sizeof(sc->left2right.stats));
			}
                        break;
		    }
		default:
			error = EINVAL;
			break;
		}
		break;
	case NGM_FLOW_COOKIE:
		if (lasthook == sc->left.hook || lasthook == sc->right.hook)  {
			hi_p const hinfo = NG_HOOK_PRIVATE(lasthook);
			if (hinfo && hinfo->dest) {
				NGI_MSG(item) = msg;
				NG_FWD_ITEM_HOOK(error, item, hinfo->dest->hook);
				return (error);
			}
		}
		break;
	default:
		error = EINVAL;
		break;
	}
done:
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/*
 * Receive data on a hook
 *
 * If data comes in the right link send a copy out right2left, and then
 * send the original onwards out through the left link.
 * Do the opposite for data coming in from the left link.
 * Data coming in right2left or left2right is forwarded
 * on through the appropriate destination hook as if it had come
 * from the other side.
 */
static int
ng_tee_rcvdata(hook_p hook, item_p item)
{
	const hi_p hinfo = NG_HOOK_PRIVATE(hook);
	hi_p	h;
	int	error = 0;
	struct mbuf *m;

	m = NGI_M(item);

	/* Update stats on incoming hook */
	hinfo->stats.inOctets += m->m_pkthdr.len;
	hinfo->stats.inFrames++;

	/* Duplicate packet if requried */
	if (hinfo->dup) {
		struct mbuf *m2;

		/* Copy packet (failure will not stop the original)*/
		m2 = m_dup(m, M_NOWAIT);
		if (m2) {
			/* Deliver duplicate */
			h = hinfo->dup;
			NG_SEND_DATA_ONLY(error, h->hook, m2);
			if (error == 0) {
				h->stats.outOctets += m->m_pkthdr.len;
				h->stats.outFrames++;
			}
		}
	}
	/* Deliver frame out destination hook */
	if (hinfo->dest) {
		h = hinfo->dest;
		h->stats.outOctets += m->m_pkthdr.len;
		h->stats.outFrames++;
		NG_FWD_ITEM_HOOK(error, item, h->hook);
	} else
		NG_FREE_ITEM(item);
	return (error);
}

/*
 * We are going to be shut down soon
 *
 * If we have both a left and right hook, then we probably want to extricate
 * ourselves and leave the two peers still linked to each other. Otherwise we
 * should just shut down as a normal node would.
 */
static int
ng_tee_close(node_p node)
{
	const sc_p privdata = NG_NODE_PRIVATE(node);

	if (privdata->left.hook && privdata->right.hook)
		ng_bypass(privdata->left.hook, privdata->right.hook);

	return (0);
}

/*
 * Shutdown processing
 */
static int
ng_tee_shutdown(node_p node)
{
	const sc_p privdata = NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	free(privdata, M_NETGRAPH);
	NG_NODE_UNREF(node);
	return (0);
}

/*
 * Hook disconnection
 */
static int
ng_tee_disconnect(hook_p hook)
{
	sc_p	sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	hi_p const hinfo = NG_HOOK_PRIVATE(hook);

	KASSERT(hinfo != NULL, ("%s: null info", __func__));
	hinfo->hook = NULL;

	/* Recalculate internal paths. */
	if (sc->left.dest == hinfo) {
		sc->left.dest = sc->left.dup;
		sc->left.dup = NULL;
	} else if (sc->left.dup == hinfo)
		sc->left.dup = NULL;
	if (sc->right.dest == hinfo) {
		sc->right.dest = sc->right.dup;
		sc->right.dup = NULL;
	} else if (sc->right.dup == hinfo)
		sc->right.dup = NULL;
	if (sc->left2right.dest == hinfo)
		sc->left2right.dest = NULL;
	if (sc->right2left.dest == hinfo)
		sc->right2left.dest = NULL;

	/* Die when last hook disconnected. */
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0) &&
	    NG_NODE_IS_VALID(NG_HOOK_NODE(hook)))
		ng_rmnode_self(NG_HOOK_NODE(hook));
	return (0);
}

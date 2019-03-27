/*
 * ng_hole.c
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
 * Author: Julian Elisher <julian@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_hole.c,v 1.10 1999/11/01 09:24:51 julian Exp $
 */

/*
 * This node is a 'black hole' that simply discards everything it receives
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_hole.h>

/* Per hook private info. */
struct ng_hole_hookinfo {
	struct ng_hole_hookstat	stats;
};
typedef struct ng_hole_hookinfo *hinfo_p;

/* Parse type for struct ng_hole_hookstat. */
static const struct ng_parse_struct_field ng_hole_hookstat_type_fields[] =
	NG_HOLE_HOOKSTAT_TYPE_INFO;
static const struct ng_parse_type ng_hole_hookstat_type = {
	&ng_parse_struct_type,
	&ng_hole_hookstat_type_fields
};

/* List of commands and how to convert arguments to/from ASCII. */
static const struct ng_cmdlist ng_hole_cmdlist[] = {
	{
	  NGM_HOLE_COOKIE,
	  NGM_HOLE_GET_STATS,
	  "getstats",
	  &ng_parse_hookbuf_type,
	  &ng_hole_hookstat_type
	},
	{
	  NGM_HOLE_COOKIE,
	  NGM_HOLE_CLR_STATS,
	  "clrstats",
	  &ng_parse_hookbuf_type,
	  NULL
	},
	{
	  NGM_HOLE_COOKIE,
	  NGM_HOLE_GETCLR_STATS,
	  "getclrstats",
	  &ng_parse_hookbuf_type,
	  &ng_hole_hookstat_type
	},
	{ 0 }
};

/* Netgraph methods */
static ng_constructor_t	ngh_cons;
static ng_rcvmsg_t	ngh_rcvmsg;
static ng_newhook_t	ngh_newhook;
static ng_rcvdata_t	ngh_rcvdata;
static ng_disconnect_t	ngh_disconnect;

static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_HOLE_NODE_TYPE,
	.constructor =	ngh_cons,
	.rcvmsg =	ngh_rcvmsg,
	.newhook = 	ngh_newhook,
	.rcvdata =	ngh_rcvdata,
	.disconnect =	ngh_disconnect,
	.cmdlist =	ng_hole_cmdlist,
};
NETGRAPH_INIT(hole, &typestruct);

/* 
 * Be obliging. but no work to do.
 */
static int
ngh_cons(node_p node)
{
	return(0);
}

/*
 * Add a hook.
 */
static int
ngh_newhook(node_p node, hook_p hook, const char *name)
{
	hinfo_p hip;

	/* Create hook private structure. */
	hip = malloc(sizeof(*hip), M_NETGRAPH, M_NOWAIT | M_ZERO);
	if (hip == NULL)
		return (ENOMEM);
	NG_HOOK_SET_PRIVATE(hook, hip);
	return (0);
}

/*
 * Receive a control message.
 */
static int
ngh_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct ng_mesg *msg;
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_hole_hookstat *stats;
	hook_p hook;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_HOLE_COOKIE:
		switch (msg->header.cmd) {
		case NGM_HOLE_GET_STATS:
		case NGM_HOLE_CLR_STATS:
		case NGM_HOLE_GETCLR_STATS:
			/* Sanity check. */
			if (msg->header.arglen != NG_HOOKSIZ) {
				error = EINVAL;
				break;
			}
			/* Find hook. */
			hook = ng_findhook(node, (char *)msg->data);
			if (hook == NULL) {
				error = ENOENT;
				break;
			}
			stats = &((hinfo_p)NG_HOOK_PRIVATE(hook))->stats;
			/* Build response (if desired). */
			if (msg->header.cmd != NGM_HOLE_CLR_STATS) {
				NG_MKRESPONSE(resp, msg, sizeof(*stats),
				    M_NOWAIT);
				if (resp == NULL) {
					error = ENOMEM;
					break;
				}
				bcopy(stats, resp->data, sizeof(*stats));
			}
			/* Clear stats (if desired). */
			if (msg->header.cmd != NGM_HOLE_GET_STATS)
				bzero(stats, sizeof(*stats));
			break;
		default:		/* Unknown command. */
			error = EINVAL;
			break;
		}
		break;
	default:			/* Unknown type cookie. */
		error = EINVAL;
		break;
	}
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/*
 * Receive data
 */
static int
ngh_rcvdata(hook_p hook, item_p item)
{
	const hinfo_p hip = NG_HOOK_PRIVATE(hook);

	hip->stats.frames++;
	hip->stats.octets += NGI_M(item)->m_pkthdr.len;
	NG_FREE_ITEM(item);
	return 0;
}

/*
 * Hook disconnection
 */
static int
ngh_disconnect(hook_p hook)
{

	free(NG_HOOK_PRIVATE(hook), M_NETGRAPH);
	NG_HOOK_SET_PRIVATE(hook, NULL);
	if (NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
		ng_rmnode_self(NG_HOOK_NODE(hook));
	return (0);
}

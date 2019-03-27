/*
 * ng_bpf.c
 */

/*-
 * Copyright (c) 1999 Whistle Communications, Inc.
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
 * $Whistle: ng_bpf.c,v 1.3 1999/12/03 20:30:23 archie Exp $
 */

/*
 * BPF NETGRAPH NODE TYPE
 *
 * This node type accepts any number of hook connections.  With each hook
 * is associated a bpf(4) filter program, and two hook names (each possibly
 * the empty string).  Incoming packets are compared against the filter;
 * matching packets are delivered out the first named hook (or dropped if
 * the empty string), and non-matching packets are delivered out the second
 * named hook (or dropped if the empty string).
 *
 * Each hook also keeps statistics about how many packets have matched, etc.
 */

#include "opt_bpf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <net/bpf.h>
#ifdef BPF_JITTER
#include <net/bpf_jitter.h>
#endif

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_bpf.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_BPF, "netgraph_bpf", "netgraph bpf node");
#else
#define M_NETGRAPH_BPF M_NETGRAPH
#endif

#define OFFSETOF(s, e) ((char *)&((s *)0)->e - (char *)((s *)0))

#define ERROUT(x)	do { error = (x); goto done; } while (0)

/* Per hook private info */
struct ng_bpf_hookinfo {
	hook_p			hook;
	hook_p			match;
	hook_p			nomatch;
	struct ng_bpf_hookprog	*prog;
#ifdef BPF_JITTER
	bpf_jit_filter		*jit_prog;
#endif
	struct ng_bpf_hookstat	stats;
};
typedef struct ng_bpf_hookinfo *hinfo_p;

/* Netgraph methods */
static ng_constructor_t	ng_bpf_constructor;
static ng_rcvmsg_t	ng_bpf_rcvmsg;
static ng_shutdown_t	ng_bpf_shutdown;
static ng_newhook_t	ng_bpf_newhook;
static ng_rcvdata_t	ng_bpf_rcvdata;
static ng_disconnect_t	ng_bpf_disconnect;

/* Maximum bpf program instructions */
extern int	bpf_maxinsns;

/* Internal helper functions */
static int	ng_bpf_setprog(hook_p hook, const struct ng_bpf_hookprog *hp);

/* Parse type for one struct bfp_insn */
static const struct ng_parse_struct_field ng_bpf_insn_type_fields[] = {
	{ "code",	&ng_parse_hint16_type	},
	{ "jt",		&ng_parse_uint8_type	},
	{ "jf",		&ng_parse_uint8_type	},
	{ "k",		&ng_parse_uint32_type	},
	{ NULL }
};
static const struct ng_parse_type ng_bpf_insn_type = {
	&ng_parse_struct_type,
	&ng_bpf_insn_type_fields
};

/* Parse type for the field 'bpf_prog' in struct ng_bpf_hookprog */
static int
ng_bpf_hookprogary_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	const struct ng_bpf_hookprog *hp;

	hp = (const struct ng_bpf_hookprog *)
	    (buf - OFFSETOF(struct ng_bpf_hookprog, bpf_prog));
	return hp->bpf_prog_len;
}

static const struct ng_parse_array_info ng_bpf_hookprogary_info = {
	&ng_bpf_insn_type,
	&ng_bpf_hookprogary_getLength,
	NULL
};
static const struct ng_parse_type ng_bpf_hookprogary_type = {
	&ng_parse_array_type,
	&ng_bpf_hookprogary_info
};

/* Parse type for struct ng_bpf_hookprog */
static const struct ng_parse_struct_field ng_bpf_hookprog_type_fields[]
	= NG_BPF_HOOKPROG_TYPE_INFO(&ng_bpf_hookprogary_type);
static const struct ng_parse_type ng_bpf_hookprog_type = {
	&ng_parse_struct_type,
	&ng_bpf_hookprog_type_fields
};

/* Parse type for struct ng_bpf_hookstat */
static const struct ng_parse_struct_field ng_bpf_hookstat_type_fields[]
	= NG_BPF_HOOKSTAT_TYPE_INFO;
static const struct ng_parse_type ng_bpf_hookstat_type = {
	&ng_parse_struct_type,
	&ng_bpf_hookstat_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_bpf_cmdlist[] = {
	{
	  NGM_BPF_COOKIE,
	  NGM_BPF_SET_PROGRAM,
	  "setprogram",
	  &ng_bpf_hookprog_type,
	  NULL
	},
	{
	  NGM_BPF_COOKIE,
	  NGM_BPF_GET_PROGRAM,
	  "getprogram",
	  &ng_parse_hookbuf_type,
	  &ng_bpf_hookprog_type
	},
	{
	  NGM_BPF_COOKIE,
	  NGM_BPF_GET_STATS,
	  "getstats",
	  &ng_parse_hookbuf_type,
	  &ng_bpf_hookstat_type
	},
	{
	  NGM_BPF_COOKIE,
	  NGM_BPF_CLR_STATS,
	  "clrstats",
	  &ng_parse_hookbuf_type,
	  NULL
	},
	{
	  NGM_BPF_COOKIE,
	  NGM_BPF_GETCLR_STATS,
	  "getclrstats",
	  &ng_parse_hookbuf_type,
	  &ng_bpf_hookstat_type
	},
	{ 0 }
};

/* Netgraph type descriptor */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_BPF_NODE_TYPE,
	.constructor =	ng_bpf_constructor,
	.rcvmsg =	ng_bpf_rcvmsg,
	.shutdown =	ng_bpf_shutdown,
	.newhook =	ng_bpf_newhook,
	.rcvdata =	ng_bpf_rcvdata,
	.disconnect =	ng_bpf_disconnect,
	.cmdlist =	ng_bpf_cmdlist,
};
NETGRAPH_INIT(bpf, &typestruct);

/* Default BPF program for a hook that matches nothing */
static const struct ng_bpf_hookprog ng_bpf_default_prog = {
	{ '\0' },		/* to be filled in at hook creation time */
	{ '\0' },
	{ '\0' },
	1,
	{ BPF_STMT(BPF_RET+BPF_K, 0) }
};

/*
 * Node constructor
 *
 * We don't keep any per-node private data
 * We go via the hooks.
 */
static int
ng_bpf_constructor(node_p node)
{
	NG_NODE_SET_PRIVATE(node, NULL);
	return (0);
}

/*
 * Callback functions to be used by NG_NODE_FOREACH_HOOK() macro.
 */
static int
ng_bpf_addrefs(hook_p hook, void* arg)
{
	hinfo_p hip = NG_HOOK_PRIVATE(hook);
	hook_p h = (hook_p)arg;

	if (strcmp(hip->prog->ifMatch, NG_HOOK_NAME(h)) == 0)
	    hip->match = h;
	if (strcmp(hip->prog->ifNotMatch, NG_HOOK_NAME(h)) == 0)
	    hip->nomatch = h;
	return (1);
}

static int
ng_bpf_remrefs(hook_p hook, void* arg)
{
	hinfo_p hip = NG_HOOK_PRIVATE(hook);
	hook_p h = (hook_p)arg;

	if (hip->match == h)
	    hip->match = NULL;
	if (hip->nomatch == h)
	    hip->nomatch = NULL;
	return (1);
}

/*
 * Add a hook
 */
static int
ng_bpf_newhook(node_p node, hook_p hook, const char *name)
{
	hinfo_p hip;
	hook_p tmp;
	int error;

	/* Create hook private structure */
	hip = malloc(sizeof(*hip), M_NETGRAPH_BPF, M_NOWAIT | M_ZERO);
	if (hip == NULL)
		return (ENOMEM);
	hip->hook = hook;
	NG_HOOK_SET_PRIVATE(hook, hip);

	/* Add our reference into other hooks data. */
	NG_NODE_FOREACH_HOOK(node, ng_bpf_addrefs, hook, tmp);

	/* Attach the default BPF program */
	if ((error = ng_bpf_setprog(hook, &ng_bpf_default_prog)) != 0) {
		free(hip, M_NETGRAPH_BPF);
		NG_HOOK_SET_PRIVATE(hook, NULL);
		return (error);
	}

	/* Set hook name */
	strlcpy(hip->prog->thisHook, name, sizeof(hip->prog->thisHook));
	return (0);
}

/*
 * Receive a control message
 */
static int
ng_bpf_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct ng_mesg *msg;
	struct ng_mesg *resp = NULL;
	int error = 0;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_BPF_COOKIE:
		switch (msg->header.cmd) {
		case NGM_BPF_SET_PROGRAM:
		    {
			struct ng_bpf_hookprog *const
			    hp = (struct ng_bpf_hookprog *)msg->data;
			hook_p hook;

			/* Sanity check */
			if (msg->header.arglen < sizeof(*hp)
			    || msg->header.arglen
			      != NG_BPF_HOOKPROG_SIZE(hp->bpf_prog_len))
				ERROUT(EINVAL);

			/* Find hook */
			if ((hook = ng_findhook(node, hp->thisHook)) == NULL)
				ERROUT(ENOENT);

			/* Set new program */
			if ((error = ng_bpf_setprog(hook, hp)) != 0)
				ERROUT(error);
			break;
		    }

		case NGM_BPF_GET_PROGRAM:
		    {
			struct ng_bpf_hookprog *hp;
			hook_p hook;

			/* Sanity check */
			if (msg->header.arglen == 0)
				ERROUT(EINVAL);
			msg->data[msg->header.arglen - 1] = '\0';

			/* Find hook */
			if ((hook = ng_findhook(node, msg->data)) == NULL)
				ERROUT(ENOENT);

			/* Build response */
			hp = ((hinfo_p)NG_HOOK_PRIVATE(hook))->prog;
			NG_MKRESPONSE(resp, msg,
			    NG_BPF_HOOKPROG_SIZE(hp->bpf_prog_len), M_NOWAIT);
			if (resp == NULL)
				ERROUT(ENOMEM);
			bcopy(hp, resp->data,
			   NG_BPF_HOOKPROG_SIZE(hp->bpf_prog_len));
			break;
		    }

		case NGM_BPF_GET_STATS:
		case NGM_BPF_CLR_STATS:
		case NGM_BPF_GETCLR_STATS:
		    {
			struct ng_bpf_hookstat *stats;
			hook_p hook;

			/* Sanity check */
			if (msg->header.arglen == 0)
				ERROUT(EINVAL);
			msg->data[msg->header.arglen - 1] = '\0';

			/* Find hook */
			if ((hook = ng_findhook(node, msg->data)) == NULL)
				ERROUT(ENOENT);
			stats = &((hinfo_p)NG_HOOK_PRIVATE(hook))->stats;

			/* Build response (if desired) */
			if (msg->header.cmd != NGM_BPF_CLR_STATS) {
				NG_MKRESPONSE(resp,
				    msg, sizeof(*stats), M_NOWAIT);
				if (resp == NULL)
					ERROUT(ENOMEM);
				bcopy(stats, resp->data, sizeof(*stats));
			}

			/* Clear stats (if desired) */
			if (msg->header.cmd != NGM_BPF_GET_STATS)
				bzero(stats, sizeof(*stats));
			break;
		    }

		default:
			error = EINVAL;
			break;
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
 * Apply the filter, and then drop or forward packet as appropriate.
 */
static int
ng_bpf_rcvdata(hook_p hook, item_p item)
{
	const hinfo_p hip = NG_HOOK_PRIVATE(hook);
	int totlen;
	int needfree = 0, error = 0, usejit = 0;
	u_char *data = NULL;
	hinfo_p dhip;
	hook_p dest;
	u_int len;
	struct mbuf *m;

	m = NGI_M(item);	/* 'item' still owns it.. we are peeking */ 
	totlen = m->m_pkthdr.len;
	/* Update stats on incoming hook. XXX Can we do 64 bits atomically? */
	/* atomic_add_int64(&hip->stats.recvFrames, 1); */
	/* atomic_add_int64(&hip->stats.recvOctets, totlen); */
	hip->stats.recvFrames++; 
	hip->stats.recvOctets += totlen;

	/* Don't call bpf_filter() with totlen == 0! */
	if (totlen == 0) {
		len = 0;
		goto ready;
	}

#ifdef BPF_JITTER
	if (bpf_jitter_enable != 0 && hip->jit_prog != NULL)
		usejit = 1;
#endif

	/* Need to put packet in contiguous memory for bpf */
	if (m->m_next != NULL && totlen > MHLEN) {
		if (usejit) {
			data = malloc(totlen, M_NETGRAPH_BPF, M_NOWAIT);
			if (data == NULL) {
				NG_FREE_ITEM(item);
				return (ENOMEM);
			}
			needfree = 1;
			m_copydata(m, 0, totlen, (caddr_t)data);
		}
	} else {
		if (m->m_next != NULL) {
			NGI_M(item) = m = m_pullup(m, totlen);
			if (m == NULL) {
				NG_FREE_ITEM(item);
				return (ENOBUFS);
			}
		}
		data = mtod(m, u_char *);
	}

	/* Run packet through filter */
#ifdef BPF_JITTER
	if (usejit)
		len = (*(hip->jit_prog->func))(data, totlen, totlen);
	else
#endif
	if (data)
		len = bpf_filter(hip->prog->bpf_prog, data, totlen, totlen);
	else
		len = bpf_filter(hip->prog->bpf_prog, (u_char *)m, totlen, 0);
	if (needfree)
		free(data, M_NETGRAPH_BPF);
ready:
	/* See if we got a match and find destination hook */
	if (len > 0) {

		/* Update stats */
		/* XXX atomically? */
		hip->stats.recvMatchFrames++;
		hip->stats.recvMatchOctets += totlen;

		/* Truncate packet length if required by the filter */
		/* Assume this never changes m */
		if (len < totlen) {
			m_adj(m, -(totlen - len));
			totlen = len;
		}
		dest = hip->match;
	} else
		dest = hip->nomatch;
	if (dest == NULL) {
		NG_FREE_ITEM(item);
		return (0);
	}

	/* Deliver frame out destination hook */
	dhip = NG_HOOK_PRIVATE(dest);
	dhip->stats.xmitOctets += totlen;
	dhip->stats.xmitFrames++;
	NG_FWD_ITEM_HOOK(error, item, dest);
	return (error);
}

/*
 * Shutdown processing
 */
static int
ng_bpf_shutdown(node_p node)
{
	NG_NODE_UNREF(node);
	return (0);
}

/*
 * Hook disconnection
 */
static int
ng_bpf_disconnect(hook_p hook)
{
	const node_p node = NG_HOOK_NODE(hook);
	const hinfo_p hip = NG_HOOK_PRIVATE(hook);
	hook_p tmp;

	KASSERT(hip != NULL, ("%s: null info", __func__));

	/* Remove our reference from other hooks data. */
	NG_NODE_FOREACH_HOOK(node, ng_bpf_remrefs, hook, tmp);

	free(hip->prog, M_NETGRAPH_BPF);
#ifdef BPF_JITTER
	if (hip->jit_prog != NULL)
		bpf_destroy_jit_filter(hip->jit_prog);
#endif
	free(hip, M_NETGRAPH_BPF);
	if ((NG_NODE_NUMHOOKS(node) == 0) &&
	    (NG_NODE_IS_VALID(node))) {
		ng_rmnode_self(node);
	}
	return (0);
}

/************************************************************************
			HELPER STUFF
 ************************************************************************/

/*
 * Set the BPF program associated with a hook
 */
static int
ng_bpf_setprog(hook_p hook, const struct ng_bpf_hookprog *hp0)
{
	const hinfo_p hip = NG_HOOK_PRIVATE(hook);
	struct ng_bpf_hookprog *hp;
#ifdef BPF_JITTER
	bpf_jit_filter *jit_prog;
#endif
	int size;

	/* Check program for validity */
	if (hp0->bpf_prog_len > bpf_maxinsns ||
	    !bpf_validate(hp0->bpf_prog, hp0->bpf_prog_len))
		return (EINVAL);

	/* Make a copy of the program */
	size = NG_BPF_HOOKPROG_SIZE(hp0->bpf_prog_len);
	hp = malloc(size, M_NETGRAPH_BPF, M_NOWAIT);
	if (hp == NULL)
		return (ENOMEM);
	bcopy(hp0, hp, size);
#ifdef BPF_JITTER
	jit_prog = bpf_jitter(hp->bpf_prog, hp->bpf_prog_len);
#endif

	/* Free previous program, if any, and assign new one */
	if (hip->prog != NULL)
		free(hip->prog, M_NETGRAPH_BPF);
	hip->prog = hp;
#ifdef BPF_JITTER
	if (hip->jit_prog != NULL)
		bpf_destroy_jit_filter(hip->jit_prog);
	hip->jit_prog = jit_prog;
#endif

	/* Prepare direct references on target hooks. */
	hip->match = ng_findhook(NG_HOOK_NODE(hook), hip->prog->ifMatch);
	hip->nomatch = ng_findhook(NG_HOOK_NODE(hook), hip->prog->ifNotMatch);
	return (0);
}

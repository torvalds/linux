/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Vadim Goncharov <vadimnuclight@tpu.ru>
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
 * Portions Copyright (c) 1999 Whistle Communications, Inc.
 * (ng_bpf by Archie Cobbs <archie@freebsd.org>)
 *
 * $FreeBSD$
 */

/*
 * TAG NETGRAPH NODE TYPE
 *
 * This node type accepts an arbitrary number of hooks. Each hook can be
 * configured for an mbuf_tags(9) definition and two hook names: a hook
 * for matched packets, and a hook for packets, that didn't match. Incoming
 * packets are examined for configured tag, matched packets are delivered
 * out via first hook, and not matched out via second. If corresponding hook
 * is not configured, packets are dropped.
 *
 * A hook can also have an outgoing tag definition configured, so that
 * all packets leaving the hook will be unconditionally appended with newly
 * allocated tag.
 *
 * Both hooks can be set to null tag definitions (that is, with zeroed
 * fields), so that packet tags are unmodified on output or all packets
 * are unconditionally forwarded to non-matching hook on input.  There is
 * also a possibility to replace tags by specifying strip flag on input
 * and replacing tag on corresponding output tag (or simply remove tag if
 * no tag specified on output).
 *
 * If compiled with NG_TAG_DEBUG, each hook also keeps statistics about
 * how many packets have matched, etc.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/stddef.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_tag.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_TAG, "netgraph_tag", "netgraph tag node");
#else
#define M_NETGRAPH_TAG M_NETGRAPH
#endif

#define ERROUT(x)	do { error = (x); goto done; } while (0)

/*
 * Per hook private info.
 *
 * We've separated API and ABI here, to make easier changes in this node,
 * if needed. If you want to change representation, please do not break API.
 * We still keep API structures in memory to simplify access to them for
 * GET* messages, but most of data is accessed in internal representation
 * only.  The reason for this is to speed things up - if data will be
 * accessed from API structures, there would be double pointer dereferencing
 * in the code, which almost necessarily leads to CPU cache misses and
 * reloads.
 *
 * We also do another optimization by using resolved pointers to
 * destination hooks instead of expensive ng_findhook().
 */
struct ng_tag_hookinfo {
	hook_p			hi_match;	/* matching hook pointer */
	hook_p			hi_nonmatch;	/* non-matching hook pointer */
	uint32_t		in_tag_cookie;
	uint32_t		out_tag_cookie;
	uint16_t		in_tag_id;
	uint16_t		in_tag_len;
	uint16_t		out_tag_id;
	uint16_t		out_tag_len;
	uint8_t			strip;
	void			*in_tag_data;
	void			*out_tag_data;
	struct ng_tag_hookin	*in;
	struct ng_tag_hookout	*out;
#ifdef NG_TAG_DEBUG
	struct ng_tag_hookstat	stats;
#endif
};
typedef struct ng_tag_hookinfo *hinfo_p;

/* Netgraph methods. */
static ng_constructor_t	ng_tag_constructor;
static ng_rcvmsg_t	ng_tag_rcvmsg;
static ng_shutdown_t	ng_tag_shutdown;
static ng_newhook_t	ng_tag_newhook;
static ng_rcvdata_t	ng_tag_rcvdata;
static ng_disconnect_t	ng_tag_disconnect;

/* Internal helper functions. */
static int	ng_tag_setdata_in(hook_p hook, const struct ng_tag_hookin *hp);
static int	ng_tag_setdata_out(hook_p hook, const struct ng_tag_hookout *hp);

/* Parse types for the field 'tag_data' in structs ng_tag_hookin and out. */
static int
ng_tag_hookinary_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	const struct ng_tag_hookin *hp;

	hp = (const struct ng_tag_hookin *)
	    (buf - offsetof(struct ng_tag_hookin, tag_data));
	return (hp->tag_len);
}

static int
ng_tag_hookoutary_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	const struct ng_tag_hookout *hp;

	hp = (const struct ng_tag_hookout *)
	    (buf - offsetof(struct ng_tag_hookout, tag_data));
	return (hp->tag_len);
}

static const struct ng_parse_type ng_tag_hookinary_type = {
	&ng_parse_bytearray_type,
	&ng_tag_hookinary_getLength
};

static const struct ng_parse_type ng_tag_hookoutary_type = {
	&ng_parse_bytearray_type,
	&ng_tag_hookoutary_getLength
};

/* Parse type for struct ng_tag_hookin. */
static const struct ng_parse_struct_field ng_tag_hookin_type_fields[]
	= NG_TAG_HOOKIN_TYPE_INFO(&ng_tag_hookinary_type);
static const struct ng_parse_type ng_tag_hookin_type = {
	&ng_parse_struct_type,
	&ng_tag_hookin_type_fields
};

/* Parse type for struct ng_tag_hookout. */
static const struct ng_parse_struct_field ng_tag_hookout_type_fields[]
	= NG_TAG_HOOKOUT_TYPE_INFO(&ng_tag_hookoutary_type);
static const struct ng_parse_type ng_tag_hookout_type = {
	&ng_parse_struct_type,
	&ng_tag_hookout_type_fields
};

#ifdef NG_TAG_DEBUG
/* Parse type for struct ng_tag_hookstat. */
static const struct ng_parse_struct_field ng_tag_hookstat_type_fields[]
	= NG_TAG_HOOKSTAT_TYPE_INFO;
static const struct ng_parse_type ng_tag_hookstat_type = {
	&ng_parse_struct_type,
	&ng_tag_hookstat_type_fields
};
#endif

/* List of commands and how to convert arguments to/from ASCII. */
static const struct ng_cmdlist ng_tag_cmdlist[] = {
	{
	  NGM_TAG_COOKIE,
	  NGM_TAG_SET_HOOKIN,
	  "sethookin",
	  &ng_tag_hookin_type,
	  NULL
	},
	{
	  NGM_TAG_COOKIE,
	  NGM_TAG_GET_HOOKIN,
	  "gethookin",
	  &ng_parse_hookbuf_type,
	  &ng_tag_hookin_type
	},
	{
	  NGM_TAG_COOKIE,
	  NGM_TAG_SET_HOOKOUT,
	  "sethookout",
	  &ng_tag_hookout_type,
	  NULL
	},
	{
	  NGM_TAG_COOKIE,
	  NGM_TAG_GET_HOOKOUT,
	  "gethookout",
	  &ng_parse_hookbuf_type,
	  &ng_tag_hookout_type
	},
#ifdef NG_TAG_DEBUG
	{
	  NGM_TAG_COOKIE,
	  NGM_TAG_GET_STATS,
	  "getstats",
	  &ng_parse_hookbuf_type,
	  &ng_tag_hookstat_type
	},
	{
	  NGM_TAG_COOKIE,
	  NGM_TAG_CLR_STATS,
	  "clrstats",
	  &ng_parse_hookbuf_type,
	  NULL
	},
	{
	  NGM_TAG_COOKIE,
	  NGM_TAG_GETCLR_STATS,
	  "getclrstats",
	  &ng_parse_hookbuf_type,
	  &ng_tag_hookstat_type
	},
#endif
	{ 0 }
};

/* Netgraph type descriptor. */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_TAG_NODE_TYPE,
	.constructor =	ng_tag_constructor,
	.rcvmsg =	ng_tag_rcvmsg,
	.shutdown =	ng_tag_shutdown,
	.newhook =	ng_tag_newhook,
	.rcvdata =	ng_tag_rcvdata,
	.disconnect =	ng_tag_disconnect,
	.cmdlist =	ng_tag_cmdlist,
};
NETGRAPH_INIT(tag, &typestruct);

/*
 * This are default API structures (initialized to zeroes) which are
 * returned in response to GET* messages when no configuration was made.
 * One could ask why to have this structures at all when we have
 * ng_tag_hookinfo initialized to zero and don't need in and out structures
 * at all to operate.  Unfortunatelly, we have to return thisHook field
 * in response to messages so the fastest and simpliest way is to have
 * this default structures and initialize thisHook once at hook creation
 * rather than to do it on every response.
 */

/* Default tag values for a hook that matches nothing. */
static const struct ng_tag_hookin ng_tag_default_in = {
	{ '\0' },		/* to be filled in at hook creation time */
	{ '\0' },
	{ '\0' },
	0,
	0,
	0,
	0
};

/* Default tag values for a hook that adds nothing */
static const struct ng_tag_hookout ng_tag_default_out = {
	{ '\0' },		/* to be filled in at hook creation time */
	0,
	0,
	0
};

/*
 * Node constructor.
 *
 * We don't keep any per-node private data - we do it on per-hook basis.
 */
static int
ng_tag_constructor(node_p node)
{
	return (0);
}

/*
 * Add a hook.
 */
static int
ng_tag_newhook(node_p node, hook_p hook, const char *name)
{
	hinfo_p hip;
	int error;

	/* Create hook private structure. */
	hip = malloc(sizeof(*hip), M_NETGRAPH_TAG, M_NOWAIT | M_ZERO);
	if (hip == NULL)
		return (ENOMEM);
	NG_HOOK_SET_PRIVATE(hook, hip);

	/*
	 * After M_ZERO both in and out hook pointers are set to NULL,
	 * as well as all members and pointers to in and out API
	 * structures, so we need to set explicitly only thisHook field
	 * in that structures (after allocating them, of course).
	 */

	/* Attach the default IN data. */
	if ((error = ng_tag_setdata_in(hook, &ng_tag_default_in)) != 0) {
		free(hip, M_NETGRAPH_TAG);
		return (error);
	}

	/* Attach the default OUT data. */
	if ((error = ng_tag_setdata_out(hook, &ng_tag_default_out)) != 0) {
		free(hip, M_NETGRAPH_TAG);
		return (error);
	}

	/*
	 * Set hook name.  This is done only once at hook creation time
	 * since hook name can't change, rather than to do it on every
	 * response to messages requesting API structures with data who
	 * we are etc.
	 */
	strncpy(hip->in->thisHook, name, sizeof(hip->in->thisHook) - 1);
	hip->in->thisHook[sizeof(hip->in->thisHook) - 1] = '\0';
	strncpy(hip->out->thisHook, name, sizeof(hip->out->thisHook) - 1);
	hip->out->thisHook[sizeof(hip->out->thisHook) - 1] = '\0';
	return (0);
}

/*
 * Receive a control message.
 */
static int
ng_tag_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct ng_mesg *msg;
	struct ng_mesg *resp = NULL;
	int error = 0;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_TAG_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TAG_SET_HOOKIN:
		    {
			struct ng_tag_hookin *const
			    hp = (struct ng_tag_hookin *)msg->data;
			hook_p hook;

			/* Sanity check. */
			if (msg->header.arglen < sizeof(*hp)
			    || msg->header.arglen !=
			    NG_TAG_HOOKIN_SIZE(hp->tag_len))
				ERROUT(EINVAL);

			/* Find hook. */
			if ((hook = ng_findhook(node, hp->thisHook)) == NULL)
				ERROUT(ENOENT);

			/* Set new tag values. */
			if ((error = ng_tag_setdata_in(hook, hp)) != 0)
				ERROUT(error);
			break;
		    }

		case NGM_TAG_SET_HOOKOUT:
		    {
			struct ng_tag_hookout *const
			    hp = (struct ng_tag_hookout *)msg->data;
			hook_p hook;

			/* Sanity check. */
			if (msg->header.arglen < sizeof(*hp)
			    || msg->header.arglen !=
			    NG_TAG_HOOKOUT_SIZE(hp->tag_len))
				ERROUT(EINVAL);

			/* Find hook. */
			if ((hook = ng_findhook(node, hp->thisHook)) == NULL)
				ERROUT(ENOENT);

			/* Set new tag values. */
			if ((error = ng_tag_setdata_out(hook, hp)) != 0)
				ERROUT(error);
			break;
		    }

		case NGM_TAG_GET_HOOKIN:
		    {
			struct ng_tag_hookin *hp;
			hook_p hook;

			/* Sanity check. */
			if (msg->header.arglen == 0)
				ERROUT(EINVAL);
			msg->data[msg->header.arglen - 1] = '\0';

			/* Find hook. */
			if ((hook = ng_findhook(node, msg->data)) == NULL)
				ERROUT(ENOENT);

			/* Build response. */
			hp = ((hinfo_p)NG_HOOK_PRIVATE(hook))->in;
			NG_MKRESPONSE(resp, msg,
			    NG_TAG_HOOKIN_SIZE(hp->tag_len), M_WAITOK);
			/* M_WAITOK can't return NULL. */
			bcopy(hp, resp->data,
			   NG_TAG_HOOKIN_SIZE(hp->tag_len));
			break;
		    }

		case NGM_TAG_GET_HOOKOUT:
		    {
			struct ng_tag_hookout *hp;
			hook_p hook;

			/* Sanity check. */
			if (msg->header.arglen == 0)
				ERROUT(EINVAL);
			msg->data[msg->header.arglen - 1] = '\0';

			/* Find hook. */
			if ((hook = ng_findhook(node, msg->data)) == NULL)
				ERROUT(ENOENT);

			/* Build response. */
			hp = ((hinfo_p)NG_HOOK_PRIVATE(hook))->out;
			NG_MKRESPONSE(resp, msg,
			    NG_TAG_HOOKOUT_SIZE(hp->tag_len), M_WAITOK);
			/* M_WAITOK can't return NULL. */
			bcopy(hp, resp->data,
			   NG_TAG_HOOKOUT_SIZE(hp->tag_len));
			break;
		    }

#ifdef NG_TAG_DEBUG
		case NGM_TAG_GET_STATS:
		case NGM_TAG_CLR_STATS:
		case NGM_TAG_GETCLR_STATS:
		    {
			struct ng_tag_hookstat *stats;
			hook_p hook;

			/* Sanity check. */
			if (msg->header.arglen == 0)
				ERROUT(EINVAL);
			msg->data[msg->header.arglen - 1] = '\0';

			/* Find hook. */
			if ((hook = ng_findhook(node, msg->data)) == NULL)
				ERROUT(ENOENT);
			stats = &((hinfo_p)NG_HOOK_PRIVATE(hook))->stats;

			/* Build response (if desired). */
			if (msg->header.cmd != NGM_TAG_CLR_STATS) {
				NG_MKRESPONSE(resp,
				    msg, sizeof(*stats), M_WAITOK);
				/* M_WAITOK can't return NULL. */
				bcopy(stats, resp->data, sizeof(*stats));
			}

			/* Clear stats (if desired). */
			if (msg->header.cmd != NGM_TAG_GET_STATS)
				bzero(stats, sizeof(*stats));
			break;
		    }
#endif /* NG_TAG_DEBUG */

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
 * Receive data on a hook.
 *
 * Apply the filter, and then drop or forward packet as appropriate.
 */
static int
ng_tag_rcvdata(hook_p hook, item_p item)
{
	struct mbuf *m;
	struct m_tag *tag = NULL;
	const hinfo_p hip = NG_HOOK_PRIVATE(hook);
	uint16_t type, tag_len;
	uint32_t cookie;
	hinfo_p dhip;
	hook_p dest;
	int totlen;
	int found = 0, error = 0;

	m = NGI_M(item);	/* 'item' still owns it.. we are peeking */
	totlen = m->m_pkthdr.len;

#ifdef NG_TAG_DEBUG
	hip->stats.recvFrames++;
	hip->stats.recvOctets += totlen;
#endif

	/* Looking up incoming tag. */
	cookie = hip->in_tag_cookie;
	type = hip->in_tag_id;
	tag_len = hip->in_tag_len;

	/*
	 * We treat case of all zeroes specially (that is, cookie and
	 * type are equal to zero), as we assume that such tag
	 * can never occur in the wild.  So we don't waste time trying
	 * to find such tag (for example, these are zeroes after hook
	 * creation in default structures).
	 */
	if ((cookie != 0) || (type != 0)) {
		tag = m_tag_locate(m, cookie, type, NULL);
		while (tag != NULL) {
			if (memcmp((void *)(tag + 1),
			    hip->in_tag_data, tag_len) == 0) {
				found = 1;
				break;
			}
			tag = m_tag_locate(m, cookie, type, tag);
		}
	}
	
	/* See if we got a match and find destination hook. */
	if (found) {
#ifdef NG_TAG_DEBUG
		hip->stats.recvMatchFrames++;
		hip->stats.recvMatchOctets += totlen;
#endif
		if (hip->strip)
			m_tag_delete(m, tag);
		dest = hip->hi_match;
	} else
		dest = hip->hi_nonmatch;
	if (dest == NULL) {
		NG_FREE_ITEM(item);
		return (0);
	}

	/* Deliver frame out destination hook. */
	dhip = NG_HOOK_PRIVATE(dest);

#ifdef NG_TAG_DEBUG
	dhip->stats.xmitOctets += totlen;
	dhip->stats.xmitFrames++;
#endif
	
	cookie = dhip->out_tag_cookie;
	type = dhip->out_tag_id;
	tag_len = dhip->out_tag_len;
	
	if ((cookie != 0) || (type != 0)) {
		tag = m_tag_alloc(cookie, type, tag_len, M_NOWAIT);
		/* XXX may be free the mbuf if tag allocation failed? */
		if (tag != NULL) {
			if (tag_len != 0) {
				/* copy tag data to its place */
				memcpy((void *)(tag + 1),
				    dhip->out_tag_data, tag_len);
			}
			m_tag_prepend(m, tag);
		}
	}
	
	NG_FWD_ITEM_HOOK(error, item, dest);
	return (error);
}

/*
 * Shutdown processing.
 */
static int
ng_tag_shutdown(node_p node)
{
	NG_NODE_UNREF(node);
	return (0);
}

/*
 * Hook disconnection.
 *
 * We must check all hooks, since they may reference this one.
 */
static int
ng_tag_disconnect(hook_p hook)
{
	const hinfo_p hip = NG_HOOK_PRIVATE(hook);
	node_p node = NG_HOOK_NODE(hook);
	hook_p hook2;

	KASSERT(hip != NULL, ("%s: null info", __func__));

	LIST_FOREACH(hook2, &node->nd_hooks, hk_hooks) {
		hinfo_p priv = NG_HOOK_PRIVATE(hook2);

		if (priv->hi_match == hook)
			priv->hi_match = NULL;
		if (priv->hi_nonmatch == hook)
			priv->hi_nonmatch = NULL;
	}

	free(hip->in, M_NETGRAPH_TAG);
	free(hip->out, M_NETGRAPH_TAG);
	free(hip, M_NETGRAPH_TAG);
	NG_HOOK_SET_PRIVATE(hook, NULL);			/* for good measure */
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0) &&
	    (NG_NODE_IS_VALID(NG_HOOK_NODE(hook)))) {
		ng_rmnode_self(NG_HOOK_NODE(hook));
	}
	return (0);
}

/************************************************************************
			HELPER STUFF
 ************************************************************************/

/*
 * Set the IN tag values associated with a hook.
 */
static int
ng_tag_setdata_in(hook_p hook, const struct ng_tag_hookin *hp0)
{
	const hinfo_p hip = NG_HOOK_PRIVATE(hook);
	struct ng_tag_hookin *hp;
	int size;

	/* Make a copy of the tag values and data. */
	size = NG_TAG_HOOKIN_SIZE(hp0->tag_len);
	hp = malloc(size, M_NETGRAPH_TAG, M_WAITOK);
	/* M_WAITOK can't return NULL. */
	bcopy(hp0, hp, size);

	/* Free previous tag, if any, and assign new one. */
	if (hip->in != NULL)
		free(hip->in, M_NETGRAPH_TAG);
	hip->in = hp;

	/*
	 * Resolve hook names to pointers.
	 *
	 * As ng_findhook() is expensive operation to do it on every packet
	 * after tag matching check, we do it here and use resolved pointers
	 * where appropriate.
	 *
	 * XXX The drawback is that user can configure a hook to use
	 * ifMatch/ifNotMatch hooks that do not yet exist and will be added
	 * by user later, so that resolved pointers will be NULL even
	 * if the hook already exists, causing node to drop packets and
	 * user to report bugs.  We could do check for this situation on
	 * every hook creation with pointers correction, but that involves
	 * re-resolving for all pointers in all hooks, up to O(n^2) operations,
	 * so we better document this in man page for user not to do
	 * configuration before creating all hooks.
	 */
	hip->hi_match = ng_findhook(NG_HOOK_NODE(hook), hip->in->ifMatch);
	hip->hi_nonmatch = ng_findhook(NG_HOOK_NODE(hook), hip->in->ifNotMatch);

	/* Fill internal values from API structures. */
	hip->in_tag_cookie = hip->in->tag_cookie;
	hip->in_tag_id = hip->in->tag_id;
	hip->in_tag_len = hip->in->tag_len;
	hip->strip = hip->in->strip;
	hip->in_tag_data = (void*)(hip->in->tag_data);
	return (0);
}

/*
 * Set the OUT tag values associated with a hook.
 */
static int
ng_tag_setdata_out(hook_p hook, const struct ng_tag_hookout *hp0)
{
	const hinfo_p hip = NG_HOOK_PRIVATE(hook);
	struct ng_tag_hookout *hp;
	int size;

	/* Make a copy of the tag values and data. */
	size = NG_TAG_HOOKOUT_SIZE(hp0->tag_len);
	hp = malloc(size, M_NETGRAPH_TAG, M_WAITOK);
	/* M_WAITOK can't return NULL. */
	bcopy(hp0, hp, size);

	/* Free previous tag, if any, and assign new one. */
	if (hip->out != NULL)
		free(hip->out, M_NETGRAPH_TAG);
	hip->out = hp;

	/* Fill internal values from API structures. */
	hip->out_tag_cookie = hip->out->tag_cookie;
	hip->out_tag_id = hip->out->tag_id;
	hip->out_tag_len = hip->out->tag_len;
	hip->out_tag_data = (void*)(hip->out->tag_data);
	return (0);
}


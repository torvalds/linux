
/*
 * ng_vjc.c
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
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_vjc.c,v 1.17 1999/11/01 09:24:52 julian Exp $
 */

/*
 * This node performs Van Jacobson IP header (de)compression.
 * You must have included net/slcompress.c in your kernel compilation.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/errno.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_vjc.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <net/slcompress.h>

/* Check agreement with slcompress.c */
#if MAX_STATES != NG_VJC_MAX_CHANNELS
#error NG_VJC_MAX_CHANNELS must be the same as MAX_STATES
#endif

/* Maximum length of a compressed TCP VJ header */
#define MAX_VJHEADER		19

/* Node private data */
struct ng_vjc_private {
	struct	ngm_vjc_config conf;
	struct	slcompress slc;
	hook_p	ip;
	hook_p	vjcomp;
	hook_p	vjuncomp;
	hook_p	vjip;
};
typedef struct ng_vjc_private *priv_p;

#define ERROUT(x)	do { error = (x); goto done; } while (0)

/* Netgraph node methods */
static ng_constructor_t	ng_vjc_constructor;
static ng_rcvmsg_t	ng_vjc_rcvmsg;
static ng_shutdown_t	ng_vjc_shutdown;
static ng_newhook_t	ng_vjc_newhook;
static ng_rcvdata_t	ng_vjc_rcvdata;
static ng_disconnect_t	ng_vjc_disconnect;

/* Helper stuff */
static struct mbuf *ng_vjc_pulluphdrs(struct mbuf *m, int knownTCP);

/* Parse type for struct ngm_vjc_config */
static const struct ng_parse_struct_field ng_vjc_config_type_fields[]
	= NG_VJC_CONFIG_TYPE_INFO;
static const struct ng_parse_type ng_vjc_config_type = {
	&ng_parse_struct_type,
	&ng_vjc_config_type_fields
};

/* Parse type for the 'last_cs' and 'cs_next' fields in struct slcompress,
   which are pointers converted to integer indices, so parse them that way. */
#ifndef __LP64__
#define NG_VJC_TSTATE_PTR_TYPE	&ng_parse_uint32_type
#else
#define NG_VJC_TSTATE_PTR_TYPE	&ng_parse_uint64_type
#endif

/* Parse type for the 'cs_hdr' field in a struct cstate. Ideally we would
   like to use a 'struct ip' type instead of a simple array of bytes. */
static const struct ng_parse_fixedarray_info ng_vjc_cs_hdr_type_info = {
	&ng_parse_hint8_type,
	MAX_HDR
};
static const struct ng_parse_type ng_vjc_cs_hdr_type = {
	&ng_parse_fixedarray_type,
	&ng_vjc_cs_hdr_type_info
};

/* Parse type for a struct cstate */
static const struct ng_parse_struct_field ng_vjc_cstate_type_fields[] = {
	{ "cs_next",		NG_VJC_TSTATE_PTR_TYPE		},
	{ "cs_hlen",		&ng_parse_uint16_type		},
	{ "cs_id",		&ng_parse_uint8_type		},
	{ "cs_filler",		&ng_parse_uint8_type		},
	{ "cs_hdr",		&ng_vjc_cs_hdr_type		},
	{ NULL }
};
static const struct ng_parse_type ng_vjc_cstate_type = {
	&ng_parse_struct_type,
	&ng_vjc_cstate_type_fields
};

/* Parse type for an array of MAX_STATES struct cstate's, ie, tstate & rstate */
static const struct ng_parse_fixedarray_info ng_vjc_cstatearray_type_info = {
	&ng_vjc_cstate_type,
	MAX_STATES
};
static const struct ng_parse_type ng_vjc_cstatearray_type = {
	&ng_parse_fixedarray_type,
	&ng_vjc_cstatearray_type_info
};

/* Parse type for struct slcompress. Keep this in sync with the
   definition of struct slcompress defined in <net/slcompress.h> */
static const struct ng_parse_struct_field ng_vjc_slcompress_type_fields[] = {
	{ "last_cs",		NG_VJC_TSTATE_PTR_TYPE		},
	{ "last_recv",		&ng_parse_uint8_type		},
	{ "last_xmit",		&ng_parse_uint8_type		},
	{ "flags",		&ng_parse_hint16_type		},
#ifndef SL_NO_STATS
	{ "sls_packets",	&ng_parse_uint32_type		},
	{ "sls_compressed",	&ng_parse_uint32_type		},
	{ "sls_searches",	&ng_parse_uint32_type		},
	{ "sls_misses",		&ng_parse_uint32_type		},
	{ "sls_uncompressedin",	&ng_parse_uint32_type		},
	{ "sls_compressedin",	&ng_parse_uint32_type		},
	{ "sls_errorin",	&ng_parse_uint32_type		},
	{ "sls_tossed",		&ng_parse_uint32_type		},
#endif
	{ "tstate",		&ng_vjc_cstatearray_type	},
	{ "rstate",		&ng_vjc_cstatearray_type	},
	{ NULL }
};
static const struct ng_parse_type ng_vjc_slcompress_type = {
	&ng_parse_struct_type,
	&ng_vjc_slcompress_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_vjc_cmds[] = {
	{
	  NGM_VJC_COOKIE,
	  NGM_VJC_SET_CONFIG,
	  "setconfig",
	  &ng_vjc_config_type,
	  NULL
	},
	{
	  NGM_VJC_COOKIE,
	  NGM_VJC_GET_CONFIG,
	  "getconfig",
	  NULL,
	  &ng_vjc_config_type,
	},
	{
	  NGM_VJC_COOKIE,
	  NGM_VJC_GET_STATE,
	  "getstate",
	  NULL,
	  &ng_vjc_slcompress_type,
	},
	{
	  NGM_VJC_COOKIE,
	  NGM_VJC_CLR_STATS,
	  "clrstats",
	  NULL,
	  NULL,
	},
	{
	  NGM_VJC_COOKIE,
	  NGM_VJC_RECV_ERROR,
	  "recverror",
	  NULL,
	  NULL,
	},
	{ 0 }
};

/* Node type descriptor */
static struct ng_type ng_vjc_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_VJC_NODE_TYPE,
	.constructor =	ng_vjc_constructor,
	.rcvmsg =	ng_vjc_rcvmsg,
	.shutdown =	ng_vjc_shutdown,
	.newhook =	ng_vjc_newhook,
	.rcvdata =	ng_vjc_rcvdata,
	.disconnect =	ng_vjc_disconnect,
	.cmdlist =	ng_vjc_cmds,
};
NETGRAPH_INIT(vjc, &ng_vjc_typestruct);

/************************************************************************
			NETGRAPH NODE METHODS
 ************************************************************************/

/*
 * Create a new node
 */
static int
ng_vjc_constructor(node_p node)
{
	priv_p priv;

	/* Allocate private structure */
	priv = malloc(sizeof(*priv), M_NETGRAPH, M_WAITOK | M_ZERO);

	NG_NODE_SET_PRIVATE(node, priv);

	/* slcompress is not thread-safe. Protect it's state here. */
	NG_NODE_FORCE_WRITER(node);

	/* Done */
	return (0);
}

/*
 * Add a new hook
 */
static int
ng_vjc_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	hook_p *hookp;

	/* Get hook */
	if (strcmp(name, NG_VJC_HOOK_IP) == 0)
		hookp = &priv->ip;
	else if (strcmp(name, NG_VJC_HOOK_VJCOMP) == 0)
		hookp = &priv->vjcomp;
	else if (strcmp(name, NG_VJC_HOOK_VJUNCOMP) == 0)
		hookp = &priv->vjuncomp;
	else if (strcmp(name, NG_VJC_HOOK_VJIP) == 0)
		hookp = &priv->vjip;
	else
		return (EINVAL);

	/* See if already connected */
	if (*hookp)
		return (EISCONN);

	/* OK */
	*hookp = hook;
	return (0);
}

/*
 * Receive a control message
 */
static int
ng_vjc_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	/* Check type cookie */
	switch (msg->header.typecookie) {
	case NGM_VJC_COOKIE:
		switch (msg->header.cmd) {
		case NGM_VJC_SET_CONFIG:
		    {
			struct ngm_vjc_config *const c =
				(struct ngm_vjc_config *) msg->data;

			if (msg->header.arglen != sizeof(*c))
				ERROUT(EINVAL);
			if ((priv->conf.enableComp || priv->conf.enableDecomp)
			    && (c->enableComp || c->enableDecomp))
				ERROUT(EALREADY);
			if (c->enableComp) {
				if (c->maxChannel > NG_VJC_MAX_CHANNELS - 1
				    || c->maxChannel < NG_VJC_MIN_CHANNELS - 1)
					ERROUT(EINVAL);
			} else
				c->maxChannel = NG_VJC_MAX_CHANNELS - 1;
			if (c->enableComp != 0 || c->enableDecomp != 0) {
				bzero(&priv->slc, sizeof(priv->slc));
				sl_compress_init(&priv->slc, c->maxChannel);
			}
			priv->conf = *c;
			break;
		    }
		case NGM_VJC_GET_CONFIG:
		    {
			struct ngm_vjc_config *conf;

			NG_MKRESPONSE(resp, msg, sizeof(*conf), M_NOWAIT);
			if (resp == NULL)
				ERROUT(ENOMEM);
			conf = (struct ngm_vjc_config *)resp->data;
			*conf = priv->conf;
			break;
		    }
		case NGM_VJC_GET_STATE:
		    {
			const struct slcompress *const sl0 = &priv->slc;
			struct slcompress *sl;
			u_int16_t index;
			int i;

			/* Get response structure */
			NG_MKRESPONSE(resp, msg, sizeof(*sl), M_NOWAIT);
			if (resp == NULL)
				ERROUT(ENOMEM);
			sl = (struct slcompress *)resp->data;
			*sl = *sl0;

			/* Replace pointers with integer indices */
			if (sl->last_cs != NULL) {
				index = sl0->last_cs - sl0->tstate;
				bzero(&sl->last_cs, sizeof(sl->last_cs));
				*((u_int16_t *)&sl->last_cs) = index;
			}
			for (i = 0; i < MAX_STATES; i++) {
				struct cstate *const cs = &sl->tstate[i];

				index = sl0->tstate[i].cs_next - sl0->tstate;
				bzero(&cs->cs_next, sizeof(cs->cs_next));
				*((u_int16_t *)&cs->cs_next) = index;
			}
			break;
		    }
		case NGM_VJC_CLR_STATS:
			priv->slc.sls_packets = 0;
			priv->slc.sls_compressed = 0;
			priv->slc.sls_searches = 0;
			priv->slc.sls_misses = 0;
			priv->slc.sls_uncompressedin = 0;
			priv->slc.sls_compressedin = 0;
			priv->slc.sls_errorin = 0;
			priv->slc.sls_tossed = 0;
			break;
		case NGM_VJC_RECV_ERROR:
			sl_uncompress_tcp(NULL, 0, TYPE_ERROR, &priv->slc);
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
done:
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/*
 * Receive data
 */
static int
ng_vjc_rcvdata(hook_p hook, item_p item)
{
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	int error = 0;
	struct mbuf *m;

	NGI_GET_M(item, m);
	if (hook == priv->ip) {			/* outgoing packet */
		u_int type = TYPE_IP;

		/* Compress packet if enabled and proto is TCP */
		if (priv->conf.enableComp) {
			struct ip *ip;

			if ((m = ng_vjc_pulluphdrs(m, 0)) == NULL) {
				NG_FREE_ITEM(item);
				return (ENOBUFS);
			}
			ip = mtod(m, struct ip *);
			if (ip->ip_p == IPPROTO_TCP) {
				const int origLen = m->m_len;

				type = sl_compress_tcp(m, ip,
				    &priv->slc, priv->conf.compressCID);
				m->m_pkthdr.len += m->m_len - origLen;
			}
		}

		/* Dispatch to the appropriate outgoing hook */
		switch (type) {
		case TYPE_IP:
			hook = priv->vjip;
			break;
		case TYPE_UNCOMPRESSED_TCP:
			hook = priv->vjuncomp;
			break;
		case TYPE_COMPRESSED_TCP:
			hook = priv->vjcomp;
			break;
		default:
			panic("%s: type=%d", __func__, type);
		}
	} else if (hook == priv->vjcomp) {	/* incoming compressed packet */
		int vjlen, need2pullup;
		struct mbuf *hm;
		u_char *hdr;
		u_int hlen;

		/* Are we decompressing? */
		if (!priv->conf.enableDecomp) {
			NG_FREE_M(m);
			NG_FREE_ITEM(item);
			return (ENXIO);
		}

		/* Pull up the necessary amount from the mbuf */
		need2pullup = MAX_VJHEADER;
		if (need2pullup > m->m_pkthdr.len)
			need2pullup = m->m_pkthdr.len;
		if (m->m_len < need2pullup
		    && (m = m_pullup(m, need2pullup)) == NULL) {
			priv->slc.sls_errorin++;
			NG_FREE_ITEM(item);
			return (ENOBUFS);
		}

		/* Uncompress packet to reconstruct TCP/IP header */
		vjlen = sl_uncompress_tcp_core(mtod(m, u_char *),
		    m->m_len, m->m_pkthdr.len, TYPE_COMPRESSED_TCP,
		    &priv->slc, &hdr, &hlen);
		if (vjlen <= 0) {
			NG_FREE_M(m);
			NG_FREE_ITEM(item);
			return (EINVAL);
		}
		m_adj(m, vjlen);

		/* Copy the reconstructed TCP/IP headers into a new mbuf */
		MGETHDR(hm, M_NOWAIT, MT_DATA);
		if (hm == NULL) {
			priv->slc.sls_errorin++;
			NG_FREE_M(m);
			NG_FREE_ITEM(item);
			return (ENOBUFS);
		}
		hm->m_len = 0;
		hm->m_pkthdr.rcvif = NULL;
		if (hlen > MHLEN) {		/* unlikely, but can happen */
			if (!(MCLGET(hm, M_NOWAIT))) {
				m_freem(hm);
				priv->slc.sls_errorin++;
				NG_FREE_M(m);
				NG_FREE_ITEM(item);
				return (ENOBUFS);
			}
		}
		bcopy(hdr, mtod(hm, u_char *), hlen);
		hm->m_len = hlen;

		/* Glue TCP/IP headers and rest of packet together */
		hm->m_next = m;
		hm->m_pkthdr.len = hlen + m->m_pkthdr.len;
		m = hm;
		hook = priv->ip;
	} else if (hook == priv->vjuncomp) {	/* incoming uncompressed pkt */
		u_char *hdr;
		u_int hlen;

		/* Are we decompressing? */
		if (!priv->conf.enableDecomp) {
			NG_FREE_M(m);
			NG_FREE_ITEM(item);
			return (ENXIO);
		}

		/* Pull up IP+TCP headers */
		if ((m = ng_vjc_pulluphdrs(m, 1)) == NULL) {
			NG_FREE_ITEM(item);
			return (ENOBUFS);
		}

		/* Run packet through uncompressor */
		if (sl_uncompress_tcp_core(mtod(m, u_char *),
		    m->m_len, m->m_pkthdr.len, TYPE_UNCOMPRESSED_TCP,
		    &priv->slc, &hdr, &hlen) < 0) {
			NG_FREE_M(m);
			NG_FREE_ITEM(item);
			return (EINVAL);
		}
		hook = priv->ip;
	} else if (hook == priv->vjip)	/* incoming regular packet (bypass) */
		hook = priv->ip;
	else
		panic("%s: unknown hook", __func__);

	/* Send result back out */
	NG_FWD_NEW_DATA(error, item, hook, m);
	return (error);
}

/*
 * Shutdown node
 */
static int
ng_vjc_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	bzero(priv, sizeof(*priv));
	free(priv, M_NETGRAPH);
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);
	return (0);
}

/*
 * Hook disconnection
 */
static int
ng_vjc_disconnect(hook_p hook)
{
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);

	/* Zero out hook pointer */
	if (hook == priv->ip)
		priv->ip = NULL;
	else if (hook == priv->vjcomp)
		priv->vjcomp = NULL;
	else if (hook == priv->vjuncomp)
		priv->vjuncomp = NULL;
	else if (hook == priv->vjip)
		priv->vjip = NULL;
	else
		panic("%s: unknown hook", __func__);

	/* Go away if no hooks left */
	if ((NG_NODE_NUMHOOKS(node) == 0)
	&& (NG_NODE_IS_VALID(node)))
		ng_rmnode_self(node);
	return (0);
}

/************************************************************************
			HELPER STUFF
 ************************************************************************/

/*
 * Pull up the full IP and TCP headers of a packet. If packet is not
 * a TCP packet, just pull up the IP header.
 */
static struct mbuf *
ng_vjc_pulluphdrs(struct mbuf *m, int knownTCP)
{
	struct ip *ip;
	struct tcphdr *tcp;
	int ihlen, thlen;

	if (m->m_len < sizeof(*ip) && (m = m_pullup(m, sizeof(*ip))) == NULL)
		return (NULL);
	ip = mtod(m, struct ip *);
	if (!knownTCP && ip->ip_p != IPPROTO_TCP)
		return (m);
	ihlen = ip->ip_hl << 2;
	if (m->m_len < ihlen + sizeof(*tcp)) {
		if ((m = m_pullup(m, ihlen + sizeof(*tcp))) == NULL)
			return (NULL);
		ip = mtod(m, struct ip *);
	}
	tcp = (struct tcphdr *)((u_char *)ip + ihlen);
	thlen = tcp->th_off << 2;
	if (m->m_len < ihlen + thlen)
		m = m_pullup(m, ihlen + thlen);
	return (m);
}


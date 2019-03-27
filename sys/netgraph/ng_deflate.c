/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Alexander Motin <mav@alkar.net>
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
 */

/*
 * Deflate PPP compression netgraph node type.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/zlib.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_deflate.h>

#include "opt_netgraph.h"

static MALLOC_DEFINE(M_NETGRAPH_DEFLATE, "netgraph_deflate",
    "netgraph deflate node");

/* DEFLATE header length */
#define DEFLATE_HDRLEN		2

#define PROT_COMPD		0x00fd

#define DEFLATE_BUF_SIZE	4096

/* Node private data */
struct ng_deflate_private {
	struct ng_deflate_config cfg;		/* configuration */
	u_char		inbuf[DEFLATE_BUF_SIZE];	/* input buffer */
	u_char		outbuf[DEFLATE_BUF_SIZE];	/* output buffer */
	z_stream 	cx;			/* compression context */
	struct ng_deflate_stats stats;		/* statistics */
	ng_ID_t		ctrlnode;		/* path to controlling node */
	uint16_t	seqnum;			/* sequence number */
	u_char		compress;		/* compress/decompress flag */
};
typedef struct ng_deflate_private *priv_p;

/* Netgraph node methods */
static ng_constructor_t	ng_deflate_constructor;
static ng_rcvmsg_t	ng_deflate_rcvmsg;
static ng_shutdown_t	ng_deflate_shutdown;
static ng_newhook_t	ng_deflate_newhook;
static ng_rcvdata_t	ng_deflate_rcvdata;
static ng_disconnect_t	ng_deflate_disconnect;

/* Helper functions */
static void	*z_alloc(void *, u_int items, u_int size);
static void	z_free(void *, void *ptr);
static int	ng_deflate_compress(node_p node,
		    struct mbuf *m, struct mbuf **resultp);
static int	ng_deflate_decompress(node_p node,
		    struct mbuf *m, struct mbuf **resultp);
static void	ng_deflate_reset_req(node_p node);

/* Parse type for struct ng_deflate_config. */
static const struct ng_parse_struct_field ng_deflate_config_type_fields[]
	= NG_DEFLATE_CONFIG_INFO;
static const struct ng_parse_type ng_deflate_config_type = {
	&ng_parse_struct_type,
	ng_deflate_config_type_fields
};

/* Parse type for struct ng_deflate_stat. */
static const struct ng_parse_struct_field ng_deflate_stats_type_fields[]
	= NG_DEFLATE_STATS_INFO;
static const struct ng_parse_type ng_deflate_stat_type = {
	&ng_parse_struct_type,
	ng_deflate_stats_type_fields
};

/* List of commands and how to convert arguments to/from ASCII. */
static const struct ng_cmdlist ng_deflate_cmds[] = {
	{
	  NGM_DEFLATE_COOKIE,
	  NGM_DEFLATE_CONFIG,
	  "config",
	  &ng_deflate_config_type,
	  NULL
	},
	{
	  NGM_DEFLATE_COOKIE,
	  NGM_DEFLATE_RESETREQ,
	  "resetreq",
	  NULL,
	  NULL
	},
	{
	  NGM_DEFLATE_COOKIE,
	  NGM_DEFLATE_GET_STATS,
	  "getstats",
	  NULL,
	  &ng_deflate_stat_type
	},
	{
	  NGM_DEFLATE_COOKIE,
	  NGM_DEFLATE_CLR_STATS,
	  "clrstats",
	  NULL,
	  NULL
	},
	{
	  NGM_DEFLATE_COOKIE,
	  NGM_DEFLATE_GETCLR_STATS,
	  "getclrstats",
	  NULL,
	  &ng_deflate_stat_type
	},
	{ 0 }
};

/* Node type descriptor */
static struct ng_type ng_deflate_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_DEFLATE_NODE_TYPE,
	.constructor =	ng_deflate_constructor,
	.rcvmsg =	ng_deflate_rcvmsg,
	.shutdown =	ng_deflate_shutdown,
	.newhook =	ng_deflate_newhook,
	.rcvdata =	ng_deflate_rcvdata,
	.disconnect =	ng_deflate_disconnect,
	.cmdlist =	ng_deflate_cmds,
};
NETGRAPH_INIT(deflate, &ng_deflate_typestruct);

/* Depend on separate zlib module. */
MODULE_DEPEND(ng_deflate, zlib, 1, 1, 1);

#define ERROUT(x)	do { error = (x); goto done; } while (0)

/************************************************************************
			NETGRAPH NODE STUFF
 ************************************************************************/

/*
 * Node type constructor
 */
static int
ng_deflate_constructor(node_p node)
{
	priv_p priv;

	/* Allocate private structure. */
	priv = malloc(sizeof(*priv), M_NETGRAPH_DEFLATE, M_WAITOK | M_ZERO);

	NG_NODE_SET_PRIVATE(node, priv);

	/* This node is not thread safe. */
	NG_NODE_FORCE_WRITER(node);

	/* Done */
	return (0);
}

/*
 * Give our OK for a hook to be added.
 */
static int
ng_deflate_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (NG_NODE_NUMHOOKS(node) > 0)
		return (EINVAL);

	if (strcmp(name, NG_DEFLATE_HOOK_COMP) == 0)
		priv->compress = 1;
	else if (strcmp(name, NG_DEFLATE_HOOK_DECOMP) == 0)
		priv->compress = 0;
	else
		return (EINVAL);

	return (0);
}

/*
 * Receive a control message
 */
static int
ng_deflate_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);

	if (msg->header.typecookie != NGM_DEFLATE_COOKIE)
		ERROUT(EINVAL);

	switch (msg->header.cmd) {
	case NGM_DEFLATE_CONFIG:
	    {
		struct ng_deflate_config *const cfg
		    = (struct ng_deflate_config *)msg->data;

		/* Check configuration. */
		if (msg->header.arglen != sizeof(*cfg))
			ERROUT(EINVAL);
		if (cfg->enable) {
		    if (cfg->windowBits < 8 || cfg->windowBits > 15)
			ERROUT(EINVAL);
		} else
		    cfg->windowBits = 0;

		/* Clear previous state. */
		if (priv->cfg.enable) {
			if (priv->compress)
				deflateEnd(&priv->cx);
			else
				inflateEnd(&priv->cx);
			priv->cfg.enable = 0;
		}

		/* Configuration is OK, reset to it. */
		priv->cfg = *cfg;

		if (priv->cfg.enable) {
			priv->cx.next_in = NULL;
			priv->cx.zalloc = z_alloc;
			priv->cx.zfree = z_free;
			int res;
			if (priv->compress) {
				if ((res = deflateInit2(&priv->cx,
				    Z_DEFAULT_COMPRESSION, Z_DEFLATED,
				    -cfg->windowBits, 8,
				    Z_DEFAULT_STRATEGY)) != Z_OK) {
					log(LOG_NOTICE,
					    "deflateInit2: error %d, %s\n",
					    res, priv->cx.msg);
					priv->cfg.enable = 0;
					ERROUT(ENOMEM);
				}
			} else {
				if ((res = inflateInit2(&priv->cx,
				    -cfg->windowBits)) != Z_OK) {
					log(LOG_NOTICE,
					    "inflateInit2: error %d, %s\n",
					    res, priv->cx.msg);
					priv->cfg.enable = 0;
					ERROUT(ENOMEM);
				}
			}
		}

		/* Initialize other state. */
		priv->seqnum = 0;

		/* Save return address so we can send reset-req's */
		priv->ctrlnode = NGI_RETADDR(item);
		break;
	    }

	case NGM_DEFLATE_RESETREQ:
		ng_deflate_reset_req(node);
		break;

	case NGM_DEFLATE_GET_STATS:
	case NGM_DEFLATE_CLR_STATS:
	case NGM_DEFLATE_GETCLR_STATS:
		/* Create response if requested. */
		if (msg->header.cmd != NGM_DEFLATE_CLR_STATS) {
			NG_MKRESPONSE(resp, msg,
			    sizeof(struct ng_deflate_stats), M_NOWAIT);
			if (resp == NULL)
				ERROUT(ENOMEM);
			bcopy(&priv->stats, resp->data,
			    sizeof(struct ng_deflate_stats));
		}

		/* Clear stats if requested. */
		if (msg->header.cmd != NGM_DEFLATE_GET_STATS)
			bzero(&priv->stats,
			    sizeof(struct ng_deflate_stats));
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
 * Receive incoming data on our hook.
 */
static int
ng_deflate_rcvdata(hook_p hook, item_p item)
{
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct mbuf *m, *out;
	int error;

	if (!priv->cfg.enable) {
		NG_FREE_ITEM(item);
		return (ENXIO);
	}

	NGI_GET_M(item, m);
	/* Compress */
	if (priv->compress) {
		if ((error = ng_deflate_compress(node, m, &out)) != 0) {
			NG_FREE_ITEM(item);
			log(LOG_NOTICE, "%s: error: %d\n", __func__, error);
			return (error);
		}

	} else { /* Decompress */
		if ((error = ng_deflate_decompress(node, m, &out)) != 0) {
			NG_FREE_ITEM(item);
			log(LOG_NOTICE, "%s: error: %d\n", __func__, error);
			if (priv->ctrlnode != 0) {
				struct ng_mesg *msg;

				/* Need to send a reset-request. */
				NG_MKMESSAGE(msg, NGM_DEFLATE_COOKIE,
				    NGM_DEFLATE_RESETREQ, 0, M_NOWAIT);
				if (msg == NULL)
					return (error);
				NG_SEND_MSG_ID(error, node, msg,
					priv->ctrlnode, 0);
			}
			return (error);
		}
	}

	NG_FWD_NEW_DATA(error, item, hook, out);
	return (error);
}

/*
 * Destroy node.
 */
static int
ng_deflate_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	/* Take down netgraph node. */
	if (priv->cfg.enable) {
	    if (priv->compress)
		deflateEnd(&priv->cx);
	    else
		inflateEnd(&priv->cx);
	}

	free(priv, M_NETGRAPH_DEFLATE);
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);		/* let the node escape */
	return (0);
}

/*
 * Hook disconnection
 */
static int
ng_deflate_disconnect(hook_p hook)
{
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (priv->cfg.enable) {
	    if (priv->compress)
		deflateEnd(&priv->cx);
	    else
		inflateEnd(&priv->cx);
	    priv->cfg.enable = 0;
	}

	/* Go away if no longer connected. */
	if ((NG_NODE_NUMHOOKS(node) == 0) && NG_NODE_IS_VALID(node))
		ng_rmnode_self(node);
	return (0);
}

/************************************************************************
			HELPER STUFF
 ************************************************************************/

/*
 * Space allocation and freeing routines for use by zlib routines.
 */

static void *
z_alloc(void *notused, u_int items, u_int size)
{

	return (malloc(items * size, M_NETGRAPH_DEFLATE, M_NOWAIT));
}

static void
z_free(void *notused, void *ptr)
{

	free(ptr, M_NETGRAPH_DEFLATE);
}

/*
 * Compress/encrypt a packet and put the result in a new mbuf at *resultp.
 * The original mbuf is not free'd.
 */
static int
ng_deflate_compress(node_p node, struct mbuf *m, struct mbuf **resultp)
{
	const priv_p 	priv = NG_NODE_PRIVATE(node);
	int 		outlen, inlen;
	int 		rtn;

	/* Initialize. */
	*resultp = NULL;

	inlen = m->m_pkthdr.len;

	priv->stats.FramesPlain++;
	priv->stats.InOctets+=inlen;

	if (inlen > DEFLATE_BUF_SIZE) {
		priv->stats.Errors++;
		NG_FREE_M(m);
		return (ENOMEM);
	}

	/* We must own the mbuf chain exclusively to modify it. */
	m = m_unshare(m, M_NOWAIT);
	if (m == NULL) {
		priv->stats.Errors++;
		return (ENOMEM);
	}

	/* Work with contiguous regions of memory. */
	m_copydata(m, 0, inlen, (caddr_t)priv->inbuf);
	outlen = DEFLATE_BUF_SIZE;

	/* Compress "inbuf" into "outbuf". */
	/* Prepare to compress. */
	if (priv->inbuf[0] != 0) {
		priv->cx.next_in = priv->inbuf;
		priv->cx.avail_in = inlen;
	} else {
		priv->cx.next_in = priv->inbuf + 1; /* compress protocol */
		priv->cx.avail_in = inlen - 1;
	}
	priv->cx.next_out = priv->outbuf + 2 + DEFLATE_HDRLEN;
	priv->cx.avail_out = outlen - 2 - DEFLATE_HDRLEN;

	/* Compress. */
	rtn = deflate(&priv->cx, Z_PACKET_FLUSH);

	/* Check return value. */
	if (rtn != Z_OK) {
		priv->stats.Errors++;
		log(LOG_NOTICE, "ng_deflate: compression error: %d (%s)\n",
		    rtn, priv->cx.msg);
		NG_FREE_M(m);
		return (EINVAL);
	}

	/* Calculate resulting size. */
	outlen -= priv->cx.avail_out;

	/* If we can't compress this packet, send it as-is. */
	if (outlen > inlen) {
		/* Return original packet uncompressed. */
		*resultp = m;
		priv->stats.FramesUncomp++;
		priv->stats.OutOctets+=inlen;
	} else {
		/* Install header. */
		be16enc(priv->outbuf, PROT_COMPD);
		be16enc(priv->outbuf + 2, priv->seqnum);

		/* Return packet in an mbuf. */
		m_copyback(m, 0, outlen, (caddr_t)priv->outbuf);
		if (m->m_pkthdr.len < outlen) {
			m_freem(m);
			priv->stats.Errors++;
			return (ENOMEM);
		} else if (outlen < m->m_pkthdr.len)
			m_adj(m, outlen - m->m_pkthdr.len);
		*resultp = m;
		priv->stats.FramesComp++;
		priv->stats.OutOctets+=outlen;
	}

	/* Update sequence number. */
	priv->seqnum++;

	return (0);
}

/*
 * Decompress/decrypt packet and put the result in a new mbuf at *resultp.
 * The original mbuf is not free'd.
 */
static int
ng_deflate_decompress(node_p node, struct mbuf *m, struct mbuf **resultp)
{
	const priv_p 	priv = NG_NODE_PRIVATE(node);
	int 		outlen, inlen;
	int 		rtn;
	uint16_t	proto;
	int		offset;
	uint16_t	rseqnum;

	/* Initialize. */
	*resultp = NULL;

	inlen = m->m_pkthdr.len;

	if (inlen > DEFLATE_BUF_SIZE) {
		priv->stats.Errors++;
		NG_FREE_M(m);
		priv->seqnum = 0;
		return (ENOMEM);
	}

	/* We must own the mbuf chain exclusively to modify it. */
	m = m_unshare(m, M_NOWAIT);
	if (m == NULL) {
		priv->stats.Errors++;
		return (ENOMEM);
	}

	/* Work with contiguous regions of memory. */
	m_copydata(m, 0, inlen, (caddr_t)priv->inbuf);

	/* Separate proto. */
	if ((priv->inbuf[0] & 0x01) != 0) {
		proto = priv->inbuf[0];
		offset = 1;
	} else {
		proto = be16dec(priv->inbuf);
		offset = 2;
	}

	priv->stats.InOctets += inlen;

	/* Packet is compressed, so decompress. */
	if (proto == PROT_COMPD) {
		priv->stats.FramesComp++;

		/* Check sequence number. */
		rseqnum = be16dec(priv->inbuf + offset);
		offset += 2;
		if (rseqnum != priv->seqnum) {
			priv->stats.Errors++;
			log(LOG_NOTICE, "ng_deflate: wrong sequence: %u "
			    "instead of %u\n", rseqnum, priv->seqnum);
			NG_FREE_M(m);
			priv->seqnum = 0;
			return (EPIPE);
		}

		outlen = DEFLATE_BUF_SIZE;

    		/* Decompress "inbuf" into "outbuf". */
		/* Prepare to decompress. */
		priv->cx.next_in = priv->inbuf + offset;
		priv->cx.avail_in = inlen - offset;
		/* Reserve space for protocol decompression. */
		priv->cx.next_out = priv->outbuf + 1;
		priv->cx.avail_out = outlen - 1;

		/* Decompress. */
		rtn = inflate(&priv->cx, Z_PACKET_FLUSH);

		/* Check return value. */
		if (rtn != Z_OK && rtn != Z_STREAM_END) {
			priv->stats.Errors++;
			NG_FREE_M(m);
			priv->seqnum = 0;
			log(LOG_NOTICE, "%s: decompression error: %d (%s)\n",
			    __func__, rtn, priv->cx.msg);

			switch (rtn) {
			case Z_MEM_ERROR:
				return (ENOMEM);
			case Z_DATA_ERROR:
				return (EIO);
			default:
				return (EINVAL);
			}
		}

		/* Calculate resulting size. */
		outlen -= priv->cx.avail_out;

		/* Decompress protocol. */
		if ((priv->outbuf[1] & 0x01) != 0) {
			priv->outbuf[0] = 0;
			/* Return packet in an mbuf. */
			m_copyback(m, 0, outlen, (caddr_t)priv->outbuf);
		} else {
			outlen--;
			/* Return packet in an mbuf. */
			m_copyback(m, 0, outlen, (caddr_t)(priv->outbuf + 1));
		}
		if (m->m_pkthdr.len < outlen) {
			m_freem(m);
			priv->stats.Errors++;
			priv->seqnum = 0;
			return (ENOMEM);
		} else if (outlen < m->m_pkthdr.len)
			m_adj(m, outlen - m->m_pkthdr.len);
		*resultp = m;
		priv->stats.FramesPlain++;
		priv->stats.OutOctets+=outlen;

	} else { /* Packet is not compressed, just update dictionary. */
		priv->stats.FramesUncomp++;
		if (priv->inbuf[0] == 0) {
		    priv->cx.next_in = priv->inbuf + 1; /* compress protocol */
		    priv->cx.avail_in = inlen - 1;
		} else {
		    priv->cx.next_in = priv->inbuf;
		    priv->cx.avail_in = inlen;
		}

		rtn = inflateIncomp(&priv->cx);

		/* Check return value */
		if (rtn != Z_OK) {
			priv->stats.Errors++;
			log(LOG_NOTICE, "%s: inflateIncomp error: %d (%s)\n",
			    __func__, rtn, priv->cx.msg);
			NG_FREE_M(m);
			priv->seqnum = 0;
			return (EINVAL);
		}

		*resultp = m;
		priv->stats.FramesPlain++;
		priv->stats.OutOctets += inlen;
	}

	/* Update sequence number. */
	priv->seqnum++;

	return (0);
}

/*
 * The peer has sent us a CCP ResetRequest, so reset our transmit state.
 */
static void
ng_deflate_reset_req(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	priv->seqnum = 0;
	if (priv->cfg.enable) {
	    if (priv->compress)
		deflateReset(&priv->cx);
	    else
		inflateReset(&priv->cx);
	}
}


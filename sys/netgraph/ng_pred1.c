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
 * Predictor-1 PPP compression netgraph node type.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/syslog.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_pred1.h>

#include "opt_netgraph.h"

static MALLOC_DEFINE(M_NETGRAPH_PRED1, "netgraph_pred1", "netgraph pred1 node");

/* PRED1 header length */
#define PRED1_HDRLEN		2

#define PRED1_TABLE_SIZE	0x10000
#define PRED1_BUF_SIZE		4096
#define PPP_INITFCS		0xffff  /* Initial FCS value */
#define PPP_GOODFCS		0xf0b8  /* Good final FCS value */

/*
 * The following hash code is the heart of the algorithm:
 * it builds a sliding hash sum of the previous 3-and-a-bit
 * characters which will be used to index the guess table.
 * A better hash function would result in additional compression,
 * at the expense of time.
 */

#define HASH(x) priv->Hash = (priv->Hash << 4) ^ (x)

/* Node private data */
struct ng_pred1_private {
	struct ng_pred1_config cfg;		/* configuration */
	u_char		GuessTable[PRED1_TABLE_SIZE];	/* dictionary */
	u_char		inbuf[PRED1_BUF_SIZE];	/* input buffer */
	u_char		outbuf[PRED1_BUF_SIZE];	/* output buffer */
	struct ng_pred1_stats stats;		/* statistics */
	uint16_t	Hash;
	ng_ID_t		ctrlnode;		/* path to controlling node */
	uint16_t	seqnum;			/* sequence number */
	u_char		compress;		/* compress/decompress flag */
};
typedef struct ng_pred1_private *priv_p;

/* Netgraph node methods */
static ng_constructor_t	ng_pred1_constructor;
static ng_rcvmsg_t	ng_pred1_rcvmsg;
static ng_shutdown_t	ng_pred1_shutdown;
static ng_newhook_t	ng_pred1_newhook;
static ng_rcvdata_t	ng_pred1_rcvdata;
static ng_disconnect_t	ng_pred1_disconnect;

/* Helper functions */
static int	ng_pred1_compress(node_p node, struct mbuf *m,
		    struct mbuf **resultp);
static int	ng_pred1_decompress(node_p node, struct mbuf *m,
		    struct mbuf **resultp);
static void	Pred1Init(node_p node);
static int	Pred1Compress(node_p node, u_char *source, u_char *dest,
		    int len);
static int	Pred1Decompress(node_p node, u_char *source, u_char *dest,
		    int slen, int dlen);
static void	Pred1SyncTable(node_p node, u_char *source, int len);
static uint16_t	Crc16(uint16_t fcs, u_char *cp, int len);

static const uint16_t	Crc16Table[];

/* Parse type for struct ng_pred1_config. */
static const struct ng_parse_struct_field ng_pred1_config_type_fields[]
	= NG_PRED1_CONFIG_INFO;
static const struct ng_parse_type ng_pred1_config_type = {
	&ng_parse_struct_type,
	ng_pred1_config_type_fields
};

/* Parse type for struct ng_pred1_stat. */
static const struct ng_parse_struct_field ng_pred1_stats_type_fields[]
	= NG_PRED1_STATS_INFO;
static const struct ng_parse_type ng_pred1_stat_type = {
	&ng_parse_struct_type,
	ng_pred1_stats_type_fields
};

/* List of commands and how to convert arguments to/from ASCII. */
static const struct ng_cmdlist ng_pred1_cmds[] = {
	{
	  NGM_PRED1_COOKIE,
	  NGM_PRED1_CONFIG,
	  "config",
	  &ng_pred1_config_type,
	  NULL
	},
	{
	  NGM_PRED1_COOKIE,
	  NGM_PRED1_RESETREQ,
	  "resetreq",
	  NULL,
	  NULL
	},
	{
	  NGM_PRED1_COOKIE,
	  NGM_PRED1_GET_STATS,
	  "getstats",
	  NULL,
	  &ng_pred1_stat_type
	},
	{
	  NGM_PRED1_COOKIE,
	  NGM_PRED1_CLR_STATS,
	  "clrstats",
	  NULL,
	  NULL
	},
	{
	  NGM_PRED1_COOKIE,
	  NGM_PRED1_GETCLR_STATS,
	  "getclrstats",
	  NULL,
	  &ng_pred1_stat_type
	},
	{ 0 }
};

/* Node type descriptor */
static struct ng_type ng_pred1_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_PRED1_NODE_TYPE,
	.constructor =	ng_pred1_constructor,
	.rcvmsg =	ng_pred1_rcvmsg,
	.shutdown =	ng_pred1_shutdown,
	.newhook =	ng_pred1_newhook,
	.rcvdata =	ng_pred1_rcvdata,
	.disconnect =	ng_pred1_disconnect,
	.cmdlist =	ng_pred1_cmds,
};
NETGRAPH_INIT(pred1, &ng_pred1_typestruct);

#define ERROUT(x)	do { error = (x); goto done; } while (0)

/************************************************************************
			NETGRAPH NODE STUFF
 ************************************************************************/

/*
 * Node type constructor
 */
static int
ng_pred1_constructor(node_p node)
{
	priv_p priv;

	/* Allocate private structure. */
	priv = malloc(sizeof(*priv), M_NETGRAPH_PRED1, M_WAITOK | M_ZERO);

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
ng_pred1_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (NG_NODE_NUMHOOKS(node) > 0)
		return (EINVAL);

	if (strcmp(name, NG_PRED1_HOOK_COMP) == 0)
		priv->compress = 1;
	else if (strcmp(name, NG_PRED1_HOOK_DECOMP) == 0)
		priv->compress = 0;
	else
		return (EINVAL);

	return (0);
}

/*
 * Receive a control message.
 */
static int
ng_pred1_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);

	if (msg->header.typecookie != NGM_PRED1_COOKIE)
		ERROUT(EINVAL);

	switch (msg->header.cmd) {
	case NGM_PRED1_CONFIG:
	    {
		struct ng_pred1_config *const cfg =
		    (struct ng_pred1_config *)msg->data;

		/* Check configuration. */
		if (msg->header.arglen != sizeof(*cfg))
			ERROUT(EINVAL);

		/* Configuration is OK, reset to it. */
		priv->cfg = *cfg;

		/* Save return address so we can send reset-req's. */
		priv->ctrlnode = NGI_RETADDR(item);

		/* Clear our state. */
		Pred1Init(node);

		break;
	    }
	case NGM_PRED1_RESETREQ:
		Pred1Init(node);
		break;

	case NGM_PRED1_GET_STATS:
	case NGM_PRED1_CLR_STATS:
	case NGM_PRED1_GETCLR_STATS:
	    {
		/* Create response. */
		if (msg->header.cmd != NGM_PRED1_CLR_STATS) {
			NG_MKRESPONSE(resp, msg,
			    sizeof(struct ng_pred1_stats), M_NOWAIT);
			if (resp == NULL)
				ERROUT(ENOMEM);
			bcopy(&priv->stats, resp->data,
			    sizeof(struct ng_pred1_stats));
		}

		if (msg->header.cmd != NGM_PRED1_GET_STATS)
			bzero(&priv->stats, sizeof(struct ng_pred1_stats));
		break;
	    }

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
ng_pred1_rcvdata(hook_p hook, item_p item)
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
	/* Compress. */
	if (priv->compress) {
		if ((error = ng_pred1_compress(node, m, &out)) != 0) {
			NG_FREE_ITEM(item);
			log(LOG_NOTICE, "%s: error: %d\n", __func__, error);
			return (error);
		}

	} else { /* Decompress. */
		if ((error = ng_pred1_decompress(node, m, &out)) != 0) {
			NG_FREE_ITEM(item);
			log(LOG_NOTICE, "%s: error: %d\n", __func__, error);
			if (priv->ctrlnode != 0) {
				struct ng_mesg *msg;

				/* Need to send a reset-request. */
				NG_MKMESSAGE(msg, NGM_PRED1_COOKIE,
				    NGM_PRED1_RESETREQ, 0, M_NOWAIT);
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
ng_pred1_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	free(priv, M_NETGRAPH_PRED1);
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);		/* Let the node escape. */
	return (0);
}

/*
 * Hook disconnection
 */
static int
ng_pred1_disconnect(hook_p hook)
{
	const node_p node = NG_HOOK_NODE(hook);

	Pred1Init(node);

	/* Go away if no longer connected. */
	if ((NG_NODE_NUMHOOKS(node) == 0) && NG_NODE_IS_VALID(node))
		ng_rmnode_self(node);
	return (0);
}

/************************************************************************
			HELPER STUFF
 ************************************************************************/

/*
 * Compress/encrypt a packet and put the result in a new mbuf at *resultp.
 * The original mbuf is not free'd.
 */
static int
ng_pred1_compress(node_p node, struct mbuf *m, struct mbuf **resultp)
{
	const priv_p 	priv = NG_NODE_PRIVATE(node);
	int 		outlen, inlen;
	u_char		*out;
	uint16_t	fcs, lenn;
	int		len;

	/* Initialize. */
	*resultp = NULL;

	inlen = m->m_pkthdr.len;

	priv->stats.FramesPlain++;
	priv->stats.InOctets += inlen;

	/* Reserve space for expansion. */
	if (inlen > (PRED1_BUF_SIZE*8/9 + 1 + 4)) {
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
	m_copydata(m, 0, inlen, (caddr_t)(priv->inbuf + 2));

	lenn = htons(inlen & 0x7FFF);

	/* Compute FCS. */
	fcs = Crc16(PPP_INITFCS, (u_char *)&lenn, 2);
	fcs = Crc16(fcs, priv->inbuf + 2, inlen);
	fcs = ~fcs;

	/* Compress data. */
	len = Pred1Compress(node, priv->inbuf + 2, priv->outbuf + 2, inlen);

	/* What happened? */
	if (len < inlen) {
		out = priv->outbuf;
		outlen = 2 + len;
		*(uint16_t *)out = lenn;
		*out |= 0x80;
		priv->stats.FramesComp++;
	} else {
		out = priv->inbuf;
		outlen = 2 + inlen;
		*(uint16_t *)out = lenn;
		priv->stats.FramesUncomp++;
	}

	/* Add FCS. */
	(out + outlen)[0] = fcs & 0xFF;
	(out + outlen)[1] = fcs >> 8;

	/* Calculate resulting size. */
	outlen += 2;

	/* Return packet in an mbuf. */
	m_copyback(m, 0, outlen, (caddr_t)out);
	if (m->m_pkthdr.len < outlen) {
		m_freem(m);
		priv->stats.Errors++;
		return (ENOMEM);
	} else if (outlen < m->m_pkthdr.len)
		m_adj(m, outlen - m->m_pkthdr.len);
	*resultp = m;
	priv->stats.OutOctets += outlen;

	return (0);
}

/*
 * Decompress/decrypt packet and put the result in a new mbuf at *resultp.
 * The original mbuf is not free'd.
 */
static int
ng_pred1_decompress(node_p node, struct mbuf *m, struct mbuf **resultp)
{
	const priv_p 	priv = NG_NODE_PRIVATE(node);
	int 		inlen;
	uint16_t	len, len1, cf, lenn;
	uint16_t	fcs;

	/* Initialize. */
	*resultp = NULL;

	inlen = m->m_pkthdr.len;

	if (inlen > PRED1_BUF_SIZE) {
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

	priv->stats.InOctets += inlen;

	/* Get initial length value. */
	len = priv->inbuf[0] << 8;
	len += priv->inbuf[1];

	cf = (len & 0x8000);
	len &= 0x7fff;

	/* Is data compressed or not really? */
	if (cf) {
		priv->stats.FramesComp++;
		len1 = Pred1Decompress(node, priv->inbuf + 2, priv->outbuf,
		    inlen - 4, PRED1_BUF_SIZE);
		if (len != len1) {
			/* Error is detected. Send reset request */
			m_freem(m);
			priv->stats.Errors++;
			log(LOG_NOTICE, "ng_pred1: Comp length error (%d) "
			    "--> len (%d)\n", len, len1);
			return (EIO);
		}

		/*
		 * CRC check on receive is defined in RFC. It is surely required
		 * for compressed frames to signal dictionary corruption,
		 * but it is actually useless for uncompressed frames because
		 * the same check has already done by HDLC and/or other layer.
		 */
		lenn = htons(len);
		fcs = Crc16(PPP_INITFCS, (u_char *)&lenn, 2);
		fcs = Crc16(fcs, priv->outbuf, len);
		fcs = Crc16(fcs, priv->inbuf + inlen - 2, 2);

		if (fcs != PPP_GOODFCS) {
			m_freem(m);
			priv->stats.Errors++;
	    		log(LOG_NOTICE, "ng_pred1: Pred1: Bad CRC-16\n");
			return (EIO);
		}

		/* Return packet in an mbuf. */
		m_copyback(m, 0, len, (caddr_t)priv->outbuf);
		if (m->m_pkthdr.len < len) {
			m_freem(m);
			priv->stats.Errors++;
			return (ENOMEM);
		} else if (len < m->m_pkthdr.len)
			m_adj(m, len - m->m_pkthdr.len);
		*resultp = m;

	} else {
		priv->stats.FramesUncomp++;
		if (len != (inlen - 4)) {
			/* Wrong length. Send reset request */
			priv->stats.Errors++;
			log(LOG_NOTICE, "ng_pred1: Uncomp length error (%d) "
			    "--> len (%d)\n", len, inlen - 4);
			NG_FREE_M(m);
			return (EIO);
		}
		Pred1SyncTable(node, priv->inbuf + 2, len);
		m_adj(m, 2);	/* Strip length. */
		m_adj(m, -2);	/* Strip fcs. */
		*resultp = m;
	}

	priv->stats.FramesPlain++;
	priv->stats.OutOctets += len;

	return (0);
}

/*
 * Pred1Init()
 */

static void
Pred1Init(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	priv->Hash = 0;
	memset(priv->GuessTable, 0, PRED1_TABLE_SIZE);
}

/*
 * Pred1Compress()
 */

static int
Pred1Compress(node_p node, u_char *source, u_char *dest, int len)
{
	const priv_p 	priv = NG_NODE_PRIVATE(node);
	int		i;
	u_char		flags;
	u_char		*flagdest, *orgdest;

	orgdest = dest;
	while (len) {
		flagdest = dest++;
		flags = 0;	/* All guesses are wrong initially. */
		for (i = 0; i < 8 && len; i++) {
    			if (priv->GuessTable[priv->Hash] == *source)
				/* Guess was right - don't output. */
				flags |= (1 << i);
    			else {
				/* Guess wrong, output char. */
				priv->GuessTable[priv->Hash] = *source;
				*dest++ = *source;
    			}
    			HASH(*source++);
    			len--;
		}
		*flagdest = flags;
	}
	return (dest - orgdest);
}

/*
 * Pred1Decompress()
 *
 * Returns decompressed size, or -1 if we ran out of space.
 */

static int
Pred1Decompress(node_p node, u_char *source, u_char *dest, int slen, int dlen)
{
	const priv_p 	priv = NG_NODE_PRIVATE(node);
	int		i;
	u_char		flags, *orgdest;

	orgdest = dest;
	while (slen) {
		flags = *source++;
		slen--;
		for (i = 0; i < 8; i++, flags >>= 1) {
			if (dlen <= 0)
				return(-1);
			if (flags & 0x01)
				/* Guess correct */
				*dest = priv->GuessTable[priv->Hash];
			else {
				if (!slen)
					/* We seem to be really done -- cabo. */
					break;

				/* Guess wrong. */
				priv->GuessTable[priv->Hash] = *source;
				/* Read from source. */
				*dest = *source++;
				slen--;
			}
			HASH(*dest++);
			dlen--;
		}
	}
	return (dest - orgdest);
}

/*
 * Pred1SyncTable()
 */

static void
Pred1SyncTable(node_p node, u_char *source, int len)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	while (len--) {
		priv->GuessTable[priv->Hash] = *source;
		HASH(*source++);
	}
}

/*
 * Crc16()
 *
 * Compute the 16 bit frame check value, per RFC 1171 Appendix B,
 * on an array of bytes.
 */

static uint16_t
Crc16(uint16_t crc, u_char *cp, int len)
{
	while (len--)
		crc = (crc >> 8) ^ Crc16Table[(crc ^ *cp++) & 0xff];
	return (crc);
}

static const uint16_t Crc16Table[256] = {
/* 00 */    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
/* 08 */    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
/* 10 */    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
/* 18 */    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
/* 20 */    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
/* 28 */    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
/* 30 */    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
/* 38 */    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
/* 40 */    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
/* 48 */    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
/* 50 */    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
/* 58 */    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
/* 60 */    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
/* 68 */    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
/* 70 */    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
/* 78 */    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
/* 80 */    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
/* 88 */    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
/* 90 */    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
/* 98 */    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
/* a0 */    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
/* a8 */    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
/* b0 */    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
/* b8 */    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
/* c0 */    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
/* c8 */    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
/* d0 */    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
/* d8 */    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
/* e0 */    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
/* e8 */    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
/* f0 */    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
/* f8 */    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};


/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2004 Benno Rice <benno@eloquent.com.au>
 * All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_atmllc.h>

#include <net/if.h>
#include <net/ethernet.h>	/* for M_HASFCS and ETHER_HDR_LEN */

#define	NG_ATMLLC_HEADER		"\252\252\3\0\200\302"
#define	NG_ATMLLC_HEADER_LEN		(sizeof(struct atmllc))
#define	NG_ATMLLC_TYPE_ETHERNET_FCS	0x0001
#define	NG_ATMLLC_TYPE_FDDI_FCS		0x0004
#define	NG_ATMLLC_TYPE_ETHERNET_NOFCS	0x0007
#define	NG_ATMLLC_TYPE_FDDI_NOFCS	0x000A

struct ng_atmllc_priv {
	hook_p		atm;
	hook_p		ether;
	hook_p		fddi;
};

struct atmllc {
	uint8_t		llchdr[6];	/* aa.aa.03.00.00.00 */
	uint8_t		type[2];	/* "ethernet" type */
};

/* ATM_LLC macros: note type code in host byte order */
#define	ATM_LLC_TYPE(X) (((X)->type[0] << 8) | ((X)->type[1]))
#define	ATM_LLC_SETTYPE(X, V) do {		\
	(X)->type[0] = ((V) >> 8) & 0xff;	\
	(X)->type[1] = ((V) & 0xff);		\
    } while (0)

/* Netgraph methods. */
static ng_constructor_t		ng_atmllc_constructor;
static ng_shutdown_t		ng_atmllc_shutdown;
static ng_rcvmsg_t		ng_atmllc_rcvmsg;
static ng_newhook_t		ng_atmllc_newhook;
static ng_rcvdata_t		ng_atmllc_rcvdata;
static ng_disconnect_t		ng_atmllc_disconnect;

static struct ng_type ng_atmllc_typestruct = {
	.version =	NG_ABI_VERSION,	
	.name =		NG_ATMLLC_NODE_TYPE,
	.constructor =	ng_atmllc_constructor,
	.rcvmsg =	ng_atmllc_rcvmsg,
	.shutdown =	ng_atmllc_shutdown,
	.newhook =	ng_atmllc_newhook,
	.rcvdata =	ng_atmllc_rcvdata,
	.disconnect =	ng_atmllc_disconnect,
};
NETGRAPH_INIT(atmllc, &ng_atmllc_typestruct);

static int
ng_atmllc_constructor(node_p node)
{
	struct	ng_atmllc_priv *priv;

	priv = malloc(sizeof(*priv), M_NETGRAPH, M_WAITOK | M_ZERO);
	NG_NODE_SET_PRIVATE(node, priv);

	return (0);
}

static int
ng_atmllc_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct	ng_mesg *msg;
	int	error;

	error = 0;
	NGI_GET_MSG(item, msg);
	msg->header.flags |= NGF_RESP;
	NG_RESPOND_MSG(error, node, item, msg);
	return (error);
}

static int
ng_atmllc_shutdown(node_p node)
{
	struct	ng_atmllc_priv *priv;

	priv = NG_NODE_PRIVATE(node);

	free(priv, M_NETGRAPH);

	NG_NODE_UNREF(node);

	return (0);
}

static int
ng_atmllc_newhook(node_p node, hook_p hook, const char *name)
{
	struct	ng_atmllc_priv *priv;

	priv = NG_NODE_PRIVATE(node);

	if (strcmp(name, NG_ATMLLC_HOOK_ATM) == 0) {
		if (priv->atm != NULL) {
			return (EISCONN);
		}
		priv->atm = hook;
	} else if (strcmp(name, NG_ATMLLC_HOOK_ETHER) == 0) {
		if (priv->ether != NULL) {
			return (EISCONN);
		}
		priv->ether = hook;
	} else if (strcmp(name, NG_ATMLLC_HOOK_FDDI) == 0) {
		if (priv->fddi != NULL) {
			return (EISCONN);
		}
		priv->fddi = hook;
	} else {
		return (EINVAL);
	}

	return (0);
}

static int
ng_atmllc_rcvdata(hook_p hook, item_p item)
{
	struct	ng_atmllc_priv *priv;
	struct	mbuf *m;
	struct	atmllc *hdr;
	hook_p	outhook;
	u_int	padding;
	int	error;

	priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	NGI_GET_M(item, m);
	outhook = NULL;
	padding = 0;

	if (hook == priv->atm) {
		/* Ditch the pseudoheader. */
		hdr = mtod(m, struct atmllc *);
		/* m_adj(m, sizeof(struct atm_pseudohdr)); */

		/*
		 * Make sure we have the LLC and ethernet headers.
		 * The ethernet header size is slightly larger than the FDDI
		 * header, which is convenient.
		 */
		if (m->m_len < sizeof(struct atmllc) + ETHER_HDR_LEN) {
			m = m_pullup(m, sizeof(struct atmllc) + ETHER_HDR_LEN);
			if (m == NULL) {
				NG_FREE_ITEM(item);
				return (ENOMEM);
			}
		}

		/* Decode the LLC header. */
		hdr = mtod(m, struct atmllc *);
		if (ATM_LLC_TYPE(hdr) == NG_ATMLLC_TYPE_ETHERNET_NOFCS) {
			m->m_flags &= ~M_HASFCS;
			outhook = priv->ether;
			padding = 2;
		} else if (ATM_LLC_TYPE(hdr) == NG_ATMLLC_TYPE_ETHERNET_FCS) {
			m->m_flags |= M_HASFCS;
			outhook = priv->ether;
			padding = 2;
		} else if (ATM_LLC_TYPE(hdr) == NG_ATMLLC_TYPE_FDDI_NOFCS) {
			m->m_flags &= ~M_HASFCS;
			outhook = priv->fddi;
			padding = 3;
		} else if (ATM_LLC_TYPE(hdr) == NG_ATMLLC_TYPE_FDDI_FCS) {
			m->m_flags |= M_HASFCS;
			outhook = priv->fddi;
			padding = 3;
		} else {
			printf("ng_atmllc: unknown type: %x\n",
			    ATM_LLC_TYPE(hdr));
		}

		/* Remove the LLC header and any padding*/
		m_adj(m, sizeof(struct atmllc) + padding);
	} else if (hook == priv->ether) {
		/* Add the LLC header */
		M_PREPEND(m, NG_ATMLLC_HEADER_LEN + 2, M_NOWAIT);
		if (m == NULL) {
			printf("ng_atmllc: M_PREPEND failed\n");
			NG_FREE_ITEM(item);
			return (ENOMEM);
		}
		hdr = mtod(m, struct atmllc *);
		bzero((void *)hdr, sizeof(struct atmllc) + 2);
		bcopy(NG_ATMLLC_HEADER, hdr->llchdr, 6);
		if ((m->m_flags & M_HASFCS) != 0) {
			ATM_LLC_SETTYPE(hdr, NG_ATMLLC_TYPE_ETHERNET_FCS);
		} else {
			ATM_LLC_SETTYPE(hdr, NG_ATMLLC_TYPE_ETHERNET_NOFCS);
		}
		outhook = priv->atm;
	} else if (hook == priv->fddi) {
		/* Add the LLC header */
		M_PREPEND(m, NG_ATMLLC_HEADER_LEN + 3, M_NOWAIT);
		if (m == NULL) {
			printf("ng_atmllc: M_PREPEND failed\n");
			NG_FREE_ITEM(item);
			return (ENOMEM);
		}
		hdr = mtod(m, struct atmllc *);
		bzero((void *)hdr, sizeof(struct atmllc) + 3);
		bcopy(NG_ATMLLC_HEADER, hdr->llchdr, 6);
		if ((m->m_flags & M_HASFCS) != 0) {
			ATM_LLC_SETTYPE(hdr, NG_ATMLLC_TYPE_FDDI_FCS);
		} else {
			ATM_LLC_SETTYPE(hdr, NG_ATMLLC_TYPE_FDDI_NOFCS);
		}
		outhook = priv->atm;
	}

	if (outhook == NULL) {
		NG_FREE_M(m);
		NG_FREE_ITEM(item);
		return (0);
	}

	NG_FWD_NEW_DATA(error, item, outhook, m);
	return (error);
}

static int
ng_atmllc_disconnect(hook_p hook)
{
	node_p	node;
	struct	ng_atmllc_priv *priv;

	node = NG_HOOK_NODE(hook);
	priv = NG_NODE_PRIVATE(node);

	if (hook == priv->atm) {
		priv->atm = NULL;
	} else if (hook == priv->ether) {
		priv->ether = NULL;
	} else if (hook == priv->fddi) {
		priv->fddi = NULL;
	}

	if (NG_NODE_NUMHOOKS(node) == 0 && NG_NODE_IS_VALID(node)) {
		ng_rmnode_self(node);
	}

	return (0);
}

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 IPNET Internet Communication Company
 * Copyright (c) 2011 - 2012 Rozhuk Ivan <rozhuk.im@gmail.com>
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
 * Author: Ruslan Ermilov <ru@FreeBSD.org>
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_vlan_var.h>

#include <netgraph/ng_message.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_vlan.h>
#include <netgraph/netgraph.h>

struct ng_vlan_private {
	hook_p		downstream_hook;
	hook_p		nomatch_hook;
	uint32_t	decap_enable;
	uint32_t	encap_enable;
	uint16_t	encap_proto;
	hook_p		vlan_hook[(EVL_VLID_MASK + 1)];
};
typedef struct ng_vlan_private *priv_p;

#define	ETHER_VLAN_HDR_LEN (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN)
#define	VLAN_TAG_MASK	0xFFFF
#define	HOOK_VLAN_TAG_SET_MASK ((uintptr_t)((~0) & ~(VLAN_TAG_MASK)))
#define	IS_HOOK_VLAN_SET(hdata) \
	    ((((uintptr_t)hdata) & HOOK_VLAN_TAG_SET_MASK) == HOOK_VLAN_TAG_SET_MASK)

static ng_constructor_t	ng_vlan_constructor;
static ng_rcvmsg_t	ng_vlan_rcvmsg;
static ng_shutdown_t	ng_vlan_shutdown;
static ng_newhook_t	ng_vlan_newhook;
static ng_rcvdata_t	ng_vlan_rcvdata;
static ng_disconnect_t	ng_vlan_disconnect;

/* Parse type for struct ng_vlan_filter. */
static const struct ng_parse_struct_field ng_vlan_filter_fields[] =
	NG_VLAN_FILTER_FIELDS;
static const struct ng_parse_type ng_vlan_filter_type = {
	&ng_parse_struct_type,
	&ng_vlan_filter_fields
};

static int
ng_vlan_getTableLength(const struct ng_parse_type *type,
    const u_char *start, const u_char *buf)
{
	const struct ng_vlan_table *const table =
	    (const struct ng_vlan_table *)(buf - sizeof(u_int32_t));

	return table->n;
}

/* Parse type for struct ng_vlan_table. */
static const struct ng_parse_array_info ng_vlan_table_array_info = {
	&ng_vlan_filter_type,
	ng_vlan_getTableLength
};
static const struct ng_parse_type ng_vlan_table_array_type = {
	&ng_parse_array_type,
	&ng_vlan_table_array_info
};
static const struct ng_parse_struct_field ng_vlan_table_fields[] =
	NG_VLAN_TABLE_FIELDS;
static const struct ng_parse_type ng_vlan_table_type = {
	&ng_parse_struct_type,
	&ng_vlan_table_fields
};

/* List of commands and how to convert arguments to/from ASCII. */
static const struct ng_cmdlist ng_vlan_cmdlist[] = {
	{
	  NGM_VLAN_COOKIE,
	  NGM_VLAN_ADD_FILTER,
	  "addfilter",
	  &ng_vlan_filter_type,
	  NULL
	},
	{
	  NGM_VLAN_COOKIE,
	  NGM_VLAN_DEL_FILTER,
	  "delfilter",
	  &ng_parse_hookbuf_type,
	  NULL
	},
	{
	  NGM_VLAN_COOKIE,
	  NGM_VLAN_GET_TABLE,
	  "gettable",
	  NULL,
	  &ng_vlan_table_type
	},
	{
	  NGM_VLAN_COOKIE,
	  NGM_VLAN_DEL_VID_FLT,
	  "delvidflt",
	  &ng_parse_uint16_type,
	  NULL
	},
	{
	  NGM_VLAN_COOKIE,
	  NGM_VLAN_GET_DECAP,
	  "getdecap",
	  NULL,
	  &ng_parse_hint32_type
	},
	{
	  NGM_VLAN_COOKIE,
	  NGM_VLAN_SET_DECAP,
	  "setdecap",
	  &ng_parse_hint32_type,
	  NULL
	},
	{
	  NGM_VLAN_COOKIE,
	  NGM_VLAN_GET_ENCAP,
	  "getencap",
	  NULL,
	  &ng_parse_hint32_type
	},
	{
	  NGM_VLAN_COOKIE,
	  NGM_VLAN_SET_ENCAP,
	  "setencap",
	  &ng_parse_hint32_type,
	  NULL
	},
	{
	  NGM_VLAN_COOKIE,
	  NGM_VLAN_GET_ENCAP_PROTO,
	  "getencapproto",
	  NULL,
	  &ng_parse_hint16_type
	},
	{
	  NGM_VLAN_COOKIE,
	  NGM_VLAN_SET_ENCAP_PROTO,
	  "setencapproto",
	  &ng_parse_hint16_type,
	  NULL
	},
	{ 0 }
};

static struct ng_type ng_vlan_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_VLAN_NODE_TYPE,
	.constructor =	ng_vlan_constructor,
	.rcvmsg =	ng_vlan_rcvmsg,
	.shutdown =	ng_vlan_shutdown,
	.newhook =	ng_vlan_newhook,
	.rcvdata =	ng_vlan_rcvdata,
	.disconnect =	ng_vlan_disconnect,
	.cmdlist =	ng_vlan_cmdlist,
};
NETGRAPH_INIT(vlan, &ng_vlan_typestruct);


/*
 * Helper functions.
 */

static __inline int
m_chk(struct mbuf **mp, int len)
{

	if ((*mp)->m_pkthdr.len < len) {
		m_freem((*mp));
		(*mp) = NULL;
		return (EINVAL);
	}
	if ((*mp)->m_len < len && ((*mp) = m_pullup((*mp), len)) == NULL)
		return (ENOBUFS);

	return (0);
}


/*
 * Netgraph node functions.
 */

static int
ng_vlan_constructor(node_p node)
{
	priv_p priv;

	priv = malloc(sizeof(*priv), M_NETGRAPH, M_WAITOK | M_ZERO);
	priv->decap_enable = 0;
	priv->encap_enable = VLAN_ENCAP_FROM_FILTER;
	priv->encap_proto = htons(ETHERTYPE_VLAN);
	NG_NODE_SET_PRIVATE(node, priv);
	return (0);
}

static int
ng_vlan_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (strcmp(name, NG_VLAN_HOOK_DOWNSTREAM) == 0)
		priv->downstream_hook = hook;
	else if (strcmp(name, NG_VLAN_HOOK_NOMATCH) == 0)
		priv->nomatch_hook = hook;
	else {
		/*
		 * Any other hook name is valid and can
		 * later be associated with a filter rule.
		 */
	}
	NG_HOOK_SET_PRIVATE(hook, NULL);
	return (0);
}

static int
ng_vlan_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *msg, *resp = NULL;
	struct ng_vlan_filter *vf;
	hook_p hook;
	struct ng_vlan_table *t;
	uintptr_t hook_data;
	int i, vlan_count;
	uint16_t vid;
	int error = 0;

	NGI_GET_MSG(item, msg);
	/* Deal with message according to cookie and command. */
	switch (msg->header.typecookie) {
	case NGM_VLAN_COOKIE:
		switch (msg->header.cmd) {
		case NGM_VLAN_ADD_FILTER:
			/* Check that message is long enough. */
			if (msg->header.arglen != sizeof(*vf)) {
				error = EINVAL;
				break;
			}
			vf = (struct ng_vlan_filter *)msg->data;
			/* Sanity check the VLAN ID value. */
#ifdef	NG_VLAN_USE_OLD_VLAN_NAME
			if (vf->vid == 0 && vf->vid != vf->vlan) {
				vf->vid = vf->vlan;
			} else if (vf->vid != 0 && vf->vlan != 0 &&
			    vf->vid != vf->vlan) {
				error = EINVAL;
				break;
			}
#endif
			if (vf->vid & ~EVL_VLID_MASK ||
			    vf->pcp & ~7 ||
			    vf->cfi & ~1) {
				error = EINVAL;
				break;
			}
			/* Check that a referenced hook exists. */
			hook = ng_findhook(node, vf->hook_name);
			if (hook == NULL) {
				error = ENOENT;
				break;
			}
			/* And is not one of the special hooks. */
			if (hook == priv->downstream_hook ||
			    hook == priv->nomatch_hook) {
				error = EINVAL;
				break;
			}
			/* And is not already in service. */
			if (IS_HOOK_VLAN_SET(NG_HOOK_PRIVATE(hook))) {
				error = EEXIST;
				break;
			}
			/* Check we don't already trap this VLAN. */
			if (priv->vlan_hook[vf->vid] != NULL) {
				error = EEXIST;
				break;
			}
			/* Link vlan and hook together. */
			NG_HOOK_SET_PRIVATE(hook,
			    (void *)(HOOK_VLAN_TAG_SET_MASK |
			    EVL_MAKETAG(vf->vid, vf->pcp, vf->cfi)));
			priv->vlan_hook[vf->vid] = hook;
			break;
		case NGM_VLAN_DEL_FILTER:
			/* Check that message is long enough. */
			if (msg->header.arglen != NG_HOOKSIZ) {
				error = EINVAL;
				break;
			}
			/* Check that hook exists and is active. */
			hook = ng_findhook(node, (char *)msg->data);
			if (hook == NULL) {
				error = ENOENT;
				break;
			}
			hook_data = (uintptr_t)NG_HOOK_PRIVATE(hook);
			if (IS_HOOK_VLAN_SET(hook_data) == 0) {
				error = ENOENT;
				break;
			}

			KASSERT(priv->vlan_hook[EVL_VLANOFTAG(hook_data)] == hook,
			    ("%s: NGM_VLAN_DEL_FILTER: Invalid VID for Hook = %s\n",
			    __func__, (char *)msg->data));

			/* Purge a rule that refers to this hook. */
			priv->vlan_hook[EVL_VLANOFTAG(hook_data)] = NULL;
			NG_HOOK_SET_PRIVATE(hook, NULL);
			break;
		case NGM_VLAN_DEL_VID_FLT:
			/* Check that message is long enough. */
			if (msg->header.arglen != sizeof(uint16_t)) {
				error = EINVAL;
				break;
			}
			vid = (*((uint16_t *)msg->data));
			/* Sanity check the VLAN ID value. */
			if (vid & ~EVL_VLID_MASK) {
				error = EINVAL;
				break;
			}
			/* Check that hook exists and is active. */
			hook = priv->vlan_hook[vid];
			if (hook == NULL) {
				error = ENOENT;
				break;
			}
			hook_data = (uintptr_t)NG_HOOK_PRIVATE(hook);
			if (IS_HOOK_VLAN_SET(hook_data) == 0) {
				error = ENOENT;
				break;
			}

			KASSERT(EVL_VLANOFTAG(hook_data) == vid,
			    ("%s: NGM_VLAN_DEL_VID_FLT:"
			    " Invalid VID Hook = %us, must be: %us\n",
			    __func__, (uint16_t )EVL_VLANOFTAG(hook_data),
			    vid));

			/* Purge a rule that refers to this hook. */
			priv->vlan_hook[vid] = NULL;
			NG_HOOK_SET_PRIVATE(hook, NULL);
			break;
		case NGM_VLAN_GET_TABLE:
			/* Calculate vlans. */
			vlan_count = 0;
			for (i = 0; i < (EVL_VLID_MASK + 1); i ++) {
				if (priv->vlan_hook[i] != NULL &&
				    NG_HOOK_IS_VALID(priv->vlan_hook[i]))
					vlan_count ++;
			}

			/* Allocate memory for response. */
			NG_MKRESPONSE(resp, msg, sizeof(*t) +
			    vlan_count * sizeof(*t->filter), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}

			/* Pack data to response. */
			t = (struct ng_vlan_table *)resp->data;
			t->n = 0;
			vf = &t->filter[0];
			for (i = 0; i < (EVL_VLID_MASK + 1); i ++) {
				hook = priv->vlan_hook[i];
				if (hook == NULL || NG_HOOK_NOT_VALID(hook))
					continue;
				hook_data = (uintptr_t)NG_HOOK_PRIVATE(hook);
				if (IS_HOOK_VLAN_SET(hook_data) == 0)
					continue;

				KASSERT(EVL_VLANOFTAG(hook_data) == i,
				    ("%s: NGM_VLAN_GET_TABLE:"
				    " hook %s VID = %us, must be: %i\n",
				    __func__, NG_HOOK_NAME(hook),
				    (uint16_t)EVL_VLANOFTAG(hook_data), i));

#ifdef	NG_VLAN_USE_OLD_VLAN_NAME
				vf->vlan = i;
#endif
				vf->vid = i;
				vf->pcp = EVL_PRIOFTAG(hook_data);
				vf->cfi = EVL_CFIOFTAG(hook_data);
				strncpy(vf->hook_name,
				    NG_HOOK_NAME(hook), NG_HOOKSIZ);
				vf ++;
				t->n ++;
			}
			break;
		case NGM_VLAN_GET_DECAP:
			NG_MKRESPONSE(resp, msg, sizeof(uint32_t), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			(*((uint32_t *)resp->data)) = priv->decap_enable;
			break;
		case NGM_VLAN_SET_DECAP:
			if (msg->header.arglen != sizeof(uint32_t)) {
				error = EINVAL;
				break;
			}
			priv->decap_enable = (*((uint32_t *)msg->data));
			break;
		case NGM_VLAN_GET_ENCAP:
			NG_MKRESPONSE(resp, msg, sizeof(uint32_t), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			(*((uint32_t *)resp->data)) = priv->encap_enable;
			break;
		case NGM_VLAN_SET_ENCAP:
			if (msg->header.arglen != sizeof(uint32_t)) {
				error = EINVAL;
				break;
			}
			priv->encap_enable = (*((uint32_t *)msg->data));
			break;
		case NGM_VLAN_GET_ENCAP_PROTO:
			NG_MKRESPONSE(resp, msg, sizeof(uint16_t), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			(*((uint16_t *)resp->data)) = ntohs(priv->encap_proto);
			break;
		case NGM_VLAN_SET_ENCAP_PROTO:
			if (msg->header.arglen != sizeof(uint16_t)) {
				error = EINVAL;
				break;
			}
			priv->encap_proto = htons((*((uint16_t *)msg->data)));
			break;
		default: /* Unknown command. */
			error = EINVAL;
			break;
		}
		break;
	case NGM_FLOW_COOKIE:
	    {
		struct ng_mesg *copy;

		/*
		 * Flow control messages should come only
		 * from downstream.
		 */

		if (lasthook == NULL)
			break;
		if (lasthook != priv->downstream_hook)
			break;
		/* Broadcast the event to all uplinks. */
		for (i = 0; i < (EVL_VLID_MASK + 1); i ++) {
			if (priv->vlan_hook[i] == NULL)
				continue;

			NG_COPYMESSAGE(copy, msg, M_NOWAIT);
			if (copy == NULL)
				continue;
			NG_SEND_MSG_HOOK(error, node, copy,
			    priv->vlan_hook[i], 0);
		}
		break;
	    }
	default: /* Unknown type cookie. */
		error = EINVAL;
		break;
	}
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

static int
ng_vlan_rcvdata(hook_p hook, item_p item)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct ether_header *eh;
	struct ether_vlan_header *evl;
	int error;
	uintptr_t hook_data;
	uint16_t vid, eth_vtag;
	struct mbuf *m;
	hook_p dst_hook;


	NGI_GET_M(item, m);

	/* Make sure we have an entire header. */
	error = m_chk(&m, ETHER_HDR_LEN);
	if (error != 0)
		goto mchk_err;

	eh = mtod(m, struct ether_header *);
	if (hook == priv->downstream_hook) {
		/*
		 * If from downstream, select between a match hook
		 * or the nomatch hook.
		 */

		dst_hook = priv->nomatch_hook;

		/* Skip packets without tag. */
		if ((m->m_flags & M_VLANTAG) == 0 &&
		    eh->ether_type != priv->encap_proto) {
			if (dst_hook == NULL)
				goto net_down;
			goto send_packet;
		}

		/* Process packets with tag. */
		if (m->m_flags & M_VLANTAG) {
			/*
			 * Packet is tagged, m contains a normal
			 * Ethernet frame; tag is stored out-of-band.
			 */
			evl = NULL;
			vid = EVL_VLANOFTAG(m->m_pkthdr.ether_vtag);
		} else { /* eh->ether_type == priv->encap_proto */
			error = m_chk(&m, ETHER_VLAN_HDR_LEN);
			if (error != 0)
				goto mchk_err;
			evl = mtod(m, struct ether_vlan_header *);
			vid = EVL_VLANOFTAG(ntohs(evl->evl_tag));
		}

		if (priv->vlan_hook[vid] != NULL) {
			/*
			 * VLAN filter: always remove vlan tags and
			 * decapsulate packet.
			 */
			dst_hook = priv->vlan_hook[vid];
			if (evl == NULL) { /* m->m_flags & M_VLANTAG */
				m->m_pkthdr.ether_vtag = 0;
				m->m_flags &= ~M_VLANTAG;
				goto send_packet;
			}
		} else { /* nomatch_hook */
			if (dst_hook == NULL)
				goto net_down;
			if (evl == NULL || priv->decap_enable == 0)
				goto send_packet;
			/* Save tag out-of-band. */
			m->m_pkthdr.ether_vtag = ntohs(evl->evl_tag);
			m->m_flags |= M_VLANTAG;
		}

		/*
		 * Decapsulate:
		 * TPID = ether type encap
		 * Move DstMAC and SrcMAC to ETHER_TYPE.
		 * Before:
		 *  [dmac] [smac] [TPID] [PCP/CFI/VID] [ether_type] [payload]
		 *  |-----------| >>>>>>>>>>>>>>>>>>>> |--------------------|
		 * After:
		 *  [free space ] [dmac] [smac] [ether_type] [payload]
		 *                |-----------| |--------------------|
		 */
		bcopy((char *)evl, ((char *)evl + ETHER_VLAN_ENCAP_LEN),
		    (ETHER_ADDR_LEN * 2));
		m_adj(m, ETHER_VLAN_ENCAP_LEN);
	} else {
		/*
		 * It is heading towards the downstream.
		 * If from nomatch, pass it unmodified.
		 * Otherwise, do the VLAN encapsulation.
		 */
		dst_hook = priv->downstream_hook;
		if (dst_hook == NULL)
			goto net_down;
		if (hook != priv->nomatch_hook) {/* Filter hook. */
			hook_data = (uintptr_t)NG_HOOK_PRIVATE(hook);
			if (IS_HOOK_VLAN_SET(hook_data) == 0) {
				/*
				 * Packet from hook not in filter
				 * call addfilter for this hook to fix.
				 */
				error = EOPNOTSUPP;
				goto drop;
			}
			eth_vtag = (hook_data & VLAN_TAG_MASK);
			if ((priv->encap_enable & VLAN_ENCAP_FROM_FILTER) == 0) {
				/* Just set packet header tag and send. */
				m->m_flags |= M_VLANTAG;
				m->m_pkthdr.ether_vtag = eth_vtag;
				goto send_packet;
			}
		} else { /* nomatch_hook */
			if ((priv->encap_enable & VLAN_ENCAP_FROM_NOMATCH) == 0 ||
			    (m->m_flags & M_VLANTAG) == 0)
				goto send_packet;
			/* Encapsulate tagged packet. */
			eth_vtag = m->m_pkthdr.ether_vtag;
			m->m_pkthdr.ether_vtag = 0;
			m->m_flags &= ~M_VLANTAG;
		}

		/*
		 * Transform the Ethernet header into an Ethernet header
		 * with 802.1Q encapsulation.
		 * Mod of: ether_vlanencap.
		 *
		 * TPID = ether type encap
		 * Move DstMAC and SrcMAC from ETHER_TYPE.
		 * Before:
		 *  [free space ] [dmac] [smac] [ether_type] [payload]
		 *  <<<<<<<<<<<<< |-----------| |--------------------|
		 * After:
		 *  [dmac] [smac] [TPID] [PCP/CFI/VID] [ether_type] [payload]
		 *  |-----------| |-- inserted tag --| |--------------------|
		 */
		M_PREPEND(m, ETHER_VLAN_ENCAP_LEN, M_NOWAIT);
		if (m == NULL)
			error = ENOMEM;
		else
			error = m_chk(&m, ETHER_VLAN_HDR_LEN);
		if (error != 0)
			goto mchk_err;

		evl = mtod(m, struct ether_vlan_header *);
		bcopy(((char *)evl + ETHER_VLAN_ENCAP_LEN),
		    (char *)evl, (ETHER_ADDR_LEN * 2));
		evl->evl_encap_proto = priv->encap_proto;
		evl->evl_tag = htons(eth_vtag);
	}

send_packet:
	NG_FWD_NEW_DATA(error, item, dst_hook, m);
	return (error);
net_down:
	error = ENETDOWN;
drop:
	m_freem(m);
mchk_err:
	NG_FREE_ITEM(item);
	return (error);
}

static int
ng_vlan_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);
	free(priv, M_NETGRAPH);
	return (0);
}

static int
ng_vlan_disconnect(hook_p hook)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	uintptr_t hook_data;

	if (hook == priv->downstream_hook)
		priv->downstream_hook = NULL;
	else if (hook == priv->nomatch_hook)
		priv->nomatch_hook = NULL;
	else {
		/* Purge a rule that refers to this hook. */
		hook_data = (uintptr_t)NG_HOOK_PRIVATE(hook);
		if (IS_HOOK_VLAN_SET(hook_data))
			priv->vlan_hook[EVL_VLANOFTAG(hook_data)] = NULL;
	}
	NG_HOOK_SET_PRIVATE(hook, NULL);
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0) &&
	    (NG_NODE_IS_VALID(NG_HOOK_NODE(hook))))
		ng_rmnode_self(NG_HOOK_NODE(hook));
	return (0);
}

/*
 * ng_hci_main.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_hci_main.c,v 1.2 2003/03/18 00:09:36 max Exp $
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/hci/ng_hci_var.h>
#include <netgraph/bluetooth/hci/ng_hci_prse.h>
#include <netgraph/bluetooth/hci/ng_hci_cmds.h>
#include <netgraph/bluetooth/hci/ng_hci_evnt.h>
#include <netgraph/bluetooth/hci/ng_hci_ulpi.h>
#include <netgraph/bluetooth/hci/ng_hci_misc.h>

/******************************************************************************
 ******************************************************************************
 **     This node implements Bluetooth Host Controller Interface (HCI)
 ******************************************************************************
 ******************************************************************************/

/* MALLOC define */
#ifdef NG_SEPARATE_MALLOC
MALLOC_DEFINE(M_NETGRAPH_HCI, "netgraph_hci", "Netgraph Bluetooth HCI node");
#else
#define M_NETGRAPH_HCI M_NETGRAPH
#endif /* NG_SEPARATE_MALLOC */

/* Netgraph node methods */
static	ng_constructor_t	ng_hci_constructor;
static	ng_shutdown_t		ng_hci_shutdown;
static	ng_newhook_t		ng_hci_newhook;
static	ng_connect_t		ng_hci_connect;
static	ng_disconnect_t		ng_hci_disconnect;
static	ng_rcvmsg_t		ng_hci_default_rcvmsg;
static	ng_rcvmsg_t		ng_hci_upper_rcvmsg;
static	ng_rcvdata_t		ng_hci_drv_rcvdata;
static	ng_rcvdata_t		ng_hci_acl_rcvdata;
static	ng_rcvdata_t		ng_hci_sco_rcvdata;
static	ng_rcvdata_t		ng_hci_raw_rcvdata;

/* Netgraph node type descriptor */
static	struct ng_type		typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_HCI_NODE_TYPE,
	.constructor =	ng_hci_constructor,
	.rcvmsg =	ng_hci_default_rcvmsg,
	.shutdown =	ng_hci_shutdown,
	.newhook =	ng_hci_newhook,
	.connect =	ng_hci_connect,
	.rcvdata =	ng_hci_drv_rcvdata,
	.disconnect =	ng_hci_disconnect,
	.cmdlist =	ng_hci_cmdlist,
};
NETGRAPH_INIT(hci, &typestruct);
MODULE_VERSION(ng_hci, NG_BLUETOOTH_VERSION);
MODULE_DEPEND(ng_hci, ng_bluetooth, NG_BLUETOOTH_VERSION,
	NG_BLUETOOTH_VERSION, NG_BLUETOOTH_VERSION);
static int ng_hci_linktype_to_addrtype(int linktype);

static int ng_hci_linktype_to_addrtype(int linktype)
{
	switch(linktype){
	case NG_HCI_LINK_LE_PUBLIC:
		return BDADDR_LE_PUBLIC;
	case NG_HCI_LINK_LE_RANDOM:
		return BDADDR_LE_RANDOM;
	case NG_HCI_LINK_ACL:
		/*FALLTHROUGH*/
	default:
		return BDADDR_BREDR;
	}
	return BDADDR_BREDR;
}
/*****************************************************************************
 *****************************************************************************
 **                   Netgraph methods implementation
 *****************************************************************************
 *****************************************************************************/

/*
 * Create new instance of HCI node (new unit)
 */

static int
ng_hci_constructor(node_p node)
{
	ng_hci_unit_p	unit = NULL;

	unit = malloc(sizeof(*unit), M_NETGRAPH_HCI, M_WAITOK | M_ZERO);

	unit->node = node;
	unit->debug = NG_HCI_WARN_LEVEL;

	unit->link_policy_mask = 0xffff; /* Enable all supported modes */
	unit->packet_mask = 0xffff; /* Enable all packet types */
	unit->role_switch = 1; /* Enable role switch (if device supports it) */

	/*
	 * Set default buffer info
	 *
	 * One HCI command
	 * One ACL packet with max. size of 17 bytes (1 DM1 packet)
	 * One SCO packet with max. size of 10 bytes (1 HV1 packet)
	 */

	NG_HCI_BUFF_CMD_SET(unit->buffer, 1);
	NG_HCI_BUFF_ACL_SET(unit->buffer, 1, 17, 1);  
	NG_HCI_BUFF_SCO_SET(unit->buffer, 1, 10, 1);

	/* Init command queue & command timeout handler */
	ng_callout_init(&unit->cmd_timo);
	NG_BT_MBUFQ_INIT(&unit->cmdq, NG_HCI_CMD_QUEUE_LEN);

	/* Init lists */
	LIST_INIT(&unit->con_list);
	LIST_INIT(&unit->neighbors);

	/*
	 * This node has to be a WRITER because both data and messages
	 * can change node state. 
	 */

	NG_NODE_FORCE_WRITER(node);
	NG_NODE_SET_PRIVATE(node, unit);

	return (0);
} /* ng_hci_constructor */

/*
 * Destroy the node
 */

static int
ng_hci_shutdown(node_p node)
{
	ng_hci_unit_p	unit = (ng_hci_unit_p) NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);

	unit->node = NULL;
	ng_hci_unit_clean(unit, 0x16 /* Connection terminated by local host */);

	NG_BT_MBUFQ_DESTROY(&unit->cmdq);

	bzero(unit, sizeof(*unit));
	free(unit, M_NETGRAPH_HCI);

	return (0);
} /* ng_hci_shutdown */

/*
 * Give our OK for a hook to be added. Unit driver is connected to the driver 
 * (NG_HCI_HOOK_DRV) hook. Upper layer protocols are connected to appropriate
 * (NG_HCI_HOOK_ACL or NG_HCI_HOOK_SCO) hooks.
 */

static int
ng_hci_newhook(node_p node, hook_p hook, char const *name)
{
	ng_hci_unit_p	 unit = (ng_hci_unit_p) NG_NODE_PRIVATE(node);
	hook_p		*h = NULL;

	if (strcmp(name, NG_HCI_HOOK_DRV) == 0)
		h = &unit->drv;
	else if (strcmp(name, NG_HCI_HOOK_ACL) == 0)
		h = &unit->acl;
	else if (strcmp(name, NG_HCI_HOOK_SCO) == 0)
		h = &unit->sco;
	else if (strcmp(name, NG_HCI_HOOK_RAW) == 0)
		h = &unit->raw;
	else
		return (EINVAL);

	if (*h != NULL)
		return (EISCONN);

	*h = hook;

	return (0);
} /* ng_hci_newhook */

/*
 * Give our final OK to connect hook
 */

static int
ng_hci_connect(hook_p hook)
{
	ng_hci_unit_p	unit = (ng_hci_unit_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (hook != unit->drv) {
		if (hook == unit->acl) {
			NG_HOOK_SET_RCVMSG(hook, ng_hci_upper_rcvmsg);
			NG_HOOK_SET_RCVDATA(hook, ng_hci_acl_rcvdata);
		} else if (hook == unit->sco) {
			NG_HOOK_SET_RCVMSG(hook, ng_hci_upper_rcvmsg);
			NG_HOOK_SET_RCVDATA(hook, ng_hci_sco_rcvdata);
		} else
			NG_HOOK_SET_RCVDATA(hook, ng_hci_raw_rcvdata);

		/* Send delayed notification to the upper layers */
		if (hook != unit->raw) 
			ng_send_fn(unit->node, hook, ng_hci_node_is_up, NULL,0);
	} else
		unit->state |= NG_HCI_UNIT_CONNECTED;

	return (0);
} /* ng_hci_connect */

/*
 * Disconnect the hook
 */

static int
ng_hci_disconnect(hook_p hook)
{
	ng_hci_unit_p	 unit = (ng_hci_unit_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (hook == unit->acl)
		unit->acl = NULL;
	else if (hook == unit->sco)
		unit->sco = NULL;
	else if (hook == unit->raw)
		unit->raw = NULL;
	else if (hook == unit->drv) {
		unit->drv = NULL;

		/* Connection terminated by local host */
		ng_hci_unit_clean(unit, 0x16);
		unit->state &= ~(NG_HCI_UNIT_CONNECTED|NG_HCI_UNIT_INITED);
	} else
		return (EINVAL);

	/* Shutdown when all hooks are disconnected */
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0) &&
	    (NG_NODE_IS_VALID(NG_HOOK_NODE(hook))))
		ng_rmnode_self(NG_HOOK_NODE(hook));

	return (0);
} /* ng_hci_disconnect */

/*
 * Default control message processing routine. Control message could be:
 *
 * 1) GENERIC Netgraph messages
 *
 * 2) Control message directed to the node itself.
 */

static int
ng_hci_default_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	ng_hci_unit_p	 unit = (ng_hci_unit_p) NG_NODE_PRIVATE(node);
	struct ng_mesg	*msg = NULL, *rsp = NULL;
	int		 error = 0;

	NGI_GET_MSG(item, msg); 

	switch (msg->header.typecookie) {
	case NGM_GENERIC_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TEXT_STATUS: {
			int	cmd_avail,
				acl_total, acl_avail, acl_size,
				sco_total, sco_avail, sco_size;

			NG_MKRESPONSE(rsp, msg, NG_TEXTRESPONSE, M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			NG_HCI_BUFF_CMD_GET(unit->buffer, cmd_avail);

			NG_HCI_BUFF_ACL_AVAIL(unit->buffer, acl_avail);
			NG_HCI_BUFF_ACL_TOTAL(unit->buffer, acl_total);
			NG_HCI_BUFF_ACL_SIZE(unit->buffer, acl_size);

			NG_HCI_BUFF_SCO_AVAIL(unit->buffer, sco_avail);
			NG_HCI_BUFF_SCO_TOTAL(unit->buffer, sco_total);
			NG_HCI_BUFF_SCO_SIZE(unit->buffer, sco_size);

			snprintf(rsp->data, NG_TEXTRESPONSE,
				"bdaddr %x:%x:%x:%x:%x:%x\n" \
				"Hooks  %s %s %s %s\n" \
				"State  %#x\n" \
				"Queue  cmd:%d\n" \
				"Buffer cmd:%d,acl:%d,%d,%d,sco:%d,%d,%d",
				unit->bdaddr.b[5], unit->bdaddr.b[4],
				unit->bdaddr.b[3], unit->bdaddr.b[2],
				unit->bdaddr.b[1], unit->bdaddr.b[0],
				(unit->drv != NULL)? NG_HCI_HOOK_DRV : "",
				(unit->acl != NULL)? NG_HCI_HOOK_ACL : "",
				(unit->sco != NULL)? NG_HCI_HOOK_SCO : "",
				(unit->raw != NULL)? NG_HCI_HOOK_RAW : "",
				unit->state,
				NG_BT_MBUFQ_LEN(&unit->cmdq),
				cmd_avail,
				acl_avail, acl_total, acl_size, 
				sco_avail, sco_total, sco_size);
			} break;

		default:
			error = EINVAL;
			break;
		}
		break;

	case NGM_HCI_COOKIE:
		switch (msg->header.cmd) {
		/* Get current node state */
		case NGM_HCI_NODE_GET_STATE:
			NG_MKRESPONSE(rsp, msg, sizeof(unit->state), M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			*((ng_hci_node_state_ep *)(rsp->data)) = unit->state;
			break;

		/* Turn INITED bit - node initialized */
		case NGM_HCI_NODE_INIT:
			if (bcmp(&unit->bdaddr, NG_HCI_BDADDR_ANY,
					sizeof(bdaddr_t)) == 0) {
				error = ENXIO;
				break;
			}
				
			unit->state |= NG_HCI_UNIT_INITED;

			ng_hci_node_is_up(unit->node, unit->acl, NULL, 0);
			ng_hci_node_is_up(unit->node, unit->sco, NULL, 0);
			break;

		/* Get node debug level */
		case NGM_HCI_NODE_GET_DEBUG:
			NG_MKRESPONSE(rsp, msg, sizeof(unit->debug), M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			*((ng_hci_node_debug_ep *)(rsp->data)) = unit->debug;
			break;

		/* Set node debug level */
		case NGM_HCI_NODE_SET_DEBUG:
			if (msg->header.arglen != sizeof(ng_hci_node_debug_ep)){
				error = EMSGSIZE;
				break;
			}

			unit->debug = *((ng_hci_node_debug_ep *)(msg->data));
			break;

		/* Get buffer info */
		case NGM_HCI_NODE_GET_BUFFER: {
			ng_hci_node_buffer_ep	*ep = NULL;

			NG_MKRESPONSE(rsp, msg, sizeof(ng_hci_node_buffer_ep),
				M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			ep = (ng_hci_node_buffer_ep *)(rsp->data);

			NG_HCI_BUFF_CMD_GET(unit->buffer, ep->cmd_free);
			NG_HCI_BUFF_ACL_AVAIL(unit->buffer, ep->acl_free);
			NG_HCI_BUFF_ACL_TOTAL(unit->buffer, ep->acl_pkts);
			NG_HCI_BUFF_ACL_SIZE(unit->buffer, ep->acl_size);
			NG_HCI_BUFF_SCO_AVAIL(unit->buffer, ep->sco_free);
			NG_HCI_BUFF_SCO_TOTAL(unit->buffer, ep->sco_pkts);
			NG_HCI_BUFF_SCO_SIZE(unit->buffer, ep->sco_size);
			} break;

		/* Get BDADDR */
		case NGM_HCI_NODE_GET_BDADDR:
			NG_MKRESPONSE(rsp, msg, sizeof(bdaddr_t), M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			bcopy(&unit->bdaddr, rsp->data, sizeof(bdaddr_t));
			break;

		/* Get features */
		case NGM_HCI_NODE_GET_FEATURES:
			NG_MKRESPONSE(rsp,msg,sizeof(unit->features),M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			bcopy(&unit->features,rsp->data,sizeof(unit->features));
			break;

		/* Get stat */
		case NGM_HCI_NODE_GET_STAT:
			NG_MKRESPONSE(rsp, msg, sizeof(unit->stat), M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			bcopy(&unit->stat, rsp->data, sizeof(unit->stat));
			break;

		/* Reset stat */
		case NGM_HCI_NODE_RESET_STAT:
			NG_HCI_STAT_RESET(unit->stat);
			break;

		/* Clean up neighbors list */
		case NGM_HCI_NODE_FLUSH_NEIGHBOR_CACHE:
			ng_hci_flush_neighbor_cache(unit);
			break;

		/* Get neighbor cache entries */
		case NGM_HCI_NODE_GET_NEIGHBOR_CACHE: {
			ng_hci_neighbor_p			 n = NULL;
			ng_hci_node_get_neighbor_cache_ep	*e1 = NULL;
			ng_hci_node_neighbor_cache_entry_ep	*e2 = NULL;
			int					 s = 0;

			/* Look for the fresh entries in the cache */
			for (n = LIST_FIRST(&unit->neighbors); n != NULL; ) {
				ng_hci_neighbor_p	nn = LIST_NEXT(n, next);

				if (ng_hci_neighbor_stale(n))
					ng_hci_free_neighbor(n);
				else
					s ++;

				n = nn;
			}
			if (s > NG_HCI_MAX_NEIGHBOR_NUM)
				s = NG_HCI_MAX_NEIGHBOR_NUM;

			/* Prepare response */
			NG_MKRESPONSE(rsp, msg, sizeof(*e1) + s * sizeof(*e2),
				M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			e1 = (ng_hci_node_get_neighbor_cache_ep *)(rsp->data);
			e2 = (ng_hci_node_neighbor_cache_entry_ep *)(e1 + 1);

			e1->num_entries = s;

			LIST_FOREACH(n, &unit->neighbors, next) {
				e2->page_scan_rep_mode = n->page_scan_rep_mode;
				e2->page_scan_mode = n->page_scan_mode;
				e2->clock_offset = n->clock_offset;
				e2->addrtype =
					ng_hci_linktype_to_addrtype(n->addrtype);
				e2->extinq_size = n->extinq_size;
				bcopy(&n->bdaddr, &e2->bdaddr, 
					sizeof(e2->bdaddr));
				bcopy(&n->features, &e2->features,
					sizeof(e2->features));
				bcopy(&n->extinq_data, &e2->extinq_data,
				      n->extinq_size);
				e2 ++;
				if (--s <= 0)
					break;
			}
			} break;

		/* Get connection list */
		case NGM_HCI_NODE_GET_CON_LIST: {
			ng_hci_unit_con_p	 c = NULL;
			ng_hci_node_con_list_ep	*e1 = NULL;
			ng_hci_node_con_ep	*e2 = NULL;
			int			 s = 0;

			/* Count number of connections in the list */
			LIST_FOREACH(c, &unit->con_list, next)
				s ++;
			if (s > NG_HCI_MAX_CON_NUM)
				s = NG_HCI_MAX_CON_NUM;

			/* Prepare response */
			NG_MKRESPONSE(rsp, msg, sizeof(*e1) + s * sizeof(*e2), 
				M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			e1 = (ng_hci_node_con_list_ep *)(rsp->data);
			e2 = (ng_hci_node_con_ep *)(e1 + 1);

			e1->num_connections = s;

			LIST_FOREACH(c, &unit->con_list, next) {
				e2->link_type = c->link_type;
				e2->encryption_mode= c->encryption_mode;
				e2->mode = c->mode;
				e2->role = c->role;

				e2->state = c->state;

				e2->pending = c->pending;
				e2->queue_len = NG_BT_ITEMQ_LEN(&c->conq);

				e2->con_handle = c->con_handle;
				bcopy(&c->bdaddr, &e2->bdaddr, 
					sizeof(e2->bdaddr));

				e2 ++;
				if (--s <= 0)
					break;
			}
			} break;

		/* Get link policy settings mask */
		case NGM_HCI_NODE_GET_LINK_POLICY_SETTINGS_MASK:
			NG_MKRESPONSE(rsp, msg, sizeof(unit->link_policy_mask),
				M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			*((ng_hci_node_link_policy_mask_ep *)(rsp->data)) = 
				unit->link_policy_mask;
			break;

		/* Set link policy settings mask */
		case NGM_HCI_NODE_SET_LINK_POLICY_SETTINGS_MASK:
			if (msg->header.arglen !=
				sizeof(ng_hci_node_link_policy_mask_ep)) {
				error = EMSGSIZE;
				break;
			}

			unit->link_policy_mask = 
				*((ng_hci_node_link_policy_mask_ep *)
					(msg->data));
			break;

		/* Get packet mask */
		case NGM_HCI_NODE_GET_PACKET_MASK:
			NG_MKRESPONSE(rsp, msg, sizeof(unit->packet_mask),
				M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			*((ng_hci_node_packet_mask_ep *)(rsp->data)) = 
				unit->packet_mask;
			break;

		/* Set packet mask */
		case NGM_HCI_NODE_SET_PACKET_MASK:
			if (msg->header.arglen !=
					sizeof(ng_hci_node_packet_mask_ep)) {
				error = EMSGSIZE;
				break;
			}

			unit->packet_mask = 
				*((ng_hci_node_packet_mask_ep *)(msg->data));
			break;

		/* Get role switch */
		case NGM_HCI_NODE_GET_ROLE_SWITCH:
			NG_MKRESPONSE(rsp, msg, sizeof(unit->role_switch),
				M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			*((ng_hci_node_role_switch_ep *)(rsp->data)) = 
				unit->role_switch;
			break;

		/* Set role switch */
		case NGM_HCI_NODE_SET_ROLE_SWITCH:
			if (msg->header.arglen !=
					sizeof(ng_hci_node_role_switch_ep)) {
				error = EMSGSIZE;
				break;
			}

			unit->role_switch = 
				*((ng_hci_node_role_switch_ep *)(msg->data));
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

	/* NG_RESPOND_MSG should take care of "item" and "rsp" */
	NG_RESPOND_MSG(error, node, item, rsp);
	NG_FREE_MSG(msg);

	return (error);
} /* ng_hci_default_rcvmsg */

/*
 * Process control message from upstream hooks (ACL and SCO).
 * Handle LP_xxx messages here, give everything else to default routine.
 */

static int
ng_hci_upper_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	ng_hci_unit_p	unit = (ng_hci_unit_p) NG_NODE_PRIVATE(node);
	int		error = 0;

	switch (NGI_MSG(item)->header.typecookie) {
	case NGM_HCI_COOKIE:
		switch (NGI_MSG(item)->header.cmd) {
		case NGM_HCI_LP_CON_REQ:
			error = ng_hci_lp_con_req(unit, item, lasthook);
			break;

		case NGM_HCI_LP_DISCON_REQ: /* XXX not defined by specs */
			error = ng_hci_lp_discon_req(unit, item, lasthook);
			break;

		case NGM_HCI_LP_CON_RSP:
			error = ng_hci_lp_con_rsp(unit, item, lasthook);
			break;

		case NGM_HCI_LP_QOS_REQ:
			error = ng_hci_lp_qos_req(unit, item, lasthook);
			break;

		default:
			error = ng_hci_default_rcvmsg(node, item, lasthook);
			break;
		}
		break;

	default:
		error = ng_hci_default_rcvmsg(node, item, lasthook);
		break;
	}

	return (error);
} /* ng_hci_upper_rcvmsg */

/*
 * Process data packet from the driver hook. 
 * We expect HCI events, ACL or SCO data packets.
 */

static int
ng_hci_drv_rcvdata(hook_p hook, item_p item)
{
	ng_hci_unit_p	 unit = (ng_hci_unit_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf	*m = NULL;
	int		 error = 0;

	/* Process packet */
	m = NGI_M(item); /* item still has mbuf, just peeking */
	m->m_flags |= M_PROTO1; /* mark as incoming packet */

	NG_HCI_STAT_BYTES_RECV(unit->stat, m->m_pkthdr.len);

	/* Give copy packet to RAW hook */
	ng_hci_mtap(unit, m);

	/*
	 * XXX XXX XXX
	 * Lower layer drivers MUST NOT send mbuf chain with empty mbuf at
	 * the beginning of the chain. HCI layer WILL NOT call m_pullup() here.
	 */

	switch (*mtod(m, u_int8_t *)) {
	case NG_HCI_ACL_DATA_PKT:
		NG_HCI_STAT_ACL_RECV(unit->stat);

		if ((unit->state & NG_HCI_UNIT_READY) != NG_HCI_UNIT_READY ||
		    unit->acl == NULL || NG_HOOK_NOT_VALID(unit->acl)) {
			NG_HCI_WARN(
"%s: %s - could not forward HCI ACL data packet, state=%#x, hook=%p\n",
				__func__, NG_NODE_NAME(unit->node), 
				unit->state, unit->acl);

			NG_FREE_ITEM(item);
		} else
			NG_FWD_ITEM_HOOK(error, item, unit->acl);
		break;

	case NG_HCI_SCO_DATA_PKT:
		NG_HCI_STAT_SCO_RECV(unit->stat);

		if ((unit->state & NG_HCI_UNIT_READY) != NG_HCI_UNIT_READY ||
		    unit->sco == NULL || NG_HOOK_NOT_VALID(unit->sco)) {
			NG_HCI_INFO(
"%s: %s - could not forward HCI SCO data packet, state=%#x, hook=%p\n",
				__func__, NG_NODE_NAME(unit->node), 
				unit->state, unit->sco);

			NG_FREE_ITEM(item);
		} else
			NG_FWD_ITEM_HOOK(error, item, unit->sco);
		break;
	
	case NG_HCI_EVENT_PKT:
		NG_HCI_STAT_EVNT_RECV(unit->stat);

		/* Detach mbuf, discard item and process event */
		NGI_GET_M(item, m);
		NG_FREE_ITEM(item);

		error = ng_hci_process_event(unit, m);
		break;
		
	default:
		NG_HCI_ALERT(
"%s: %s - got unknown HCI packet type=%#x\n",
			__func__, NG_NODE_NAME(unit->node),
			*mtod(m, u_int8_t *));

		NG_FREE_ITEM(item);

		error = EINVAL;
		break;
	}

	return (error);
} /* ng_hci_drv_rcvdata */

/*
 * Process data packet from ACL upstream hook.
 * We expect valid HCI ACL data packets.
 */

static int
ng_hci_acl_rcvdata(hook_p hook, item_p item)
{
	ng_hci_unit_p		 unit = (ng_hci_unit_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf		*m = NULL;
	ng_hci_unit_con_p	 con = NULL;
	u_int16_t		 con_handle;
	int			 size, error = 0;

	NG_HCI_BUFF_ACL_SIZE(unit->buffer, size);
	/* Check packet */
	NGI_GET_M(item, m);

	if (*mtod(m, u_int8_t *) != NG_HCI_ACL_DATA_PKT) {
		NG_HCI_ALERT(
"%s: %s - invalid HCI data packet type=%#x\n",
			__func__, NG_NODE_NAME(unit->node),
			*mtod(m, u_int8_t *));

		error = EINVAL;
		goto drop;
	}
	if (m->m_pkthdr.len < sizeof(ng_hci_acldata_pkt_t) ||
	    m->m_pkthdr.len > sizeof(ng_hci_acldata_pkt_t) + size) {
		NG_HCI_ALERT(
"%s: %s - invalid HCI ACL data packet, len=%d, mtu=%d\n",
			__func__, NG_NODE_NAME(unit->node), 
			m->m_pkthdr.len, size);

		error = EMSGSIZE;
		goto drop;
	}

	NG_HCI_M_PULLUP(m, sizeof(ng_hci_acldata_pkt_t));
	if (m == NULL) {
		error = ENOBUFS;
		goto drop;
	}

	con_handle = NG_HCI_CON_HANDLE(le16toh(
			mtod(m, ng_hci_acldata_pkt_t *)->con_handle));
	size = le16toh(mtod(m, ng_hci_acldata_pkt_t *)->length);

	if (m->m_pkthdr.len != sizeof(ng_hci_acldata_pkt_t) + size) {
		NG_HCI_ALERT(
"%s: %s - invalid HCI ACL data packet size, len=%d, length=%d\n",
			__func__, NG_NODE_NAME(unit->node), 
			m->m_pkthdr.len, size);

		error = EMSGSIZE;
		goto drop;
	}

	/* Queue packet */
	con = ng_hci_con_by_handle(unit, con_handle);
	if (con == NULL) {
		NG_HCI_ERR(
"%s: %s - unexpected HCI ACL data packet. Connection does not exists, " \
"con_handle=%d\n",	__func__, NG_NODE_NAME(unit->node), con_handle);

		error = ENOENT;
		goto drop;
	}

	if (con->link_type == NG_HCI_LINK_SCO) {
		NG_HCI_ERR(
"%s: %s - unexpected HCI ACL data packet. Not ACL link, con_handle=%d, " \
"link_type=%d\n",	__func__, NG_NODE_NAME(unit->node), 
			con_handle, con->link_type);

		error = EINVAL;
		goto drop;
	}

	if (con->state != NG_HCI_CON_OPEN) {
		NG_HCI_ERR(
"%s: %s - unexpected HCI ACL data packet. Invalid connection state=%d, " \
"con_handle=%d\n",	 __func__, NG_NODE_NAME(unit->node), 
			con->state, con_handle);

		error = EHOSTDOWN;
		goto drop;
	}

	if (NG_BT_ITEMQ_FULL(&con->conq)) {
		NG_HCI_ALERT(
"%s: %s - dropping HCI ACL data packet, con_handle=%d, len=%d, queue_len=%d\n",
			 __func__, NG_NODE_NAME(unit->node), con_handle, 
			m->m_pkthdr.len, NG_BT_ITEMQ_LEN(&con->conq));

		NG_BT_ITEMQ_DROP(&con->conq);

		error = ENOBUFS;
		goto drop;
	}

	/* Queue item and schedule data transfer */
	NGI_M(item) = m;
	NG_BT_ITEMQ_ENQUEUE(&con->conq, item);
	item = NULL;
	m = NULL;

	ng_hci_send_data(unit);
drop:
	if (item != NULL)
		NG_FREE_ITEM(item);

	NG_FREE_M(m); /* NG_FREE_M() checks for m != NULL */

	return (error);
} /* ng_hci_acl_rcvdata */

/*
 * Process data packet from SCO upstream hook.
 * We expect valid HCI SCO data packets
 */

static int
ng_hci_sco_rcvdata(hook_p hook, item_p item)
{
	ng_hci_unit_p		 unit = (ng_hci_unit_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf		*m = NULL;
	ng_hci_unit_con_p	 con = NULL;
	u_int16_t		 con_handle;
	int			 size, error = 0;

	NG_HCI_BUFF_SCO_SIZE(unit->buffer, size);

	/* Check packet */
	NGI_GET_M(item, m);

	if (*mtod(m, u_int8_t *) != NG_HCI_SCO_DATA_PKT) {
		NG_HCI_ALERT(
"%s: %s - invalid HCI data packet type=%#x\n",
			__func__, NG_NODE_NAME(unit->node),
			*mtod(m, u_int8_t *));

		error = EINVAL;
		goto drop;
	}

	if (m->m_pkthdr.len < sizeof(ng_hci_scodata_pkt_t) ||
	    m->m_pkthdr.len > sizeof(ng_hci_scodata_pkt_t) + size) {
		NG_HCI_ALERT(
"%s: %s - invalid HCI SCO data packet, len=%d, mtu=%d\n",
			__func__, NG_NODE_NAME(unit->node), 
			m->m_pkthdr.len, size);

		error = EMSGSIZE;
		goto drop;
	}

	NG_HCI_M_PULLUP(m, sizeof(ng_hci_scodata_pkt_t));
	if (m == NULL) {
		error = ENOBUFS;
		goto drop;
	}

	con_handle = NG_HCI_CON_HANDLE(le16toh(
			mtod(m, ng_hci_scodata_pkt_t *)->con_handle));
	size = mtod(m, ng_hci_scodata_pkt_t *)->length;

	if (m->m_pkthdr.len != sizeof(ng_hci_scodata_pkt_t) + size) {
		NG_HCI_ALERT(
"%s: %s - invalid HCI SCO data packet size, len=%d, length=%d\n",
			__func__, NG_NODE_NAME(unit->node), 
			m->m_pkthdr.len, size);

		error = EMSGSIZE;
		goto drop;
	}

	/* Queue packet */
	con = ng_hci_con_by_handle(unit, con_handle);
	if (con == NULL) {
		NG_HCI_ERR(
"%s: %s - unexpected HCI SCO data packet. Connection does not exists, " \
"con_handle=%d\n",	__func__, NG_NODE_NAME(unit->node), con_handle);

		error = ENOENT;
		goto drop;
	}

	if (con->link_type != NG_HCI_LINK_SCO) {
		NG_HCI_ERR(
"%s: %s - unexpected HCI SCO data packet. Not SCO link, con_handle=%d, " \
"link_type=%d\n",	__func__, NG_NODE_NAME(unit->node), 
			con_handle, con->link_type);

		error = EINVAL;
		goto drop;
	}

	if (con->state != NG_HCI_CON_OPEN) {
		NG_HCI_ERR(
"%s: %s - unexpected HCI SCO data packet. Invalid connection state=%d, " \
"con_handle=%d\n",	__func__, NG_NODE_NAME(unit->node), 
			con->state, con_handle);

		error = EHOSTDOWN;
		goto drop;
	}

	if (NG_BT_ITEMQ_FULL(&con->conq)) {
		NG_HCI_ALERT(
"%s: %s - dropping HCI SCO data packet, con_handle=%d, len=%d, queue_len=%d\n",
			__func__, NG_NODE_NAME(unit->node), con_handle, 
			m->m_pkthdr.len, NG_BT_ITEMQ_LEN(&con->conq));

		NG_BT_ITEMQ_DROP(&con->conq);

		error = ENOBUFS;
		goto drop;
	}

	/* Queue item and schedule data transfer */
	NGI_M(item) = m;
	NG_BT_ITEMQ_ENQUEUE(&con->conq, item);
	item = NULL;
	m = NULL;

	ng_hci_send_data(unit);
drop:
	if (item != NULL)
		NG_FREE_ITEM(item);

	NG_FREE_M(m); /* NG_FREE_M() checks for m != NULL */

	return (error);
} /* ng_hci_sco_rcvdata */

/*
 * Process data packet from uptream RAW hook.
 * We expect valid HCI command packets.
 */

static int
ng_hci_raw_rcvdata(hook_p hook, item_p item)
{
	ng_hci_unit_p	 unit = (ng_hci_unit_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf	*m = NULL;
	int		 error = 0;

	NGI_GET_M(item, m); 
	NG_FREE_ITEM(item);

	/* Check packet */
	if (*mtod(m, u_int8_t *) != NG_HCI_CMD_PKT) {
		NG_HCI_ALERT(
"%s: %s - invalid HCI command packet type=%#x\n",
			__func__, NG_NODE_NAME(unit->node),
			*mtod(m, u_int8_t *));

		error = EINVAL;
		goto drop;
	}

	if (m->m_pkthdr.len < sizeof(ng_hci_cmd_pkt_t)) {
		NG_HCI_ALERT(
"%s: %s - invalid HCI command packet len=%d\n",
			__func__, NG_NODE_NAME(unit->node), m->m_pkthdr.len);

		error = EMSGSIZE;
		goto drop;
	}

	NG_HCI_M_PULLUP(m, sizeof(ng_hci_cmd_pkt_t));
	if (m == NULL) {
		error = ENOBUFS;
		goto drop;
	}

	if (m->m_pkthdr.len != 
	    mtod(m, ng_hci_cmd_pkt_t *)->length + sizeof(ng_hci_cmd_pkt_t)) {
		NG_HCI_ALERT(
"%s: %s - invalid HCI command packet size, len=%d, length=%d\n",
			__func__, NG_NODE_NAME(unit->node), m->m_pkthdr.len,
			mtod(m, ng_hci_cmd_pkt_t *)->length);

		error = EMSGSIZE;
		goto drop;
	}

	if (mtod(m, ng_hci_cmd_pkt_t *)->opcode == 0) {
		NG_HCI_ALERT(
"%s: %s - invalid HCI command opcode\n", 
			__func__, NG_NODE_NAME(unit->node));

		error = EINVAL;
		goto drop;
	}

	if (NG_BT_MBUFQ_FULL(&unit->cmdq)) {
		NG_HCI_ALERT(
"%s: %s - dropping HCI command packet, len=%d, queue_len=%d\n",
			__func__, NG_NODE_NAME(unit->node), m->m_pkthdr.len, 
			NG_BT_MBUFQ_LEN(&unit->cmdq));

		NG_BT_MBUFQ_DROP(&unit->cmdq);

		error = ENOBUFS;
		goto drop;
	}

	/* Queue and send command */
	NG_BT_MBUFQ_ENQUEUE(&unit->cmdq, m);
	m = NULL;

	if (!(unit->state & NG_HCI_UNIT_COMMAND_PENDING))
		error = ng_hci_send_command(unit);
drop:
	NG_FREE_M(m); /* NG_FREE_M() checks for m != NULL */

	return (error);
} /* ng_hci_raw_rcvdata */


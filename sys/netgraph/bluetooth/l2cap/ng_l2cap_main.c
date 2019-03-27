/*
 * ng_l2cap_main.c
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
 * $Id: ng_l2cap_main.c,v 1.2 2003/04/28 21:44:59 max Exp $
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/include/ng_l2cap.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_var.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_cmds.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_evnt.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_llpi.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_ulpi.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_misc.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_prse.h>

/******************************************************************************
 ******************************************************************************
 **  This node implements Link Layer Control and Adaptation Protocol (L2CAP)
 ******************************************************************************
 ******************************************************************************/

/* MALLOC define */
#ifdef NG_SEPARATE_MALLOC
MALLOC_DEFINE(M_NETGRAPH_L2CAP, "netgraph_l2cap", 
	"Netgraph Bluetooth L2CAP node");
#else
#define M_NETGRAPH_L2CAP M_NETGRAPH
#endif /* NG_SEPARATE_MALLOC */

/* Netgraph node methods */
static	ng_constructor_t	ng_l2cap_constructor;
static	ng_shutdown_t		ng_l2cap_shutdown;
static	ng_newhook_t		ng_l2cap_newhook;
static	ng_connect_t		ng_l2cap_connect;
static	ng_disconnect_t		ng_l2cap_disconnect;
static	ng_rcvmsg_t		ng_l2cap_lower_rcvmsg;
static	ng_rcvmsg_t		ng_l2cap_upper_rcvmsg;
static	ng_rcvmsg_t		ng_l2cap_default_rcvmsg;
static	ng_rcvdata_t		ng_l2cap_rcvdata;

/* Netgraph node type descriptor */
static	struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_L2CAP_NODE_TYPE,
	.constructor =	ng_l2cap_constructor,
	.rcvmsg =	ng_l2cap_default_rcvmsg,
	.shutdown =	ng_l2cap_shutdown,
	.newhook =	ng_l2cap_newhook,
	.connect =	ng_l2cap_connect,
	.rcvdata =	ng_l2cap_rcvdata,
	.disconnect =	ng_l2cap_disconnect,
	.cmdlist =	ng_l2cap_cmdlist,
};
NETGRAPH_INIT(l2cap, &typestruct);
MODULE_VERSION(ng_l2cap, NG_BLUETOOTH_VERSION);
MODULE_DEPEND(ng_l2cap, ng_bluetooth, NG_BLUETOOTH_VERSION,
        NG_BLUETOOTH_VERSION, NG_BLUETOOTH_VERSION);

/*****************************************************************************
 *****************************************************************************
 **                   Netgraph methods implementation
 *****************************************************************************
 *****************************************************************************/

static void ng_l2cap_cleanup          (ng_l2cap_p);
static void ng_l2cap_destroy_channels (ng_l2cap_p);

/*
 * Create new instance of L2CAP node
 */

static int
ng_l2cap_constructor(node_p node)
{
	ng_l2cap_p	l2cap = NULL;

	/* Create new L2CAP node */
	l2cap = malloc(sizeof(*l2cap), M_NETGRAPH_L2CAP, M_WAITOK | M_ZERO);

	l2cap->node = node;
	l2cap->debug = NG_L2CAP_WARN_LEVEL;
	l2cap->discon_timo = 5; /* sec */

	LIST_INIT(&l2cap->con_list);
	LIST_INIT(&l2cap->chan_list);

	NG_NODE_SET_PRIVATE(node, l2cap);
	NG_NODE_FORCE_WRITER(node);

	return (0);
} /* ng_l2cap_constructor */

/*
 * Shutdown L2CAP node
 */

static int
ng_l2cap_shutdown(node_p node)
{
	ng_l2cap_p	l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);

	/* Clean up L2CAP node. Delete all connection, channels and commands */
	l2cap->node = NULL;
	ng_l2cap_cleanup(l2cap);

	bzero(l2cap, sizeof(*l2cap));
	free(l2cap, M_NETGRAPH_L2CAP);

	return (0);
} /* ng_l2cap_shutdown */

/*
 * Give our OK for a hook to be added. HCI layer is connected to the HCI 
 * (NG_L2CAP_HOOK_HCI) hook. As per specification L2CAP layer MUST provide
 * Procol/Service Multiplexing, so the L2CAP node provides separate hooks 
 * for SDP (NG_L2CAP_HOOK_SDP), RFCOMM (NG_L2CAP_HOOK_RFCOMM) and TCP 
 * (NG_L2CAP_HOOK_TCP) protcols. Unknown PSM will be forwarded to 
 * NG_L2CAP_HOOK_ORPHAN hook. Control node/application is connected to 
 * control (NG_L2CAP_HOOK_CTL) hook. 
 */

static int
ng_l2cap_newhook(node_p node, hook_p hook, char const *name)
{
	ng_l2cap_p	 l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(node);
	hook_p		*h = NULL;

	if (strcmp(name, NG_L2CAP_HOOK_HCI) == 0)
		h = &l2cap->hci;
	else if (strcmp(name, NG_L2CAP_HOOK_L2C) == 0)
		h = &l2cap->l2c;
	else if (strcmp(name, NG_L2CAP_HOOK_CTL) == 0)
		h = &l2cap->ctl;
	else
		return (EINVAL);

	if (*h != NULL)
		return (EISCONN);

	*h = hook;

	return (0);
} /* ng_l2cap_newhook */

/*
 * Give our final OK to connect hook. Nothing to do here.
 */

static int
ng_l2cap_connect(hook_p hook)
{
	ng_l2cap_p	l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	int		error = 0;

	if (hook == l2cap->hci)
		NG_HOOK_SET_RCVMSG(hook, ng_l2cap_lower_rcvmsg);
	else
	if (hook == l2cap->l2c || hook == l2cap->ctl) {
		NG_HOOK_SET_RCVMSG(hook, ng_l2cap_upper_rcvmsg);

		/* Send delayed notification to the upper layer */
		error = ng_send_fn(l2cap->node, hook, ng_l2cap_send_hook_info,
				NULL, 0);
	} else
		error = EINVAL;

	return (error);
} /* ng_l2cap_connect */

/*
 * Disconnect the hook. For downstream hook we must notify upper layers.
 * 
 * XXX For upstream hooks this is really ugly :( Hook was disconnected and it 
 * XXX is now too late to do anything. For now we just clean up our own mess
 * XXX and remove all channels that use disconnected upstream hook. If we don't 
 * XXX do that then L2CAP node can get out of sync with upper layers.
 * XXX No notification will be sent to remote peer. 
 */

static int
ng_l2cap_disconnect(hook_p hook)
{
	ng_l2cap_p	 l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	hook_p		*h = NULL;

	if (hook == l2cap->hci) {
		ng_l2cap_cleanup(l2cap);
		h = &l2cap->hci;
	} else
	if (hook == l2cap->l2c) {
		ng_l2cap_destroy_channels(l2cap);
		h = &l2cap->l2c;
	} else
	if (hook == l2cap->ctl)
		h = &l2cap->ctl;
	else
		return (EINVAL);

	*h = NULL;

	/* Shutdown when all hooks are disconnected */
	if (NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0 &&
	    NG_NODE_IS_VALID(NG_HOOK_NODE(hook)))
		ng_rmnode_self(NG_HOOK_NODE(hook));

	return (0);
} /* ng_l2cap_disconnect */

/*
 * Process control message from lower layer
 */

static int
ng_l2cap_lower_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	ng_l2cap_p	 l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(node);
	struct ng_mesg	*msg = NGI_MSG(item); /* item still has message */
	int		 error = 0;

	switch (msg->header.typecookie) {
	case NGM_HCI_COOKIE:
		switch (msg->header.cmd) {
		/* HCI node is ready */
		case NGM_HCI_NODE_UP: {
			ng_hci_node_up_ep	*ep = NULL;

			if (msg->header.arglen != sizeof(*ep))
				error = EMSGSIZE;
			else {
				ep = (ng_hci_node_up_ep *)(msg->data);

				NG_L2CAP_INFO(
"%s: %s - HCI node is up, bdaddr: %x:%x:%x:%x:%x:%x, " \
"pkt_size=%d bytes, num_pkts=%d\n",	__func__, NG_NODE_NAME(l2cap->node),
					ep->bdaddr.b[5], ep->bdaddr.b[4],
					ep->bdaddr.b[3], ep->bdaddr.b[2],
					ep->bdaddr.b[1], ep->bdaddr.b[0],
					ep->pkt_size, ep->num_pkts);

				bcopy(&ep->bdaddr, &l2cap->bdaddr,
					sizeof(l2cap->bdaddr));
				l2cap->pkt_size = ep->pkt_size;
				l2cap->num_pkts = ep->num_pkts;

				/* Notify upper layers */
				ng_l2cap_send_hook_info(l2cap->node,
					l2cap->l2c, NULL, 0);
				ng_l2cap_send_hook_info(l2cap->node,
					l2cap->ctl, NULL, 0);
			}
			} break;

		case NGM_HCI_SYNC_CON_QUEUE: {
			ng_hci_sync_con_queue_ep	*ep = NULL;
			ng_l2cap_con_p			 con = NULL;

			if (msg->header.arglen != sizeof(*ep))
				error = EMSGSIZE;
			else {
				ep = (ng_hci_sync_con_queue_ep *)(msg->data);
				con = ng_l2cap_con_by_handle(l2cap,
							ep->con_handle);
				if (con == NULL)
					break;

				NG_L2CAP_INFO(
"%s: %s - sync HCI connection queue, con_handle=%d, pending=%d, completed=%d\n",
					 __func__, NG_NODE_NAME(l2cap->node),
					ep->con_handle, con->pending,
					ep->completed);

				con->pending -= ep->completed;
				if (con->pending < 0) {
					NG_L2CAP_WARN(
"%s: %s - pending packet counter is out of sync! " \
"con_handle=%d, pending=%d, completed=%d\n",	__func__, 
						NG_NODE_NAME(l2cap->node),
						con->con_handle, con->pending, 
						ep->completed);

					con->pending = 0;
				}

				ng_l2cap_lp_deliver(con);
			}
			} break;

		/* LP_ConnectCfm[Neg] */
		case NGM_HCI_LP_CON_CFM:
			error = ng_l2cap_lp_con_cfm(l2cap, msg);
			break;

		/* LP_ConnectInd */
		case NGM_HCI_LP_CON_IND:
			error = ng_l2cap_lp_con_ind(l2cap, msg);
			break;

		/* LP_DisconnectInd */
		case NGM_HCI_LP_DISCON_IND:
			error = ng_l2cap_lp_discon_ind(l2cap, msg);
			break;

		/* LP_QoSSetupCfm[Neg] */
		case NGM_HCI_LP_QOS_CFM:
			error = ng_l2cap_lp_qos_cfm(l2cap, msg);
			break;

		/* LP_OoSViolationInd */
		case NGM_HCI_LP_QOS_IND:
			error = ng_l2cap_lp_qos_ind(l2cap, msg);
			break;
		case NGM_HCI_LP_ENC_CHG:
			error = ng_l2cap_lp_enc_change(l2cap, msg);
			break;
		default:
			error = EINVAL;
			break;
		}
		break;

	default:
		return (ng_l2cap_default_rcvmsg(node, item, lasthook));
		/* NOT REACHED */
	}

	NG_FREE_ITEM(item);

	return (error);
} /* ng_l2cap_lower_rcvmsg */

/*
 * Process control message from upper layer
 */

static int
ng_l2cap_upper_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	ng_l2cap_p	 l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(node);
	struct ng_mesg	*msg = NGI_MSG(item); /* item still has message */
	int		 error = 0;

	switch (msg->header.typecookie) {
	case NGM_L2CAP_COOKIE:
		switch (msg->header.cmd) {
		/* L2CA_Connect */
		case NGM_L2CAP_L2CA_CON:
			error = ng_l2cap_l2ca_con_req(l2cap, msg);
			break;

		/* L2CA_ConnectRsp */
		case NGM_L2CAP_L2CA_CON_RSP:
			error = ng_l2cap_l2ca_con_rsp_req(l2cap, msg);
			break;

		/* L2CA_Config */
		case NGM_L2CAP_L2CA_CFG:
			error = ng_l2cap_l2ca_cfg_req(l2cap, msg);
			break;

		/* L2CA_ConfigRsp */
		case NGM_L2CAP_L2CA_CFG_RSP:
			error = ng_l2cap_l2ca_cfg_rsp_req(l2cap, msg);
			break;

		/* L2CA_Disconnect */
		case NGM_L2CAP_L2CA_DISCON:
			error = ng_l2cap_l2ca_discon_req(l2cap, msg);
			break;

		/* L2CA_GroupCreate */
		case NGM_L2CAP_L2CA_GRP_CREATE:
			error = ng_l2cap_l2ca_grp_create(l2cap, msg);
			break;

		/* L2CA_GroupClose */
		case NGM_L2CAP_L2CA_GRP_CLOSE:
			error = ng_l2cap_l2ca_grp_close(l2cap, msg);
			break;

		/* L2CA_GroupAddMember */
		case NGM_L2CAP_L2CA_GRP_ADD_MEMBER:
			error = ng_l2cap_l2ca_grp_add_member_req(l2cap, msg);
			break;

		/* L2CA_GroupDeleteMember */
		case NGM_L2CAP_L2CA_GRP_REM_MEMBER:
			error = ng_l2cap_l2ca_grp_rem_member(l2cap, msg);
			break;

		/* L2CA_GroupMembership */
		case NGM_L2CAP_L2CA_GRP_MEMBERSHIP:
			error = ng_l2cap_l2ca_grp_get_members(l2cap, msg);
			break;

		/* L2CA_Ping */
		case NGM_L2CAP_L2CA_PING:
			error = ng_l2cap_l2ca_ping_req(l2cap, msg);
			break;

		/* L2CA_GetInfo */
		case NGM_L2CAP_L2CA_GET_INFO:
			error = ng_l2cap_l2ca_get_info_req(l2cap, msg);
			break;

		/* L2CA_EnableCLT */
		case NGM_L2CAP_L2CA_ENABLE_CLT:
			error = ng_l2cap_l2ca_enable_clt(l2cap, msg);
			break;

		default:
			return (ng_l2cap_default_rcvmsg(node, item, lasthook));
			/* NOT REACHED */
		}
		break;

	default:
		return (ng_l2cap_default_rcvmsg(node, item, lasthook));
		/* NOT REACHED */
	}

	NG_FREE_ITEM(item);

	return (error);
} /* ng_l2cap_upper_rcvmsg */

/*
 * Default control message processing routine
 */

static int
ng_l2cap_default_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	ng_l2cap_p	 l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(node);
	struct ng_mesg	*msg = NULL, *rsp = NULL;
	int		 error = 0;

	/* Detach and process message */
	NGI_GET_MSG(item, msg);

	switch (msg->header.typecookie) {
	case NGM_GENERIC_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TEXT_STATUS:
			NG_MKRESPONSE(rsp, msg, NG_TEXTRESPONSE, M_NOWAIT);
			if (rsp == NULL)
				error = ENOMEM;
			else
				snprintf(rsp->data, NG_TEXTRESPONSE,
					"bdaddr %x:%x:%x:%x:%x:%x, " \
					"pkt_size %d\n" \
					"Hooks %s %s %s\n" \
					"Flags %#x\n",
					l2cap->bdaddr.b[5], l2cap->bdaddr.b[4],
					l2cap->bdaddr.b[3], l2cap->bdaddr.b[2],
					l2cap->bdaddr.b[1], l2cap->bdaddr.b[0],
					l2cap->pkt_size,
					(l2cap->hci != NULL)?
						NG_L2CAP_HOOK_HCI : "",
					(l2cap->l2c != NULL)?
						NG_L2CAP_HOOK_L2C : "", 
					(l2cap->ctl != NULL)?
						NG_L2CAP_HOOK_CTL : "", 
					l2cap->flags);
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	/* Messages from the upper layer or directed to the local node */
	case NGM_L2CAP_COOKIE:
		switch (msg->header.cmd) {
		/* Get node flags */
		case NGM_L2CAP_NODE_GET_FLAGS:
			NG_MKRESPONSE(rsp, msg, sizeof(ng_l2cap_node_flags_ep),
				M_NOWAIT);
			if (rsp == NULL)
				error = ENOMEM;
			else
				*((ng_l2cap_node_flags_ep *)(rsp->data)) =
					l2cap->flags;
			break;

		/* Get node debug */
		case NGM_L2CAP_NODE_GET_DEBUG:
			NG_MKRESPONSE(rsp, msg, sizeof(ng_l2cap_node_debug_ep),
				M_NOWAIT);
			if (rsp == NULL)
				error = ENOMEM;
			else
				*((ng_l2cap_node_debug_ep *)(rsp->data)) =
					l2cap->debug;
			break;

		/* Set node debug */
		case NGM_L2CAP_NODE_SET_DEBUG:
			if (msg->header.arglen !=
					sizeof(ng_l2cap_node_debug_ep))
				error = EMSGSIZE;
			else
				l2cap->debug =
					*((ng_l2cap_node_debug_ep *)(msg->data));
			break;

		/* Get connection list */
		case NGM_L2CAP_NODE_GET_CON_LIST: {
			ng_l2cap_con_p			 con = NULL;
			ng_l2cap_node_con_list_ep	*e1 = NULL;
			ng_l2cap_node_con_ep		*e2 = NULL;
			int				 n = 0;

			/* Count number of connections */
			LIST_FOREACH(con, &l2cap->con_list, next)
				n++;
			if (n > NG_L2CAP_MAX_CON_NUM)
				n = NG_L2CAP_MAX_CON_NUM;

			/* Prepare response */
			NG_MKRESPONSE(rsp, msg,
				sizeof(*e1) + n * sizeof(*e2), M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			e1 = (ng_l2cap_node_con_list_ep *)(rsp->data);
			e2 = (ng_l2cap_node_con_ep *)(e1 + 1);

			e1->num_connections = n;

			LIST_FOREACH(con, &l2cap->con_list, next) {
				e2->state = con->state;

				e2->flags = con->flags;
				if (con->tx_pkt != NULL)
					e2->flags |= NG_L2CAP_CON_TX;
				if (con->rx_pkt != NULL)
					e2->flags |= NG_L2CAP_CON_RX;

				e2->pending = con->pending;

				e2->con_handle = con->con_handle;
				bcopy(&con->remote, &e2->remote,
					sizeof(e2->remote));

				e2 ++;
				if (--n <= 0)
					break;
			}
			} break;

		/* Get channel list */
		case NGM_L2CAP_NODE_GET_CHAN_LIST: {
			ng_l2cap_chan_p			 ch = NULL;
			ng_l2cap_node_chan_list_ep	*e1 = NULL;
			ng_l2cap_node_chan_ep		*e2 = NULL;
			int				 n = 0;

			/* Count number of channels */
			LIST_FOREACH(ch, &l2cap->chan_list, next)
				n ++;
			if (n > NG_L2CAP_MAX_CHAN_NUM)
				n = NG_L2CAP_MAX_CHAN_NUM;

			/* Prepare response */
			NG_MKRESPONSE(rsp, msg,
				sizeof(ng_l2cap_node_chan_list_ep) +
				n * sizeof(ng_l2cap_node_chan_ep), M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			e1 = (ng_l2cap_node_chan_list_ep *)(rsp->data);
			e2 = (ng_l2cap_node_chan_ep *)(e1 + 1);

			e1->num_channels = n;

			LIST_FOREACH(ch, &l2cap->chan_list, next) {
				e2->state = ch->state;

				e2->scid = ch->scid;
				e2->dcid = ch->dcid;

				e2->imtu = ch->imtu;
				e2->omtu = ch->omtu;

				e2->psm = ch->psm;
				bcopy(&ch->con->remote, &e2->remote,
					sizeof(e2->remote));

				e2 ++;
				if (--n <= 0)
					break;
			}
			} break;

		case NGM_L2CAP_NODE_GET_AUTO_DISCON_TIMO:
			NG_MKRESPONSE(rsp, msg,
				sizeof(ng_l2cap_node_auto_discon_ep), M_NOWAIT);
			if (rsp == NULL)
				error = ENOMEM;
			else
				*((ng_l2cap_node_auto_discon_ep *)(rsp->data)) =
					l2cap->discon_timo;
			break;

		case NGM_L2CAP_NODE_SET_AUTO_DISCON_TIMO:
			if (msg->header.arglen !=
					sizeof(ng_l2cap_node_auto_discon_ep))
				error = EMSGSIZE;
			else
				l2cap->discon_timo =
					*((ng_l2cap_node_auto_discon_ep *)
							(msg->data));
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

	NG_RESPOND_MSG(error, node, item, rsp);
	NG_FREE_MSG(msg);

	return (error);
} /* ng_l2cap_rcvmsg */

/*
 * Process data packet from one of our hooks.
 *
 * From the HCI hook we expect to receive ACL data packets. ACL data packets 
 * gets re-assembled into one L2CAP packet (according to length) and then gets 
 * processed.
 *
 * NOTE: We expect to receive L2CAP packet header in the first fragment. 
 *       Otherwise we WILL NOT be able to get length of the L2CAP packet.
 * 
 * Signaling L2CAP packets (destination channel ID == 0x1) are processed within
 * the node. Connectionless data packets (destination channel ID == 0x2) will 
 * be forwarded to appropriate upstream hook unless it is not connected or 
 * connectionless traffic for the specified PSM was disabled.
 *
 * From the upstream hooks we expect to receive data packets. These data 
 * packets will be converted into L2CAP data packets. The length of each
 * L2CAP packet must not exceed channel's omtu (our peer's imtu). Then
 * these L2CAP packets will be converted to ACL data packets (according to 
 * HCI layer MTU) and sent to lower layer.
 *
 * No data is expected from the control hook.
 */

static int
ng_l2cap_rcvdata(hook_p hook, item_p item)
{
	ng_l2cap_p	 l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf	*m = NULL;
	int		 error = 0;

	/* Detach mbuf, discard item and process data */
	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if (hook == l2cap->hci)
		error = ng_l2cap_lp_receive(l2cap, m);
	else if (hook == l2cap->l2c)
		error = ng_l2cap_l2ca_write_req(l2cap, m);
	else {
		NG_FREE_M(m);
		error = EINVAL;
	}

	return (error);
} /* ng_l2cap_rcvdata */

/*
 * Clean all connections, channels and commands for the L2CAP node
 */

static void
ng_l2cap_cleanup(ng_l2cap_p l2cap)
{
	ng_l2cap_con_p	con = NULL;

	/* Clean up connection and channels */
	while (!LIST_EMPTY(&l2cap->con_list)) {
		con = LIST_FIRST(&l2cap->con_list);

		if (con->flags & NG_L2CAP_CON_LP_TIMO)
			ng_l2cap_lp_untimeout(con);
		else if (con->flags & NG_L2CAP_CON_AUTO_DISCON_TIMO)
			ng_l2cap_discon_untimeout(con);

		/* Connection terminated by local host */
		ng_l2cap_con_fail(con, 0x16);
	}
} /* ng_l2cap_cleanup */

/*
 * Destroy all channels that use specified upstream hook
 */

static void
ng_l2cap_destroy_channels(ng_l2cap_p l2cap)
{
	while (!LIST_EMPTY(&l2cap->chan_list))
		ng_l2cap_free_chan(LIST_FIRST(&l2cap->chan_list));
} /* ng_l2cap_destroy_channels_by_hook */


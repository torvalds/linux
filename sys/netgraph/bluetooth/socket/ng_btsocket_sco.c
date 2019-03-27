/*
 * ng_btsocket_sco.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: ng_btsocket_sco.c,v 1.2 2005/10/31 18:08:51 max Exp $
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitstring.h>
#include <sys/domain.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/filedesc.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/protosw.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/vnet.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/include/ng_l2cap.h>
#include <netgraph/bluetooth/include/ng_btsocket.h>
#include <netgraph/bluetooth/include/ng_btsocket_sco.h>

/* MALLOC define */
#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_BTSOCKET_SCO, "netgraph_btsocks_sco",
		"Netgraph Bluetooth SCO sockets");
#else
#define M_NETGRAPH_BTSOCKET_SCO M_NETGRAPH
#endif /* NG_SEPARATE_MALLOC */

/* Netgraph node methods */
static ng_constructor_t	ng_btsocket_sco_node_constructor;
static ng_rcvmsg_t	ng_btsocket_sco_node_rcvmsg;
static ng_shutdown_t	ng_btsocket_sco_node_shutdown;
static ng_newhook_t	ng_btsocket_sco_node_newhook;
static ng_connect_t	ng_btsocket_sco_node_connect;
static ng_rcvdata_t	ng_btsocket_sco_node_rcvdata;
static ng_disconnect_t	ng_btsocket_sco_node_disconnect;

static void		ng_btsocket_sco_input   (void *, int);
static void		ng_btsocket_sco_rtclean (void *, int);

/* Netgraph type descriptor */
static struct ng_type	typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_BTSOCKET_SCO_NODE_TYPE,
	.constructor =	ng_btsocket_sco_node_constructor,
	.rcvmsg =	ng_btsocket_sco_node_rcvmsg,
	.shutdown =	ng_btsocket_sco_node_shutdown,
	.newhook =	ng_btsocket_sco_node_newhook,
	.connect =	ng_btsocket_sco_node_connect,
	.rcvdata =	ng_btsocket_sco_node_rcvdata,
	.disconnect =	ng_btsocket_sco_node_disconnect,
};

/* Globals */
static u_int32_t				ng_btsocket_sco_debug_level;
static node_p					ng_btsocket_sco_node;
static struct ng_bt_itemq			ng_btsocket_sco_queue;
static struct mtx				ng_btsocket_sco_queue_mtx;
static struct task				ng_btsocket_sco_queue_task;
static struct mtx				ng_btsocket_sco_sockets_mtx;
static LIST_HEAD(, ng_btsocket_sco_pcb)		ng_btsocket_sco_sockets;
static LIST_HEAD(, ng_btsocket_sco_rtentry)	ng_btsocket_sco_rt;
static struct mtx				ng_btsocket_sco_rt_mtx;
static struct task				ng_btsocket_sco_rt_task;
static struct timeval				ng_btsocket_sco_lasttime;
static int					ng_btsocket_sco_curpps;

/* Sysctl tree */
SYSCTL_DECL(_net_bluetooth_sco_sockets);
static SYSCTL_NODE(_net_bluetooth_sco_sockets, OID_AUTO, seq, CTLFLAG_RW,
	0, "Bluetooth SEQPACKET SCO sockets family");
SYSCTL_UINT(_net_bluetooth_sco_sockets_seq, OID_AUTO, debug_level,
	CTLFLAG_RW,
	&ng_btsocket_sco_debug_level, NG_BTSOCKET_WARN_LEVEL,
	"Bluetooth SEQPACKET SCO sockets debug level");
SYSCTL_UINT(_net_bluetooth_sco_sockets_seq, OID_AUTO, queue_len,
	CTLFLAG_RD,
	&ng_btsocket_sco_queue.len, 0,
	"Bluetooth SEQPACKET SCO sockets input queue length");
SYSCTL_UINT(_net_bluetooth_sco_sockets_seq, OID_AUTO, queue_maxlen,
	CTLFLAG_RD,
	&ng_btsocket_sco_queue.maxlen, 0,
	"Bluetooth SEQPACKET SCO sockets input queue max. length");
SYSCTL_UINT(_net_bluetooth_sco_sockets_seq, OID_AUTO, queue_drops,
	CTLFLAG_RD,
	&ng_btsocket_sco_queue.drops, 0,
	"Bluetooth SEQPACKET SCO sockets input queue drops");

/* Debug */
#define NG_BTSOCKET_SCO_INFO \
	if (ng_btsocket_sco_debug_level >= NG_BTSOCKET_INFO_LEVEL && \
	    ppsratecheck(&ng_btsocket_sco_lasttime, &ng_btsocket_sco_curpps, 1)) \
		printf

#define NG_BTSOCKET_SCO_WARN \
	if (ng_btsocket_sco_debug_level >= NG_BTSOCKET_WARN_LEVEL && \
	    ppsratecheck(&ng_btsocket_sco_lasttime, &ng_btsocket_sco_curpps, 1)) \
		printf

#define NG_BTSOCKET_SCO_ERR \
	if (ng_btsocket_sco_debug_level >= NG_BTSOCKET_ERR_LEVEL && \
	    ppsratecheck(&ng_btsocket_sco_lasttime, &ng_btsocket_sco_curpps, 1)) \
		printf

#define NG_BTSOCKET_SCO_ALERT \
	if (ng_btsocket_sco_debug_level >= NG_BTSOCKET_ALERT_LEVEL && \
	    ppsratecheck(&ng_btsocket_sco_lasttime, &ng_btsocket_sco_curpps, 1)) \
		printf

/* 
 * Netgraph message processing routines
 */

static int ng_btsocket_sco_process_lp_con_cfm
	(struct ng_mesg *, ng_btsocket_sco_rtentry_p);
static int ng_btsocket_sco_process_lp_con_ind
	(struct ng_mesg *, ng_btsocket_sco_rtentry_p);
static int ng_btsocket_sco_process_lp_discon_ind
	(struct ng_mesg *, ng_btsocket_sco_rtentry_p);

/*
 * Send LP messages to the lower layer
 */

static int  ng_btsocket_sco_send_lp_con_req
	(ng_btsocket_sco_pcb_p);
static int  ng_btsocket_sco_send_lp_con_rsp
	(ng_btsocket_sco_rtentry_p, bdaddr_p, int);
static int  ng_btsocket_sco_send_lp_discon_req
	(ng_btsocket_sco_pcb_p);

static int ng_btsocket_sco_send2
	(ng_btsocket_sco_pcb_p);

/* 
 * Timeout processing routines
 */

static void ng_btsocket_sco_timeout         (ng_btsocket_sco_pcb_p);
static void ng_btsocket_sco_untimeout       (ng_btsocket_sco_pcb_p);
static void ng_btsocket_sco_process_timeout (void *);

/* 
 * Other stuff 
 */

static ng_btsocket_sco_pcb_p	ng_btsocket_sco_pcb_by_addr(bdaddr_p);
static ng_btsocket_sco_pcb_p	ng_btsocket_sco_pcb_by_handle(bdaddr_p, int);
static ng_btsocket_sco_pcb_p	ng_btsocket_sco_pcb_by_addrs(bdaddr_p, bdaddr_p);

#define ng_btsocket_sco_wakeup_input_task() \
	taskqueue_enqueue(taskqueue_swi, &ng_btsocket_sco_queue_task)

#define ng_btsocket_sco_wakeup_route_task() \
	taskqueue_enqueue(taskqueue_swi, &ng_btsocket_sco_rt_task)

/*****************************************************************************
 *****************************************************************************
 **                        Netgraph node interface
 *****************************************************************************
 *****************************************************************************/

/*
 * Netgraph node constructor. Do not allow to create node of this type.
 */

static int
ng_btsocket_sco_node_constructor(node_p node)
{
	return (EINVAL);
} /* ng_btsocket_sco_node_constructor */

/*
 * Do local shutdown processing. Let old node go and create new fresh one.
 */

static int
ng_btsocket_sco_node_shutdown(node_p node)
{
	int	error = 0;

	NG_NODE_UNREF(node);

	/* Create new node */
	error = ng_make_node_common(&typestruct, &ng_btsocket_sco_node);
	if (error != 0) {
		NG_BTSOCKET_SCO_ALERT(
"%s: Could not create Netgraph node, error=%d\n", __func__, error);

		ng_btsocket_sco_node = NULL;

		return (error);
	}

	error = ng_name_node(ng_btsocket_sco_node,
				NG_BTSOCKET_SCO_NODE_TYPE);
	if (error != 0) {
		NG_BTSOCKET_SCO_ALERT(
"%s: Could not name Netgraph node, error=%d\n", __func__, error);

		NG_NODE_UNREF(ng_btsocket_sco_node);
		ng_btsocket_sco_node = NULL;

		return (error);
	}
		
	return (0);
} /* ng_btsocket_sco_node_shutdown */

/*
 * We allow any hook to be connected to the node.
 */

static int
ng_btsocket_sco_node_newhook(node_p node, hook_p hook, char const *name)
{
	return (0);
} /* ng_btsocket_sco_node_newhook */

/* 
 * Just say "YEP, that's OK by me!"
 */

static int
ng_btsocket_sco_node_connect(hook_p hook)
{
	NG_HOOK_SET_PRIVATE(hook, NULL);
	NG_HOOK_REF(hook); /* Keep extra reference to the hook */

#if 0
	NG_HOOK_FORCE_QUEUE(NG_HOOK_PEER(hook));
	NG_HOOK_FORCE_QUEUE(hook);
#endif

	return (0);
} /* ng_btsocket_sco_node_connect */

/*
 * Hook disconnection. Schedule route cleanup task
 */

static int
ng_btsocket_sco_node_disconnect(hook_p hook)
{
	/*
	 * If hook has private information than we must have this hook in
	 * the routing table and must schedule cleaning for the routing table.
	 * Otherwise hook was connected but we never got "hook_info" message,
	 * so we have never added this hook to the routing table and it save
	 * to just delete it.
	 */

	if (NG_HOOK_PRIVATE(hook) != NULL)
		return (ng_btsocket_sco_wakeup_route_task());

	NG_HOOK_UNREF(hook); /* Remove extra reference */

	return (0);
} /* ng_btsocket_sco_node_disconnect */

/*
 * Process incoming messages 
 */

static int
ng_btsocket_sco_node_rcvmsg(node_p node, item_p item, hook_p hook)
{
	struct ng_mesg	*msg = NGI_MSG(item); /* item still has message */
	int		 error = 0;

	if (msg != NULL && msg->header.typecookie == NGM_HCI_COOKIE) {
		mtx_lock(&ng_btsocket_sco_queue_mtx);
		if (NG_BT_ITEMQ_FULL(&ng_btsocket_sco_queue)) {
			NG_BTSOCKET_SCO_ERR(
"%s: Input queue is full (msg)\n", __func__);

			NG_BT_ITEMQ_DROP(&ng_btsocket_sco_queue);
			NG_FREE_ITEM(item);
			error = ENOBUFS;
		} else {
			if (hook != NULL) {
				NG_HOOK_REF(hook);
				NGI_SET_HOOK(item, hook);
			}

			NG_BT_ITEMQ_ENQUEUE(&ng_btsocket_sco_queue, item);
			error = ng_btsocket_sco_wakeup_input_task();
		}
		mtx_unlock(&ng_btsocket_sco_queue_mtx);
	} else {
		NG_FREE_ITEM(item);
		error = EINVAL;
	}

	return (error);
} /* ng_btsocket_sco_node_rcvmsg */

/*
 * Receive data on a hook
 */

static int
ng_btsocket_sco_node_rcvdata(hook_p hook, item_p item)
{
	int	error = 0;

	mtx_lock(&ng_btsocket_sco_queue_mtx);
	if (NG_BT_ITEMQ_FULL(&ng_btsocket_sco_queue)) {
		NG_BTSOCKET_SCO_ERR(
"%s: Input queue is full (data)\n", __func__);

		NG_BT_ITEMQ_DROP(&ng_btsocket_sco_queue);
		NG_FREE_ITEM(item);
		error = ENOBUFS;
	} else {
		NG_HOOK_REF(hook);
		NGI_SET_HOOK(item, hook);

		NG_BT_ITEMQ_ENQUEUE(&ng_btsocket_sco_queue, item);
		error = ng_btsocket_sco_wakeup_input_task();
	}
	mtx_unlock(&ng_btsocket_sco_queue_mtx);

	return (error);
} /* ng_btsocket_sco_node_rcvdata */

/*
 * Process LP_ConnectCfm event from the lower layer protocol
 */

static int
ng_btsocket_sco_process_lp_con_cfm(struct ng_mesg *msg,
		ng_btsocket_sco_rtentry_p rt)
{
	ng_hci_lp_con_cfm_ep	*ep = NULL;
	ng_btsocket_sco_pcb_t	*pcb = NULL;
	int			 error = 0;

	if (msg->header.arglen != sizeof(*ep))
		return (EMSGSIZE);

	ep = (ng_hci_lp_con_cfm_ep *)(msg->data);

	mtx_lock(&ng_btsocket_sco_sockets_mtx);

	/* Look for the socket with the token */
	pcb = ng_btsocket_sco_pcb_by_addrs(&rt->src, &ep->bdaddr);
	if (pcb == NULL) {
		mtx_unlock(&ng_btsocket_sco_sockets_mtx);
		return (ENOENT);
	}

	/* pcb is locked */

	NG_BTSOCKET_SCO_INFO(
"%s: Got LP_ConnectCfm response, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, status=%d, handle=%d, state=%d\n",
		__func__,
		pcb->src.b[5], pcb->src.b[4], pcb->src.b[3],
		pcb->src.b[2], pcb->src.b[1], pcb->src.b[0],
		pcb->dst.b[5], pcb->dst.b[4], pcb->dst.b[3],
		pcb->dst.b[2], pcb->dst.b[1], pcb->dst.b[0],
		ep->status, ep->con_handle, pcb->state);

	if (pcb->state != NG_BTSOCKET_SCO_CONNECTING) {
		mtx_unlock(&pcb->pcb_mtx);
		mtx_unlock(&ng_btsocket_sco_sockets_mtx);

		return (ENOENT);
	}

	ng_btsocket_sco_untimeout(pcb);

	if (ep->status == 0) {
		/*
		 * Connection is open. Update connection handle and
		 * socket state
		 */

		pcb->con_handle = ep->con_handle; 
		pcb->state = NG_BTSOCKET_SCO_OPEN;
		soisconnected(pcb->so); 
	} else {
		/*
		 * We have failed to open connection, so disconnect the socket
		 */

		pcb->so->so_error = ECONNREFUSED; /* XXX convert status ??? */
		pcb->state = NG_BTSOCKET_SCO_CLOSED;
		soisdisconnected(pcb->so); 
	}

	mtx_unlock(&pcb->pcb_mtx);
	mtx_unlock(&ng_btsocket_sco_sockets_mtx);

	return (error);
} /* ng_btsocket_sco_process_lp_con_cfm */

/*
 * Process LP_ConnectInd indicator. Find socket that listens on address.
 * Find exact or closest match.
 */

static int
ng_btsocket_sco_process_lp_con_ind(struct ng_mesg *msg,
		ng_btsocket_sco_rtentry_p rt)
{
	ng_hci_lp_con_ind_ep	*ep = NULL;
	ng_btsocket_sco_pcb_t	*pcb = NULL, *pcb1 = NULL;
	int			 error = 0;
	u_int16_t		 status = 0;

	if (msg->header.arglen != sizeof(*ep))
		return (EMSGSIZE);

	ep = (ng_hci_lp_con_ind_ep *)(msg->data);

	NG_BTSOCKET_SCO_INFO(
"%s: Got LP_ConnectInd indicator, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x\n",
		__func__,
		rt->src.b[5], rt->src.b[4], rt->src.b[3],
		rt->src.b[2], rt->src.b[1], rt->src.b[0],
		ep->bdaddr.b[5], ep->bdaddr.b[4], ep->bdaddr.b[3],
		ep->bdaddr.b[2], ep->bdaddr.b[1], ep->bdaddr.b[0]);

	mtx_lock(&ng_btsocket_sco_sockets_mtx);

	pcb = ng_btsocket_sco_pcb_by_addr(&rt->src);
	if (pcb != NULL) {
		struct socket *so1;

		/* pcb is locked */

		CURVNET_SET(pcb->so->so_vnet);
		so1 = sonewconn(pcb->so, 0);
		CURVNET_RESTORE();

		if (so1 == NULL) {
			status = 0x0d; /* Rejected due to limited resources */
			goto respond;
		}

		/*
		 * If we got here than we have created new socket. So complete 
		 * connection. If we we listening on specific address then copy 
		 * source address from listening socket, otherwise copy source 
		 * address from hook's routing information.
		 */

		pcb1 = so2sco_pcb(so1);
		KASSERT((pcb1 != NULL),
("%s: pcb1 == NULL\n", __func__));

 		mtx_lock(&pcb1->pcb_mtx);

		if (bcmp(&pcb->src, NG_HCI_BDADDR_ANY, sizeof(pcb->src)) != 0)
			bcopy(&pcb->src, &pcb1->src, sizeof(pcb1->src));
		else
			bcopy(&rt->src, &pcb1->src, sizeof(pcb1->src));

		pcb1->flags &= ~NG_BTSOCKET_SCO_CLIENT;

		bcopy(&ep->bdaddr, &pcb1->dst, sizeof(pcb1->dst));
		pcb1->rt = rt;
	} else
		/* Nobody listens on requested BDADDR */
		status = 0x1f; /* Unspecified Error */

respond:
	error = ng_btsocket_sco_send_lp_con_rsp(rt, &ep->bdaddr, status);
	if (pcb1 != NULL) {
		if (error != 0) {
			pcb1->so->so_error = error;
			pcb1->state = NG_BTSOCKET_SCO_CLOSED;
			soisdisconnected(pcb1->so);
		} else {
			pcb1->state = NG_BTSOCKET_SCO_CONNECTING;
			soisconnecting(pcb1->so);

			ng_btsocket_sco_timeout(pcb1);
		}

		mtx_unlock(&pcb1->pcb_mtx);
	}

	if (pcb != NULL)
		mtx_unlock(&pcb->pcb_mtx);

	mtx_unlock(&ng_btsocket_sco_sockets_mtx);

	return (error);
} /* ng_btsocket_sco_process_lp_con_ind */

/*
 * Process LP_DisconnectInd indicator
 */

static int
ng_btsocket_sco_process_lp_discon_ind(struct ng_mesg *msg,
		ng_btsocket_sco_rtentry_p rt)
{
	ng_hci_lp_discon_ind_ep	*ep = NULL;
	ng_btsocket_sco_pcb_t	*pcb = NULL;

	/* Check message */
	if (msg->header.arglen != sizeof(*ep))
		return (EMSGSIZE);

	ep = (ng_hci_lp_discon_ind_ep *)(msg->data);

	mtx_lock(&ng_btsocket_sco_sockets_mtx);

	/* Look for the socket with given channel ID */
	pcb = ng_btsocket_sco_pcb_by_handle(&rt->src, ep->con_handle);
	if (pcb == NULL) {
		mtx_unlock(&ng_btsocket_sco_sockets_mtx);
		return (0);
	}

	/*
	 * Disconnect the socket. If there was any pending request we can
	 * not do anything here anyway.
	 */

	/* pcb is locked */

       	NG_BTSOCKET_SCO_INFO(
"%s: Got LP_DisconnectInd indicator, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, handle=%d, state=%d\n",
		__func__,
		pcb->src.b[5], pcb->src.b[4], pcb->src.b[3],
		pcb->src.b[2], pcb->src.b[1], pcb->src.b[0],
		pcb->dst.b[5], pcb->dst.b[4], pcb->dst.b[3],
		pcb->dst.b[2], pcb->dst.b[1], pcb->dst.b[0],
		pcb->con_handle, pcb->state);

	if (pcb->flags & NG_BTSOCKET_SCO_TIMO)
		ng_btsocket_sco_untimeout(pcb);

	pcb->state = NG_BTSOCKET_SCO_CLOSED;
	soisdisconnected(pcb->so);

	mtx_unlock(&pcb->pcb_mtx);
	mtx_unlock(&ng_btsocket_sco_sockets_mtx);

	return (0);
} /* ng_btsocket_sco_process_lp_discon_ind */

/*
 * Send LP_ConnectReq request
 */

static int
ng_btsocket_sco_send_lp_con_req(ng_btsocket_sco_pcb_p pcb)
{
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_con_req_ep	*ep = NULL;
	int			 error = 0;

	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	if (pcb->rt == NULL || 
	    pcb->rt->hook == NULL || NG_HOOK_NOT_VALID(pcb->rt->hook))
		return (ENETDOWN); 

	NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_CON_REQ,
		sizeof(*ep), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	ep = (ng_hci_lp_con_req_ep *)(msg->data);
	ep->link_type = NG_HCI_LINK_SCO;
	bcopy(&pcb->dst, &ep->bdaddr, sizeof(ep->bdaddr));

	NG_SEND_MSG_HOOK(error, ng_btsocket_sco_node, msg, pcb->rt->hook, 0);

	return (error);
} /* ng_btsocket_sco_send_lp_con_req */

/*
 * Send LP_ConnectRsp response
 */

static int
ng_btsocket_sco_send_lp_con_rsp(ng_btsocket_sco_rtentry_p rt, bdaddr_p dst, int status)
{
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_con_rsp_ep	*ep = NULL;
	int			 error = 0;

	if (rt == NULL || rt->hook == NULL || NG_HOOK_NOT_VALID(rt->hook))
		return (ENETDOWN); 

	NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_CON_RSP,
		sizeof(*ep), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	ep = (ng_hci_lp_con_rsp_ep *)(msg->data);
	ep->status = status;
	ep->link_type = NG_HCI_LINK_SCO;
	bcopy(dst, &ep->bdaddr, sizeof(ep->bdaddr));

	NG_SEND_MSG_HOOK(error, ng_btsocket_sco_node, msg, rt->hook, 0);

	return (error);
} /* ng_btsocket_sco_send_lp_con_rsp */

/*
 * Send LP_DisconReq request
 */

static int
ng_btsocket_sco_send_lp_discon_req(ng_btsocket_sco_pcb_p pcb)
{
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_discon_req_ep	*ep = NULL;
	int			 error = 0;

	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	if (pcb->rt == NULL || 
	    pcb->rt->hook == NULL || NG_HOOK_NOT_VALID(pcb->rt->hook))
		return (ENETDOWN); 

	NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_DISCON_REQ,
		sizeof(*ep), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	ep = (ng_hci_lp_discon_req_ep *)(msg->data);
	ep->con_handle = pcb->con_handle;
	ep->reason = 0x13; /* User Ended Connection */

	NG_SEND_MSG_HOOK(error, ng_btsocket_sco_node, msg, pcb->rt->hook, 0);

	return (error);
} /* ng_btsocket_sco_send_lp_discon_req */

/*****************************************************************************
 *****************************************************************************
 **                              Socket interface
 *****************************************************************************
 *****************************************************************************/

/*
 * SCO sockets data input routine
 */

static void
ng_btsocket_sco_data_input(struct mbuf *m, hook_p hook)
{
	ng_hci_scodata_pkt_t		*hdr = NULL;
	ng_btsocket_sco_pcb_t		*pcb = NULL;
	ng_btsocket_sco_rtentry_t	*rt = NULL;
	u_int16_t			 con_handle;

	if (hook == NULL) {
		NG_BTSOCKET_SCO_ALERT(
"%s: Invalid source hook for SCO data packet\n", __func__);
		goto drop;
	}

	rt = (ng_btsocket_sco_rtentry_t *) NG_HOOK_PRIVATE(hook);
	if (rt == NULL) {
		NG_BTSOCKET_SCO_ALERT(
"%s: Could not find out source bdaddr for SCO data packet\n", __func__);
		goto drop;
	}

	/* Make sure we can access header */
	if (m->m_pkthdr.len < sizeof(*hdr)) {
		NG_BTSOCKET_SCO_ERR(
"%s: SCO data packet too small, len=%d\n", __func__, m->m_pkthdr.len);
		goto drop;
	}

	if (m->m_len < sizeof(*hdr)) { 
		m = m_pullup(m, sizeof(*hdr));
		if (m == NULL)
			goto drop;
	}

	/* Strip SCO packet header and verify packet length */
	hdr = mtod(m, ng_hci_scodata_pkt_t *);
	m_adj(m, sizeof(*hdr));

	if (hdr->length != m->m_pkthdr.len) {
		NG_BTSOCKET_SCO_ERR(
"%s: Bad SCO data packet length, len=%d, length=%d\n",
			__func__, m->m_pkthdr.len, hdr->length);
		goto drop;
	}

	/*
	 * Now process packet
	 */

	con_handle = NG_HCI_CON_HANDLE(le16toh(hdr->con_handle));

	NG_BTSOCKET_SCO_INFO(
"%s: Received SCO data packet: src bdaddr=%x:%x:%x:%x:%x:%x, handle=%d, " \
"length=%d\n",	__func__,
		rt->src.b[5], rt->src.b[4], rt->src.b[3],
		rt->src.b[2], rt->src.b[1], rt->src.b[0],
		con_handle, hdr->length);

	mtx_lock(&ng_btsocket_sco_sockets_mtx);

	/* Find socket */
	pcb = ng_btsocket_sco_pcb_by_handle(&rt->src, con_handle);
	if (pcb == NULL) {
		mtx_unlock(&ng_btsocket_sco_sockets_mtx);
		goto drop;
	}

	/* pcb is locked */

	if (pcb->state != NG_BTSOCKET_SCO_OPEN) {
		NG_BTSOCKET_SCO_ERR(
"%s: No connected socket found, src bdaddr=%x:%x:%x:%x:%x:%x, state=%d\n",
			__func__,
			rt->src.b[5], rt->src.b[4], rt->src.b[3],
			rt->src.b[2], rt->src.b[1], rt->src.b[0],
			pcb->state);

		mtx_unlock(&pcb->pcb_mtx);
		mtx_unlock(&ng_btsocket_sco_sockets_mtx);
		goto drop;
	}

	/* Check if we have enough space in socket receive queue */
	if (m->m_pkthdr.len > sbspace(&pcb->so->so_rcv)) {
		NG_BTSOCKET_SCO_ERR(
"%s: Not enough space in socket receive queue. Dropping SCO data packet, " \
"src bdaddr=%x:%x:%x:%x:%x:%x, len=%d, space=%ld\n",
			__func__,
			rt->src.b[5], rt->src.b[4], rt->src.b[3],
			rt->src.b[2], rt->src.b[1], rt->src.b[0],
			m->m_pkthdr.len,
			sbspace(&pcb->so->so_rcv));

		mtx_unlock(&pcb->pcb_mtx);
		mtx_unlock(&ng_btsocket_sco_sockets_mtx);
		goto drop;
	}

	/* Append packet to the socket receive queue and wakeup */
	sbappendrecord(&pcb->so->so_rcv, m);
	m = NULL;

	sorwakeup(pcb->so);

	mtx_unlock(&pcb->pcb_mtx);
	mtx_unlock(&ng_btsocket_sco_sockets_mtx);
drop:
	NG_FREE_M(m); /* checks for m != NULL */
} /* ng_btsocket_sco_data_input */

/*
 * SCO sockets default message input routine
 */

static void
ng_btsocket_sco_default_msg_input(struct ng_mesg *msg, hook_p hook)
{
	ng_btsocket_sco_rtentry_t	*rt = NULL;

	if (hook == NULL || NG_HOOK_NOT_VALID(hook))
		return;

	rt = (ng_btsocket_sco_rtentry_t *) NG_HOOK_PRIVATE(hook);

	switch (msg->header.cmd) {
	case NGM_HCI_NODE_UP: {
		ng_hci_node_up_ep	*ep = NULL;

		if (msg->header.arglen != sizeof(*ep))
			break;

		ep = (ng_hci_node_up_ep *)(msg->data);
		if (bcmp(&ep->bdaddr, NG_HCI_BDADDR_ANY, sizeof(bdaddr_t)) == 0)
			break;

		if (rt == NULL) {
			rt = malloc(sizeof(*rt),
				M_NETGRAPH_BTSOCKET_SCO, M_NOWAIT|M_ZERO);
			if (rt == NULL)
				break;

			NG_HOOK_SET_PRIVATE(hook, rt);

			mtx_lock(&ng_btsocket_sco_rt_mtx);

			LIST_INSERT_HEAD(&ng_btsocket_sco_rt, rt, next);
		} else
			mtx_lock(&ng_btsocket_sco_rt_mtx);

		bcopy(&ep->bdaddr, &rt->src, sizeof(rt->src));
		rt->pkt_size = (ep->pkt_size == 0)? 60 : ep->pkt_size;
		rt->num_pkts = ep->num_pkts;
		rt->hook = hook;

		mtx_unlock(&ng_btsocket_sco_rt_mtx);

		NG_BTSOCKET_SCO_INFO(
"%s: Updating hook \"%s\", src bdaddr=%x:%x:%x:%x:%x:%x, pkt_size=%d, " \
"num_pkts=%d\n",	__func__, NG_HOOK_NAME(hook), 
			rt->src.b[5], rt->src.b[4], rt->src.b[3], 
			rt->src.b[2], rt->src.b[1], rt->src.b[0],
			rt->pkt_size, rt->num_pkts);
		} break;

	case NGM_HCI_SYNC_CON_QUEUE: {
		ng_hci_sync_con_queue_ep	*ep = NULL;
		ng_btsocket_sco_pcb_t		*pcb = NULL;

		if (rt == NULL || msg->header.arglen != sizeof(*ep))
			break;

		ep = (ng_hci_sync_con_queue_ep *)(msg->data);

		rt->pending -= ep->completed;
		if (rt->pending < 0) {
			NG_BTSOCKET_SCO_WARN(
"%s: Pending packet counter is out of sync! bdaddr=%x:%x:%x:%x:%x:%x, " \
"handle=%d, pending=%d, completed=%d\n",
				__func__,
				rt->src.b[5], rt->src.b[4], rt->src.b[3],
				rt->src.b[2], rt->src.b[1], rt->src.b[0],
				ep->con_handle, rt->pending,
				ep->completed);

			rt->pending = 0;
		}

		mtx_lock(&ng_btsocket_sco_sockets_mtx);

		/* Find socket */
		pcb = ng_btsocket_sco_pcb_by_handle(&rt->src, ep->con_handle);
		if (pcb == NULL) {
			mtx_unlock(&ng_btsocket_sco_sockets_mtx);
			break;
		}

		/* pcb is locked */

		/* Check state */
		if (pcb->state == NG_BTSOCKET_SCO_OPEN) {
			/* Remove timeout */
			ng_btsocket_sco_untimeout(pcb);
			
			/* Drop completed packets from the send queue */
			for (; ep->completed > 0; ep->completed --)
				sbdroprecord(&pcb->so->so_snd);

			/* Send more if we have any */
			if (sbavail(&pcb->so->so_snd) > 0)
				if (ng_btsocket_sco_send2(pcb) == 0)
					ng_btsocket_sco_timeout(pcb);

			/* Wake up writers */
			sowwakeup(pcb->so);
		}

		mtx_unlock(&pcb->pcb_mtx);
		mtx_unlock(&ng_btsocket_sco_sockets_mtx);
	} break;

	default:
		NG_BTSOCKET_SCO_WARN(
"%s: Unknown message, cmd=%d\n", __func__, msg->header.cmd);
		break;
	}

	NG_FREE_MSG(msg); /* Checks for msg != NULL */
} /* ng_btsocket_sco_default_msg_input */

/*
 * SCO sockets LP message input routine
 */

static void
ng_btsocket_sco_lp_msg_input(struct ng_mesg *msg, hook_p hook)
{
	ng_btsocket_sco_rtentry_p	 rt = NULL;

	if (hook == NULL) {
		NG_BTSOCKET_SCO_ALERT(
"%s: Invalid source hook for LP message\n", __func__);
		goto drop;
	}

	rt = (ng_btsocket_sco_rtentry_p) NG_HOOK_PRIVATE(hook);
	if (rt == NULL) {
		NG_BTSOCKET_SCO_ALERT(
"%s: Could not find out source bdaddr for LP message\n", __func__);
		goto drop;
	}

	switch (msg->header.cmd) {
	case NGM_HCI_LP_CON_CFM: /* Connection Confirmation Event */
		ng_btsocket_sco_process_lp_con_cfm(msg, rt);
		break;

	case NGM_HCI_LP_CON_IND: /* Connection Indication Event */
		ng_btsocket_sco_process_lp_con_ind(msg, rt);
		break;

	case NGM_HCI_LP_DISCON_IND: /* Disconnection Indication Event */
		ng_btsocket_sco_process_lp_discon_ind(msg, rt);
		break;

	/* XXX FIXME add other LP messages */

	default:
		NG_BTSOCKET_SCO_WARN(
"%s: Unknown LP message, cmd=%d\n", __func__, msg->header.cmd);
		break;
	}
drop:
	NG_FREE_MSG(msg);
} /* ng_btsocket_sco_lp_msg_input */

/*
 * SCO sockets input routine
 */

static void
ng_btsocket_sco_input(void *context, int pending)
{
	item_p	item = NULL;
	hook_p	hook = NULL;

	for (;;) {
		mtx_lock(&ng_btsocket_sco_queue_mtx);
		NG_BT_ITEMQ_DEQUEUE(&ng_btsocket_sco_queue, item);
		mtx_unlock(&ng_btsocket_sco_queue_mtx);

		if (item == NULL)
			break;

		NGI_GET_HOOK(item, hook);
		if (hook != NULL && NG_HOOK_NOT_VALID(hook))
			goto drop;

		switch(item->el_flags & NGQF_TYPE) {
		case NGQF_DATA: {
			struct mbuf     *m = NULL;

			NGI_GET_M(item, m);
			ng_btsocket_sco_data_input(m, hook);
			} break;

		case NGQF_MESG: {
			struct ng_mesg  *msg = NULL;

			NGI_GET_MSG(item, msg);

			switch (msg->header.cmd) {
			case NGM_HCI_LP_CON_CFM:
			case NGM_HCI_LP_CON_IND:
			case NGM_HCI_LP_DISCON_IND:
			/* XXX FIXME add other LP messages */
				ng_btsocket_sco_lp_msg_input(msg, hook);
				break;

			default:
				ng_btsocket_sco_default_msg_input(msg, hook);
				break;
			}
			} break;

		default:
			KASSERT(0,
("%s: invalid item type=%ld\n", __func__, (item->el_flags & NGQF_TYPE)));
			break;
		}
drop:
		if (hook != NULL)
			NG_HOOK_UNREF(hook);

		NG_FREE_ITEM(item);
	}
} /* ng_btsocket_sco_input */

/*
 * Route cleanup task. Gets scheduled when hook is disconnected. Here we 
 * will find all sockets that use "invalid" hook and disconnect them.
 */

static void
ng_btsocket_sco_rtclean(void *context, int pending)
{
	ng_btsocket_sco_pcb_p		pcb = NULL, pcb_next = NULL;
	ng_btsocket_sco_rtentry_p	rt = NULL;

	/*
	 * First disconnect all sockets that use "invalid" hook
	 */

	mtx_lock(&ng_btsocket_sco_sockets_mtx);

	for(pcb = LIST_FIRST(&ng_btsocket_sco_sockets); pcb != NULL; ) {
		mtx_lock(&pcb->pcb_mtx);
		pcb_next = LIST_NEXT(pcb, next);

		if (pcb->rt != NULL &&
		    pcb->rt->hook != NULL && NG_HOOK_NOT_VALID(pcb->rt->hook)) {
			if (pcb->flags & NG_BTSOCKET_SCO_TIMO)
				ng_btsocket_sco_untimeout(pcb);

			pcb->rt = NULL;
			pcb->so->so_error = ENETDOWN;
			pcb->state = NG_BTSOCKET_SCO_CLOSED;
			soisdisconnected(pcb->so);
		}

		mtx_unlock(&pcb->pcb_mtx);
		pcb = pcb_next;
	}

	mtx_unlock(&ng_btsocket_sco_sockets_mtx);

	/*
	 * Now cleanup routing table
	 */

	mtx_lock(&ng_btsocket_sco_rt_mtx);

	for (rt = LIST_FIRST(&ng_btsocket_sco_rt); rt != NULL; ) {
		ng_btsocket_sco_rtentry_p	rt_next = LIST_NEXT(rt, next);

		if (rt->hook != NULL && NG_HOOK_NOT_VALID(rt->hook)) {
			LIST_REMOVE(rt, next);

			NG_HOOK_SET_PRIVATE(rt->hook, NULL);
			NG_HOOK_UNREF(rt->hook); /* Remove extra reference */

			bzero(rt, sizeof(*rt));
			free(rt, M_NETGRAPH_BTSOCKET_SCO);
		}

		rt = rt_next;
	}

	mtx_unlock(&ng_btsocket_sco_rt_mtx);
} /* ng_btsocket_sco_rtclean */

/*
 * Initialize everything
 */

void
ng_btsocket_sco_init(void)
{
	int	error = 0;

	/* Skip initialization of globals for non-default instances. */
	if (!IS_DEFAULT_VNET(curvnet))
		return;

	ng_btsocket_sco_node = NULL;
	ng_btsocket_sco_debug_level = NG_BTSOCKET_WARN_LEVEL;

	/* Register Netgraph node type */
	error = ng_newtype(&typestruct);
	if (error != 0) {
		NG_BTSOCKET_SCO_ALERT(
"%s: Could not register Netgraph node type, error=%d\n", __func__, error);

                return;
	}

	/* Create Netgrapg node */
	error = ng_make_node_common(&typestruct, &ng_btsocket_sco_node);
	if (error != 0) {
		NG_BTSOCKET_SCO_ALERT(
"%s: Could not create Netgraph node, error=%d\n", __func__, error);

		ng_btsocket_sco_node = NULL;

		return;
	}

	error = ng_name_node(ng_btsocket_sco_node, NG_BTSOCKET_SCO_NODE_TYPE);
	if (error != 0) {
		NG_BTSOCKET_SCO_ALERT(
"%s: Could not name Netgraph node, error=%d\n", __func__, error);

		NG_NODE_UNREF(ng_btsocket_sco_node);
		ng_btsocket_sco_node = NULL;

		return;
	}

	/* Create input queue */
	NG_BT_ITEMQ_INIT(&ng_btsocket_sco_queue, 300);
	mtx_init(&ng_btsocket_sco_queue_mtx,
		"btsocks_sco_queue_mtx", NULL, MTX_DEF);
	TASK_INIT(&ng_btsocket_sco_queue_task, 0,
		ng_btsocket_sco_input, NULL);

	/* Create list of sockets */
	LIST_INIT(&ng_btsocket_sco_sockets);
	mtx_init(&ng_btsocket_sco_sockets_mtx,
		"btsocks_sco_sockets_mtx", NULL, MTX_DEF);

	/* Routing table */
	LIST_INIT(&ng_btsocket_sco_rt);
	mtx_init(&ng_btsocket_sco_rt_mtx,
		"btsocks_sco_rt_mtx", NULL, MTX_DEF);
	TASK_INIT(&ng_btsocket_sco_rt_task, 0,
		ng_btsocket_sco_rtclean, NULL);
} /* ng_btsocket_sco_init */

/*
 * Abort connection on socket
 */

void
ng_btsocket_sco_abort(struct socket *so)
{
	so->so_error = ECONNABORTED;

	(void) ng_btsocket_sco_disconnect(so);
} /* ng_btsocket_sco_abort */

void
ng_btsocket_sco_close(struct socket *so)
{
	(void) ng_btsocket_sco_disconnect(so);
} /* ng_btsocket_sco_close */

/*
 * Accept connection on socket. Nothing to do here, socket must be connected
 * and ready, so just return peer address and be done with it.
 */

int
ng_btsocket_sco_accept(struct socket *so, struct sockaddr **nam)
{
	if (ng_btsocket_sco_node == NULL) 
		return (EINVAL);

	return (ng_btsocket_sco_peeraddr(so, nam));
} /* ng_btsocket_sco_accept */

/*
 * Create and attach new socket
 */

int
ng_btsocket_sco_attach(struct socket *so, int proto, struct thread *td)
{
	ng_btsocket_sco_pcb_p	pcb = so2sco_pcb(so);
	int			error;

	/* Check socket and protocol */
	if (ng_btsocket_sco_node == NULL) 
		return (EPROTONOSUPPORT);
	if (so->so_type != SOCK_SEQPACKET)
		return (ESOCKTNOSUPPORT);

#if 0 /* XXX sonewconn() calls "pru_attach" with proto == 0 */
	if (proto != 0) 
		if (proto != BLUETOOTH_PROTO_SCO)
			return (EPROTONOSUPPORT);
#endif /* XXX */

	if (pcb != NULL)
		return (EISCONN);

	/* Reserve send and receive space if it is not reserved yet */
	if ((so->so_snd.sb_hiwat == 0) || (so->so_rcv.sb_hiwat == 0)) {
		error = soreserve(so, NG_BTSOCKET_SCO_SENDSPACE,
					NG_BTSOCKET_SCO_RECVSPACE);
		if (error != 0)
			return (error);
	}

	/* Allocate the PCB */
        pcb = malloc(sizeof(*pcb),
		M_NETGRAPH_BTSOCKET_SCO, M_NOWAIT | M_ZERO);
        if (pcb == NULL)
                return (ENOMEM);

	/* Link the PCB and the socket */
	so->so_pcb = (caddr_t) pcb;
	pcb->so = so;
	pcb->state = NG_BTSOCKET_SCO_CLOSED;

	callout_init(&pcb->timo, 1);

	/*
	 * Mark PCB mutex as DUPOK to prevent "duplicated lock of
	 * the same type" message. When accepting new SCO connection 
	 * ng_btsocket_sco_process_lp_con_ind() holds both PCB mutexes 
	 * for "old" (accepting) PCB and "new" (created) PCB.
	 */
		
	mtx_init(&pcb->pcb_mtx, "btsocks_sco_pcb_mtx", NULL,
		MTX_DEF|MTX_DUPOK);

	/*
	 * Add the PCB to the list
	 *
	 * XXX FIXME VERY IMPORTANT!
	 *
	 * This is totally FUBAR. We could get here in two cases:
	 *
	 * 1) When user calls socket()
	 * 2) When we need to accept new incoming connection and call
	 *    sonewconn()
	 *
	 * In the first case we must acquire ng_btsocket_sco_sockets_mtx.
	 * In the second case we hold ng_btsocket_sco_sockets_mtx already.
	 * So we now need to distinguish between these cases. From reading
	 * /sys/kern/uipc_socket2.c we can find out that sonewconn() calls
	 * pru_attach with proto == 0 and td == NULL. For now use this fact
	 * to figure out if we were called from socket() or from sonewconn().
	 */

	if (td != NULL)
		mtx_lock(&ng_btsocket_sco_sockets_mtx);
	else
		mtx_assert(&ng_btsocket_sco_sockets_mtx, MA_OWNED);

	LIST_INSERT_HEAD(&ng_btsocket_sco_sockets, pcb, next);

	if (td != NULL)
		mtx_unlock(&ng_btsocket_sco_sockets_mtx);

        return (0);
} /* ng_btsocket_sco_attach */

/*
 * Bind socket
 */

int
ng_btsocket_sco_bind(struct socket *so, struct sockaddr *nam, 
		struct thread *td)
{
	ng_btsocket_sco_pcb_t	*pcb = NULL;
	struct sockaddr_sco	*sa = (struct sockaddr_sco *) nam;

	if (ng_btsocket_sco_node == NULL) 
		return (EINVAL);

	/* Verify address */
	if (sa == NULL)
		return (EINVAL);
	if (sa->sco_family != AF_BLUETOOTH)
		return (EAFNOSUPPORT);
	if (sa->sco_len != sizeof(*sa))
		return (EINVAL);

	mtx_lock(&ng_btsocket_sco_sockets_mtx);

	/* 
	 * Check if other socket has this address already (look for exact
	 * match in bdaddr) and assign socket address if it's available.
	 */

	if (bcmp(&sa->sco_bdaddr, NG_HCI_BDADDR_ANY, sizeof(sa->sco_bdaddr)) != 0) {
 		LIST_FOREACH(pcb, &ng_btsocket_sco_sockets, next) {
			mtx_lock(&pcb->pcb_mtx);

			if (bcmp(&pcb->src, &sa->sco_bdaddr, sizeof(bdaddr_t)) == 0) {
				mtx_unlock(&pcb->pcb_mtx);
				mtx_unlock(&ng_btsocket_sco_sockets_mtx);

				return (EADDRINUSE);
			}

			mtx_unlock(&pcb->pcb_mtx);
		}

	}

	pcb = so2sco_pcb(so);
	if (pcb == NULL) {
		mtx_unlock(&ng_btsocket_sco_sockets_mtx);
		return (EINVAL);
	}

	mtx_lock(&pcb->pcb_mtx);
	bcopy(&sa->sco_bdaddr, &pcb->src, sizeof(pcb->src));
	mtx_unlock(&pcb->pcb_mtx);

	mtx_unlock(&ng_btsocket_sco_sockets_mtx);

	return (0);
} /* ng_btsocket_sco_bind */

/*
 * Connect socket
 */

int
ng_btsocket_sco_connect(struct socket *so, struct sockaddr *nam, 
		struct thread *td)
{
	ng_btsocket_sco_pcb_t		*pcb = so2sco_pcb(so);
	struct sockaddr_sco		*sa = (struct sockaddr_sco *) nam;
	ng_btsocket_sco_rtentry_t	*rt = NULL;
	int				 have_src, error = 0;

	/* Check socket */
	if (pcb == NULL)
		return (EINVAL);
	if (ng_btsocket_sco_node == NULL) 
		return (EINVAL);

	/* Verify address */
	if (sa == NULL)
		return (EINVAL);
	if (sa->sco_family != AF_BLUETOOTH)
		return (EAFNOSUPPORT);
	if (sa->sco_len != sizeof(*sa))
		return (EINVAL);
	if (bcmp(&sa->sco_bdaddr, NG_HCI_BDADDR_ANY, sizeof(bdaddr_t)) == 0)
		return (EDESTADDRREQ);

	/*
	 * Routing. Socket should be bound to some source address. The source
	 * address can be ANY. Destination address must be set and it must not
	 * be ANY. If source address is ANY then find first rtentry that has
	 * src != dst.
	 */

	mtx_lock(&ng_btsocket_sco_rt_mtx);
	mtx_lock(&pcb->pcb_mtx);

	if (pcb->state == NG_BTSOCKET_SCO_CONNECTING) {
		mtx_unlock(&pcb->pcb_mtx);
		mtx_unlock(&ng_btsocket_sco_rt_mtx);

		return (EINPROGRESS);
	}

	if (bcmp(&sa->sco_bdaddr, &pcb->src, sizeof(pcb->src)) == 0) {
		mtx_unlock(&pcb->pcb_mtx);
		mtx_unlock(&ng_btsocket_sco_rt_mtx);

		return (EINVAL);
	}

	/* Send destination address and PSM */
	bcopy(&sa->sco_bdaddr, &pcb->dst, sizeof(pcb->dst));

	pcb->rt = NULL;
	have_src = bcmp(&pcb->src, NG_HCI_BDADDR_ANY, sizeof(pcb->src));

	LIST_FOREACH(rt, &ng_btsocket_sco_rt, next) {
		if (rt->hook == NULL || NG_HOOK_NOT_VALID(rt->hook))
			continue;

		/* Match src and dst */
		if (have_src) {
			if (bcmp(&pcb->src, &rt->src, sizeof(rt->src)) == 0)
				break;
		} else {
			if (bcmp(&pcb->dst, &rt->src, sizeof(rt->src)) != 0)
				break;
		}
	}

	if (rt != NULL) {
		pcb->rt = rt;

		if (!have_src)
			bcopy(&rt->src, &pcb->src, sizeof(pcb->src));
	} else
		error = EHOSTUNREACH;

	/*
	 * Send LP_Connect request 
	 */

	if (error == 0) {	
		error = ng_btsocket_sco_send_lp_con_req(pcb);
		if (error == 0) {
			pcb->flags |= NG_BTSOCKET_SCO_CLIENT;
			pcb->state = NG_BTSOCKET_SCO_CONNECTING;
			soisconnecting(pcb->so);

			ng_btsocket_sco_timeout(pcb);
		}
	}

	mtx_unlock(&pcb->pcb_mtx);
	mtx_unlock(&ng_btsocket_sco_rt_mtx);

	return (error);
} /* ng_btsocket_sco_connect */

/*
 * Process ioctl's calls on socket
 */

int
ng_btsocket_sco_control(struct socket *so, u_long cmd, caddr_t data,
		struct ifnet *ifp, struct thread *td)
{
	return (EINVAL);
} /* ng_btsocket_sco_control */

/*
 * Process getsockopt/setsockopt system calls
 */

int
ng_btsocket_sco_ctloutput(struct socket *so, struct sockopt *sopt)
{
	ng_btsocket_sco_pcb_p	pcb = so2sco_pcb(so);
        int			error, tmp;

	if (ng_btsocket_sco_node == NULL) 
		return (EINVAL);
	if (pcb == NULL)
		return (EINVAL);

	if (sopt->sopt_level != SOL_SCO)
		return (0);

	mtx_lock(&pcb->pcb_mtx);

	switch (sopt->sopt_dir) {
	case SOPT_GET:
		if (pcb->state != NG_BTSOCKET_SCO_OPEN) {
			error = ENOTCONN;
			break;
		}
		
		switch (sopt->sopt_name) {
		case SO_SCO_MTU:
			tmp = pcb->rt->pkt_size;
			error = sooptcopyout(sopt, &tmp, sizeof(tmp));
			break;

		case SO_SCO_CONNINFO:
			tmp = pcb->con_handle;
			error = sooptcopyout(sopt, &tmp, sizeof(tmp));
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	case SOPT_SET:
		error = ENOPROTOOPT;
		break;

	default:
		error = EINVAL;
		break;
	}

	mtx_unlock(&pcb->pcb_mtx);
	
	return (error);
} /* ng_btsocket_sco_ctloutput */

/*
 * Detach and destroy socket
 */

void
ng_btsocket_sco_detach(struct socket *so)
{
	ng_btsocket_sco_pcb_p	pcb = so2sco_pcb(so);

	KASSERT(pcb != NULL, ("ng_btsocket_sco_detach: pcb == NULL"));

	if (ng_btsocket_sco_node == NULL) 
		return;

	mtx_lock(&ng_btsocket_sco_sockets_mtx);
	mtx_lock(&pcb->pcb_mtx);

	if (pcb->flags & NG_BTSOCKET_SCO_TIMO)
		ng_btsocket_sco_untimeout(pcb);

	if (pcb->state == NG_BTSOCKET_SCO_OPEN)
		ng_btsocket_sco_send_lp_discon_req(pcb);

	pcb->state = NG_BTSOCKET_SCO_CLOSED;

	LIST_REMOVE(pcb, next);

	mtx_unlock(&pcb->pcb_mtx);
	mtx_unlock(&ng_btsocket_sco_sockets_mtx);

	mtx_destroy(&pcb->pcb_mtx);
	bzero(pcb, sizeof(*pcb));
	free(pcb, M_NETGRAPH_BTSOCKET_SCO);

	soisdisconnected(so);
	so->so_pcb = NULL;
} /* ng_btsocket_sco_detach */

/*
 * Disconnect socket
 */

int
ng_btsocket_sco_disconnect(struct socket *so)
{
	ng_btsocket_sco_pcb_p	pcb = so2sco_pcb(so);

	if (pcb == NULL)
		return (EINVAL);
	if (ng_btsocket_sco_node == NULL) 
		return (EINVAL);

	mtx_lock(&pcb->pcb_mtx);

	if (pcb->state == NG_BTSOCKET_SCO_DISCONNECTING) {
		mtx_unlock(&pcb->pcb_mtx);

		return (EINPROGRESS);
	}

	if (pcb->flags & NG_BTSOCKET_SCO_TIMO)
		ng_btsocket_sco_untimeout(pcb);

	if (pcb->state == NG_BTSOCKET_SCO_OPEN) {
		ng_btsocket_sco_send_lp_discon_req(pcb);

		pcb->state = NG_BTSOCKET_SCO_DISCONNECTING;
		soisdisconnecting(so);

		ng_btsocket_sco_timeout(pcb);
	} else {
		pcb->state = NG_BTSOCKET_SCO_CLOSED;
		soisdisconnected(so);
	}

	mtx_unlock(&pcb->pcb_mtx);

	return (0);
} /* ng_btsocket_sco_disconnect */

/*
 * Listen on socket
 */

int
ng_btsocket_sco_listen(struct socket *so, int backlog, struct thread *td)
{
	ng_btsocket_sco_pcb_p	pcb = so2sco_pcb(so);
	int			error;

	if (pcb == NULL)
		return (EINVAL);
	if (ng_btsocket_sco_node == NULL)
		return (EINVAL);

	SOCK_LOCK(so);
	mtx_lock(&pcb->pcb_mtx);

	error = solisten_proto_check(so);
	if (error != 0)
		goto out;
#if 0
	if (bcmp(&pcb->src, NG_HCI_BDADDR_ANY, sizeof(bdaddr_t)) == 0) {
		error = EDESTADDRREQ;
		goto out;
	}
#endif
	solisten_proto(so, backlog);
out:
	mtx_unlock(&pcb->pcb_mtx);
	SOCK_UNLOCK(so);

	return (error);
} /* ng_btsocket_listen */

/*
 * Get peer address
 */

int
ng_btsocket_sco_peeraddr(struct socket *so, struct sockaddr **nam)
{
	ng_btsocket_sco_pcb_p	pcb = so2sco_pcb(so);
	struct sockaddr_sco	sa;

	if (pcb == NULL)
		return (EINVAL);
	if (ng_btsocket_sco_node == NULL) 
		return (EINVAL);

	mtx_lock(&pcb->pcb_mtx);
	bcopy(&pcb->dst, &sa.sco_bdaddr, sizeof(sa.sco_bdaddr));
	mtx_unlock(&pcb->pcb_mtx);

	sa.sco_len = sizeof(sa);
	sa.sco_family = AF_BLUETOOTH;

	*nam = sodupsockaddr((struct sockaddr *) &sa, M_NOWAIT);

	return ((*nam == NULL)? ENOMEM : 0);
} /* ng_btsocket_sco_peeraddr */

/*
 * Send data to socket
 */

int
ng_btsocket_sco_send(struct socket *so, int flags, struct mbuf *m,
		struct sockaddr *nam, struct mbuf *control, struct thread *td)
{
	ng_btsocket_sco_pcb_t	*pcb = so2sco_pcb(so);
	int			 error = 0;
                        
	if (ng_btsocket_sco_node == NULL) {
		error = ENETDOWN;
		goto drop;
	}

	/* Check socket and input */
	if (pcb == NULL || m == NULL || control != NULL) {
		error = EINVAL;
		goto drop;
	}
                 
	mtx_lock(&pcb->pcb_mtx);
                  
	/* Make sure socket is connected */
	if (pcb->state != NG_BTSOCKET_SCO_OPEN) {
		mtx_unlock(&pcb->pcb_mtx); 
		error = ENOTCONN;
		goto drop;
	}

	/* Check route */
	if (pcb->rt == NULL ||
	    pcb->rt->hook == NULL || NG_HOOK_NOT_VALID(pcb->rt->hook)) {
		mtx_unlock(&pcb->pcb_mtx);
		error = ENETDOWN;
		goto drop;
	}

	/* Check packet size */
	if (m->m_pkthdr.len > pcb->rt->pkt_size) {
		NG_BTSOCKET_SCO_ERR(
"%s: Packet too big, len=%d, pkt_size=%d\n",
			__func__, m->m_pkthdr.len, pcb->rt->pkt_size);

		mtx_unlock(&pcb->pcb_mtx);
		error = EMSGSIZE;
		goto drop;
	}

	/*
	 * First put packet on socket send queue. Then check if we have
	 * pending timeout. If we do not have timeout then we must send
	 * packet and schedule timeout. Otherwise do nothing and wait for
	 * NGM_HCI_SYNC_CON_QUEUE message.
	 */

	sbappendrecord(&pcb->so->so_snd, m);
	m = NULL;

	if (!(pcb->flags & NG_BTSOCKET_SCO_TIMO)) {
		error = ng_btsocket_sco_send2(pcb);
		if (error == 0)
			ng_btsocket_sco_timeout(pcb);
		else
			sbdroprecord(&pcb->so->so_snd); /* XXX */
	}

	mtx_unlock(&pcb->pcb_mtx);
drop:
	NG_FREE_M(m); /* checks for != NULL */
	NG_FREE_M(control);

	return (error);
} /* ng_btsocket_sco_send */

/*
 * Send first packet in the socket queue to the SCO layer
 */

static int
ng_btsocket_sco_send2(ng_btsocket_sco_pcb_p pcb)
{
	struct  mbuf		*m = NULL;
	ng_hci_scodata_pkt_t	*hdr = NULL;
	int			 error = 0;

	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	while (pcb->rt->pending < pcb->rt->num_pkts &&
	       sbavail(&pcb->so->so_snd) > 0) {
		/* Get a copy of the first packet on send queue */
		m = m_dup(pcb->so->so_snd.sb_mb, M_NOWAIT);
		if (m == NULL) {
			error = ENOBUFS;
			break;
		}

		/* Create SCO packet header */
		M_PREPEND(m, sizeof(*hdr), M_NOWAIT);
		if (m != NULL)
			if (m->m_len < sizeof(*hdr))
				m = m_pullup(m, sizeof(*hdr));

		if (m == NULL) {
			error = ENOBUFS;
			break;
		}

		/* Fill in the header */
		hdr = mtod(m, ng_hci_scodata_pkt_t *);
		hdr->type = NG_HCI_SCO_DATA_PKT;
		hdr->con_handle = htole16(NG_HCI_MK_CON_HANDLE(pcb->con_handle, 0, 0));
		hdr->length = m->m_pkthdr.len - sizeof(*hdr);

		/* Send packet */
		NG_SEND_DATA_ONLY(error, pcb->rt->hook, m);
		if (error != 0)
			break;

		pcb->rt->pending ++;
	}

	return ((pcb->rt->pending > 0)? 0 : error);
} /* ng_btsocket_sco_send2 */

/*
 * Get socket address
 */

int
ng_btsocket_sco_sockaddr(struct socket *so, struct sockaddr **nam)
{
	ng_btsocket_sco_pcb_p	pcb = so2sco_pcb(so);
	struct sockaddr_sco	sa;

	if (pcb == NULL)
		return (EINVAL);
	if (ng_btsocket_sco_node == NULL) 
		return (EINVAL);

	mtx_lock(&pcb->pcb_mtx);
	bcopy(&pcb->src, &sa.sco_bdaddr, sizeof(sa.sco_bdaddr));
	mtx_unlock(&pcb->pcb_mtx);

	sa.sco_len = sizeof(sa);
	sa.sco_family = AF_BLUETOOTH;

	*nam = sodupsockaddr((struct sockaddr *) &sa, M_NOWAIT);

	return ((*nam == NULL)? ENOMEM : 0);
} /* ng_btsocket_sco_sockaddr */

/*****************************************************************************
 *****************************************************************************
 **                              Misc. functions
 *****************************************************************************
 *****************************************************************************/

/*
 * Look for the socket that listens on given bdaddr.
 * Returns exact or close match (if any).
 * Caller must hold ng_btsocket_sco_sockets_mtx.
 * Returns with locked pcb.
 */

static ng_btsocket_sco_pcb_p
ng_btsocket_sco_pcb_by_addr(bdaddr_p bdaddr)
{
	ng_btsocket_sco_pcb_p	p = NULL, p1 = NULL;

	mtx_assert(&ng_btsocket_sco_sockets_mtx, MA_OWNED);

	LIST_FOREACH(p, &ng_btsocket_sco_sockets, next) {
		mtx_lock(&p->pcb_mtx);

		if (p->so == NULL || !(p->so->so_options & SO_ACCEPTCONN)) {
			mtx_unlock(&p->pcb_mtx);
			continue;
		}

		if (bcmp(&p->src, bdaddr, sizeof(p->src)) == 0)
			return (p); /* return with locked pcb */

		if (bcmp(&p->src, NG_HCI_BDADDR_ANY, sizeof(p->src)) == 0)
			p1 = p;

		mtx_unlock(&p->pcb_mtx);
	}

	if (p1 != NULL)
		mtx_lock(&p1->pcb_mtx);

	return (p1);
} /* ng_btsocket_sco_pcb_by_addr */

/*
 * Look for the socket that assigned to given source address and handle.
 * Caller must hold ng_btsocket_sco_sockets_mtx.
 * Returns with locked pcb.
 */

static ng_btsocket_sco_pcb_p
ng_btsocket_sco_pcb_by_handle(bdaddr_p src, int con_handle)
{
	ng_btsocket_sco_pcb_p	p = NULL;

	mtx_assert(&ng_btsocket_sco_sockets_mtx, MA_OWNED);

	LIST_FOREACH(p, &ng_btsocket_sco_sockets, next) {
		mtx_lock(&p->pcb_mtx);

		if (p->con_handle == con_handle &&
		    bcmp(src, &p->src, sizeof(p->src)) == 0)
			return (p); /* return with locked pcb */

		mtx_unlock(&p->pcb_mtx);
	}

	return (NULL);
} /* ng_btsocket_sco_pcb_by_handle */

/*
 * Look for the socket in CONNECTING state with given source and destination
 * addresses. Caller must hold ng_btsocket_sco_sockets_mtx.
 * Returns with locked pcb.
 */

static ng_btsocket_sco_pcb_p
ng_btsocket_sco_pcb_by_addrs(bdaddr_p src, bdaddr_p dst)
{
	ng_btsocket_sco_pcb_p	p = NULL;

	mtx_assert(&ng_btsocket_sco_sockets_mtx, MA_OWNED);

	LIST_FOREACH(p, &ng_btsocket_sco_sockets, next) {
		mtx_lock(&p->pcb_mtx);

		if (p->state == NG_BTSOCKET_SCO_CONNECTING &&
		    bcmp(src, &p->src, sizeof(p->src)) == 0 &&
		    bcmp(dst, &p->dst, sizeof(p->dst)) == 0)
			return (p); /* return with locked pcb */

		mtx_unlock(&p->pcb_mtx);
	}

	return (NULL);
} /* ng_btsocket_sco_pcb_by_addrs */

/*
 * Set timeout on socket
 */

static void
ng_btsocket_sco_timeout(ng_btsocket_sco_pcb_p pcb)
{
	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	if (!(pcb->flags & NG_BTSOCKET_SCO_TIMO)) {
		pcb->flags |= NG_BTSOCKET_SCO_TIMO;
		callout_reset(&pcb->timo, bluetooth_sco_rtx_timeout(),
					ng_btsocket_sco_process_timeout, pcb);
	} else
		KASSERT(0,
("%s: Duplicated socket timeout?!\n", __func__));
} /* ng_btsocket_sco_timeout */

/*
 * Unset timeout on socket
 */

static void
ng_btsocket_sco_untimeout(ng_btsocket_sco_pcb_p pcb)
{
	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	if (pcb->flags & NG_BTSOCKET_SCO_TIMO) {
		callout_stop(&pcb->timo);
		pcb->flags &= ~NG_BTSOCKET_SCO_TIMO;
	} else
		KASSERT(0,
("%s: No socket timeout?!\n", __func__));
} /* ng_btsocket_sco_untimeout */

/*
 * Process timeout on socket
 */

static void
ng_btsocket_sco_process_timeout(void *xpcb)
{
	ng_btsocket_sco_pcb_p	 pcb = (ng_btsocket_sco_pcb_p) xpcb;

	mtx_lock(&pcb->pcb_mtx);

	pcb->flags &= ~NG_BTSOCKET_SCO_TIMO;
	pcb->so->so_error = ETIMEDOUT;

	switch (pcb->state) {
	case NG_BTSOCKET_SCO_CONNECTING:
		/* Connect timeout - close the socket */
		pcb->state = NG_BTSOCKET_SCO_CLOSED;
		soisdisconnected(pcb->so);
		break;

	case NG_BTSOCKET_SCO_OPEN:
		/* Send timeout - did not get NGM_HCI_SYNC_CON_QUEUE */
		sbdroprecord(&pcb->so->so_snd);
		sowwakeup(pcb->so);
		/* XXX FIXME what to do with pcb->rt->pending??? */
		break;

	case NG_BTSOCKET_SCO_DISCONNECTING:
		/* Disconnect timeout - disconnect the socket anyway */
		pcb->state = NG_BTSOCKET_SCO_CLOSED;
		soisdisconnected(pcb->so);
		break;

	default:
		NG_BTSOCKET_SCO_ERR(
"%s: Invalid socket state=%d\n", __func__, pcb->state);
		break;
	}

	mtx_unlock(&pcb->pcb_mtx);
} /* ng_btsocket_sco_process_timeout */


/*
 * ng_l2cap_llpi.c
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
 * $Id: ng_l2cap_llpi.c,v 1.5 2003/09/08 19:11:45 max Exp $
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
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/include/ng_l2cap.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_var.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_cmds.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_evnt.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_llpi.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_ulpi.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_misc.h>

/******************************************************************************
 ******************************************************************************
 **                 Lower Layer Protocol (HCI) Interface module
 ******************************************************************************
 ******************************************************************************/

/*
 * Send LP_ConnectReq event to the lower layer protocol. Create new connection
 * descriptor and initialize it. Create LP_ConnectReq event and send it to the
 * lower layer, then adjust connection state and start timer. The function WILL
 * FAIL if connection to the remote unit already exists.
 */

int
ng_l2cap_lp_con_req(ng_l2cap_p l2cap, bdaddr_p bdaddr, int type)
{
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_con_req_ep	*ep = NULL;
	ng_l2cap_con_p		 con = NULL;
	int			 error = 0;

	/* Verify that we DO NOT have connection to the remote unit */
	con = ng_l2cap_con_by_addr(l2cap, bdaddr, type);
	if (con != NULL) {
		NG_L2CAP_ALERT(
"%s: %s - unexpected LP_ConnectReq event. " \
"Connection already exists, state=%d, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con->state, 
			con->con_handle);

		return (EEXIST);
	}

	/* Check if lower layer protocol is still connected */
	if (l2cap->hci == NULL || NG_HOOK_NOT_VALID(l2cap->hci)) {
		NG_L2CAP_ERR(
"%s: %s - hook \"%s\" is not connected or valid\n",
			__func__, NG_NODE_NAME(l2cap->node), NG_L2CAP_HOOK_HCI);

		return (ENOTCONN);
	}

	/* Create and intialize new connection descriptor */
	con = ng_l2cap_new_con(l2cap, bdaddr, type);
	if (con == NULL)
		return (ENOMEM);

	/* Create and send LP_ConnectReq event */
	NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_CON_REQ,
		sizeof(*ep), M_NOWAIT);
	if (msg == NULL) {
		ng_l2cap_free_con(con);

		return (ENOMEM);
	}

	ep = (ng_hci_lp_con_req_ep *) (msg->data);
	bcopy(bdaddr, &ep->bdaddr, sizeof(ep->bdaddr));
	ep->link_type = type;

	con->flags |= NG_L2CAP_CON_OUTGOING;
	con->state = NG_L2CAP_W4_LP_CON_CFM;
	ng_l2cap_lp_timeout(con);

	NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->hci, 0);
	if (error != 0) {
		if (ng_l2cap_lp_untimeout(con) == 0)
			ng_l2cap_free_con(con);

		/*
		 * Do not free connection if ng_l2cap_lp_untimeout() failed
		 * let timeout handler deal with it. Always return error to
		 * the caller.
		 */
	}
	
	return (error);
} /* ng_l2cap_lp_con_req */

/*
 * Process LP_ConnectCfm event from the lower layer protocol. It could be 
 * positive or negative. Verify remote unit address then stop the timer and 
 * process event.
 */

int
ng_l2cap_lp_con_cfm(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_hci_lp_con_cfm_ep	*ep = NULL;
	ng_l2cap_con_p		 con = NULL;
	int			 error = 0;

	/* Check message */
	if (msg->header.arglen != sizeof(*ep)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid LP_ConnectCfm[Neg] message size\n",
			__func__, NG_NODE_NAME(l2cap->node));
		error = EMSGSIZE;
		goto out;
	}

	ep = (ng_hci_lp_con_cfm_ep *) (msg->data);
	/* Check if we have requested/accepted this connection */
	con = ng_l2cap_con_by_addr(l2cap, &ep->bdaddr, ep->link_type);
	if (con == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected LP_ConnectCfm event. Connection does not exist\n",
			__func__, NG_NODE_NAME(l2cap->node));
		error = ENOENT;
		goto out;
	}

	/* Check connection state */
	if (con->state != NG_L2CAP_W4_LP_CON_CFM) {
		NG_L2CAP_ALERT(
"%s: %s - unexpected LP_ConnectCfm event. " \
"Invalid connection state, state=%d, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con->state, 
			con->con_handle);
		error = EINVAL;
		goto out;
	}

	/*
	 * Looks like it is our confirmation. It is safe now to cancel 
	 * connection timer and notify upper layer. If timeout already
	 * happened then ignore connection confirmation and let timeout
	 * handle that.
 	 */

	if ((error = ng_l2cap_lp_untimeout(con)) != 0)
		goto out;

	if (ep->status == 0) {
		con->state = NG_L2CAP_CON_OPEN;
		con->con_handle = ep->con_handle;
		ng_l2cap_lp_deliver(con);
	} else /* Negative confirmation - remove connection descriptor */
		ng_l2cap_con_fail(con, ep->status);
out:
	return (error);
} /* ng_l2cap_lp_con_cfm */

/*
 * Process LP_ConnectInd event from the lower layer protocol. This is a good 
 * place to put some extra check on remote unit address and/or class. We could
 * even forward this information to control hook (or check against internal
 * black list) and thus implement some kind of firewall. But for now be simple 
 * and create new connection descriptor, start timer and send LP_ConnectRsp 
 * event (i.e. accept connection).
 */

int
ng_l2cap_lp_con_ind(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_hci_lp_con_ind_ep	*ep = NULL;
	ng_hci_lp_con_rsp_ep	*rp = NULL;
	struct ng_mesg		*rsp = NULL;
	ng_l2cap_con_p		 con = NULL;
	int			 error = 0;

	/* Check message */
	if (msg->header.arglen != sizeof(*ep)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid LP_ConnectInd message size\n",
			__func__, NG_NODE_NAME(l2cap->node));

		return (EMSGSIZE);
	}

 	ep = (ng_hci_lp_con_ind_ep *) (msg->data);

	/* Make sure we have only one connection to the remote unit */
	con = ng_l2cap_con_by_addr(l2cap, &ep->bdaddr, ep->link_type);
	if (con != NULL) {
		NG_L2CAP_ALERT(
"%s: %s - unexpected LP_ConnectInd event. " \
"Connection already exists, state=%d, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con->state, 
			con->con_handle);

		return (EEXIST);
	}

	/* Check if lower layer protocol is still connected */
	if (l2cap->hci == NULL || NG_HOOK_NOT_VALID(l2cap->hci)) {
		NG_L2CAP_ERR(
"%s: %s - hook \"%s\" is not connected or valid",
			__func__, NG_NODE_NAME(l2cap->node), NG_L2CAP_HOOK_HCI);

		return (ENOTCONN);
	}

	/* Create and intialize new connection descriptor */
	con = ng_l2cap_new_con(l2cap, &ep->bdaddr, ep->link_type);
	if (con == NULL)
		return (ENOMEM);

	/* Create and send LP_ConnectRsp event */
	NG_MKMESSAGE(rsp, NGM_HCI_COOKIE, NGM_HCI_LP_CON_RSP,
		sizeof(*rp), M_NOWAIT);
	if (rsp == NULL) {
		ng_l2cap_free_con(con);

		return (ENOMEM);
	}

	rp = (ng_hci_lp_con_rsp_ep *)(rsp->data);
	rp->status = 0x00; /* accept connection */
	rp->link_type = NG_HCI_LINK_ACL;
	bcopy(&ep->bdaddr, &rp->bdaddr, sizeof(rp->bdaddr));

	con->state = NG_L2CAP_W4_LP_CON_CFM;
	ng_l2cap_lp_timeout(con);

	NG_SEND_MSG_HOOK(error, l2cap->node, rsp, l2cap->hci, 0);
	if (error != 0) {
		if (ng_l2cap_lp_untimeout(con) == 0)
			ng_l2cap_free_con(con);

		/*
		 * Do not free connection if ng_l2cap_lp_untimeout() failed
		 * let timeout handler deal with it. Always return error to
		 * the caller.
		 */
	}

	return (error);
} /* ng_l2cap_lp_con_ind */

/*
 * Process LP_DisconnectInd event from the lower layer protocol. We have been
 * disconnected from the remote unit. So notify the upper layer protocol.
 */

int
ng_l2cap_lp_discon_ind(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_hci_lp_discon_ind_ep	*ep = NULL;
	ng_l2cap_con_p		 con = NULL;
	int			 error = 0;

	/* Check message */
	if (msg->header.arglen != sizeof(*ep)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid LP_DisconnectInd message size\n",
			__func__, NG_NODE_NAME(l2cap->node));
		error = EMSGSIZE;
		goto out;
	}

	ep = (ng_hci_lp_discon_ind_ep *) (msg->data);

	/* Check if we have this connection */
	con = ng_l2cap_con_by_handle(l2cap, ep->con_handle);
	if (con == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected LP_DisconnectInd event. " \
"Connection does not exist, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ep->con_handle);
		error = ENOENT;
		goto out;
	}

	/* XXX Verify connection state -- do we need to check this? */
	if (con->state != NG_L2CAP_CON_OPEN) {
		NG_L2CAP_ERR(
"%s: %s - unexpected LP_DisconnectInd event. " \
"Invalid connection state, state=%d, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con->state, 
			con->con_handle);
		error = EINVAL;
		goto out;
	}

	/*
	 * Notify upper layer and remove connection
	 * Note: The connection could have auto disconnect timeout set. Try
	 * to remove it. If auto disconnect timeout happened then ignore
	 * disconnect indication and let timeout handle that.
	 */

	if (con->flags & NG_L2CAP_CON_AUTO_DISCON_TIMO)
		if ((error = ng_l2cap_discon_untimeout(con)) != 0)
			return (error);

	ng_l2cap_con_fail(con, ep->reason);
out:
	return (error);
} /* ng_l2cap_lp_discon_ind */

/*
 * Send LP_QoSSetupReq event to the lower layer protocol
 */

int
ng_l2cap_lp_qos_req(ng_l2cap_p l2cap, u_int16_t con_handle,
		ng_l2cap_flow_p flow)
{
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_qos_req_ep	*ep = NULL;
	ng_l2cap_con_p		 con = NULL;
	int			 error = 0;

	/* Verify that we have this connection */
	con = ng_l2cap_con_by_handle(l2cap, con_handle);
	if (con == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected LP_QoSSetupReq event. " \
"Connection does not exist, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con_handle);

		return (ENOENT);
	}

	/* Verify connection state */
	if (con->state != NG_L2CAP_CON_OPEN) {
		NG_L2CAP_ERR(
"%s: %s - unexpected LP_QoSSetupReq event. " \
"Invalid connection state, state=%d, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con->state,
			con->con_handle);

		return (EINVAL);
	}

	/* Check if lower layer protocol is still connected */
	if (l2cap->hci == NULL || NG_HOOK_NOT_VALID(l2cap->hci)) {
		NG_L2CAP_ERR(
"%s: %s - hook \"%s\" is not connected or valid",
			__func__, NG_NODE_NAME(l2cap->node), NG_L2CAP_HOOK_HCI);

		return (ENOTCONN);
	}

	/* Create and send LP_QoSSetupReq event */
	NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_QOS_REQ,
		sizeof(*ep), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	ep = (ng_hci_lp_qos_req_ep *) (msg->data);
	ep->con_handle = con_handle;
	ep->flags = flow->flags;
	ep->service_type = flow->service_type;
	ep->token_rate = flow->token_rate;
	ep->peak_bandwidth = flow->peak_bandwidth;
	ep->latency = flow->latency;
	ep->delay_variation = flow->delay_variation;

	NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->hci, 0);
	
	return (error);
} /* ng_l2cap_lp_con_req */

/*
 * Process LP_QoSSetupCfm from the lower layer protocol
 */

int
ng_l2cap_lp_qos_cfm(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_hci_lp_qos_cfm_ep	*ep = NULL;
	int			 error = 0;

	/* Check message */
	if (msg->header.arglen != sizeof(*ep)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid LP_QoSSetupCfm[Neg] message size\n",
			__func__, NG_NODE_NAME(l2cap->node));
		error = EMSGSIZE;
		goto out;
	}

	ep = (ng_hci_lp_qos_cfm_ep *) (msg->data);
	/* XXX FIXME do something */
out:
	return (error);
} /* ng_l2cap_lp_qos_cfm */

/*
 * Process LP_QoSViolationInd event from the lower layer protocol. Lower 
 * layer protocol has detected QoS Violation, so we MUST notify the 
 * upper layer.
 */

int
ng_l2cap_lp_qos_ind(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_hci_lp_qos_ind_ep	*ep = NULL;
	ng_l2cap_con_p		 con = NULL;
	int			 error = 0;

	/* Check message */
	if (msg->header.arglen != sizeof(*ep)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid LP_QoSViolation message size\n",
			__func__, NG_NODE_NAME(l2cap->node));
		error = EMSGSIZE;
		goto out;
	}

	ep = (ng_hci_lp_qos_ind_ep *) (msg->data);

	/* Check if we have this connection */
	con = ng_l2cap_con_by_handle(l2cap, ep->con_handle);
	if (con == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected LP_QoSViolationInd event. " \
"Connection does not exist, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ep->con_handle);
		error = ENOENT;
		goto out;
	}

	/* Verify connection state */
	if (con->state != NG_L2CAP_CON_OPEN) {
		NG_L2CAP_ERR(
"%s: %s - unexpected LP_QoSViolationInd event. " \
"Invalid connection state, state=%d, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con->state, 
			con->con_handle);
		error = EINVAL;
		goto out;
	}

	/* XXX FIXME Notify upper layer and terminate channels if required */
out:
	return (error);
} /* ng_l2cap_qos_ind */

int
ng_l2cap_lp_enc_change(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_hci_lp_enc_change_ep	*ep = NULL;
	ng_l2cap_con_p		 con = NULL;
	int			 error = 0;
	ng_l2cap_chan_p 	 ch = NULL;
	/* Check message */
	if (msg->header.arglen != sizeof(*ep)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid LP_ENCChange message size\n",
			__func__, NG_NODE_NAME(l2cap->node));
		error = EMSGSIZE;
		goto out;
	}

	ep = (ng_hci_lp_enc_change_ep *) (msg->data);

	/* Check if we have this connection */
	con = ng_l2cap_con_by_handle(l2cap, ep->con_handle);
	if (con == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected LP_Enc Change Event. " \
"Connection does not exist, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ep->con_handle);
		error = ENOENT;
		goto out;
	}

	/* Verify connection state */
	if (con->state != NG_L2CAP_CON_OPEN) {
		NG_L2CAP_ERR(
"%s: %s - unexpected ENC_CHANGE event. " \
"Invalid connection state, state=%d, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con->state, 
			con->con_handle);
		error = EINVAL;
		goto out;
	}

	con->encryption = ep->status;
	
	LIST_FOREACH(ch, &l2cap->chan_list, next){
		if((ch->con->con_handle == ep->con_handle) &&
		   (ch->con->linktype == ep->link_type))
			ng_l2cap_l2ca_encryption_change(ch, ep->status);
	}
	
out:
	return (error);
} /* ng_l2cap_enc_change */

/*
 * Prepare L2CAP packet. Prepend packet with L2CAP packet header and then 
 * segment it according to HCI MTU.
 */

int
ng_l2cap_lp_send(ng_l2cap_con_p con, u_int16_t dcid, struct mbuf *m0)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	ng_l2cap_hdr_t		*l2cap_hdr = NULL;
        ng_hci_acldata_pkt_t	*acl_hdr = NULL;
        struct mbuf		*m_last = NULL, *m = NULL;
        int			 len, flag = NG_HCI_PACKET_START;

	KASSERT((con->tx_pkt == NULL),
("%s: %s - another packet pending?!\n", __func__, NG_NODE_NAME(l2cap->node)));
	KASSERT((l2cap->pkt_size > 0),
("%s: %s - invalid l2cap->pkt_size?!\n", __func__, NG_NODE_NAME(l2cap->node)));

	/* Prepend mbuf with L2CAP header */
	m0 = ng_l2cap_prepend(m0, sizeof(*l2cap_hdr));
	if (m0 == NULL) {
		NG_L2CAP_ALERT(
"%s: %s - ng_l2cap_prepend(%zd) failed\n",
			__func__, NG_NODE_NAME(l2cap->node),
			sizeof(*l2cap_hdr));

		goto fail;
	}

	l2cap_hdr = mtod(m0, ng_l2cap_hdr_t *);
	l2cap_hdr->length = htole16(m0->m_pkthdr.len - sizeof(*l2cap_hdr));
	l2cap_hdr->dcid = htole16(dcid);

	/*
	 * Segment single L2CAP packet according to the HCI layer MTU. Convert 
	 * each segment into ACL data packet and prepend it with ACL data packet
	 * header. Link all segments together via m_nextpkt link. 
 	 *
	 * XXX BC (Broadcast flag) will always be 0 (zero).
	 */

	while (m0 != NULL) {
		/* Check length of the packet against HCI MTU */
		len = m0->m_pkthdr.len;
		if (len > l2cap->pkt_size) {
			m = m_split(m0, l2cap->pkt_size, M_NOWAIT);
			if (m == NULL) {
				NG_L2CAP_ALERT(
"%s: %s - m_split(%d) failed\n",	__func__, NG_NODE_NAME(l2cap->node),
					l2cap->pkt_size);
				goto fail;
			}

			len = l2cap->pkt_size;
		}

		/* Convert packet fragment into ACL data packet */
		m0 = ng_l2cap_prepend(m0, sizeof(*acl_hdr));
		if (m0 == NULL) {
			NG_L2CAP_ALERT(
"%s: %s - ng_l2cap_prepend(%zd) failed\n",
				__func__, NG_NODE_NAME(l2cap->node),
				sizeof(*acl_hdr));
			goto fail;
		}

		acl_hdr = mtod(m0, ng_hci_acldata_pkt_t *);
		acl_hdr->type = NG_HCI_ACL_DATA_PKT;
		acl_hdr->length = htole16(len);
		acl_hdr->con_handle = htole16(NG_HCI_MK_CON_HANDLE(
					con->con_handle, flag, 0));

		/* Add fragment to the chain */
		m0->m_nextpkt = NULL;

		if (con->tx_pkt == NULL)
			con->tx_pkt = m_last = m0;
		else {
			m_last->m_nextpkt = m0;
			m_last = m0;
		}

		NG_L2CAP_INFO(
"%s: %s - attaching ACL packet, con_handle=%d, PB=%#x, length=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con->con_handle,
			flag, len);

		m0 = m;
		m = NULL;
		flag = NG_HCI_PACKET_FRAGMENT;
	}

	return (0);
fail:
	NG_FREE_M(m0);
	NG_FREE_M(m);

	while (con->tx_pkt != NULL) {
		m = con->tx_pkt->m_nextpkt;
		m_freem(con->tx_pkt);
		con->tx_pkt = m;
	}

	return (ENOBUFS);
} /* ng_l2cap_lp_send */

/*
 * Receive ACL data packet from the HCI layer. First strip ACL packet header
 * and get connection handle, PB (Packet Boundary) flag and payload length.
 * Then find connection descriptor and verify its state. Then process ACL 
 * packet as follows.
 * 
 * 1) If we got first segment (pb == NG_HCI_PACKET_START) then extract L2CAP 
 *    header and get total length of the L2CAP packet. Then start new L2CAP 
 *    packet.
 *
 * 2) If we got other (then first :) segment (pb == NG_HCI_PACKET_FRAGMENT)
 *    then add segment to the packet.
 */

int
ng_l2cap_lp_receive(ng_l2cap_p l2cap, struct mbuf *m)
{
	ng_hci_acldata_pkt_t	*acl_hdr = NULL;
	ng_l2cap_hdr_t		*l2cap_hdr = NULL;
	ng_l2cap_con_p		 con = NULL;
	u_int16_t		 con_handle, length, pb;
	int			 error = 0;

	/* Check ACL data packet */
	if (m->m_pkthdr.len < sizeof(*acl_hdr)) {
		NG_L2CAP_ERR(
"%s: %s - invalid ACL data packet. Packet too small, length=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), m->m_pkthdr.len);
		error = EMSGSIZE;
		goto drop;
	}

	/* Strip ACL data packet header */
	NG_L2CAP_M_PULLUP(m, sizeof(*acl_hdr));
	if (m == NULL)
		return (ENOBUFS);

	acl_hdr = mtod(m, ng_hci_acldata_pkt_t *);
	m_adj(m, sizeof(*acl_hdr));

	/* Get ACL connection handle, PB flag and payload length */
	acl_hdr->con_handle = le16toh(acl_hdr->con_handle);
	con_handle = NG_HCI_CON_HANDLE(acl_hdr->con_handle);
	pb = NG_HCI_PB_FLAG(acl_hdr->con_handle);
	length = le16toh(acl_hdr->length);

	NG_L2CAP_INFO(
"%s: %s - got ACL data packet, con_handle=%d, PB=%#x, length=%d\n",
		__func__, NG_NODE_NAME(l2cap->node), con_handle, pb, length);

	/* Get connection descriptor */
	con = ng_l2cap_con_by_handle(l2cap, con_handle);
	if (con == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected ACL data packet. " \
"Connection does not exist, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con_handle);
		error = ENOENT;
		goto drop;
	}

	/* Verify connection state */
	if (con->state != NG_L2CAP_CON_OPEN) {
		NG_L2CAP_ERR(
"%s: %s - unexpected ACL data packet. Invalid connection state=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con->state);
		error = EHOSTDOWN;
		goto drop;
	}

	/* Process packet */
	if (pb == NG_HCI_PACKET_START) {
		if (con->rx_pkt != NULL) {
			NG_L2CAP_ERR(
"%s: %s - dropping incomplete L2CAP packet, got %d bytes, want %d bytes\n",
				__func__, NG_NODE_NAME(l2cap->node),
				con->rx_pkt->m_pkthdr.len, con->rx_pkt_len);
			NG_FREE_M(con->rx_pkt);
			con->rx_pkt_len = 0;
		}

		/* Get L2CAP header */
		if (m->m_pkthdr.len < sizeof(*l2cap_hdr)) {
			NG_L2CAP_ERR(
"%s: %s - invalid L2CAP packet start fragment. Packet too small, length=%d\n",
				__func__, NG_NODE_NAME(l2cap->node),
				m->m_pkthdr.len);
			error = EMSGSIZE;
			goto drop;
		}

		NG_L2CAP_M_PULLUP(m, sizeof(*l2cap_hdr));
		if (m == NULL)
			return (ENOBUFS);

		l2cap_hdr = mtod(m, ng_l2cap_hdr_t *);

		NG_L2CAP_INFO(
"%s: %s - staring new L2CAP packet, con_handle=%d, length=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), con_handle,
			le16toh(l2cap_hdr->length));

		/* Start new L2CAP packet */
		con->rx_pkt = m;
		con->rx_pkt_len = le16toh(l2cap_hdr->length)+sizeof(*l2cap_hdr);
	} else if (pb == NG_HCI_PACKET_FRAGMENT) {
		if (con->rx_pkt == NULL) {
			NG_L2CAP_ERR(
"%s: %s - unexpected ACL data packet fragment, con_handle=%d\n",
				__func__, NG_NODE_NAME(l2cap->node), 
				con->con_handle);
			goto drop;
		}

		/* Add fragment to the L2CAP packet */
		m_cat(con->rx_pkt, m);
		con->rx_pkt->m_pkthdr.len += length;
	} else {
		NG_L2CAP_ERR(
"%s: %s - invalid ACL data packet. Invalid PB flag=%#x\n",
			__func__, NG_NODE_NAME(l2cap->node), pb);
		error = EINVAL;
		goto drop;
	}

	con->rx_pkt_len -= length;
	if (con->rx_pkt_len < 0) {
		NG_L2CAP_ALERT(
"%s: %s - packet length mismatch. Got %d bytes, offset %d bytes\n",
			__func__, NG_NODE_NAME(l2cap->node), 
			con->rx_pkt->m_pkthdr.len, con->rx_pkt_len);
		NG_FREE_M(con->rx_pkt);
		con->rx_pkt_len = 0;
	} else if (con->rx_pkt_len == 0) {
		/* OK, we have got complete L2CAP packet, so process it */
		error = ng_l2cap_receive(con);
		con->rx_pkt = NULL;
		con->rx_pkt_len = 0;
	}

	return (error);

drop:
	NG_FREE_M(m);

	return (error);
} /* ng_l2cap_lp_receive */

/*
 * Send queued ACL packets to the HCI layer
 */

void
ng_l2cap_lp_deliver(ng_l2cap_con_p con)
{
	ng_l2cap_p	 l2cap = con->l2cap;
	struct mbuf	*m = NULL;
	int		 error;

	/* Check connection */
	if (con->state != NG_L2CAP_CON_OPEN)
		return;

	if (con->tx_pkt == NULL)
		ng_l2cap_con_wakeup(con);

	if (con->tx_pkt == NULL)
		return;

	/* Check if lower layer protocol is still connected */
	if (l2cap->hci == NULL || NG_HOOK_NOT_VALID(l2cap->hci)) {
		NG_L2CAP_ERR(
"%s: %s - hook \"%s\" is not connected or valid",
			__func__, NG_NODE_NAME(l2cap->node), NG_L2CAP_HOOK_HCI);

		goto drop; /* XXX what to do with "pending"? */
	}

	/* Send ACL data packets */
	while (con->pending < con->l2cap->num_pkts && con->tx_pkt != NULL) {
		m = con->tx_pkt;
		con->tx_pkt = con->tx_pkt->m_nextpkt;
		m->m_nextpkt = NULL;

		if(m->m_flags &M_PROTO2){
			ng_l2cap_lp_receive(con->l2cap, m);
			continue;
		}
		NG_L2CAP_INFO(
"%s: %s - sending ACL packet, con_handle=%d, len=%d\n", 
			__func__, NG_NODE_NAME(l2cap->node), con->con_handle, 
			m->m_pkthdr.len);

		NG_SEND_DATA_ONLY(error, l2cap->hci, m);
		if (error != 0) {
			NG_L2CAP_ERR(
"%s: %s - could not send ACL data packet, con_handle=%d, error=%d\n",
				__func__, NG_NODE_NAME(l2cap->node), 
				con->con_handle, error);

			goto drop; /* XXX what to do with "pending"? */
		}

		con->pending ++;
	}

	NG_L2CAP_INFO(
"%s: %s - %d ACL packets have been sent, con_handle=%d\n",
		__func__, NG_NODE_NAME(l2cap->node), con->pending, 
		con->con_handle);

	return;

drop:
	while (con->tx_pkt != NULL) {
		m = con->tx_pkt->m_nextpkt;
		m_freem(con->tx_pkt);
		con->tx_pkt = m;
	}
} /* ng_l2cap_lp_deliver */

/*
 * Process connection timeout. Remove connection from the list. If there
 * are any channels that wait for the connection then notify them. Free 
 * connection descriptor.
 */

void
ng_l2cap_process_lp_timeout(node_p node, hook_p hook, void *arg1, int con_handle)
{
	ng_l2cap_p	l2cap = NULL;
	ng_l2cap_con_p	con = NULL;

	if (NG_NODE_NOT_VALID(node)) {
		printf("%s: Netgraph node is not valid\n", __func__);
		return;
	}

	l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(node);
	con = ng_l2cap_con_by_handle(l2cap, con_handle);

	if (con == NULL) {
		NG_L2CAP_ALERT(
"%s: %s - could not find connection, con_handle=%d\n",
			__func__, NG_NODE_NAME(node), con_handle);
		return;
	}

	if (!(con->flags & NG_L2CAP_CON_LP_TIMO)) {
		NG_L2CAP_ALERT(
"%s: %s - no pending LP timeout, con_handle=%d, state=%d, flags=%#x\n",
			__func__, NG_NODE_NAME(node), con_handle, con->state,
			con->flags);
		return;
	}

	/*
	 * Notify channels that connection has timed out. This will remove 
	 * connection, channels and pending commands.
	 */

	con->flags &= ~NG_L2CAP_CON_LP_TIMO;
	ng_l2cap_con_fail(con, NG_L2CAP_TIMEOUT);
} /* ng_l2cap_process_lp_timeout */

/*
 * Process auto disconnect timeout and send LP_DisconReq event to the 
 * lower layer protocol
 */

void
ng_l2cap_process_discon_timeout(node_p node, hook_p hook, void *arg1, int con_handle)
{
	ng_l2cap_p		 l2cap = NULL;
	ng_l2cap_con_p		 con = NULL;
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_discon_req_ep	*ep = NULL;
	int			 error;

	if (NG_NODE_NOT_VALID(node)) {
		printf("%s: Netgraph node is not valid\n", __func__);
		return;
	}

	l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(node);
	con = ng_l2cap_con_by_handle(l2cap, con_handle);

	if (con == NULL) {
		NG_L2CAP_ALERT(
"%s: %s - could not find connection, con_handle=%d\n",
			__func__, NG_NODE_NAME(node), con_handle);
		return;
	}

	if (!(con->flags & NG_L2CAP_CON_AUTO_DISCON_TIMO)) {
		NG_L2CAP_ALERT(
"%s: %s - no pending disconnect timeout, con_handle=%d, state=%d, flags=%#x\n",
			__func__, NG_NODE_NAME(node), con_handle, con->state,
			con->flags);
		return;
	}

	con->flags &= ~NG_L2CAP_CON_AUTO_DISCON_TIMO;

	/* Check if lower layer protocol is still connected */
	if (l2cap->hci == NULL || NG_HOOK_NOT_VALID(l2cap->hci)) {
		NG_L2CAP_ERR(
"%s: %s - hook \"%s\" is not connected or valid\n",
			__func__, NG_NODE_NAME(l2cap->node), NG_L2CAP_HOOK_HCI);
		return;
	}

	/* Create and send LP_DisconReq event */
	NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_DISCON_REQ,
		sizeof(*ep), M_NOWAIT);
	if (msg == NULL)
		return;

	ep = (ng_hci_lp_discon_req_ep *) (msg->data);
	ep->con_handle = con->con_handle;
	ep->reason = 0x13; /* User Ended Connection */

	NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->hci, 0);
} /* ng_l2cap_process_discon_timeout */


/*
 * ng_hci_ulpi.c
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
 * $Id: ng_hci_ulpi.c,v 1.7 2003/09/08 18:57:51 max Exp $
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
#include <netgraph/bluetooth/hci/ng_hci_var.h>
#include <netgraph/bluetooth/hci/ng_hci_cmds.h>
#include <netgraph/bluetooth/hci/ng_hci_evnt.h>
#include <netgraph/bluetooth/hci/ng_hci_ulpi.h>
#include <netgraph/bluetooth/hci/ng_hci_misc.h>

/******************************************************************************
 ******************************************************************************
 **                 Upper Layer Protocol Interface module
 ******************************************************************************
 ******************************************************************************/

static int ng_hci_lp_acl_con_req (ng_hci_unit_p, item_p, hook_p);
static int ng_hci_lp_sco_con_req (ng_hci_unit_p, item_p, hook_p);
static int ng_hci_lp_le_con_req (ng_hci_unit_p, item_p, hook_p, int);

/*
 * Process LP_ConnectReq event from the upper layer protocol
 */

int
ng_hci_lp_con_req(ng_hci_unit_p unit, item_p item, hook_p hook)
{
	int link_type;
	
	if ((unit->state & NG_HCI_UNIT_READY) != NG_HCI_UNIT_READY) {
		NG_HCI_WARN(
"%s: %s - unit is not ready, state=%#x\n",
			__func__, NG_NODE_NAME(unit->node), unit->state);

		NG_FREE_ITEM(item);

		return (ENXIO);
	}

	if (NGI_MSG(item)->header.arglen != sizeof(ng_hci_lp_con_req_ep)) {
		NG_HCI_ALERT(
"%s: %s - invalid LP_ConnectReq message size=%d\n",
			__func__, NG_NODE_NAME(unit->node),
			NGI_MSG(item)->header.arglen);

		NG_FREE_ITEM(item);

		return (EMSGSIZE);
	}
	link_type = ((ng_hci_lp_con_req_ep *)(NGI_MSG(item)->data))->link_type;
	switch(link_type){
	case NG_HCI_LINK_ACL:
		return (ng_hci_lp_acl_con_req(unit, item, hook));
	case NG_HCI_LINK_SCO:
		if (hook != unit->sco ) {
			NG_HCI_WARN(
				"%s: %s - LP_ConnectReq for SCO connection came from wrong hook=%p\n",
				__func__, NG_NODE_NAME(unit->node), hook);
			
			NG_FREE_ITEM(item);
			
			return (EINVAL);
		}
		
		return (ng_hci_lp_sco_con_req(unit, item, hook));
	case NG_HCI_LINK_LE_PUBLIC:
	case NG_HCI_LINK_LE_RANDOM:		
		return (ng_hci_lp_le_con_req(unit, item, hook, link_type));
	default:
		panic("%s: link_type invalid.", __func__);
	}
	
	return (EINVAL);
} /* ng_hci_lp_con_req */

/*
 * Request to create new ACL connection
 */

static int
ng_hci_lp_acl_con_req(ng_hci_unit_p unit, item_p item, hook_p hook)
{
	struct acl_con_req {
		ng_hci_cmd_pkt_t	 hdr;
		ng_hci_create_con_cp	 cp;
	} __attribute__ ((packed))	*req = NULL;
	ng_hci_lp_con_req_ep		*ep = NULL;
	ng_hci_unit_con_p		 con = NULL;
	ng_hci_neighbor_t		*n = NULL;
	struct mbuf			*m = NULL;
	int				 error = 0;

	ep = (ng_hci_lp_con_req_ep *)(NGI_MSG(item)->data);

	/*
	 * Only one ACL connection can exist between each pair of units.
	 * So try to find ACL connection descriptor (in any state) that
	 * has requested remote BD_ADDR.
	 *
	 * Two cases:
	 *
	 * 1) We do not have connection to the remote unit. This is simple.
	 *    Just create new connection descriptor and send HCI command to
	 *    create new connection.
	 *
	 * 2) We do have connection descriptor. We need to check connection
	 *    state:
	 * 
	 * 2.1) NG_HCI_CON_W4_LP_CON_RSP means that we are in the middle of
	 *      accepting connection from the remote unit. This is a race
	 *      condition. We will ignore this message.
	 *
	 * 2.2) NG_HCI_CON_W4_CONN_COMPLETE means that upper layer already
	 *      requested connection or we just accepted it. In any case
	 *      all we need to do here is set appropriate notification bit
	 *      and wait.
	 *	
	 * 2.3) NG_HCI_CON_OPEN means connection is open. Just reply back
	 *      and let upper layer know that we have connection already.
	 */

	con = ng_hci_con_by_bdaddr(unit, &ep->bdaddr, NG_HCI_LINK_ACL);
	if (con != NULL) {
		switch (con->state) {
		case NG_HCI_CON_W4_LP_CON_RSP: /* XXX */
			error = EALREADY;
			break;

		case NG_HCI_CON_W4_CONN_COMPLETE:
			if (hook == unit->acl)
				con->flags |= NG_HCI_CON_NOTIFY_ACL;
			else
				con->flags |= NG_HCI_CON_NOTIFY_SCO;
			break;

		case NG_HCI_CON_OPEN: {
			struct ng_mesg		*msg = NULL;
			ng_hci_lp_con_cfm_ep	*cfm = NULL;

			if (hook != NULL && NG_HOOK_IS_VALID(hook)) {
				NGI_GET_MSG(item, msg);
				NG_FREE_MSG(msg);

				NG_MKMESSAGE(msg, NGM_HCI_COOKIE, 
					NGM_HCI_LP_CON_CFM, sizeof(*cfm), 
					M_NOWAIT);
				if (msg != NULL) {
					cfm = (ng_hci_lp_con_cfm_ep *)msg->data;
					cfm->status = 0;
					cfm->link_type = con->link_type;
					cfm->con_handle = con->con_handle;
					bcopy(&con->bdaddr, &cfm->bdaddr, 
						sizeof(cfm->bdaddr));

					/*
					 * This will forward item back to
					 * sender and set item to NULL
					 */

					_NGI_MSG(item) = msg;
					NG_FWD_ITEM_HOOK(error, item, hook);
				} else
					error = ENOMEM;
			} else
				NG_HCI_INFO(
"%s: %s - Source hook is not valid, hook=%p\n",
					__func__, NG_NODE_NAME(unit->node), 
					hook);
			} break;

		default:
			panic(
"%s: %s - Invalid connection state=%d\n",
				__func__, NG_NODE_NAME(unit->node), con->state);
			break;
		}

		goto out;
	}

	/*
	 * If we got here then we need to create new ACL connection descriptor
	 * and submit HCI command. First create new connection desriptor, set
	 * bdaddr and notification flags.
	 */

	con = ng_hci_new_con(unit, NG_HCI_LINK_ACL);
	if (con == NULL) {
		error = ENOMEM;
		goto out;
	}

	bcopy(&ep->bdaddr, &con->bdaddr, sizeof(con->bdaddr));

	/* 
	 * Create HCI command 
	 */

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		ng_hci_free_con(con);
		error = ENOBUFS;
		goto out;
	}

	m->m_pkthdr.len = m->m_len = sizeof(*req);
	req = mtod(m, struct acl_con_req *);
	req->hdr.type = NG_HCI_CMD_PKT;
	req->hdr.length = sizeof(req->cp);
	req->hdr.opcode = htole16(NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
					NG_HCI_OCF_CREATE_CON));

	bcopy(&ep->bdaddr, &req->cp.bdaddr, sizeof(req->cp.bdaddr));

	req->cp.pkt_type = (NG_HCI_PKT_DM1|NG_HCI_PKT_DH1);
	if (unit->features[0] & NG_HCI_LMP_3SLOT)
		req->cp.pkt_type |= (NG_HCI_PKT_DM3|NG_HCI_PKT_DH3);
	if (unit->features[0] & NG_HCI_LMP_5SLOT)
		req->cp.pkt_type |= (NG_HCI_PKT_DM5|NG_HCI_PKT_DH5);

	req->cp.pkt_type &= unit->packet_mask;
	if ((req->cp.pkt_type & (NG_HCI_PKT_DM1|NG_HCI_PKT_DH1|
				 NG_HCI_PKT_DM3|NG_HCI_PKT_DH3|
				 NG_HCI_PKT_DM5|NG_HCI_PKT_DH5)) == 0)
		req->cp.pkt_type = (NG_HCI_PKT_DM1|NG_HCI_PKT_DH1);

	req->cp.pkt_type = htole16(req->cp.pkt_type);

	if ((unit->features[0] & NG_HCI_LMP_SWITCH) && unit->role_switch)
		req->cp.accept_role_switch = 1;
	else
		req->cp.accept_role_switch = 0;

	/*
	 * We may speed up connect by specifying valid parameters. 
	 * So check the neighbor cache.
	 */

	n = ng_hci_get_neighbor(unit, &ep->bdaddr, NG_HCI_LINK_ACL);
	if (n == NULL) {
		req->cp.page_scan_rep_mode = 0;
		req->cp.page_scan_mode = 0;
		req->cp.clock_offset = 0;
	} else {
		req->cp.page_scan_rep_mode = n->page_scan_rep_mode;
		req->cp.page_scan_mode = n->page_scan_mode;
		req->cp.clock_offset = htole16(n->clock_offset);
	}

	/* 
	 * Adust connection state 
	 */

	if (hook == unit->acl)
		con->flags |= NG_HCI_CON_NOTIFY_ACL;
	else
		con->flags |= NG_HCI_CON_NOTIFY_SCO;

	con->state = NG_HCI_CON_W4_CONN_COMPLETE;
	ng_hci_con_timeout(con);

	/* 
	 * Queue and send HCI command 
	 */

	NG_BT_MBUFQ_ENQUEUE(&unit->cmdq, m);
	if (!(unit->state & NG_HCI_UNIT_COMMAND_PENDING))
		error = ng_hci_send_command(unit);
out:
	if (item != NULL)
		NG_FREE_ITEM(item);

	return (error);
} /* ng_hci_lp_acl_con_req */

/*
 * Request to create new SCO connection
 */

static int
ng_hci_lp_sco_con_req(ng_hci_unit_p unit, item_p item, hook_p hook)
{
	struct sco_con_req {
		ng_hci_cmd_pkt_t	 hdr;
		ng_hci_add_sco_con_cp	 cp;
	} __attribute__ ((packed))	*req = NULL;
	ng_hci_lp_con_req_ep		*ep = NULL;
	ng_hci_unit_con_p		 acl_con = NULL, sco_con = NULL;
	struct mbuf			*m = NULL;
	int				 error = 0;

	ep = (ng_hci_lp_con_req_ep *)(NGI_MSG(item)->data);

	/*
	 * SCO connection without ACL link
	 *
	 * If upper layer requests SCO connection and there is no open ACL 
	 * connection to the desired remote unit, we will reject the request.
	 */

	LIST_FOREACH(acl_con, &unit->con_list, next)
		if (acl_con->link_type == NG_HCI_LINK_ACL &&
		    acl_con->state == NG_HCI_CON_OPEN &&
		    bcmp(&acl_con->bdaddr, &ep->bdaddr, sizeof(bdaddr_t)) == 0)
			break;

	if (acl_con == NULL) {
		NG_HCI_INFO(
"%s: %s - No open ACL connection to bdaddr=%x:%x:%x:%x:%x:%x\n",
			__func__, NG_NODE_NAME(unit->node),
			ep->bdaddr.b[5], ep->bdaddr.b[4], ep->bdaddr.b[3],
			ep->bdaddr.b[2], ep->bdaddr.b[1], ep->bdaddr.b[0]);

		error = ENOENT;
		goto out;
	}

	/*
	 * Multiple SCO connections can exist between the same pair of units.
	 * We assume that multiple SCO connections have to be opened one after 
	 * another. 
	 *
	 * Try to find SCO connection descriptor that matches the following:
	 *
	 * 1) sco_con->link_type == NG_HCI_LINK_SCO
	 * 
	 * 2) sco_con->state == NG_HCI_CON_W4_LP_CON_RSP ||
	 *    sco_con->state == NG_HCI_CON_W4_CONN_COMPLETE
	 * 
	 * 3) sco_con->bdaddr == ep->bdaddr
	 *
	 * Two cases:
	 *
	 * 1) We do not have connection descriptor. This is simple. Just 
	 *    create new connection and submit Add_SCO_Connection command.
	 *
	 * 2) We do have connection descriptor. We need to check the state.
	 *
	 * 2.1) NG_HCI_CON_W4_LP_CON_RSP means we in the middle of accepting
	 *      connection from the remote unit. This is a race condition and
	 *      we will ignore the request.
	 *
	 * 2.2) NG_HCI_CON_W4_CONN_COMPLETE means upper layer already requested
	 *      connection or we just accepted it.
	 */

	LIST_FOREACH(sco_con, &unit->con_list, next)
		if (sco_con->link_type == NG_HCI_LINK_SCO &&
		    (sco_con->state == NG_HCI_CON_W4_LP_CON_RSP ||
		     sco_con->state == NG_HCI_CON_W4_CONN_COMPLETE) &&
		    bcmp(&sco_con->bdaddr, &ep->bdaddr, sizeof(bdaddr_t)) == 0)
			break;

	if (sco_con != NULL) {
		switch (sco_con->state) {
		case NG_HCI_CON_W4_LP_CON_RSP: /* XXX */
			error = EALREADY;
			break;

		case NG_HCI_CON_W4_CONN_COMPLETE:
			sco_con->flags |= NG_HCI_CON_NOTIFY_SCO;
			break;

		default:
			panic(
"%s: %s - Invalid connection state=%d\n",
				__func__, NG_NODE_NAME(unit->node),
				sco_con->state);
			break;
		}

		goto out;
	}

	/*
	 * If we got here then we need to create new SCO connection descriptor
	 * and submit HCI command.
	 */

	sco_con = ng_hci_new_con(unit, NG_HCI_LINK_SCO);
	if (sco_con == NULL) {
		error = ENOMEM;
		goto out;
	}

	bcopy(&ep->bdaddr, &sco_con->bdaddr, sizeof(sco_con->bdaddr));

	/* 
	 * Create HCI command 
	 */

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		ng_hci_free_con(sco_con);
		error = ENOBUFS;
		goto out;
	}

	m->m_pkthdr.len = m->m_len = sizeof(*req);
	req = mtod(m, struct sco_con_req *);
	req->hdr.type = NG_HCI_CMD_PKT;
	req->hdr.length = sizeof(req->cp);
	req->hdr.opcode = htole16(NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
					NG_HCI_OCF_ADD_SCO_CON));

	req->cp.con_handle = htole16(acl_con->con_handle);

	req->cp.pkt_type = NG_HCI_PKT_HV1;
	if (unit->features[1] & NG_HCI_LMP_HV2_PKT)
		req->cp.pkt_type |= NG_HCI_PKT_HV2;
	if (unit->features[1] & NG_HCI_LMP_HV3_PKT)
		req->cp.pkt_type |= NG_HCI_PKT_HV3;

	req->cp.pkt_type &= unit->packet_mask;
	if ((req->cp.pkt_type & (NG_HCI_PKT_HV1|
				 NG_HCI_PKT_HV2|
				 NG_HCI_PKT_HV3)) == 0)
		req->cp.pkt_type = NG_HCI_PKT_HV1;

	req->cp.pkt_type = htole16(req->cp.pkt_type);

	/* 
	 * Adust connection state
	 */

	sco_con->flags |= NG_HCI_CON_NOTIFY_SCO;

	sco_con->state = NG_HCI_CON_W4_CONN_COMPLETE;
	ng_hci_con_timeout(sco_con);

	/* 
	 * Queue and send HCI command
	 */

	NG_BT_MBUFQ_ENQUEUE(&unit->cmdq, m);
	if (!(unit->state & NG_HCI_UNIT_COMMAND_PENDING))
		error = ng_hci_send_command(unit);
out:
	NG_FREE_ITEM(item);

	return (error);
} /* ng_hci_lp_sco_con_req */

static int
ng_hci_lp_le_con_req(ng_hci_unit_p unit, item_p item, hook_p hook, int link_type)
{
	struct acl_con_req {
		ng_hci_cmd_pkt_t	 hdr;
		ng_hci_le_create_connection_cp	 cp;
	} __attribute__ ((packed))	*req = NULL;
	ng_hci_lp_con_req_ep		*ep = NULL;
	ng_hci_unit_con_p		 con = NULL;
	struct mbuf			*m = NULL;
	int				 error = 0;

	ep = (ng_hci_lp_con_req_ep *)(NGI_MSG(item)->data);
	if((link_type != NG_HCI_LINK_LE_PUBLIC)&&
	   (link_type != NG_HCI_LINK_LE_RANDOM)){
		printf("%s: Link type %d Cannot be here \n", __func__, 
		       link_type);
	}
	/*
	 * Only one ACL connection can exist between each pair of units.
	 * So try to find ACL connection descriptor (in any state) that
	 * has requested remote BD_ADDR.
	 *
	 * Two cases:
	 *
	 * 1) We do not have connection to the remote unit. This is simple.
	 *    Just create new connection descriptor and send HCI command to
	 *    create new connection.
	 *
	 * 2) We do have connection descriptor. We need to check connection
	 *    state:
	 * 
	 * 2.1) NG_HCI_CON_W4_LP_CON_RSP means that we are in the middle of
	 *      accepting connection from the remote unit. This is a race
	 *      condition. We will ignore this message.
	 *
	 * 2.2) NG_HCI_CON_W4_CONN_COMPLETE means that upper layer already
	 *      requested connection or we just accepted it. In any case
	 *      all we need to do here is set appropriate notification bit
	 *      and wait.
	 *	
	 * 2.3) NG_HCI_CON_OPEN means connection is open. Just reply back
	 *      and let upper layer know that we have connection already.
	 */

	con = ng_hci_con_by_bdaddr(unit, &ep->bdaddr, link_type);
	if (con != NULL) {
		switch (con->state) {
		case NG_HCI_CON_W4_LP_CON_RSP: /* XXX */
			error = EALREADY;
			break;

		case NG_HCI_CON_W4_CONN_COMPLETE:
			if (hook != unit->sco)
				con->flags |= NG_HCI_CON_NOTIFY_ACL;
			else
				con->flags |= NG_HCI_CON_NOTIFY_SCO;
			break;

		case NG_HCI_CON_OPEN: {
			struct ng_mesg		*msg = NULL;
			ng_hci_lp_con_cfm_ep	*cfm = NULL;

			if (hook != NULL && NG_HOOK_IS_VALID(hook)) {
				NGI_GET_MSG(item, msg);
				NG_FREE_MSG(msg);

				NG_MKMESSAGE(msg, NGM_HCI_COOKIE, 
					NGM_HCI_LP_CON_CFM, sizeof(*cfm), 
					M_NOWAIT);
				if (msg != NULL) {
					cfm = (ng_hci_lp_con_cfm_ep *)msg->data;
					cfm->status = 0;
					cfm->link_type = con->link_type;
					cfm->con_handle = con->con_handle;
					bcopy(&con->bdaddr, &cfm->bdaddr, 
						sizeof(cfm->bdaddr));

					/*
					 * This will forward item back to
					 * sender and set item to NULL
					 */

					_NGI_MSG(item) = msg;
					NG_FWD_ITEM_HOOK(error, item, hook);
				} else
					error = ENOMEM;
			} else
				NG_HCI_INFO(
"%s: %s - Source hook is not valid, hook=%p\n",
					__func__, NG_NODE_NAME(unit->node), 
					hook);
			} break;

		default:
			panic(
"%s: %s - Invalid connection state=%d\n",
				__func__, NG_NODE_NAME(unit->node), con->state);
			break;
		}

		goto out;
	}

	/*
	 * If we got here then we need to create new ACL connection descriptor
	 * and submit HCI command. First create new connection desriptor, set
	 * bdaddr and notification flags.
	 */

	con = ng_hci_new_con(unit, link_type);
	if (con == NULL) {
		error = ENOMEM;
		goto out;
	}

	bcopy(&ep->bdaddr, &con->bdaddr, sizeof(con->bdaddr));

	/* 
	 * Create HCI command 
	 */

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		ng_hci_free_con(con);
		error = ENOBUFS;
		goto out;
	}

	m->m_pkthdr.len = m->m_len = sizeof(*req);
	req = mtod(m, struct acl_con_req *);
	req->hdr.type = NG_HCI_CMD_PKT;
	req->hdr.length = sizeof(req->cp);
	req->hdr.opcode = htole16(NG_HCI_OPCODE(NG_HCI_OGF_LE,
					NG_HCI_OCF_LE_CREATE_CONNECTION));
	
	bcopy(&ep->bdaddr, &req->cp.peer_addr, sizeof(req->cp.peer_addr));
	req->cp.own_address_type = 0;
	req->cp.peer_addr_type = (link_type == NG_HCI_LINK_LE_RANDOM)? 1:0;
	req->cp.scan_interval = htole16(4);
	req->cp.scan_window = htole16(4);
	req->cp.filter_policy = 0;
	req->cp.conn_interval_min = htole16(0xf);
	req->cp.conn_interval_max = htole16(0xf);
	req->cp.conn_latency = htole16(0);
	req->cp.supervision_timeout = htole16(0xc80);
	req->cp.min_ce_length = htole16(1);
	req->cp.max_ce_length = htole16(1);
	/* 
	 * Adust connection state 
	 */

	if (hook != unit->sco)
		con->flags |= NG_HCI_CON_NOTIFY_ACL;
	else
		con->flags |= NG_HCI_CON_NOTIFY_SCO;

	con->state = NG_HCI_CON_W4_CONN_COMPLETE;
	ng_hci_con_timeout(con);

	/* 
	 * Queue and send HCI command 
	 */

	NG_BT_MBUFQ_ENQUEUE(&unit->cmdq, m);
	if (!(unit->state & NG_HCI_UNIT_COMMAND_PENDING))
		error = ng_hci_send_command(unit);
out:
	if (item != NULL)
		NG_FREE_ITEM(item);

	return (error);
} /* ng_hci_lp_acl_con_req */

/*
 * Process LP_DisconnectReq event from the upper layer protocol
 */

int
ng_hci_lp_discon_req(ng_hci_unit_p unit, item_p item, hook_p hook)
{
	struct discon_req {
		ng_hci_cmd_pkt_t	 hdr;
		ng_hci_discon_cp	 cp;
	} __attribute__ ((packed))	*req = NULL;
	ng_hci_lp_discon_req_ep		*ep = NULL;
	ng_hci_unit_con_p		 con = NULL;
	struct mbuf			*m = NULL;
	int				 error = 0;

	/* Check if unit is ready */
	if ((unit->state & NG_HCI_UNIT_READY) != NG_HCI_UNIT_READY) {
		NG_HCI_WARN(
"%s: %s - unit is not ready, state=%#x\n",
			__func__, NG_NODE_NAME(unit->node), unit->state);

		error = ENXIO;
		goto out;
	}

	if (NGI_MSG(item)->header.arglen != sizeof(*ep)) {
		NG_HCI_ALERT(
"%s: %s - invalid LP_DisconnectReq message size=%d\n",
			__func__, NG_NODE_NAME(unit->node),
			NGI_MSG(item)->header.arglen);

		error = EMSGSIZE;
		goto out;
	}

	ep = (ng_hci_lp_discon_req_ep *)(NGI_MSG(item)->data);

	con = ng_hci_con_by_handle(unit, ep->con_handle);
	if (con == NULL) {
		NG_HCI_ERR(
"%s: %s - invalid connection handle=%d\n",
			__func__, NG_NODE_NAME(unit->node), ep->con_handle);

		error = ENOENT;
		goto out;
	}

	if (con->state != NG_HCI_CON_OPEN) {
		NG_HCI_ERR(
"%s: %s - invalid connection state=%d, handle=%d\n",
			__func__, NG_NODE_NAME(unit->node), con->state,
			ep->con_handle);

		error = EINVAL;
		goto out;
	}

	/* 
	 * Create HCI command
	 */

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		error = ENOBUFS;
		goto out;
	}

	m->m_pkthdr.len = m->m_len = sizeof(*req);
	req = mtod(m, struct discon_req *);
	req->hdr.type = NG_HCI_CMD_PKT;
	req->hdr.length = sizeof(req->cp);
	req->hdr.opcode = htole16(NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
							NG_HCI_OCF_DISCON));

	req->cp.con_handle = htole16(ep->con_handle);
	req->cp.reason = ep->reason;

	/* 
	 * Queue and send HCI command 
	 */

	NG_BT_MBUFQ_ENQUEUE(&unit->cmdq, m);
	if (!(unit->state & NG_HCI_UNIT_COMMAND_PENDING))
		error = ng_hci_send_command(unit);
out:
	NG_FREE_ITEM(item);

	return (error);
} /* ng_hci_lp_discon_req */

/*
 * Send LP_ConnectCfm event to the upper layer protocol
 */

int
ng_hci_lp_con_cfm(ng_hci_unit_con_p con, int status)
{
	ng_hci_unit_p		 unit = con->unit;
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_con_cfm_ep	*ep = NULL;
	int			 error;

	/*
	 * Check who wants to be notified. For ACL links both ACL and SCO
	 * upstream hooks will be notified (if required). For SCO links
	 * only SCO upstream hook will receive notification
	 */

	if (con->link_type != NG_HCI_LINK_SCO && 
	    con->flags & NG_HCI_CON_NOTIFY_ACL) {
		if (unit->acl != NULL && NG_HOOK_IS_VALID(unit->acl)) {
			NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_CON_CFM, 
				sizeof(*ep), M_NOWAIT);
			if (msg != NULL) {
				ep = (ng_hci_lp_con_cfm_ep *) msg->data;
				ep->status = status;
				ep->link_type = con->link_type;
				ep->con_handle = con->con_handle;
				bcopy(&con->bdaddr, &ep->bdaddr, 
					sizeof(ep->bdaddr));

				NG_SEND_MSG_HOOK(error, unit->node, msg,
					unit->acl, 0);
			}
		} else
			NG_HCI_INFO(
"%s: %s - ACL hook not valid, hook=%p\n",
				__func__, NG_NODE_NAME(unit->node), unit->acl);

		con->flags &= ~NG_HCI_CON_NOTIFY_ACL;
	}

	if (con->flags & NG_HCI_CON_NOTIFY_SCO) {
		if (unit->sco != NULL && NG_HOOK_IS_VALID(unit->sco)) {
			NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_CON_CFM, 
				sizeof(*ep), M_NOWAIT);
			if (msg != NULL) {
				ep = (ng_hci_lp_con_cfm_ep *) msg->data;
				ep->status = status;
				ep->link_type = con->link_type;
				ep->con_handle = con->con_handle;
				bcopy(&con->bdaddr, &ep->bdaddr, 
					sizeof(ep->bdaddr));

				NG_SEND_MSG_HOOK(error, unit->node, msg,
					unit->sco, 0);
			}
		} else
			NG_HCI_INFO(
"%s: %s - SCO hook not valid, hook=%p\n",
				__func__, NG_NODE_NAME(unit->node), unit->acl);

		con->flags &= ~NG_HCI_CON_NOTIFY_SCO;
	}

	return (0);
} /* ng_hci_lp_con_cfm */

int
ng_hci_lp_enc_change(ng_hci_unit_con_p con, int status)
{
	ng_hci_unit_p		 unit = con->unit;
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_enc_change_ep	*ep = NULL;
	int			 error;


	if (con->link_type != NG_HCI_LINK_SCO) {
		if (unit->acl != NULL && NG_HOOK_IS_VALID(unit->acl)) {
			NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_ENC_CHG, 
				sizeof(*ep), M_NOWAIT);
			if (msg != NULL) {
				ep = (ng_hci_lp_enc_change_ep *) msg->data;
				ep->status = status;
				ep->link_type = con->link_type;
				ep->con_handle = con->con_handle;

				NG_SEND_MSG_HOOK(error, unit->node, msg,
					unit->acl, 0);
			}
		} else
			NG_HCI_INFO(
"%s: %s - ACL hook not valid, hook=%p\n",
				__func__, NG_NODE_NAME(unit->node), unit->acl);

	}
	return (0);
} /* ng_hci_lp_con_cfm */

/*
 * Send LP_ConnectInd event to the upper layer protocol
 */

int
ng_hci_lp_con_ind(ng_hci_unit_con_p con, u_int8_t *uclass)
{
	ng_hci_unit_p		 unit = con->unit;
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_con_ind_ep	*ep = NULL;
	hook_p			 hook = NULL;
	int			 error = 0;

	/*
	 * Connection_Request event is generated for specific link type.
	 * Use link_type to select upstream hook.
	 */

	if (con->link_type != NG_HCI_LINK_SCO)
		hook = unit->acl;
	else
		hook = unit->sco;

	if (hook != NULL && NG_HOOK_IS_VALID(hook)) {
		NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_CON_IND, 
			sizeof(*ep), M_NOWAIT);
		if (msg == NULL)
			return (ENOMEM);

		ep = (ng_hci_lp_con_ind_ep *)(msg->data);
		ep->link_type = con->link_type;
		bcopy(uclass, ep->uclass, sizeof(ep->uclass));
		bcopy(&con->bdaddr, &ep->bdaddr, sizeof(ep->bdaddr));

		NG_SEND_MSG_HOOK(error, unit->node, msg, hook, 0);
	} else {
		NG_HCI_WARN(
"%s: %s - Upstream hook is not connected or not valid, hook=%p\n",
			__func__, NG_NODE_NAME(unit->node), hook);

		error = ENOTCONN;
	}

	return (error);
} /* ng_hci_lp_con_ind */

/*
 * Process LP_ConnectRsp event from the upper layer protocol
 */

int
ng_hci_lp_con_rsp(ng_hci_unit_p unit, item_p item, hook_p hook)
{
	struct con_rsp_req {
		ng_hci_cmd_pkt_t		 hdr;
		union {
			ng_hci_accept_con_cp	 acc;
			ng_hci_reject_con_cp	 rej;
		} __attribute__ ((packed))	 cp;
	} __attribute__ ((packed))		*req = NULL;
	ng_hci_lp_con_rsp_ep			*ep = NULL;
	ng_hci_unit_con_p			 con = NULL;
	struct mbuf				*m = NULL;
	int					 error = 0;

	/* Check if unit is ready */
	if ((unit->state & NG_HCI_UNIT_READY) != NG_HCI_UNIT_READY) {
		NG_HCI_WARN(
"%s: %s - unit is not ready, state=%#x\n",
			__func__, NG_NODE_NAME(unit->node), unit->state);

		error = ENXIO;
		goto out;
	}

	if (NGI_MSG(item)->header.arglen != sizeof(*ep)) {
		NG_HCI_ALERT(
"%s: %s - invalid LP_ConnectRsp message size=%d\n",
			__func__, NG_NODE_NAME(unit->node),
			NGI_MSG(item)->header.arglen);

		error = EMSGSIZE;
		goto out;
	}

	ep = (ng_hci_lp_con_rsp_ep *)(NGI_MSG(item)->data);

	/*
	 * Here we have to deal with race. Upper layers might send conflicting
	 * requests. One might send Accept and other Reject. We will not try
	 * to solve all the problems, so first request will always win.
	 *
	 * Try to find connection that matches the following:
	 *
	 * 1) con->link_type == ep->link_type
	 *
	 * 2) con->state == NG_HCI_CON_W4_LP_CON_RSP ||
	 *    con->state == NG_HCI_CON_W4_CONN_COMPLETE
	 *
	 * 3) con->bdaddr == ep->bdaddr
	 *
	 * Two cases:
	 *
	 * 1) We do not have connection descriptor. Could be bogus request or
	 *    we have rejected connection already.
	 *
	 * 2) We do have connection descriptor. Then we need to check state:
	 *
	 * 2.1) NG_HCI_CON_W4_LP_CON_RSP means upper layer has requested 
	 *      connection and it is a first response from the upper layer.
	 *      if "status == 0" (Accept) then we will send Accept_Connection
	 *      command and change connection state to W4_CONN_COMPLETE, else
	 *      send reject and delete connection.
	 *
	 * 2.2) NG_HCI_CON_W4_CONN_COMPLETE means that we already accepted 
	 *      connection. If "status == 0" we just need to link request
	 *      and wait, else ignore Reject request.
	 */

	LIST_FOREACH(con, &unit->con_list, next)
		if (con->link_type == ep->link_type &&
		    (con->state == NG_HCI_CON_W4_LP_CON_RSP ||
		     con->state == NG_HCI_CON_W4_CONN_COMPLETE) &&
		    bcmp(&con->bdaddr, &ep->bdaddr, sizeof(bdaddr_t)) == 0)
			break;

	if (con == NULL) {
		/* Reject for non-existing connection is fine */
		error = (ep->status == 0)? ENOENT : 0;
		goto out;
	}

	/* 
	 * Remove connection timeout and check connection state.
	 * Note: if ng_hci_con_untimeout() fails (returns non-zero value) then
	 * timeout already happened and event went into node's queue.
	 */

	if ((error = ng_hci_con_untimeout(con)) != 0)
		goto out;

	switch (con->state) {
	case NG_HCI_CON_W4_LP_CON_RSP:

		/* 
		 * Create HCI command 
		 */

		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL) {
			error = ENOBUFS;
			goto out;
		}
		
		req = mtod(m, struct con_rsp_req *);
		req->hdr.type = NG_HCI_CMD_PKT;

		if (ep->status == 0) {
			req->hdr.length = sizeof(req->cp.acc);
			req->hdr.opcode = htole16(NG_HCI_OPCODE(
							NG_HCI_OGF_LINK_CONTROL,
							NG_HCI_OCF_ACCEPT_CON));

			bcopy(&ep->bdaddr, &req->cp.acc.bdaddr,
				sizeof(req->cp.acc.bdaddr));

			/*
			 * We are accepting connection, so if we support role 
			 * switch and role switch was enabled then set role to 
			 * NG_HCI_ROLE_MASTER and let LM peform role switch.
			 * Otherwise we remain slave. In this case LM WILL NOT
			 * perform role switch.
			 */

			if ((unit->features[0] & NG_HCI_LMP_SWITCH) &&
			    unit->role_switch)
				req->cp.acc.role = NG_HCI_ROLE_MASTER;
			else
				req->cp.acc.role = NG_HCI_ROLE_SLAVE;

			/* 
			 * Adjust connection state 
			 */

			if (hook == unit->acl)
				con->flags |= NG_HCI_CON_NOTIFY_ACL;
			else
				con->flags |= NG_HCI_CON_NOTIFY_SCO;

			con->state = NG_HCI_CON_W4_CONN_COMPLETE;
			ng_hci_con_timeout(con);
		} else {
			req->hdr.length = sizeof(req->cp.rej);
			req->hdr.opcode = htole16(NG_HCI_OPCODE(
							NG_HCI_OGF_LINK_CONTROL,
							NG_HCI_OCF_REJECT_CON));

			bcopy(&ep->bdaddr, &req->cp.rej.bdaddr,
				sizeof(req->cp.rej.bdaddr));

			req->cp.rej.reason = ep->status;

			/*
			 * Free connection descritor
			 * Item will be deleted just before return.
			 */
			
			ng_hci_free_con(con);
		}

		m->m_pkthdr.len = m->m_len = sizeof(req->hdr) + req->hdr.length;

		/* Queue and send HCI command */
		NG_BT_MBUFQ_ENQUEUE(&unit->cmdq, m);
		if (!(unit->state & NG_HCI_UNIT_COMMAND_PENDING))
			error = ng_hci_send_command(unit);
		break;

	case NG_HCI_CON_W4_CONN_COMPLETE:
		if (ep->status == 0) {
			if (hook == unit->acl)
				con->flags |= NG_HCI_CON_NOTIFY_ACL;
			else
				con->flags |= NG_HCI_CON_NOTIFY_SCO;
		} else
			error = EPERM;
		break;

	default:
		panic(
"%s: %s - Invalid connection state=%d\n",
			__func__, NG_NODE_NAME(unit->node), con->state);
		break;
	}
out:
	NG_FREE_ITEM(item);

	return (error);
} /* ng_hci_lp_con_rsp */

/*
 * Send LP_DisconnectInd to the upper layer protocol
 */

int
ng_hci_lp_discon_ind(ng_hci_unit_con_p con, int reason)
{
	ng_hci_unit_p		 unit = con->unit;
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_discon_ind_ep	*ep = NULL;
	int			 error = 0;

	/*
	 * Disconnect_Complete event is generated for specific connection
	 * handle. For ACL connection handles both ACL and SCO upstream
	 * hooks will receive notification. For SCO connection handles
	 * only SCO upstream hook will receive notification.
	 */

	if (con->link_type != NG_HCI_LINK_SCO) {
		if (unit->acl != NULL && NG_HOOK_IS_VALID(unit->acl)) {
			NG_MKMESSAGE(msg, NGM_HCI_COOKIE, 
				NGM_HCI_LP_DISCON_IND, sizeof(*ep), M_NOWAIT);
			if (msg == NULL)
				return (ENOMEM);

			ep = (ng_hci_lp_discon_ind_ep *) msg->data;
			ep->reason = reason;
			ep->link_type = con->link_type;
			ep->con_handle = con->con_handle;

			NG_SEND_MSG_HOOK(error,unit->node,msg,unit->acl,0);
		} else
			NG_HCI_INFO(
"%s: %s - ACL hook is not connected or not valid, hook=%p\n",
				__func__, NG_NODE_NAME(unit->node), unit->acl);
	}

	if (unit->sco != NULL && NG_HOOK_IS_VALID(unit->sco)) {
		NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_DISCON_IND, 
			sizeof(*ep), M_NOWAIT);
		if (msg == NULL)
			return (ENOMEM);

		ep = (ng_hci_lp_discon_ind_ep *) msg->data;
		ep->reason = reason;
		ep->link_type = con->link_type;
		ep->con_handle = con->con_handle;

		NG_SEND_MSG_HOOK(error, unit->node, msg, unit->sco, 0);
	} else
		NG_HCI_INFO(
"%s: %s - SCO hook is not connected or not valid, hook=%p\n",
			__func__, NG_NODE_NAME(unit->node), unit->sco);

	return (0);
} /* ng_hci_lp_discon_ind */

/*
 * Process LP_QoSReq action from the upper layer protocol
 */

int
ng_hci_lp_qos_req(ng_hci_unit_p unit, item_p item, hook_p hook)
{
	struct qos_setup_req {
		ng_hci_cmd_pkt_t	 hdr;
		ng_hci_qos_setup_cp	 cp;
	} __attribute__ ((packed))	*req = NULL;
	ng_hci_lp_qos_req_ep		*ep = NULL;
	ng_hci_unit_con_p		 con = NULL;
	struct mbuf			*m = NULL;
	int				 error = 0;

	/* Check if unit is ready */
	if ((unit->state & NG_HCI_UNIT_READY) != NG_HCI_UNIT_READY) {
		NG_HCI_WARN(
"%s: %s - unit is not ready, state=%#x\n",
			__func__, NG_NODE_NAME(unit->node), unit->state);

		error = ENXIO;
		goto out;
	}

	if (NGI_MSG(item)->header.arglen != sizeof(*ep)) {
		NG_HCI_ALERT(
"%s: %s - invalid LP_QoSSetupReq message size=%d\n",
			__func__, NG_NODE_NAME(unit->node),
			NGI_MSG(item)->header.arglen);

		error = EMSGSIZE;
		goto out;
	}

	ep = (ng_hci_lp_qos_req_ep *)(NGI_MSG(item)->data);

	con = ng_hci_con_by_handle(unit, ep->con_handle);
	if (con == NULL) {
		NG_HCI_ERR(
"%s: %s - invalid connection handle=%d\n",
			__func__, NG_NODE_NAME(unit->node), ep->con_handle);

		error = EINVAL;
		goto out;
	}

	if (con->link_type != NG_HCI_LINK_ACL) {
		NG_HCI_ERR("%s: %s - invalid link type=%d\n",
			__func__, NG_NODE_NAME(unit->node), con->link_type);

		error = EINVAL;
		goto out;
	}

	if (con->state != NG_HCI_CON_OPEN) {
		NG_HCI_ERR(
"%s: %s - invalid connection state=%d, handle=%d\n",
			__func__, NG_NODE_NAME(unit->node), con->state,
			con->con_handle);

		error = EINVAL;
		goto out;
	}

	/* 
	 * Create HCI command 
	 */

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		error = ENOBUFS;
		goto out;
	}

	m->m_pkthdr.len = m->m_len = sizeof(*req);
	req = mtod(m, struct qos_setup_req *);
	req->hdr.type = NG_HCI_CMD_PKT;
	req->hdr.length = sizeof(req->cp);
	req->hdr.opcode = htole16(NG_HCI_OPCODE(NG_HCI_OGF_LINK_POLICY,
			NG_HCI_OCF_QOS_SETUP));

	req->cp.con_handle = htole16(ep->con_handle);
	req->cp.flags = ep->flags;
	req->cp.service_type = ep->service_type;
	req->cp.token_rate = htole32(ep->token_rate);
	req->cp.peak_bandwidth = htole32(ep->peak_bandwidth);
	req->cp.latency = htole32(ep->latency);
	req->cp.delay_variation = htole32(ep->delay_variation);

	/* 
	 * Adjust connection state 
 	 */

	if (hook == unit->acl)
		con->flags |= NG_HCI_CON_NOTIFY_ACL;
	else
		con->flags |= NG_HCI_CON_NOTIFY_SCO;

	/* 
	 * Queue and send HCI command 
	 */

	NG_BT_MBUFQ_ENQUEUE(&unit->cmdq, m);
	if (!(unit->state & NG_HCI_UNIT_COMMAND_PENDING))
		error = ng_hci_send_command(unit);
out:
	NG_FREE_ITEM(item);
	
	return (error);
} /* ng_hci_lp_qos_req */

/*
 * Send LP_QoSCfm event to the upper layer protocol
 */

int
ng_hci_lp_qos_cfm(ng_hci_unit_con_p con, int status)
{
	ng_hci_unit_p		 unit = con->unit;
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_qos_cfm_ep	*ep = NULL;
	int			 error;

	if (con->flags & NG_HCI_CON_NOTIFY_ACL) {
		if (unit->acl != NULL && NG_HOOK_IS_VALID(unit->acl)) {
			NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_QOS_CFM, 
				sizeof(*ep), M_NOWAIT);
			if (msg != NULL) {
				ep = (ng_hci_lp_qos_cfm_ep *) msg->data;
				ep->status = status;
				ep->con_handle = con->con_handle;

				NG_SEND_MSG_HOOK(error, unit->node, msg,
					unit->acl, 0);
			}
		} else
			NG_HCI_INFO(
"%s: %s - ACL hook not valid, hook=%p\n",
				__func__, NG_NODE_NAME(unit->node), unit->acl);

		con->flags &= ~NG_HCI_CON_NOTIFY_ACL;
	}

	if (con->flags & NG_HCI_CON_NOTIFY_SCO) {
		if (unit->sco != NULL && NG_HOOK_IS_VALID(unit->sco)) {
			NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_QOS_CFM, 
				sizeof(*ep), M_NOWAIT);
			if (msg != NULL) {
				ep = (ng_hci_lp_qos_cfm_ep *) msg->data;
				ep->status = status;
				ep->con_handle = con->con_handle;

				NG_SEND_MSG_HOOK(error, unit->node, msg,
					unit->sco, 0);
			}
		} else
			NG_HCI_INFO(
"%s: %s - SCO hook not valid, hook=%p\n",
				 __func__, NG_NODE_NAME(unit->node), unit->sco);

		con->flags &= ~NG_HCI_CON_NOTIFY_SCO;
	}
	
	return (0);
} /* ng_hci_lp_qos_cfm */

/*
 * Send LP_QoSViolationInd event to the upper layer protocol
 */

int
ng_hci_lp_qos_ind(ng_hci_unit_con_p con)
{
	ng_hci_unit_p		 unit = con->unit;
	struct ng_mesg		*msg = NULL;
	ng_hci_lp_qos_ind_ep	*ep = NULL;
	int			 error;

	/* 
	 * QoS Violation can only be generated for ACL connection handles.
	 * Both ACL and SCO upstream hooks will receive notification.
	 */

	if (unit->acl != NULL && NG_HOOK_IS_VALID(unit->acl)) {
		NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_QOS_IND, 
			sizeof(*ep), M_NOWAIT);
		if (msg == NULL)
			return (ENOMEM);

		ep = (ng_hci_lp_qos_ind_ep *) msg->data;
		ep->con_handle = con->con_handle;

		NG_SEND_MSG_HOOK(error, unit->node, msg, unit->acl, 0);
	} else
		NG_HCI_INFO(
"%s: %s - ACL hook is not connected or not valid, hook=%p\n",
			__func__, NG_NODE_NAME(unit->node), unit->acl);

	if (unit->sco != NULL && NG_HOOK_IS_VALID(unit->sco)) {
		NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_LP_QOS_IND, 
			sizeof(*ep), M_NOWAIT);
		if (msg == NULL)
			return (ENOMEM);

		ep = (ng_hci_lp_qos_ind_ep *) msg->data;
		ep->con_handle = con->con_handle;

		NG_SEND_MSG_HOOK(error, unit->node, msg, unit->sco, 0);
	} else
		NG_HCI_INFO(
"%s: %s - SCO hook is not connected or not valid, hook=%p\n",
			__func__, NG_NODE_NAME(unit->node), unit->sco);

	return (0);
} /* ng_hci_lp_qos_ind */

/*
 * Process connection timeout
 */

void
ng_hci_process_con_timeout(node_p node, hook_p hook, void *arg1, int con_handle)
{
	ng_hci_unit_p		unit = NULL;
	ng_hci_unit_con_p	con = NULL;

	if (NG_NODE_NOT_VALID(node)) {
		printf("%s: Netgraph node is not valid\n", __func__);
		return;
	}

	unit = (ng_hci_unit_p) NG_NODE_PRIVATE(node);
	con = ng_hci_con_by_handle(unit, con_handle);

	if (con == NULL) {
		NG_HCI_ALERT(
"%s: %s - could not find connection, handle=%d\n",
			__func__, NG_NODE_NAME(node), con_handle);
		return;
	}

	if (!(con->flags & NG_HCI_CON_TIMEOUT_PENDING)) {
		NG_HCI_ALERT(
"%s: %s - no pending connection timeout, handle=%d, state=%d, flags=%#x\n",
			__func__, NG_NODE_NAME(node), con_handle, con->state,
			con->flags);
		return;
	}

	con->flags &= ~NG_HCI_CON_TIMEOUT_PENDING;

	/*
	 * We expect to receive connection timeout in one of the following
	 * states:
	 *
	 * 1) NG_HCI_CON_W4_LP_CON_RSP means that upper layer has not responded
	 *    to our LP_CON_IND. Do nothing and destroy connection. Remote peer
	 *    most likely already gave up on us.
	 * 
	 * 2) NG_HCI_CON_W4_CONN_COMPLETE means upper layer requested connection
	 *    (or we in the process of accepting it) and baseband has timedout
	 *    on us. Inform upper layers and send LP_CON_CFM.
	 */

	switch (con->state) {
	case NG_HCI_CON_W4_LP_CON_RSP:
		break;

	case NG_HCI_CON_W4_CONN_COMPLETE:
		ng_hci_lp_con_cfm(con, 0xee);
		break;

	default:
		panic(
"%s: %s - Invalid connection state=%d\n",
			__func__, NG_NODE_NAME(node), con->state);
		break;
	}

	ng_hci_free_con(con);
} /* ng_hci_process_con_timeout */


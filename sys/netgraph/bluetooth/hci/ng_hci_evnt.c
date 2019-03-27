/*
 * ng_hci_evnt.c
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
 * $Id: ng_hci_evnt.c,v 1.6 2003/09/08 18:57:51 max Exp $
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
 **                     HCI event processing module
 ******************************************************************************
 ******************************************************************************/

/* 
 * Event processing routines 
 */

static int inquiry_result             (ng_hci_unit_p, struct mbuf *);
static int con_compl                  (ng_hci_unit_p, struct mbuf *);
static int con_req                    (ng_hci_unit_p, struct mbuf *);
static int discon_compl               (ng_hci_unit_p, struct mbuf *);
static int encryption_change          (ng_hci_unit_p, struct mbuf *);
static int read_remote_features_compl (ng_hci_unit_p, struct mbuf *);
static int qos_setup_compl            (ng_hci_unit_p, struct mbuf *);
static int hardware_error             (ng_hci_unit_p, struct mbuf *);
static int role_change                (ng_hci_unit_p, struct mbuf *);
static int num_compl_pkts             (ng_hci_unit_p, struct mbuf *);
static int mode_change                (ng_hci_unit_p, struct mbuf *);
static int data_buffer_overflow       (ng_hci_unit_p, struct mbuf *);
static int read_clock_offset_compl    (ng_hci_unit_p, struct mbuf *);
static int qos_violation              (ng_hci_unit_p, struct mbuf *);
static int page_scan_mode_change      (ng_hci_unit_p, struct mbuf *);
static int page_scan_rep_mode_change  (ng_hci_unit_p, struct mbuf *);
static int sync_con_queue             (ng_hci_unit_p, ng_hci_unit_con_p, int);
static int send_data_packets          (ng_hci_unit_p, int, int);
static int le_event		      (ng_hci_unit_p, struct mbuf *);

/*
 * Process HCI event packet
 */
 
int
ng_hci_process_event(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_event_pkt_t	*hdr = NULL;
	int			 error = 0;

	/* Get event packet header */
	NG_HCI_M_PULLUP(event, sizeof(*hdr));
	if (event == NULL)
		return (ENOBUFS);

	hdr = mtod(event, ng_hci_event_pkt_t *);

	NG_HCI_INFO(
"%s: %s - got HCI event=%#x, length=%d\n",
		__func__, NG_NODE_NAME(unit->node), hdr->event, hdr->length);

	/* Get rid of event header and process event */
	m_adj(event, sizeof(*hdr));

	switch (hdr->event) {
	case NG_HCI_EVENT_INQUIRY_COMPL:
	case NG_HCI_EVENT_RETURN_LINK_KEYS:
	case NG_HCI_EVENT_PIN_CODE_REQ:
	case NG_HCI_EVENT_LINK_KEY_REQ:
	case NG_HCI_EVENT_LINK_KEY_NOTIFICATION:
	case NG_HCI_EVENT_LOOPBACK_COMMAND:
	case NG_HCI_EVENT_AUTH_COMPL:
	case NG_HCI_EVENT_CHANGE_CON_LINK_KEY_COMPL:
	case NG_HCI_EVENT_MASTER_LINK_KEY_COMPL:
	case NG_HCI_EVENT_FLUSH_OCCUR:	/* XXX Do we have to handle it? */
	case NG_HCI_EVENT_MAX_SLOT_CHANGE:
	case NG_HCI_EVENT_CON_PKT_TYPE_CHANGED:
	case NG_HCI_EVENT_BT_LOGO:
	case NG_HCI_EVENT_VENDOR:
	case NG_HCI_EVENT_REMOTE_NAME_REQ_COMPL:
	case NG_HCI_EVENT_READ_REMOTE_VER_INFO_COMPL:
		/* These do not need post processing */
		NG_FREE_M(event);
		break;
	case NG_HCI_EVENT_LE:
		error = le_event(unit, event);
		break;

	case NG_HCI_EVENT_INQUIRY_RESULT:
		error = inquiry_result(unit, event);
		break;

	case NG_HCI_EVENT_CON_COMPL:
		error = con_compl(unit, event);
		break;

	case NG_HCI_EVENT_CON_REQ:
		error = con_req(unit, event);
		break;

	case NG_HCI_EVENT_DISCON_COMPL:
		error = discon_compl(unit, event);
		break;

	case NG_HCI_EVENT_ENCRYPTION_CHANGE:
		error = encryption_change(unit, event);
		break;

	case NG_HCI_EVENT_READ_REMOTE_FEATURES_COMPL:
		error = read_remote_features_compl(unit, event);
		break;

	case NG_HCI_EVENT_QOS_SETUP_COMPL:
		error = qos_setup_compl(unit, event);
		break;

	case NG_HCI_EVENT_COMMAND_COMPL:
		error = ng_hci_process_command_complete(unit, event);
		break;

	case NG_HCI_EVENT_COMMAND_STATUS:
		error = ng_hci_process_command_status(unit, event);
		break;

	case NG_HCI_EVENT_HARDWARE_ERROR:
		error = hardware_error(unit, event);
		break;

	case NG_HCI_EVENT_ROLE_CHANGE:
		error = role_change(unit, event);
		break;

	case NG_HCI_EVENT_NUM_COMPL_PKTS:
		error = num_compl_pkts(unit, event);
		break;

	case NG_HCI_EVENT_MODE_CHANGE:
		error = mode_change(unit, event);
		break;

	case NG_HCI_EVENT_DATA_BUFFER_OVERFLOW:
		error = data_buffer_overflow(unit, event);
		break;

	case NG_HCI_EVENT_READ_CLOCK_OFFSET_COMPL:
		error = read_clock_offset_compl(unit, event);
		break;

	case NG_HCI_EVENT_QOS_VIOLATION:
		error = qos_violation(unit, event);
		break;

	case NG_HCI_EVENT_PAGE_SCAN_MODE_CHANGE:
		error = page_scan_mode_change(unit, event);
		break;

	case NG_HCI_EVENT_PAGE_SCAN_REP_MODE_CHANGE:
		error = page_scan_rep_mode_change(unit, event);
		break;

	default:
		NG_FREE_M(event);
		error = EINVAL;
		break;
	}

	return (error);
} /* ng_hci_process_event */

/*
 * Send ACL and/or SCO data to the unit driver
 */

void
ng_hci_send_data(ng_hci_unit_p unit)
{
	int	count;

	/* Send ACL data */
	NG_HCI_BUFF_ACL_AVAIL(unit->buffer, count);

	NG_HCI_INFO(
"%s: %s - sending ACL data packets, count=%d\n",
		__func__, NG_NODE_NAME(unit->node), count);

	if (count > 0) {
		count = send_data_packets(unit, NG_HCI_LINK_ACL, count);
		NG_HCI_STAT_ACL_SENT(unit->stat, count);
		NG_HCI_BUFF_ACL_USE(unit->buffer, count);
	}

	/* Send SCO data */
	NG_HCI_BUFF_SCO_AVAIL(unit->buffer, count);

	NG_HCI_INFO(
"%s: %s - sending SCO data packets, count=%d\n",
		__func__, NG_NODE_NAME(unit->node), count);

	if (count > 0) {
		count = send_data_packets(unit, NG_HCI_LINK_SCO, count);
		NG_HCI_STAT_SCO_SENT(unit->stat, count);
		NG_HCI_BUFF_SCO_USE(unit->buffer, count);
	}
} /* ng_hci_send_data */

/*
 * Send data packets to the lower layer.
 */

static int
send_data_packets(ng_hci_unit_p unit, int link_type, int limit)
{
	ng_hci_unit_con_p	con = NULL, winner = NULL;
	int			reallink_type;
	item_p			item = NULL;
	int			min_pending, total_sent, sent, error, v;

	for (total_sent = 0; limit > 0; ) {
		min_pending = 0x0fffffff;
		winner = NULL;

		/*
		 * Find the connection that has has data to send 
		 * and the smallest number of pending packets
		 */

		LIST_FOREACH(con, &unit->con_list, next) {
			reallink_type = (con->link_type == NG_HCI_LINK_SCO)?
				NG_HCI_LINK_SCO: NG_HCI_LINK_ACL;
			if (reallink_type != link_type){
				continue;
			}
			if (NG_BT_ITEMQ_LEN(&con->conq) == 0)
				continue;
        
			if (con->pending < min_pending) {
				winner = con;
				min_pending = con->pending;
			}
		}

	        if (winner == NULL)
			break;

		/* 
		 * OK, we have a winner now send as much packets as we can
		 * Count the number of packets we have sent and then sync
		 * winner connection queue.
		 */

		for (sent = 0; limit > 0; limit --, total_sent ++, sent ++) {
			NG_BT_ITEMQ_DEQUEUE(&winner->conq, item);
			if (item == NULL)
				break;
		
			NG_HCI_INFO(
"%s: %s - sending data packet, handle=%d, len=%d\n",
				__func__, NG_NODE_NAME(unit->node), 
				winner->con_handle, NGI_M(item)->m_pkthdr.len);

			/* Check if driver hook still there */
			v = (unit->drv != NULL && NG_HOOK_IS_VALID(unit->drv));
			if (!v || (unit->state & NG_HCI_UNIT_READY) != 
					NG_HCI_UNIT_READY) {
				NG_HCI_ERR(
"%s: %s - could not send data. Hook \"%s\" is %svalid, state=%#x\n",
					__func__, NG_NODE_NAME(unit->node),
					NG_HCI_HOOK_DRV, ((v)? "" : "not "),
					unit->state);

				NG_FREE_ITEM(item);
				error = ENOTCONN;
			} else {
				v = NGI_M(item)->m_pkthdr.len;

				/* Give packet to raw hook */
				ng_hci_mtap(unit, NGI_M(item));

				/* ... and forward item to the driver */
				NG_FWD_ITEM_HOOK(error, item, unit->drv);
			}

			if (error != 0) {
				NG_HCI_ERR(
"%s: %s - could not send data packet, handle=%d, error=%d\n",
					__func__, NG_NODE_NAME(unit->node),
					winner->con_handle, error);
				break;
			}

			winner->pending ++;
			NG_HCI_STAT_BYTES_SENT(unit->stat, v);
		}

		/*
		 * Sync connection queue for the winner
		 */
		sync_con_queue(unit, winner, sent);
	}

	return (total_sent);
} /* send_data_packets */

/*
 * Send flow control messages to the upper layer
 */

static int
sync_con_queue(ng_hci_unit_p unit, ng_hci_unit_con_p con, int completed)
{
	hook_p				 hook = NULL;
	struct ng_mesg			*msg = NULL;
	ng_hci_sync_con_queue_ep	*state = NULL;
	int				 error;

	hook = (con->link_type != NG_HCI_LINK_SCO)? unit->acl : unit->sco;
	if (hook == NULL || NG_HOOK_NOT_VALID(hook))
		return (ENOTCONN);

	NG_MKMESSAGE(msg, NGM_HCI_COOKIE, NGM_HCI_SYNC_CON_QUEUE,
		sizeof(*state), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	state = (ng_hci_sync_con_queue_ep *)(msg->data);
	state->con_handle = con->con_handle;
	state->completed = completed;

	NG_SEND_MSG_HOOK(error, unit->node, msg, hook, 0);

	return (error);
} /* sync_con_queue */
/* le meta event */
/* Inquiry result event */
static int
le_advertizing_report(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_le_advertising_report_ep	*ep = NULL;
	ng_hci_neighbor_p		 n = NULL;
	bdaddr_t			 bdaddr;
	int				 error = 0;
	u_int8_t event_type;
	u_int8_t addr_type;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_le_advertising_report_ep *);
	m_adj(event, sizeof(*ep));

	for (; ep->num_reports > 0; ep->num_reports --) {
		/* Get remote unit address */
		NG_HCI_M_PULLUP(event, sizeof(u_int8_t));
		event_type = *mtod(event, u_int8_t *);
		m_adj(event, sizeof(u_int8_t));
		NG_HCI_M_PULLUP(event, sizeof(u_int8_t));
		addr_type = *mtod(event, u_int8_t *);
		m_adj(event, sizeof(u_int8_t));

		m_copydata(event, 0, sizeof(bdaddr), (caddr_t) &bdaddr);
		m_adj(event, sizeof(bdaddr));
		
		/* Lookup entry in the cache */
		n = ng_hci_get_neighbor(unit, &bdaddr, (addr_type) ? NG_HCI_LINK_LE_RANDOM:NG_HCI_LINK_LE_PUBLIC);
		if (n == NULL) {
			/* Create new entry */
			n = ng_hci_new_neighbor(unit);
			if (n == NULL) {
				error = ENOMEM;
				break;
			}
			bcopy(&bdaddr, &n->bdaddr, sizeof(n->bdaddr));
			n->addrtype = (addr_type)? NG_HCI_LINK_LE_RANDOM :
			  NG_HCI_LINK_LE_PUBLIC;
			
		} else
			getmicrotime(&n->updated);
		
		{
			/* 
			 * TODO: Make these information 
			 * Available from userland.
			 */
			u_int8_t length_data;
			
			event = m_pullup(event, sizeof(u_int8_t));
			if(event == NULL){
				NG_HCI_WARN("%s: Event datasize Pullup Failed\n", __func__);
				goto out;
			}
			length_data = *mtod(event, u_int8_t *);
			m_adj(event, sizeof(u_int8_t));
			n->extinq_size = (length_data < NG_HCI_EXTINQ_MAX)?
				length_data : NG_HCI_EXTINQ_MAX;
			
			/*Advertizement data*/
			event = m_pullup(event, n->extinq_size);
			if(event == NULL){
				NG_HCI_WARN("%s: Event data pullup Failed\n", __func__);
				goto out;
			}
			m_copydata(event, 0, n->extinq_size, n->extinq_data);
			m_adj(event, n->extinq_size);
			event = m_pullup(event, sizeof(char ));
			/*Get RSSI*/
			if(event == NULL){
				NG_HCI_WARN("%s: Event rssi pull up Failed\n", __func__);
				
				goto out;
			}				
			n->page_scan_mode = *mtod(event, char *);
			m_adj(event, sizeof(u_int8_t));
		}
	}
 out:
	NG_FREE_M(event);

	return (error);
} /* inquiry_result */

static int le_connection_complete(ng_hci_unit_p unit, struct mbuf *event)
{
	int			 error = 0;

	ng_hci_le_connection_complete_ep	*ep = NULL;
	ng_hci_unit_con_p	 con = NULL;
	int link_type;
	uint8_t uclass[3] = {0,0,0};//dummy uclass

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_le_connection_complete_ep *);
	link_type = (ep->address_type)? NG_HCI_LINK_LE_RANDOM :
	  NG_HCI_LINK_LE_PUBLIC;
	/*
	 * Find the first connection descriptor that matches the following:
	 *
	 * 1) con->link_type == link_type
	 * 2) con->state == NG_HCI_CON_W4_CONN_COMPLETE
	 * 3) con->bdaddr == ep->address
	 */
	LIST_FOREACH(con, &unit->con_list, next)
		if (con->link_type == link_type &&
		    con->state == NG_HCI_CON_W4_CONN_COMPLETE &&
		    bcmp(&con->bdaddr, &ep->address, sizeof(bdaddr_t)) == 0)
			break;

	/*
	 * Two possible cases:
	 *
	 * 1) We have found connection descriptor. That means upper layer has
	 *    requested this connection via LP_CON_REQ message. In this case
	 *    connection must have timeout set. If ng_hci_con_untimeout() fails
	 *    then timeout message already went into node's queue. In this case
	 *    ignore Connection_Complete event and let timeout deal with it.
	 *
	 * 2) We do not have connection descriptor. That means upper layer
	 *    nas not requested this connection , (less likely) we gave up
	 *    on this connection (timeout) or as node act as slave role.
	 *    The most likely scenario is that
	 *    we have received LE_Create_Connection command 
	 *    from the RAW hook
	 */

	if (con == NULL) {
		if (ep->status != 0)
			goto out;

		con = ng_hci_new_con(unit, link_type);
		if (con == NULL) {
			error = ENOMEM;
			goto out;
		}

		con->state = NG_HCI_CON_W4_LP_CON_RSP;
		ng_hci_con_timeout(con);

		bcopy(&ep->address, &con->bdaddr, sizeof(con->bdaddr));
		error = ng_hci_lp_con_ind(con, uclass);
		if (error != 0) {
			ng_hci_con_untimeout(con);
			ng_hci_free_con(con);
		}

	} else if ((error = ng_hci_con_untimeout(con)) != 0)
			goto out;

	/*
	 * Update connection descriptor and send notification 
	 * to the upper layers.
	 */

	con->con_handle = NG_HCI_CON_HANDLE(le16toh(ep->handle));
	con->encryption_mode = NG_HCI_ENCRYPTION_MODE_NONE;

	ng_hci_lp_con_cfm(con, ep->status);

	/* Adjust connection state */
	if (ep->status != 0)
		ng_hci_free_con(con);
	else {
		con->state = NG_HCI_CON_OPEN;

		/*	
		 * Change link policy for the ACL connections. Enable all 
		 * supported link modes. Enable Role switch as well if
		 * device supports it.
		 */

	}

out:
	NG_FREE_M(event);

	return (error);

}

static int le_connection_update(ng_hci_unit_p unit, struct mbuf *event)
{
	int error = 0;
	/*TBD*/
	
	NG_FREE_M(event);
	return error;

}
static int
le_event(ng_hci_unit_p unit, struct mbuf *event)
{
	int error = 0;
	ng_hci_le_ep *lep;

	NG_HCI_M_PULLUP(event, sizeof(*lep));
	if(event ==NULL){
		return ENOBUFS;
	}
	lep = mtod(event, ng_hci_le_ep *);
	m_adj(event, sizeof(*lep));
	switch(lep->subevent_code){
	case NG_HCI_LEEV_CON_COMPL:
		le_connection_complete(unit, event);
		break;
	case NG_HCI_LEEV_ADVREP:
		le_advertizing_report(unit, event);
		break;
	case NG_HCI_LEEV_CON_UPDATE_COMPL:
		le_connection_update(unit, event);
		break;
	case NG_HCI_LEEV_READ_REMOTE_FEATURES_COMPL:
		//TBD
	  /*FALLTHROUGH*/
	case NG_HCI_LEEV_LONG_TERM_KEY_REQUEST:
		//TBD
	  /*FALLTHROUGH*/
	default:
	  	NG_FREE_M(event);
	}
	return error;
}

/* Inquiry result event */
static int
inquiry_result(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_inquiry_result_ep	*ep = NULL;
	ng_hci_neighbor_p		 n = NULL;
	bdaddr_t			 bdaddr;
	int				 error = 0;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_inquiry_result_ep *);
	m_adj(event, sizeof(*ep));

	for (; ep->num_responses > 0; ep->num_responses --) {
		/* Get remote unit address */
		m_copydata(event, 0, sizeof(bdaddr), (caddr_t) &bdaddr);
		m_adj(event, sizeof(bdaddr));

		/* Lookup entry in the cache */
		n = ng_hci_get_neighbor(unit, &bdaddr, NG_HCI_LINK_ACL);
		if (n == NULL) {
			/* Create new entry */
			n = ng_hci_new_neighbor(unit);
			if (n == NULL) {
				error = ENOMEM;
				break;
			}
		} else
			getmicrotime(&n->updated);

		bcopy(&bdaddr, &n->bdaddr, sizeof(n->bdaddr));
		n->addrtype = NG_HCI_LINK_ACL;

		/* XXX call m_pullup here? */

		n->page_scan_rep_mode = *mtod(event, u_int8_t *);
		m_adj(event, sizeof(u_int8_t));

		/* page_scan_period_mode */
		m_adj(event, sizeof(u_int8_t));

		n->page_scan_mode = *mtod(event, u_int8_t *);
		m_adj(event, sizeof(u_int8_t));

		/* class */
		m_adj(event, NG_HCI_CLASS_SIZE);

		/* clock offset */
		m_copydata(event, 0, sizeof(n->clock_offset), 
			(caddr_t) &n->clock_offset);
		n->clock_offset = le16toh(n->clock_offset);
	}

	NG_FREE_M(event);

	return (error);
} /* inquiry_result */

/* Connection complete event */
static int
con_compl(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_con_compl_ep	*ep = NULL;
	ng_hci_unit_con_p	 con = NULL;
	int			 error = 0;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_con_compl_ep *);

	/*
	 * Find the first connection descriptor that matches the following:
	 *
	 * 1) con->link_type == ep->link_type
	 * 2) con->state == NG_HCI_CON_W4_CONN_COMPLETE
	 * 3) con->bdaddr == ep->bdaddr
	 */

	LIST_FOREACH(con, &unit->con_list, next)
		if (con->link_type == ep->link_type &&
		    con->state == NG_HCI_CON_W4_CONN_COMPLETE &&
		    bcmp(&con->bdaddr, &ep->bdaddr, sizeof(bdaddr_t)) == 0)
			break;

	/*
	 * Two possible cases:
	 *
	 * 1) We have found connection descriptor. That means upper layer has
	 *    requested this connection via LP_CON_REQ message. In this case
	 *    connection must have timeout set. If ng_hci_con_untimeout() fails
	 *    then timeout message already went into node's queue. In this case
	 *    ignore Connection_Complete event and let timeout deal with it.
	 *
	 * 2) We do not have connection descriptor. That means upper layer
	 *    nas not requested this connection or (less likely) we gave up
	 *    on this connection (timeout). The most likely scenario is that
	 *    we have received Create_Connection/Add_SCO_Connection command 
	 *    from the RAW hook
	 */

	if (con == NULL) {
		if (ep->status != 0)
			goto out;

		con = ng_hci_new_con(unit, ep->link_type);
		if (con == NULL) {
			error = ENOMEM;
			goto out;
		}

		bcopy(&ep->bdaddr, &con->bdaddr, sizeof(con->bdaddr));
	} else if ((error = ng_hci_con_untimeout(con)) != 0)
			goto out;

	/*
	 * Update connection descriptor and send notification 
	 * to the upper layers.
	 */

	con->con_handle = NG_HCI_CON_HANDLE(le16toh(ep->con_handle));
	con->encryption_mode = ep->encryption_mode;

	ng_hci_lp_con_cfm(con, ep->status);

	/* Adjust connection state */
	if (ep->status != 0)
		ng_hci_free_con(con);
	else {
		con->state = NG_HCI_CON_OPEN;

		/*	
		 * Change link policy for the ACL connections. Enable all 
		 * supported link modes. Enable Role switch as well if
		 * device supports it.
		 */

		if (ep->link_type == NG_HCI_LINK_ACL) {
			struct __link_policy {
				ng_hci_cmd_pkt_t			 hdr;
				ng_hci_write_link_policy_settings_cp	 cp;
			} __attribute__ ((packed))			*lp;
			struct mbuf					*m;

			MGETHDR(m, M_NOWAIT, MT_DATA);
			if (m != NULL) {
				m->m_pkthdr.len = m->m_len = sizeof(*lp);
				lp = mtod(m, struct __link_policy *);

				lp->hdr.type = NG_HCI_CMD_PKT;
				lp->hdr.opcode = htole16(NG_HCI_OPCODE(
					NG_HCI_OGF_LINK_POLICY,
					NG_HCI_OCF_WRITE_LINK_POLICY_SETTINGS));
				lp->hdr.length = sizeof(lp->cp);

				lp->cp.con_handle = ep->con_handle;

				lp->cp.settings = 0;
				if ((unit->features[0] & NG_HCI_LMP_SWITCH) &&
				    unit->role_switch)
					lp->cp.settings |= 0x1;
				if (unit->features[0] & NG_HCI_LMP_HOLD_MODE)
					lp->cp.settings |= 0x2;
				if (unit->features[0] & NG_HCI_LMP_SNIFF_MODE)
					lp->cp.settings |= 0x4;
				if (unit->features[1] & NG_HCI_LMP_PARK_MODE)
					lp->cp.settings |= 0x8;

				lp->cp.settings &= unit->link_policy_mask;
				lp->cp.settings = htole16(lp->cp.settings);

				NG_BT_MBUFQ_ENQUEUE(&unit->cmdq, m);
				if (!(unit->state & NG_HCI_UNIT_COMMAND_PENDING))
					ng_hci_send_command(unit);
			}
		}
	}
out:
	NG_FREE_M(event);

	return (error);
} /* con_compl */

/* Connection request event */
static int
con_req(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_con_req_ep	*ep = NULL;
	ng_hci_unit_con_p	 con = NULL;
	int			 error = 0;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_con_req_ep *);

	/*
	 * Find the first connection descriptor that matches the following:
	 *
	 * 1) con->link_type == ep->link_type
	 *
	 * 2) con->state == NG_HCI_CON_W4_LP_CON_RSP ||
	 *    con->state == NG_HCI_CON_W4_CONN_COMPL
	 * 
	 * 3) con->bdaddr == ep->bdaddr
	 *
	 * Possible cases:
	 *
	 * 1) We do not have connection descriptor. This is simple. Create
	 *    new fresh connection descriptor and send notification to the
	 *    appropriate upstream hook (based on link_type).
	 *
	 * 2) We found connection handle. This is more complicated.
	 * 
	 * 2.1) ACL links
	 *
	 *      Since only one ACL link can exist between each pair of
	 *      units then we have a race. Our upper layer has requested 
	 *      an ACL connection to the remote unit, but we did not send 
	 *      command yet. At the same time the remote unit has requested
	 *      an ACL connection from us. In this case we will ignore 
	 *	Connection_Request event. This probably will cause connect
	 *      failure	on both units.
	 *
	 * 2.2) SCO links
	 *
	 *      The spec on page 45 says :
	 *
	 *      "The master can support up to three SCO links to the same 
	 *       slave or to different slaves. A slave can support up to 
	 *       three SCO links from the same master, or two SCO links if 
	 *       the links originate from different masters."
	 *
	 *      The only problem is how to handle multiple SCO links between
	 *      matster and slave. For now we will assume that multiple SCO
	 *      links MUST be opened one after another. 
	 */

	LIST_FOREACH(con, &unit->con_list, next)
		if (con->link_type == ep->link_type &&
		    (con->state == NG_HCI_CON_W4_LP_CON_RSP ||
		     con->state == NG_HCI_CON_W4_CONN_COMPLETE) &&
		    bcmp(&con->bdaddr, &ep->bdaddr, sizeof(bdaddr_t)) == 0)
			break;

	if (con == NULL) {
		con = ng_hci_new_con(unit, ep->link_type);
		if (con != NULL) {
			bcopy(&ep->bdaddr, &con->bdaddr, sizeof(con->bdaddr));

			con->state = NG_HCI_CON_W4_LP_CON_RSP;
			ng_hci_con_timeout(con);

			error = ng_hci_lp_con_ind(con, ep->uclass);
			if (error != 0) {
				ng_hci_con_untimeout(con);
				ng_hci_free_con(con);
			}
		} else
			error = ENOMEM;
	}

	NG_FREE_M(event);

	return (error);
} /* con_req */

/* Disconnect complete event */
static int
discon_compl(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_discon_compl_ep	*ep = NULL;
	ng_hci_unit_con_p	 con = NULL;
	int			 error = 0;
	u_int16_t		 h;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_discon_compl_ep *);

	/* 
	 * XXX 
	 * Do we have to send notification if ep->status != 0? 
	 * For now we will send notification for both ACL and SCO connections
	 * ONLY if ep->status == 0.
	 */

	if (ep->status == 0) {
		h = NG_HCI_CON_HANDLE(le16toh(ep->con_handle));
		con = ng_hci_con_by_handle(unit, h);
		if (con != NULL) {
			error = ng_hci_lp_discon_ind(con, ep->reason);

			/* Remove all timeouts (if any) */
			if (con->flags & NG_HCI_CON_TIMEOUT_PENDING)
				ng_hci_con_untimeout(con);

			ng_hci_free_con(con);
		} else {
			NG_HCI_ALERT(
"%s: %s - invalid connection handle=%d\n",
				__func__, NG_NODE_NAME(unit->node), h);
			error = ENOENT;
		}
	}

	NG_FREE_M(event);

	return (error);
} /* discon_compl */

/* Encryption change event */
static int
encryption_change(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_encryption_change_ep	*ep = NULL;
	ng_hci_unit_con_p		 con = NULL;
	int				 error = 0;
	u_int16_t	h;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_encryption_change_ep *);
	h = NG_HCI_CON_HANDLE(le16toh(ep->con_handle));
	con = ng_hci_con_by_handle(unit, h);

	if (ep->status == 0) {
		if (con == NULL) {
			NG_HCI_ALERT(
"%s: %s - invalid connection handle=%d\n",
				__func__, NG_NODE_NAME(unit->node), h);
			error = ENOENT;
		} else if (con->link_type == NG_HCI_LINK_SCO) {
			NG_HCI_ALERT(
"%s: %s - invalid link type=%d\n",
				__func__, NG_NODE_NAME(unit->node), 
				con->link_type);
			error = EINVAL;
		} else if (ep->encryption_enable)
			/* XXX is that true? */
			con->encryption_mode = NG_HCI_ENCRYPTION_MODE_P2P;
		else
			con->encryption_mode = NG_HCI_ENCRYPTION_MODE_NONE;
	} else
		NG_HCI_ERR(
"%s: %s - failed to change encryption mode, status=%d\n",
			__func__, NG_NODE_NAME(unit->node), ep->status);

	/*Anyway, propagete encryption status to upper layer*/
	ng_hci_lp_enc_change(con, con->encryption_mode);

	NG_FREE_M(event);

	return (error);
} /* encryption_change */

/* Read remote feature complete event */
static int
read_remote_features_compl(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_read_remote_features_compl_ep	*ep = NULL;
	ng_hci_unit_con_p			 con = NULL;
	ng_hci_neighbor_p			 n = NULL;
	u_int16_t				 h;
	int					 error = 0;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_read_remote_features_compl_ep *);

	if (ep->status == 0) {
		/* Check if we have this connection handle */
		h = NG_HCI_CON_HANDLE(le16toh(ep->con_handle));
		con = ng_hci_con_by_handle(unit, h);
		if (con == NULL) {
			NG_HCI_ALERT(
"%s: %s - invalid connection handle=%d\n",
				__func__, NG_NODE_NAME(unit->node), h);
			error = ENOENT;
			goto out;
		}

		/* Update cache entry */
		n = ng_hci_get_neighbor(unit, &con->bdaddr, NG_HCI_LINK_ACL);
		if (n == NULL) {
			n = ng_hci_new_neighbor(unit);
			if (n == NULL) {
				error = ENOMEM;
				goto out;
			}

			bcopy(&con->bdaddr, &n->bdaddr, sizeof(n->bdaddr));
			n->addrtype = NG_HCI_LINK_ACL;
		} else
			getmicrotime(&n->updated);

		bcopy(ep->features, n->features, sizeof(n->features));
	} else
		NG_HCI_ERR(
"%s: %s - failed to read remote unit features, status=%d\n",
			__func__, NG_NODE_NAME(unit->node), ep->status);
out:
	NG_FREE_M(event);

	return (error);
} /* read_remote_features_compl */

/* QoS setup complete event */
static int
qos_setup_compl(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_qos_setup_compl_ep	*ep = NULL;
	ng_hci_unit_con_p		 con = NULL;
	u_int16_t			 h;
	int				 error = 0;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_qos_setup_compl_ep *);

	/* Check if we have this connection handle */
	h = NG_HCI_CON_HANDLE(le16toh(ep->con_handle));
	con = ng_hci_con_by_handle(unit, h);
	if (con == NULL) {
		NG_HCI_ALERT(
"%s: %s - invalid connection handle=%d\n",
			__func__, NG_NODE_NAME(unit->node), h);
		error = ENOENT;
	} else if (con->link_type != NG_HCI_LINK_ACL) {
		NG_HCI_ALERT(
"%s: %s - invalid link type=%d, handle=%d\n",
			__func__, NG_NODE_NAME(unit->node), con->link_type, h);
		error = EINVAL;
	} else if (con->state != NG_HCI_CON_OPEN) {
		NG_HCI_ALERT(
"%s: %s - invalid connection state=%d, handle=%d\n",
			__func__, NG_NODE_NAME(unit->node), 
			con->state, h);
		error = EINVAL;
	} else /* Notify upper layer */
		error = ng_hci_lp_qos_cfm(con, ep->status);

	NG_FREE_M(event);

	return (error);
} /* qos_setup_compl */

/* Hardware error event */
static int
hardware_error(ng_hci_unit_p unit, struct mbuf *event)
{
	NG_HCI_ALERT(
"%s: %s - hardware error %#x\n",
		__func__, NG_NODE_NAME(unit->node), *mtod(event, u_int8_t *));

	NG_FREE_M(event);

	return (0);
} /* hardware_error */

/* Role change event */
static int
role_change(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_role_change_ep	*ep = NULL;
	ng_hci_unit_con_p	 con = NULL;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_role_change_ep *);

	if (ep->status == 0) {
		/* XXX shoud we also change "role" for SCO connections? */
		con = ng_hci_con_by_bdaddr(unit, &ep->bdaddr, NG_HCI_LINK_ACL);
		if (con != NULL)
			con->role = ep->role;
		else
			NG_HCI_ALERT(
"%s: %s - ACL connection does not exist, bdaddr=%x:%x:%x:%x:%x:%x\n",
				__func__, NG_NODE_NAME(unit->node),
				ep->bdaddr.b[5], ep->bdaddr.b[4], 
				ep->bdaddr.b[3], ep->bdaddr.b[2], 
				ep->bdaddr.b[1], ep->bdaddr.b[0]);
	} else
		NG_HCI_ERR(
"%s: %s - failed to change role, status=%d, bdaddr=%x:%x:%x:%x:%x:%x\n",
			__func__, NG_NODE_NAME(unit->node), ep->status,
			ep->bdaddr.b[5], ep->bdaddr.b[4], ep->bdaddr.b[3],
			ep->bdaddr.b[2], ep->bdaddr.b[1], ep->bdaddr.b[0]);

	NG_FREE_M(event);

	return (0);
} /* role_change */

/* Number of completed packets event */
static int
num_compl_pkts(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_num_compl_pkts_ep	*ep = NULL;
	ng_hci_unit_con_p		 con = NULL;
	u_int16_t			 h, p;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_num_compl_pkts_ep *);
	m_adj(event, sizeof(*ep));

	for (; ep->num_con_handles > 0; ep->num_con_handles --) {
		/* Get connection handle */
		m_copydata(event, 0, sizeof(h), (caddr_t) &h);
		m_adj(event, sizeof(h));
		h = NG_HCI_CON_HANDLE(le16toh(h));

		/* Get number of completed packets */
		m_copydata(event, 0, sizeof(p), (caddr_t) &p);
		m_adj(event, sizeof(p));
		p = le16toh(p);

		/* Check if we have this connection handle */
		con = ng_hci_con_by_handle(unit, h);
		if (con != NULL) {
			con->pending -= p;
			if (con->pending < 0) {
				NG_HCI_WARN(
"%s: %s - pending packet counter is out of sync! " \
"handle=%d, pending=%d, ncp=%d\n",	__func__, NG_NODE_NAME(unit->node), 
					con->con_handle, con->pending, p);

				con->pending = 0;
			}

			/* Update buffer descriptor */
			if (con->link_type != NG_HCI_LINK_SCO)
				NG_HCI_BUFF_ACL_FREE(unit->buffer, p);
			else 
				NG_HCI_BUFF_SCO_FREE(unit->buffer, p);
		} else
			NG_HCI_ALERT(
"%s: %s - invalid connection handle=%d\n",
				__func__, NG_NODE_NAME(unit->node), h);
	}

	NG_FREE_M(event);

	/* Send more data */
	ng_hci_send_data(unit);

	return (0);
} /* num_compl_pkts */

/* Mode change event */
static int
mode_change(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_mode_change_ep	*ep = NULL;
	ng_hci_unit_con_p	 con = NULL;
	int			 error = 0;
	
	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_mode_change_ep *);

	if (ep->status == 0) {
		u_int16_t	h = NG_HCI_CON_HANDLE(le16toh(ep->con_handle));

		con = ng_hci_con_by_handle(unit, h);
		if (con == NULL) {
			NG_HCI_ALERT(
"%s: %s - invalid connection handle=%d\n",
				__func__, NG_NODE_NAME(unit->node), h);
			error = ENOENT;
		} else if (con->link_type != NG_HCI_LINK_ACL) {
			NG_HCI_ALERT(
"%s: %s - invalid link type=%d\n",
				__func__, NG_NODE_NAME(unit->node), 
				con->link_type);
			error = EINVAL;
		} else
			con->mode = ep->unit_mode;
	} else
		NG_HCI_ERR(
"%s: %s - failed to change mode, status=%d\n",
			__func__, NG_NODE_NAME(unit->node), ep->status);

	NG_FREE_M(event);

	return (error);
} /* mode_change */

/* Data buffer overflow event */
static int
data_buffer_overflow(ng_hci_unit_p unit, struct mbuf *event)
{
	NG_HCI_ALERT(
"%s: %s - %s data buffer overflow\n",
		__func__, NG_NODE_NAME(unit->node),
		(*mtod(event, u_int8_t *) == NG_HCI_LINK_ACL)? "ACL" : "SCO");

	NG_FREE_M(event);

	return (0);
} /* data_buffer_overflow */

/* Read clock offset complete event */
static int
read_clock_offset_compl(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_read_clock_offset_compl_ep	*ep = NULL;
	ng_hci_unit_con_p			 con = NULL;
	ng_hci_neighbor_p			 n = NULL;
	int					 error = 0;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_read_clock_offset_compl_ep *);

	if (ep->status == 0) {
		u_int16_t	h = NG_HCI_CON_HANDLE(le16toh(ep->con_handle));

		con = ng_hci_con_by_handle(unit, h);
		if (con == NULL) {
			NG_HCI_ALERT(
"%s: %s - invalid connection handle=%d\n",
				__func__, NG_NODE_NAME(unit->node), h);
			error = ENOENT;
			goto out;
		}

		/* Update cache entry */
		n = ng_hci_get_neighbor(unit, &con->bdaddr, NG_HCI_LINK_ACL);
		if (n == NULL) {
			n = ng_hci_new_neighbor(unit);
			if (n == NULL) {
				error = ENOMEM;
				goto out;
			}

			bcopy(&con->bdaddr, &n->bdaddr, sizeof(n->bdaddr));
			n->addrtype = NG_HCI_LINK_ACL;
		} else
			getmicrotime(&n->updated);

		n->clock_offset = le16toh(ep->clock_offset);
	} else
		NG_HCI_ERR(
"%s: %s - failed to Read Remote Clock Offset, status=%d\n",
			__func__, NG_NODE_NAME(unit->node), ep->status);
out:
	NG_FREE_M(event);

	return (error);
} /* read_clock_offset_compl */

/* QoS violation event */
static int
qos_violation(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_qos_violation_ep	*ep = NULL;
	ng_hci_unit_con_p	 con = NULL;
	u_int16_t		 h;
	int			 error = 0;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_qos_violation_ep *);

	/* Check if we have this connection handle */
	h = NG_HCI_CON_HANDLE(le16toh(ep->con_handle));
	con = ng_hci_con_by_handle(unit, h);
	if (con == NULL) {
		NG_HCI_ALERT(
"%s: %s - invalid connection handle=%d\n",
			__func__, NG_NODE_NAME(unit->node), h);
		error = ENOENT;
	} else if (con->link_type != NG_HCI_LINK_ACL) {
		NG_HCI_ALERT(
"%s: %s - invalid link type=%d\n",
			__func__, NG_NODE_NAME(unit->node), con->link_type);
		error = EINVAL;
	} else if (con->state != NG_HCI_CON_OPEN) {
		NG_HCI_ALERT(
"%s: %s - invalid connection state=%d, handle=%d\n",
			__func__, NG_NODE_NAME(unit->node), con->state, h);
		error = EINVAL;
	} else /* Notify upper layer */
		error = ng_hci_lp_qos_ind(con); 

	NG_FREE_M(event);

	return (error);
} /* qos_violation */

/* Page scan mode change event */
static int
page_scan_mode_change(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_page_scan_mode_change_ep	*ep = NULL;
	ng_hci_neighbor_p		 n = NULL;
	int				 error = 0;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_page_scan_mode_change_ep *);

	/* Update cache entry */
	n = ng_hci_get_neighbor(unit, &ep->bdaddr, NG_HCI_LINK_ACL);
	if (n == NULL) {
		n = ng_hci_new_neighbor(unit);
		if (n == NULL) {
			error = ENOMEM;
			goto out;
		}

		bcopy(&ep->bdaddr, &n->bdaddr, sizeof(n->bdaddr));
		n->addrtype = NG_HCI_LINK_ACL;
	} else
		getmicrotime(&n->updated);

	n->page_scan_mode = ep->page_scan_mode;
out:
	NG_FREE_M(event);

	return (error);
} /* page_scan_mode_change */

/* Page scan repetition mode change event */
static int
page_scan_rep_mode_change(ng_hci_unit_p unit, struct mbuf *event)
{
	ng_hci_page_scan_rep_mode_change_ep	*ep = NULL;
	ng_hci_neighbor_p			 n = NULL;
	int					 error = 0;

	NG_HCI_M_PULLUP(event, sizeof(*ep));
	if (event == NULL)
		return (ENOBUFS);

	ep = mtod(event, ng_hci_page_scan_rep_mode_change_ep *);

	/* Update cache entry */
	n = ng_hci_get_neighbor(unit, &ep->bdaddr, NG_HCI_LINK_ACL);
	if (n == NULL) {
		n = ng_hci_new_neighbor(unit);
		if (n == NULL) {
			error = ENOMEM;
			goto out;
		}

		bcopy(&ep->bdaddr, &n->bdaddr, sizeof(n->bdaddr));
		n->addrtype = NG_HCI_LINK_ACL;
	} else
		getmicrotime(&n->updated);

	n->page_scan_rep_mode = ep->page_scan_rep_mode;
out:
	NG_FREE_M(event);

	return (error);
} /* page_scan_rep_mode_change */


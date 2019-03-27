/*
 * ng_l2cap_evnt.c
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
 * $Id: ng_l2cap_evnt.c,v 1.5 2003/09/08 19:11:45 max Exp $
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
 **                    L2CAP events processing module
 ******************************************************************************
 ******************************************************************************/

static int ng_l2cap_process_signal_cmd (ng_l2cap_con_p);
static int ng_l2cap_process_lesignal_cmd (ng_l2cap_con_p);
static int ng_l2cap_process_cmd_rej    (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_cmd_urq    (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_cmd_urs    (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_con_req    (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_con_rsp    (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_cfg_req    (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_cfg_rsp    (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_discon_req (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_discon_rsp (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_echo_req   (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_echo_rsp   (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_info_req   (ng_l2cap_con_p, u_int8_t);
static int ng_l2cap_process_info_rsp   (ng_l2cap_con_p, u_int8_t);
static int send_l2cap_reject
	(ng_l2cap_con_p, u_int8_t, u_int16_t, u_int16_t, u_int16_t, u_int16_t);
static int send_l2cap_con_rej
	(ng_l2cap_con_p, u_int8_t, u_int16_t, u_int16_t, u_int16_t);
static int send_l2cap_cfg_rsp
	(ng_l2cap_con_p, u_int8_t, u_int16_t, u_int16_t, struct mbuf *);
static int send_l2cap_param_urs
       (ng_l2cap_con_p , u_int8_t , u_int16_t);

static int get_next_l2cap_opt
	(struct mbuf *, int *, ng_l2cap_cfg_opt_p, ng_l2cap_cfg_opt_val_p);

/*
 * Receive L2CAP packet. First get L2CAP header and verify packet. Than
 * get destination channel and process packet.
 */

int
ng_l2cap_receive(ng_l2cap_con_p con)
{
	ng_l2cap_p	 l2cap = con->l2cap;
	ng_l2cap_hdr_t	*hdr = NULL;
	int		 error = 0;

	/* Check packet */
	if (con->rx_pkt->m_pkthdr.len < sizeof(*hdr)) {
		NG_L2CAP_ERR(
"%s: %s - invalid L2CAP packet. Packet too small, len=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), 
			con->rx_pkt->m_pkthdr.len);
		error = EMSGSIZE;
		goto drop;
	}

	/* Get L2CAP header */
	NG_L2CAP_M_PULLUP(con->rx_pkt, sizeof(*hdr));
	if (con->rx_pkt == NULL)
		return (ENOBUFS);

	hdr = mtod(con->rx_pkt, ng_l2cap_hdr_t *);
	hdr->length = le16toh(hdr->length);
	hdr->dcid = le16toh(hdr->dcid);

	/* Check payload size */
	if (hdr->length != con->rx_pkt->m_pkthdr.len - sizeof(*hdr)) {
		NG_L2CAP_ERR(
"%s: %s - invalid L2CAP packet. Payload length mismatch, length=%d, len=%zd\n",
			__func__, NG_NODE_NAME(l2cap->node), hdr->length, 
			con->rx_pkt->m_pkthdr.len - sizeof(*hdr));
		error = EMSGSIZE;
		goto drop;
	}

	/* Process packet */
	switch (hdr->dcid) {
	case NG_L2CAP_SIGNAL_CID: /* L2CAP command */
		m_adj(con->rx_pkt, sizeof(*hdr));
		error = ng_l2cap_process_signal_cmd(con);
		break;
  	case NG_L2CAP_LESIGNAL_CID:
		m_adj(con->rx_pkt, sizeof(*hdr));
		error = ng_l2cap_process_lesignal_cmd(con);
		break;
	case NG_L2CAP_CLT_CID: /* Connectionless packet */
		error = ng_l2cap_l2ca_clt_receive(con);
		break;

	default: /* Data packet */
		error = ng_l2cap_l2ca_receive(con);
		break;
	}

	return (error);
drop:
	NG_FREE_M(con->rx_pkt);

	return (error);
} /* ng_l2cap_receive */

/*
 * Process L2CAP signaling command. We already know that destination channel ID
 * is 0x1 that means we have received signaling command from peer's L2CAP layer.
 * So get command header, decode and process it.
 *
 * XXX do we need to check signaling MTU here?
 */

static int
ng_l2cap_process_signal_cmd(ng_l2cap_con_p con)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	ng_l2cap_cmd_hdr_t	*hdr = NULL;
	struct mbuf		*m = NULL;

	while (con->rx_pkt != NULL) {
		/* Verify packet length */
		if (con->rx_pkt->m_pkthdr.len < sizeof(*hdr)) {
			NG_L2CAP_ERR(
"%s: %s - invalid L2CAP signaling command. Packet too small, len=%d\n",
				__func__, NG_NODE_NAME(l2cap->node),
				con->rx_pkt->m_pkthdr.len);
			NG_FREE_M(con->rx_pkt);

			return (EMSGSIZE);
		}

		/* Get signaling command */
		NG_L2CAP_M_PULLUP(con->rx_pkt, sizeof(*hdr));
		if (con->rx_pkt == NULL)
			return (ENOBUFS);

		hdr = mtod(con->rx_pkt, ng_l2cap_cmd_hdr_t *);
		hdr->length = le16toh(hdr->length);
		m_adj(con->rx_pkt, sizeof(*hdr));

		/* Verify command length */
		if (con->rx_pkt->m_pkthdr.len < hdr->length) {
			NG_L2CAP_ERR(
"%s: %s - invalid L2CAP signaling command, code=%#x, ident=%d. " \
"Invalid command length=%d, m_pkthdr.len=%d\n",
				__func__, NG_NODE_NAME(l2cap->node),
				hdr->code, hdr->ident, hdr->length,
				con->rx_pkt->m_pkthdr.len);
			NG_FREE_M(con->rx_pkt);

			return (EMSGSIZE);
		}

		/* Get the command, save the rest (if any) */
		if (con->rx_pkt->m_pkthdr.len > hdr->length)
			m = m_split(con->rx_pkt, hdr->length, M_NOWAIT);
		else
			m = NULL;

		/* Process command */
		switch (hdr->code) {
		case NG_L2CAP_CMD_REJ:
			ng_l2cap_process_cmd_rej(con, hdr->ident);
			break;

		case NG_L2CAP_CON_REQ:
			ng_l2cap_process_con_req(con, hdr->ident);
			break;

		case NG_L2CAP_CON_RSP:
			ng_l2cap_process_con_rsp(con, hdr->ident);
			break;

		case NG_L2CAP_CFG_REQ:
			ng_l2cap_process_cfg_req(con, hdr->ident);
			break;

		case NG_L2CAP_CFG_RSP:
			ng_l2cap_process_cfg_rsp(con, hdr->ident);
			break;

		case NG_L2CAP_DISCON_REQ:
			ng_l2cap_process_discon_req(con, hdr->ident);
			break;

		case NG_L2CAP_DISCON_RSP:
			ng_l2cap_process_discon_rsp(con, hdr->ident);
			break;

		case NG_L2CAP_ECHO_REQ:
			ng_l2cap_process_echo_req(con, hdr->ident);
			break;

		case NG_L2CAP_ECHO_RSP:
			ng_l2cap_process_echo_rsp(con, hdr->ident);
			break;

		case NG_L2CAP_INFO_REQ:
			ng_l2cap_process_info_req(con, hdr->ident);
			break;

		case NG_L2CAP_INFO_RSP:
			ng_l2cap_process_info_rsp(con, hdr->ident);
			break;

		default:
			NG_L2CAP_ERR(
"%s: %s - unknown L2CAP signaling command, code=%#x, ident=%d, length=%d\n",
				__func__, NG_NODE_NAME(l2cap->node),
				hdr->code, hdr->ident, hdr->length);

			/*
			 * Send L2CAP_CommandRej. Do not really care 
			 * about the result
			 */

			send_l2cap_reject(con, hdr->ident,
				NG_L2CAP_REJ_NOT_UNDERSTOOD, 0, 0, 0);
			NG_FREE_M(con->rx_pkt);
			break;
		}

		con->rx_pkt = m;
	}

	return (0);
} /* ng_l2cap_process_signal_cmd */
static int
ng_l2cap_process_lesignal_cmd(ng_l2cap_con_p con)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	ng_l2cap_cmd_hdr_t	*hdr = NULL;
	struct mbuf		*m = NULL;

	while (con->rx_pkt != NULL) {
		/* Verify packet length */
		if (con->rx_pkt->m_pkthdr.len < sizeof(*hdr)) {
			NG_L2CAP_ERR(
"%s: %s - invalid L2CAP signaling command. Packet too small, len=%d\n",
				__func__, NG_NODE_NAME(l2cap->node),
				con->rx_pkt->m_pkthdr.len);
			NG_FREE_M(con->rx_pkt);

			return (EMSGSIZE);
		}

		/* Get signaling command */
		NG_L2CAP_M_PULLUP(con->rx_pkt, sizeof(*hdr));
		if (con->rx_pkt == NULL)
			return (ENOBUFS);

		hdr = mtod(con->rx_pkt, ng_l2cap_cmd_hdr_t *);
		hdr->length = le16toh(hdr->length);
		m_adj(con->rx_pkt, sizeof(*hdr));

		/* Verify command length */
		if (con->rx_pkt->m_pkthdr.len < hdr->length) {
			NG_L2CAP_ERR(
"%s: %s - invalid L2CAP signaling command, code=%#x, ident=%d. " \
"Invalid command length=%d, m_pkthdr.len=%d\n",
				__func__, NG_NODE_NAME(l2cap->node),
				hdr->code, hdr->ident, hdr->length,
				con->rx_pkt->m_pkthdr.len);
			NG_FREE_M(con->rx_pkt);

			return (EMSGSIZE);
		}

		/* Get the command, save the rest (if any) */
		if (con->rx_pkt->m_pkthdr.len > hdr->length)
			m = m_split(con->rx_pkt, hdr->length, M_NOWAIT);
		else
			m = NULL;

		/* Process command */
		switch (hdr->code) {
		case NG_L2CAP_CMD_REJ:
			ng_l2cap_process_cmd_rej(con, hdr->ident);
			break;
		case NG_L2CAP_CMD_PARAM_UPDATE_REQUEST:
			ng_l2cap_process_cmd_urq(con, hdr->ident);
			break;
		case NG_L2CAP_CMD_PARAM_UPDATE_RESPONSE:
			ng_l2cap_process_cmd_urs(con, hdr->ident);
			break;
			

		default:
			NG_L2CAP_ERR(
"%s: %s - unknown L2CAP signaling command, code=%#x, ident=%d, length=%d\n",
				__func__, NG_NODE_NAME(l2cap->node),
				hdr->code, hdr->ident, hdr->length);

			/*
			 * Send L2CAP_CommandRej. Do not really care 
			 * about the result
			 */

			send_l2cap_reject(con, hdr->ident,
				NG_L2CAP_REJ_NOT_UNDERSTOOD, 0, 0, 0);
			NG_FREE_M(con->rx_pkt);
			break;
		}

		con->rx_pkt = m;
	}

	return (0);
} /* ng_l2cap_process_signal_cmd */
/*Update Paramater Request*/
static int ng_l2cap_process_cmd_urq(ng_l2cap_con_p con, uint8_t ident)
{
	/* We do not implement parameter negotiation for now. */
	send_l2cap_param_urs(con, ident, NG_L2CAP_UPDATE_PARAM_ACCEPT);
	NG_FREE_M(con->rx_pkt);
	return 0;
}

static int ng_l2cap_process_cmd_urs(ng_l2cap_con_p con, uint8_t ident)
{
	/* We only support master side yet .*/
	//send_l2cap_reject(con,ident ... );
	
	NG_FREE_M(con->rx_pkt);
	return 0;
}

/*
 * Process L2CAP_CommandRej command
 */

static int
ng_l2cap_process_cmd_rej(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	ng_l2cap_cmd_rej_cp	*cp = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;

	/* Get command parameters */
	NG_L2CAP_M_PULLUP(con->rx_pkt, sizeof(*cp));
	if (con->rx_pkt == NULL)
		return (ENOBUFS);

	cp = mtod(con->rx_pkt, ng_l2cap_cmd_rej_cp *);
	cp->reason = le16toh(cp->reason);

	/* Check if we have pending command descriptor */
	cmd = ng_l2cap_cmd_by_ident(con, ident);
	if (cmd != NULL) {
		/* If command timeout already happened then ignore reject */
		if (ng_l2cap_command_untimeout(cmd) != 0) {
			NG_FREE_M(con->rx_pkt);
			return (ETIMEDOUT);
		}

		ng_l2cap_unlink_cmd(cmd);

		switch (cmd->code) {
		case NG_L2CAP_CON_REQ:
			ng_l2cap_l2ca_con_rsp(cmd->ch,cmd->token,cp->reason,0);
			ng_l2cap_free_chan(cmd->ch);
			break;

		case NG_L2CAP_CFG_REQ:
			ng_l2cap_l2ca_cfg_rsp(cmd->ch, cmd->token, cp->reason);
			break;

		case NG_L2CAP_DISCON_REQ:
			ng_l2cap_l2ca_discon_rsp(cmd->ch,cmd->token,cp->reason);
			ng_l2cap_free_chan(cmd->ch); /* XXX free channel */
			break;

		case NG_L2CAP_ECHO_REQ:
			ng_l2cap_l2ca_ping_rsp(cmd->con, cmd->token,
				cp->reason, NULL);
			break;

		case NG_L2CAP_INFO_REQ:
			ng_l2cap_l2ca_get_info_rsp(cmd->con, cmd->token,
				cp->reason, NULL);
			break;

		default:
			NG_L2CAP_ALERT(
"%s: %s - unexpected L2CAP_CommandRej. Unexpected L2CAP command opcode=%d\n",
				__func__, NG_NODE_NAME(l2cap->node), cmd->code);
			break;
		}

		ng_l2cap_free_cmd(cmd);
	} else
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_CommandRej command. " \
"Requested ident does not exist, ident=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ident);

	NG_FREE_M(con->rx_pkt);

	return (0);
} /* ng_l2cap_process_cmd_rej */

/*
 * Process L2CAP_ConnectReq command
 */

static int
ng_l2cap_process_con_req(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	struct mbuf		*m = con->rx_pkt;
	ng_l2cap_con_req_cp	*cp = NULL;
	ng_l2cap_chan_p		 ch = NULL;
	int			 error = 0;
	u_int16_t		 dcid, psm;
	int idtype;
	
	/* Get command parameters */
	NG_L2CAP_M_PULLUP(m, sizeof(*cp));
	if (m == NULL)
		return (ENOBUFS);

	cp = mtod(m, ng_l2cap_con_req_cp *);
	psm = le16toh(cp->psm);
	dcid = le16toh(cp->scid);

	NG_FREE_M(m);
	con->rx_pkt = NULL;
	if(dcid == NG_L2CAP_ATT_CID)
		idtype = NG_L2CAP_L2CA_IDTYPE_ATT;
	else if(dcid == NG_L2CAP_SMP_CID)
		idtype = NG_L2CAP_L2CA_IDTYPE_SMP;
	else if( con->linktype != NG_HCI_LINK_ACL)
		idtype = NG_L2CAP_L2CA_IDTYPE_LE;
	else
		idtype = NG_L2CAP_L2CA_IDTYPE_BREDR;

	/*
	 * Create new channel and send L2CA_ConnectInd notification 
	 * to the upper layer protocol.
	 */

	ch = ng_l2cap_new_chan(l2cap, con, psm, idtype);

	if (ch == NULL)
		return (send_l2cap_con_rej(con, ident, 0, dcid,
				NG_L2CAP_NO_RESOURCES));

	/* Update channel IDs */
	ch->dcid = dcid;

	/* Sent L2CA_ConnectInd notification to the upper layer */
	ch->ident = ident;
	ch->state = NG_L2CAP_W4_L2CA_CON_RSP;

	error = ng_l2cap_l2ca_con_ind(ch);
	if (error != 0) {
		send_l2cap_con_rej(con, ident, ch->scid, dcid, 
			(error == ENOMEM)? NG_L2CAP_NO_RESOURCES :
				NG_L2CAP_PSM_NOT_SUPPORTED);
		ng_l2cap_free_chan(ch);
	}

	return (error);
} /* ng_l2cap_process_con_req */

/*
 * Process L2CAP_ConnectRsp command
 */

static int
ng_l2cap_process_con_rsp(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	struct mbuf		*m = con->rx_pkt;
	ng_l2cap_con_rsp_cp	*cp = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;
	u_int16_t		 scid, dcid, result, status;
	int			 error = 0;

	/* Get command parameters */
	NG_L2CAP_M_PULLUP(m, sizeof(*cp));
	if (m == NULL)
		return (ENOBUFS);

	cp = mtod(m, ng_l2cap_con_rsp_cp *);
	dcid = le16toh(cp->dcid);
	scid = le16toh(cp->scid);
	result = le16toh(cp->result);
	status = le16toh(cp->status);

	NG_FREE_M(m);
	con->rx_pkt = NULL;

	/* Check if we have pending command descriptor */
	cmd = ng_l2cap_cmd_by_ident(con, ident);
	if (cmd == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_ConnectRsp command. ident=%d, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ident, 
			con->con_handle);

		return (ENOENT);
	}

	/* Verify channel state, if invalid - do nothing */
	if (cmd->ch->state != NG_L2CAP_W4_L2CAP_CON_RSP) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_ConnectRsp. " \
"Invalid channel state, cid=%d, state=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), scid, 
			cmd->ch->state);
		goto reject;
	}

	/* Verify CIDs and send reject if does not match */
	if (cmd->ch->scid != scid) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_ConnectRsp. Channel IDs do not match, scid=%d(%d)\n",
			 __func__, NG_NODE_NAME(l2cap->node), cmd->ch->scid, 
			scid);
		goto reject;
	}

	/*
	 * Looks good. We got confirmation from our peer. Now process
	 * it. First disable RTX timer. Then check the result and send 
	 * notification to the upper layer. If command timeout already
	 * happened then ignore response.
	 */

	if ((error = ng_l2cap_command_untimeout(cmd)) != 0)
		return (error);

	if (result == NG_L2CAP_PENDING) {
		/*
		 * Our peer wants more time to complete connection. We shall 
		 * start ERTX timer and wait. Keep command in the list.
		 */

		cmd->ch->dcid = dcid;
		ng_l2cap_command_timeout(cmd, bluetooth_l2cap_ertx_timeout());

		error = ng_l2cap_l2ca_con_rsp(cmd->ch, cmd->token, 
				result, status);
		if (error != 0)
			ng_l2cap_free_chan(cmd->ch);
	} else {
		ng_l2cap_unlink_cmd(cmd);

		if (result == NG_L2CAP_SUCCESS) {
			/*
			 * Channel is open. Complete command and move to CONFIG
			 * state. Since we have sent positive confirmation we 
			 * expect to receive L2CA_Config request from the upper
			 * layer protocol.
			 */

			cmd->ch->dcid = dcid;
			cmd->ch->state = ((cmd->ch->scid == NG_L2CAP_ATT_CID)||
					  (cmd->ch->scid == NG_L2CAP_SMP_CID))
					  ?
			  NG_L2CAP_OPEN : NG_L2CAP_CONFIG;
		} else
			/* There was an error, so close the channel */
			NG_L2CAP_INFO(
"%s: %s - failed to open L2CAP channel, result=%d, status=%d\n",
				__func__, NG_NODE_NAME(l2cap->node), result, 
				status);

		error = ng_l2cap_l2ca_con_rsp(cmd->ch, cmd->token, 
				result, status);

		/* XXX do we have to remove the channel on error? */
		if (error != 0 || result != NG_L2CAP_SUCCESS)
			ng_l2cap_free_chan(cmd->ch);

		ng_l2cap_free_cmd(cmd);
	}

	return (error);

reject:
	/* Send reject. Do not really care about the result */
	send_l2cap_reject(con, ident, NG_L2CAP_REJ_INVALID_CID, 0, scid, dcid);

	return (0);
} /* ng_l2cap_process_con_rsp */

/*
 * Process L2CAP_ConfigReq command
 */

static int
ng_l2cap_process_cfg_req(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	struct mbuf		*m = con->rx_pkt;
	ng_l2cap_cfg_req_cp	*cp = NULL;
	ng_l2cap_chan_p		 ch = NULL;
	u_int16_t		 dcid, respond, result;
	ng_l2cap_cfg_opt_t	 hdr;
	ng_l2cap_cfg_opt_val_t	 val;
	int			 off, error = 0;

	/* Get command parameters */
	con->rx_pkt = NULL;
	NG_L2CAP_M_PULLUP(m, sizeof(*cp));
	if (m == NULL)
		return (ENOBUFS);

	cp = mtod(m, ng_l2cap_cfg_req_cp *);
	dcid = le16toh(cp->dcid);
	respond = NG_L2CAP_OPT_CFLAG(le16toh(cp->flags));
	m_adj(m, sizeof(*cp));

	/* Check if we have this channel and it is in valid state */
	ch = ng_l2cap_chan_by_scid(l2cap, dcid, NG_L2CAP_L2CA_IDTYPE_BREDR);
	if (ch == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_ConfigReq command. " \
"Channel does not exist, cid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), dcid);
		goto reject;
	}

	/* Verify channel state */
	if (ch->state != NG_L2CAP_CONFIG && ch->state != NG_L2CAP_OPEN) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_ConfigReq. " \
"Invalid channel state, cid=%d, state=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), dcid, ch->state);
		goto reject;
	}

	if (ch->state == NG_L2CAP_OPEN) { /* Re-configuration */
		ch->cfg_state = 0;
		ch->state = NG_L2CAP_CONFIG;
	}

	for (result = 0, off = 0; ; ) {
		error = get_next_l2cap_opt(m, &off, &hdr, &val);
		if (error == 0) { /* We done with this packet */
			NG_FREE_M(m);
			break;
		} else if (error > 0) { /* Got option */
			switch (hdr.type) {
			case NG_L2CAP_OPT_MTU:
				ch->omtu = val.mtu;
				break;

			case NG_L2CAP_OPT_FLUSH_TIMO:
				ch->flush_timo = val.flush_timo;
				break;

			case NG_L2CAP_OPT_QOS:
				bcopy(&val.flow, &ch->iflow, sizeof(ch->iflow));
				break;

			default: /* Ignore unknown hint option */
				break;
			}
		} else { /* Oops, something is wrong */
			respond = 1;

			if (error == -3) {

				/*
				 * Adjust mbuf so we can get to the start
				 * of the first option we did not like.
				 */

				m_adj(m, off - sizeof(hdr));
				m->m_pkthdr.len = sizeof(hdr) + hdr.length;

				result = NG_L2CAP_UNKNOWN_OPTION;
			} else {
				/* XXX FIXME Send other reject codes? */
				NG_FREE_M(m);
				result = NG_L2CAP_REJECT;
			}

			break;
		}
	}

	/*
	 * Now check and see if we have to respond. If everything was OK then 
	 * respond contain "C flag" and (if set) we will respond with empty 
	 * packet and will wait for more options. 
	 * 
	 * Other case is that we did not like peer's options and will respond 
	 * with L2CAP_Config response command with Reject error code. 
	 * 
	 * When "respond == 0" than we have received all options and we will 
	 * sent L2CA_ConfigInd event to the upper layer protocol.
	 */

	if (respond) {
		error = send_l2cap_cfg_rsp(con, ident, ch->dcid, result, m);
		if (error != 0) {
			ng_l2cap_l2ca_discon_ind(ch);
			ng_l2cap_free_chan(ch);
		}
	} else {
		/* Send L2CA_ConfigInd event to the upper layer protocol */
		ch->ident = ident;
		error = ng_l2cap_l2ca_cfg_ind(ch);
		if (error != 0)
			ng_l2cap_free_chan(ch);
	}

	return (error);

reject:
	/* Send reject. Do not really care about the result */
	NG_FREE_M(m);

	send_l2cap_reject(con, ident, NG_L2CAP_REJ_INVALID_CID, 0, 0, dcid);

	return (0);
} /* ng_l2cap_process_cfg_req */

/*
 * Process L2CAP_ConfigRsp command
 */

static int
ng_l2cap_process_cfg_rsp(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	struct mbuf		*m = con->rx_pkt;
	ng_l2cap_cfg_rsp_cp	*cp = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;
	u_int16_t		 scid, cflag, result;
	ng_l2cap_cfg_opt_t	 hdr;
	ng_l2cap_cfg_opt_val_t	 val;
	int			 off, error = 0;

	/* Get command parameters */
	con->rx_pkt = NULL;
	NG_L2CAP_M_PULLUP(m, sizeof(*cp));
	if (m == NULL)
		return (ENOBUFS);

	cp = mtod(m, ng_l2cap_cfg_rsp_cp *);
	scid = le16toh(cp->scid);
	cflag = NG_L2CAP_OPT_CFLAG(le16toh(cp->flags));
	result = le16toh(cp->result);
	m_adj(m, sizeof(*cp));

	/* Check if we have this command */
	cmd = ng_l2cap_cmd_by_ident(con, ident);
	if (cmd == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_ConfigRsp command. ident=%d, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ident, 
			con->con_handle);
		NG_FREE_M(m);

		return (ENOENT);
	}

	/* Verify CIDs and send reject if does not match */
	if (cmd->ch->scid != scid) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_ConfigRsp. " \
"Channel ID does not match, scid=%d(%d)\n",
			__func__, NG_NODE_NAME(l2cap->node), cmd->ch->scid, 
			scid);
		goto reject;
	}

	/* Verify channel state and reject if invalid */
	if (cmd->ch->state != NG_L2CAP_CONFIG) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_ConfigRsp. " \
"Invalid channel state, scid=%d, state=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), cmd->ch->scid,
			cmd->ch->state);
		goto reject;
	}

	/*
	 * Looks like it is our response, so process it. First parse options,
	 * then verify C flag. If it is set then we shall expect more 
	 * configuration options from the peer and we will wait. Otherwise we 
	 * have received all options and we will send L2CA_ConfigRsp event to
	 * the upper layer protocol. If command timeout already happened then
	 * ignore response.
	 */

	if ((error = ng_l2cap_command_untimeout(cmd)) != 0) {
		NG_FREE_M(m);
		return (error);
	}

	for (off = 0; ; ) {
		error = get_next_l2cap_opt(m, &off, &hdr, &val); 
		if (error == 0) /* We done with this packet */
			break;
		else if (error > 0) { /* Got option */
			switch (hdr.type) {
			case NG_L2CAP_OPT_MTU:
				cmd->ch->imtu = val.mtu;
			break;

			case NG_L2CAP_OPT_FLUSH_TIMO:
				cmd->ch->flush_timo = val.flush_timo;
				break;

			case NG_L2CAP_OPT_QOS:
				bcopy(&val.flow, &cmd->ch->oflow,
					sizeof(cmd->ch->oflow));
			break;

			default: /* Ignore unknown hint option */
				break;
			}
		} else {
			/*
			 * XXX FIXME What to do here?
			 *
			 * This is really BAD :( options packet was broken, or 
			 * peer sent us option that we did not understand. Let 
			 * upper layer know and do not wait for more options.
			 */

			NG_L2CAP_ALERT(
"%s: %s - failed to parse configuration options, error=%d\n", 
				__func__, NG_NODE_NAME(l2cap->node), error);

			result = NG_L2CAP_UNKNOWN;
			cflag = 0;

			break;
		}
	}

	NG_FREE_M(m);

	if (cflag) /* Restart timer and wait for more options */
		ng_l2cap_command_timeout(cmd, bluetooth_l2cap_rtx_timeout());
	else {
		ng_l2cap_unlink_cmd(cmd);

		/* Send L2CA_Config response to the upper layer protocol */
		error = ng_l2cap_l2ca_cfg_rsp(cmd->ch, cmd->token, result);
		if (error != 0) {
			/*
			 * XXX FIXME what to do here? we were not able to send
			 * response to the upper layer protocol, so for now 
			 * just close the channel. Send L2CAP_Disconnect to 
			 * remote peer?
			 */

			NG_L2CAP_ERR(
"%s: %s - failed to send L2CA_Config response, error=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), error);

			ng_l2cap_free_chan(cmd->ch);
		}

		ng_l2cap_free_cmd(cmd);
	}

	return (error);

reject:
	/* Send reject. Do not really care about the result */
	NG_FREE_M(m);

	send_l2cap_reject(con, ident, NG_L2CAP_REJ_INVALID_CID, 0, scid, 0);

	return (0);
} /* ng_l2cap_process_cfg_rsp */

/*
 * Process L2CAP_DisconnectReq command
 */

static int
ng_l2cap_process_discon_req(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	ng_l2cap_discon_req_cp	*cp = NULL;
	ng_l2cap_chan_p		 ch = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;
	u_int16_t		 scid, dcid;

	/* Get command parameters */
	NG_L2CAP_M_PULLUP(con->rx_pkt, sizeof(*cp));
	if (con->rx_pkt == NULL)
		return (ENOBUFS);

	cp = mtod(con->rx_pkt, ng_l2cap_discon_req_cp *);
	dcid = le16toh(cp->dcid);
	scid = le16toh(cp->scid);

	NG_FREE_M(con->rx_pkt);

	/* Check if we have this channel and it is in valid state */
	ch = ng_l2cap_chan_by_scid(l2cap, dcid, NG_L2CAP_L2CA_IDTYPE_BREDR);
	if (ch == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_DisconnectReq message. " \
"Channel does not exist, cid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), dcid);
		goto reject;
	}

	/* XXX Verify channel state and reject if invalid -- is that true? */
	if (ch->state != NG_L2CAP_OPEN && ch->state != NG_L2CAP_CONFIG &&
	    ch->state != NG_L2CAP_W4_L2CAP_DISCON_RSP) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_DisconnectReq. " \
"Invalid channel state, cid=%d, state=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), dcid, ch->state);
		goto reject;
	}

	/* Match destination channel ID */
	if (ch->dcid != scid || ch->scid != dcid) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_DisconnectReq. " \
"Channel IDs does not match, channel: scid=%d, dcid=%d, " \
"request: scid=%d, dcid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->scid, ch->dcid,
			scid, dcid);
		goto reject;
	}

	/*
	 * Looks good, so notify upper layer protocol that channel is about 
	 * to be disconnected and send L2CA_DisconnectInd message. Then respond
	 * with L2CAP_DisconnectRsp.
	 */

	if (ch->state != NG_L2CAP_W4_L2CAP_DISCON_RSP) {
		ng_l2cap_l2ca_discon_ind(ch); /* do not care about result */
		ng_l2cap_free_chan(ch);
	}

	/* Send L2CAP_DisconnectRsp */
	cmd = ng_l2cap_new_cmd(con, NULL, ident, NG_L2CAP_DISCON_RSP, 0);
	if (cmd == NULL)
		return (ENOMEM);

	_ng_l2cap_discon_rsp(cmd->aux, ident, dcid, scid);
	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);

		return (ENOBUFS);
	}

	/* Link command to the queue */
	ng_l2cap_link_cmd(con, cmd);
	ng_l2cap_lp_deliver(con);

	return (0);

reject:
	/* Send reject. Do not really care about the result */
	send_l2cap_reject(con, ident, NG_L2CAP_REJ_INVALID_CID, 0, scid, dcid);

	return (0);
} /* ng_l2cap_process_discon_req */

/*
 * Process L2CAP_DisconnectRsp command
 */

static int
ng_l2cap_process_discon_rsp(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	ng_l2cap_discon_rsp_cp	*cp = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;
	u_int16_t		 scid, dcid;
	int			 error = 0;

	/* Get command parameters */
	NG_L2CAP_M_PULLUP(con->rx_pkt, sizeof(*cp));
	if (con->rx_pkt == NULL)
		return (ENOBUFS);

	cp = mtod(con->rx_pkt, ng_l2cap_discon_rsp_cp *);
	dcid = le16toh(cp->dcid);
	scid = le16toh(cp->scid);

	NG_FREE_M(con->rx_pkt);

	/* Check if we have pending command descriptor */
	cmd = ng_l2cap_cmd_by_ident(con, ident);
	if (cmd == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_DisconnectRsp command. ident=%d, con_handle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ident, 
			con->con_handle);
		goto out;
	}

	/* Verify channel state, do nothing if invalid */
	if (cmd->ch->state != NG_L2CAP_W4_L2CAP_DISCON_RSP) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_DisconnectRsp. " \
"Invalid channel state, cid=%d, state=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), scid,
			cmd->ch->state);
		goto out;
	}

	/* Verify CIDs and send reject if does not match */
	if (cmd->ch->scid != scid || cmd->ch->dcid != dcid) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_DisconnectRsp. " \
"Channel IDs do not match, scid=%d(%d), dcid=%d(%d)\n",
			__func__, NG_NODE_NAME(l2cap->node), cmd->ch->scid, 
			scid, cmd->ch->dcid, dcid);
		goto out;
	}

	/*
	 * Looks like we have successfully disconnected channel, so notify 
	 * upper layer. If command timeout already happened then ignore
	 * response.
	 */

	if ((error = ng_l2cap_command_untimeout(cmd)) != 0)
		goto out;

	error = ng_l2cap_l2ca_discon_rsp(cmd->ch, cmd->token, NG_L2CAP_SUCCESS);
	ng_l2cap_free_chan(cmd->ch); /* this will free commands too */
out:
	return (error);
} /* ng_l2cap_process_discon_rsp */

/*
 * Process L2CAP_EchoReq command
 */

static int
ng_l2cap_process_echo_req(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	ng_l2cap_cmd_hdr_t	*hdr = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;

	con->rx_pkt = ng_l2cap_prepend(con->rx_pkt, sizeof(*hdr));
	if (con->rx_pkt == NULL) {
		NG_L2CAP_ALERT(
"%s: %s - ng_l2cap_prepend() failed, size=%zd\n",
			__func__, NG_NODE_NAME(l2cap->node), sizeof(*hdr));

		return (ENOBUFS);
	}

	hdr = mtod(con->rx_pkt, ng_l2cap_cmd_hdr_t *);
	hdr->code = NG_L2CAP_ECHO_RSP;
	hdr->ident = ident;
	hdr->length = htole16(con->rx_pkt->m_pkthdr.len - sizeof(*hdr));

	cmd = ng_l2cap_new_cmd(con, NULL, ident, NG_L2CAP_ECHO_RSP, 0);
	if (cmd == NULL) {
		NG_FREE_M(con->rx_pkt);

		return (ENOBUFS);
	}

	/* Attach data and link command to the queue */
	cmd->aux = con->rx_pkt;
	con->rx_pkt = NULL;
	ng_l2cap_link_cmd(con, cmd);
	ng_l2cap_lp_deliver(con);

	return (0);
} /* ng_l2cap_process_echo_req */

/*
 * Process L2CAP_EchoRsp command
 */

static int
ng_l2cap_process_echo_rsp(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p	l2cap = con->l2cap;
	ng_l2cap_cmd_p	cmd = NULL;
	int		error = 0;

	/* Check if we have this command */
	cmd = ng_l2cap_cmd_by_ident(con, ident);
	if (cmd != NULL) {
		/* If command timeout already happened then ignore response */
		if ((error = ng_l2cap_command_untimeout(cmd)) != 0) {
			NG_FREE_M(con->rx_pkt);
			return (error);
		}

		ng_l2cap_unlink_cmd(cmd);

		error = ng_l2cap_l2ca_ping_rsp(cmd->con, cmd->token,
				NG_L2CAP_SUCCESS, con->rx_pkt);

		ng_l2cap_free_cmd(cmd);
		con->rx_pkt = NULL;
	} else {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_EchoRsp command. " \
"Requested ident does not exist, ident=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ident);
		NG_FREE_M(con->rx_pkt);
	}

	return (error);
} /* ng_l2cap_process_echo_rsp */

/*
 * Process L2CAP_InfoReq command
 */

static int
ng_l2cap_process_info_req(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p	l2cap = con->l2cap;
	ng_l2cap_cmd_p	cmd = NULL;
	u_int16_t	type;

	/* Get command parameters */
	NG_L2CAP_M_PULLUP(con->rx_pkt, sizeof(ng_l2cap_info_req_cp));
	if (con->rx_pkt == NULL)
		return (ENOBUFS);

	type = le16toh(mtod(con->rx_pkt, ng_l2cap_info_req_cp *)->type);
	NG_FREE_M(con->rx_pkt);

	cmd = ng_l2cap_new_cmd(con, NULL, ident, NG_L2CAP_INFO_RSP, 0);
	if (cmd == NULL)
		return (ENOMEM);

	switch (type) {
	case NG_L2CAP_CONNLESS_MTU:
		_ng_l2cap_info_rsp(cmd->aux, ident, NG_L2CAP_CONNLESS_MTU,
				NG_L2CAP_SUCCESS, NG_L2CAP_MTU_DEFAULT);
		break;

	default:
		_ng_l2cap_info_rsp(cmd->aux, ident, type,
				NG_L2CAP_NOT_SUPPORTED, 0);
		break;
	}

	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);

		return (ENOBUFS);
	}

	/* Link command to the queue */
	ng_l2cap_link_cmd(con, cmd);
	ng_l2cap_lp_deliver(con);

	return (0);
} /* ng_l2cap_process_info_req */

/*
 * Process L2CAP_InfoRsp command
 */

static int
ng_l2cap_process_info_rsp(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	ng_l2cap_info_rsp_cp	*cp = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;
	int			 error = 0;

	/* Get command parameters */
	NG_L2CAP_M_PULLUP(con->rx_pkt, sizeof(*cp));
	if (con->rx_pkt == NULL)
		return (ENOBUFS);

	cp = mtod(con->rx_pkt, ng_l2cap_info_rsp_cp *);
	cp->type = le16toh(cp->type);
	cp->result = le16toh(cp->result);
	m_adj(con->rx_pkt, sizeof(*cp));

	/* Check if we have pending command descriptor */
	cmd = ng_l2cap_cmd_by_ident(con, ident);
	if (cmd == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP_InfoRsp command. " \
"Requested ident does not exist, ident=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ident);
		NG_FREE_M(con->rx_pkt);

		return (ENOENT);
	}
	
	/* If command timeout already happened then ignore response */
	if ((error = ng_l2cap_command_untimeout(cmd)) != 0) {
		NG_FREE_M(con->rx_pkt);
		return (error);
	}

	ng_l2cap_unlink_cmd(cmd);

	if (cp->result == NG_L2CAP_SUCCESS) {
		switch (cp->type) {
		case NG_L2CAP_CONNLESS_MTU:
	    		if (con->rx_pkt->m_pkthdr.len == sizeof(u_int16_t))
				*mtod(con->rx_pkt, u_int16_t *) = 
					le16toh(*mtod(con->rx_pkt,u_int16_t *));
			else {
				cp->result = NG_L2CAP_UNKNOWN; /* XXX */

				NG_L2CAP_ERR(
"%s: %s - invalid L2CAP_InfoRsp command. " \
"Bad connectionless MTU parameter, len=%d\n",
					__func__, NG_NODE_NAME(l2cap->node),
					con->rx_pkt->m_pkthdr.len);
			}
			break;

		default:
			NG_L2CAP_WARN(
"%s: %s - invalid L2CAP_InfoRsp command. Unknown info type=%d\n",
				__func__, NG_NODE_NAME(l2cap->node), cp->type);
			break;
		}
	}

	error = ng_l2cap_l2ca_get_info_rsp(cmd->con, cmd->token,
			cp->result, con->rx_pkt);

	ng_l2cap_free_cmd(cmd);
	con->rx_pkt = NULL;

	return (error);
} /* ng_l2cap_process_info_rsp */

/*
 * Send L2CAP reject
 */

static int
send_l2cap_reject(ng_l2cap_con_p con, u_int8_t ident, u_int16_t reason,
		u_int16_t mtu, u_int16_t scid, u_int16_t dcid)
{
	ng_l2cap_cmd_p	cmd = NULL;

	cmd = ng_l2cap_new_cmd(con, NULL, ident, NG_L2CAP_CMD_REJ, 0);
	if (cmd == NULL)
		return (ENOMEM);

	 _ng_l2cap_cmd_rej(cmd->aux, cmd->ident, reason, mtu, scid, dcid);
	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);

		return (ENOBUFS);
	}

	/* Link command to the queue */
	ng_l2cap_link_cmd(con, cmd);
	ng_l2cap_lp_deliver(con);

	return (0);
} /* send_l2cap_reject */

/*
 * Send L2CAP connection reject
 */

static int
send_l2cap_con_rej(ng_l2cap_con_p con, u_int8_t ident, u_int16_t scid,
		u_int16_t dcid, u_int16_t result)
{
	ng_l2cap_cmd_p	cmd = NULL;

	cmd = ng_l2cap_new_cmd(con, NULL, ident, NG_L2CAP_CON_RSP, 0);
	if (cmd == NULL)
		return (ENOMEM);

	_ng_l2cap_con_rsp(cmd->aux, cmd->ident, scid, dcid, result, 0);
	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);

		return (ENOBUFS);
	}
	
	/* Link command to the queue */
	ng_l2cap_link_cmd(con, cmd);
	ng_l2cap_lp_deliver(con);

	return (0);
} /* send_l2cap_con_rej */

/*
 * Send L2CAP config response
 */

static int 
send_l2cap_cfg_rsp(ng_l2cap_con_p con, u_int8_t ident, u_int16_t scid,
		u_int16_t result, struct mbuf *opt)
{
	ng_l2cap_cmd_p	cmd = NULL;

	cmd = ng_l2cap_new_cmd(con, NULL, ident, NG_L2CAP_CFG_RSP, 0);
	if (cmd == NULL) {
		NG_FREE_M(opt);

		return (ENOMEM);
	}

	_ng_l2cap_cfg_rsp(cmd->aux, cmd->ident, scid, 0, result, opt);
	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);

		return (ENOBUFS);
	}

	/* Link command to the queue */
	ng_l2cap_link_cmd(con, cmd);
	ng_l2cap_lp_deliver(con);

	return (0);
} /* send_l2cap_cfg_rsp */

static int 
send_l2cap_param_urs(ng_l2cap_con_p con, u_int8_t ident,
		     u_int16_t result)
{
	ng_l2cap_cmd_p	cmd = NULL;

	cmd = ng_l2cap_new_cmd(con, NULL, ident,
			       NG_L2CAP_CMD_PARAM_UPDATE_RESPONSE,
			       0);
	if (cmd == NULL) {

		return (ENOMEM);
	}

	_ng_l2cap_cmd_urs(cmd->aux, cmd->ident, result);
	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);

		return (ENOBUFS);
	}

	/* Link command to the queue */
	ng_l2cap_link_cmd(con, cmd);
	ng_l2cap_lp_deliver(con);

	return (0);
} /* send_l2cap_cfg_rsp */

/*
 * Get next L2CAP configuration option
 *
 * Return codes:
 *  0   no option
 *  1   we have got option
 * -1   header too short
 * -2   bad option value or length
 * -3   unknown option
 */

static int
get_next_l2cap_opt(struct mbuf *m, int *off, ng_l2cap_cfg_opt_p hdr,
		ng_l2cap_cfg_opt_val_p val)
{
	int	hint, len = m->m_pkthdr.len - (*off);

	if (len == 0)
		return (0);
	if (len < 0 || len < sizeof(*hdr))
		return (-1);

	m_copydata(m, *off, sizeof(*hdr), (caddr_t) hdr);
	*off += sizeof(*hdr);
	len  -= sizeof(*hdr);

	hint = NG_L2CAP_OPT_HINT(hdr->type);
	hdr->type &= NG_L2CAP_OPT_HINT_MASK;

	switch (hdr->type) {
	case NG_L2CAP_OPT_MTU:
		if (hdr->length != NG_L2CAP_OPT_MTU_SIZE || len < hdr->length)
			return (-2);

		m_copydata(m, *off, NG_L2CAP_OPT_MTU_SIZE, (caddr_t) val);
		val->mtu = le16toh(val->mtu);
		*off += NG_L2CAP_OPT_MTU_SIZE;
		break;

	case NG_L2CAP_OPT_FLUSH_TIMO:
		if (hdr->length != NG_L2CAP_OPT_FLUSH_TIMO_SIZE || 
		    len < hdr->length)
			return (-2);

		m_copydata(m, *off, NG_L2CAP_OPT_FLUSH_TIMO_SIZE, (caddr_t)val);
		val->flush_timo = le16toh(val->flush_timo);
		*off += NG_L2CAP_OPT_FLUSH_TIMO_SIZE;
		break;

	case NG_L2CAP_OPT_QOS:
		if (hdr->length != NG_L2CAP_OPT_QOS_SIZE || len < hdr->length)
			return (-2);

		m_copydata(m, *off, NG_L2CAP_OPT_QOS_SIZE, (caddr_t) val);
		val->flow.token_rate = le32toh(val->flow.token_rate);
		val->flow.token_bucket_size = 
				le32toh(val->flow.token_bucket_size);
		val->flow.peak_bandwidth = le32toh(val->flow.peak_bandwidth);
		val->flow.latency = le32toh(val->flow.latency);
		val->flow.delay_variation = le32toh(val->flow.delay_variation);
		*off += NG_L2CAP_OPT_QOS_SIZE;
		break;

	default:
		if (hint)
			*off += hdr->length;
		else
			return (-3);
		break;
	}

	return (1);
} /* get_next_l2cap_opt */


/*
 * ng_l2cap_ulpi.c
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
 * $Id: ng_l2cap_ulpi.c,v 1.1 2002/11/24 19:47:06 max Exp $
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
 **                 Upper Layer Protocol Interface module
 ******************************************************************************
 ******************************************************************************/

/*
 * Process L2CA_Connect request from the upper layer protocol.
 */

int
ng_l2cap_l2ca_con_req(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_l2cap_l2ca_con_ip	*ip = NULL;
	ng_l2cap_con_p		 con = NULL;
	ng_l2cap_chan_p		 ch = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;
	int			 error = 0;

	/* Check message */
	if (msg->header.arglen != sizeof(*ip)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid L2CA_Connect request message size, size=%d\n",
			__func__, NG_NODE_NAME(l2cap->node),
			msg->header.arglen);
		error = EMSGSIZE;
		goto out;
	}

	ip = (ng_l2cap_l2ca_con_ip *)(msg->data);

	/* Check if we have connection to the remote unit */
	con = ng_l2cap_con_by_addr(l2cap, &ip->bdaddr, ip->linktype);
	if (con == NULL) {
		/* Submit LP_ConnectReq to the lower layer */
		error = ng_l2cap_lp_con_req(l2cap, &ip->bdaddr,ip->linktype);
		if (error != 0) {
			NG_L2CAP_ERR(
"%s: %s - unable to send LP_ConnectReq message, error=%d\n",
				__func__, NG_NODE_NAME(l2cap->node), error);
			goto out;
		}

		/* This should not fail */
		con = ng_l2cap_con_by_addr(l2cap, &ip->bdaddr, ip->linktype);
		KASSERT((con != NULL),
("%s: %s - could not find connection!\n", __func__, NG_NODE_NAME(l2cap->node)));
	}

	/*
	 * Create new empty channel descriptor. In case of any failure do 
	 * not touch connection descriptor.
	 */

	ch = ng_l2cap_new_chan(l2cap, con, ip->psm, ip->idtype);
	if (ch == NULL) {
		error = ENOMEM;
		goto out;
	}

	/* Now create L2CAP_ConnectReq command */
	cmd = ng_l2cap_new_cmd(ch->con, ch, ng_l2cap_get_ident(con),
			NG_L2CAP_CON_REQ, msg->header.token);
	if (cmd == NULL) {
		ng_l2cap_free_chan(ch);
		error = ENOMEM;
		goto out;
	}

	if (cmd->ident == NG_L2CAP_NULL_IDENT) {
		ng_l2cap_free_cmd(cmd);
		ng_l2cap_free_chan(ch);
		error = EIO;
		goto out;
	}

	/* Create L2CAP command packet */
	if(ip->idtype == NG_L2CAP_L2CA_IDTYPE_ATT){
		_ng_l2cap_con_rsp(cmd->aux, cmd->ident, NG_L2CAP_ATT_CID,
				  NG_L2CAP_ATT_CID, 0, 0);
		cmd->aux->m_flags |= M_PROTO2;
	}else if(ip->idtype == NG_L2CAP_L2CA_IDTYPE_SMP){
		_ng_l2cap_con_rsp(cmd->aux, cmd->ident, NG_L2CAP_SMP_CID,
				  NG_L2CAP_SMP_CID, 0, 0);
		cmd->aux->m_flags |= M_PROTO2;
	}else{
		_ng_l2cap_con_req(cmd->aux, cmd->ident, ch->psm, ch->scid);
	}
	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);
		ng_l2cap_free_chan(ch);
		error = ENOBUFS;
		goto out;
	}

	ch->state = NG_L2CAP_W4_L2CAP_CON_RSP;

	/* Link command to the queue */
	ng_l2cap_link_cmd(ch->con, cmd);
	ng_l2cap_lp_deliver(ch->con);
out:
	return (error);
} /* ng_l2cap_l2ca_con_req */

/*
 * Send L2CA_Connect response to the upper layer protocol.
 */

int
ng_l2cap_l2ca_con_rsp(ng_l2cap_chan_p ch, u_int32_t token, u_int16_t result,
		u_int16_t status)
{
	ng_l2cap_p		 l2cap = ch->con->l2cap;
	struct ng_mesg		*msg = NULL;
	ng_l2cap_l2ca_con_op	*op = NULL;
	int			 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CA_Connect response message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_Connect response message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_CON,
		sizeof(*op), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		msg->header.token = token;
		msg->header.flags |= NGF_RESP;

		op = (ng_l2cap_l2ca_con_op *)(msg->data);
		
		/*
		 * XXX Spec. says we should only populate LCID when result == 0
		 * What about PENDING? What the heck, for now always populate
		 * LCID :)
		 */
		if(ch->scid == NG_L2CAP_ATT_CID){
			op->idtype = NG_L2CAP_L2CA_IDTYPE_ATT;
			op->lcid = ch->con->con_handle;
		}else if(ch->scid == NG_L2CAP_SMP_CID){
			op->idtype = NG_L2CAP_L2CA_IDTYPE_SMP;
			op->lcid = ch->con->con_handle;
		}else{
			op->idtype = (ch->con->linktype == NG_HCI_LINK_ACL)?
				NG_L2CAP_L2CA_IDTYPE_BREDR :
				NG_L2CAP_L2CA_IDTYPE_LE;
			op->lcid = ch->scid;				
		}
		op->encryption = ch->con->encryption;
		op->result = result;
		op->status = status;

		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);
	}

	return (error);
} /* ng_l2cap_l2ca_con_rsp */

/*
 * Process L2CA_ConnectRsp request from the upper layer protocol.
 */

int
ng_l2cap_l2ca_con_rsp_req(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_l2cap_l2ca_con_rsp_ip	*ip = NULL;
	ng_l2cap_con_p			 con = NULL;
	ng_l2cap_chan_p			 ch = NULL;
	ng_l2cap_cmd_p			 cmd = NULL;
	u_int16_t			 dcid;
	int				 error = 0;

	/* Check message */
	if (msg->header.arglen != sizeof(*ip)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid L2CA_ConnectRsp request message size, size=%d\n",
			__func__, NG_NODE_NAME(l2cap->node),
			msg->header.arglen);
		error = EMSGSIZE;
		goto out;
	}

	ip = (ng_l2cap_l2ca_con_rsp_ip *)(msg->data);

	/* Check if we have this channel */
	if((ip->lcid != NG_L2CAP_ATT_CID)&&
	   (ip->lcid != NG_L2CAP_SMP_CID)){
		ch = ng_l2cap_chan_by_scid(l2cap, ip->lcid
					   ,(ip->linktype == NG_HCI_LINK_ACL)?
					   NG_L2CAP_L2CA_IDTYPE_BREDR:
					   NG_L2CAP_L2CA_IDTYPE_LE);
	}else{
		// For now not support on ATT device.
		ch = NULL;
	}
	if (ch == NULL) {
		NG_L2CAP_ALERT(
"%s: %s - unexpected L2CA_ConnectRsp request message. " \
"Channel does not exist, lcid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ip->lcid);
		error = ENOENT;
		goto out;
	}

	/* Check channel state */
	if (ch->state != NG_L2CAP_W4_L2CA_CON_RSP) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CA_ConnectRsp request message. " \
"Invalid channel state, state=%d, lcid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->state,
			ip->lcid);
		error = EINVAL;
		goto out;
	}

	dcid = ch->dcid;
	con = ch->con;

	/*
	 * Now we are pretty much sure it is our response. So create and send 
	 * L2CAP_ConnectRsp message to our peer.
	 */

	if (ch->ident != ip->ident)
		NG_L2CAP_WARN(
"%s: %s - channel ident and response ident do not match, scid=%d, ident=%d. " \
"Will use response ident=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->scid, 
			ch->ident, ip->ident);

	/* Check result */
	switch (ip->result) {
	case NG_L2CAP_SUCCESS:
		ch->state = ((ch->scid == NG_L2CAP_ATT_CID)||
			     (ch->scid == NG_L2CAP_SMP_CID))?
			NG_L2CAP_OPEN : NG_L2CAP_CONFIG;
		ch->cfg_state = 0;
		break;

	case NG_L2CAP_PENDING:
		break;

	default:
		ng_l2cap_free_chan(ch);
		ch = NULL;
		break;
	}

	/* Create L2CAP command */
	cmd = ng_l2cap_new_cmd(con, ch, ip->ident, NG_L2CAP_CON_RSP,
			msg->header.token);
	if (cmd == NULL) {
		if (ch != NULL)
			ng_l2cap_free_chan(ch);

		error = ENOMEM;
		goto out;
	}

	_ng_l2cap_con_rsp(cmd->aux, cmd->ident, ip->lcid, dcid, 
		ip->result, ip->status);
	if (cmd->aux == NULL) {
		if (ch != NULL)
			ng_l2cap_free_chan(ch);

		ng_l2cap_free_cmd(cmd);
		error = ENOBUFS;
		goto out;
	} 

	/* Link command to the queue */
	ng_l2cap_link_cmd(con, cmd);
	ng_l2cap_lp_deliver(con);
out:
	return (error);
} /* ng_l2cap_l2ca_con_rsp_req */

int ng_l2cap_l2ca_encryption_change(ng_l2cap_chan_p ch, uint16_t result)
{
	ng_l2cap_p			 l2cap = ch->con->l2cap;
	struct ng_mesg			*msg = NULL;
	ng_l2cap_l2ca_enc_chg_op	*op = NULL;
	int				 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CA_ConnectRsp response message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_ConnectRsp response message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_ENC_CHANGE,
		sizeof(*op), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		msg->header.token = 0;
		msg->header.flags |= NGF_RESP;

		op = (ng_l2cap_l2ca_enc_chg_op *)(msg->data);
		op->result = result;
		if(ch->scid ==NG_L2CAP_ATT_CID||
		   ch->scid ==NG_L2CAP_SMP_CID){
			op->lcid = ch->con->con_handle;
			op->idtype = (ch->scid==NG_L2CAP_ATT_CID)?
				NG_L2CAP_L2CA_IDTYPE_ATT:
				NG_L2CAP_L2CA_IDTYPE_SMP;
		}else{
			op->idtype =(ch->con->linktype ==NG_HCI_LINK_ACL)?
				NG_L2CAP_L2CA_IDTYPE_BREDR:
				NG_L2CAP_L2CA_IDTYPE_LE;
		}
			

		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);
	}

	return (error);
	
}
/*
 * Send L2CAP_ConnectRsp response to the upper layer
 */
 
int
ng_l2cap_l2ca_con_rsp_rsp(ng_l2cap_chan_p ch, u_int32_t token, u_int16_t result)
{
	ng_l2cap_p			 l2cap = ch->con->l2cap;
	struct ng_mesg			*msg = NULL;
	ng_l2cap_l2ca_con_rsp_op	*op = NULL;
	int				 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CA_ConnectRsp response message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_ConnectRsp response message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_CON_RSP,
		sizeof(*op), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		msg->header.token = token;
		msg->header.flags |= NGF_RESP;

		op = (ng_l2cap_l2ca_con_rsp_op *)(msg->data);
		op->result = result;

		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);
	}

	return (error);
} /* ng_l2cap_l2ca_con_rsp_rsp */

/*
 * Send L2CA_ConnectInd message to the upper layer protocol. 
 */

int
ng_l2cap_l2ca_con_ind(ng_l2cap_chan_p ch)
{
	ng_l2cap_p			 l2cap = ch->con->l2cap;
	struct ng_mesg			*msg = NULL;
	ng_l2cap_l2ca_con_ind_ip	*ip = NULL;
	int				 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CA_ConnectInd message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_ConnectInd message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_CON_IND,
		sizeof(*ip), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		ip = (ng_l2cap_l2ca_con_ind_ip *)(msg->data);

		bcopy(&ch->con->remote, &ip->bdaddr, sizeof(ip->bdaddr));
		ip->lcid = ch->scid;
		ip->psm = ch->psm;
		ip->ident = ch->ident;
		ip->linktype = ch->con->linktype;

		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);
	}

	return (error);
} /* ng_l2cap_l2ca_con_ind */

/*
 * Process L2CA_Config request from the upper layer protocol
 */

int
ng_l2cap_l2ca_cfg_req(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_l2cap_l2ca_cfg_ip	*ip = NULL;
	ng_l2cap_chan_p		 ch = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;
	struct mbuf		*opt = NULL;
        u_int16_t		*mtu = NULL, *flush_timo = NULL;
        ng_l2cap_flow_p		 flow = NULL;
	int			 error = 0;

	/* Check message */
	if (msg->header.arglen != sizeof(*ip)) {
		NG_L2CAP_ALERT(
"%s: %s - Invalid L2CA_Config request message size, size=%d\n",
			__func__, NG_NODE_NAME(l2cap->node),
			msg->header.arglen);
		error = EMSGSIZE;
		goto out;
	}

	ip = (ng_l2cap_l2ca_cfg_ip *)(msg->data);

	/* Check if we have this channel */
	ch = ng_l2cap_chan_by_scid(l2cap, ip->lcid, NG_L2CAP_L2CA_IDTYPE_BREDR);
	if (ch == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CA_Config request message. " \
"Channel does not exist, lcid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ip->lcid);
		error = ENOENT;
		goto out;
	}

	/* Check channel state */
	if (ch->state != NG_L2CAP_OPEN && ch->state != NG_L2CAP_CONFIG) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CA_Config request message. " \
"Invalid channel state, state=%d, lcid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->state,
			ch->scid);
		error = EINVAL;
		goto out;
	}

	/* Set requested channel configuration options */
	ch->imtu = ip->imtu;
	bcopy(&ip->oflow, &ch->oflow, sizeof(ch->oflow));
	ch->flush_timo = ip->flush_timo;
	ch->link_timo = ip->link_timo;

	/* Compare channel settings with defaults */
	if (ch->imtu != NG_L2CAP_MTU_DEFAULT)
		mtu = &ch->imtu;
	if (ch->flush_timo != NG_L2CAP_FLUSH_TIMO_DEFAULT)
		flush_timo = &ch->flush_timo;
	if (bcmp(ng_l2cap_default_flow(), &ch->oflow, sizeof(ch->oflow)) != 0)
		flow = &ch->oflow;

	/* Create configuration options */
	_ng_l2cap_build_cfg_options(opt, mtu, flush_timo, flow);
	if (opt == NULL) {
                error = ENOBUFS;
		goto out;
	}

	/* Create L2CAP command descriptor */
	cmd = ng_l2cap_new_cmd(ch->con, ch, ng_l2cap_get_ident(ch->con),
			NG_L2CAP_CFG_REQ, msg->header.token);
	if (cmd == NULL) {
		NG_FREE_M(opt);
		error = ENOMEM;
		goto out;
	}

	if (cmd->ident == NG_L2CAP_NULL_IDENT) {
		ng_l2cap_free_cmd(cmd);
		NG_FREE_M(opt);
		error = EIO;
		goto out;
	}

	/* Create L2CAP command packet */
	_ng_l2cap_cfg_req(cmd->aux, cmd->ident, ch->dcid, 0, opt);
	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);
		error =  ENOBUFS;
		goto out;
	}

	/* Adjust channel state for re-configuration */
	if (ch->state == NG_L2CAP_OPEN) {
		ch->state = ((ch->scid == NG_L2CAP_ATT_CID)||
			     (ch->scid == NG_L2CAP_SMP_CID))?
			NG_L2CAP_OPEN : NG_L2CAP_CONFIG;
		ch->cfg_state = 0;
	}

        /* Link command to the queue */
	ng_l2cap_link_cmd(ch->con, cmd);
	ng_l2cap_lp_deliver(ch->con);
out:
	return (error);
} /* ng_l2cap_l2ca_cfg_req */

/*
 * Send L2CA_Config response to the upper layer protocol
 */

int
ng_l2cap_l2ca_cfg_rsp(ng_l2cap_chan_p ch, u_int32_t token, u_int16_t result)
{
	ng_l2cap_p		 l2cap = ch->con->l2cap;
	struct ng_mesg		*msg = NULL;
	ng_l2cap_l2ca_cfg_op	*op = NULL;
	int			 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CA_Config response message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_Config response message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_CFG,
		sizeof(*op), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		msg->header.token = token;
		msg->header.flags |= NGF_RESP;

		op = (ng_l2cap_l2ca_cfg_op *)(msg->data);
		op->result = result;
		op->imtu = ch->imtu;
		bcopy(&ch->oflow, &op->oflow, sizeof(op->oflow));
		op->flush_timo = ch->flush_timo;

		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);

		if (error == 0 && result == NG_L2CAP_SUCCESS) {
			ch->cfg_state |= NG_L2CAP_CFG_IN;

			if (ch->cfg_state == NG_L2CAP_CFG_BOTH)
				ch->state = NG_L2CAP_OPEN;
		}
	}

	return (error);
} /* ng_l2cap_l2ca_cfg_rsp */

/*
 * Process L2CA_ConfigRsp request from the upper layer protocol
 *
 * XXX XXX XXX
 *
 * NOTE: The Bluetooth specification says that Configuration_Response 
 * (L2CA_ConfigRsp) should be used to issue response to configuration request
 * indication. The minor problem here is L2CAP command ident. We should use 
 * ident from original L2CAP request to make sure our peer can match request
 * and response. For some reason Bluetooth specification does not include
 * ident field into L2CA_ConfigInd and L2CA_ConfigRsp messages. This seems
 * strange to me, because L2CA_ConnectInd and L2CA_ConnectRsp do have ident
 * field. So we should store last known L2CAP request command ident in channel.
 * Also it seems that upper layer can not reject configuration request, as
 * Configuration_Response message does not have status/reason field.
 */

int
ng_l2cap_l2ca_cfg_rsp_req(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_l2cap_l2ca_cfg_rsp_ip	*ip = NULL;
	ng_l2cap_chan_p			 ch = NULL;
	ng_l2cap_cmd_p			 cmd = NULL;
	struct mbuf			*opt = NULL;
	u_int16_t			*mtu = NULL;
	ng_l2cap_flow_p			 flow = NULL;
	int				 error = 0;

	/* Check message */
	if (msg->header.arglen != sizeof(*ip)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid L2CA_ConfigRsp request message size, size=%d\n",
			__func__, NG_NODE_NAME(l2cap->node),
			msg->header.arglen);
		error = EMSGSIZE;
		goto out;
	}

	ip = (ng_l2cap_l2ca_cfg_rsp_ip *)(msg->data);

	/* Check if we have this channel */
	ch = ng_l2cap_chan_by_scid(l2cap, ip->lcid,
				   NG_L2CAP_L2CA_IDTYPE_BREDR);
	if (ch == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CA_ConfigRsp request message. " \
"Channel does not exist, lcid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ip->lcid);
		error = ENOENT;
		goto out;
	}

	/* Check channel state */
	if (ch->state != NG_L2CAP_CONFIG) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CA_ConfigRsp request message. " \
"Invalid channel state, state=%d, lcid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->state,
			ch->scid);
		error = EINVAL;
		goto out;
	}

	/* Set channel settings */
	if (ip->omtu != ch->omtu) {
		ch->omtu = ip->omtu;
		mtu = &ch->omtu;
	}

	if (bcmp(&ip->iflow, &ch->iflow, sizeof(ch->iflow)) != 0) { 
		bcopy(&ip->iflow, &ch->iflow, sizeof(ch->iflow));
		flow = &ch->iflow;
	}

	if (mtu != NULL || flow != NULL) {
		_ng_l2cap_build_cfg_options(opt, mtu, NULL, flow);
		if (opt == NULL) {
			error = ENOBUFS;
			goto out;
		}
	}

	/* Create L2CAP command */
	cmd = ng_l2cap_new_cmd(ch->con, ch, ch->ident, NG_L2CAP_CFG_RSP,
			msg->header.token);
	if (cmd == NULL) {
		NG_FREE_M(opt);
		error = ENOMEM;
		goto out;
	}

	_ng_l2cap_cfg_rsp(cmd->aux,cmd->ident,ch->dcid,0,NG_L2CAP_SUCCESS,opt);
	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);
		error = ENOBUFS;
		goto out;
	}

	/* XXX FIXME - not here ??? */
	ch->cfg_state |= NG_L2CAP_CFG_OUT;
	if (ch->cfg_state == NG_L2CAP_CFG_BOTH)
		ch->state = NG_L2CAP_OPEN;

	/* Link command to the queue */
	ng_l2cap_link_cmd(ch->con, cmd);
	ng_l2cap_lp_deliver(ch->con);
out:
	return (error);
} /* ng_l2cap_l2ca_cfg_rsp_req */

/*
 * Send L2CA_ConfigRsp response to the upper layer protocol
 */

int
ng_l2cap_l2ca_cfg_rsp_rsp(ng_l2cap_chan_p ch, u_int32_t token, u_int16_t result)
{
	ng_l2cap_p			 l2cap = ch->con->l2cap;
	struct ng_mesg			*msg = NULL;
	ng_l2cap_l2ca_cfg_rsp_op	*op = NULL;
	int				 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CA_ConfigRsp response message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_ConfigRsp response message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_CFG_RSP,
		sizeof(*op), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		msg->header.token = token;
		msg->header.flags |= NGF_RESP;

		op = (ng_l2cap_l2ca_cfg_rsp_op *)(msg->data);
		op->result = result;

		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);
	}

	return (error);
} /* ng_l2cap_l2ca_cfg_rsp_rsp */

/*
 * Send L2CA_ConfigInd message to the upper layer protocol
 *
 * XXX XXX XXX
 *
 * NOTE: The Bluetooth specification says that Configuration_Response 
 * (L2CA_ConfigRsp) should be used to issue response to configuration request
 * indication. The minor problem here is L2CAP command ident. We should use 
 * ident from original L2CAP request to make sure our peer can match request
 * and response. For some reason Bluetooth specification does not include
 * ident field into L2CA_ConfigInd and L2CA_ConfigRsp messages. This seems
 * strange to me, because L2CA_ConnectInd and L2CA_ConnectRsp do have ident
 * field. So we should store last known L2CAP request command ident in channel.
 * Also it seems that upper layer can not reject configuration request, as
 * Configuration_Response message does not have status/reason field.
 */

int
ng_l2cap_l2ca_cfg_ind(ng_l2cap_chan_p ch)
{
	ng_l2cap_p			 l2cap = ch->con->l2cap;
	struct ng_mesg			*msg = NULL;
	ng_l2cap_l2ca_cfg_ind_ip	*ip = NULL;
	int				 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - Unable to send L2CA_ConfigInd message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_ConnectInd message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_CFG_IND,
			sizeof(*ip), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		ip = (ng_l2cap_l2ca_cfg_ind_ip *)(msg->data);
		ip->lcid = ch->scid;
		ip->omtu = ch->omtu;
		bcopy(&ch->iflow, &ip->iflow, sizeof(ip->iflow));
		ip->flush_timo = ch->flush_timo;

		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);
	}

	return (error);
} /* ng_l2cap_l2ca_cfg_ind */

/*
 * Process L2CA_Write event
 */

int
ng_l2cap_l2ca_write_req(ng_l2cap_p l2cap, struct mbuf *m)
{
	ng_l2cap_l2ca_hdr_t	*l2ca_hdr = NULL;
	ng_l2cap_chan_p		 ch = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;
	int			 error = 0;
	u_int32_t		 token = 0;

	/* Make sure we can access L2CA data packet header */
	if (m->m_pkthdr.len < sizeof(*l2ca_hdr)) {
		NG_L2CAP_ERR(
"%s: %s - L2CA Data packet too small, len=%d\n",
			__func__,NG_NODE_NAME(l2cap->node),m->m_pkthdr.len);
		error = EMSGSIZE;
		goto drop;
	}

	/* Get L2CA data packet header */
	NG_L2CAP_M_PULLUP(m, sizeof(*l2ca_hdr));
	if (m == NULL)
		return (ENOBUFS);

	l2ca_hdr = mtod(m, ng_l2cap_l2ca_hdr_t *);
	token = l2ca_hdr->token;
	m_adj(m, sizeof(*l2ca_hdr));

	/* Verify payload size */
	if (l2ca_hdr->length != m->m_pkthdr.len) {
		NG_L2CAP_ERR(
"%s: %s - invalid L2CA Data packet. " \
"Payload length does not match, length=%d, len=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), l2ca_hdr->length,
			m->m_pkthdr.len);
		error = EMSGSIZE;
		goto drop;
	}

	/* Check channel ID */
	if (l2ca_hdr->idtype == NG_L2CAP_L2CA_IDTYPE_ATT){
		ch = ng_l2cap_chan_by_conhandle(l2cap, NG_L2CAP_ATT_CID,
						l2ca_hdr->lcid);
	} else if (l2ca_hdr->idtype == NG_L2CAP_L2CA_IDTYPE_SMP){
		ch = ng_l2cap_chan_by_conhandle(l2cap, NG_L2CAP_SMP_CID,
						l2ca_hdr->lcid);
	}else{
		if (l2ca_hdr->lcid < NG_L2CAP_FIRST_CID) {
			NG_L2CAP_ERR(
				"%s: %s - invalid L2CA Data packet. Inavlid channel ID, cid=%d\n",
				__func__, NG_NODE_NAME(l2cap->node),
				l2ca_hdr->lcid);
			error = EINVAL;
			goto drop;
		}

		/* Verify that we have the channel and make sure it is open */
		ch = ng_l2cap_chan_by_scid(l2cap, l2ca_hdr->lcid,
					   l2ca_hdr->idtype);
	}
	
	if (ch == NULL) {
		NG_L2CAP_ERR(
"%s: %s - invalid L2CA Data packet. Channel does not exist, cid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), l2ca_hdr->lcid);
		error = ENOENT;
		goto drop;
	}

	if (ch->state != NG_L2CAP_OPEN) {
		NG_L2CAP_ERR(
"%s: %s - invalid L2CA Data packet. Invalid channel state, scid=%d, state=%d\n",
			 __func__, NG_NODE_NAME(l2cap->node), ch->scid, 
			ch->state);
		error = EHOSTDOWN;
		goto drop; /* XXX not always - re-configure */
	}

	/* Create L2CAP command descriptor */
	cmd = ng_l2cap_new_cmd(ch->con, ch, 0, NGM_L2CAP_L2CA_WRITE, token);
	if (cmd == NULL) {
		error = ENOMEM;
		goto drop;
	}

	/* Attach data packet and link command to the queue */
	cmd->aux = m;
	ng_l2cap_link_cmd(ch->con, cmd);
	ng_l2cap_lp_deliver(ch->con);

	return (error);
drop:
	NG_FREE_M(m);

	return (error);
} /* ng_l2cap_l2ca_write_req */

/*
 * Send L2CA_Write response
 */

int
ng_l2cap_l2ca_write_rsp(ng_l2cap_chan_p ch, u_int32_t token, u_int16_t result,
		u_int16_t length)
{
	ng_l2cap_p		 l2cap = ch->con->l2cap;
	struct ng_mesg		*msg = NULL;
	ng_l2cap_l2ca_write_op	*op = NULL;
	int			 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CA_WriteRsp message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_WriteRsp message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_WRITE,
			sizeof(*op), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		msg->header.token = token;
		msg->header.flags |= NGF_RESP;

		op = (ng_l2cap_l2ca_write_op *)(msg->data);
		op->result = result;
		op->length = length;
		if(ch->scid == NG_L2CAP_ATT_CID){
			op->idtype = NG_L2CAP_L2CA_IDTYPE_ATT;
			op->lcid = ch->con->con_handle;
		}else if(ch->scid == NG_L2CAP_SMP_CID){
			op->idtype = NG_L2CAP_L2CA_IDTYPE_SMP;
			op->lcid = ch->con->con_handle;
		}else{
			op->idtype = (ch->con->linktype == NG_HCI_LINK_ACL)?
				NG_L2CAP_L2CA_IDTYPE_BREDR :
				NG_L2CAP_L2CA_IDTYPE_LE;
			op->lcid = ch->scid;				
			
		}
		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);
	}

	return (error);
} /* ng_l2cap_l2ca_write_rsp */

/*
 * Receive packet from the lower layer protocol and send it to the upper
 * layer protocol (L2CAP_Read)
 */

int
ng_l2cap_l2ca_receive(ng_l2cap_con_p con)
{
	ng_l2cap_p	 l2cap = con->l2cap;
	ng_l2cap_hdr_t	*hdr = NULL;
	ng_l2cap_chan_p  ch = NULL;
	int		 error = 0;
	int idtype;
	uint16_t *idp;
	int silent = 0;
	
	NG_L2CAP_M_PULLUP(con->rx_pkt, sizeof(*hdr));
	if (con->rx_pkt == NULL)
		return (ENOBUFS);

	hdr = mtod(con->rx_pkt, ng_l2cap_hdr_t *);

	/* Check channel */

	if(hdr->dcid == NG_L2CAP_ATT_CID){
		idtype = NG_L2CAP_L2CA_IDTYPE_ATT;
		ch = ng_l2cap_chan_by_conhandle(l2cap, NG_L2CAP_ATT_CID,
						con->con_handle);
		/*
		 * Here,ATT channel is distinguished by 
		 * connection handle
		 */
		hdr->dcid = con->con_handle;
		silent = 1;
	}else if(hdr->dcid == NG_L2CAP_SMP_CID){
		idtype = NG_L2CAP_L2CA_IDTYPE_SMP;
		ch = ng_l2cap_chan_by_conhandle(l2cap, NG_L2CAP_SMP_CID,
						con->con_handle);
		/*
		 * Here,SMP channel is distinguished by 
		 * connection handle
		 */
		silent = 1;
		hdr->dcid = con->con_handle; 
	}else{
		idtype = (con->linktype==NG_HCI_LINK_ACL)?
			NG_L2CAP_L2CA_IDTYPE_BREDR:
			NG_L2CAP_L2CA_IDTYPE_LE;
		ch = ng_l2cap_chan_by_scid(l2cap, hdr->dcid, idtype);
	}
	if (ch == NULL) {
		if(!silent)
			NG_L2CAP_ERR(
"%s: %s - unexpected L2CAP data packet. Channel does not exist, cid=%d, idtype=%d\n",
	__func__, NG_NODE_NAME(l2cap->node), hdr->dcid, idtype);
		error = ENOENT;
		goto drop;
	}

	/* Check channel state */
	if (ch->state != NG_L2CAP_OPEN) {
		NG_L2CAP_WARN(
"%s: %s - unexpected L2CAP data packet. " \
"Invalid channel state, cid=%d, state=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->scid,
			ch->state);
		error = EHOSTDOWN; /* XXX not always - re-configuration */
		goto drop;
	}

	/* Check payload size and channel's MTU */
	if (hdr->length > ch->imtu) {
		NG_L2CAP_ERR(
"%s: %s - invalid L2CAP data packet. " \
"Packet too big, length=%d, imtu=%d, cid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), hdr->length, 
			ch->imtu, ch->scid);
		error = EMSGSIZE;
		goto drop;
	}

	/*
	 * If we got here then everything looks good and we can sent packet
	 * to the upper layer protocol.
	 */

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CAP data packet. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);
		error = ENOTCONN;
		goto drop;
	}
	M_PREPEND(con->rx_pkt, sizeof(uint16_t), M_NOWAIT);
	if(con->rx_pkt == NULL)
		goto drop;
	idp = mtod(con->rx_pkt, uint16_t *);
	*idp = idtype;

	NG_SEND_DATA_ONLY(error, l2cap->l2c, con->rx_pkt);
	con->rx_pkt = NULL;
drop:
	NG_FREE_M(con->rx_pkt); /* checks for != NULL */

	return (error);
} /* ng_l2cap_receive */

/*
 * Receive connectioless (multicast) packet from the lower layer protocol and 
 * send it to the upper layer protocol
 */

int
ng_l2cap_l2ca_clt_receive(ng_l2cap_con_p con)
{
	struct _clt_pkt {
		ng_l2cap_hdr_t		 h;
		ng_l2cap_clt_hdr_t	 c_h;
	} __attribute__ ((packed))	*hdr = NULL;
	ng_l2cap_p			 l2cap = con->l2cap;
	int				 length, error = 0;

	NG_L2CAP_M_PULLUP(con->rx_pkt, sizeof(*hdr));
	if (con->rx_pkt == NULL)
		return (ENOBUFS);

	hdr = mtod(con->rx_pkt, struct _clt_pkt *);

	/* Check packet */
	length = con->rx_pkt->m_pkthdr.len - sizeof(*hdr);
	if (length < 0) {
		NG_L2CAP_ERR(
"%s: %s - invalid L2CAP CLT data packet. Packet too small, length=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), length);
		error = EMSGSIZE;
		goto drop;
	}

	/* Check payload size against CLT MTU */
	if (length > NG_L2CAP_MTU_DEFAULT) {
		NG_L2CAP_ERR(
"%s: %s - invalid L2CAP CLT data packet. Packet too big, length=%d, mtu=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), length,
			NG_L2CAP_MTU_DEFAULT);
		error = EMSGSIZE;
		goto drop;
	}

	hdr->c_h.psm = le16toh(hdr->c_h.psm);

	/*
	 * If we got here then everything looks good and we can sent packet
	 * to the upper layer protocol.
	 */

	/* Select upstream hook based on PSM */
	switch (hdr->c_h.psm) {
	case NG_L2CAP_PSM_SDP:
		if (l2cap->flags & NG_L2CAP_CLT_SDP_DISABLED)
			goto drop;
		break;

	case NG_L2CAP_PSM_RFCOMM:
		if (l2cap->flags & NG_L2CAP_CLT_RFCOMM_DISABLED)
			goto drop;
		break;

	case NG_L2CAP_PSM_TCP:
		if (l2cap->flags & NG_L2CAP_CLT_TCP_DISABLED)
			goto drop;
		break;
        }

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CAP CLT data packet. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), hdr->c_h.psm);
		error = ENOTCONN;
		goto drop;
	}

	NG_SEND_DATA_ONLY(error, l2cap->l2c, con->rx_pkt);
	con->rx_pkt = NULL;
drop:
	NG_FREE_M(con->rx_pkt); /* checks for != NULL */

	return (error);
} /* ng_l2cap_l2ca_clt_receive */

/*
 * Send L2CA_QoSViolationInd to the upper layer protocol
 */

int
ng_l2cap_l2ca_qos_ind(ng_l2cap_chan_p ch)
{
	ng_l2cap_p			 l2cap = ch->con->l2cap;
	struct ng_mesg			*msg = NULL;
	ng_l2cap_l2ca_qos_ind_ip	*ip = NULL;
	int				 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CA_QoSViolationInd message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_QoSViolationInd message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_QOS_IND,
		sizeof(*ip), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		ip = (ng_l2cap_l2ca_qos_ind_ip *)(msg->data);
		bcopy(&ch->con->remote, &ip->bdaddr, sizeof(ip->bdaddr));
		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);
	}

	return (error);
} /* ng_l2cap_l2ca_qos_ind */

/*
 * Process L2CA_Disconnect request from the upper layer protocol.
 */

int
ng_l2cap_l2ca_discon_req(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_l2cap_l2ca_discon_ip	*ip = NULL;
	ng_l2cap_chan_p		 ch = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;
	int			 error = 0;

	/* Check message */
	if (msg->header.arglen != sizeof(*ip)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid L2CA_Disconnect request message size, size=%d\n",
			__func__, NG_NODE_NAME(l2cap->node),
			msg->header.arglen);
		error = EMSGSIZE;
		goto out;
	}

	ip = (ng_l2cap_l2ca_discon_ip *)(msg->data);


	if(ip->idtype == NG_L2CAP_L2CA_IDTYPE_ATT){
		/* Don't send Disconnect request on L2CAP Layer*/
		ch = ng_l2cap_chan_by_conhandle(l2cap, NG_L2CAP_ATT_CID,
			ip->lcid);
		
		if(ch != NULL){
			ng_l2cap_free_chan(ch);
		}else{
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CA_Disconnect request message. " \
"Channel does not exist, conhandle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ip->lcid);
			error = EINVAL;
		}
		goto out;
	}else if(ip->idtype == NG_L2CAP_L2CA_IDTYPE_SMP){
		/* Don't send Disconnect request on L2CAP Layer*/
		ch = ng_l2cap_chan_by_conhandle(l2cap, NG_L2CAP_SMP_CID,
			ip->lcid);
		
		if(ch != NULL){
			ng_l2cap_free_chan(ch);
		}else{
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CA_Disconnect request message. " \
"Channel does not exist, conhandle=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ip->lcid);
			error = EINVAL;
		}
		goto out;
	}else{
		/* Check if we have this channel */
		ch = ng_l2cap_chan_by_scid(l2cap, ip->lcid, ip->idtype);
	}
	if (ch == NULL) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CA_Disconnect request message. " \
"Channel does not exist, lcid=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ip->lcid);
		error = ENOENT;
		goto out;
	}

	/* Check channel state */
	if (ch->state != NG_L2CAP_CONFIG && ch->state != NG_L2CAP_OPEN && 
	    ch->state != NG_L2CAP_W4_L2CAP_DISCON_RSP) {
		NG_L2CAP_ERR(
"%s: %s - unexpected L2CA_Disconnect request message. " \
"Invalid channel state, state=%d, lcid=%d\n", 
			__func__, NG_NODE_NAME(l2cap->node), ch->state,
			ch->scid);
		error = EINVAL;
		goto out;
	}

	/* Create and send L2CAP_DisconReq message */
	cmd = ng_l2cap_new_cmd(ch->con, ch, ng_l2cap_get_ident(ch->con),
			NG_L2CAP_DISCON_REQ, msg->header.token);
	if (cmd == NULL) {
		ng_l2cap_free_chan(ch);
		error = ENOMEM;
		goto out;
	}

	if (cmd->ident == NG_L2CAP_NULL_IDENT) {
		ng_l2cap_free_chan(ch);
		ng_l2cap_free_cmd(cmd);
		error = EIO;
		goto out;
	}

	_ng_l2cap_discon_req(cmd->aux, cmd->ident, ch->dcid, ch->scid);
	if (cmd->aux == NULL) {
		ng_l2cap_free_chan(ch);
		ng_l2cap_free_cmd(cmd);
		error = ENOBUFS;
		goto out;
	}

	ch->state = NG_L2CAP_W4_L2CAP_DISCON_RSP;

	/* Link command to the queue */
	ng_l2cap_link_cmd(ch->con, cmd);
	ng_l2cap_lp_deliver(ch->con);
out:
	return (error);
} /* ng_l2cap_l2ca_discon_req */

/*
 * Send L2CA_Disconnect response to the upper layer protocol
 */

int
ng_l2cap_l2ca_discon_rsp(ng_l2cap_chan_p ch, u_int32_t token, u_int16_t result)
{
	ng_l2cap_p		 l2cap = ch->con->l2cap;
	struct ng_mesg		*msg = NULL;
	ng_l2cap_l2ca_discon_op	*op = NULL;
	int			 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CA_Disconnect response message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_Disconnect response message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_DISCON,
		sizeof(*op), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		msg->header.token = token;
		msg->header.flags |= NGF_RESP;

		op = (ng_l2cap_l2ca_discon_op *)(msg->data);
		op->result = result;

		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);
	}

	return (error);
} /* ng_l2cap_l2ca_discon_rsp */

/*
 * Send L2CA_DisconnectInd message to the upper layer protocol.
 */

int
ng_l2cap_l2ca_discon_ind(ng_l2cap_chan_p ch)
{
	ng_l2cap_p			 l2cap = ch->con->l2cap;
	struct ng_mesg			*msg = NULL;
	ng_l2cap_l2ca_discon_ind_ip	*ip = NULL;
	int				 error = 0;

	/* Check if upstream hook is connected and valid */
	if (l2cap->l2c == NULL || NG_HOOK_NOT_VALID(l2cap->l2c)) {
		NG_L2CAP_ERR(
"%s: %s - unable to send L2CA_DisconnectInd message. " \
"Hook is not connected or valid, psm=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ch->psm);

		return (ENOTCONN);
	}

	/* Create and send L2CA_DisconnectInd message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_DISCON_IND,
		sizeof(*ip), M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		ip = (ng_l2cap_l2ca_discon_ind_ip *)(msg->data);
		ip->idtype = ch->idtype;
		if(ch->idtype == NG_L2CAP_L2CA_IDTYPE_ATT||
		   ch->idtype == NG_L2CAP_L2CA_IDTYPE_SMP)
			ip->lcid = ch->con->con_handle;
		else
			ip->lcid = ch->scid;
		
		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->l2c, 0);
	} 

	return (error);
} /* ng_l2cap_l2ca_discon_ind */

/*
 * Process L2CA_GroupCreate request from the upper layer protocol.
 * XXX FIXME
 */

int
ng_l2cap_l2ca_grp_create(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	return (ENOTSUP);
} /* ng_l2cap_l2ca_grp_create */

/*
 * Process L2CA_GroupClose request from the upper layer protocol
 * XXX FIXME
 */

int
ng_l2cap_l2ca_grp_close(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	return (ENOTSUP);
} /* ng_l2cap_l2ca_grp_close */

/*
 * Process L2CA_GroupAddMember request from the upper layer protocol.
 * XXX FIXME
 */

int
ng_l2cap_l2ca_grp_add_member_req(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	return (ENOTSUP);
} /* ng_l2cap_l2ca_grp_add_member_req */

/*
 * Send L2CA_GroupAddMember response to the upper layer protocol.
 * XXX FIXME
 */

int
ng_l2cap_l2ca_grp_add_member_rsp(ng_l2cap_chan_p ch, u_int32_t token,
		u_int16_t result)
{
	return (0);
} /* ng_l2cap_l2ca_grp_add_member_rsp */

/*
 * Process L2CA_GroupDeleteMember request from the upper layer protocol
 * XXX FIXME
 */

int
ng_l2cap_l2ca_grp_rem_member(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	return (ENOTSUP);
} /* ng_l2cap_l2ca_grp_rem_member */

/*
 * Process L2CA_GroupGetMembers request from the upper layer protocol
 * XXX FIXME
 */

int
ng_l2cap_l2ca_grp_get_members(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	return (ENOTSUP);
} /* ng_l2cap_l2ca_grp_get_members */

/*
 * Process L2CA_Ping request from the upper layer protocol
 */

int
ng_l2cap_l2ca_ping_req(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_l2cap_l2ca_ping_ip	*ip = NULL;
	ng_l2cap_con_p		 con = NULL;
	ng_l2cap_cmd_p		 cmd = NULL;
	int			 error = 0;

	/* Verify message */
	if (msg->header.arglen < sizeof(*ip)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid L2CA_Ping request message size, size=%d\n",
			__func__, NG_NODE_NAME(l2cap->node),
			msg->header.arglen);
		error = EMSGSIZE;
		goto out;
	}

	ip = (ng_l2cap_l2ca_ping_ip *)(msg->data);
	if (ip->echo_size > NG_L2CAP_MAX_ECHO_SIZE) {
		NG_L2CAP_WARN(
"%s: %s - invalid L2CA_Ping request. Echo size is too big, echo_size=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), ip->echo_size);
		error = EMSGSIZE;
		goto out;
	}

	/* Check if we have connection to the unit */
	con = ng_l2cap_con_by_addr(l2cap, &ip->bdaddr, NG_HCI_LINK_ACL);
	if (con == NULL) {
		/* Submit LP_ConnectReq to the lower layer */
	  error = ng_l2cap_lp_con_req(l2cap, &ip->bdaddr, NG_HCI_LINK_ACL);
		if (error != 0) {
			NG_L2CAP_ERR(
"%s: %s - unable to send LP_ConnectReq message, error=%d\n",
				__func__, NG_NODE_NAME(l2cap->node), error);
			goto out;
		}

		/* This should not fail */
		con = ng_l2cap_con_by_addr(l2cap, &ip->bdaddr, NG_HCI_LINK_ACL);
		KASSERT((con != NULL),
("%s: %s - could not find connection!\n", __func__, NG_NODE_NAME(l2cap->node)));
	}

	/* Create L2CAP command descriptor */
	cmd = ng_l2cap_new_cmd(con, NULL, ng_l2cap_get_ident(con),
			NG_L2CAP_ECHO_REQ, msg->header.token);
	if (cmd == NULL) {
		error = ENOMEM;
		goto out;
	}

	if (cmd->ident == NG_L2CAP_NULL_IDENT) {
		ng_l2cap_free_cmd(cmd);
                error = EIO;
		goto out;
	}

	/* Create L2CAP command packet */
	_ng_l2cap_echo_req(cmd->aux, cmd->ident, 
			msg->data + sizeof(*ip), ip->echo_size);
	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);
                error = ENOBUFS;
		goto out;
	}

        /* Link command to the queue */
        ng_l2cap_link_cmd(con, cmd);
	ng_l2cap_lp_deliver(con);
out:
	return (error);
} /* ng_l2cap_l2ca_ping_req */

/*
 * Send L2CA_Ping response to the upper layer protocol
 */

int
ng_l2cap_l2ca_ping_rsp(ng_l2cap_con_p con, u_int32_t token, u_int16_t result,
		struct mbuf *data)
{
	ng_l2cap_p		 l2cap = con->l2cap;
	struct ng_mesg		*msg = NULL;
	ng_l2cap_l2ca_ping_op	*op = NULL;
	int			 error = 0, size = 0;

	/* Check if control hook is connected and valid */
	if (l2cap->ctl == NULL || NG_HOOK_NOT_VALID(l2cap->ctl)) {
		NG_L2CAP_WARN(
"%s: %s - unable to send L2CA_Ping response message. " \
"Hook is not connected or valid\n",
			__func__, NG_NODE_NAME(l2cap->node));
		error = ENOTCONN;
		goto out;
	}

	size = (data == NULL)? 0 : data->m_pkthdr.len;

	/* Create and send L2CA_Ping response message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_PING,
		sizeof(*op) + size, M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		msg->header.token = token;
		msg->header.flags |= NGF_RESP;

		op = (ng_l2cap_l2ca_ping_op *)(msg->data);
		op->result = result;
		bcopy(&con->remote, &op->bdaddr, sizeof(op->bdaddr));
		if (data != NULL && size > 0) {
			op->echo_size = size;
			m_copydata(data, 0, size, (caddr_t) op + sizeof(*op));
		}

		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->ctl, 0);
	}
out:
	NG_FREE_M(data);

	return (error);
} /* ng_l2cap_l2ca_ping_rsp */

/*
 * Process L2CA_GetInfo request from the upper layer protocol
 */

int
ng_l2cap_l2ca_get_info_req(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_l2cap_l2ca_get_info_ip	*ip = NULL;
	ng_l2cap_con_p			 con = NULL;
	ng_l2cap_cmd_p			 cmd = NULL;
	int				 error = 0;

	/* Verify message */
	if (msg->header.arglen != sizeof(*ip)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid L2CA_GetInfo request message size, size=%d\n",
			__func__, NG_NODE_NAME(l2cap->node),
			msg->header.arglen);
		error = EMSGSIZE;
		goto out;
	}

	ip = (ng_l2cap_l2ca_get_info_ip *)(msg->data);

	/* Check if we have connection to the unit */
	con = ng_l2cap_con_by_addr(l2cap, &ip->bdaddr,ip->linktype);
	if (con == NULL) {
		/* Submit LP_ConnectReq to the lower layer */
		error = ng_l2cap_lp_con_req(l2cap, &ip->bdaddr,ip->linktype);
		if (error != 0) {
			NG_L2CAP_ERR(
"%s: %s - unable to send LP_ConnectReq message, error=%d\n",
				__func__, NG_NODE_NAME(l2cap->node), error);
			goto out;
		}

		/* This should not fail */
		con = ng_l2cap_con_by_addr(l2cap, &ip->bdaddr, ip->linktype);
		KASSERT((con != NULL),
("%s: %s - could not find connection!\n", __func__, NG_NODE_NAME(l2cap->node)));
	}

	/* Create L2CAP command descriptor */
	cmd = ng_l2cap_new_cmd(con, NULL, ng_l2cap_get_ident(con),
			NG_L2CAP_INFO_REQ, msg->header.token);
	if (cmd == NULL) {
		error = ENOMEM;
		goto out;
	}

	if (cmd->ident == NG_L2CAP_NULL_IDENT) {
		ng_l2cap_free_cmd(cmd);
		error = EIO;
		goto out;
	}

	/* Create L2CAP command packet */
	_ng_l2cap_info_req(cmd->aux, cmd->ident, ip->info_type);
	if (cmd->aux == NULL) {
		ng_l2cap_free_cmd(cmd);
		error = ENOBUFS;
		goto out;
	}

        /* Link command to the queue */
	ng_l2cap_link_cmd(con, cmd);
	ng_l2cap_lp_deliver(con);
out:
	return (error);
} /* ng_l2cap_l2ca_get_info_req */

/*
 * Send L2CA_GetInfo response to the upper layer protocol
 */

int
ng_l2cap_l2ca_get_info_rsp(ng_l2cap_con_p con, u_int32_t token, 
		u_int16_t result, struct mbuf *data)
{
	ng_l2cap_p			 l2cap = con->l2cap;
	struct ng_mesg			*msg = NULL;
	ng_l2cap_l2ca_get_info_op	*op = NULL;
	int				 error = 0, size;

	/* Check if control hook is connected and valid */
	if (l2cap->ctl == NULL || NG_HOOK_NOT_VALID(l2cap->ctl)) {
		NG_L2CAP_WARN(
"%s: %s - unable to send L2CA_GetInfo response message. " \
"Hook is not connected or valid\n",
			__func__, NG_NODE_NAME(l2cap->node));
		error = ENOTCONN;
		goto out;
	}

	size = (data == NULL)? 0 : data->m_pkthdr.len;

	/* Create and send L2CA_GetInfo response message */
	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_GET_INFO,
		sizeof(*op) + size, M_NOWAIT);
	if (msg == NULL)
		error = ENOMEM;
	else {
		msg->header.token = token;
		msg->header.flags |= NGF_RESP;

		op = (ng_l2cap_l2ca_get_info_op *)(msg->data);
		op->result = result;
		if (data != NULL && size > 0) {
			op->info_size = size;
			m_copydata(data, 0, size, (caddr_t) op + sizeof(*op));
		}

		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->ctl, 0);
	}
out:
	NG_FREE_M(data);

	return (error);
} /* ng_l2cap_l2ca_get_info_rsp */
	
/*
 * Process L2CA_EnableCLT message from the upper layer protocol
 * XXX convert to NGN_L2CAP_NODE_SET_FLAGS?
 */

int
ng_l2cap_l2ca_enable_clt(ng_l2cap_p l2cap, struct ng_mesg *msg)
{
	ng_l2cap_l2ca_enable_clt_ip	*ip = NULL;
	int				 error = 0;
#if 0
 *	ng_l2cap_l2ca_enable_clt_op	*op = NULL;
 *	u_int16_t			 result; 
 * 	u_int32_t			 token;
#endif

	/* Check message */
	if (msg->header.arglen != sizeof(*ip)) {
		NG_L2CAP_ALERT(
"%s: %s - invalid L2CA_EnableCLT message size, size=%d\n",
			__func__, NG_NODE_NAME(l2cap->node),
			msg->header.arglen);

		return (EMSGSIZE);
	}

	/* Process request */
	ip = (ng_l2cap_l2ca_enable_clt_ip *) (msg->data);
#if 0
 *	result = NG_L2CAP_SUCCESS;
#endif

	switch (ip->psm) 
	{
	case 0:
		/* Special case: disable/enable all PSM */
		if (ip->enable)
			l2cap->flags &= ~(NG_L2CAP_CLT_SDP_DISABLED    |
					  NG_L2CAP_CLT_RFCOMM_DISABLED |
					  NG_L2CAP_CLT_TCP_DISABLED);
		else
			l2cap->flags |= (NG_L2CAP_CLT_SDP_DISABLED    |
					 NG_L2CAP_CLT_RFCOMM_DISABLED |
					 NG_L2CAP_CLT_TCP_DISABLED);
		break;

	case NG_L2CAP_PSM_SDP:
		if (ip->enable)
			l2cap->flags &= ~NG_L2CAP_CLT_SDP_DISABLED;
		else
			l2cap->flags |= NG_L2CAP_CLT_SDP_DISABLED;
		break;

	case NG_L2CAP_PSM_RFCOMM:
		if (ip->enable)
			l2cap->flags &= ~NG_L2CAP_CLT_RFCOMM_DISABLED;
		else
			l2cap->flags |= NG_L2CAP_CLT_RFCOMM_DISABLED;
		break;

	case NG_L2CAP_PSM_TCP:
		if (ip->enable)
			l2cap->flags &= ~NG_L2CAP_CLT_TCP_DISABLED;
		else
			l2cap->flags |= NG_L2CAP_CLT_TCP_DISABLED;
		break;
	
	default:
		NG_L2CAP_ERR(
"%s: %s - unsupported PSM=%d\n", __func__, NG_NODE_NAME(l2cap->node), ip->psm);
#if 0
 *		result = NG_L2CAP_PSM_NOT_SUPPORTED;
#endif
		error = ENOTSUP;
		break;
	}

#if 0
 *	/* Create and send response message */
 * 	token = msg->header.token;
 * 	NG_FREE_MSG(msg);
 * 	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_ENABLE_CLT,
 * 		sizeof(*op), M_NOWAIT);
 * 	if (msg == NULL)
 * 		error = ENOMEM;
 * 	else {
 * 		msg->header.token = token;
 * 		msg->header.flags |= NGF_RESP;
 * 
 * 		op = (ng_l2cap_l2ca_enable_clt_op *)(msg->data);
 * 		op->result = result;
 * 	}
 * 
 * 	/* Send response to control hook */
 * 	if (l2cap->ctl != NULL && NG_HOOK_IS_VALID(l2cap->ctl))
 * 		NG_SEND_MSG_HOOK(error, l2cap->node, msg, l2cap->ctl, 0);
#endif

	return (error);
} /* ng_l2cap_l2ca_enable_clt */


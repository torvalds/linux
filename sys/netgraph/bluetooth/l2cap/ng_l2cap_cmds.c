/*
 * ng_l2cap_cmds.c
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
 * $Id: ng_l2cap_cmds.c,v 1.2 2003/09/08 19:11:45 max Exp $
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
 **                    L2CAP commands processing module
 ******************************************************************************
 ******************************************************************************/

/*
 * Process L2CAP command queue on connection
 */

void
ng_l2cap_con_wakeup(ng_l2cap_con_p con)
{
	ng_l2cap_cmd_p	 cmd = NULL;
	struct mbuf	*m = NULL;
	int		 error = 0;

	/* Find first non-pending command in the queue */
	TAILQ_FOREACH(cmd, &con->cmd_list, next) {
		KASSERT((cmd->con == con),
("%s: %s - invalid connection pointer!\n",
			__func__, NG_NODE_NAME(con->l2cap->node)));

		if (!(cmd->flags & NG_L2CAP_CMD_PENDING))
			break;
	}

	if (cmd == NULL)
		return;

	/* Detach command packet */
	m = cmd->aux;
	cmd->aux = NULL;

	/* Process command */
	switch (cmd->code) {
	case NG_L2CAP_DISCON_RSP:
	case NG_L2CAP_ECHO_RSP:
	case NG_L2CAP_INFO_RSP:
		/*
		 * Do not check return ng_l2cap_lp_send() value, because
		 * in these cases we do not really have a graceful way out.
		 * ECHO and INFO responses are internal to the stack and not
		 * visible to user. REJect is just being nice to remote end
		 * (otherwise remote end will timeout anyway). DISCON is
		 * probably most interesting here, however, if it fails
		 * there is nothing we can do anyway.
		 */

		(void) ng_l2cap_lp_send(con, NG_L2CAP_SIGNAL_CID, m);
		ng_l2cap_unlink_cmd(cmd);
		ng_l2cap_free_cmd(cmd);
		break;
	case NG_L2CAP_CMD_REJ:
		(void) ng_l2cap_lp_send(con,
					(con->linktype == NG_HCI_LINK_ACL)?
					NG_L2CAP_SIGNAL_CID:
					NG_L2CAP_LESIGNAL_CID
					, m);
		ng_l2cap_unlink_cmd(cmd);
		ng_l2cap_free_cmd(cmd);
		break;
		
	case NG_L2CAP_CON_REQ:
		error = ng_l2cap_lp_send(con, NG_L2CAP_SIGNAL_CID, m);
		if (error != 0) {
			ng_l2cap_l2ca_con_rsp(cmd->ch, cmd->token,
				NG_L2CAP_NO_RESOURCES, 0);
			ng_l2cap_free_chan(cmd->ch); /* will free commands */
		} else
			ng_l2cap_command_timeout(cmd,
				bluetooth_l2cap_rtx_timeout());
		break;
	case NG_L2CAP_CON_RSP:
		error = ng_l2cap_lp_send(con, NG_L2CAP_SIGNAL_CID, m);
		ng_l2cap_unlink_cmd(cmd);
		if (cmd->ch != NULL) {
			ng_l2cap_l2ca_con_rsp_rsp(cmd->ch, cmd->token,
				(error == 0)? NG_L2CAP_SUCCESS : 
					NG_L2CAP_NO_RESOURCES);
			if (error != 0)
				ng_l2cap_free_chan(cmd->ch);
		}
		ng_l2cap_free_cmd(cmd);
		break;

	case NG_L2CAP_CFG_REQ:
		error = ng_l2cap_lp_send(con, NG_L2CAP_SIGNAL_CID, m);
		if (error != 0) {
			ng_l2cap_l2ca_cfg_rsp(cmd->ch, cmd->token,
				NG_L2CAP_NO_RESOURCES);
			ng_l2cap_unlink_cmd(cmd);
			ng_l2cap_free_cmd(cmd);
		} else
			ng_l2cap_command_timeout(cmd,
				bluetooth_l2cap_rtx_timeout());
		break;

	case NG_L2CAP_CFG_RSP:
		error = ng_l2cap_lp_send(con, NG_L2CAP_SIGNAL_CID, m);
		ng_l2cap_unlink_cmd(cmd);
		if (cmd->ch != NULL)
			ng_l2cap_l2ca_cfg_rsp_rsp(cmd->ch, cmd->token,
				(error == 0)? NG_L2CAP_SUCCESS :
					NG_L2CAP_NO_RESOURCES);
		ng_l2cap_free_cmd(cmd);
		break;

	case NG_L2CAP_DISCON_REQ:
		error = ng_l2cap_lp_send(con, NG_L2CAP_SIGNAL_CID, m);
		ng_l2cap_l2ca_discon_rsp(cmd->ch, cmd->token,
			(error == 0)? NG_L2CAP_SUCCESS : NG_L2CAP_NO_RESOURCES);
		if (error != 0)
			ng_l2cap_free_chan(cmd->ch); /* XXX free channel */
		else
			ng_l2cap_command_timeout(cmd,
				bluetooth_l2cap_rtx_timeout());
		break;

	case NG_L2CAP_ECHO_REQ:
		error = ng_l2cap_lp_send(con, NG_L2CAP_SIGNAL_CID, m);
		if (error != 0) {
			ng_l2cap_l2ca_ping_rsp(con, cmd->token,
					NG_L2CAP_NO_RESOURCES, NULL);
			ng_l2cap_unlink_cmd(cmd);
			ng_l2cap_free_cmd(cmd);
		} else
			ng_l2cap_command_timeout(cmd, 
				bluetooth_l2cap_rtx_timeout());
		break;

	case NG_L2CAP_INFO_REQ:
		error = ng_l2cap_lp_send(con, NG_L2CAP_SIGNAL_CID, m);
		if (error != 0) {
			ng_l2cap_l2ca_get_info_rsp(con, cmd->token, 
				NG_L2CAP_NO_RESOURCES, NULL);
			ng_l2cap_unlink_cmd(cmd);
			ng_l2cap_free_cmd(cmd);
		} else
			ng_l2cap_command_timeout(cmd, 
				bluetooth_l2cap_rtx_timeout());
		break;

	case NGM_L2CAP_L2CA_WRITE: {
		int	length = m->m_pkthdr.len;

		if (cmd->ch->dcid == NG_L2CAP_CLT_CID) {
			m = ng_l2cap_prepend(m, sizeof(ng_l2cap_clt_hdr_t));
			if (m == NULL)
				error = ENOBUFS;
			else
                		mtod(m, ng_l2cap_clt_hdr_t *)->psm =
							htole16(cmd->ch->psm);
		}

		if (error == 0)
			error = ng_l2cap_lp_send(con, cmd->ch->dcid, m);

		ng_l2cap_l2ca_write_rsp(cmd->ch, cmd->token,
			(error == 0)? NG_L2CAP_SUCCESS : NG_L2CAP_NO_RESOURCES,
			length);

		ng_l2cap_unlink_cmd(cmd);
		ng_l2cap_free_cmd(cmd);
		} break;
	case NG_L2CAP_CMD_PARAM_UPDATE_RESPONSE:
		error = ng_l2cap_lp_send(con, NG_L2CAP_LESIGNAL_CID, m);
		ng_l2cap_unlink_cmd(cmd);
		ng_l2cap_free_cmd(cmd);
		break;
	case NG_L2CAP_CMD_PARAM_UPDATE_REQUEST:
		  /*TBD.*/
	/* XXX FIXME add other commands */
	default:
		panic(
"%s: %s - unknown command code=%d\n",
			__func__, NG_NODE_NAME(con->l2cap->node), cmd->code);
		break;
	}
} /* ng_l2cap_con_wakeup */

/*
 * We have failed to open ACL connection to the remote unit. Could be negative
 * confirmation or timeout. So fail any "delayed" commands, notify upper layer,
 * remove all channels and remove connection descriptor.
 */

void
ng_l2cap_con_fail(ng_l2cap_con_p con, u_int16_t result)
{
	ng_l2cap_p	l2cap = con->l2cap;
	ng_l2cap_cmd_p	cmd = NULL;
	ng_l2cap_chan_p	ch = NULL;

	NG_L2CAP_INFO(
"%s: %s - ACL connection failed, result=%d\n",
		__func__, NG_NODE_NAME(l2cap->node), result);

	/* Connection is dying */
	con->flags |= NG_L2CAP_CON_DYING;

	/* Clean command queue */
	while (!TAILQ_EMPTY(&con->cmd_list)) {
		cmd = TAILQ_FIRST(&con->cmd_list);

		ng_l2cap_unlink_cmd(cmd);
		if(cmd->flags & NG_L2CAP_CMD_PENDING)
			ng_l2cap_command_untimeout(cmd);

		KASSERT((cmd->con == con),
("%s: %s - invalid connection pointer!\n",
			__func__, NG_NODE_NAME(l2cap->node)));

		switch (cmd->code) {
		case NG_L2CAP_CMD_REJ:
		case NG_L2CAP_DISCON_RSP:
		case NG_L2CAP_ECHO_RSP:
		case NG_L2CAP_INFO_RSP:
		case NG_L2CAP_CMD_PARAM_UPDATE_RESPONSE:
			break;

		case NG_L2CAP_CON_REQ:
			ng_l2cap_l2ca_con_rsp(cmd->ch, cmd->token, result, 0);
			break;

		case NG_L2CAP_CON_RSP:
			if (cmd->ch != NULL)
				ng_l2cap_l2ca_con_rsp_rsp(cmd->ch, cmd->token,
					result);
			break;

		case NG_L2CAP_CFG_REQ:
		case NG_L2CAP_CFG_RSP:
		case NGM_L2CAP_L2CA_WRITE:
			ng_l2cap_l2ca_discon_ind(cmd->ch);
			break;

		case NG_L2CAP_DISCON_REQ:
			ng_l2cap_l2ca_discon_rsp(cmd->ch, cmd->token,
				NG_L2CAP_SUCCESS);
			break;

		case NG_L2CAP_ECHO_REQ:
			ng_l2cap_l2ca_ping_rsp(cmd->con, cmd->token,
				result, NULL);
			break;

		case NG_L2CAP_INFO_REQ:
			ng_l2cap_l2ca_get_info_rsp(cmd->con, cmd->token,
				result, NULL);
			break;

		/* XXX FIXME add other commands */

		default:
			panic(
"%s: %s - unexpected command code=%d\n",
				__func__, NG_NODE_NAME(l2cap->node), cmd->code);
			break;
		}

		if (cmd->ch != NULL)
			ng_l2cap_free_chan(cmd->ch);

		ng_l2cap_free_cmd(cmd);
	}

	/*
	 * There still might be channels (in OPEN state?) that
	 * did not submit any commands, so disconnect them
	 */

	LIST_FOREACH(ch, &l2cap->chan_list, next)
		if (ch->con == con)
			ng_l2cap_l2ca_discon_ind(ch);

	/* Free connection descriptor */
	ng_l2cap_free_con(con);
} /* ng_l2cap_con_fail */

/*
 * Process L2CAP command timeout. In general - notify upper layer and destroy
 * channel. Do not pay much attention to return code, just do our best.
 */

void
ng_l2cap_process_command_timeout(node_p node, hook_p hook, void *arg1, int arg2)
{
	ng_l2cap_p	l2cap = NULL;
	ng_l2cap_con_p	con = NULL;
	ng_l2cap_cmd_p	cmd = NULL;
	u_int16_t	con_handle = (arg2 & 0x0ffff);
	u_int8_t	ident = ((arg2 >> 16) & 0xff);

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

	cmd = ng_l2cap_cmd_by_ident(con, ident);
	if (cmd == NULL) {
		NG_L2CAP_ALERT(
"%s: %s - could not find command, con_handle=%d, ident=%d\n",
			__func__, NG_NODE_NAME(node), con_handle, ident);
		return;
	}

	cmd->flags &= ~NG_L2CAP_CMD_PENDING;
	ng_l2cap_unlink_cmd(cmd);

	switch (cmd->code) {
 	case NG_L2CAP_CON_REQ:
		ng_l2cap_l2ca_con_rsp(cmd->ch, cmd->token, NG_L2CAP_TIMEOUT, 0);
		ng_l2cap_free_chan(cmd->ch); 
		break;

	case NG_L2CAP_CFG_REQ:
		ng_l2cap_l2ca_cfg_rsp(cmd->ch, cmd->token, NG_L2CAP_TIMEOUT);
		break;

 	case NG_L2CAP_DISCON_REQ:
		ng_l2cap_l2ca_discon_rsp(cmd->ch, cmd->token, NG_L2CAP_TIMEOUT);
		ng_l2cap_free_chan(cmd->ch); /* XXX free channel */
		break;

	case NG_L2CAP_ECHO_REQ:
		/* Echo request timed out. Let the upper layer know */
		ng_l2cap_l2ca_ping_rsp(cmd->con, cmd->token,
			NG_L2CAP_TIMEOUT, NULL);
		break;

	case NG_L2CAP_INFO_REQ:
		/* Info request timed out. Let the upper layer know */
		ng_l2cap_l2ca_get_info_rsp(cmd->con, cmd->token,
			NG_L2CAP_TIMEOUT, NULL);
		break;

	/* XXX FIXME add other commands */

	default:
		panic(
"%s: %s - unexpected command code=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), cmd->code);
		break;
	}

	ng_l2cap_free_cmd(cmd);
} /* ng_l2cap_process_command_timeout */


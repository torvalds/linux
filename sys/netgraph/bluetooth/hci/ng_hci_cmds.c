/*
 * ng_hci_cmds.c
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
 * $Id: ng_hci_cmds.c,v 1.4 2003/09/08 18:57:51 max Exp $
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
 **                     HCI commands processing module
 ******************************************************************************
 ******************************************************************************/

#undef	min
#define	min(a, b)	((a) < (b))? (a) : (b)

static int  complete_command (ng_hci_unit_p, int, struct mbuf **);

static int process_link_control_params
	(ng_hci_unit_p, u_int16_t, struct mbuf *, struct mbuf *);
static int process_link_policy_params
	(ng_hci_unit_p, u_int16_t, struct mbuf *, struct mbuf *);
static int process_hc_baseband_params
	(ng_hci_unit_p, u_int16_t, struct mbuf *, struct mbuf *);
static int process_info_params
	(ng_hci_unit_p, u_int16_t, struct mbuf *, struct mbuf *);
static int process_status_params
	(ng_hci_unit_p, u_int16_t, struct mbuf *, struct mbuf *);
static int process_testing_params
	(ng_hci_unit_p, u_int16_t, struct mbuf *, struct mbuf *);
static int process_le_params
	(ng_hci_unit_p, u_int16_t, struct mbuf *, struct mbuf *);

static int process_link_control_status
	(ng_hci_unit_p, ng_hci_command_status_ep *, struct mbuf *);
static int process_link_policy_status
	(ng_hci_unit_p, ng_hci_command_status_ep *, struct mbuf *);
static int process_le_status
	(ng_hci_unit_p, ng_hci_command_status_ep *, struct mbuf *);

/*
 * Send HCI command to the driver.
 */

int
ng_hci_send_command(ng_hci_unit_p unit)
{
	struct mbuf	*m0 = NULL, *m = NULL;
	int		 free, error = 0;

	/* Check if other command is pending */
	if (unit->state & NG_HCI_UNIT_COMMAND_PENDING)
		return (0);

	/* Check if unit can accept our command */
	NG_HCI_BUFF_CMD_GET(unit->buffer, free);
	if (free == 0)
		return (0);

	/* Check if driver hook is still ok */
	if (unit->drv == NULL || NG_HOOK_NOT_VALID(unit->drv)) {
		NG_HCI_WARN(
"%s: %s - hook \"%s\" is not connected or valid\n",
			__func__, NG_NODE_NAME(unit->node), NG_HCI_HOOK_DRV);

		NG_BT_MBUFQ_DRAIN(&unit->cmdq);

		return (ENOTCONN);
	}

	/* 
	 * Get first command from queue, give it to RAW hook then 
	 * make copy of it and send it to the driver
	 */

	m0 = NG_BT_MBUFQ_FIRST(&unit->cmdq);
	if (m0 == NULL)
		return (0);

	ng_hci_mtap(unit, m0);

	m = m_dup(m0, M_NOWAIT);
	if (m != NULL)
		NG_SEND_DATA_ONLY(error, unit->drv, m);
	else
		error = ENOBUFS;

	if (error != 0)
		NG_HCI_ERR(
"%s: %s - could not send HCI command, error=%d\n",
			__func__, NG_NODE_NAME(unit->node), error);

	/*
	 * Even if we were not able to send command we still pretend
	 * that everything is OK and let timeout handle that.
	 */

	NG_HCI_BUFF_CMD_USE(unit->buffer, 1);
	NG_HCI_STAT_CMD_SENT(unit->stat);
	NG_HCI_STAT_BYTES_SENT(unit->stat, m0->m_pkthdr.len);

	/*
	 * Note: ng_hci_command_timeout() will set 
	 * NG_HCI_UNIT_COMMAND_PENDING flag
	 */

	ng_hci_command_timeout(unit);

	return (0);
} /* ng_hci_send_command */

/*
 * Process HCI Command_Compete event. Complete HCI command, and do post 
 * processing on the command parameters (cp) and command return parameters
 * (e) if required (for example adjust state).
 */

int
ng_hci_process_command_complete(ng_hci_unit_p unit, struct mbuf *e)
{
	ng_hci_command_compl_ep		*ep = NULL;
	struct mbuf			*cp = NULL;
	int				 error = 0;

	/* Get event packet and update command buffer info */
	NG_HCI_M_PULLUP(e, sizeof(*ep));
	if (e == NULL)
		return (ENOBUFS); /* XXX this is bad */

	ep = mtod(e, ng_hci_command_compl_ep *);
        NG_HCI_BUFF_CMD_SET(unit->buffer, ep->num_cmd_pkts);

	/* Check for special NOOP command */
	if (ep->opcode == 0x0000) {
		NG_FREE_M(e);
		goto out;
	}

	/* Try to match first command item in the queue */
	error = complete_command(unit, ep->opcode, &cp);
	if (error != 0) {
		NG_FREE_M(e);
		goto out;
	}

	/* 
	 * Perform post processing on command parameters and return parameters
	 * do it only if status is OK (status == 0). Status is the first byte
	 * of any command return parameters.
	 */

	ep->opcode = le16toh(ep->opcode);
	m_adj(e, sizeof(*ep));

	if (*mtod(e, u_int8_t *) == 0) { /* XXX m_pullup here? */
		switch (NG_HCI_OGF(ep->opcode)) {
		case NG_HCI_OGF_LINK_CONTROL:
			error = process_link_control_params(unit,
					NG_HCI_OCF(ep->opcode), cp, e);
			break;

		case NG_HCI_OGF_LINK_POLICY:
			error = process_link_policy_params(unit,
					NG_HCI_OCF(ep->opcode), cp, e);
			break;

		case NG_HCI_OGF_HC_BASEBAND:
			error = process_hc_baseband_params(unit,
					NG_HCI_OCF(ep->opcode), cp, e);
			break;

		case NG_HCI_OGF_INFO:
			error = process_info_params(unit,
					NG_HCI_OCF(ep->opcode), cp, e);
			break;

		case NG_HCI_OGF_STATUS:
			error = process_status_params(unit,
					NG_HCI_OCF(ep->opcode), cp, e);
			break;

		case NG_HCI_OGF_TESTING:
			error = process_testing_params(unit,
					NG_HCI_OCF(ep->opcode), cp, e);
			break;
		case NG_HCI_OGF_LE:
			error = process_le_params(unit,
					  NG_HCI_OCF(ep->opcode), cp, e);
			break;
		case NG_HCI_OGF_BT_LOGO:
		case NG_HCI_OGF_VENDOR:
			NG_FREE_M(cp);
			NG_FREE_M(e);
			break;

		default:
			NG_FREE_M(cp);
			NG_FREE_M(e);
			error = EINVAL;
			break;
		}
	} else {
		NG_HCI_ERR(
"%s: %s - HCI command failed, OGF=%#x, OCF=%#x, status=%#x\n",
			__func__, NG_NODE_NAME(unit->node),
			NG_HCI_OGF(ep->opcode), NG_HCI_OCF(ep->opcode), 
			*mtod(e, u_int8_t *));

		NG_FREE_M(cp);
		NG_FREE_M(e);
	}
out:
	ng_hci_send_command(unit);

	return (error);
} /* ng_hci_process_command_complete */

/*
 * Process HCI Command_Status event. Check the status (mst) and do post 
 * processing (if required).
 */

int
ng_hci_process_command_status(ng_hci_unit_p unit, struct mbuf *e)
{
	ng_hci_command_status_ep	*ep = NULL;
	struct mbuf			*cp = NULL;
	int				 error = 0;

	/* Update command buffer info */
	NG_HCI_M_PULLUP(e, sizeof(*ep));
	if (e == NULL)
		return (ENOBUFS); /* XXX this is bad */

	ep = mtod(e, ng_hci_command_status_ep *);
	NG_HCI_BUFF_CMD_SET(unit->buffer, ep->num_cmd_pkts);

	/* Check for special NOOP command */
	if (ep->opcode == 0x0000)
		goto out;

	/* Try to match first command item in the queue */
	error = complete_command(unit, ep->opcode, &cp);
        if (error != 0)
		goto out;

	/* 
	 * Perform post processing on HCI Command_Status event
	 */

	ep->opcode = le16toh(ep->opcode);

	switch (NG_HCI_OGF(ep->opcode)) {
	case NG_HCI_OGF_LINK_CONTROL:
		error = process_link_control_status(unit, ep, cp);
		break;

	case NG_HCI_OGF_LINK_POLICY:
		error = process_link_policy_status(unit, ep, cp);
		break;
	case NG_HCI_OGF_LE:
		error = process_le_status(unit, ep, cp);
		break;
	case NG_HCI_OGF_BT_LOGO:
	case NG_HCI_OGF_VENDOR:
		NG_FREE_M(cp);
		break;

	case NG_HCI_OGF_HC_BASEBAND:
	case NG_HCI_OGF_INFO:
	case NG_HCI_OGF_STATUS:
	case NG_HCI_OGF_TESTING:
	default:
		NG_FREE_M(cp);
		error = EINVAL;
		break;
	}
out:
	NG_FREE_M(e);
	ng_hci_send_command(unit);

	return (error);
} /* ng_hci_process_command_status */

/*
 * Complete queued HCI command. 
 */

static int
complete_command(ng_hci_unit_p unit, int opcode, struct mbuf **cp)
{
	struct mbuf	*m = NULL;

	/* Check unit state */
	if (!(unit->state & NG_HCI_UNIT_COMMAND_PENDING)) {
		NG_HCI_ALERT(
"%s: %s - no pending command, state=%#x\n",
			__func__, NG_NODE_NAME(unit->node), unit->state);

		return (EINVAL);
	}

	/* Get first command in the queue */
	m = NG_BT_MBUFQ_FIRST(&unit->cmdq);
	if (m == NULL) {
		NG_HCI_ALERT(
"%s: %s - empty command queue?!\n", __func__, NG_NODE_NAME(unit->node));

		return (EINVAL);
	}

	/*
	 * Match command opcode, if does not match - do nothing and 
	 * let timeout handle that.
	 */

	if (mtod(m, ng_hci_cmd_pkt_t *)->opcode != opcode) {
		NG_HCI_ALERT(
"%s: %s - command queue is out of sync\n", __func__, NG_NODE_NAME(unit->node));

		return (EINVAL);
	}

	/* 
	 * Now we can remove command timeout, dequeue completed command
	 * and return command parameters. ng_hci_command_untimeout will
	 * drop NG_HCI_UNIT_COMMAND_PENDING flag.
	 * Note: if ng_hci_command_untimeout() fails (returns non-zero)
	 * then timeout already happened and timeout message went info node
	 * queue. In this case we ignore command completion and pretend
	 * there is a timeout.
	 */

	if (ng_hci_command_untimeout(unit) != 0)
		return (ETIMEDOUT);

	NG_BT_MBUFQ_DEQUEUE(&unit->cmdq, *cp);
	m_adj(*cp, sizeof(ng_hci_cmd_pkt_t));

	return (0);
} /* complete_command */

/*
 * Process HCI command timeout
 */

void
ng_hci_process_command_timeout(node_p node, hook_p hook, void *arg1, int arg2)
{
	ng_hci_unit_p	 unit = NULL;
	struct mbuf	*m = NULL;
	u_int16_t	 opcode;

	if (NG_NODE_NOT_VALID(node)) {
		printf("%s: Netgraph node is not valid\n", __func__);
		return;
	}

	unit = (ng_hci_unit_p) NG_NODE_PRIVATE(node);

	if (unit->state & NG_HCI_UNIT_COMMAND_PENDING) {
		unit->state &= ~NG_HCI_UNIT_COMMAND_PENDING;

		NG_BT_MBUFQ_DEQUEUE(&unit->cmdq, m);
		if (m == NULL) {
			NG_HCI_ALERT(
"%s: %s - command queue is out of sync!\n", __func__, NG_NODE_NAME(unit->node));

			return;
		}

		opcode = le16toh(mtod(m, ng_hci_cmd_pkt_t *)->opcode);
		NG_FREE_M(m);

		NG_HCI_ERR(
"%s: %s - unable to complete HCI command OGF=%#x, OCF=%#x. Timeout\n",
			__func__, NG_NODE_NAME(unit->node), NG_HCI_OGF(opcode),
			NG_HCI_OCF(opcode));

		/* Try to send more commands */
 		NG_HCI_BUFF_CMD_SET(unit->buffer, 1);
		ng_hci_send_command(unit);
	} else
		NG_HCI_ALERT(
"%s: %s - no pending command\n", __func__, NG_NODE_NAME(unit->node));
} /* ng_hci_process_command_timeout */

/* 
 * Process link command return parameters
 */

static int
process_link_control_params(ng_hci_unit_p unit, u_int16_t ocf, 
		struct mbuf *mcp, struct mbuf *mrp)
{
	int	error  = 0;

	switch (ocf) {
	case NG_HCI_OCF_INQUIRY_CANCEL:
	case NG_HCI_OCF_PERIODIC_INQUIRY:
	case NG_HCI_OCF_EXIT_PERIODIC_INQUIRY:
	case NG_HCI_OCF_LINK_KEY_REP:
	case NG_HCI_OCF_LINK_KEY_NEG_REP: 
	case NG_HCI_OCF_PIN_CODE_REP:
	case NG_HCI_OCF_PIN_CODE_NEG_REP:
		/* These do not need post processing */
		break;

	case NG_HCI_OCF_INQUIRY:
	case NG_HCI_OCF_CREATE_CON:
	case NG_HCI_OCF_DISCON:
	case NG_HCI_OCF_ADD_SCO_CON:
	case NG_HCI_OCF_ACCEPT_CON:
	case NG_HCI_OCF_REJECT_CON:
	case NG_HCI_OCF_CHANGE_CON_PKT_TYPE:
	case NG_HCI_OCF_AUTH_REQ:
	case NG_HCI_OCF_SET_CON_ENCRYPTION:
	case NG_HCI_OCF_CHANGE_CON_LINK_KEY:
	case NG_HCI_OCF_MASTER_LINK_KEY:
	case NG_HCI_OCF_REMOTE_NAME_REQ:
	case NG_HCI_OCF_READ_REMOTE_FEATURES:
	case NG_HCI_OCF_READ_REMOTE_VER_INFO:
	case NG_HCI_OCF_READ_CLOCK_OFFSET:
	default:

		/*
		 * None of these command was supposed to generate 
		 * Command_Complete event. Instead Command_Status event 
		 * should have been generated and then appropriate event 
		 * should have been sent to indicate the final result.
		 */

		error = EINVAL;
		break;
	}

	NG_FREE_M(mcp);
	NG_FREE_M(mrp);

	return (error);
} /* process_link_control_params */

/* 
 * Process link policy command return parameters
 */

static int
process_link_policy_params(ng_hci_unit_p unit, u_int16_t ocf,
		struct mbuf *mcp, struct mbuf *mrp)
{
	int	error = 0;

	switch (ocf){
	case NG_HCI_OCF_ROLE_DISCOVERY: {
		ng_hci_role_discovery_rp	*rp = NULL;
		ng_hci_unit_con_t		*con = NULL;
		u_int16_t			 h;

		NG_HCI_M_PULLUP(mrp, sizeof(*rp));
		if (mrp != NULL) {
			rp = mtod(mrp, ng_hci_role_discovery_rp *);

			h = NG_HCI_CON_HANDLE(le16toh(rp->con_handle));
			con = ng_hci_con_by_handle(unit, h);
			if (con == NULL) {
				NG_HCI_ALERT(
"%s: %s - invalid connection handle=%d\n",
					__func__, NG_NODE_NAME(unit->node), h); 
				error = ENOENT;
			} else if (con->link_type != NG_HCI_LINK_ACL) {
				NG_HCI_ALERT(
"%s: %s - invalid link type=%d\n", __func__, NG_NODE_NAME(unit->node),
					con->link_type);
				error = EINVAL;
			} else
				con->role = rp->role;
		} else
			error = ENOBUFS;
		} break;

	case NG_HCI_OCF_READ_LINK_POLICY_SETTINGS:
	case NG_HCI_OCF_WRITE_LINK_POLICY_SETTINGS:
		/* These do not need post processing */
		break;
	
	case NG_HCI_OCF_HOLD_MODE:
	case NG_HCI_OCF_SNIFF_MODE:
	case NG_HCI_OCF_EXIT_SNIFF_MODE:
	case NG_HCI_OCF_PARK_MODE:
	case NG_HCI_OCF_EXIT_PARK_MODE:
	case NG_HCI_OCF_QOS_SETUP:
	case NG_HCI_OCF_SWITCH_ROLE:
	default:

		/*
		 * None of these command was supposed to generate 
		 * Command_Complete event. Instead Command_Status event 
		 * should have been generated and then appropriate event
		 * should have been sent to indicate the final result.
		 */

		error = EINVAL;
		break;
	} 

	NG_FREE_M(mcp);
	NG_FREE_M(mrp);

	return (error);
} /* process_link_policy_params */

/* 
 * Process HC and baseband command return parameters
 */

int
process_hc_baseband_params(ng_hci_unit_p unit, u_int16_t ocf, 
		struct mbuf *mcp, struct mbuf *mrp)
{
	int	error = 0;

	switch (ocf) {
	case NG_HCI_OCF_SET_EVENT_MASK:
	case NG_HCI_OCF_SET_EVENT_FILTER:
	case NG_HCI_OCF_FLUSH:	/* XXX Do we need to handle that? */
	case NG_HCI_OCF_READ_PIN_TYPE:
	case NG_HCI_OCF_WRITE_PIN_TYPE:
	case NG_HCI_OCF_CREATE_NEW_UNIT_KEY:
	case NG_HCI_OCF_WRITE_STORED_LINK_KEY:
	case NG_HCI_OCF_WRITE_CON_ACCEPT_TIMO:
	case NG_HCI_OCF_WRITE_PAGE_TIMO:
	case NG_HCI_OCF_READ_SCAN_ENABLE:
	case NG_HCI_OCF_WRITE_SCAN_ENABLE:
	case NG_HCI_OCF_WRITE_PAGE_SCAN_ACTIVITY:
	case NG_HCI_OCF_WRITE_INQUIRY_SCAN_ACTIVITY:
	case NG_HCI_OCF_READ_AUTH_ENABLE:
	case NG_HCI_OCF_WRITE_AUTH_ENABLE:
	case NG_HCI_OCF_READ_ENCRYPTION_MODE:
	case NG_HCI_OCF_WRITE_ENCRYPTION_MODE:
	case NG_HCI_OCF_WRITE_VOICE_SETTINGS:
	case NG_HCI_OCF_READ_NUM_BROADCAST_RETRANS:
	case NG_HCI_OCF_WRITE_NUM_BROADCAST_RETRANS:
	case NG_HCI_OCF_READ_HOLD_MODE_ACTIVITY:
	case NG_HCI_OCF_WRITE_HOLD_MODE_ACTIVITY:
	case NG_HCI_OCF_READ_SCO_FLOW_CONTROL:
	case NG_HCI_OCF_WRITE_SCO_FLOW_CONTROL:
	case NG_HCI_OCF_H2HC_FLOW_CONTROL: /* XXX Not supported this time */
	case NG_HCI_OCF_HOST_BUFFER_SIZE:
	case NG_HCI_OCF_READ_IAC_LAP:
	case NG_HCI_OCF_WRITE_IAC_LAP:
	case NG_HCI_OCF_READ_PAGE_SCAN_PERIOD:
	case NG_HCI_OCF_WRITE_PAGE_SCAN_PERIOD:
	case NG_HCI_OCF_READ_PAGE_SCAN:
	case NG_HCI_OCF_WRITE_PAGE_SCAN:
	case NG_HCI_OCF_READ_LINK_SUPERVISION_TIMO:
	case NG_HCI_OCF_WRITE_LINK_SUPERVISION_TIMO:
	case NG_HCI_OCF_READ_SUPPORTED_IAC_NUM:
	case NG_HCI_OCF_READ_STORED_LINK_KEY:
	case NG_HCI_OCF_DELETE_STORED_LINK_KEY:
	case NG_HCI_OCF_READ_CON_ACCEPT_TIMO:
	case NG_HCI_OCF_READ_PAGE_TIMO:
	case NG_HCI_OCF_READ_PAGE_SCAN_ACTIVITY:
	case NG_HCI_OCF_READ_INQUIRY_SCAN_ACTIVITY:
	case NG_HCI_OCF_READ_VOICE_SETTINGS:
	case NG_HCI_OCF_READ_AUTO_FLUSH_TIMO:
	case NG_HCI_OCF_WRITE_AUTO_FLUSH_TIMO:
	case NG_HCI_OCF_READ_XMIT_LEVEL:
	case NG_HCI_OCF_HOST_NUM_COMPL_PKTS:	/* XXX Can get here? */
	case NG_HCI_OCF_CHANGE_LOCAL_NAME:
	case NG_HCI_OCF_READ_LOCAL_NAME:
	case NG_HCI_OCF_READ_UNIT_CLASS:
	case NG_HCI_OCF_WRITE_UNIT_CLASS:
	case NG_HCI_OCF_READ_LE_HOST_SUPPORTED:
	case NG_HCI_OCF_WRITE_LE_HOST_SUPPORTED:
		/* These do not need post processing */
		break;

	case NG_HCI_OCF_RESET: {
		ng_hci_unit_con_p	con = NULL;
		int			size;

		/*
		 * XXX 
		 *
		 * After RESET command unit goes into standby mode
		 * and all operational state is lost. Host controller
		 * will revert to default values for all parameters.
		 * 
		 * For now we shall terminate all connections and drop
		 * inited bit. After RESET unit must be re-initialized.
		 */

		while (!LIST_EMPTY(&unit->con_list)) {
			con = LIST_FIRST(&unit->con_list);

			/* Remove all timeouts (if any) */
			if (con->flags & NG_HCI_CON_TIMEOUT_PENDING)
				ng_hci_con_untimeout(con);

			/* Connection terminated by local host */
			ng_hci_lp_discon_ind(con, 0x16);
			ng_hci_free_con(con);
		}

		NG_HCI_BUFF_ACL_TOTAL(unit->buffer, size);
		NG_HCI_BUFF_ACL_FREE(unit->buffer, size);

		NG_HCI_BUFF_SCO_TOTAL(unit->buffer, size);
		NG_HCI_BUFF_SCO_FREE(unit->buffer, size);

		unit->state &= ~NG_HCI_UNIT_INITED;
		} break;

	default:
		error = EINVAL;
		break;
	}

	NG_FREE_M(mcp);
	NG_FREE_M(mrp);

	return (error);
} /* process_hc_baseband_params */

/* 
 * Process info command return parameters
 */

static int
process_info_params(ng_hci_unit_p unit, u_int16_t ocf, struct mbuf *mcp,
		struct mbuf *mrp)
{
	int	error = 0, len;

	switch (ocf) {
	case NG_HCI_OCF_READ_LOCAL_VER:
	case NG_HCI_OCF_READ_COUNTRY_CODE:
		break;

	case NG_HCI_OCF_READ_LOCAL_FEATURES:
		m_adj(mrp, sizeof(u_int8_t));
		len = min(mrp->m_pkthdr.len, sizeof(unit->features));
		m_copydata(mrp, 0, len, (caddr_t) unit->features);
		break;

	case NG_HCI_OCF_READ_BUFFER_SIZE: {
		ng_hci_read_buffer_size_rp	*rp = NULL;

		/* Do not update buffer descriptor if node was initialized */
		if ((unit->state & NG_HCI_UNIT_READY) == NG_HCI_UNIT_READY)
			break;

		NG_HCI_M_PULLUP(mrp, sizeof(*rp));
		if (mrp != NULL) {
			rp = mtod(mrp, ng_hci_read_buffer_size_rp *);

			NG_HCI_BUFF_ACL_SET(
				unit->buffer,
				le16toh(rp->num_acl_pkt),  /* number */
				le16toh(rp->max_acl_size), /* size */
				le16toh(rp->num_acl_pkt)   /* free */
			);

			NG_HCI_BUFF_SCO_SET(
				unit->buffer,
				le16toh(rp->num_sco_pkt), /* number */
				rp->max_sco_size,         /* size */
				le16toh(rp->num_sco_pkt)  /* free */
			);

			/* Let upper layers know */
			ng_hci_node_is_up(unit->node, unit->acl, NULL, 0);
			ng_hci_node_is_up(unit->node, unit->sco, NULL, 0);
		} else
			error = ENOBUFS;
		} break;

	case NG_HCI_OCF_READ_BDADDR:
		/* Do not update BD_ADDR if node was initialized */
		if ((unit->state & NG_HCI_UNIT_READY) == NG_HCI_UNIT_READY)
			break;

		m_adj(mrp, sizeof(u_int8_t));
		len = min(mrp->m_pkthdr.len, sizeof(unit->bdaddr));
		m_copydata(mrp, 0, len, (caddr_t) &unit->bdaddr);

		/* Let upper layers know */
		ng_hci_node_is_up(unit->node, unit->acl, NULL, 0);
		ng_hci_node_is_up(unit->node, unit->sco, NULL, 0);
		break;
	
	default:
		error = EINVAL;
		break;
	}

	NG_FREE_M(mcp);
	NG_FREE_M(mrp);

	return (error);
} /* process_info_params */

/* 
 * Process status command return parameters
 */

static int
process_status_params(ng_hci_unit_p unit, u_int16_t ocf, struct mbuf *mcp,
		struct mbuf *mrp)
{
	int	error = 0;

	switch (ocf) {
	case NG_HCI_OCF_READ_FAILED_CONTACT_CNTR:
	case NG_HCI_OCF_RESET_FAILED_CONTACT_CNTR:
	case NG_HCI_OCF_GET_LINK_QUALITY:
	case NG_HCI_OCF_READ_RSSI:
		/* These do not need post processing */
		break;

	default:
		error = EINVAL;
		break;
	}

	NG_FREE_M(mcp);
	NG_FREE_M(mrp);

	return (error);
} /* process_status_params */

/* 
 * Process testing command return parameters
 */

int
process_testing_params(ng_hci_unit_p unit, u_int16_t ocf, struct mbuf *mcp,
		struct mbuf *mrp)
{
	int	error = 0;

	switch (ocf) {

	/*
	 * XXX FIXME
	 * We do not support these features at this time. However,
	 * HCI node could support this and do something smart. At least
	 * node can change unit state.
	 */
 
	case NG_HCI_OCF_READ_LOOPBACK_MODE:
	case NG_HCI_OCF_WRITE_LOOPBACK_MODE:
	case NG_HCI_OCF_ENABLE_UNIT_UNDER_TEST:
		break;

	default:
		error = EINVAL;
		break;
	}

	NG_FREE_M(mcp);
	NG_FREE_M(mrp);

	return (error);
} /* process_testing_params */

/* 
 * Process LE command return parameters
 */

static int
process_le_params(ng_hci_unit_p unit, u_int16_t ocf,
		struct mbuf *mcp, struct mbuf *mrp)
{
	int	error = 0;

	switch (ocf){
	case NG_HCI_OCF_LE_SET_EVENT_MASK:
	case NG_HCI_OCF_LE_READ_BUFFER_SIZE:
	case NG_HCI_OCF_LE_READ_LOCAL_SUPPORTED_FEATURES:
	case NG_HCI_OCF_LE_SET_RANDOM_ADDRESS:
	case NG_HCI_OCF_LE_SET_ADVERTISING_PARAMETERS:
	case NG_HCI_OCF_LE_READ_ADVERTISING_CHANNEL_TX_POWER:
	case NG_HCI_OCF_LE_SET_ADVERTISING_DATA:
	case NG_HCI_OCF_LE_SET_SCAN_RESPONSE_DATA:
	case NG_HCI_OCF_LE_SET_ADVERTISE_ENABLE:
	case NG_HCI_OCF_LE_SET_SCAN_PARAMETERS:
	case NG_HCI_OCF_LE_SET_SCAN_ENABLE:
	case NG_HCI_OCF_LE_CREATE_CONNECTION_CANCEL:
	case NG_HCI_OCF_LE_CLEAR_WHITE_LIST:
	case NG_HCI_OCF_LE_READ_WHITE_LIST_SIZE:
	case NG_HCI_OCF_LE_ADD_DEVICE_TO_WHITE_LIST:
	case NG_HCI_OCF_LE_REMOVE_DEVICE_FROM_WHITE_LIST:
	case NG_HCI_OCF_LE_SET_HOST_CHANNEL_CLASSIFICATION:
	case NG_HCI_OCF_LE_READ_CHANNEL_MAP:
	case NG_HCI_OCF_LE_ENCRYPT:
	case NG_HCI_OCF_LE_RAND:
	case NG_HCI_OCF_LE_LONG_TERM_KEY_REQUEST_REPLY:
	case NG_HCI_OCF_LE_LONG_TERM_KEY_REQUEST_NEGATIVE_REPLY:
	case NG_HCI_OCF_LE_READ_SUPPORTED_STATUS:
	case NG_HCI_OCF_LE_RECEIVER_TEST:
	case NG_HCI_OCF_LE_TRANSMITTER_TEST:
	case NG_HCI_OCF_LE_TEST_END:

		/* These do not need post processing */
		break;
	case NG_HCI_OCF_LE_CREATE_CONNECTION:
	case NG_HCI_OCF_LE_CONNECTION_UPDATE:
	case NG_HCI_OCF_LE_READ_REMOTE_USED_FEATURES:
	case NG_HCI_OCF_LE_START_ENCRYPTION:


	default:
		/*
		 * None of these command was supposed to generate 
		 * Command_Complete event. Instead Command_Status event 
		 * should have been generated and then appropriate event
		 * should have been sent to indicate the final result.
		 */

		error = EINVAL;
		break;
	} 

	NG_FREE_M(mcp);
	NG_FREE_M(mrp);

	return (error);

}



static int
process_le_status(ng_hci_unit_p unit,ng_hci_command_status_ep *ep,
		struct mbuf *mcp)
{
	int	error = 0;

	switch (NG_HCI_OCF(ep->opcode)){
	case NG_HCI_OCF_LE_CREATE_CONNECTION:
	case NG_HCI_OCF_LE_CONNECTION_UPDATE:
	case NG_HCI_OCF_LE_READ_REMOTE_USED_FEATURES:
	case NG_HCI_OCF_LE_START_ENCRYPTION:

		/* These do not need post processing */
		break;

	case NG_HCI_OCF_LE_SET_EVENT_MASK:
	case NG_HCI_OCF_LE_READ_BUFFER_SIZE:
	case NG_HCI_OCF_LE_READ_LOCAL_SUPPORTED_FEATURES:
	case NG_HCI_OCF_LE_SET_RANDOM_ADDRESS:
	case NG_HCI_OCF_LE_SET_ADVERTISING_PARAMETERS:
	case NG_HCI_OCF_LE_READ_ADVERTISING_CHANNEL_TX_POWER:
	case NG_HCI_OCF_LE_SET_ADVERTISING_DATA:
	case NG_HCI_OCF_LE_SET_SCAN_RESPONSE_DATA:
	case NG_HCI_OCF_LE_SET_ADVERTISE_ENABLE:
	case NG_HCI_OCF_LE_SET_SCAN_PARAMETERS:
	case NG_HCI_OCF_LE_SET_SCAN_ENABLE:
	case NG_HCI_OCF_LE_CREATE_CONNECTION_CANCEL:
	case NG_HCI_OCF_LE_CLEAR_WHITE_LIST:
	case NG_HCI_OCF_LE_READ_WHITE_LIST_SIZE:
	case NG_HCI_OCF_LE_ADD_DEVICE_TO_WHITE_LIST:
	case NG_HCI_OCF_LE_REMOVE_DEVICE_FROM_WHITE_LIST:
	case NG_HCI_OCF_LE_SET_HOST_CHANNEL_CLASSIFICATION:
	case NG_HCI_OCF_LE_READ_CHANNEL_MAP:
	case NG_HCI_OCF_LE_ENCRYPT:
	case NG_HCI_OCF_LE_RAND:
	case NG_HCI_OCF_LE_LONG_TERM_KEY_REQUEST_REPLY:
	case NG_HCI_OCF_LE_LONG_TERM_KEY_REQUEST_NEGATIVE_REPLY:
	case NG_HCI_OCF_LE_READ_SUPPORTED_STATUS:
	case NG_HCI_OCF_LE_RECEIVER_TEST:
	case NG_HCI_OCF_LE_TRANSMITTER_TEST:
	case NG_HCI_OCF_LE_TEST_END:


	default:
		/*
		 * None of these command was supposed to generate 
		 * Command_Stutus event. Command Complete instead.
		 */

		error = EINVAL;
		break;
	} 

	NG_FREE_M(mcp);

	return (error);

}

/*
 * Process link control command status
 */

static int
process_link_control_status(ng_hci_unit_p unit, ng_hci_command_status_ep *ep,
		struct mbuf *mcp)
{
	int	error = 0;

	switch (NG_HCI_OCF(ep->opcode)) {
	case NG_HCI_OCF_INQUIRY:
	case NG_HCI_OCF_DISCON:		/* XXX */
	case NG_HCI_OCF_REJECT_CON:	/* XXX */
	case NG_HCI_OCF_CHANGE_CON_PKT_TYPE:
	case NG_HCI_OCF_AUTH_REQ:
	case NG_HCI_OCF_SET_CON_ENCRYPTION:
	case NG_HCI_OCF_CHANGE_CON_LINK_KEY:
	case NG_HCI_OCF_MASTER_LINK_KEY:
	case NG_HCI_OCF_REMOTE_NAME_REQ:
	case NG_HCI_OCF_READ_REMOTE_FEATURES:
	case NG_HCI_OCF_READ_REMOTE_VER_INFO:
	case NG_HCI_OCF_READ_CLOCK_OFFSET:
		/* These do not need post processing */
		break;

	case NG_HCI_OCF_CREATE_CON:
		break;

	case NG_HCI_OCF_ADD_SCO_CON:
		break;

	case NG_HCI_OCF_ACCEPT_CON:
		break;

	case NG_HCI_OCF_INQUIRY_CANCEL:
	case NG_HCI_OCF_PERIODIC_INQUIRY:
	case NG_HCI_OCF_EXIT_PERIODIC_INQUIRY:
	case NG_HCI_OCF_LINK_KEY_REP:
	case NG_HCI_OCF_LINK_KEY_NEG_REP: 
	case NG_HCI_OCF_PIN_CODE_REP:
	case NG_HCI_OCF_PIN_CODE_NEG_REP:
	default:

		/*
		 * None of these command was supposed to generate 
		 * Command_Status event. Instead Command_Complete event 
		 * should have been sent.
		 */

		error = EINVAL;
		break;
	}

	NG_FREE_M(mcp);

	return (error);
} /* process_link_control_status */

/*
 * Process link policy command status
 */

static int
process_link_policy_status(ng_hci_unit_p unit, ng_hci_command_status_ep *ep,
		struct mbuf *mcp)
{
	int	error = 0;

	switch (NG_HCI_OCF(ep->opcode)) {
	case NG_HCI_OCF_HOLD_MODE:
	case NG_HCI_OCF_SNIFF_MODE:
	case NG_HCI_OCF_EXIT_SNIFF_MODE:
	case NG_HCI_OCF_PARK_MODE:
	case NG_HCI_OCF_EXIT_PARK_MODE:
	case NG_HCI_OCF_SWITCH_ROLE:
		/* These do not need post processing */
		break;

	case NG_HCI_OCF_QOS_SETUP:
		break;

	case NG_HCI_OCF_ROLE_DISCOVERY:
	case NG_HCI_OCF_READ_LINK_POLICY_SETTINGS:
	case NG_HCI_OCF_WRITE_LINK_POLICY_SETTINGS:
	default:

		/*
		 * None of these command was supposed to generate 
		 * Command_Status event. Instead Command_Complete event 
		 * should have been sent.
		 */

		error = EINVAL;
		break;
	}

	NG_FREE_M(mcp);

	return (error);
} /* process_link_policy_status */


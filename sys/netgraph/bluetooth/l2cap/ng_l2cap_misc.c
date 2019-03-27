/*
 * ng_l2cap_misc.c
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
 * $Id: ng_l2cap_misc.c,v 1.5 2003/09/08 19:11:45 max Exp $
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
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/include/ng_l2cap.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_var.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_cmds.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_evnt.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_llpi.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_ulpi.h>
#include <netgraph/bluetooth/l2cap/ng_l2cap_misc.h>

static u_int16_t	ng_l2cap_get_cid	(ng_l2cap_p, int);

/******************************************************************************
 ******************************************************************************
 **                              Utility routines
 ******************************************************************************
 ******************************************************************************/

/*
 * Send hook information to the upper layer
 */

void
ng_l2cap_send_hook_info(node_p node, hook_p hook, void *arg1, int arg2)
{
	ng_l2cap_p	 l2cap = NULL;
	struct ng_mesg	*msg = NULL;
	int		 error = 0;
	ng_l2cap_node_hook_info_ep *ep ;

	if (node == NULL || NG_NODE_NOT_VALID(node) ||
	    hook == NULL || NG_HOOK_NOT_VALID(hook))
		return;

	l2cap = (ng_l2cap_p) NG_NODE_PRIVATE(node);
	if (l2cap->hci == NULL || NG_HOOK_NOT_VALID(l2cap->hci) ||
	    bcmp(&l2cap->bdaddr, NG_HCI_BDADDR_ANY, sizeof(l2cap->bdaddr)) == 0)
		return;

	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_NODE_HOOK_INFO,
		     sizeof(*ep), M_NOWAIT);

	if (msg != NULL) {
		ep = (ng_l2cap_node_hook_info_ep *) &msg->data;
		bcopy(&l2cap->bdaddr, &ep->addr, sizeof(bdaddr_t));
		NG_SEND_MSG_HOOK(error, node, msg, hook, 0);
	} else
		error = ENOMEM;

	if (error != 0)
		NG_L2CAP_INFO(
"%s: %s - failed to send HOOK_INFO message to hook \"%s\", error=%d\n",
			__func__, NG_NODE_NAME(l2cap->node), NG_HOOK_NAME(hook),
			error);
} /* ng_l2cap_send_hook_info */

/*
 * Create new connection descriptor for the "remote" unit. 
 * Will link connection descriptor to the l2cap node.
 */

ng_l2cap_con_p
ng_l2cap_new_con(ng_l2cap_p l2cap, bdaddr_p bdaddr, int type)
{
	static int	fake_con_handle = 0x0f00;
	ng_l2cap_con_p	con = NULL;

	/* Create new connection descriptor */
	con = malloc(sizeof(*con), M_NETGRAPH_L2CAP,
		M_NOWAIT|M_ZERO);
	if (con == NULL)
		return (NULL);

	con->l2cap = l2cap;
	con->state = NG_L2CAP_CON_CLOSED;
	con->encryption = 0;
	/*
	 * XXX
	 *
	 * Assign fake connection handle to the connection descriptor.
	 * Bluetooth specification marks 0x0f00 - 0x0fff connection 
	 * handles as reserved. We need this fake connection handles 
	 * for timeouts. Connection handle will be passed as argument
	 * to timeout so when timeout happens we can find the right
	 * connection descriptor. We can not pass pointers, because
	 * timeouts are external (to Netgraph) events and there might
	 * be a race when node/hook goes down and timeout event already
	 * went into node's queue
	 */

	con->con_handle = fake_con_handle ++;
	if (fake_con_handle > 0x0fff)
		fake_con_handle = 0x0f00;

	bcopy(bdaddr, &con->remote, sizeof(con->remote));
	con->linktype = type;
	ng_callout_init(&con->con_timo);

	con->ident = NG_L2CAP_FIRST_IDENT - 1;
	TAILQ_INIT(&con->cmd_list);

	/* Link connection */
	LIST_INSERT_HEAD(&l2cap->con_list, con, next); 

	return (con);
} /* ng_l2cap_new_con */

/*
 * Add reference to the connection descriptor
 */

void
ng_l2cap_con_ref(ng_l2cap_con_p con)
{
	con->refcnt ++;

	if (con->flags & NG_L2CAP_CON_AUTO_DISCON_TIMO) {
		if ((con->state != NG_L2CAP_CON_OPEN) ||
		    (con->flags & NG_L2CAP_CON_OUTGOING) == 0)
			panic(
"%s: %s - bad auto disconnect timeout, state=%d, flags=%#x\n",
				__func__, NG_NODE_NAME(con->l2cap->node),
				con->state, con->flags);

		ng_l2cap_discon_untimeout(con);
	}
} /* ng_l2cap_con_ref */

/*
 * Remove reference from the connection descriptor
 */

void
ng_l2cap_con_unref(ng_l2cap_con_p con)
{
	con->refcnt --;

	if (con->refcnt < 0)
		panic(
"%s: %s - con->refcnt < 0\n", __func__, NG_NODE_NAME(con->l2cap->node));

	/*
	 * Set auto disconnect timer only if the following conditions are met:
	 * 1) we have no reference on the connection
	 * 2) connection is in OPEN state
	 * 3) it is an outgoing connection
	 * 4) disconnect timeout > 0
	 * 5) connection is not dying
	 */

	if ((con->refcnt == 0) &&
	    (con->state == NG_L2CAP_CON_OPEN) &&
	    (con->flags & NG_L2CAP_CON_OUTGOING) && 
	    (con->l2cap->discon_timo > 0) &&
	    ((con->flags & NG_L2CAP_CON_DYING) == 0))
		ng_l2cap_discon_timeout(con);
} /* ng_l2cap_con_unref */

/*
 * Set auto disconnect timeout
 * XXX FIXME: check return code from ng_callout
 */

int
ng_l2cap_discon_timeout(ng_l2cap_con_p con)
{
	if (con->flags & (NG_L2CAP_CON_LP_TIMO|NG_L2CAP_CON_AUTO_DISCON_TIMO))
		panic(
"%s: %s - invalid timeout, state=%d, flags=%#x\n",
			__func__, NG_NODE_NAME(con->l2cap->node),
			con->state, con->flags);

	con->flags |= NG_L2CAP_CON_AUTO_DISCON_TIMO;
	ng_callout(&con->con_timo, con->l2cap->node, NULL,
				con->l2cap->discon_timo * hz,
				ng_l2cap_process_discon_timeout, NULL,
				con->con_handle);

	return (0);
} /* ng_l2cap_discon_timeout */

/*
 * Unset auto disconnect timeout
 */

int
ng_l2cap_discon_untimeout(ng_l2cap_con_p con)
{
	if (!(con->flags & NG_L2CAP_CON_AUTO_DISCON_TIMO))
		panic(
"%s: %s - no disconnect timeout, state=%d, flags=%#x\n",
			__func__,  NG_NODE_NAME(con->l2cap->node),
			con->state, con->flags);
	
	if (ng_uncallout(&con->con_timo, con->l2cap->node) == 0)
		return (ETIMEDOUT);

	con->flags &= ~NG_L2CAP_CON_AUTO_DISCON_TIMO;

	return (0);
} /* ng_l2cap_discon_untimeout */

/*
 * Free connection descriptor. Will unlink connection and free everything.
 */

void
ng_l2cap_free_con(ng_l2cap_con_p con)
{
	ng_l2cap_chan_p f = NULL, n = NULL;

	con->state = NG_L2CAP_CON_CLOSED;

	while (con->tx_pkt != NULL) {
		struct mbuf	*m = con->tx_pkt->m_nextpkt;

		m_freem(con->tx_pkt);
		con->tx_pkt = m;
	}

	NG_FREE_M(con->rx_pkt);

	for (f = LIST_FIRST(&con->l2cap->chan_list); f != NULL; ) {
		n = LIST_NEXT(f, next);

		if (f->con == con)
			ng_l2cap_free_chan(f);

		f = n;
	}

	while (!TAILQ_EMPTY(&con->cmd_list)) {
		ng_l2cap_cmd_p	cmd = TAILQ_FIRST(&con->cmd_list);

		ng_l2cap_unlink_cmd(cmd);
		if (cmd->flags & NG_L2CAP_CMD_PENDING)
			ng_l2cap_command_untimeout(cmd);
		ng_l2cap_free_cmd(cmd);
	}

	if (con->flags & (NG_L2CAP_CON_AUTO_DISCON_TIMO|NG_L2CAP_CON_LP_TIMO))
		panic(
"%s: %s - timeout pending! state=%d, flags=%#x\n",
			__func__,  NG_NODE_NAME(con->l2cap->node),
			con->state, con->flags);

	LIST_REMOVE(con, next);

	bzero(con, sizeof(*con));
	free(con, M_NETGRAPH_L2CAP);
} /* ng_l2cap_free_con */

/*
 * Get connection by "remote" address
 */

ng_l2cap_con_p
ng_l2cap_con_by_addr(ng_l2cap_p l2cap, bdaddr_p bdaddr, unsigned int type)
{
	ng_l2cap_con_p	con = NULL;

	LIST_FOREACH(con, &l2cap->con_list, next)
		if ((bcmp(bdaddr, &con->remote, sizeof(con->remote)) == 0)&&
		    (con->linktype == type))
			break;

	return (con);
} /* ng_l2cap_con_by_addr */

/*
 * Get connection by "handle" 
 */

ng_l2cap_con_p
ng_l2cap_con_by_handle(ng_l2cap_p l2cap, u_int16_t con_handle)
{
	ng_l2cap_con_p	con = NULL;

	LIST_FOREACH(con, &l2cap->con_list, next)
		if (con->con_handle == con_handle)
			break;

	return (con);
} /* ng_l2cap_con_by_handle */

/*
 * Allocate new L2CAP channel descriptor on "con" connection with "psm".
 * Will link the channel to the l2cap node
 */

ng_l2cap_chan_p
ng_l2cap_new_chan(ng_l2cap_p l2cap, ng_l2cap_con_p con, u_int16_t psm, int idtype)
{
	ng_l2cap_chan_p	ch = NULL;

	ch = malloc(sizeof(*ch), M_NETGRAPH_L2CAP,
		M_NOWAIT|M_ZERO);
	if (ch == NULL)
		return (NULL);
	if(idtype == NG_L2CAP_L2CA_IDTYPE_ATT){
		ch->scid = ch->dcid = NG_L2CAP_ATT_CID;
	}else if(idtype == NG_L2CAP_L2CA_IDTYPE_SMP){
		ch->scid = ch->dcid = NG_L2CAP_SMP_CID;
	}else{
		ch->scid = ng_l2cap_get_cid(l2cap,
					    (con->linktype!= NG_HCI_LINK_ACL));
	}
	
	ch->idtype = idtype;
	if (ch->scid != NG_L2CAP_NULL_CID) {
		/* Initialize channel */
		ch->psm = psm;
		ch->con = con;
		ch->state = NG_L2CAP_CLOSED;

		/* Set MTU and flow control settings to defaults */
		ch->imtu = NG_L2CAP_MTU_DEFAULT;
		bcopy(ng_l2cap_default_flow(), &ch->iflow, sizeof(ch->iflow));

		ch->omtu = NG_L2CAP_MTU_DEFAULT;
		bcopy(ng_l2cap_default_flow(), &ch->oflow, sizeof(ch->oflow));

		ch->flush_timo = NG_L2CAP_FLUSH_TIMO_DEFAULT;
		ch->link_timo = NG_L2CAP_LINK_TIMO_DEFAULT;

		LIST_INSERT_HEAD(&l2cap->chan_list, ch, next);

		ng_l2cap_con_ref(con);
	} else {
		bzero(ch, sizeof(*ch));
		free(ch, M_NETGRAPH_L2CAP);
		ch = NULL;
	}

	return (ch);
} /* ng_l2cap_new_chan */


ng_l2cap_chan_p
ng_l2cap_chan_by_scid(ng_l2cap_p l2cap, u_int16_t scid, int idtype)
{
	ng_l2cap_chan_p	ch = NULL;

	if((idtype == NG_L2CAP_L2CA_IDTYPE_ATT)||
	   (idtype == NG_L2CAP_L2CA_IDTYPE_SMP)){
		return NULL;
	}
	
	LIST_FOREACH(ch, &l2cap->chan_list, next){
		if((idtype != NG_L2CAP_L2CA_IDTYPE_BREDR)&&
		   (ch->con->linktype == NG_HCI_LINK_ACL ))
			continue;
		if((idtype != NG_L2CAP_L2CA_IDTYPE_LE)&&
		   (ch->con->linktype != NG_HCI_LINK_ACL ))
			continue;
		if (ch->scid == scid)
			break;
	}
	return (ch);
} /* ng_l2cap_chan_by_scid */

ng_l2cap_chan_p
ng_l2cap_chan_by_conhandle(ng_l2cap_p l2cap, uint16_t scid,
			   u_int16_t con_handle)
{
	ng_l2cap_chan_p	ch = NULL;

	
	LIST_FOREACH(ch, &l2cap->chan_list, next){
		if ((ch->scid == scid) &&
		    (ch->con->con_handle == con_handle))
			break;
	}
	return (ch);
} /* ng_l2cap_chan_by_scid */

/*
 * Free channel descriptor.
 */

void
ng_l2cap_free_chan(ng_l2cap_chan_p ch)
{
	ng_l2cap_cmd_p	f = NULL, n = NULL;

	f = TAILQ_FIRST(&ch->con->cmd_list);

	while (f != NULL) {
		n = TAILQ_NEXT(f, next);

		if (f->ch == ch) {
			ng_l2cap_unlink_cmd(f);
			if (f->flags & NG_L2CAP_CMD_PENDING)
				ng_l2cap_command_untimeout(f);
			ng_l2cap_free_cmd(f);
		}

		f = n;
	}

	LIST_REMOVE(ch, next);

	ng_l2cap_con_unref(ch->con);

	bzero(ch, sizeof(*ch));
	free(ch, M_NETGRAPH_L2CAP);
} /* ng_l2cap_free_chan */

/*
 * Create new L2CAP command descriptor. WILL NOT add command to the queue.
 */

ng_l2cap_cmd_p
ng_l2cap_new_cmd(ng_l2cap_con_p con, ng_l2cap_chan_p ch, u_int8_t ident,
		u_int8_t code, u_int32_t token)
{
	ng_l2cap_cmd_p	cmd = NULL;

	KASSERT((ch == NULL || ch->con == con),
("%s: %s - invalid channel pointer!\n",
		__func__, NG_NODE_NAME(con->l2cap->node)));

	cmd = malloc(sizeof(*cmd), M_NETGRAPH_L2CAP,
		M_NOWAIT|M_ZERO);
	if (cmd == NULL)
		return (NULL);

	cmd->con = con;
	cmd->ch = ch;
	cmd->ident = ident;
	cmd->code = code;
	cmd->token = token;
	ng_callout_init(&cmd->timo);

	return (cmd);
} /* ng_l2cap_new_cmd */
 
/*
 * Get pending (i.e. initiated by local side) L2CAP command descriptor by ident
 */

ng_l2cap_cmd_p
ng_l2cap_cmd_by_ident(ng_l2cap_con_p con, u_int8_t ident)
{
	ng_l2cap_cmd_p	cmd = NULL;

	TAILQ_FOREACH(cmd, &con->cmd_list, next) {
		if ((cmd->flags & NG_L2CAP_CMD_PENDING) && cmd->ident == ident) {
			KASSERT((cmd->con == con),
("%s: %s - invalid connection pointer!\n",
				__func__, NG_NODE_NAME(con->l2cap->node)));

			break;
		}
	}

	return (cmd);
} /* ng_l2cap_cmd_by_ident */

/*
 * Set LP timeout
 * XXX FIXME: check return code from ng_callout
 */

int
ng_l2cap_lp_timeout(ng_l2cap_con_p con)
{
	if (con->flags & (NG_L2CAP_CON_LP_TIMO|NG_L2CAP_CON_AUTO_DISCON_TIMO))
		panic(
"%s: %s - invalid timeout, state=%d, flags=%#x\n",
			__func__, NG_NODE_NAME(con->l2cap->node),
			con->state, con->flags);

	con->flags |= NG_L2CAP_CON_LP_TIMO;
	ng_callout(&con->con_timo, con->l2cap->node, NULL,
				bluetooth_hci_connect_timeout(),
				ng_l2cap_process_lp_timeout, NULL,
				con->con_handle);

	return (0);
} /* ng_l2cap_lp_timeout */

/*
 * Unset LP timeout
 */

int
ng_l2cap_lp_untimeout(ng_l2cap_con_p con)
{
	if (!(con->flags & NG_L2CAP_CON_LP_TIMO))
		panic(
"%s: %s - no LP connection timeout, state=%d, flags=%#x\n",
			__func__,  NG_NODE_NAME(con->l2cap->node),
			con->state, con->flags);
	
	if (ng_uncallout(&con->con_timo, con->l2cap->node) == 0)
		return (ETIMEDOUT);

	con->flags &= ~NG_L2CAP_CON_LP_TIMO;

	return (0);
} /* ng_l2cap_lp_untimeout */

/*
 * Set L2CAP command timeout
 * XXX FIXME: check return code from ng_callout
 */

int
ng_l2cap_command_timeout(ng_l2cap_cmd_p cmd, int timo)
{
	int	arg;

	if (cmd->flags & NG_L2CAP_CMD_PENDING)
		panic(
"%s: %s - duplicated command timeout, code=%#x, flags=%#x\n",
			__func__, NG_NODE_NAME(cmd->con->l2cap->node),
			cmd->code, cmd->flags);

	arg = ((cmd->ident << 16) | cmd->con->con_handle);
	cmd->flags |= NG_L2CAP_CMD_PENDING;
	ng_callout(&cmd->timo, cmd->con->l2cap->node, NULL, timo,
				ng_l2cap_process_command_timeout, NULL, arg);

	return (0);
} /* ng_l2cap_command_timeout */

/*
 * Unset L2CAP command timeout
 */

int
ng_l2cap_command_untimeout(ng_l2cap_cmd_p cmd)
{
	if (!(cmd->flags & NG_L2CAP_CMD_PENDING))
		panic(
"%s: %s - no command timeout, code=%#x, flags=%#x\n",
			__func__, NG_NODE_NAME(cmd->con->l2cap->node),
			cmd->code, cmd->flags);

	if (ng_uncallout(&cmd->timo, cmd->con->l2cap->node) == 0)
		return (ETIMEDOUT);

	cmd->flags &= ~NG_L2CAP_CMD_PENDING;

	return (0);
} /* ng_l2cap_command_untimeout */

/*
 * Prepend "m"buf with "size" bytes
 */

struct mbuf *
ng_l2cap_prepend(struct mbuf *m, int size)
{
	M_PREPEND(m, size, M_NOWAIT);
	if (m == NULL || (m->m_len < size && (m = m_pullup(m, size)) == NULL))
		return (NULL);

	return (m);
} /* ng_l2cap_prepend */

/*
 * Default flow settings
 */

ng_l2cap_flow_p
ng_l2cap_default_flow(void)
{
	static ng_l2cap_flow_t	default_flow = {
		/* flags */		0x0,
		/* service_type */	NG_HCI_SERVICE_TYPE_BEST_EFFORT,
		/* token_rate */	0xffffffff, /* maximum */
		/* token_bucket_size */	0xffffffff, /* maximum */
		/* peak_bandwidth */	0x00000000, /* maximum */
		/* latency */		0xffffffff, /* don't care */
		/* delay_variation */	0xffffffff  /* don't care */
	};

	return (&default_flow);
} /* ng_l2cap_default_flow */

/*
 * Get next available channel ID
 * XXX FIXME this is *UGLY* but will do for now
 */

static u_int16_t
ng_l2cap_get_cid(ng_l2cap_p l2cap,int isle)
{
	u_int16_t	cid ;
	u_int16_t 	endcid;
	uint16_t	 mask;
	int idtype;
	if(isle){
		endcid = l2cap->lecid;
		/*Assume Last CID is 2^n-1 */
		mask = NG_L2CAP_LELAST_CID;
		idtype = NG_L2CAP_L2CA_IDTYPE_LE;
	}else{
		endcid = l2cap->cid;
		/*Assume Last CID is 2^n-1 */		
		mask = NG_L2CAP_LAST_CID;
		idtype = NG_L2CAP_L2CA_IDTYPE_BREDR;
	}
	cid = (endcid+1) & mask;
	     
	if (cid < NG_L2CAP_FIRST_CID)
		cid = NG_L2CAP_FIRST_CID;

	while (cid != endcid) {
		if (ng_l2cap_chan_by_scid(l2cap, cid, idtype) == NULL) {
			if(!isle){
				l2cap->cid = cid;
			}else{
				l2cap->lecid = cid;
			}
			return (cid);
		}

		cid ++;
		cid &= mask;
		if (cid < NG_L2CAP_FIRST_CID)
			cid = NG_L2CAP_FIRST_CID;
	}
		
	return (NG_L2CAP_NULL_CID);
} /* ng_l2cap_get_cid */

/*
 * Get next available command ident
 * XXX FIXME this is *UGLY* but will do for now
 */

u_int8_t
ng_l2cap_get_ident(ng_l2cap_con_p con)
{
	u_int8_t	ident = con->ident + 1;

	if (ident < NG_L2CAP_FIRST_IDENT)
		ident = NG_L2CAP_FIRST_IDENT;

	while (ident != con->ident) {
		if (ng_l2cap_cmd_by_ident(con, ident) == NULL) {
			con->ident = ident;

			return (ident);
		}

		ident ++;
		if (ident < NG_L2CAP_FIRST_IDENT)
			ident = NG_L2CAP_FIRST_IDENT;
	}

	return (NG_L2CAP_NULL_IDENT);
} /* ng_l2cap_get_ident */


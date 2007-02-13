/*
 * llc_conn.c - Driver routines for connection component.
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 *		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */

#include <linux/init.h>
#include <net/llc_sap.h>
#include <net/llc_conn.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/llc_c_ev.h>
#include <net/llc_c_ac.h>
#include <net/llc_c_st.h>
#include <net/llc_pdu.h>

#if 0
#define dprintk(args...) printk(KERN_DEBUG args)
#else
#define dprintk(args...)
#endif

static int llc_find_offset(int state, int ev_type);
static void llc_conn_send_pdus(struct sock *sk);
static int llc_conn_service(struct sock *sk, struct sk_buff *skb);
static int llc_exec_conn_trans_actions(struct sock *sk,
				       struct llc_conn_state_trans *trans,
				       struct sk_buff *ev);
static struct llc_conn_state_trans *llc_qualify_conn_ev(struct sock *sk,
							struct sk_buff *skb);

/* Offset table on connection states transition diagram */
static int llc_offset_table[NBR_CONN_STATES][NBR_CONN_EV];

int sysctl_llc2_ack_timeout = LLC2_ACK_TIME * HZ;
int sysctl_llc2_p_timeout = LLC2_P_TIME * HZ;
int sysctl_llc2_rej_timeout = LLC2_REJ_TIME * HZ;
int sysctl_llc2_busy_timeout = LLC2_BUSY_TIME * HZ;

/**
 *	llc_conn_state_process - sends event to connection state machine
 *	@sk: connection
 *	@skb: occurred event
 *
 *	Sends an event to connection state machine. After processing event
 *	(executing it's actions and changing state), upper layer will be
 *	indicated or confirmed, if needed. Returns 0 for success, 1 for
 *	failure. The socket lock has to be held before calling this function.
 */
int llc_conn_state_process(struct sock *sk, struct sk_buff *skb)
{
	int rc;
	struct llc_sock *llc = llc_sk(skb->sk);
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	/*
	 * We have to hold the skb, because llc_conn_service will kfree it in
	 * the sending path and we need to look at the skb->cb, where we encode
	 * llc_conn_state_ev.
	 */
	skb_get(skb);
	ev->ind_prim = ev->cfm_prim = 0;
	/*
	 * Send event to state machine
	 */
	rc = llc_conn_service(skb->sk, skb);
	if (unlikely(rc != 0)) {
		printk(KERN_ERR "%s: llc_conn_service failed\n", __FUNCTION__);
		goto out_kfree_skb;
	}

	if (unlikely(!ev->ind_prim && !ev->cfm_prim)) {
		/* indicate or confirm not required */
		/* XXX this is not very pretty, perhaps we should store
		 * XXX indicate/confirm-needed state in the llc_conn_state_ev
		 * XXX control block of the SKB instead? -DaveM
		 */
		if (!skb->next)
			goto out_kfree_skb;
		goto out_skb_put;
	}

	if (unlikely(ev->ind_prim && ev->cfm_prim)) /* Paranoia */
		skb_get(skb);

	switch (ev->ind_prim) {
	case LLC_DATA_PRIM:
		llc_save_primitive(sk, skb, LLC_DATA_PRIM);
		if (unlikely(sock_queue_rcv_skb(sk, skb))) {
			/*
			 * shouldn't happen
			 */
			printk(KERN_ERR "%s: sock_queue_rcv_skb failed!\n",
			       __FUNCTION__);
			kfree_skb(skb);
		}
		break;
	case LLC_CONN_PRIM:
		/*
		 * Can't be sock_queue_rcv_skb, because we have to leave the
		 * skb->sk pointing to the newly created struct sock in
		 * llc_conn_handler. -acme
		 */
		skb_queue_tail(&sk->sk_receive_queue, skb);
		sk->sk_state_change(sk);
		break;
	case LLC_DISC_PRIM:
		sock_hold(sk);
		if (sk->sk_type == SOCK_STREAM &&
		    sk->sk_state == TCP_ESTABLISHED) {
			sk->sk_shutdown       = SHUTDOWN_MASK;
			sk->sk_socket->state  = SS_UNCONNECTED;
			sk->sk_state          = TCP_CLOSE;
			if (!sock_flag(sk, SOCK_DEAD)) {
				sock_set_flag(sk, SOCK_DEAD);
				sk->sk_state_change(sk);
			}
		}
		kfree_skb(skb);
		sock_put(sk);
		break;
	case LLC_RESET_PRIM:
		/*
		 * FIXME:
		 * RESET is not being notified to upper layers for now
		 */
		printk(KERN_INFO "%s: received a reset ind!\n", __FUNCTION__);
		kfree_skb(skb);
		break;
	default:
		if (ev->ind_prim) {
			printk(KERN_INFO "%s: received unknown %d prim!\n",
				__FUNCTION__, ev->ind_prim);
			kfree_skb(skb);
		}
		/* No indication */
		break;
	}

	switch (ev->cfm_prim) {
	case LLC_DATA_PRIM:
		if (!llc_data_accept_state(llc->state))
			sk->sk_write_space(sk);
		else
			rc = llc->failed_data_req = 1;
		break;
	case LLC_CONN_PRIM:
		if (sk->sk_type == SOCK_STREAM &&
		    sk->sk_state == TCP_SYN_SENT) {
			if (ev->status) {
				sk->sk_socket->state = SS_UNCONNECTED;
				sk->sk_state         = TCP_CLOSE;
			} else {
				sk->sk_socket->state = SS_CONNECTED;
				sk->sk_state         = TCP_ESTABLISHED;
			}
			sk->sk_state_change(sk);
		}
		break;
	case LLC_DISC_PRIM:
		sock_hold(sk);
		if (sk->sk_type == SOCK_STREAM && sk->sk_state == TCP_CLOSING) {
			sk->sk_socket->state = SS_UNCONNECTED;
			sk->sk_state         = TCP_CLOSE;
			sk->sk_state_change(sk);
		}
		sock_put(sk);
		break;
	case LLC_RESET_PRIM:
		/*
		 * FIXME:
		 * RESET is not being notified to upper layers for now
		 */
		printk(KERN_INFO "%s: received a reset conf!\n", __FUNCTION__);
		break;
	default:
		if (ev->cfm_prim) {
			printk(KERN_INFO "%s: received unknown %d prim!\n",
					__FUNCTION__, ev->cfm_prim);
			break;
		}
		goto out_skb_put; /* No confirmation */
	}
out_kfree_skb:
	kfree_skb(skb);
out_skb_put:
	kfree_skb(skb);
	return rc;
}

void llc_conn_send_pdu(struct sock *sk, struct sk_buff *skb)
{
	/* queue PDU to send to MAC layer */
	skb_queue_tail(&sk->sk_write_queue, skb);
	llc_conn_send_pdus(sk);
}

/**
 *	llc_conn_rtn_pdu - sends received data pdu to upper layer
 *	@sk: Active connection
 *	@skb: Received data frame
 *
 *	Sends received data pdu to upper layer (by using indicate function).
 *	Prepares service parameters (prim and prim_data). calling indication
 *	function will be done in llc_conn_state_process.
 */
void llc_conn_rtn_pdu(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->ind_prim = LLC_DATA_PRIM;
}

/**
 *	llc_conn_resend_i_pdu_as_cmd - resend all all unacknowledged I PDUs
 *	@sk: active connection
 *	@nr: NR
 *	@first_p_bit: p_bit value of first pdu
 *
 *	Resend all unacknowledged I PDUs, starting with the NR; send first as
 *	command PDU with P bit equal first_p_bit; if more than one send
 *	subsequent as command PDUs with P bit equal zero (0).
 */
void llc_conn_resend_i_pdu_as_cmd(struct sock *sk, u8 nr, u8 first_p_bit)
{
	struct sk_buff *skb;
	struct llc_pdu_sn *pdu;
	u16 nbr_unack_pdus;
	struct llc_sock *llc;
	u8 howmany_resend = 0;

	llc_conn_remove_acked_pdus(sk, nr, &nbr_unack_pdus);
	if (!nbr_unack_pdus)
		goto out;
	/*
	 * Process unack PDUs only if unack queue is not empty; remove
	 * appropriate PDUs, fix them up, and put them on mac_pdu_q.
	 */
	llc = llc_sk(sk);

	while ((skb = skb_dequeue(&llc->pdu_unack_q)) != NULL) {
		pdu = llc_pdu_sn_hdr(skb);
		llc_pdu_set_cmd_rsp(skb, LLC_PDU_CMD);
		llc_pdu_set_pf_bit(skb, first_p_bit);
		skb_queue_tail(&sk->sk_write_queue, skb);
		first_p_bit = 0;
		llc->vS = LLC_I_GET_NS(pdu);
		howmany_resend++;
	}
	if (howmany_resend > 0)
		llc->vS = (llc->vS + 1) % LLC_2_SEQ_NBR_MODULO;
	/* any PDUs to re-send are queued up; start sending to MAC */
	llc_conn_send_pdus(sk);
out:;
}

/**
 *	llc_conn_resend_i_pdu_as_rsp - Resend all unacknowledged I PDUs
 *	@sk: active connection.
 *	@nr: NR
 *	@first_f_bit: f_bit value of first pdu.
 *
 *	Resend all unacknowledged I PDUs, starting with the NR; send first as
 *	response PDU with F bit equal first_f_bit; if more than one send
 *	subsequent as response PDUs with F bit equal zero (0).
 */
void llc_conn_resend_i_pdu_as_rsp(struct sock *sk, u8 nr, u8 first_f_bit)
{
	struct sk_buff *skb;
	u16 nbr_unack_pdus;
	struct llc_sock *llc = llc_sk(sk);
	u8 howmany_resend = 0;

	llc_conn_remove_acked_pdus(sk, nr, &nbr_unack_pdus);
	if (!nbr_unack_pdus)
		goto out;
	/*
	 * Process unack PDUs only if unack queue is not empty; remove
	 * appropriate PDUs, fix them up, and put them on mac_pdu_q
	 */
	while ((skb = skb_dequeue(&llc->pdu_unack_q)) != NULL) {
		struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

		llc_pdu_set_cmd_rsp(skb, LLC_PDU_RSP);
		llc_pdu_set_pf_bit(skb, first_f_bit);
		skb_queue_tail(&sk->sk_write_queue, skb);
		first_f_bit = 0;
		llc->vS = LLC_I_GET_NS(pdu);
		howmany_resend++;
	}
	if (howmany_resend > 0)
		llc->vS = (llc->vS + 1) % LLC_2_SEQ_NBR_MODULO;
	/* any PDUs to re-send are queued up; start sending to MAC */
	llc_conn_send_pdus(sk);
out:;
}

/**
 *	llc_conn_remove_acked_pdus - Removes acknowledged pdus from tx queue
 *	@sk: active connection
 *	nr: NR
 *	how_many_unacked: size of pdu_unack_q after removing acked pdus
 *
 *	Removes acknowledged pdus from transmit queue (pdu_unack_q). Returns
 *	the number of pdus that removed from queue.
 */
int llc_conn_remove_acked_pdus(struct sock *sk, u8 nr, u16 *how_many_unacked)
{
	int pdu_pos, i;
	struct sk_buff *skb;
	struct llc_pdu_sn *pdu;
	int nbr_acked = 0;
	struct llc_sock *llc = llc_sk(sk);
	int q_len = skb_queue_len(&llc->pdu_unack_q);

	if (!q_len)
		goto out;
	skb = skb_peek(&llc->pdu_unack_q);
	pdu = llc_pdu_sn_hdr(skb);

	/* finding position of last acked pdu in queue */
	pdu_pos = ((int)LLC_2_SEQ_NBR_MODULO + (int)nr -
			(int)LLC_I_GET_NS(pdu)) % LLC_2_SEQ_NBR_MODULO;

	for (i = 0; i < pdu_pos && i < q_len; i++) {
		skb = skb_dequeue(&llc->pdu_unack_q);
		if (skb)
			kfree_skb(skb);
		nbr_acked++;
	}
out:
	*how_many_unacked = skb_queue_len(&llc->pdu_unack_q);
	return nbr_acked;
}

/**
 *	llc_conn_send_pdus - Sends queued PDUs
 *	@sk: active connection
 *
 *	Sends queued pdus to MAC layer for transmission.
 */
static void llc_conn_send_pdus(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&sk->sk_write_queue)) != NULL) {
		struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

		if (LLC_PDU_TYPE_IS_I(pdu) &&
		    !(skb->dev->flags & IFF_LOOPBACK)) {
			struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);

			skb_queue_tail(&llc_sk(sk)->pdu_unack_q, skb);
			if (!skb2)
				break;
			skb = skb2;
		}
		dev_queue_xmit(skb);
	}
}

/**
 *	llc_conn_service - finds transition and changes state of connection
 *	@sk: connection
 *	@skb: happened event
 *
 *	This function finds transition that matches with happened event, then
 *	executes related actions and finally changes state of connection.
 *	Returns 0 for success, 1 for failure.
 */
static int llc_conn_service(struct sock *sk, struct sk_buff *skb)
{
	int rc = 1;
	struct llc_sock *llc = llc_sk(sk);
	struct llc_conn_state_trans *trans;

	if (llc->state > NBR_CONN_STATES)
		goto out;
	rc = 0;
	trans = llc_qualify_conn_ev(sk, skb);
	if (trans) {
		rc = llc_exec_conn_trans_actions(sk, trans, skb);
		if (!rc && trans->next_state != NO_STATE_CHANGE) {
			llc->state = trans->next_state;
			if (!llc_data_accept_state(llc->state))
				sk->sk_state_change(sk);
		}
	}
out:
	return rc;
}

/**
 *	llc_qualify_conn_ev - finds transition for event
 *	@sk: connection
 *	@skb: happened event
 *
 *	This function finds transition that matches with happened event.
 *	Returns pointer to found transition on success, %NULL otherwise.
 */
static struct llc_conn_state_trans *llc_qualify_conn_ev(struct sock *sk,
							struct sk_buff *skb)
{
	struct llc_conn_state_trans **next_trans;
	llc_conn_ev_qfyr_t *next_qualifier;
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);
	struct llc_sock *llc = llc_sk(sk);
	struct llc_conn_state *curr_state =
					&llc_conn_state_table[llc->state - 1];

	/* search thru events for this state until
	 * list exhausted or until no more
	 */
	for (next_trans = curr_state->transitions +
		llc_find_offset(llc->state - 1, ev->type);
	     (*next_trans)->ev; next_trans++) {
		if (!((*next_trans)->ev)(sk, skb)) {
			/* got POSSIBLE event match; the event may require
			 * qualification based on the values of a number of
			 * state flags; if all qualifications are met (i.e.,
			 * if all qualifying functions return success, or 0,
			 * then this is THE event we're looking for
			 */
			for (next_qualifier = (*next_trans)->ev_qualifiers;
			     next_qualifier && *next_qualifier &&
			     !(*next_qualifier)(sk, skb); next_qualifier++)
				/* nothing */;
			if (!next_qualifier || !*next_qualifier)
				/* all qualifiers executed successfully; this is
				 * our transition; return it so we can perform
				 * the associated actions & change the state
				 */
				return *next_trans;
		}
	}
	return NULL;
}

/**
 *	llc_exec_conn_trans_actions - executes related actions
 *	@sk: connection
 *	@trans: transition that it's actions must be performed
 *	@skb: event
 *
 *	Executes actions that is related to happened event. Returns 0 for
 *	success, 1 to indicate failure of at least one action.
 */
static int llc_exec_conn_trans_actions(struct sock *sk,
				       struct llc_conn_state_trans *trans,
				       struct sk_buff *skb)
{
	int rc = 0;
	llc_conn_action_t *next_action;

	for (next_action = trans->ev_actions;
	     next_action && *next_action; next_action++) {
		int rc2 = (*next_action)(sk, skb);

		if (rc2 == 2) {
			rc = rc2;
			break;
		} else if (rc2)
			rc = 1;
	}
	return rc;
}

/**
 *	__llc_lookup_established - Finds connection for the remote/local sap/mac
 *	@sap: SAP
 *	@daddr: address of remote LLC (MAC + SAP)
 *	@laddr: address of local LLC (MAC + SAP)
 *
 *	Search connection list of the SAP and finds connection using the remote
 *	mac, remote sap, local mac, and local sap. Returns pointer for
 *	connection found, %NULL otherwise.
 *	Caller has to make sure local_bh is disabled.
 */
static struct sock *__llc_lookup_established(struct llc_sap *sap,
					     struct llc_addr *daddr,
					     struct llc_addr *laddr)
{
	struct sock *rc;
	struct hlist_node *node;

	read_lock(&sap->sk_list.lock);
	sk_for_each(rc, node, &sap->sk_list.list) {
		struct llc_sock *llc = llc_sk(rc);

		if (llc->laddr.lsap == laddr->lsap &&
		    llc->daddr.lsap == daddr->lsap &&
		    llc_mac_match(llc->laddr.mac, laddr->mac) &&
		    llc_mac_match(llc->daddr.mac, daddr->mac)) {
			sock_hold(rc);
			goto found;
		}
	}
	rc = NULL;
found:
	read_unlock(&sap->sk_list.lock);
	return rc;
}

struct sock *llc_lookup_established(struct llc_sap *sap,
				    struct llc_addr *daddr,
				    struct llc_addr *laddr)
{
	struct sock *sk;

	local_bh_disable();
	sk = __llc_lookup_established(sap, daddr, laddr);
	local_bh_enable();
	return sk;
}

/**
 *	llc_lookup_listener - Finds listener for local MAC + SAP
 *	@sap: SAP
 *	@laddr: address of local LLC (MAC + SAP)
 *
 *	Search connection list of the SAP and finds connection listening on
 *	local mac, and local sap. Returns pointer for parent socket found,
 *	%NULL otherwise.
 *	Caller has to make sure local_bh is disabled.
 */
static struct sock *llc_lookup_listener(struct llc_sap *sap,
					struct llc_addr *laddr)
{
	struct sock *rc;
	struct hlist_node *node;

	read_lock(&sap->sk_list.lock);
	sk_for_each(rc, node, &sap->sk_list.list) {
		struct llc_sock *llc = llc_sk(rc);

		if (rc->sk_type == SOCK_STREAM && rc->sk_state == TCP_LISTEN &&
		    llc->laddr.lsap == laddr->lsap &&
		    (llc_mac_match(llc->laddr.mac, laddr->mac) ||
		     llc_mac_null(llc->laddr.mac))) {
			sock_hold(rc);
			goto found;
		}
	}
	rc = NULL;
found:
	read_unlock(&sap->sk_list.lock);
	return rc;
}

static struct sock *__llc_lookup(struct llc_sap *sap,
				 struct llc_addr *daddr,
				 struct llc_addr *laddr)
{
	struct sock *sk = __llc_lookup_established(sap, daddr, laddr);

	return sk ? : llc_lookup_listener(sap, laddr);
}

/**
 *	llc_data_accept_state - designates if in this state data can be sent.
 *	@state: state of connection.
 *
 *	Returns 0 if data can be sent, 1 otherwise.
 */
u8 llc_data_accept_state(u8 state)
{
	return state != LLC_CONN_STATE_NORMAL && state != LLC_CONN_STATE_BUSY &&
	       state != LLC_CONN_STATE_REJ;
}

/**
 *	llc_find_next_offset - finds offset for next category of transitions
 *	@state: state table.
 *	@offset: start offset.
 *
 *	Finds offset of next category of transitions in transition table.
 *	Returns the start index of next category.
 */
static u16 __init llc_find_next_offset(struct llc_conn_state *state, u16 offset)
{
	u16 cnt = 0;
	struct llc_conn_state_trans **next_trans;

	for (next_trans = state->transitions + offset;
	     (*next_trans)->ev; next_trans++)
		++cnt;
	return cnt;
}

/**
 *	llc_build_offset_table - builds offset table of connection
 *
 *	Fills offset table of connection state transition table
 *	(llc_offset_table).
 */
void __init llc_build_offset_table(void)
{
	struct llc_conn_state *curr_state;
	int state, ev_type, next_offset;

	for (state = 0; state < NBR_CONN_STATES; state++) {
		curr_state = &llc_conn_state_table[state];
		next_offset = 0;
		for (ev_type = 0; ev_type < NBR_CONN_EV; ev_type++) {
			llc_offset_table[state][ev_type] = next_offset;
			next_offset += llc_find_next_offset(curr_state,
							    next_offset) + 1;
		}
	}
}

/**
 *	llc_find_offset - finds start offset of category of transitions
 *	@state: state of connection
 *	@ev_type: type of happened event
 *
 *	Finds start offset of desired category of transitions. Returns the
 *	desired start offset.
 */
static int llc_find_offset(int state, int ev_type)
{
	int rc = 0;
	/* at this stage, llc_offset_table[..][2] is not important. it is for
	 * init_pf_cycle and I don't know what is it.
	 */
	switch (ev_type) {
	case LLC_CONN_EV_TYPE_PRIM:
		rc = llc_offset_table[state][0]; break;
	case LLC_CONN_EV_TYPE_PDU:
		rc = llc_offset_table[state][4]; break;
	case LLC_CONN_EV_TYPE_SIMPLE:
		rc = llc_offset_table[state][1]; break;
	case LLC_CONN_EV_TYPE_P_TMR:
	case LLC_CONN_EV_TYPE_ACK_TMR:
	case LLC_CONN_EV_TYPE_REJ_TMR:
	case LLC_CONN_EV_TYPE_BUSY_TMR:
		rc = llc_offset_table[state][3]; break;
	}
	return rc;
}

/**
 *	llc_sap_add_socket - adds a socket to a SAP
 *	@sap: SAP
 *	@sk: socket
 *
 *	This function adds a socket to sk_list of a SAP.
 */
void llc_sap_add_socket(struct llc_sap *sap, struct sock *sk)
{
	llc_sap_hold(sap);
	write_lock_bh(&sap->sk_list.lock);
	llc_sk(sk)->sap = sap;
	sk_add_node(sk, &sap->sk_list.list);
	write_unlock_bh(&sap->sk_list.lock);
}

/**
 *	llc_sap_remove_socket - removes a socket from SAP
 *	@sap: SAP
 *	@sk: socket
 *
 *	This function removes a connection from sk_list.list of a SAP if
 *	the connection was in this list.
 */
void llc_sap_remove_socket(struct llc_sap *sap, struct sock *sk)
{
	write_lock_bh(&sap->sk_list.lock);
	sk_del_node_init(sk);
	write_unlock_bh(&sap->sk_list.lock);
	llc_sap_put(sap);
}

/**
 *	llc_conn_rcv - sends received pdus to the connection state machine
 *	@sk: current connection structure.
 *	@skb: received frame.
 *
 *	Sends received pdus to the connection state machine.
 */
static int llc_conn_rcv(struct sock* sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->type   = LLC_CONN_EV_TYPE_PDU;
	ev->reason = 0;
	return llc_conn_state_process(sk, skb);
}

static struct sock *llc_create_incoming_sock(struct sock *sk,
					     struct net_device *dev,
					     struct llc_addr *saddr,
					     struct llc_addr *daddr)
{
	struct sock *newsk = llc_sk_alloc(sk->sk_family, GFP_ATOMIC,
					  sk->sk_prot);
	struct llc_sock *newllc, *llc = llc_sk(sk);

	if (!newsk)
		goto out;
	newllc = llc_sk(newsk);
	memcpy(&newllc->laddr, daddr, sizeof(newllc->laddr));
	memcpy(&newllc->daddr, saddr, sizeof(newllc->daddr));
	newllc->dev = dev;
	dev_hold(dev);
	llc_sap_add_socket(llc->sap, newsk);
	llc_sap_hold(llc->sap);
out:
	return newsk;
}

void llc_conn_handler(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_addr saddr, daddr;
	struct sock *sk;

	llc_pdu_decode_sa(skb, saddr.mac);
	llc_pdu_decode_ssap(skb, &saddr.lsap);
	llc_pdu_decode_da(skb, daddr.mac);
	llc_pdu_decode_dsap(skb, &daddr.lsap);

	sk = __llc_lookup(sap, &saddr, &daddr);
	if (!sk)
		goto drop;

	bh_lock_sock(sk);
	/*
	 * This has to be done here and not at the upper layer ->accept
	 * method because of the way the PROCOM state machine works:
	 * it needs to set several state variables (see, for instance,
	 * llc_adm_actions_2 in net/llc/llc_c_st.c) and send a packet to
	 * the originator of the new connection, and this state has to be
	 * in the newly created struct sock private area. -acme
	 */
	if (unlikely(sk->sk_state == TCP_LISTEN)) {
		struct sock *newsk = llc_create_incoming_sock(sk, skb->dev,
							      &saddr, &daddr);
		if (!newsk)
			goto drop_unlock;
		skb_set_owner_r(skb, newsk);
	} else {
		/*
		 * Can't be skb_set_owner_r, this will be done at the
		 * llc_conn_state_process function, later on, when we will use
		 * skb_queue_rcv_skb to send it to upper layers, this is
		 * another trick required to cope with how the PROCOM state
		 * machine works. -acme
		 */
		skb->sk = sk;
	}
	if (!sock_owned_by_user(sk))
		llc_conn_rcv(sk, skb);
	else {
		dprintk("%s: adding to backlog...\n", __FUNCTION__);
		llc_set_backlog_type(skb, LLC_PACKET);
		sk_add_backlog(sk, skb);
	}
out:
	bh_unlock_sock(sk);
	sock_put(sk);
	return;
drop:
	kfree_skb(skb);
	return;
drop_unlock:
	kfree_skb(skb);
	goto out;
}

#undef LLC_REFCNT_DEBUG
#ifdef LLC_REFCNT_DEBUG
static atomic_t llc_sock_nr;
#endif

/**
 *	llc_backlog_rcv - Processes rx frames and expired timers.
 *	@sk: LLC sock (p8022 connection)
 *	@skb: queued rx frame or event
 *
 *	This function processes frames that has received and timers that has
 *	expired during sending an I pdu (refer to data_req_handler).  frames
 *	queue by llc_rcv function (llc_mac.c) and timers queue by timer
 *	callback functions(llc_c_ac.c).
 */
static int llc_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	int rc = 0;
	struct llc_sock *llc = llc_sk(sk);

	if (likely(llc_backlog_type(skb) == LLC_PACKET)) {
		if (likely(llc->state > 1)) /* not closed */
			rc = llc_conn_rcv(sk, skb);
		else
			goto out_kfree_skb;
	} else if (llc_backlog_type(skb) == LLC_EVENT) {
		/* timer expiration event */
		if (likely(llc->state > 1))  /* not closed */
			rc = llc_conn_state_process(sk, skb);
		else
			goto out_kfree_skb;
	} else {
		printk(KERN_ERR "%s: invalid skb in backlog\n", __FUNCTION__);
		goto out_kfree_skb;
	}
out:
	return rc;
out_kfree_skb:
	kfree_skb(skb);
	goto out;
}

/**
 *     llc_sk_init - Initializes a socket with default llc values.
 *     @sk: socket to initialize.
 *
 *     Initializes a socket with default llc values.
 */
static void llc_sk_init(struct sock* sk)
{
	struct llc_sock *llc = llc_sk(sk);

	llc->state    = LLC_CONN_STATE_ADM;
	llc->inc_cntr = llc->dec_cntr = 2;
	llc->dec_step = llc->connect_step = 1;

	init_timer(&llc->ack_timer.timer);
	llc->ack_timer.expire	      = sysctl_llc2_ack_timeout;
	llc->ack_timer.timer.data     = (unsigned long)sk;
	llc->ack_timer.timer.function = llc_conn_ack_tmr_cb;

	init_timer(&llc->pf_cycle_timer.timer);
	llc->pf_cycle_timer.expire	   = sysctl_llc2_p_timeout;
	llc->pf_cycle_timer.timer.data     = (unsigned long)sk;
	llc->pf_cycle_timer.timer.function = llc_conn_pf_cycle_tmr_cb;

	init_timer(&llc->rej_sent_timer.timer);
	llc->rej_sent_timer.expire	   = sysctl_llc2_rej_timeout;
	llc->rej_sent_timer.timer.data     = (unsigned long)sk;
	llc->rej_sent_timer.timer.function = llc_conn_rej_tmr_cb;

	init_timer(&llc->busy_state_timer.timer);
	llc->busy_state_timer.expire	     = sysctl_llc2_busy_timeout;
	llc->busy_state_timer.timer.data     = (unsigned long)sk;
	llc->busy_state_timer.timer.function = llc_conn_busy_tmr_cb;

	llc->n2 = 2;   /* max retransmit */
	llc->k  = 2;   /* tx win size, will adjust dynam */
	llc->rw = 128; /* rx win size (opt and equal to
			* tx_win of remote LLC) */
	skb_queue_head_init(&llc->pdu_unack_q);
	sk->sk_backlog_rcv = llc_backlog_rcv;
}

/**
 *	llc_sk_alloc - Allocates LLC sock
 *	@family: upper layer protocol family
 *	@priority: for allocation (%GFP_KERNEL, %GFP_ATOMIC, etc)
 *
 *	Allocates a LLC sock and initializes it. Returns the new LLC sock
 *	or %NULL if there's no memory available for one
 */
struct sock *llc_sk_alloc(int family, gfp_t priority, struct proto *prot)
{
	struct sock *sk = sk_alloc(family, priority, prot, 1);

	if (!sk)
		goto out;
	llc_sk_init(sk);
	sock_init_data(NULL, sk);
#ifdef LLC_REFCNT_DEBUG
	atomic_inc(&llc_sock_nr);
	printk(KERN_DEBUG "LLC socket %p created in %s, now we have %d alive\n", sk,
		__FUNCTION__, atomic_read(&llc_sock_nr));
#endif
out:
	return sk;
}

/**
 *	llc_sk_free - Frees a LLC socket
 *	@sk - socket to free
 *
 *	Frees a LLC socket
 */
void llc_sk_free(struct sock *sk)
{
	struct llc_sock *llc = llc_sk(sk);

	llc->state = LLC_CONN_OUT_OF_SVC;
	/* Stop all (possibly) running timers */
	llc_conn_ac_stop_all_timers(sk, NULL);
#ifdef DEBUG_LLC_CONN_ALLOC
	printk(KERN_INFO "%s: unackq=%d, txq=%d\n", __FUNCTION__,
		skb_queue_len(&llc->pdu_unack_q),
		skb_queue_len(&sk->sk_write_queue));
#endif
	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);
	skb_queue_purge(&llc->pdu_unack_q);
#ifdef LLC_REFCNT_DEBUG
	if (atomic_read(&sk->sk_refcnt) != 1) {
		printk(KERN_DEBUG "Destruction of LLC sock %p delayed in %s, cnt=%d\n",
			sk, __FUNCTION__, atomic_read(&sk->sk_refcnt));
		printk(KERN_DEBUG "%d LLC sockets are still alive\n",
			atomic_read(&llc_sock_nr));
	} else {
		atomic_dec(&llc_sock_nr);
		printk(KERN_DEBUG "LLC socket %p released in %s, %d are still alive\n", sk,
			__FUNCTION__, atomic_read(&llc_sock_nr));
	}
#endif
	sock_put(sk);
}

/**
 *	llc_sk_reset - resets a connection
 *	@sk: LLC socket to reset
 *
 *	Resets a connection to the out of service state. Stops its timers
 *	and frees any frames in the queues of the connection.
 */
void llc_sk_reset(struct sock *sk)
{
	struct llc_sock *llc = llc_sk(sk);

	llc_conn_ac_stop_all_timers(sk, NULL);
	skb_queue_purge(&sk->sk_write_queue);
	skb_queue_purge(&llc->pdu_unack_q);
	llc->remote_busy_flag	= 0;
	llc->cause_flag		= 0;
	llc->retry_count	= 0;
	llc_conn_set_p_flag(sk, 0);
	llc->f_flag		= 0;
	llc->s_flag		= 0;
	llc->ack_pf		= 0;
	llc->first_pdu_Ns	= 0;
	llc->ack_must_be_send	= 0;
	llc->dec_step		= 1;
	llc->inc_cntr		= 2;
	llc->dec_cntr		= 2;
	llc->X			= 0;
	llc->failed_data_req	= 0 ;
	llc->last_nr		= 0;
}

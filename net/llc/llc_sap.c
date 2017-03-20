/*
 * llc_sap.c - driver routines for SAP component.
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */

#include <net/llc.h>
#include <net/llc_if.h>
#include <net/llc_conn.h>
#include <net/llc_pdu.h>
#include <net/llc_sap.h>
#include <net/llc_s_ac.h>
#include <net/llc_s_ev.h>
#include <net/llc_s_st.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <linux/llc.h>
#include <linux/slab.h>

static int llc_mac_header_len(unsigned short devtype)
{
	switch (devtype) {
	case ARPHRD_ETHER:
	case ARPHRD_LOOPBACK:
		return sizeof(struct ethhdr);
	}
	return 0;
}

/**
 *	llc_alloc_frame - allocates sk_buff for frame
 *	@dev: network device this skb will be sent over
 *	@type: pdu type to allocate
 *	@data_size: data size to allocate
 *
 *	Allocates an sk_buff for frame and initializes sk_buff fields.
 *	Returns allocated skb or %NULL when out of memory.
 */
struct sk_buff *llc_alloc_frame(struct sock *sk, struct net_device *dev,
				u8 type, u32 data_size)
{
	int hlen = type == LLC_PDU_TYPE_U ? 3 : 4;
	struct sk_buff *skb;

	hlen += llc_mac_header_len(dev->type);
	skb = alloc_skb(hlen + data_size, GFP_ATOMIC);

	if (skb) {
		skb_reset_mac_header(skb);
		skb_reserve(skb, hlen);
		skb_reset_network_header(skb);
		skb_reset_transport_header(skb);
		skb->protocol = htons(ETH_P_802_2);
		skb->dev      = dev;
		if (sk != NULL)
			skb_set_owner_w(skb, sk);
	}
	return skb;
}

void llc_save_primitive(struct sock *sk, struct sk_buff *skb, u8 prim)
{
	struct sockaddr_llc *addr;

       /* save primitive for use by the user. */
	addr		  = llc_ui_skb_cb(skb);

	memset(addr, 0, sizeof(*addr));
	addr->sllc_family = sk->sk_family;
	addr->sllc_arphrd = skb->dev->type;
	addr->sllc_test   = prim == LLC_TEST_PRIM;
	addr->sllc_xid    = prim == LLC_XID_PRIM;
	addr->sllc_ua     = prim == LLC_DATAUNIT_PRIM;
	llc_pdu_decode_sa(skb, addr->sllc_mac);
	llc_pdu_decode_ssap(skb, &addr->sllc_sap);
}

/**
 *	llc_sap_rtn_pdu - Informs upper layer on rx of an UI, XID or TEST pdu.
 *	@sap: pointer to SAP
 *	@skb: received pdu
 */
void llc_sap_rtn_pdu(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	switch (LLC_U_PDU_RSP(pdu)) {
	case LLC_1_PDU_CMD_TEST:
		ev->prim = LLC_TEST_PRIM;	break;
	case LLC_1_PDU_CMD_XID:
		ev->prim = LLC_XID_PRIM;	break;
	case LLC_1_PDU_CMD_UI:
		ev->prim = LLC_DATAUNIT_PRIM;	break;
	}
	ev->ind_cfm_flag = LLC_IND;
}

/**
 *	llc_find_sap_trans - finds transition for event
 *	@sap: pointer to SAP
 *	@skb: happened event
 *
 *	This function finds transition that matches with happened event.
 *	Returns the pointer to found transition on success or %NULL for
 *	failure.
 */
static struct llc_sap_state_trans *llc_find_sap_trans(struct llc_sap *sap,
						      struct sk_buff *skb)
{
	int i = 0;
	struct llc_sap_state_trans *rc = NULL;
	struct llc_sap_state_trans **next_trans;
	struct llc_sap_state *curr_state = &llc_sap_state_table[sap->state - 1];
	/*
	 * Search thru events for this state until list exhausted or until
	 * its obvious the event is not valid for the current state
	 */
	for (next_trans = curr_state->transitions; next_trans[i]->ev; i++)
		if (!next_trans[i]->ev(sap, skb)) {
			rc = next_trans[i]; /* got event match; return it */
			break;
		}
	return rc;
}

/**
 *	llc_exec_sap_trans_actions - execute actions related to event
 *	@sap: pointer to SAP
 *	@trans: pointer to transition that it's actions must be performed
 *	@skb: happened event.
 *
 *	This function executes actions that is related to happened event.
 *	Returns 0 for success and 1 for failure of at least one action.
 */
static int llc_exec_sap_trans_actions(struct llc_sap *sap,
				      struct llc_sap_state_trans *trans,
				      struct sk_buff *skb)
{
	int rc = 0;
	const llc_sap_action_t *next_action = trans->ev_actions;

	for (; next_action && *next_action; next_action++)
		if ((*next_action)(sap, skb))
			rc = 1;
	return rc;
}

/**
 *	llc_sap_next_state - finds transition, execs actions & change SAP state
 *	@sap: pointer to SAP
 *	@skb: happened event
 *
 *	This function finds transition that matches with happened event, then
 *	executes related actions and finally changes state of SAP. It returns
 *	0 on success and 1 for failure.
 */
static int llc_sap_next_state(struct llc_sap *sap, struct sk_buff *skb)
{
	int rc = 1;
	struct llc_sap_state_trans *trans;

	if (sap->state > LLC_NR_SAP_STATES)
		goto out;
	trans = llc_find_sap_trans(sap, skb);
	if (!trans)
		goto out;
	/*
	 * Got the state to which we next transition; perform the actions
	 * associated with this transition before actually transitioning to the
	 * next state
	 */
	rc = llc_exec_sap_trans_actions(sap, trans, skb);
	if (rc)
		goto out;
	/*
	 * Transition SAP to next state if all actions execute successfully
	 */
	sap->state = trans->next_state;
out:
	return rc;
}

/**
 *	llc_sap_state_process - sends event to SAP state machine
 *	@sap: sap to use
 *	@skb: pointer to occurred event
 *
 *	After executing actions of the event, upper layer will be indicated
 *	if needed(on receiving an UI frame). sk can be null for the
 *	datalink_proto case.
 */
static void llc_sap_state_process(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	/*
	 * We have to hold the skb, because llc_sap_next_state
	 * will kfree it in the sending path and we need to
	 * look at the skb->cb, where we encode llc_sap_state_ev.
	 */
	skb_get(skb);
	ev->ind_cfm_flag = 0;
	llc_sap_next_state(sap, skb);
	if (ev->ind_cfm_flag == LLC_IND) {
		if (skb->sk->sk_state == TCP_LISTEN)
			kfree_skb(skb);
		else {
			llc_save_primitive(skb->sk, skb, ev->prim);

			/* queue skb to the user. */
			if (sock_queue_rcv_skb(skb->sk, skb))
				kfree_skb(skb);
		}
	}
	kfree_skb(skb);
}

/**
 *	llc_build_and_send_test_pkt - TEST interface for upper layers.
 *	@sap: sap to use
 *	@skb: packet to send
 *	@dmac: destination mac address
 *	@dsap: destination sap
 *
 *	This function is called when upper layer wants to send a TEST pdu.
 *	Returns 0 for success, 1 otherwise.
 */
void llc_build_and_send_test_pkt(struct llc_sap *sap,
				 struct sk_buff *skb, u8 *dmac, u8 dsap)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	ev->saddr.lsap = sap->laddr.lsap;
	ev->daddr.lsap = dsap;
	memcpy(ev->saddr.mac, skb->dev->dev_addr, IFHWADDRLEN);
	memcpy(ev->daddr.mac, dmac, IFHWADDRLEN);

	ev->type      = LLC_SAP_EV_TYPE_PRIM;
	ev->prim      = LLC_TEST_PRIM;
	ev->prim_type = LLC_PRIM_TYPE_REQ;
	llc_sap_state_process(sap, skb);
}

/**
 *	llc_build_and_send_xid_pkt - XID interface for upper layers
 *	@sap: sap to use
 *	@skb: packet to send
 *	@dmac: destination mac address
 *	@dsap: destination sap
 *
 *	This function is called when upper layer wants to send a XID pdu.
 *	Returns 0 for success, 1 otherwise.
 */
void llc_build_and_send_xid_pkt(struct llc_sap *sap, struct sk_buff *skb,
				u8 *dmac, u8 dsap)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	ev->saddr.lsap = sap->laddr.lsap;
	ev->daddr.lsap = dsap;
	memcpy(ev->saddr.mac, skb->dev->dev_addr, IFHWADDRLEN);
	memcpy(ev->daddr.mac, dmac, IFHWADDRLEN);

	ev->type      = LLC_SAP_EV_TYPE_PRIM;
	ev->prim      = LLC_XID_PRIM;
	ev->prim_type = LLC_PRIM_TYPE_REQ;
	llc_sap_state_process(sap, skb);
}

/**
 *	llc_sap_rcv - sends received pdus to the sap state machine
 *	@sap: current sap component structure.
 *	@skb: received frame.
 *
 *	Sends received pdus to the sap state machine.
 */
static void llc_sap_rcv(struct llc_sap *sap, struct sk_buff *skb,
			struct sock *sk)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	ev->type   = LLC_SAP_EV_TYPE_PDU;
	ev->reason = 0;
	skb_orphan(skb);
	sock_hold(sk);
	skb->sk = sk;
	skb->destructor = sock_efree;
	llc_sap_state_process(sap, skb);
}

static inline bool llc_dgram_match(const struct llc_sap *sap,
				   const struct llc_addr *laddr,
				   const struct sock *sk)
{
     struct llc_sock *llc = llc_sk(sk);

     return sk->sk_type == SOCK_DGRAM &&
	  llc->laddr.lsap == laddr->lsap &&
	  ether_addr_equal(llc->laddr.mac, laddr->mac);
}

/**
 *	llc_lookup_dgram - Finds dgram socket for the local sap/mac
 *	@sap: SAP
 *	@laddr: address of local LLC (MAC + SAP)
 *
 *	Search socket list of the SAP and finds connection using the local
 *	mac, and local sap. Returns pointer for socket found, %NULL otherwise.
 */
static struct sock *llc_lookup_dgram(struct llc_sap *sap,
				     const struct llc_addr *laddr)
{
	struct sock *rc;
	struct hlist_nulls_node *node;
	int slot = llc_sk_laddr_hashfn(sap, laddr);
	struct hlist_nulls_head *laddr_hb = &sap->sk_laddr_hash[slot];

	rcu_read_lock_bh();
again:
	sk_nulls_for_each_rcu(rc, node, laddr_hb) {
		if (llc_dgram_match(sap, laddr, rc)) {
			/* Extra checks required by SLAB_DESTROY_BY_RCU */
			if (unlikely(!atomic_inc_not_zero(&rc->sk_refcnt)))
				goto again;
			if (unlikely(llc_sk(rc)->sap != sap ||
				     !llc_dgram_match(sap, laddr, rc))) {
				sock_put(rc);
				continue;
			}
			goto found;
		}
	}
	rc = NULL;
	/*
	 * if the nulls value we got at the end of this lookup is
	 * not the expected one, we must restart lookup.
	 * We probably met an item that was moved to another chain.
	 */
	if (unlikely(get_nulls_value(node) != slot))
		goto again;
found:
	rcu_read_unlock_bh();
	return rc;
}

static inline bool llc_mcast_match(const struct llc_sap *sap,
				   const struct llc_addr *laddr,
				   const struct sk_buff *skb,
				   const struct sock *sk)
{
     struct llc_sock *llc = llc_sk(sk);

     return sk->sk_type == SOCK_DGRAM &&
	  llc->laddr.lsap == laddr->lsap &&
	  llc->dev == skb->dev;
}

static void llc_do_mcast(struct llc_sap *sap, struct sk_buff *skb,
			 struct sock **stack, int count)
{
	struct sk_buff *skb1;
	int i;

	for (i = 0; i < count; i++) {
		skb1 = skb_clone(skb, GFP_ATOMIC);
		if (!skb1) {
			sock_put(stack[i]);
			continue;
		}

		llc_sap_rcv(sap, skb1, stack[i]);
		sock_put(stack[i]);
	}
}

/**
 * 	llc_sap_mcast - Deliver multicast PDU's to all matching datagram sockets.
 *	@sap: SAP
 *	@laddr: address of local LLC (MAC + SAP)
 *
 *	Search socket list of the SAP and finds connections with same sap.
 *	Deliver clone to each.
 */
static void llc_sap_mcast(struct llc_sap *sap,
			  const struct llc_addr *laddr,
			  struct sk_buff *skb)
{
	int i = 0, count = 256 / sizeof(struct sock *);
	struct sock *sk, *stack[count];
	struct llc_sock *llc;
	struct hlist_head *dev_hb = llc_sk_dev_hash(sap, skb->dev->ifindex);

	spin_lock_bh(&sap->sk_lock);
	hlist_for_each_entry(llc, dev_hb, dev_hash_node) {

		sk = &llc->sk;

		if (!llc_mcast_match(sap, laddr, skb, sk))
			continue;

		sock_hold(sk);
		if (i < count)
			stack[i++] = sk;
		else {
			llc_do_mcast(sap, skb, stack, i);
			i = 0;
		}
	}
	spin_unlock_bh(&sap->sk_lock);

	llc_do_mcast(sap, skb, stack, i);
}


void llc_sap_handler(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_addr laddr;

	llc_pdu_decode_da(skb, laddr.mac);
	llc_pdu_decode_dsap(skb, &laddr.lsap);

	if (is_multicast_ether_addr(laddr.mac)) {
		llc_sap_mcast(sap, &laddr, skb);
		kfree_skb(skb);
	} else {
		struct sock *sk = llc_lookup_dgram(sap, &laddr);
		if (sk) {
			llc_sap_rcv(sap, skb, sk);
			sock_put(sk);
		} else
			kfree_skb(skb);
	}
}

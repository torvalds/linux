/*
 * net/tipc/bcast.c: TIPC broadcast code
 *
 * Copyright (c) 2004-2006, 2014-2015, Ericsson AB
 * Copyright (c) 2004, Intel Corporation.
 * Copyright (c) 2005, 2010-2011, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "socket.h"
#include "msg.h"
#include "bcast.h"
#include "name_distr.h"
#include "core.h"

#define	MAX_PKT_DEFAULT_MCAST	1500	/* bcast link max packet size (fixed) */
#define	BCLINK_WIN_DEFAULT	50	/* bcast link window size (default) */
#define	BCLINK_WIN_MIN	        32	/* bcast minimum link window size */

const char tipc_bclink_name[] = "broadcast-link";

static void tipc_nmap_diff(struct tipc_node_map *nm_a,
			   struct tipc_node_map *nm_b,
			   struct tipc_node_map *nm_diff);
static void tipc_nmap_add(struct tipc_node_map *nm_ptr, u32 node);
static void tipc_nmap_remove(struct tipc_node_map *nm_ptr, u32 node);

static void tipc_bclink_lock(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	spin_lock_bh(&tn->bclink->lock);
}

static void tipc_bclink_unlock(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	spin_unlock_bh(&tn->bclink->lock);
}

void tipc_bclink_input(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	tipc_sk_mcast_rcv(net, &tn->bclink->arrvq, &tn->bclink->inputq);
}

uint  tipc_bclink_get_mtu(void)
{
	return MAX_PKT_DEFAULT_MCAST;
}

static u32 bcbuf_acks(struct sk_buff *buf)
{
	return (u32)(unsigned long)TIPC_SKB_CB(buf)->handle;
}

static void bcbuf_set_acks(struct sk_buff *buf, u32 acks)
{
	TIPC_SKB_CB(buf)->handle = (void *)(unsigned long)acks;
}

static void bcbuf_decr_acks(struct sk_buff *buf)
{
	bcbuf_set_acks(buf, bcbuf_acks(buf) - 1);
}

void tipc_bclink_add_node(struct net *net, u32 addr)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	tipc_bclink_lock(net);
	tipc_nmap_add(&tn->bclink->bcast_nodes, addr);
	tipc_bclink_unlock(net);
}

void tipc_bclink_remove_node(struct net *net, u32 addr)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	tipc_bclink_lock(net);
	tipc_nmap_remove(&tn->bclink->bcast_nodes, addr);

	/* Last node? => reset backlog queue */
	if (!tn->bclink->bcast_nodes.count)
		tipc_link_purge_backlog(&tn->bclink->link);

	tipc_bclink_unlock(net);
}

static void bclink_set_last_sent(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *bcl = tn->bcl;

	bcl->silent_intv_cnt = mod(bcl->snd_nxt - 1);
}

u32 tipc_bclink_get_last_sent(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	return tn->bcl->silent_intv_cnt;
}

static void bclink_update_last_sent(struct tipc_node *node, u32 seqno)
{
	node->bclink.last_sent = less_eq(node->bclink.last_sent, seqno) ?
						seqno : node->bclink.last_sent;
}

/**
 * tipc_bclink_retransmit_to - get most recent node to request retransmission
 *
 * Called with bclink_lock locked
 */
struct tipc_node *tipc_bclink_retransmit_to(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	return tn->bclink->retransmit_to;
}

/**
 * bclink_retransmit_pkt - retransmit broadcast packets
 * @after: sequence number of last packet to *not* retransmit
 * @to: sequence number of last packet to retransmit
 *
 * Called with bclink_lock locked
 */
static void bclink_retransmit_pkt(struct tipc_net *tn, u32 after, u32 to)
{
	struct sk_buff *skb;
	struct tipc_link *bcl = tn->bcl;

	skb_queue_walk(&bcl->transmq, skb) {
		if (more(buf_seqno(skb), after)) {
			tipc_link_retransmit(bcl, skb, mod(to - after));
			break;
		}
	}
}

/**
 * bclink_prepare_wakeup - prepare users for wakeup after congestion
 * @bcl: broadcast link
 * @resultq: queue for users which can be woken up
 * Move a number of waiting users, as permitted by available space in
 * the send queue, from link wait queue to specified queue for wakeup
 */
static void bclink_prepare_wakeup(struct tipc_link *bcl, struct sk_buff_head *resultq)
{
	int pnd[TIPC_SYSTEM_IMPORTANCE + 1] = {0,};
	int imp, lim;
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&bcl->wakeupq, skb, tmp) {
		imp = TIPC_SKB_CB(skb)->chain_imp;
		lim = bcl->window + bcl->backlog[imp].limit;
		pnd[imp] += TIPC_SKB_CB(skb)->chain_sz;
		if ((pnd[imp] + bcl->backlog[imp].len) >= lim)
			continue;
		skb_unlink(skb, &bcl->wakeupq);
		skb_queue_tail(resultq, skb);
	}
}

/**
 * tipc_bclink_wakeup_users - wake up pending users
 *
 * Called with no locks taken
 */
void tipc_bclink_wakeup_users(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *bcl = tn->bcl;
	struct sk_buff_head resultq;

	skb_queue_head_init(&resultq);
	bclink_prepare_wakeup(bcl, &resultq);
	tipc_sk_rcv(net, &resultq);
}

/**
 * tipc_bclink_acknowledge - handle acknowledgement of broadcast packets
 * @n_ptr: node that sent acknowledgement info
 * @acked: broadcast sequence # that has been acknowledged
 *
 * Node is locked, bclink_lock unlocked.
 */
void tipc_bclink_acknowledge(struct tipc_node *n_ptr, u32 acked)
{
	struct sk_buff *skb, *tmp;
	unsigned int released = 0;
	struct net *net = n_ptr->net;
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	if (unlikely(!n_ptr->bclink.recv_permitted))
		return;

	tipc_bclink_lock(net);

	/* Bail out if tx queue is empty (no clean up is required) */
	skb = skb_peek(&tn->bcl->transmq);
	if (!skb)
		goto exit;

	/* Determine which messages need to be acknowledged */
	if (acked == INVALID_LINK_SEQ) {
		/*
		 * Contact with specified node has been lost, so need to
		 * acknowledge sent messages only (if other nodes still exist)
		 * or both sent and unsent messages (otherwise)
		 */
		if (tn->bclink->bcast_nodes.count)
			acked = tn->bcl->silent_intv_cnt;
		else
			acked = tn->bcl->snd_nxt;
	} else {
		/*
		 * Bail out if specified sequence number does not correspond
		 * to a message that has been sent and not yet acknowledged
		 */
		if (less(acked, buf_seqno(skb)) ||
		    less(tn->bcl->silent_intv_cnt, acked) ||
		    less_eq(acked, n_ptr->bclink.acked))
			goto exit;
	}

	/* Skip over packets that node has previously acknowledged */
	skb_queue_walk(&tn->bcl->transmq, skb) {
		if (more(buf_seqno(skb), n_ptr->bclink.acked))
			break;
	}

	/* Update packets that node is now acknowledging */
	skb_queue_walk_from_safe(&tn->bcl->transmq, skb, tmp) {
		if (more(buf_seqno(skb), acked))
			break;
		bcbuf_decr_acks(skb);
		bclink_set_last_sent(net);
		if (bcbuf_acks(skb) == 0) {
			__skb_unlink(skb, &tn->bcl->transmq);
			kfree_skb(skb);
			released = 1;
		}
	}
	n_ptr->bclink.acked = acked;

	/* Try resolving broadcast link congestion, if necessary */
	if (unlikely(skb_peek(&tn->bcl->backlogq))) {
		tipc_link_push_packets(tn->bcl);
		bclink_set_last_sent(net);
	}
	if (unlikely(released && !skb_queue_empty(&tn->bcl->wakeupq)))
		n_ptr->action_flags |= TIPC_WAKEUP_BCAST_USERS;
exit:
	tipc_bclink_unlock(net);
}

/**
 * tipc_bclink_update_link_state - update broadcast link state
 *
 * RCU and node lock set
 */
void tipc_bclink_update_link_state(struct tipc_node *n_ptr,
				   u32 last_sent)
{
	struct sk_buff *buf;
	struct net *net = n_ptr->net;
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	/* Ignore "stale" link state info */
	if (less_eq(last_sent, n_ptr->bclink.last_in))
		return;

	/* Update link synchronization state; quit if in sync */
	bclink_update_last_sent(n_ptr, last_sent);

	if (n_ptr->bclink.last_sent == n_ptr->bclink.last_in)
		return;

	/* Update out-of-sync state; quit if loss is still unconfirmed */
	if ((++n_ptr->bclink.oos_state) == 1) {
		if (n_ptr->bclink.deferred_size < (TIPC_MIN_LINK_WIN / 2))
			return;
		n_ptr->bclink.oos_state++;
	}

	/* Don't NACK if one has been recently sent (or seen) */
	if (n_ptr->bclink.oos_state & 0x1)
		return;

	/* Send NACK */
	buf = tipc_buf_acquire(INT_H_SIZE);
	if (buf) {
		struct tipc_msg *msg = buf_msg(buf);
		struct sk_buff *skb = skb_peek(&n_ptr->bclink.deferdq);
		u32 to = skb ? buf_seqno(skb) - 1 : n_ptr->bclink.last_sent;

		tipc_msg_init(tn->own_addr, msg, BCAST_PROTOCOL, STATE_MSG,
			      INT_H_SIZE, n_ptr->addr);
		msg_set_non_seq(msg, 1);
		msg_set_mc_netid(msg, tn->net_id);
		msg_set_bcast_ack(msg, n_ptr->bclink.last_in);
		msg_set_bcgap_after(msg, n_ptr->bclink.last_in);
		msg_set_bcgap_to(msg, to);

		tipc_bclink_lock(net);
		tipc_bearer_send(net, MAX_BEARERS, buf, NULL);
		tn->bcl->stats.sent_nacks++;
		tipc_bclink_unlock(net);
		kfree_skb(buf);

		n_ptr->bclink.oos_state++;
	}
}

void tipc_bclink_sync_state(struct tipc_node *n, struct tipc_msg *hdr)
{
	u16 last = msg_last_bcast(hdr);
	int mtyp = msg_type(hdr);

	if (unlikely(msg_user(hdr) != LINK_PROTOCOL))
		return;
	if (mtyp == STATE_MSG) {
		tipc_bclink_update_link_state(n, last);
		return;
	}
	/* Compatibility: older nodes don't know BCAST_PROTOCOL synchronization,
	 * and transfer synch info in LINK_PROTOCOL messages.
	 */
	if (tipc_node_is_up(n))
		return;
	if ((mtyp != RESET_MSG) && (mtyp != ACTIVATE_MSG))
		return;
	n->bclink.last_sent = last;
	n->bclink.last_in = last;
	n->bclink.oos_state = 0;
}

/**
 * bclink_peek_nack - monitor retransmission requests sent by other nodes
 *
 * Delay any upcoming NACK by this node if another node has already
 * requested the first message this node is going to ask for.
 */
static void bclink_peek_nack(struct net *net, struct tipc_msg *msg)
{
	struct tipc_node *n_ptr = tipc_node_find(net, msg_destnode(msg));

	if (unlikely(!n_ptr))
		return;

	tipc_node_lock(n_ptr);
	if (n_ptr->bclink.recv_permitted &&
	    (n_ptr->bclink.last_in != n_ptr->bclink.last_sent) &&
	    (n_ptr->bclink.last_in == msg_bcgap_after(msg)))
		n_ptr->bclink.oos_state = 2;
	tipc_node_unlock(n_ptr);
	tipc_node_put(n_ptr);
}

/* tipc_bclink_xmit - deliver buffer chain to all nodes in cluster
 *                    and to identified node local sockets
 * @net: the applicable net namespace
 * @list: chain of buffers containing message
 * Consumes the buffer chain, except when returning -ELINKCONG
 * Returns 0 if success, otherwise errno: -ELINKCONG,-EHOSTUNREACH,-EMSGSIZE
 */
int tipc_bclink_xmit(struct net *net, struct sk_buff_head *list)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *bcl = tn->bcl;
	struct tipc_bclink *bclink = tn->bclink;
	int rc = 0;
	int bc = 0;
	struct sk_buff *skb;
	struct sk_buff_head arrvq;
	struct sk_buff_head inputq;

	/* Prepare clone of message for local node */
	skb = tipc_msg_reassemble(list);
	if (unlikely(!skb))
		return -EHOSTUNREACH;

	/* Broadcast to all nodes */
	if (likely(bclink)) {
		tipc_bclink_lock(net);
		if (likely(bclink->bcast_nodes.count)) {
			rc = __tipc_link_xmit(net, bcl, list);
			if (likely(!rc)) {
				u32 len = skb_queue_len(&bcl->transmq);

				bclink_set_last_sent(net);
				bcl->stats.queue_sz_counts++;
				bcl->stats.accu_queue_sz += len;
			}
			bc = 1;
		}
		tipc_bclink_unlock(net);
	}

	if (unlikely(!bc))
		__skb_queue_purge(list);

	if (unlikely(rc)) {
		kfree_skb(skb);
		return rc;
	}
	/* Deliver message clone */
	__skb_queue_head_init(&arrvq);
	skb_queue_head_init(&inputq);
	__skb_queue_tail(&arrvq, skb);
	tipc_sk_mcast_rcv(net, &arrvq, &inputq);
	return rc;
}

/**
 * bclink_accept_pkt - accept an incoming, in-sequence broadcast packet
 *
 * Called with both sending node's lock and bclink_lock taken.
 */
static void bclink_accept_pkt(struct tipc_node *node, u32 seqno)
{
	struct tipc_net *tn = net_generic(node->net, tipc_net_id);

	bclink_update_last_sent(node, seqno);
	node->bclink.last_in = seqno;
	node->bclink.oos_state = 0;
	tn->bcl->stats.recv_info++;

	/*
	 * Unicast an ACK periodically, ensuring that
	 * all nodes in the cluster don't ACK at the same time
	 */
	if (((seqno - tn->own_addr) % TIPC_MIN_LINK_WIN) == 0) {
		tipc_link_proto_xmit(node_active_link(node, node->addr),
				     STATE_MSG, 0, 0, 0, 0);
		tn->bcl->stats.sent_acks++;
	}
}

/**
 * tipc_bclink_rcv - receive a broadcast packet, and deliver upwards
 *
 * RCU is locked, no other locks set
 */
void tipc_bclink_rcv(struct net *net, struct sk_buff *buf)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *bcl = tn->bcl;
	struct tipc_msg *msg = buf_msg(buf);
	struct tipc_node *node;
	u32 next_in;
	u32 seqno;
	int deferred = 0;
	int pos = 0;
	struct sk_buff *iskb;
	struct sk_buff_head *arrvq, *inputq;

	/* Screen out unwanted broadcast messages */
	if (msg_mc_netid(msg) != tn->net_id)
		goto exit;

	node = tipc_node_find(net, msg_prevnode(msg));
	if (unlikely(!node))
		goto exit;

	tipc_node_lock(node);
	if (unlikely(!node->bclink.recv_permitted))
		goto unlock;

	/* Handle broadcast protocol message */
	if (unlikely(msg_user(msg) == BCAST_PROTOCOL)) {
		if (msg_type(msg) != STATE_MSG)
			goto unlock;
		if (msg_destnode(msg) == tn->own_addr) {
			tipc_bclink_acknowledge(node, msg_bcast_ack(msg));
			tipc_bclink_lock(net);
			bcl->stats.recv_nacks++;
			tn->bclink->retransmit_to = node;
			bclink_retransmit_pkt(tn, msg_bcgap_after(msg),
					      msg_bcgap_to(msg));
			tipc_bclink_unlock(net);
			tipc_node_unlock(node);
		} else {
			tipc_node_unlock(node);
			bclink_peek_nack(net, msg);
		}
		tipc_node_put(node);
		goto exit;
	}

	/* Handle in-sequence broadcast message */
	seqno = msg_seqno(msg);
	next_in = mod(node->bclink.last_in + 1);
	arrvq = &tn->bclink->arrvq;
	inputq = &tn->bclink->inputq;

	if (likely(seqno == next_in)) {
receive:
		/* Deliver message to destination */
		if (likely(msg_isdata(msg))) {
			tipc_bclink_lock(net);
			bclink_accept_pkt(node, seqno);
			spin_lock_bh(&inputq->lock);
			__skb_queue_tail(arrvq, buf);
			spin_unlock_bh(&inputq->lock);
			node->action_flags |= TIPC_BCAST_MSG_EVT;
			tipc_bclink_unlock(net);
			tipc_node_unlock(node);
		} else if (msg_user(msg) == MSG_BUNDLER) {
			tipc_bclink_lock(net);
			bclink_accept_pkt(node, seqno);
			bcl->stats.recv_bundles++;
			bcl->stats.recv_bundled += msg_msgcnt(msg);
			pos = 0;
			while (tipc_msg_extract(buf, &iskb, &pos)) {
				spin_lock_bh(&inputq->lock);
				__skb_queue_tail(arrvq, iskb);
				spin_unlock_bh(&inputq->lock);
			}
			node->action_flags |= TIPC_BCAST_MSG_EVT;
			tipc_bclink_unlock(net);
			tipc_node_unlock(node);
		} else if (msg_user(msg) == MSG_FRAGMENTER) {
			tipc_bclink_lock(net);
			bclink_accept_pkt(node, seqno);
			tipc_buf_append(&node->bclink.reasm_buf, &buf);
			if (unlikely(!buf && !node->bclink.reasm_buf)) {
				tipc_bclink_unlock(net);
				goto unlock;
			}
			bcl->stats.recv_fragments++;
			if (buf) {
				bcl->stats.recv_fragmented++;
				msg = buf_msg(buf);
				tipc_bclink_unlock(net);
				goto receive;
			}
			tipc_bclink_unlock(net);
			tipc_node_unlock(node);
		} else {
			tipc_bclink_lock(net);
			bclink_accept_pkt(node, seqno);
			tipc_bclink_unlock(net);
			tipc_node_unlock(node);
			kfree_skb(buf);
		}
		buf = NULL;

		/* Determine new synchronization state */
		tipc_node_lock(node);
		if (unlikely(!tipc_node_is_up(node)))
			goto unlock;

		if (node->bclink.last_in == node->bclink.last_sent)
			goto unlock;

		if (skb_queue_empty(&node->bclink.deferdq)) {
			node->bclink.oos_state = 1;
			goto unlock;
		}

		msg = buf_msg(skb_peek(&node->bclink.deferdq));
		seqno = msg_seqno(msg);
		next_in = mod(next_in + 1);
		if (seqno != next_in)
			goto unlock;

		/* Take in-sequence message from deferred queue & deliver it */
		buf = __skb_dequeue(&node->bclink.deferdq);
		goto receive;
	}

	/* Handle out-of-sequence broadcast message */
	if (less(next_in, seqno)) {
		deferred = tipc_link_defer_pkt(&node->bclink.deferdq,
					       buf);
		bclink_update_last_sent(node, seqno);
		buf = NULL;
	}

	tipc_bclink_lock(net);

	if (deferred)
		bcl->stats.deferred_recv++;
	else
		bcl->stats.duplicates++;

	tipc_bclink_unlock(net);

unlock:
	tipc_node_unlock(node);
	tipc_node_put(node);
exit:
	kfree_skb(buf);
}

u32 tipc_bclink_acks_missing(struct tipc_node *n_ptr)
{
	return (n_ptr->bclink.recv_permitted &&
		(tipc_bclink_get_last_sent(n_ptr->net) != n_ptr->bclink.acked));
}


/**
 * tipc_bcbearer_send - send a packet through the broadcast pseudo-bearer
 *
 * Send packet over as many bearers as necessary to reach all nodes
 * that have joined the broadcast link.
 *
 * Returns 0 (packet sent successfully) under all circumstances,
 * since the broadcast link's pseudo-bearer never blocks
 */
static int tipc_bcbearer_send(struct net *net, struct sk_buff *buf,
			      struct tipc_bearer *unused1,
			      struct tipc_media_addr *unused2)
{
	int bp_index;
	struct tipc_msg *msg = buf_msg(buf);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bcbearer *bcbearer = tn->bcbearer;
	struct tipc_bclink *bclink = tn->bclink;

	/* Prepare broadcast link message for reliable transmission,
	 * if first time trying to send it;
	 * preparation is skipped for broadcast link protocol messages
	 * since they are sent in an unreliable manner and don't need it
	 */
	if (likely(!msg_non_seq(buf_msg(buf)))) {
		bcbuf_set_acks(buf, bclink->bcast_nodes.count);
		msg_set_non_seq(msg, 1);
		msg_set_mc_netid(msg, tn->net_id);
		tn->bcl->stats.sent_info++;
		if (WARN_ON(!bclink->bcast_nodes.count)) {
			dump_stack();
			return 0;
		}
	}

	/* Send buffer over bearers until all targets reached */
	bcbearer->remains = bclink->bcast_nodes;

	for (bp_index = 0; bp_index < MAX_BEARERS; bp_index++) {
		struct tipc_bearer *p = bcbearer->bpairs[bp_index].primary;
		struct tipc_bearer *s = bcbearer->bpairs[bp_index].secondary;
		struct tipc_bearer *bp[2] = {p, s};
		struct tipc_bearer *b = bp[msg_link_selector(msg)];
		struct sk_buff *tbuf;

		if (!p)
			break; /* No more bearers to try */
		if (!b)
			b = p;
		tipc_nmap_diff(&bcbearer->remains, &b->nodes,
			       &bcbearer->remains_new);
		if (bcbearer->remains_new.count == bcbearer->remains.count)
			continue; /* Nothing added by bearer pair */

		if (bp_index == 0) {
			/* Use original buffer for first bearer */
			tipc_bearer_send(net, b->identity, buf, &b->bcast_addr);
		} else {
			/* Avoid concurrent buffer access */
			tbuf = pskb_copy_for_clone(buf, GFP_ATOMIC);
			if (!tbuf)
				break;
			tipc_bearer_send(net, b->identity, tbuf,
					 &b->bcast_addr);
			kfree_skb(tbuf); /* Bearer keeps a clone */
		}
		if (bcbearer->remains_new.count == 0)
			break; /* All targets reached */

		bcbearer->remains = bcbearer->remains_new;
	}

	return 0;
}

/**
 * tipc_bcbearer_sort - create sets of bearer pairs used by broadcast bearer
 */
void tipc_bcbearer_sort(struct net *net, struct tipc_node_map *nm_ptr,
			u32 node, bool action)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bcbearer *bcbearer = tn->bcbearer;
	struct tipc_bcbearer_pair *bp_temp = bcbearer->bpairs_temp;
	struct tipc_bcbearer_pair *bp_curr;
	struct tipc_bearer *b;
	int b_index;
	int pri;

	tipc_bclink_lock(net);

	if (action)
		tipc_nmap_add(nm_ptr, node);
	else
		tipc_nmap_remove(nm_ptr, node);

	/* Group bearers by priority (can assume max of two per priority) */
	memset(bp_temp, 0, sizeof(bcbearer->bpairs_temp));

	rcu_read_lock();
	for (b_index = 0; b_index < MAX_BEARERS; b_index++) {
		b = rcu_dereference_rtnl(tn->bearer_list[b_index]);
		if (!b || !b->nodes.count)
			continue;

		if (!bp_temp[b->priority].primary)
			bp_temp[b->priority].primary = b;
		else
			bp_temp[b->priority].secondary = b;
	}
	rcu_read_unlock();

	/* Create array of bearer pairs for broadcasting */
	bp_curr = bcbearer->bpairs;
	memset(bcbearer->bpairs, 0, sizeof(bcbearer->bpairs));

	for (pri = TIPC_MAX_LINK_PRI; pri >= 0; pri--) {

		if (!bp_temp[pri].primary)
			continue;

		bp_curr->primary = bp_temp[pri].primary;

		if (bp_temp[pri].secondary) {
			if (tipc_nmap_equal(&bp_temp[pri].primary->nodes,
					    &bp_temp[pri].secondary->nodes)) {
				bp_curr->secondary = bp_temp[pri].secondary;
			} else {
				bp_curr++;
				bp_curr->primary = bp_temp[pri].secondary;
			}
		}

		bp_curr++;
	}

	tipc_bclink_unlock(net);
}

static int __tipc_nl_add_bc_link_stat(struct sk_buff *skb,
				      struct tipc_stats *stats)
{
	int i;
	struct nlattr *nest;

	struct nla_map {
		__u32 key;
		__u32 val;
	};

	struct nla_map map[] = {
		{TIPC_NLA_STATS_RX_INFO, stats->recv_info},
		{TIPC_NLA_STATS_RX_FRAGMENTS, stats->recv_fragments},
		{TIPC_NLA_STATS_RX_FRAGMENTED, stats->recv_fragmented},
		{TIPC_NLA_STATS_RX_BUNDLES, stats->recv_bundles},
		{TIPC_NLA_STATS_RX_BUNDLED, stats->recv_bundled},
		{TIPC_NLA_STATS_TX_INFO, stats->sent_info},
		{TIPC_NLA_STATS_TX_FRAGMENTS, stats->sent_fragments},
		{TIPC_NLA_STATS_TX_FRAGMENTED, stats->sent_fragmented},
		{TIPC_NLA_STATS_TX_BUNDLES, stats->sent_bundles},
		{TIPC_NLA_STATS_TX_BUNDLED, stats->sent_bundled},
		{TIPC_NLA_STATS_RX_NACKS, stats->recv_nacks},
		{TIPC_NLA_STATS_RX_DEFERRED, stats->deferred_recv},
		{TIPC_NLA_STATS_TX_NACKS, stats->sent_nacks},
		{TIPC_NLA_STATS_TX_ACKS, stats->sent_acks},
		{TIPC_NLA_STATS_RETRANSMITTED, stats->retransmitted},
		{TIPC_NLA_STATS_DUPLICATES, stats->duplicates},
		{TIPC_NLA_STATS_LINK_CONGS, stats->link_congs},
		{TIPC_NLA_STATS_MAX_QUEUE, stats->max_queue_sz},
		{TIPC_NLA_STATS_AVG_QUEUE, stats->queue_sz_counts ?
			(stats->accu_queue_sz / stats->queue_sz_counts) : 0}
	};

	nest = nla_nest_start(skb, TIPC_NLA_LINK_STATS);
	if (!nest)
		return -EMSGSIZE;

	for (i = 0; i <  ARRAY_SIZE(map); i++)
		if (nla_put_u32(skb, map[i].key, map[i].val))
			goto msg_full;

	nla_nest_end(skb, nest);

	return 0;
msg_full:
	nla_nest_cancel(skb, nest);

	return -EMSGSIZE;
}

int tipc_nl_add_bc_link(struct net *net, struct tipc_nl_msg *msg)
{
	int err;
	void *hdr;
	struct nlattr *attrs;
	struct nlattr *prop;
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *bcl = tn->bcl;

	if (!bcl)
		return 0;

	tipc_bclink_lock(net);

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  NLM_F_MULTI, TIPC_NL_LINK_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_LINK);
	if (!attrs)
		goto msg_full;

	/* The broadcast link is always up */
	if (nla_put_flag(msg->skb, TIPC_NLA_LINK_UP))
		goto attr_msg_full;

	if (nla_put_flag(msg->skb, TIPC_NLA_LINK_BROADCAST))
		goto attr_msg_full;
	if (nla_put_string(msg->skb, TIPC_NLA_LINK_NAME, bcl->name))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_RX, bcl->rcv_nxt))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_TX, bcl->snd_nxt))
		goto attr_msg_full;

	prop = nla_nest_start(msg->skb, TIPC_NLA_LINK_PROP);
	if (!prop)
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_WIN, bcl->window))
		goto prop_msg_full;
	nla_nest_end(msg->skb, prop);

	err = __tipc_nl_add_bc_link_stat(msg->skb, &bcl->stats);
	if (err)
		goto attr_msg_full;

	tipc_bclink_unlock(net);
	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

prop_msg_full:
	nla_nest_cancel(msg->skb, prop);
attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	tipc_bclink_unlock(net);
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

int tipc_bclink_reset_stats(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *bcl = tn->bcl;

	if (!bcl)
		return -ENOPROTOOPT;

	tipc_bclink_lock(net);
	memset(&bcl->stats, 0, sizeof(bcl->stats));
	tipc_bclink_unlock(net);
	return 0;
}

int tipc_bclink_set_queue_limits(struct net *net, u32 limit)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *bcl = tn->bcl;

	if (!bcl)
		return -ENOPROTOOPT;
	if (limit < BCLINK_WIN_MIN)
		limit = BCLINK_WIN_MIN;
	if (limit > TIPC_MAX_LINK_WIN)
		return -EINVAL;
	tipc_bclink_lock(net);
	tipc_link_set_queue_limits(bcl, limit);
	tipc_bclink_unlock(net);
	return 0;
}

int tipc_nl_bc_link_set(struct net *net, struct nlattr *attrs[])
{
	int err;
	u32 win;
	struct nlattr *props[TIPC_NLA_PROP_MAX + 1];

	if (!attrs[TIPC_NLA_LINK_PROP])
		return -EINVAL;

	err = tipc_nl_parse_link_prop(attrs[TIPC_NLA_LINK_PROP], props);
	if (err)
		return err;

	if (!props[TIPC_NLA_PROP_WIN])
		return -EOPNOTSUPP;

	win = nla_get_u32(props[TIPC_NLA_PROP_WIN]);

	return tipc_bclink_set_queue_limits(net, win);
}

int tipc_bclink_init(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bcbearer *bcbearer;
	struct tipc_bclink *bclink;
	struct tipc_link *bcl;

	bcbearer = kzalloc(sizeof(*bcbearer), GFP_ATOMIC);
	if (!bcbearer)
		return -ENOMEM;

	bclink = kzalloc(sizeof(*bclink), GFP_ATOMIC);
	if (!bclink) {
		kfree(bcbearer);
		return -ENOMEM;
	}

	bcl = &bclink->link;
	bcbearer->bearer.media = &bcbearer->media;
	bcbearer->media.send_msg = tipc_bcbearer_send;
	sprintf(bcbearer->media.name, "tipc-broadcast");

	spin_lock_init(&bclink->lock);
	__skb_queue_head_init(&bcl->transmq);
	__skb_queue_head_init(&bcl->backlogq);
	__skb_queue_head_init(&bcl->deferdq);
	skb_queue_head_init(&bcl->wakeupq);
	bcl->snd_nxt = 1;
	spin_lock_init(&bclink->node.lock);
	__skb_queue_head_init(&bclink->arrvq);
	skb_queue_head_init(&bclink->inputq);
	bcl->owner = &bclink->node;
	bcl->owner->net = net;
	bcl->mtu = MAX_PKT_DEFAULT_MCAST;
	tipc_link_set_queue_limits(bcl, BCLINK_WIN_DEFAULT);
	bcl->bearer_id = MAX_BEARERS;
	rcu_assign_pointer(tn->bearer_list[MAX_BEARERS], &bcbearer->bearer);
	bcl->pmsg = (struct tipc_msg *)&bcl->proto_msg;
	msg_set_prevnode(bcl->pmsg, tn->own_addr);
	strlcpy(bcl->name, tipc_bclink_name, TIPC_MAX_LINK_NAME);
	tn->bcbearer = bcbearer;
	tn->bclink = bclink;
	tn->bcl = bcl;
	return 0;
}

void tipc_bclink_stop(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	tipc_bclink_lock(net);
	tipc_link_purge_queues(tn->bcl);
	tipc_bclink_unlock(net);

	RCU_INIT_POINTER(tn->bearer_list[BCBEARER], NULL);
	synchronize_net();
	kfree(tn->bcbearer);
	kfree(tn->bclink);
}

/**
 * tipc_nmap_add - add a node to a node map
 */
static void tipc_nmap_add(struct tipc_node_map *nm_ptr, u32 node)
{
	int n = tipc_node(node);
	int w = n / WSIZE;
	u32 mask = (1 << (n % WSIZE));

	if ((nm_ptr->map[w] & mask) == 0) {
		nm_ptr->count++;
		nm_ptr->map[w] |= mask;
	}
}

/**
 * tipc_nmap_remove - remove a node from a node map
 */
static void tipc_nmap_remove(struct tipc_node_map *nm_ptr, u32 node)
{
	int n = tipc_node(node);
	int w = n / WSIZE;
	u32 mask = (1 << (n % WSIZE));

	if ((nm_ptr->map[w] & mask) != 0) {
		nm_ptr->map[w] &= ~mask;
		nm_ptr->count--;
	}
}

/**
 * tipc_nmap_diff - find differences between node maps
 * @nm_a: input node map A
 * @nm_b: input node map B
 * @nm_diff: output node map A-B (i.e. nodes of A that are not in B)
 */
static void tipc_nmap_diff(struct tipc_node_map *nm_a,
			   struct tipc_node_map *nm_b,
			   struct tipc_node_map *nm_diff)
{
	int stop = ARRAY_SIZE(nm_a->map);
	int w;
	int b;
	u32 map;

	memset(nm_diff, 0, sizeof(*nm_diff));
	for (w = 0; w < stop; w++) {
		map = nm_a->map[w] ^ (nm_a->map[w] & nm_b->map[w]);
		nm_diff->map[w] = map;
		if (map != 0) {
			for (b = 0 ; b < WSIZE; b++) {
				if (map & (1 << b))
					nm_diff->count++;
			}
		}
	}
}

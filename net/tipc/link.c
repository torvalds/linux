/*
 * net/tipc/link.c: TIPC link code
 *
 * Copyright (c) 1996-2007, 2012-2015, Ericsson AB
 * Copyright (c) 2004-2007, 2010-2013, Wind River Systems
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

#include "core.h"
#include "link.h"
#include "bcast.h"
#include "socket.h"
#include "name_distr.h"
#include "discover.h"
#include "netlink.h"

#include <linux/pkt_sched.h>

/*
 * Error message prefixes
 */
static const char *link_co_err = "Link changeover error, ";
static const char *link_rst_msg = "Resetting link ";
static const char *link_unk_evt = "Unknown link event ";

static const struct nla_policy tipc_nl_link_policy[TIPC_NLA_LINK_MAX + 1] = {
	[TIPC_NLA_LINK_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_LINK_NAME] = {
		.type = NLA_STRING,
		.len = TIPC_MAX_LINK_NAME
	},
	[TIPC_NLA_LINK_MTU]		= { .type = NLA_U32 },
	[TIPC_NLA_LINK_BROADCAST]	= { .type = NLA_FLAG },
	[TIPC_NLA_LINK_UP]		= { .type = NLA_FLAG },
	[TIPC_NLA_LINK_ACTIVE]		= { .type = NLA_FLAG },
	[TIPC_NLA_LINK_PROP]		= { .type = NLA_NESTED },
	[TIPC_NLA_LINK_STATS]		= { .type = NLA_NESTED },
	[TIPC_NLA_LINK_RX]		= { .type = NLA_U32 },
	[TIPC_NLA_LINK_TX]		= { .type = NLA_U32 }
};

/* Properties valid for media, bearar and link */
static const struct nla_policy tipc_nl_prop_policy[TIPC_NLA_PROP_MAX + 1] = {
	[TIPC_NLA_PROP_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_PROP_PRIO]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_TOL]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_WIN]		= { .type = NLA_U32 }
};

/*
 * Out-of-range value for link session numbers
 */
#define INVALID_SESSION 0x10000

/*
 * Link state events:
 */
#define  STARTING_EVT    856384768	/* link processing trigger */
#define  TRAFFIC_MSG_EVT 560815u	/* rx'd ??? */
#define  TIMEOUT_EVT     560817u	/* link timer expired */

/*
 * The following two 'message types' is really just implementation
 * data conveniently stored in the message header.
 * They must not be considered part of the protocol
 */
#define OPEN_MSG   0
#define CLOSED_MSG 1

/*
 * State value stored in 'exp_msg_count'
 */
#define START_CHANGEOVER 100000u

static void link_handle_out_of_seq_msg(struct tipc_link *link,
				       struct sk_buff *skb);
static void tipc_link_proto_rcv(struct tipc_link *link,
				struct sk_buff *skb);
static int  tipc_link_tunnel_rcv(struct tipc_node *node,
				 struct sk_buff **skb);
static void link_set_supervision_props(struct tipc_link *l_ptr, u32 tol);
static void link_state_event(struct tipc_link *l_ptr, u32 event);
static void link_reset_statistics(struct tipc_link *l_ptr);
static void link_print(struct tipc_link *l_ptr, const char *str);
static void tipc_link_sync_xmit(struct tipc_link *l);
static void tipc_link_sync_rcv(struct tipc_node *n, struct sk_buff *buf);
static void tipc_link_input(struct tipc_link *l, struct sk_buff *skb);
static bool tipc_data_input(struct tipc_link *l, struct sk_buff *skb);

/*
 *  Simple link routines
 */
static unsigned int align(unsigned int i)
{
	return (i + 3) & ~3u;
}

static void tipc_link_release(struct kref *kref)
{
	kfree(container_of(kref, struct tipc_link, ref));
}

static void tipc_link_get(struct tipc_link *l_ptr)
{
	kref_get(&l_ptr->ref);
}

static void tipc_link_put(struct tipc_link *l_ptr)
{
	kref_put(&l_ptr->ref, tipc_link_release);
}

static void link_init_max_pkt(struct tipc_link *l_ptr)
{
	struct tipc_node *node = l_ptr->owner;
	struct tipc_net *tn = net_generic(node->net, tipc_net_id);
	struct tipc_bearer *b_ptr;
	u32 max_pkt;

	rcu_read_lock();
	b_ptr = rcu_dereference_rtnl(tn->bearer_list[l_ptr->bearer_id]);
	if (!b_ptr) {
		rcu_read_unlock();
		return;
	}
	max_pkt = (b_ptr->mtu & ~3);
	rcu_read_unlock();

	if (max_pkt > MAX_MSG_SIZE)
		max_pkt = MAX_MSG_SIZE;

	l_ptr->max_pkt_target = max_pkt;
	if (l_ptr->max_pkt_target < MAX_PKT_DEFAULT)
		l_ptr->max_pkt = l_ptr->max_pkt_target;
	else
		l_ptr->max_pkt = MAX_PKT_DEFAULT;

	l_ptr->max_pkt_probes = 0;
}

/*
 *  Simple non-static link routines (i.e. referenced outside this file)
 */
int tipc_link_is_up(struct tipc_link *l_ptr)
{
	if (!l_ptr)
		return 0;
	return link_working_working(l_ptr) || link_working_unknown(l_ptr);
}

int tipc_link_is_active(struct tipc_link *l_ptr)
{
	return	(l_ptr->owner->active_links[0] == l_ptr) ||
		(l_ptr->owner->active_links[1] == l_ptr);
}

/**
 * link_timeout - handle expiration of link timer
 * @l_ptr: pointer to link
 */
static void link_timeout(unsigned long data)
{
	struct tipc_link *l_ptr = (struct tipc_link *)data;
	struct sk_buff *skb;

	tipc_node_lock(l_ptr->owner);

	/* update counters used in statistical profiling of send traffic */
	l_ptr->stats.accu_queue_sz += skb_queue_len(&l_ptr->transmq);
	l_ptr->stats.queue_sz_counts++;

	skb = skb_peek(&l_ptr->transmq);
	if (skb) {
		struct tipc_msg *msg = buf_msg(skb);
		u32 length = msg_size(msg);

		if ((msg_user(msg) == MSG_FRAGMENTER) &&
		    (msg_type(msg) == FIRST_FRAGMENT)) {
			length = msg_size(msg_get_wrapped(msg));
		}
		if (length) {
			l_ptr->stats.msg_lengths_total += length;
			l_ptr->stats.msg_length_counts++;
			if (length <= 64)
				l_ptr->stats.msg_length_profile[0]++;
			else if (length <= 256)
				l_ptr->stats.msg_length_profile[1]++;
			else if (length <= 1024)
				l_ptr->stats.msg_length_profile[2]++;
			else if (length <= 4096)
				l_ptr->stats.msg_length_profile[3]++;
			else if (length <= 16384)
				l_ptr->stats.msg_length_profile[4]++;
			else if (length <= 32768)
				l_ptr->stats.msg_length_profile[5]++;
			else
				l_ptr->stats.msg_length_profile[6]++;
		}
	}

	/* do all other link processing performed on a periodic basis */
	link_state_event(l_ptr, TIMEOUT_EVT);

	if (skb_queue_len(&l_ptr->backlogq))
		tipc_link_push_packets(l_ptr);

	tipc_node_unlock(l_ptr->owner);
	tipc_link_put(l_ptr);
}

static void link_set_timer(struct tipc_link *link, unsigned long time)
{
	if (!mod_timer(&link->timer, jiffies + time))
		tipc_link_get(link);
}

/**
 * tipc_link_create - create a new link
 * @n_ptr: pointer to associated node
 * @b_ptr: pointer to associated bearer
 * @media_addr: media address to use when sending messages over link
 *
 * Returns pointer to link.
 */
struct tipc_link *tipc_link_create(struct tipc_node *n_ptr,
				   struct tipc_bearer *b_ptr,
				   const struct tipc_media_addr *media_addr)
{
	struct tipc_net *tn = net_generic(n_ptr->net, tipc_net_id);
	struct tipc_link *l_ptr;
	struct tipc_msg *msg;
	char *if_name;
	char addr_string[16];
	u32 peer = n_ptr->addr;

	if (n_ptr->link_cnt >= MAX_BEARERS) {
		tipc_addr_string_fill(addr_string, n_ptr->addr);
		pr_err("Attempt to establish %uth link to %s. Max %u allowed.\n",
			n_ptr->link_cnt, addr_string, MAX_BEARERS);
		return NULL;
	}

	if (n_ptr->links[b_ptr->identity]) {
		tipc_addr_string_fill(addr_string, n_ptr->addr);
		pr_err("Attempt to establish second link on <%s> to %s\n",
		       b_ptr->name, addr_string);
		return NULL;
	}

	l_ptr = kzalloc(sizeof(*l_ptr), GFP_ATOMIC);
	if (!l_ptr) {
		pr_warn("Link creation failed, no memory\n");
		return NULL;
	}
	kref_init(&l_ptr->ref);
	l_ptr->addr = peer;
	if_name = strchr(b_ptr->name, ':') + 1;
	sprintf(l_ptr->name, "%u.%u.%u:%s-%u.%u.%u:unknown",
		tipc_zone(tn->own_addr), tipc_cluster(tn->own_addr),
		tipc_node(tn->own_addr),
		if_name,
		tipc_zone(peer), tipc_cluster(peer), tipc_node(peer));
		/* note: peer i/f name is updated by reset/activate message */
	memcpy(&l_ptr->media_addr, media_addr, sizeof(*media_addr));
	l_ptr->owner = n_ptr;
	l_ptr->checkpoint = 1;
	l_ptr->peer_session = INVALID_SESSION;
	l_ptr->bearer_id = b_ptr->identity;
	link_set_supervision_props(l_ptr, b_ptr->tolerance);
	l_ptr->state = RESET_UNKNOWN;

	l_ptr->pmsg = (struct tipc_msg *)&l_ptr->proto_msg;
	msg = l_ptr->pmsg;
	tipc_msg_init(tn->own_addr, msg, LINK_PROTOCOL, RESET_MSG, INT_H_SIZE,
		      l_ptr->addr);
	msg_set_size(msg, sizeof(l_ptr->proto_msg));
	msg_set_session(msg, (tn->random & 0xffff));
	msg_set_bearer_id(msg, b_ptr->identity);
	strcpy((char *)msg_data(msg), if_name);

	l_ptr->priority = b_ptr->priority;
	tipc_link_set_queue_limits(l_ptr, b_ptr->window);

	l_ptr->net_plane = b_ptr->net_plane;
	link_init_max_pkt(l_ptr);

	l_ptr->next_out_no = 1;
	__skb_queue_head_init(&l_ptr->transmq);
	__skb_queue_head_init(&l_ptr->backlogq);
	__skb_queue_head_init(&l_ptr->deferdq);
	skb_queue_head_init(&l_ptr->wakeupq);
	skb_queue_head_init(&l_ptr->inputq);
	skb_queue_head_init(&l_ptr->namedq);
	link_reset_statistics(l_ptr);
	tipc_node_attach_link(n_ptr, l_ptr);
	setup_timer(&l_ptr->timer, link_timeout, (unsigned long)l_ptr);
	link_state_event(l_ptr, STARTING_EVT);

	return l_ptr;
}

/**
 * link_delete - Conditional deletion of link.
 *               If timer still running, real delete is done when it expires
 * @link: link to be deleted
 */
void tipc_link_delete(struct tipc_link *link)
{
	tipc_link_reset_fragments(link);
	tipc_node_detach_link(link->owner, link);
	tipc_link_put(link);
}

void tipc_link_delete_list(struct net *net, unsigned int bearer_id,
			   bool shutting_down)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *link;
	struct tipc_node *node;
	bool del_link;

	rcu_read_lock();
	list_for_each_entry_rcu(node, &tn->node_list, list) {
		tipc_node_lock(node);
		link = node->links[bearer_id];
		if (!link) {
			tipc_node_unlock(node);
			continue;
		}
		del_link = !tipc_link_is_up(link) && !link->exp_msg_count;
		tipc_link_reset(link);
		if (del_timer(&link->timer))
			tipc_link_put(link);
		link->flags |= LINK_STOPPED;
		/* Delete link now, or when failover is finished: */
		if (shutting_down || !tipc_node_is_up(node) || del_link)
			tipc_link_delete(link);
		tipc_node_unlock(node);
	}
	rcu_read_unlock();
}

/**
 * link_schedule_user - schedule user for wakeup after congestion
 * @link: congested link
 * @oport: sending port
 * @chain_sz: size of buffer chain that was attempted sent
 * @imp: importance of message attempted sent
 * Create pseudo msg to send back to user when congestion abates
 */
static bool link_schedule_user(struct tipc_link *link, u32 oport,
			       uint chain_sz, uint imp)
{
	struct sk_buff *buf;

	buf = tipc_msg_create(SOCK_WAKEUP, 0, INT_H_SIZE, 0,
			      link_own_addr(link), link_own_addr(link),
			      oport, 0, 0);
	if (!buf)
		return false;
	TIPC_SKB_CB(buf)->chain_sz = chain_sz;
	TIPC_SKB_CB(buf)->chain_imp = imp;
	skb_queue_tail(&link->wakeupq, buf);
	link->stats.link_congs++;
	return true;
}

/**
 * link_prepare_wakeup - prepare users for wakeup after congestion
 * @link: congested link
 * Move a number of waiting users, as permitted by available space in
 * the send queue, from link wait queue to node wait queue for wakeup
 */
void link_prepare_wakeup(struct tipc_link *link)
{
	uint pend_qsz = skb_queue_len(&link->backlogq);
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&link->wakeupq, skb, tmp) {
		if (pend_qsz >= link->queue_limit[TIPC_SKB_CB(skb)->chain_imp])
			break;
		pend_qsz += TIPC_SKB_CB(skb)->chain_sz;
		skb_unlink(skb, &link->wakeupq);
		skb_queue_tail(&link->inputq, skb);
		link->owner->inputq = &link->inputq;
		link->owner->action_flags |= TIPC_MSG_EVT;
	}
}

/**
 * tipc_link_reset_fragments - purge link's inbound message fragments queue
 * @l_ptr: pointer to link
 */
void tipc_link_reset_fragments(struct tipc_link *l_ptr)
{
	kfree_skb(l_ptr->reasm_buf);
	l_ptr->reasm_buf = NULL;
}

/**
 * tipc_link_purge_queues - purge all pkt queues associated with link
 * @l_ptr: pointer to link
 */
void tipc_link_purge_queues(struct tipc_link *l_ptr)
{
	__skb_queue_purge(&l_ptr->deferdq);
	__skb_queue_purge(&l_ptr->transmq);
	__skb_queue_purge(&l_ptr->backlogq);
	tipc_link_reset_fragments(l_ptr);
}

void tipc_link_reset(struct tipc_link *l_ptr)
{
	u32 prev_state = l_ptr->state;
	u32 checkpoint = l_ptr->next_in_no;
	int was_active_link = tipc_link_is_active(l_ptr);
	struct tipc_node *owner = l_ptr->owner;

	msg_set_session(l_ptr->pmsg, ((msg_session(l_ptr->pmsg) + 1) & 0xffff));

	/* Link is down, accept any session */
	l_ptr->peer_session = INVALID_SESSION;

	/* Prepare for max packet size negotiation */
	link_init_max_pkt(l_ptr);

	l_ptr->state = RESET_UNKNOWN;

	if ((prev_state == RESET_UNKNOWN) || (prev_state == RESET_RESET))
		return;

	tipc_node_link_down(l_ptr->owner, l_ptr);
	tipc_bearer_remove_dest(owner->net, l_ptr->bearer_id, l_ptr->addr);

	if (was_active_link && tipc_node_active_links(l_ptr->owner)) {
		l_ptr->reset_checkpoint = checkpoint;
		l_ptr->exp_msg_count = START_CHANGEOVER;
	}

	/* Clean up all queues, except inputq: */
	__skb_queue_purge(&l_ptr->transmq);
	__skb_queue_purge(&l_ptr->backlogq);
	__skb_queue_purge(&l_ptr->deferdq);
	if (!owner->inputq)
		owner->inputq = &l_ptr->inputq;
	skb_queue_splice_init(&l_ptr->wakeupq, owner->inputq);
	if (!skb_queue_empty(owner->inputq))
		owner->action_flags |= TIPC_MSG_EVT;
	l_ptr->rcv_unacked = 0;
	l_ptr->checkpoint = 1;
	l_ptr->next_out_no = 1;
	l_ptr->fsm_msg_cnt = 0;
	l_ptr->stale_count = 0;
	link_reset_statistics(l_ptr);
}

void tipc_link_reset_list(struct net *net, unsigned int bearer_id)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *l_ptr;
	struct tipc_node *n_ptr;

	rcu_read_lock();
	list_for_each_entry_rcu(n_ptr, &tn->node_list, list) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->links[bearer_id];
		if (l_ptr)
			tipc_link_reset(l_ptr);
		tipc_node_unlock(n_ptr);
	}
	rcu_read_unlock();
}

static void link_activate(struct tipc_link *link)
{
	struct tipc_node *node = link->owner;

	link->next_in_no = 1;
	link->stats.recv_info = 1;
	tipc_node_link_up(node, link);
	tipc_bearer_add_dest(node->net, link->bearer_id, link->addr);
}

/**
 * link_state_event - link finite state machine
 * @l_ptr: pointer to link
 * @event: state machine event to process
 */
static void link_state_event(struct tipc_link *l_ptr, unsigned int event)
{
	struct tipc_link *other;
	unsigned long cont_intv = l_ptr->cont_intv;

	if (l_ptr->flags & LINK_STOPPED)
		return;

	if (!(l_ptr->flags & LINK_STARTED) && (event != STARTING_EVT))
		return;		/* Not yet. */

	/* Check whether changeover is going on */
	if (l_ptr->exp_msg_count) {
		if (event == TIMEOUT_EVT)
			link_set_timer(l_ptr, cont_intv);
		return;
	}

	switch (l_ptr->state) {
	case WORKING_WORKING:
		switch (event) {
		case TRAFFIC_MSG_EVT:
		case ACTIVATE_MSG:
			break;
		case TIMEOUT_EVT:
			if (l_ptr->next_in_no != l_ptr->checkpoint) {
				l_ptr->checkpoint = l_ptr->next_in_no;
				if (tipc_bclink_acks_missing(l_ptr->owner)) {
					tipc_link_proto_xmit(l_ptr, STATE_MSG,
							     0, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				} else if (l_ptr->max_pkt < l_ptr->max_pkt_target) {
					tipc_link_proto_xmit(l_ptr, STATE_MSG,
							     1, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				}
				link_set_timer(l_ptr, cont_intv);
				break;
			}
			l_ptr->state = WORKING_UNKNOWN;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_proto_xmit(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv / 4);
			break;
		case RESET_MSG:
			pr_debug("%s<%s>, requested by peer\n",
				 link_rst_msg, l_ptr->name);
			tipc_link_reset(l_ptr);
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_proto_xmit(l_ptr, ACTIVATE_MSG,
					     0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		default:
			pr_debug("%s%u in WW state\n", link_unk_evt, event);
		}
		break;
	case WORKING_UNKNOWN:
		switch (event) {
		case TRAFFIC_MSG_EVT:
		case ACTIVATE_MSG:
			l_ptr->state = WORKING_WORKING;
			l_ptr->fsm_msg_cnt = 0;
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			pr_debug("%s<%s>, requested by peer while probing\n",
				 link_rst_msg, l_ptr->name);
			tipc_link_reset(l_ptr);
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_proto_xmit(l_ptr, ACTIVATE_MSG,
					     0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case TIMEOUT_EVT:
			if (l_ptr->next_in_no != l_ptr->checkpoint) {
				l_ptr->state = WORKING_WORKING;
				l_ptr->fsm_msg_cnt = 0;
				l_ptr->checkpoint = l_ptr->next_in_no;
				if (tipc_bclink_acks_missing(l_ptr->owner)) {
					tipc_link_proto_xmit(l_ptr, STATE_MSG,
							     0, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				}
				link_set_timer(l_ptr, cont_intv);
			} else if (l_ptr->fsm_msg_cnt < l_ptr->abort_limit) {
				tipc_link_proto_xmit(l_ptr, STATE_MSG,
						     1, 0, 0, 0, 0);
				l_ptr->fsm_msg_cnt++;
				link_set_timer(l_ptr, cont_intv / 4);
			} else {	/* Link has failed */
				pr_debug("%s<%s>, peer not responding\n",
					 link_rst_msg, l_ptr->name);
				tipc_link_reset(l_ptr);
				l_ptr->state = RESET_UNKNOWN;
				l_ptr->fsm_msg_cnt = 0;
				tipc_link_proto_xmit(l_ptr, RESET_MSG,
						     0, 0, 0, 0, 0);
				l_ptr->fsm_msg_cnt++;
				link_set_timer(l_ptr, cont_intv);
			}
			break;
		default:
			pr_err("%s%u in WU state\n", link_unk_evt, event);
		}
		break;
	case RESET_UNKNOWN:
		switch (event) {
		case TRAFFIC_MSG_EVT:
			break;
		case ACTIVATE_MSG:
			other = l_ptr->owner->active_links[0];
			if (other && link_working_unknown(other))
				break;
			l_ptr->state = WORKING_WORKING;
			l_ptr->fsm_msg_cnt = 0;
			link_activate(l_ptr);
			tipc_link_proto_xmit(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			if (l_ptr->owner->working_links == 1)
				tipc_link_sync_xmit(l_ptr);
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_proto_xmit(l_ptr, ACTIVATE_MSG,
					     1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case STARTING_EVT:
			l_ptr->flags |= LINK_STARTED;
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case TIMEOUT_EVT:
			tipc_link_proto_xmit(l_ptr, RESET_MSG, 0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		default:
			pr_err("%s%u in RU state\n", link_unk_evt, event);
		}
		break;
	case RESET_RESET:
		switch (event) {
		case TRAFFIC_MSG_EVT:
		case ACTIVATE_MSG:
			other = l_ptr->owner->active_links[0];
			if (other && link_working_unknown(other))
				break;
			l_ptr->state = WORKING_WORKING;
			l_ptr->fsm_msg_cnt = 0;
			link_activate(l_ptr);
			tipc_link_proto_xmit(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			if (l_ptr->owner->working_links == 1)
				tipc_link_sync_xmit(l_ptr);
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			break;
		case TIMEOUT_EVT:
			tipc_link_proto_xmit(l_ptr, ACTIVATE_MSG,
					     0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		default:
			pr_err("%s%u in RR state\n", link_unk_evt, event);
		}
		break;
	default:
		pr_err("Unknown link state %u/%u\n", l_ptr->state, event);
	}
}

/* tipc_link_cong: determine return value and how to treat the
 * sent buffer during link congestion.
 * - For plain, errorless user data messages we keep the buffer and
 *   return -ELINKONG.
 * - For all other messages we discard the buffer and return -EHOSTUNREACH
 * - For TIPC internal messages we also reset the link
 */
static int tipc_link_cong(struct tipc_link *link, struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek(list);
	struct tipc_msg *msg = buf_msg(skb);
	uint imp = tipc_msg_tot_importance(msg);
	u32 oport = msg_tot_origport(msg);

	if (unlikely(imp > TIPC_CRITICAL_IMPORTANCE)) {
		pr_warn("%s<%s>, send queue full", link_rst_msg, link->name);
		tipc_link_reset(link);
		goto drop;
	}
	if (unlikely(msg_errcode(msg)))
		goto drop;
	if (unlikely(msg_reroute_cnt(msg)))
		goto drop;
	if (TIPC_SKB_CB(skb)->wakeup_pending)
		return -ELINKCONG;
	if (link_schedule_user(link, oport, skb_queue_len(list), imp))
		return -ELINKCONG;
drop:
	__skb_queue_purge(list);
	return -EHOSTUNREACH;
}

/**
 * __tipc_link_xmit(): same as tipc_link_xmit, but destlink is known & locked
 * @link: link to use
 * @list: chain of buffers containing message
 *
 * Consumes the buffer chain, except when returning -ELINKCONG
 * Returns 0 if success, otherwise errno: -ELINKCONG, -EMSGSIZE (plain socket
 * user data messages) or -EHOSTUNREACH (all other messages/senders)
 * Only the socket functions tipc_send_stream() and tipc_send_packet() need
 * to act on the return value, since they may need to do more send attempts.
 */
int __tipc_link_xmit(struct net *net, struct tipc_link *link,
		     struct sk_buff_head *list)
{
	struct tipc_msg *msg = buf_msg(skb_peek(list));
	unsigned int maxwin = link->window;
	uint imp = tipc_msg_tot_importance(msg);
	uint mtu = link->max_pkt;
	uint ack = mod(link->next_in_no - 1);
	uint seqno = link->next_out_no;
	uint bc_last_in = link->owner->bclink.last_in;
	struct tipc_media_addr *addr = &link->media_addr;
	struct sk_buff_head *transmq = &link->transmq;
	struct sk_buff_head *backlogq = &link->backlogq;
	struct sk_buff *skb, *tmp;

	/* Match queue limits against msg importance: */
	if (unlikely(skb_queue_len(backlogq) >= link->queue_limit[imp]))
		return tipc_link_cong(link, list);

	/* Has valid packet limit been used ? */
	if (unlikely(msg_size(msg) > mtu)) {
		__skb_queue_purge(list);
		return -EMSGSIZE;
	}

	/* Prepare each packet for sending, and add to relevant queue: */
	skb_queue_walk_safe(list, skb, tmp) {
		__skb_unlink(skb, list);
		msg = buf_msg(skb);
		msg_set_seqno(msg, seqno);
		msg_set_ack(msg, ack);
		msg_set_bcast_ack(msg, bc_last_in);

		if (likely(skb_queue_len(transmq) < maxwin)) {
			__skb_queue_tail(transmq, skb);
			tipc_bearer_send(net, link->bearer_id, skb, addr);
			link->rcv_unacked = 0;
			seqno++;
			continue;
		}
		if (tipc_msg_bundle(skb_peek_tail(backlogq), skb, mtu)) {
			link->stats.sent_bundled++;
			continue;
		}
		if (tipc_msg_make_bundle(&skb, mtu, link->addr)) {
			link->stats.sent_bundled++;
			link->stats.sent_bundles++;
		}
		__skb_queue_tail(backlogq, skb);
		seqno++;
	}
	link->next_out_no = seqno;
	return 0;
}

static void skb2list(struct sk_buff *skb, struct sk_buff_head *list)
{
	skb_queue_head_init(list);
	__skb_queue_tail(list, skb);
}

static int __tipc_link_xmit_skb(struct tipc_link *link, struct sk_buff *skb)
{
	struct sk_buff_head head;

	skb2list(skb, &head);
	return __tipc_link_xmit(link->owner->net, link, &head);
}

int tipc_link_xmit_skb(struct net *net, struct sk_buff *skb, u32 dnode,
		       u32 selector)
{
	struct sk_buff_head head;

	skb2list(skb, &head);
	return tipc_link_xmit(net, &head, dnode, selector);
}

/**
 * tipc_link_xmit() is the general link level function for message sending
 * @net: the applicable net namespace
 * @list: chain of buffers containing message
 * @dsz: amount of user data to be sent
 * @dnode: address of destination node
 * @selector: a number used for deterministic link selection
 * Consumes the buffer chain, except when returning -ELINKCONG
 * Returns 0 if success, otherwise errno: -ELINKCONG,-EHOSTUNREACH,-EMSGSIZE
 */
int tipc_link_xmit(struct net *net, struct sk_buff_head *list, u32 dnode,
		   u32 selector)
{
	struct tipc_link *link = NULL;
	struct tipc_node *node;
	int rc = -EHOSTUNREACH;

	node = tipc_node_find(net, dnode);
	if (node) {
		tipc_node_lock(node);
		link = node->active_links[selector & 1];
		if (link)
			rc = __tipc_link_xmit(net, link, list);
		tipc_node_unlock(node);
	}
	if (link)
		return rc;

	if (likely(in_own_node(net, dnode)))
		return tipc_sk_rcv(net, list);

	__skb_queue_purge(list);
	return rc;
}

/*
 * tipc_link_sync_xmit - synchronize broadcast link endpoints.
 *
 * Give a newly added peer node the sequence number where it should
 * start receiving and acking broadcast packets.
 *
 * Called with node locked
 */
static void tipc_link_sync_xmit(struct tipc_link *link)
{
	struct sk_buff *skb;
	struct tipc_msg *msg;

	skb = tipc_buf_acquire(INT_H_SIZE);
	if (!skb)
		return;

	msg = buf_msg(skb);
	tipc_msg_init(link_own_addr(link), msg, BCAST_PROTOCOL, STATE_MSG,
		      INT_H_SIZE, link->addr);
	msg_set_last_bcast(msg, link->owner->bclink.acked);
	__tipc_link_xmit_skb(link, skb);
}

/*
 * tipc_link_sync_rcv - synchronize broadcast link endpoints.
 * Receive the sequence number where we should start receiving and
 * acking broadcast packets from a newly added peer node, and open
 * up for reception of such packets.
 *
 * Called with node locked
 */
static void tipc_link_sync_rcv(struct tipc_node *n, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);

	n->bclink.last_sent = n->bclink.last_in = msg_last_bcast(msg);
	n->bclink.recv_permitted = true;
	kfree_skb(buf);
}

/*
 * tipc_link_push_packets - push unsent packets to bearer
 *
 * Push out the unsent messages of a link where congestion
 * has abated. Node is locked.
 *
 * Called with node locked
 */
void tipc_link_push_packets(struct tipc_link *link)
{
	struct sk_buff *skb;
	struct tipc_msg *msg;
	unsigned int ack = mod(link->next_in_no - 1);

	while (skb_queue_len(&link->transmq) < link->window) {
		skb = __skb_dequeue(&link->backlogq);
		if (!skb)
			break;
		msg = buf_msg(skb);
		msg_set_ack(msg, ack);
		msg_set_bcast_ack(msg, link->owner->bclink.last_in);
		link->rcv_unacked = 0;
		__skb_queue_tail(&link->transmq, skb);
		tipc_bearer_send(link->owner->net, link->bearer_id,
				 skb, &link->media_addr);
	}
}

void tipc_link_reset_all(struct tipc_node *node)
{
	char addr_string[16];
	u32 i;

	tipc_node_lock(node);

	pr_warn("Resetting all links to %s\n",
		tipc_addr_string_fill(addr_string, node->addr));

	for (i = 0; i < MAX_BEARERS; i++) {
		if (node->links[i]) {
			link_print(node->links[i], "Resetting link\n");
			tipc_link_reset(node->links[i]);
		}
	}

	tipc_node_unlock(node);
}

static void link_retransmit_failure(struct tipc_link *l_ptr,
				    struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	struct net *net = l_ptr->owner->net;

	pr_warn("Retransmission failure on link <%s>\n", l_ptr->name);

	if (l_ptr->addr) {
		/* Handle failure on standard link */
		link_print(l_ptr, "Resetting link\n");
		tipc_link_reset(l_ptr);

	} else {
		/* Handle failure on broadcast link */
		struct tipc_node *n_ptr;
		char addr_string[16];

		pr_info("Msg seq number: %u,  ", msg_seqno(msg));
		pr_cont("Outstanding acks: %lu\n",
			(unsigned long) TIPC_SKB_CB(buf)->handle);

		n_ptr = tipc_bclink_retransmit_to(net);
		tipc_node_lock(n_ptr);

		tipc_addr_string_fill(addr_string, n_ptr->addr);
		pr_info("Broadcast link info for %s\n", addr_string);
		pr_info("Reception permitted: %d,  Acked: %u\n",
			n_ptr->bclink.recv_permitted,
			n_ptr->bclink.acked);
		pr_info("Last in: %u,  Oos state: %u,  Last sent: %u\n",
			n_ptr->bclink.last_in,
			n_ptr->bclink.oos_state,
			n_ptr->bclink.last_sent);

		tipc_node_unlock(n_ptr);

		tipc_bclink_set_flags(net, TIPC_BCLINK_RESET);
		l_ptr->stale_count = 0;
	}
}

void tipc_link_retransmit(struct tipc_link *l_ptr, struct sk_buff *skb,
			  u32 retransmits)
{
	struct tipc_msg *msg;

	if (!skb)
		return;

	msg = buf_msg(skb);

	/* Detect repeated retransmit failures */
	if (l_ptr->last_retransmitted == msg_seqno(msg)) {
		if (++l_ptr->stale_count > 100) {
			link_retransmit_failure(l_ptr, skb);
			return;
		}
	} else {
		l_ptr->last_retransmitted = msg_seqno(msg);
		l_ptr->stale_count = 1;
	}

	skb_queue_walk_from(&l_ptr->transmq, skb) {
		if (!retransmits)
			break;
		msg = buf_msg(skb);
		msg_set_ack(msg, mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
		tipc_bearer_send(l_ptr->owner->net, l_ptr->bearer_id, skb,
				 &l_ptr->media_addr);
		retransmits--;
		l_ptr->stats.retransmitted++;
	}
}

static void link_retrieve_defq(struct tipc_link *link,
			       struct sk_buff_head *list)
{
	u32 seq_no;

	if (skb_queue_empty(&link->deferdq))
		return;

	seq_no = buf_seqno(skb_peek(&link->deferdq));
	if (seq_no == mod(link->next_in_no))
		skb_queue_splice_tail_init(&link->deferdq, list);
}

/**
 * tipc_rcv - process TIPC packets/messages arriving from off-node
 * @net: the applicable net namespace
 * @skb: TIPC packet
 * @b_ptr: pointer to bearer message arrived on
 *
 * Invoked with no locks held.  Bearer pointer must point to a valid bearer
 * structure (i.e. cannot be NULL), but bearer can be inactive.
 */
void tipc_rcv(struct net *net, struct sk_buff *skb, struct tipc_bearer *b_ptr)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct sk_buff_head head;
	struct tipc_node *n_ptr;
	struct tipc_link *l_ptr;
	struct sk_buff *skb1, *tmp;
	struct tipc_msg *msg;
	u32 seq_no;
	u32 ackd;
	u32 released;

	skb2list(skb, &head);

	while ((skb = __skb_dequeue(&head))) {
		/* Ensure message is well-formed */
		if (unlikely(!tipc_msg_validate(skb)))
			goto discard;

		/* Handle arrival of a non-unicast link message */
		msg = buf_msg(skb);
		if (unlikely(msg_non_seq(msg))) {
			if (msg_user(msg) ==  LINK_CONFIG)
				tipc_disc_rcv(net, skb, b_ptr);
			else
				tipc_bclink_rcv(net, skb);
			continue;
		}

		/* Discard unicast link messages destined for another node */
		if (unlikely(!msg_short(msg) &&
			     (msg_destnode(msg) != tn->own_addr)))
			goto discard;

		/* Locate neighboring node that sent message */
		n_ptr = tipc_node_find(net, msg_prevnode(msg));
		if (unlikely(!n_ptr))
			goto discard;
		tipc_node_lock(n_ptr);

		/* Locate unicast link endpoint that should handle message */
		l_ptr = n_ptr->links[b_ptr->identity];
		if (unlikely(!l_ptr))
			goto unlock;

		/* Verify that communication with node is currently allowed */
		if ((n_ptr->action_flags & TIPC_WAIT_PEER_LINKS_DOWN) &&
		    msg_user(msg) == LINK_PROTOCOL &&
		    (msg_type(msg) == RESET_MSG ||
		    msg_type(msg) == ACTIVATE_MSG) &&
		    !msg_redundant_link(msg))
			n_ptr->action_flags &= ~TIPC_WAIT_PEER_LINKS_DOWN;

		if (tipc_node_blocked(n_ptr))
			goto unlock;

		/* Validate message sequence number info */
		seq_no = msg_seqno(msg);
		ackd = msg_ack(msg);

		/* Release acked messages */
		if (unlikely(n_ptr->bclink.acked != msg_bcast_ack(msg)))
			tipc_bclink_acknowledge(n_ptr, msg_bcast_ack(msg));

		released = 0;
		skb_queue_walk_safe(&l_ptr->transmq, skb1, tmp) {
			if (more(buf_seqno(skb1), ackd))
				break;
			 __skb_unlink(skb1, &l_ptr->transmq);
			 kfree_skb(skb1);
			 released = 1;
		}

		/* Try sending any messages link endpoint has pending */
		if (unlikely(skb_queue_len(&l_ptr->backlogq)))
			tipc_link_push_packets(l_ptr);

		if (released && !skb_queue_empty(&l_ptr->wakeupq))
			link_prepare_wakeup(l_ptr);

		/* Process the incoming packet */
		if (unlikely(!link_working_working(l_ptr))) {
			if (msg_user(msg) == LINK_PROTOCOL) {
				tipc_link_proto_rcv(l_ptr, skb);
				link_retrieve_defq(l_ptr, &head);
				skb = NULL;
				goto unlock;
			}

			/* Traffic message. Conditionally activate link */
			link_state_event(l_ptr, TRAFFIC_MSG_EVT);

			if (link_working_working(l_ptr)) {
				/* Re-insert buffer in front of queue */
				__skb_queue_head(&head, skb);
				skb = NULL;
				goto unlock;
			}
			goto unlock;
		}

		/* Link is now in state WORKING_WORKING */
		if (unlikely(seq_no != mod(l_ptr->next_in_no))) {
			link_handle_out_of_seq_msg(l_ptr, skb);
			link_retrieve_defq(l_ptr, &head);
			skb = NULL;
			goto unlock;
		}
		l_ptr->next_in_no++;
		if (unlikely(!skb_queue_empty(&l_ptr->deferdq)))
			link_retrieve_defq(l_ptr, &head);
		if (unlikely(++l_ptr->rcv_unacked >= TIPC_MIN_LINK_WIN)) {
			l_ptr->stats.sent_acks++;
			tipc_link_proto_xmit(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
		}
		tipc_link_input(l_ptr, skb);
		skb = NULL;
unlock:
		tipc_node_unlock(n_ptr);
discard:
		if (unlikely(skb))
			kfree_skb(skb);
	}
}

/* tipc_data_input - deliver data and name distr msgs to upper layer
 *
 * Consumes buffer if message is of right type
 * Node lock must be held
 */
static bool tipc_data_input(struct tipc_link *link, struct sk_buff *skb)
{
	struct tipc_node *node = link->owner;
	struct tipc_msg *msg = buf_msg(skb);
	u32 dport = msg_destport(msg);

	switch (msg_user(msg)) {
	case TIPC_LOW_IMPORTANCE:
	case TIPC_MEDIUM_IMPORTANCE:
	case TIPC_HIGH_IMPORTANCE:
	case TIPC_CRITICAL_IMPORTANCE:
	case CONN_MANAGER:
		if (tipc_skb_queue_tail(&link->inputq, skb, dport)) {
			node->inputq = &link->inputq;
			node->action_flags |= TIPC_MSG_EVT;
		}
		return true;
	case NAME_DISTRIBUTOR:
		node->bclink.recv_permitted = true;
		node->namedq = &link->namedq;
		skb_queue_tail(&link->namedq, skb);
		if (skb_queue_len(&link->namedq) == 1)
			node->action_flags |= TIPC_NAMED_MSG_EVT;
		return true;
	case MSG_BUNDLER:
	case CHANGEOVER_PROTOCOL:
	case MSG_FRAGMENTER:
	case BCAST_PROTOCOL:
		return false;
	default:
		pr_warn("Dropping received illegal msg type\n");
		kfree_skb(skb);
		return false;
	};
}

/* tipc_link_input - process packet that has passed link protocol check
 *
 * Consumes buffer
 * Node lock must be held
 */
static void tipc_link_input(struct tipc_link *link, struct sk_buff *skb)
{
	struct tipc_node *node = link->owner;
	struct tipc_msg *msg = buf_msg(skb);
	struct sk_buff *iskb;
	int pos = 0;

	if (likely(tipc_data_input(link, skb)))
		return;

	switch (msg_user(msg)) {
	case CHANGEOVER_PROTOCOL:
		if (!tipc_link_tunnel_rcv(node, &skb))
			break;
		if (msg_user(buf_msg(skb)) != MSG_BUNDLER) {
			tipc_data_input(link, skb);
			break;
		}
	case MSG_BUNDLER:
		link->stats.recv_bundles++;
		link->stats.recv_bundled += msg_msgcnt(msg);

		while (tipc_msg_extract(skb, &iskb, &pos))
			tipc_data_input(link, iskb);
		break;
	case MSG_FRAGMENTER:
		link->stats.recv_fragments++;
		if (tipc_buf_append(&link->reasm_buf, &skb)) {
			link->stats.recv_fragmented++;
			tipc_data_input(link, skb);
		} else if (!link->reasm_buf) {
			tipc_link_reset(link);
		}
		break;
	case BCAST_PROTOCOL:
		tipc_link_sync_rcv(node, skb);
		break;
	default:
		break;
	};
}

/**
 * tipc_link_defer_pkt - Add out-of-sequence message to deferred reception queue
 *
 * Returns increase in queue length (i.e. 0 or 1)
 */
u32 tipc_link_defer_pkt(struct sk_buff_head *list, struct sk_buff *skb)
{
	struct sk_buff *skb1;
	u32 seq_no = buf_seqno(skb);

	/* Empty queue ? */
	if (skb_queue_empty(list)) {
		__skb_queue_tail(list, skb);
		return 1;
	}

	/* Last ? */
	if (less(buf_seqno(skb_peek_tail(list)), seq_no)) {
		__skb_queue_tail(list, skb);
		return 1;
	}

	/* Locate insertion point in queue, then insert; discard if duplicate */
	skb_queue_walk(list, skb1) {
		u32 curr_seqno = buf_seqno(skb1);

		if (seq_no == curr_seqno) {
			kfree_skb(skb);
			return 0;
		}

		if (less(seq_no, curr_seqno))
			break;
	}

	__skb_queue_before(list, skb1, skb);
	return 1;
}

/*
 * link_handle_out_of_seq_msg - handle arrival of out-of-sequence packet
 */
static void link_handle_out_of_seq_msg(struct tipc_link *l_ptr,
				       struct sk_buff *buf)
{
	u32 seq_no = buf_seqno(buf);

	if (likely(msg_user(buf_msg(buf)) == LINK_PROTOCOL)) {
		tipc_link_proto_rcv(l_ptr, buf);
		return;
	}

	/* Record OOS packet arrival (force mismatch on next timeout) */
	l_ptr->checkpoint--;

	/*
	 * Discard packet if a duplicate; otherwise add it to deferred queue
	 * and notify peer of gap as per protocol specification
	 */
	if (less(seq_no, mod(l_ptr->next_in_no))) {
		l_ptr->stats.duplicates++;
		kfree_skb(buf);
		return;
	}

	if (tipc_link_defer_pkt(&l_ptr->deferdq, buf)) {
		l_ptr->stats.deferred_recv++;
		if ((skb_queue_len(&l_ptr->deferdq) % TIPC_MIN_LINK_WIN) == 1)
			tipc_link_proto_xmit(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
	} else {
		l_ptr->stats.duplicates++;
	}
}

/*
 * Send protocol message to the other endpoint.
 */
void tipc_link_proto_xmit(struct tipc_link *l_ptr, u32 msg_typ, int probe_msg,
			  u32 gap, u32 tolerance, u32 priority, u32 ack_mtu)
{
	struct sk_buff *buf = NULL;
	struct tipc_msg *msg = l_ptr->pmsg;
	u32 msg_size = sizeof(l_ptr->proto_msg);
	int r_flag;

	/* Don't send protocol message during link changeover */
	if (l_ptr->exp_msg_count)
		return;

	/* Abort non-RESET send if communication with node is prohibited */
	if ((tipc_node_blocked(l_ptr->owner)) && (msg_typ != RESET_MSG))
		return;

	/* Create protocol message with "out-of-sequence" sequence number */
	msg_set_type(msg, msg_typ);
	msg_set_net_plane(msg, l_ptr->net_plane);
	msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
	msg_set_last_bcast(msg, tipc_bclink_get_last_sent(l_ptr->owner->net));

	if (msg_typ == STATE_MSG) {
		u32 next_sent = mod(l_ptr->next_out_no);

		if (!tipc_link_is_up(l_ptr))
			return;
		if (skb_queue_len(&l_ptr->backlogq))
			next_sent = buf_seqno(skb_peek(&l_ptr->backlogq));
		msg_set_next_sent(msg, next_sent);
		if (!skb_queue_empty(&l_ptr->deferdq)) {
			u32 rec = buf_seqno(skb_peek(&l_ptr->deferdq));
			gap = mod(rec - mod(l_ptr->next_in_no));
		}
		msg_set_seq_gap(msg, gap);
		if (gap)
			l_ptr->stats.sent_nacks++;
		msg_set_link_tolerance(msg, tolerance);
		msg_set_linkprio(msg, priority);
		msg_set_max_pkt(msg, ack_mtu);
		msg_set_ack(msg, mod(l_ptr->next_in_no - 1));
		msg_set_probe(msg, probe_msg != 0);
		if (probe_msg) {
			u32 mtu = l_ptr->max_pkt;

			if ((mtu < l_ptr->max_pkt_target) &&
			    link_working_working(l_ptr) &&
			    l_ptr->fsm_msg_cnt) {
				msg_size = (mtu + (l_ptr->max_pkt_target - mtu)/2 + 2) & ~3;
				if (l_ptr->max_pkt_probes == 10) {
					l_ptr->max_pkt_target = (msg_size - 4);
					l_ptr->max_pkt_probes = 0;
					msg_size = (mtu + (l_ptr->max_pkt_target - mtu)/2 + 2) & ~3;
				}
				l_ptr->max_pkt_probes++;
			}

			l_ptr->stats.sent_probes++;
		}
		l_ptr->stats.sent_states++;
	} else {		/* RESET_MSG or ACTIVATE_MSG */
		msg_set_ack(msg, mod(l_ptr->reset_checkpoint - 1));
		msg_set_seq_gap(msg, 0);
		msg_set_next_sent(msg, 1);
		msg_set_probe(msg, 0);
		msg_set_link_tolerance(msg, l_ptr->tolerance);
		msg_set_linkprio(msg, l_ptr->priority);
		msg_set_max_pkt(msg, l_ptr->max_pkt_target);
	}

	r_flag = (l_ptr->owner->working_links > tipc_link_is_up(l_ptr));
	msg_set_redundant_link(msg, r_flag);
	msg_set_linkprio(msg, l_ptr->priority);
	msg_set_size(msg, msg_size);

	msg_set_seqno(msg, mod(l_ptr->next_out_no + (0xffff/2)));

	buf = tipc_buf_acquire(msg_size);
	if (!buf)
		return;

	skb_copy_to_linear_data(buf, msg, sizeof(l_ptr->proto_msg));
	buf->priority = TC_PRIO_CONTROL;
	tipc_bearer_send(l_ptr->owner->net, l_ptr->bearer_id, buf,
			 &l_ptr->media_addr);
	l_ptr->rcv_unacked = 0;
	kfree_skb(buf);
}

/*
 * Receive protocol message :
 * Note that network plane id propagates through the network, and may
 * change at any time. The node with lowest address rules
 */
static void tipc_link_proto_rcv(struct tipc_link *l_ptr,
				struct sk_buff *buf)
{
	u32 rec_gap = 0;
	u32 max_pkt_info;
	u32 max_pkt_ack;
	u32 msg_tol;
	struct tipc_msg *msg = buf_msg(buf);

	/* Discard protocol message during link changeover */
	if (l_ptr->exp_msg_count)
		goto exit;

	if (l_ptr->net_plane != msg_net_plane(msg))
		if (link_own_addr(l_ptr) > msg_prevnode(msg))
			l_ptr->net_plane = msg_net_plane(msg);

	switch (msg_type(msg)) {

	case RESET_MSG:
		if (!link_working_unknown(l_ptr) &&
		    (l_ptr->peer_session != INVALID_SESSION)) {
			if (less_eq(msg_session(msg), l_ptr->peer_session))
				break; /* duplicate or old reset: ignore */
		}

		if (!msg_redundant_link(msg) && (link_working_working(l_ptr) ||
				link_working_unknown(l_ptr))) {
			/*
			 * peer has lost contact -- don't allow peer's links
			 * to reactivate before we recognize loss & clean up
			 */
			l_ptr->owner->action_flags |= TIPC_WAIT_OWN_LINKS_DOWN;
		}

		link_state_event(l_ptr, RESET_MSG);

		/* fall thru' */
	case ACTIVATE_MSG:
		/* Update link settings according other endpoint's values */
		strcpy((strrchr(l_ptr->name, ':') + 1), (char *)msg_data(msg));

		msg_tol = msg_link_tolerance(msg);
		if (msg_tol > l_ptr->tolerance)
			link_set_supervision_props(l_ptr, msg_tol);

		if (msg_linkprio(msg) > l_ptr->priority)
			l_ptr->priority = msg_linkprio(msg);

		max_pkt_info = msg_max_pkt(msg);
		if (max_pkt_info) {
			if (max_pkt_info < l_ptr->max_pkt_target)
				l_ptr->max_pkt_target = max_pkt_info;
			if (l_ptr->max_pkt > l_ptr->max_pkt_target)
				l_ptr->max_pkt = l_ptr->max_pkt_target;
		} else {
			l_ptr->max_pkt = l_ptr->max_pkt_target;
		}

		/* Synchronize broadcast link info, if not done previously */
		if (!tipc_node_is_up(l_ptr->owner)) {
			l_ptr->owner->bclink.last_sent =
				l_ptr->owner->bclink.last_in =
				msg_last_bcast(msg);
			l_ptr->owner->bclink.oos_state = 0;
		}

		l_ptr->peer_session = msg_session(msg);
		l_ptr->peer_bearer_id = msg_bearer_id(msg);

		if (msg_type(msg) == ACTIVATE_MSG)
			link_state_event(l_ptr, ACTIVATE_MSG);
		break;
	case STATE_MSG:

		msg_tol = msg_link_tolerance(msg);
		if (msg_tol)
			link_set_supervision_props(l_ptr, msg_tol);

		if (msg_linkprio(msg) &&
		    (msg_linkprio(msg) != l_ptr->priority)) {
			pr_debug("%s<%s>, priority change %u->%u\n",
				 link_rst_msg, l_ptr->name,
				 l_ptr->priority, msg_linkprio(msg));
			l_ptr->priority = msg_linkprio(msg);
			tipc_link_reset(l_ptr); /* Enforce change to take effect */
			break;
		}

		/* Record reception; force mismatch at next timeout: */
		l_ptr->checkpoint--;

		link_state_event(l_ptr, TRAFFIC_MSG_EVT);
		l_ptr->stats.recv_states++;
		if (link_reset_unknown(l_ptr))
			break;

		if (less_eq(mod(l_ptr->next_in_no), msg_next_sent(msg))) {
			rec_gap = mod(msg_next_sent(msg) -
				      mod(l_ptr->next_in_no));
		}

		max_pkt_ack = msg_max_pkt(msg);
		if (max_pkt_ack > l_ptr->max_pkt) {
			l_ptr->max_pkt = max_pkt_ack;
			l_ptr->max_pkt_probes = 0;
		}

		max_pkt_ack = 0;
		if (msg_probe(msg)) {
			l_ptr->stats.recv_probes++;
			if (msg_size(msg) > sizeof(l_ptr->proto_msg))
				max_pkt_ack = msg_size(msg);
		}

		/* Protocol message before retransmits, reduce loss risk */
		if (l_ptr->owner->bclink.recv_permitted)
			tipc_bclink_update_link_state(l_ptr->owner,
						      msg_last_bcast(msg));

		if (rec_gap || (msg_probe(msg))) {
			tipc_link_proto_xmit(l_ptr, STATE_MSG, 0, rec_gap, 0,
					     0, max_pkt_ack);
		}
		if (msg_seq_gap(msg)) {
			l_ptr->stats.recv_nacks++;
			tipc_link_retransmit(l_ptr, skb_peek(&l_ptr->transmq),
					     msg_seq_gap(msg));
		}
		break;
	}
exit:
	kfree_skb(buf);
}


/* tipc_link_tunnel_xmit(): Tunnel one packet via a link belonging to
 * a different bearer. Owner node is locked.
 */
static void tipc_link_tunnel_xmit(struct tipc_link *l_ptr,
				  struct tipc_msg *tunnel_hdr,
				  struct tipc_msg *msg,
				  u32 selector)
{
	struct tipc_link *tunnel;
	struct sk_buff *skb;
	u32 length = msg_size(msg);

	tunnel = l_ptr->owner->active_links[selector & 1];
	if (!tipc_link_is_up(tunnel)) {
		pr_warn("%stunnel link no longer available\n", link_co_err);
		return;
	}
	msg_set_size(tunnel_hdr, length + INT_H_SIZE);
	skb = tipc_buf_acquire(length + INT_H_SIZE);
	if (!skb) {
		pr_warn("%sunable to send tunnel msg\n", link_co_err);
		return;
	}
	skb_copy_to_linear_data(skb, tunnel_hdr, INT_H_SIZE);
	skb_copy_to_linear_data_offset(skb, INT_H_SIZE, msg, length);
	__tipc_link_xmit_skb(tunnel, skb);
}


/* tipc_link_failover_send_queue(): A link has gone down, but a second
 * link is still active. We can do failover. Tunnel the failing link's
 * whole send queue via the remaining link. This way, we don't lose
 * any packets, and sequence order is preserved for subsequent traffic
 * sent over the remaining link. Owner node is locked.
 */
void tipc_link_failover_send_queue(struct tipc_link *l_ptr)
{
	int msgcount;
	struct tipc_link *tunnel = l_ptr->owner->active_links[0];
	struct tipc_msg tunnel_hdr;
	struct sk_buff *skb;
	int split_bundles;

	if (!tunnel)
		return;

	tipc_msg_init(link_own_addr(l_ptr), &tunnel_hdr, CHANGEOVER_PROTOCOL,
		      ORIGINAL_MSG, INT_H_SIZE, l_ptr->addr);
	skb_queue_splice_tail_init(&l_ptr->backlogq, &l_ptr->transmq);
	msgcount = skb_queue_len(&l_ptr->transmq);
	msg_set_bearer_id(&tunnel_hdr, l_ptr->peer_bearer_id);
	msg_set_msgcnt(&tunnel_hdr, msgcount);

	if (skb_queue_empty(&l_ptr->transmq)) {
		skb = tipc_buf_acquire(INT_H_SIZE);
		if (skb) {
			skb_copy_to_linear_data(skb, &tunnel_hdr, INT_H_SIZE);
			msg_set_size(&tunnel_hdr, INT_H_SIZE);
			__tipc_link_xmit_skb(tunnel, skb);
		} else {
			pr_warn("%sunable to send changeover msg\n",
				link_co_err);
		}
		return;
	}

	split_bundles = (l_ptr->owner->active_links[0] !=
			 l_ptr->owner->active_links[1]);

	skb_queue_walk(&l_ptr->transmq, skb) {
		struct tipc_msg *msg = buf_msg(skb);

		if ((msg_user(msg) == MSG_BUNDLER) && split_bundles) {
			struct tipc_msg *m = msg_get_wrapped(msg);
			unchar *pos = (unchar *)m;

			msgcount = msg_msgcnt(msg);
			while (msgcount--) {
				msg_set_seqno(m, msg_seqno(msg));
				tipc_link_tunnel_xmit(l_ptr, &tunnel_hdr, m,
						      msg_link_selector(m));
				pos += align(msg_size(m));
				m = (struct tipc_msg *)pos;
			}
		} else {
			tipc_link_tunnel_xmit(l_ptr, &tunnel_hdr, msg,
					      msg_link_selector(msg));
		}
	}
}

/* tipc_link_dup_queue_xmit(): A second link has become active. Tunnel a
 * duplicate of the first link's send queue via the new link. This way, we
 * are guaranteed that currently queued packets from a socket are delivered
 * before future traffic from the same socket, even if this is using the
 * new link. The last arriving copy of each duplicate packet is dropped at
 * the receiving end by the regular protocol check, so packet cardinality
 * and sequence order is preserved per sender/receiver socket pair.
 * Owner node is locked.
 */
void tipc_link_dup_queue_xmit(struct tipc_link *link,
			      struct tipc_link *tnl)
{
	struct sk_buff *skb;
	struct tipc_msg tnl_hdr;
	struct sk_buff_head *queue = &link->transmq;
	int mcnt;

	tipc_msg_init(link_own_addr(link), &tnl_hdr, CHANGEOVER_PROTOCOL,
		      DUPLICATE_MSG, INT_H_SIZE, link->addr);
	mcnt = skb_queue_len(&link->transmq) + skb_queue_len(&link->backlogq);
	msg_set_msgcnt(&tnl_hdr, mcnt);
	msg_set_bearer_id(&tnl_hdr, link->peer_bearer_id);

tunnel_queue:
	skb_queue_walk(queue, skb) {
		struct sk_buff *outskb;
		struct tipc_msg *msg = buf_msg(skb);
		u32 len = msg_size(msg);

		msg_set_ack(msg, mod(link->next_in_no - 1));
		msg_set_bcast_ack(msg, link->owner->bclink.last_in);
		msg_set_size(&tnl_hdr, len + INT_H_SIZE);
		outskb = tipc_buf_acquire(len + INT_H_SIZE);
		if (outskb == NULL) {
			pr_warn("%sunable to send duplicate msg\n",
				link_co_err);
			return;
		}
		skb_copy_to_linear_data(outskb, &tnl_hdr, INT_H_SIZE);
		skb_copy_to_linear_data_offset(outskb, INT_H_SIZE,
					       skb->data, len);
		__tipc_link_xmit_skb(tnl, outskb);
		if (!tipc_link_is_up(link))
			return;
	}
	if (queue == &link->backlogq)
		return;
	queue = &link->backlogq;
	goto tunnel_queue;
}

/* tipc_link_dup_rcv(): Receive a tunnelled DUPLICATE_MSG packet.
 * Owner node is locked.
 */
static void tipc_link_dup_rcv(struct tipc_link *link,
			      struct sk_buff *skb)
{
	struct sk_buff *iskb;
	int pos = 0;

	if (!tipc_link_is_up(link))
		return;

	if (!tipc_msg_extract(skb, &iskb, &pos)) {
		pr_warn("%sfailed to extract inner dup pkt\n", link_co_err);
		return;
	}
	/* Append buffer to deferred queue, if applicable: */
	link_handle_out_of_seq_msg(link, iskb);
}

/*  tipc_link_failover_rcv(): Receive a tunnelled ORIGINAL_MSG packet
 *  Owner node is locked.
 */
static struct sk_buff *tipc_link_failover_rcv(struct tipc_link *l_ptr,
					      struct sk_buff *t_buf)
{
	struct tipc_msg *t_msg = buf_msg(t_buf);
	struct sk_buff *buf = NULL;
	struct tipc_msg *msg;
	int pos = 0;

	if (tipc_link_is_up(l_ptr))
		tipc_link_reset(l_ptr);

	/* First failover packet? */
	if (l_ptr->exp_msg_count == START_CHANGEOVER)
		l_ptr->exp_msg_count = msg_msgcnt(t_msg);

	/* Should there be an inner packet? */
	if (l_ptr->exp_msg_count) {
		l_ptr->exp_msg_count--;
		if (!tipc_msg_extract(t_buf, &buf, &pos)) {
			pr_warn("%sno inner failover pkt\n", link_co_err);
			goto exit;
		}
		msg = buf_msg(buf);

		if (less(msg_seqno(msg), l_ptr->reset_checkpoint)) {
			kfree_skb(buf);
			buf = NULL;
			goto exit;
		}
		if (msg_user(msg) == MSG_FRAGMENTER) {
			l_ptr->stats.recv_fragments++;
			tipc_buf_append(&l_ptr->reasm_buf, &buf);
		}
	}
exit:
	if ((!l_ptr->exp_msg_count) && (l_ptr->flags & LINK_STOPPED))
		tipc_link_delete(l_ptr);
	return buf;
}

/*  tipc_link_tunnel_rcv(): Receive a tunnelled packet, sent
 *  via other link as result of a failover (ORIGINAL_MSG) or
 *  a new active link (DUPLICATE_MSG). Failover packets are
 *  returned to the active link for delivery upwards.
 *  Owner node is locked.
 */
static int tipc_link_tunnel_rcv(struct tipc_node *n_ptr,
				struct sk_buff **buf)
{
	struct sk_buff *t_buf = *buf;
	struct tipc_link *l_ptr;
	struct tipc_msg *t_msg = buf_msg(t_buf);
	u32 bearer_id = msg_bearer_id(t_msg);

	*buf = NULL;

	if (bearer_id >= MAX_BEARERS)
		goto exit;

	l_ptr = n_ptr->links[bearer_id];
	if (!l_ptr)
		goto exit;

	if (msg_type(t_msg) == DUPLICATE_MSG)
		tipc_link_dup_rcv(l_ptr, t_buf);
	else if (msg_type(t_msg) == ORIGINAL_MSG)
		*buf = tipc_link_failover_rcv(l_ptr, t_buf);
	else
		pr_warn("%sunknown tunnel pkt received\n", link_co_err);
exit:
	kfree_skb(t_buf);
	return *buf != NULL;
}

static void link_set_supervision_props(struct tipc_link *l_ptr, u32 tol)
{
	unsigned long intv = ((tol / 4) > 500) ? 500 : tol / 4;

	if ((tol < TIPC_MIN_LINK_TOL) || (tol > TIPC_MAX_LINK_TOL))
		return;

	l_ptr->tolerance = tol;
	l_ptr->cont_intv = msecs_to_jiffies(intv);
	l_ptr->abort_limit = tol / (jiffies_to_msecs(l_ptr->cont_intv) / 4);
}

void tipc_link_set_queue_limits(struct tipc_link *l_ptr, u32 window)
{
	l_ptr->window = window;

	/* Data messages from this node, inclusive FIRST_FRAGM */
	l_ptr->queue_limit[TIPC_LOW_IMPORTANCE] = window;
	l_ptr->queue_limit[TIPC_MEDIUM_IMPORTANCE] = (window / 3) * 4;
	l_ptr->queue_limit[TIPC_HIGH_IMPORTANCE] = (window / 3) * 5;
	l_ptr->queue_limit[TIPC_CRITICAL_IMPORTANCE] = (window / 3) * 6;
	/* Transiting data messages,inclusive FIRST_FRAGM */
	l_ptr->queue_limit[TIPC_LOW_IMPORTANCE + 4] = 300;
	l_ptr->queue_limit[TIPC_MEDIUM_IMPORTANCE + 4] = 600;
	l_ptr->queue_limit[TIPC_HIGH_IMPORTANCE + 4] = 900;
	l_ptr->queue_limit[TIPC_CRITICAL_IMPORTANCE + 4] = 1200;
	l_ptr->queue_limit[CONN_MANAGER] = 1200;
	l_ptr->queue_limit[CHANGEOVER_PROTOCOL] = 2500;
	l_ptr->queue_limit[NAME_DISTRIBUTOR] = 3000;
	/* FRAGMENT and LAST_FRAGMENT packets */
	l_ptr->queue_limit[MSG_FRAGMENTER] = 4000;
}

/* tipc_link_find_owner - locate owner node of link by link's name
 * @net: the applicable net namespace
 * @name: pointer to link name string
 * @bearer_id: pointer to index in 'node->links' array where the link was found.
 *
 * Returns pointer to node owning the link, or 0 if no matching link is found.
 */
static struct tipc_node *tipc_link_find_owner(struct net *net,
					      const char *link_name,
					      unsigned int *bearer_id)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *l_ptr;
	struct tipc_node *n_ptr;
	struct tipc_node *found_node = NULL;
	int i;

	*bearer_id = 0;
	rcu_read_lock();
	list_for_each_entry_rcu(n_ptr, &tn->node_list, list) {
		tipc_node_lock(n_ptr);
		for (i = 0; i < MAX_BEARERS; i++) {
			l_ptr = n_ptr->links[i];
			if (l_ptr && !strcmp(l_ptr->name, link_name)) {
				*bearer_id = i;
				found_node = n_ptr;
				break;
			}
		}
		tipc_node_unlock(n_ptr);
		if (found_node)
			break;
	}
	rcu_read_unlock();

	return found_node;
}

/**
 * link_reset_statistics - reset link statistics
 * @l_ptr: pointer to link
 */
static void link_reset_statistics(struct tipc_link *l_ptr)
{
	memset(&l_ptr->stats, 0, sizeof(l_ptr->stats));
	l_ptr->stats.sent_info = l_ptr->next_out_no;
	l_ptr->stats.recv_info = l_ptr->next_in_no;
}

static void link_print(struct tipc_link *l_ptr, const char *str)
{
	struct tipc_net *tn = net_generic(l_ptr->owner->net, tipc_net_id);
	struct tipc_bearer *b_ptr;

	rcu_read_lock();
	b_ptr = rcu_dereference_rtnl(tn->bearer_list[l_ptr->bearer_id]);
	if (b_ptr)
		pr_info("%s Link %x<%s>:", str, l_ptr->addr, b_ptr->name);
	rcu_read_unlock();

	if (link_working_unknown(l_ptr))
		pr_cont(":WU\n");
	else if (link_reset_reset(l_ptr))
		pr_cont(":RR\n");
	else if (link_reset_unknown(l_ptr))
		pr_cont(":RU\n");
	else if (link_working_working(l_ptr))
		pr_cont(":WW\n");
	else
		pr_cont("\n");
}

/* Parse and validate nested (link) properties valid for media, bearer and link
 */
int tipc_nl_parse_link_prop(struct nlattr *prop, struct nlattr *props[])
{
	int err;

	err = nla_parse_nested(props, TIPC_NLA_PROP_MAX, prop,
			       tipc_nl_prop_policy);
	if (err)
		return err;

	if (props[TIPC_NLA_PROP_PRIO]) {
		u32 prio;

		prio = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
		if (prio > TIPC_MAX_LINK_PRI)
			return -EINVAL;
	}

	if (props[TIPC_NLA_PROP_TOL]) {
		u32 tol;

		tol = nla_get_u32(props[TIPC_NLA_PROP_TOL]);
		if ((tol < TIPC_MIN_LINK_TOL) || (tol > TIPC_MAX_LINK_TOL))
			return -EINVAL;
	}

	if (props[TIPC_NLA_PROP_WIN]) {
		u32 win;

		win = nla_get_u32(props[TIPC_NLA_PROP_WIN]);
		if ((win < TIPC_MIN_LINK_WIN) || (win > TIPC_MAX_LINK_WIN))
			return -EINVAL;
	}

	return 0;
}

int tipc_nl_link_set(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	int res = 0;
	int bearer_id;
	char *name;
	struct tipc_link *link;
	struct tipc_node *node;
	struct nlattr *attrs[TIPC_NLA_LINK_MAX + 1];
	struct net *net = sock_net(skb->sk);

	if (!info->attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_LINK_MAX,
			       info->attrs[TIPC_NLA_LINK],
			       tipc_nl_link_policy);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;

	name = nla_data(attrs[TIPC_NLA_LINK_NAME]);

	node = tipc_link_find_owner(net, name, &bearer_id);
	if (!node)
		return -EINVAL;

	tipc_node_lock(node);

	link = node->links[bearer_id];
	if (!link) {
		res = -EINVAL;
		goto out;
	}

	if (attrs[TIPC_NLA_LINK_PROP]) {
		struct nlattr *props[TIPC_NLA_PROP_MAX + 1];

		err = tipc_nl_parse_link_prop(attrs[TIPC_NLA_LINK_PROP],
					      props);
		if (err) {
			res = err;
			goto out;
		}

		if (props[TIPC_NLA_PROP_TOL]) {
			u32 tol;

			tol = nla_get_u32(props[TIPC_NLA_PROP_TOL]);
			link_set_supervision_props(link, tol);
			tipc_link_proto_xmit(link, STATE_MSG, 0, 0, tol, 0, 0);
		}
		if (props[TIPC_NLA_PROP_PRIO]) {
			u32 prio;

			prio = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
			link->priority = prio;
			tipc_link_proto_xmit(link, STATE_MSG, 0, 0, 0, prio, 0);
		}
		if (props[TIPC_NLA_PROP_WIN]) {
			u32 win;

			win = nla_get_u32(props[TIPC_NLA_PROP_WIN]);
			tipc_link_set_queue_limits(link, win);
		}
	}

out:
	tipc_node_unlock(node);

	return res;
}

static int __tipc_nl_add_stats(struct sk_buff *skb, struct tipc_stats *s)
{
	int i;
	struct nlattr *stats;

	struct nla_map {
		u32 key;
		u32 val;
	};

	struct nla_map map[] = {
		{TIPC_NLA_STATS_RX_INFO, s->recv_info},
		{TIPC_NLA_STATS_RX_FRAGMENTS, s->recv_fragments},
		{TIPC_NLA_STATS_RX_FRAGMENTED, s->recv_fragmented},
		{TIPC_NLA_STATS_RX_BUNDLES, s->recv_bundles},
		{TIPC_NLA_STATS_RX_BUNDLED, s->recv_bundled},
		{TIPC_NLA_STATS_TX_INFO, s->sent_info},
		{TIPC_NLA_STATS_TX_FRAGMENTS, s->sent_fragments},
		{TIPC_NLA_STATS_TX_FRAGMENTED, s->sent_fragmented},
		{TIPC_NLA_STATS_TX_BUNDLES, s->sent_bundles},
		{TIPC_NLA_STATS_TX_BUNDLED, s->sent_bundled},
		{TIPC_NLA_STATS_MSG_PROF_TOT, (s->msg_length_counts) ?
			s->msg_length_counts : 1},
		{TIPC_NLA_STATS_MSG_LEN_CNT, s->msg_length_counts},
		{TIPC_NLA_STATS_MSG_LEN_TOT, s->msg_lengths_total},
		{TIPC_NLA_STATS_MSG_LEN_P0, s->msg_length_profile[0]},
		{TIPC_NLA_STATS_MSG_LEN_P1, s->msg_length_profile[1]},
		{TIPC_NLA_STATS_MSG_LEN_P2, s->msg_length_profile[2]},
		{TIPC_NLA_STATS_MSG_LEN_P3, s->msg_length_profile[3]},
		{TIPC_NLA_STATS_MSG_LEN_P4, s->msg_length_profile[4]},
		{TIPC_NLA_STATS_MSG_LEN_P5, s->msg_length_profile[5]},
		{TIPC_NLA_STATS_MSG_LEN_P6, s->msg_length_profile[6]},
		{TIPC_NLA_STATS_RX_STATES, s->recv_states},
		{TIPC_NLA_STATS_RX_PROBES, s->recv_probes},
		{TIPC_NLA_STATS_RX_NACKS, s->recv_nacks},
		{TIPC_NLA_STATS_RX_DEFERRED, s->deferred_recv},
		{TIPC_NLA_STATS_TX_STATES, s->sent_states},
		{TIPC_NLA_STATS_TX_PROBES, s->sent_probes},
		{TIPC_NLA_STATS_TX_NACKS, s->sent_nacks},
		{TIPC_NLA_STATS_TX_ACKS, s->sent_acks},
		{TIPC_NLA_STATS_RETRANSMITTED, s->retransmitted},
		{TIPC_NLA_STATS_DUPLICATES, s->duplicates},
		{TIPC_NLA_STATS_LINK_CONGS, s->link_congs},
		{TIPC_NLA_STATS_MAX_QUEUE, s->max_queue_sz},
		{TIPC_NLA_STATS_AVG_QUEUE, s->queue_sz_counts ?
			(s->accu_queue_sz / s->queue_sz_counts) : 0}
	};

	stats = nla_nest_start(skb, TIPC_NLA_LINK_STATS);
	if (!stats)
		return -EMSGSIZE;

	for (i = 0; i <  ARRAY_SIZE(map); i++)
		if (nla_put_u32(skb, map[i].key, map[i].val))
			goto msg_full;

	nla_nest_end(skb, stats);

	return 0;
msg_full:
	nla_nest_cancel(skb, stats);

	return -EMSGSIZE;
}

/* Caller should hold appropriate locks to protect the link */
static int __tipc_nl_add_link(struct net *net, struct tipc_nl_msg *msg,
			      struct tipc_link *link)
{
	int err;
	void *hdr;
	struct nlattr *attrs;
	struct nlattr *prop;
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  NLM_F_MULTI, TIPC_NL_LINK_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_LINK);
	if (!attrs)
		goto msg_full;

	if (nla_put_string(msg->skb, TIPC_NLA_LINK_NAME, link->name))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_DEST,
			tipc_cluster_mask(tn->own_addr)))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_MTU, link->max_pkt))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_RX, link->next_in_no))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_TX, link->next_out_no))
		goto attr_msg_full;

	if (tipc_link_is_up(link))
		if (nla_put_flag(msg->skb, TIPC_NLA_LINK_UP))
			goto attr_msg_full;
	if (tipc_link_is_active(link))
		if (nla_put_flag(msg->skb, TIPC_NLA_LINK_ACTIVE))
			goto attr_msg_full;

	prop = nla_nest_start(msg->skb, TIPC_NLA_LINK_PROP);
	if (!prop)
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_PRIO, link->priority))
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_TOL, link->tolerance))
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_WIN,
			link->queue_limit[TIPC_LOW_IMPORTANCE]))
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_PRIO, link->priority))
		goto prop_msg_full;
	nla_nest_end(msg->skb, prop);

	err = __tipc_nl_add_stats(msg->skb, &link->stats);
	if (err)
		goto attr_msg_full;

	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

prop_msg_full:
	nla_nest_cancel(msg->skb, prop);
attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

/* Caller should hold node lock  */
static int __tipc_nl_add_node_links(struct net *net, struct tipc_nl_msg *msg,
				    struct tipc_node *node, u32 *prev_link)
{
	u32 i;
	int err;

	for (i = *prev_link; i < MAX_BEARERS; i++) {
		*prev_link = i;

		if (!node->links[i])
			continue;

		err = __tipc_nl_add_link(net, msg, node->links[i]);
		if (err)
			return err;
	}
	*prev_link = 0;

	return 0;
}

int tipc_nl_link_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_node *node;
	struct tipc_nl_msg msg;
	u32 prev_node = cb->args[0];
	u32 prev_link = cb->args[1];
	int done = cb->args[2];
	int err;

	if (done)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rcu_read_lock();

	if (prev_node) {
		node = tipc_node_find(net, prev_node);
		if (!node) {
			/* We never set seq or call nl_dump_check_consistent()
			 * this means that setting prev_seq here will cause the
			 * consistence check to fail in the netlink callback
			 * handler. Resulting in the last NLMSG_DONE message
			 * having the NLM_F_DUMP_INTR flag set.
			 */
			cb->prev_seq = 1;
			goto out;
		}

		list_for_each_entry_continue_rcu(node, &tn->node_list,
						 list) {
			tipc_node_lock(node);
			err = __tipc_nl_add_node_links(net, &msg, node,
						       &prev_link);
			tipc_node_unlock(node);
			if (err)
				goto out;

			prev_node = node->addr;
		}
	} else {
		err = tipc_nl_add_bc_link(net, &msg);
		if (err)
			goto out;

		list_for_each_entry_rcu(node, &tn->node_list, list) {
			tipc_node_lock(node);
			err = __tipc_nl_add_node_links(net, &msg, node,
						       &prev_link);
			tipc_node_unlock(node);
			if (err)
				goto out;

			prev_node = node->addr;
		}
	}
	done = 1;
out:
	rcu_read_unlock();

	cb->args[0] = prev_node;
	cb->args[1] = prev_link;
	cb->args[2] = done;

	return skb->len;
}

int tipc_nl_link_get(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct sk_buff *ans_skb;
	struct tipc_nl_msg msg;
	struct tipc_link *link;
	struct tipc_node *node;
	char *name;
	int bearer_id;
	int err;

	if (!info->attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;

	name = nla_data(info->attrs[TIPC_NLA_LINK_NAME]);
	node = tipc_link_find_owner(net, name, &bearer_id);
	if (!node)
		return -EINVAL;

	ans_skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!ans_skb)
		return -ENOMEM;

	msg.skb = ans_skb;
	msg.portid = info->snd_portid;
	msg.seq = info->snd_seq;

	tipc_node_lock(node);
	link = node->links[bearer_id];
	if (!link) {
		err = -EINVAL;
		goto err_out;
	}

	err = __tipc_nl_add_link(net, &msg, link);
	if (err)
		goto err_out;

	tipc_node_unlock(node);

	return genlmsg_reply(ans_skb, info);

err_out:
	tipc_node_unlock(node);
	nlmsg_free(ans_skb);

	return err;
}

int tipc_nl_link_reset_stats(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *link_name;
	unsigned int bearer_id;
	struct tipc_link *link;
	struct tipc_node *node;
	struct nlattr *attrs[TIPC_NLA_LINK_MAX + 1];
	struct net *net = sock_net(skb->sk);

	if (!info->attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_LINK_MAX,
			       info->attrs[TIPC_NLA_LINK],
			       tipc_nl_link_policy);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;

	link_name = nla_data(attrs[TIPC_NLA_LINK_NAME]);

	if (strcmp(link_name, tipc_bclink_name) == 0) {
		err = tipc_bclink_reset_stats(net);
		if (err)
			return err;
		return 0;
	}

	node = tipc_link_find_owner(net, link_name, &bearer_id);
	if (!node)
		return -EINVAL;

	tipc_node_lock(node);

	link = node->links[bearer_id];
	if (!link) {
		tipc_node_unlock(node);
		return -EINVAL;
	}

	link_reset_statistics(link);

	tipc_node_unlock(node);

	return 0;
}

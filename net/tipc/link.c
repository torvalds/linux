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
#include "subscr.h"
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
 * Interval between NACKs when packets arrive out of order
 */
#define TIPC_NACK_INTV (TIPC_MIN_LINK_WIN * 2)
/*
 * Out-of-range value for link session numbers
 */
#define WILDCARD_SESSION 0x10000

/* State value stored in 'failover_pkts'
 */
#define FIRST_FAILOVER 0xffffu

/* Link FSM states and events:
 */
enum {
	TIPC_LINK_WORKING,
	TIPC_LINK_PROBING,
	TIPC_LINK_RESETTING,
	TIPC_LINK_ESTABLISHING
};

enum {
	PEER_RESET_EVT    = RESET_MSG,
	ACTIVATE_EVT      = ACTIVATE_MSG,
	TRAFFIC_EVT,      /* Any other valid msg from peer */
	SILENCE_EVT       /* Peer was silent during last timer interval*/
};

/* Link FSM state checking routines
 */
static int link_working(struct tipc_link *l)
{
	return l->state == TIPC_LINK_WORKING;
}

static int link_probing(struct tipc_link *l)
{
	return l->state == TIPC_LINK_PROBING;
}

static int link_resetting(struct tipc_link *l)
{
	return l->state == TIPC_LINK_RESETTING;
}

static int link_establishing(struct tipc_link *l)
{
	return l->state == TIPC_LINK_ESTABLISHING;
}

static int tipc_link_proto_rcv(struct tipc_link *l, struct sk_buff *skb,
			       struct sk_buff_head *xmitq);
static void tipc_link_build_proto_msg(struct tipc_link *l, int mtyp, bool probe,
				      u16 rcvgap, int tolerance, int priority,
				      struct sk_buff_head *xmitq);
static void link_reset_statistics(struct tipc_link *l_ptr);
static void link_print(struct tipc_link *l_ptr, const char *str);
static void tipc_link_build_bcast_sync_msg(struct tipc_link *l,
					   struct sk_buff_head *xmitq);
static void tipc_link_sync_rcv(struct tipc_node *n, struct sk_buff *buf);
static void tipc_link_input(struct tipc_link *l, struct sk_buff *skb);
static bool tipc_data_input(struct tipc_link *l, struct sk_buff *skb);
static bool tipc_link_failover_rcv(struct tipc_link *l, struct sk_buff **skb);

/*
 *  Simple link routines
 */
static unsigned int align(unsigned int i)
{
	return (i + 3) & ~3u;
}

static struct tipc_link *tipc_parallel_link(struct tipc_link *l)
{
	struct tipc_node *n = l->owner;

	if (node_active_link(n, 0) != l)
		return node_active_link(n, 0);
	return node_active_link(n, 1);
}

/*
 *  Simple non-static link routines (i.e. referenced outside this file)
 */
int tipc_link_is_up(struct tipc_link *l_ptr)
{
	if (!l_ptr)
		return 0;
	return link_working(l_ptr) || link_probing(l_ptr);
}

int tipc_link_is_active(struct tipc_link *l)
{
	struct tipc_node *n = l->owner;

	return (node_active_link(n, 0) == l) || (node_active_link(n, 1) == l);
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
				   const struct tipc_media_addr *media_addr,
				   struct sk_buff_head *inputq,
				   struct sk_buff_head *namedq)
{
	struct tipc_net *tn = net_generic(n_ptr->net, tipc_net_id);
	struct tipc_link *l_ptr;
	struct tipc_msg *msg;
	char *if_name;
	char addr_string[16];
	u32 peer = n_ptr->addr;

	if (n_ptr->link_cnt >= MAX_BEARERS) {
		tipc_addr_string_fill(addr_string, n_ptr->addr);
		pr_err("Cannot establish %uth link to %s. Max %u allowed.\n",
		       n_ptr->link_cnt, addr_string, MAX_BEARERS);
		return NULL;
	}

	if (n_ptr->links[b_ptr->identity].link) {
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
	l_ptr->peer_session = WILDCARD_SESSION;
	l_ptr->bearer_id = b_ptr->identity;
	l_ptr->tolerance = b_ptr->tolerance;
	l_ptr->state = TIPC_LINK_RESETTING;

	l_ptr->pmsg = (struct tipc_msg *)&l_ptr->proto_msg;
	msg = l_ptr->pmsg;
	tipc_msg_init(tn->own_addr, msg, LINK_PROTOCOL, RESET_MSG, INT_H_SIZE,
		      l_ptr->addr);
	msg_set_size(msg, sizeof(l_ptr->proto_msg));
	msg_set_session(msg, (tn->random & 0xffff));
	msg_set_bearer_id(msg, b_ptr->identity);
	strcpy((char *)msg_data(msg), if_name);
	l_ptr->net_plane = b_ptr->net_plane;
	l_ptr->advertised_mtu = b_ptr->mtu;
	l_ptr->mtu = l_ptr->advertised_mtu;
	l_ptr->priority = b_ptr->priority;
	tipc_link_set_queue_limits(l_ptr, b_ptr->window);
	l_ptr->snd_nxt = 1;
	__skb_queue_head_init(&l_ptr->transmq);
	__skb_queue_head_init(&l_ptr->backlogq);
	__skb_queue_head_init(&l_ptr->deferdq);
	skb_queue_head_init(&l_ptr->wakeupq);
	l_ptr->inputq = inputq;
	l_ptr->namedq = namedq;
	skb_queue_head_init(l_ptr->inputq);
	link_reset_statistics(l_ptr);
	tipc_node_attach_link(n_ptr, l_ptr);
	return l_ptr;
}

/**
 * tipc_link_delete - Delete a link
 * @l: link to be deleted
 */
void tipc_link_delete(struct tipc_link *l)
{
	tipc_link_reset(l);
	tipc_link_reset_fragments(l);
	tipc_node_detach_link(l->owner, l);
}

void tipc_link_delete_list(struct net *net, unsigned int bearer_id)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *link;
	struct tipc_node *node;

	rcu_read_lock();
	list_for_each_entry_rcu(node, &tn->node_list, list) {
		tipc_node_lock(node);
		link = node->links[bearer_id].link;
		if (link)
			tipc_link_delete(link);
		tipc_node_unlock(node);
	}
	rcu_read_unlock();
}

/* tipc_link_build_bcast_sync_msg() - synchronize broadcast link endpoints.
 *
 * Give a newly added peer node the sequence number where it should
 * start receiving and acking broadcast packets.
 */
static void tipc_link_build_bcast_sync_msg(struct tipc_link *l,
					   struct sk_buff_head *xmitq)
{
	struct sk_buff *skb;
	struct sk_buff_head list;

	skb = tipc_msg_create(BCAST_PROTOCOL, STATE_MSG, INT_H_SIZE,
			      0, l->addr, link_own_addr(l), 0, 0, 0);
	if (!skb)
		return;
	__skb_queue_head_init(&list);
	__skb_queue_tail(&list, skb);
	tipc_link_xmit(l, &list, xmitq);
}

/**
 * tipc_link_fsm_evt - link finite state machine
 * @l: pointer to link
 * @evt: state machine event to be processed
 * @xmitq: queue to prepend created protocol message, if any
 */
static int tipc_link_fsm_evt(struct tipc_link *l, int evt,
			     struct sk_buff_head *xmitq)
{
	int mtyp = 0, rc = 0;
	struct tipc_link *pl;
	enum {
		LINK_RESET     = 1,
		LINK_ACTIVATE  = (1 << 1),
		SND_PROBE      = (1 << 2),
		SND_STATE      = (1 << 3),
		SND_RESET      = (1 << 4),
		SND_ACTIVATE   = (1 << 5),
		SND_BCAST_SYNC = (1 << 6)
	} actions = 0;

	if (l->exec_mode == TIPC_LINK_BLOCKED)
		return rc;

	switch (l->state) {
	case TIPC_LINK_WORKING:
		switch (evt) {
		case TRAFFIC_EVT:
		case ACTIVATE_EVT:
			break;
		case SILENCE_EVT:
			l->state = TIPC_LINK_PROBING;
			actions |= SND_PROBE;
			break;
		case PEER_RESET_EVT:
			actions |= LINK_RESET | SND_ACTIVATE;
			break;
		default:
			pr_debug("%s%u WORKING\n", link_unk_evt, evt);
		}
		break;
	case TIPC_LINK_PROBING:
		switch (evt) {
		case TRAFFIC_EVT:
		case ACTIVATE_EVT:
			l->state = TIPC_LINK_WORKING;
			break;
		case PEER_RESET_EVT:
			actions |= LINK_RESET | SND_ACTIVATE;
			break;
		case SILENCE_EVT:
			if (l->silent_intv_cnt <= l->abort_limit) {
				actions |= SND_PROBE;
				break;
			}
			actions |= LINK_RESET | SND_RESET;
			break;
		default:
			pr_err("%s%u PROBING\n", link_unk_evt, evt);
		}
		break;
	case TIPC_LINK_RESETTING:
		switch (evt) {
		case TRAFFIC_EVT:
			break;
		case ACTIVATE_EVT:
			pl = node_active_link(l->owner, 0);
			if (pl && link_probing(pl))
				break;
			actions |= LINK_ACTIVATE;
			if (!l->owner->working_links)
				actions |= SND_BCAST_SYNC;
			break;
		case PEER_RESET_EVT:
			l->state = TIPC_LINK_ESTABLISHING;
			actions |= SND_ACTIVATE;
			break;
		case SILENCE_EVT:
			actions |= SND_RESET;
			break;
		default:
			pr_err("%s%u in RESETTING\n", link_unk_evt, evt);
		}
		break;
	case TIPC_LINK_ESTABLISHING:
		switch (evt) {
		case TRAFFIC_EVT:
		case ACTIVATE_EVT:
			pl = node_active_link(l->owner, 0);
			if (pl && link_probing(pl))
				break;
			actions |= LINK_ACTIVATE;
			if (!l->owner->working_links)
				actions |= SND_BCAST_SYNC;
			break;
		case PEER_RESET_EVT:
			break;
		case SILENCE_EVT:
			actions |= SND_ACTIVATE;
			break;
		default:
			pr_err("%s%u ESTABLISHING\n", link_unk_evt, evt);
		}
		break;
	default:
		pr_err("Unknown link state %u/%u\n", l->state, evt);
	}

	/* Perform actions as decided by FSM */
	if (actions & LINK_RESET) {
		l->exec_mode = TIPC_LINK_BLOCKED;
		rc |= TIPC_LINK_DOWN_EVT;
	}
	if (actions & LINK_ACTIVATE) {
		l->exec_mode = TIPC_LINK_OPEN;
		rc |= TIPC_LINK_UP_EVT;
	}
	if (actions & (SND_STATE | SND_PROBE))
		mtyp = STATE_MSG;
	if (actions & SND_RESET)
		mtyp = RESET_MSG;
	if (actions & SND_ACTIVATE)
		mtyp = ACTIVATE_MSG;
	if (actions & (SND_PROBE | SND_STATE | SND_RESET | SND_ACTIVATE))
		tipc_link_build_proto_msg(l, mtyp, actions & SND_PROBE,
					  0, 0, 0, xmitq);
	if (actions & SND_BCAST_SYNC)
		tipc_link_build_bcast_sync_msg(l, xmitq);
	return rc;
}

/* link_profile_stats - update statistical profiling of traffic
 */
static void link_profile_stats(struct tipc_link *l)
{
	struct sk_buff *skb;
	struct tipc_msg *msg;
	int length;

	/* Update counters used in statistical profiling of send traffic */
	l->stats.accu_queue_sz += skb_queue_len(&l->transmq);
	l->stats.queue_sz_counts++;

	skb = skb_peek(&l->transmq);
	if (!skb)
		return;
	msg = buf_msg(skb);
	length = msg_size(msg);

	if (msg_user(msg) == MSG_FRAGMENTER) {
		if (msg_type(msg) != FIRST_FRAGMENT)
			return;
		length = msg_size(msg_get_wrapped(msg));
	}
	l->stats.msg_lengths_total += length;
	l->stats.msg_length_counts++;
	if (length <= 64)
		l->stats.msg_length_profile[0]++;
	else if (length <= 256)
		l->stats.msg_length_profile[1]++;
	else if (length <= 1024)
		l->stats.msg_length_profile[2]++;
	else if (length <= 4096)
		l->stats.msg_length_profile[3]++;
	else if (length <= 16384)
		l->stats.msg_length_profile[4]++;
	else if (length <= 32768)
		l->stats.msg_length_profile[5]++;
	else
		l->stats.msg_length_profile[6]++;
}

/* tipc_link_timeout - perform periodic task as instructed from node timeout
 */
int tipc_link_timeout(struct tipc_link *l, struct sk_buff_head *xmitq)
{
	int rc = 0;

	link_profile_stats(l);
	if (l->silent_intv_cnt)
		rc = tipc_link_fsm_evt(l, SILENCE_EVT, xmitq);
	else if (link_working(l) && tipc_bclink_acks_missing(l->owner))
		tipc_link_build_proto_msg(l, STATE_MSG, 0, 0, 0, 0, xmitq);
	l->silent_intv_cnt++;
	return rc;
}

/**
 * link_schedule_user - schedule a message sender for wakeup after congestion
 * @link: congested link
 * @list: message that was attempted sent
 * Create pseudo msg to send back to user when congestion abates
 * Does not consume buffer list
 */
static int link_schedule_user(struct tipc_link *link, struct sk_buff_head *list)
{
	struct tipc_msg *msg = buf_msg(skb_peek(list));
	int imp = msg_importance(msg);
	u32 oport = msg_origport(msg);
	u32 addr = link_own_addr(link);
	struct sk_buff *skb;

	/* This really cannot happen...  */
	if (unlikely(imp > TIPC_CRITICAL_IMPORTANCE)) {
		pr_warn("%s<%s>, send queue full", link_rst_msg, link->name);
		return -ENOBUFS;
	}
	/* Non-blocking sender: */
	if (TIPC_SKB_CB(skb_peek(list))->wakeup_pending)
		return -ELINKCONG;

	/* Create and schedule wakeup pseudo message */
	skb = tipc_msg_create(SOCK_WAKEUP, 0, INT_H_SIZE, 0,
			      addr, addr, oport, 0, 0);
	if (!skb)
		return -ENOBUFS;
	TIPC_SKB_CB(skb)->chain_sz = skb_queue_len(list);
	TIPC_SKB_CB(skb)->chain_imp = imp;
	skb_queue_tail(&link->wakeupq, skb);
	link->stats.link_congs++;
	return -ELINKCONG;
}

/**
 * link_prepare_wakeup - prepare users for wakeup after congestion
 * @link: congested link
 * Move a number of waiting users, as permitted by available space in
 * the send queue, from link wait queue to node wait queue for wakeup
 */
void link_prepare_wakeup(struct tipc_link *l)
{
	int pnd[TIPC_SYSTEM_IMPORTANCE + 1] = {0,};
	int imp, lim;
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&l->wakeupq, skb, tmp) {
		imp = TIPC_SKB_CB(skb)->chain_imp;
		lim = l->window + l->backlog[imp].limit;
		pnd[imp] += TIPC_SKB_CB(skb)->chain_sz;
		if ((pnd[imp] + l->backlog[imp].len) >= lim)
			break;
		skb_unlink(skb, &l->wakeupq);
		skb_queue_tail(l->inputq, skb);
		l->owner->inputq = l->inputq;
		l->owner->action_flags |= TIPC_MSG_EVT;
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

void tipc_link_purge_backlog(struct tipc_link *l)
{
	__skb_queue_purge(&l->backlogq);
	l->backlog[TIPC_LOW_IMPORTANCE].len = 0;
	l->backlog[TIPC_MEDIUM_IMPORTANCE].len = 0;
	l->backlog[TIPC_HIGH_IMPORTANCE].len = 0;
	l->backlog[TIPC_CRITICAL_IMPORTANCE].len = 0;
	l->backlog[TIPC_SYSTEM_IMPORTANCE].len = 0;
}

/**
 * tipc_link_purge_queues - purge all pkt queues associated with link
 * @l_ptr: pointer to link
 */
void tipc_link_purge_queues(struct tipc_link *l_ptr)
{
	__skb_queue_purge(&l_ptr->deferdq);
	__skb_queue_purge(&l_ptr->transmq);
	tipc_link_purge_backlog(l_ptr);
	tipc_link_reset_fragments(l_ptr);
}

void tipc_link_reset(struct tipc_link *l_ptr)
{
	u32 prev_state = l_ptr->state;
	int was_active_link = tipc_link_is_active(l_ptr);
	struct tipc_node *owner = l_ptr->owner;
	struct tipc_link *pl = tipc_parallel_link(l_ptr);

	msg_set_session(l_ptr->pmsg, ((msg_session(l_ptr->pmsg) + 1) & 0xffff));

	/* Link is down, accept any session */
	l_ptr->peer_session = WILDCARD_SESSION;

	/* Prepare for renewed mtu size negotiation */
	l_ptr->mtu = l_ptr->advertised_mtu;

	l_ptr->state = TIPC_LINK_RESETTING;

	if ((prev_state == TIPC_LINK_RESETTING) ||
	    (prev_state == TIPC_LINK_ESTABLISHING))
		return;

	tipc_node_link_down(l_ptr->owner, l_ptr->bearer_id);
	tipc_bearer_remove_dest(owner->net, l_ptr->bearer_id, l_ptr->addr);

	if (was_active_link && tipc_node_is_up(l_ptr->owner) && (pl != l_ptr)) {
		l_ptr->exec_mode = TIPC_LINK_BLOCKED;
		l_ptr->failover_checkpt = l_ptr->rcv_nxt;
		pl->failover_pkts = FIRST_FAILOVER;
		pl->failover_checkpt = l_ptr->rcv_nxt;
		pl->failover_skb = l_ptr->reasm_buf;
	} else {
		kfree_skb(l_ptr->reasm_buf);
	}
	/* Clean up all queues, except inputq: */
	__skb_queue_purge(&l_ptr->transmq);
	__skb_queue_purge(&l_ptr->deferdq);
	if (!owner->inputq)
		owner->inputq = l_ptr->inputq;
	skb_queue_splice_init(&l_ptr->wakeupq, owner->inputq);
	if (!skb_queue_empty(owner->inputq))
		owner->action_flags |= TIPC_MSG_EVT;
	tipc_link_purge_backlog(l_ptr);
	l_ptr->reasm_buf = NULL;
	l_ptr->rcv_unacked = 0;
	l_ptr->snd_nxt = 1;
	l_ptr->rcv_nxt = 1;
	l_ptr->silent_intv_cnt = 0;
	l_ptr->stats.recv_info = 0;
	l_ptr->stale_count = 0;
	link_reset_statistics(l_ptr);
}

void tipc_link_activate(struct tipc_link *link)
{
	struct tipc_node *node = link->owner;

	link->rcv_nxt = 1;
	link->stats.recv_info = 1;
	link->silent_intv_cnt = 0;
	link->state = TIPC_LINK_WORKING;
	link->exec_mode = TIPC_LINK_OPEN;
	tipc_node_link_up(node, link->bearer_id);
	tipc_bearer_add_dest(node->net, link->bearer_id, link->addr);
}

/**
 * __tipc_link_xmit(): same as tipc_link_xmit, but destlink is known & locked
 * @link: link to use
 * @list: chain of buffers containing message
 *
 * Consumes the buffer chain, except when returning an error code,
 * Returns 0 if success, or errno: -ELINKCONG, -EMSGSIZE or -ENOBUFS
 * Messages at TIPC_SYSTEM_IMPORTANCE are always accepted
 */
int __tipc_link_xmit(struct net *net, struct tipc_link *link,
		     struct sk_buff_head *list)
{
	struct tipc_msg *msg = buf_msg(skb_peek(list));
	unsigned int maxwin = link->window;
	unsigned int i, imp = msg_importance(msg);
	uint mtu = link->mtu;
	u16 ack = mod(link->rcv_nxt - 1);
	u16 seqno = link->snd_nxt;
	u16 bc_last_in = link->owner->bclink.last_in;
	struct tipc_media_addr *addr = &link->media_addr;
	struct sk_buff_head *transmq = &link->transmq;
	struct sk_buff_head *backlogq = &link->backlogq;
	struct sk_buff *skb, *bskb;

	/* Match msg importance against this and all higher backlog limits: */
	for (i = imp; i <= TIPC_SYSTEM_IMPORTANCE; i++) {
		if (unlikely(link->backlog[i].len >= link->backlog[i].limit))
			return link_schedule_user(link, list);
	}
	if (unlikely(msg_size(msg) > mtu))
		return -EMSGSIZE;

	/* Prepare each packet for sending, and add to relevant queue: */
	while (skb_queue_len(list)) {
		skb = skb_peek(list);
		msg = buf_msg(skb);
		msg_set_seqno(msg, seqno);
		msg_set_ack(msg, ack);
		msg_set_bcast_ack(msg, bc_last_in);

		if (likely(skb_queue_len(transmq) < maxwin)) {
			__skb_dequeue(list);
			__skb_queue_tail(transmq, skb);
			tipc_bearer_send(net, link->bearer_id, skb, addr);
			link->rcv_unacked = 0;
			seqno++;
			continue;
		}
		if (tipc_msg_bundle(skb_peek_tail(backlogq), msg, mtu)) {
			kfree_skb(__skb_dequeue(list));
			link->stats.sent_bundled++;
			continue;
		}
		if (tipc_msg_make_bundle(&bskb, msg, mtu, link->addr)) {
			kfree_skb(__skb_dequeue(list));
			__skb_queue_tail(backlogq, bskb);
			link->backlog[msg_importance(buf_msg(bskb))].len++;
			link->stats.sent_bundled++;
			link->stats.sent_bundles++;
			continue;
		}
		link->backlog[imp].len += skb_queue_len(list);
		skb_queue_splice_tail_init(list, backlogq);
	}
	link->snd_nxt = seqno;
	return 0;
}

/**
 * tipc_link_xmit(): enqueue buffer list according to queue situation
 * @link: link to use
 * @list: chain of buffers containing message
 * @xmitq: returned list of packets to be sent by caller
 *
 * Consumes the buffer chain, except when returning -ELINKCONG,
 * since the caller then may want to make more send attempts.
 * Returns 0 if success, or errno: -ELINKCONG, -EMSGSIZE or -ENOBUFS
 * Messages at TIPC_SYSTEM_IMPORTANCE are always accepted
 */
int tipc_link_xmit(struct tipc_link *l, struct sk_buff_head *list,
		   struct sk_buff_head *xmitq)
{
	struct tipc_msg *hdr = buf_msg(skb_peek(list));
	unsigned int maxwin = l->window;
	unsigned int i, imp = msg_importance(hdr);
	unsigned int mtu = l->mtu;
	u16 ack = l->rcv_nxt - 1;
	u16 seqno = l->snd_nxt;
	u16 bc_last_in = l->owner->bclink.last_in;
	struct sk_buff_head *transmq = &l->transmq;
	struct sk_buff_head *backlogq = &l->backlogq;
	struct sk_buff *skb, *_skb, *bskb;

	/* Match msg importance against this and all higher backlog limits: */
	for (i = imp; i <= TIPC_SYSTEM_IMPORTANCE; i++) {
		if (unlikely(l->backlog[i].len >= l->backlog[i].limit))
			return link_schedule_user(l, list);
	}
	if (unlikely(msg_size(hdr) > mtu))
		return -EMSGSIZE;

	/* Prepare each packet for sending, and add to relevant queue: */
	while (skb_queue_len(list)) {
		skb = skb_peek(list);
		hdr = buf_msg(skb);
		msg_set_seqno(hdr, seqno);
		msg_set_ack(hdr, ack);
		msg_set_bcast_ack(hdr, bc_last_in);

		if (likely(skb_queue_len(transmq) < maxwin)) {
			_skb = skb_clone(skb, GFP_ATOMIC);
			if (!_skb)
				return -ENOBUFS;
			__skb_dequeue(list);
			__skb_queue_tail(transmq, skb);
			__skb_queue_tail(xmitq, _skb);
			l->rcv_unacked = 0;
			seqno++;
			continue;
		}
		if (tipc_msg_bundle(skb_peek_tail(backlogq), hdr, mtu)) {
			kfree_skb(__skb_dequeue(list));
			l->stats.sent_bundled++;
			continue;
		}
		if (tipc_msg_make_bundle(&bskb, hdr, mtu, l->addr)) {
			kfree_skb(__skb_dequeue(list));
			__skb_queue_tail(backlogq, bskb);
			l->backlog[msg_importance(buf_msg(bskb))].len++;
			l->stats.sent_bundled++;
			l->stats.sent_bundles++;
			continue;
		}
		l->backlog[imp].len += skb_queue_len(list);
		skb_queue_splice_tail_init(list, backlogq);
	}
	l->snd_nxt = seqno;
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
	u16 seqno = link->snd_nxt;
	u16 ack = mod(link->rcv_nxt - 1);

	while (skb_queue_len(&link->transmq) < link->window) {
		skb = __skb_dequeue(&link->backlogq);
		if (!skb)
			break;
		msg = buf_msg(skb);
		link->backlog[msg_importance(msg)].len--;
		msg_set_ack(msg, ack);
		msg_set_seqno(msg, seqno);
		seqno = mod(seqno + 1);
		msg_set_bcast_ack(msg, link->owner->bclink.last_in);
		link->rcv_unacked = 0;
		__skb_queue_tail(&link->transmq, skb);
		tipc_bearer_send(link->owner->net, link->bearer_id,
				 skb, &link->media_addr);
	}
	link->snd_nxt = seqno;
}

void tipc_link_advance_backlog(struct tipc_link *l, struct sk_buff_head *xmitq)
{
	struct sk_buff *skb, *_skb;
	struct tipc_msg *hdr;
	u16 seqno = l->snd_nxt;
	u16 ack = l->rcv_nxt - 1;

	while (skb_queue_len(&l->transmq) < l->window) {
		skb = skb_peek(&l->backlogq);
		if (!skb)
			break;
		_skb = skb_clone(skb, GFP_ATOMIC);
		if (!_skb)
			break;
		__skb_dequeue(&l->backlogq);
		hdr = buf_msg(skb);
		l->backlog[msg_importance(hdr)].len--;
		__skb_queue_tail(&l->transmq, skb);
		__skb_queue_tail(xmitq, _skb);
		msg_set_ack(hdr, ack);
		msg_set_seqno(hdr, seqno);
		msg_set_bcast_ack(hdr, l->owner->bclink.last_in);
		l->rcv_unacked = 0;
		seqno++;
	}
	l->snd_nxt = seqno;
}

void tipc_link_reset_all(struct tipc_node *node)
{
	char addr_string[16];
	u32 i;

	tipc_node_lock(node);

	pr_warn("Resetting all links to %s\n",
		tipc_addr_string_fill(addr_string, node->addr));

	for (i = 0; i < MAX_BEARERS; i++) {
		if (node->links[i].link) {
			link_print(node->links[i].link, "Resetting link\n");
			tipc_link_reset(node->links[i].link);
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
		link_print(l_ptr, "Resetting link ");
		pr_info("Failed msg: usr %u, typ %u, len %u, err %u\n",
			msg_user(msg), msg_type(msg), msg_size(msg),
			msg_errcode(msg));
		pr_info("sqno %u, prev: %x, src: %x\n",
			msg_seqno(msg), msg_prevnode(msg), msg_orignode(msg));
		tipc_link_reset(l_ptr);
	} else {
		/* Handle failure on broadcast link */
		struct tipc_node *n_ptr;
		char addr_string[16];

		pr_info("Msg seq number: %u,  ", msg_seqno(msg));
		pr_cont("Outstanding acks: %lu\n",
			(unsigned long) TIPC_SKB_CB(buf)->handle);

		n_ptr = tipc_bclink_retransmit_to(net);

		tipc_addr_string_fill(addr_string, n_ptr->addr);
		pr_info("Broadcast link info for %s\n", addr_string);
		pr_info("Reception permitted: %d,  Acked: %u\n",
			n_ptr->bclink.recv_permitted,
			n_ptr->bclink.acked);
		pr_info("Last in: %u,  Oos state: %u,  Last sent: %u\n",
			n_ptr->bclink.last_in,
			n_ptr->bclink.oos_state,
			n_ptr->bclink.last_sent);

		n_ptr->action_flags |= TIPC_BCAST_RESET;
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
	if (l_ptr->last_retransm == msg_seqno(msg)) {
		if (++l_ptr->stale_count > 100) {
			link_retransmit_failure(l_ptr, skb);
			return;
		}
	} else {
		l_ptr->last_retransm = msg_seqno(msg);
		l_ptr->stale_count = 1;
	}

	skb_queue_walk_from(&l_ptr->transmq, skb) {
		if (!retransmits)
			break;
		msg = buf_msg(skb);
		msg_set_ack(msg, mod(l_ptr->rcv_nxt - 1));
		msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
		tipc_bearer_send(l_ptr->owner->net, l_ptr->bearer_id, skb,
				 &l_ptr->media_addr);
		retransmits--;
		l_ptr->stats.retransmitted++;
	}
}

static int tipc_link_retransm(struct tipc_link *l, int retransm,
			      struct sk_buff_head *xmitq)
{
	struct sk_buff *_skb, *skb = skb_peek(&l->transmq);
	struct tipc_msg *hdr;

	if (!skb)
		return 0;

	/* Detect repeated retransmit failures on same packet */
	if (likely(l->last_retransm != buf_seqno(skb))) {
		l->last_retransm = buf_seqno(skb);
		l->stale_count = 1;
	} else if (++l->stale_count > 100) {
		link_retransmit_failure(l, skb);
		return TIPC_LINK_DOWN_EVT;
	}
	skb_queue_walk(&l->transmq, skb) {
		if (!retransm)
			return 0;
		hdr = buf_msg(skb);
		_skb = __pskb_copy(skb, MIN_H_SIZE, GFP_ATOMIC);
		if (!_skb)
			return 0;
		hdr = buf_msg(_skb);
		msg_set_ack(hdr, l->rcv_nxt - 1);
		msg_set_bcast_ack(hdr, l->owner->bclink.last_in);
		_skb->priority = TC_PRIO_CONTROL;
		__skb_queue_tail(xmitq, _skb);
		retransm--;
		l->stats.retransmitted++;
	}
	return 0;
}

/* link_synch(): check if all packets arrived before the synch
 *               point have been consumed
 * Returns true if the parallel links are synched, otherwise false
 */
static bool link_synch(struct tipc_link *l)
{
	unsigned int post_synch;
	struct tipc_link *pl;

	pl  = tipc_parallel_link(l);
	if (pl == l)
		goto synched;

	/* Was last pre-synch packet added to input queue ? */
	if (less_eq(pl->rcv_nxt, l->synch_point))
		return false;

	/* Is it still in the input queue ? */
	post_synch = mod(pl->rcv_nxt - l->synch_point) - 1;
	if (skb_queue_len(pl->inputq) > post_synch)
		return false;
synched:
	l->exec_mode = TIPC_LINK_OPEN;
	return true;
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
		if (tipc_skb_queue_tail(link->inputq, skb, dport)) {
			node->inputq = link->inputq;
			node->action_flags |= TIPC_MSG_EVT;
		}
		return true;
	case NAME_DISTRIBUTOR:
		node->bclink.recv_permitted = true;
		node->namedq = link->namedq;
		skb_queue_tail(link->namedq, skb);
		if (skb_queue_len(link->namedq) == 1)
			node->action_flags |= TIPC_NAMED_MSG_EVT;
		return true;
	case MSG_BUNDLER:
	case TUNNEL_PROTOCOL:
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

	switch (msg_user(msg)) {
	case TUNNEL_PROTOCOL:
		if (msg_dup(msg)) {
			link->exec_mode = TIPC_LINK_TUNNEL;
			link->synch_point = msg_seqno(msg_get_wrapped(msg));
			kfree_skb(skb);
			break;
		}
		if (!tipc_link_failover_rcv(link, &skb))
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

static bool tipc_link_release_pkts(struct tipc_link *l, u16 acked)
{
	bool released = false;
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&l->transmq, skb, tmp) {
		if (more(buf_seqno(skb), acked))
			break;
		__skb_unlink(skb, &l->transmq);
		kfree_skb(skb);
		released = true;
	}
	return released;
}

/* tipc_link_rcv - process TIPC packets/messages arriving from off-node
 * @link: the link that should handle the message
 * @skb: TIPC packet
 * @xmitq: queue to place packets to be sent after this call
 */
int tipc_link_rcv(struct tipc_link *l, struct sk_buff *skb,
		  struct sk_buff_head *xmitq)
{
	struct sk_buff_head *arrvq = &l->deferdq;
	struct sk_buff *tmp;
	struct tipc_msg *hdr;
	u16 seqno, rcv_nxt;
	int rc = 0;

	if (unlikely(!__tipc_skb_queue_sorted(arrvq, skb))) {
		if (!(skb_queue_len(arrvq) % TIPC_NACK_INTV))
			tipc_link_build_proto_msg(l, STATE_MSG, 0,
						  0, 0, 0, xmitq);
		return rc;
	}

	skb_queue_walk_safe(arrvq, skb, tmp) {
		hdr = buf_msg(skb);

		/* Verify and update link state */
		if (unlikely(msg_user(hdr) == LINK_PROTOCOL)) {
			__skb_dequeue(arrvq);
			rc |= tipc_link_proto_rcv(l, skb, xmitq);
			continue;
		}

		if (unlikely(!link_working(l))) {
			rc |= tipc_link_fsm_evt(l, TRAFFIC_EVT, xmitq);
			if (!link_working(l)) {
				kfree_skb(__skb_dequeue(arrvq));
				return rc;
			}
		}

		l->silent_intv_cnt = 0;

		/* Forward queues and wake up waiting users */
		if (likely(tipc_link_release_pkts(l, msg_ack(hdr)))) {
			tipc_link_advance_backlog(l, xmitq);
			if (unlikely(!skb_queue_empty(&l->wakeupq)))
				link_prepare_wakeup(l);
		}

		/* Defer reception if there is a gap in the sequence */
		seqno = msg_seqno(hdr);
		rcv_nxt = l->rcv_nxt;
		if (unlikely(less(rcv_nxt, seqno))) {
			l->stats.deferred_recv++;
			return rc;
		}

		__skb_dequeue(arrvq);

		/* Drop if packet already received */
		if (unlikely(more(rcv_nxt, seqno))) {
			l->stats.duplicates++;
			kfree_skb(skb);
			return rc;
		}

		/* Synchronize with parallel link if applicable */
		if (unlikely(l->exec_mode == TIPC_LINK_TUNNEL))
			if (!msg_dup(hdr) && !link_synch(l)) {
				kfree_skb(skb);
				return rc;
			}

		/* Packet can be delivered */
		l->rcv_nxt++;
		l->stats.recv_info++;
		if (unlikely(!tipc_data_input(l, skb)))
			tipc_link_input(l, skb);

		/* Ack at regular intervals */
		if (unlikely(++l->rcv_unacked >= TIPC_MIN_LINK_WIN)) {
			l->rcv_unacked = 0;
			l->stats.sent_acks++;
			tipc_link_build_proto_msg(l, STATE_MSG,
						  0, 0, 0, 0, xmitq);
		}
	}
	return rc;
}

/**
 * tipc_link_defer_pkt - Add out-of-sequence message to deferred reception queue
 *
 * Returns increase in queue length (i.e. 0 or 1)
 */
u32 tipc_link_defer_pkt(struct sk_buff_head *list, struct sk_buff *skb)
{
	struct sk_buff *skb1;
	u16 seq_no = buf_seqno(skb);

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
		u16 curr_seqno = buf_seqno(skb1);

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
 * Send protocol message to the other endpoint.
 */
void tipc_link_proto_xmit(struct tipc_link *l, u32 msg_typ, int probe_msg,
			  u32 gap, u32 tolerance, u32 priority)
{
	struct sk_buff *skb = NULL;
	struct sk_buff_head xmitq;

	__skb_queue_head_init(&xmitq);
	tipc_link_build_proto_msg(l, msg_typ, probe_msg, gap,
				  tolerance, priority, &xmitq);
	skb = __skb_dequeue(&xmitq);
	if (!skb)
		return;
	tipc_bearer_send(l->owner->net, l->bearer_id, skb, &l->media_addr);
	l->rcv_unacked = 0;
	kfree_skb(skb);
}

/* tipc_link_build_proto_msg: prepare link protocol message for transmission
 */
static void tipc_link_build_proto_msg(struct tipc_link *l, int mtyp, bool probe,
				      u16 rcvgap, int tolerance, int priority,
				      struct sk_buff_head *xmitq)
{
	struct sk_buff *skb = NULL;
	struct tipc_msg *hdr = l->pmsg;
	u16 snd_nxt = l->snd_nxt;
	u16 rcv_nxt = l->rcv_nxt;
	u16 rcv_last = rcv_nxt - 1;
	int node_up = l->owner->bclink.recv_permitted;

	/* Don't send protocol message during reset or link failover */
	if (l->exec_mode == TIPC_LINK_BLOCKED)
		return;

	msg_set_type(hdr, mtyp);
	msg_set_net_plane(hdr, l->net_plane);
	msg_set_bcast_ack(hdr, l->owner->bclink.last_in);
	msg_set_last_bcast(hdr, tipc_bclink_get_last_sent(l->owner->net));
	msg_set_link_tolerance(hdr, tolerance);
	msg_set_linkprio(hdr, priority);
	msg_set_redundant_link(hdr, node_up);
	msg_set_seq_gap(hdr, 0);

	/* Compatibility: created msg must not be in sequence with pkt flow */
	msg_set_seqno(hdr, snd_nxt + U16_MAX / 2);

	if (mtyp == STATE_MSG) {
		if (!tipc_link_is_up(l))
			return;
		msg_set_next_sent(hdr, snd_nxt);

		/* Override rcvgap if there are packets in deferred queue */
		if (!skb_queue_empty(&l->deferdq))
			rcvgap = buf_seqno(skb_peek(&l->deferdq)) - rcv_nxt;
		if (rcvgap) {
			msg_set_seq_gap(hdr, rcvgap);
			l->stats.sent_nacks++;
		}
		msg_set_ack(hdr, rcv_last);
		msg_set_probe(hdr, probe);
		if (probe)
			l->stats.sent_probes++;
		l->stats.sent_states++;
	} else {
		/* RESET_MSG or ACTIVATE_MSG */
		msg_set_max_pkt(hdr, l->advertised_mtu);
		msg_set_ack(hdr, l->failover_checkpt - 1);
		msg_set_next_sent(hdr, 1);
	}
	skb = tipc_buf_acquire(msg_size(hdr));
	if (!skb)
		return;
	skb_copy_to_linear_data(skb, hdr, msg_size(hdr));
	skb->priority = TC_PRIO_CONTROL;
	__skb_queue_head(xmitq, skb);
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

	tunnel = node_active_link(l_ptr->owner, selector & 1);
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
	struct tipc_link *tunnel = node_active_link(l_ptr->owner, 0);
	struct tipc_msg tunnel_hdr;
	struct sk_buff *skb;
	int split_bundles;

	if (!tunnel)
		return;

	tipc_msg_init(link_own_addr(l_ptr), &tunnel_hdr, TUNNEL_PROTOCOL,
		      FAILOVER_MSG, INT_H_SIZE, l_ptr->addr);

	skb_queue_walk(&l_ptr->backlogq, skb) {
		msg_set_seqno(buf_msg(skb), l_ptr->snd_nxt);
		l_ptr->snd_nxt = mod(l_ptr->snd_nxt + 1);
	}
	skb_queue_splice_tail_init(&l_ptr->backlogq, &l_ptr->transmq);
	tipc_link_purge_backlog(l_ptr);
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

	split_bundles = (node_active_link(l_ptr->owner, 0) !=
			 node_active_link(l_ptr->owner, 0));

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
	u16 seqno;

	tipc_msg_init(link_own_addr(link), &tnl_hdr, TUNNEL_PROTOCOL,
		      SYNCH_MSG, INT_H_SIZE, link->addr);
	mcnt = skb_queue_len(&link->transmq) + skb_queue_len(&link->backlogq);
	msg_set_msgcnt(&tnl_hdr, mcnt);
	msg_set_bearer_id(&tnl_hdr, link->peer_bearer_id);

tunnel_queue:
	skb_queue_walk(queue, skb) {
		struct sk_buff *outskb;
		struct tipc_msg *msg = buf_msg(skb);
		u32 len = msg_size(msg);

		msg_set_ack(msg, mod(link->rcv_nxt - 1));
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
	seqno = link->snd_nxt;
	skb_queue_walk(&link->backlogq, skb) {
		msg_set_seqno(buf_msg(skb), seqno);
		seqno = mod(seqno + 1);
	}
	queue = &link->backlogq;
	goto tunnel_queue;
}

/*  tipc_link_failover_rcv(): Receive a tunnelled FAILOVER_MSG packet
 *  Owner node is locked.
 */
static bool tipc_link_failover_rcv(struct tipc_link *link,
				   struct sk_buff **skb)
{
	struct tipc_msg *msg = buf_msg(*skb);
	struct sk_buff *iskb = NULL;
	struct tipc_link *pl = NULL;
	int bearer_id = msg_bearer_id(msg);
	int pos = 0;

	if (msg_type(msg) != FAILOVER_MSG) {
		pr_warn("%sunknown tunnel pkt received\n", link_co_err);
		goto exit;
	}
	if (bearer_id >= MAX_BEARERS)
		goto exit;

	if (bearer_id == link->bearer_id)
		goto exit;

	pl = link->owner->links[bearer_id].link;
	if (pl && tipc_link_is_up(pl))
		tipc_link_reset(pl);

	if (link->failover_pkts == FIRST_FAILOVER)
		link->failover_pkts = msg_msgcnt(msg);

	/* Should we expect an inner packet? */
	if (!link->failover_pkts)
		goto exit;

	if (!tipc_msg_extract(*skb, &iskb, &pos)) {
		pr_warn("%sno inner failover pkt\n", link_co_err);
		*skb = NULL;
		goto exit;
	}
	link->failover_pkts--;
	*skb = NULL;

	/* Was this packet already delivered? */
	if (less(buf_seqno(iskb), link->failover_checkpt)) {
		kfree_skb(iskb);
		iskb = NULL;
		goto exit;
	}
	if (msg_user(buf_msg(iskb)) == MSG_FRAGMENTER) {
		link->stats.recv_fragments++;
		tipc_buf_append(&link->failover_skb, &iskb);
	}
exit:
	if (!link->failover_pkts && pl)
		pl->exec_mode = TIPC_LINK_OPEN;
	kfree_skb(*skb);
	*skb = iskb;
	return *skb;
}

/* tipc_link_proto_rcv(): receive link level protocol message :
 * Note that network plane id propagates through the network, and may
 * change at any time. The node with lowest numerical id determines
 * network plane
 */
static int tipc_link_proto_rcv(struct tipc_link *l, struct sk_buff *skb,
			       struct sk_buff_head *xmitq)
{
	struct tipc_msg *hdr = buf_msg(skb);
	u16 rcvgap = 0;
	u16 nacked_gap = msg_seq_gap(hdr);
	u16 peers_snd_nxt =  msg_next_sent(hdr);
	u16 peers_tol = msg_link_tolerance(hdr);
	u16 peers_prio = msg_linkprio(hdr);
	char *if_name;
	int rc = 0;

	if (l->exec_mode == TIPC_LINK_BLOCKED)
		goto exit;

	if (link_own_addr(l) > msg_prevnode(hdr))
		l->net_plane = msg_net_plane(hdr);

	switch (msg_type(hdr)) {
	case RESET_MSG:

		/* Ignore duplicate RESET with old session number */
		if ((less_eq(msg_session(hdr), l->peer_session)) &&
		    (l->peer_session != WILDCARD_SESSION))
			break;
		/* fall thru' */
	case ACTIVATE_MSG:

		/* Complete own link name with peer's interface name */
		if_name =  strrchr(l->name, ':') + 1;
		if (sizeof(l->name) - (if_name - l->name) <= TIPC_MAX_IF_NAME)
			break;
		if (msg_data_sz(hdr) < TIPC_MAX_IF_NAME)
			break;
		strncpy(if_name, msg_data(hdr),	TIPC_MAX_IF_NAME);

		/* Update own tolerance if peer indicates a non-zero value */
		if (in_range(peers_tol, TIPC_MIN_LINK_TOL, TIPC_MAX_LINK_TOL))
			l->tolerance = peers_tol;

		/* Update own priority if peer's priority is higher */
		if (in_range(peers_prio, l->priority + 1, TIPC_MAX_LINK_PRI))
			l->priority = peers_prio;

		l->peer_session = msg_session(hdr);
		l->peer_bearer_id = msg_bearer_id(hdr);
		rc = tipc_link_fsm_evt(l, msg_type(hdr), xmitq);
		if (l->mtu > msg_max_pkt(hdr))
			l->mtu = msg_max_pkt(hdr);
		break;
	case STATE_MSG:
		/* Update own tolerance if peer indicates a non-zero value */
		if (in_range(peers_tol, TIPC_MIN_LINK_TOL, TIPC_MAX_LINK_TOL))
			l->tolerance = peers_tol;

		l->silent_intv_cnt = 0;
		l->stats.recv_states++;
		if (msg_probe(hdr))
			l->stats.recv_probes++;
		rc = tipc_link_fsm_evt(l, TRAFFIC_EVT, xmitq);
		if (!tipc_link_is_up(l))
			break;

		/* Has peer sent packets we haven't received yet ? */
		if (more(peers_snd_nxt, l->rcv_nxt))
			rcvgap = peers_snd_nxt - l->rcv_nxt;
		if (rcvgap || (msg_probe(hdr)))
			tipc_link_build_proto_msg(l, STATE_MSG, 0, rcvgap,
						  0, 0, xmitq);
		tipc_link_release_pkts(l, msg_ack(hdr));

		/* If NACK, retransmit will now start at right position */
		if (nacked_gap) {
			rc |= tipc_link_retransm(l, nacked_gap, xmitq);
			l->stats.recv_nacks++;
		}
		tipc_link_advance_backlog(l, xmitq);
		if (unlikely(!skb_queue_empty(&l->wakeupq)))
			link_prepare_wakeup(l);
	}
exit:
	kfree_skb(skb);
	return rc;
}

void tipc_link_set_queue_limits(struct tipc_link *l, u32 win)
{
	int max_bulk = TIPC_MAX_PUBLICATIONS / (l->mtu / ITEM_SIZE);

	l->window = win;
	l->backlog[TIPC_LOW_IMPORTANCE].limit      = win / 2;
	l->backlog[TIPC_MEDIUM_IMPORTANCE].limit   = win;
	l->backlog[TIPC_HIGH_IMPORTANCE].limit     = win / 2 * 3;
	l->backlog[TIPC_CRITICAL_IMPORTANCE].limit = win * 2;
	l->backlog[TIPC_SYSTEM_IMPORTANCE].limit   = max_bulk;
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
			l_ptr = n_ptr->links[i].link;
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
	l_ptr->stats.sent_info = l_ptr->snd_nxt;
	l_ptr->stats.recv_info = l_ptr->rcv_nxt;
}

static void link_print(struct tipc_link *l, const char *str)
{
	struct sk_buff *hskb = skb_peek(&l->transmq);
	u16 head = hskb ? msg_seqno(buf_msg(hskb)) : l->snd_nxt;
	u16 tail = l->snd_nxt - 1;

	pr_info("%s Link <%s>:", str, l->name);

	if (link_probing(l))
		pr_cont(":P\n");
	else if (link_establishing(l))
		pr_cont(":E\n");
	else if (link_resetting(l))
		pr_cont(":R\n");
	else if (link_working(l))
		pr_cont(":W\n");
	else
		pr_cont("\n");

	pr_info("XMTQ: %u [%u-%u], BKLGQ: %u, SNDNX: %u, RCVNX: %u\n",
		skb_queue_len(&l->transmq), head, tail,
		skb_queue_len(&l->backlogq), l->snd_nxt, l->rcv_nxt);
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

	if (strcmp(name, tipc_bclink_name) == 0)
		return tipc_nl_bc_link_set(net, attrs);

	node = tipc_link_find_owner(net, name, &bearer_id);
	if (!node)
		return -EINVAL;

	tipc_node_lock(node);

	link = node->links[bearer_id].link;
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
			link->tolerance = tol;
			tipc_link_proto_xmit(link, STATE_MSG, 0, 0, tol, 0);
		}
		if (props[TIPC_NLA_PROP_PRIO]) {
			u32 prio;

			prio = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
			link->priority = prio;
			tipc_link_proto_xmit(link, STATE_MSG, 0, 0, 0, prio);
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
			      struct tipc_link *link, int nlflags)
{
	int err;
	void *hdr;
	struct nlattr *attrs;
	struct nlattr *prop;
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  nlflags, TIPC_NL_LINK_GET);
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
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_MTU, link->mtu))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_RX, link->rcv_nxt))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_TX, link->snd_nxt))
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
			link->window))
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

		if (!node->links[i].link)
			continue;

		err = __tipc_nl_add_link(net, msg,
					 node->links[i].link, NLM_F_MULTI);
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
		tipc_node_put(node);

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
	struct tipc_nl_msg msg;
	char *name;
	int err;

	msg.portid = info->snd_portid;
	msg.seq = info->snd_seq;

	if (!info->attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;
	name = nla_data(info->attrs[TIPC_NLA_LINK_NAME]);

	msg.skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg.skb)
		return -ENOMEM;

	if (strcmp(name, tipc_bclink_name) == 0) {
		err = tipc_nl_add_bc_link(net, &msg);
		if (err) {
			nlmsg_free(msg.skb);
			return err;
		}
	} else {
		int bearer_id;
		struct tipc_node *node;
		struct tipc_link *link;

		node = tipc_link_find_owner(net, name, &bearer_id);
		if (!node)
			return -EINVAL;

		tipc_node_lock(node);
		link = node->links[bearer_id].link;
		if (!link) {
			tipc_node_unlock(node);
			nlmsg_free(msg.skb);
			return -EINVAL;
		}

		err = __tipc_nl_add_link(net, &msg, link, 0);
		tipc_node_unlock(node);
		if (err) {
			nlmsg_free(msg.skb);
			return err;
		}
	}

	return genlmsg_reply(msg.skb, info);
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

	link = node->links[bearer_id].link;
	if (!link) {
		tipc_node_unlock(node);
		return -EINVAL;
	}

	link_reset_statistics(link);

	tipc_node_unlock(node);

	return 0;
}

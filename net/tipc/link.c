/*
 * net/tipc/link.c: TIPC link code
 *
 * Copyright (c) 1996-2007, 2012-2014, Ericsson AB
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
#include "port.h"
#include "socket.h"
#include "name_distr.h"
#include "discover.h"
#include "config.h"

#include <linux/pkt_sched.h>

/*
 * Error message prefixes
 */
static const char *link_co_err = "Link changeover error, ";
static const char *link_rst_msg = "Resetting link ";
static const char *link_unk_evt = "Unknown link event ";

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

static void link_handle_out_of_seq_msg(struct tipc_link *l_ptr,
				       struct sk_buff *buf);
static void tipc_link_proto_rcv(struct tipc_link *l_ptr, struct sk_buff *buf);
static int  tipc_link_tunnel_rcv(struct tipc_node *n_ptr,
				 struct sk_buff **buf);
static void link_set_supervision_props(struct tipc_link *l_ptr, u32 tolerance);
static void link_state_event(struct tipc_link *l_ptr, u32 event);
static void link_reset_statistics(struct tipc_link *l_ptr);
static void link_print(struct tipc_link *l_ptr, const char *str);
static int tipc_link_frag_xmit(struct tipc_link *l_ptr, struct sk_buff *buf);
static void tipc_link_sync_xmit(struct tipc_link *l);
static void tipc_link_sync_rcv(struct tipc_node *n, struct sk_buff *buf);
static int tipc_link_input(struct tipc_link *l, struct sk_buff *buf);
static int tipc_link_prepare_input(struct tipc_link *l, struct sk_buff **buf);

/*
 *  Simple link routines
 */
static unsigned int align(unsigned int i)
{
	return (i + 3) & ~3u;
}

static void link_init_max_pkt(struct tipc_link *l_ptr)
{
	struct tipc_bearer *b_ptr;
	u32 max_pkt;

	rcu_read_lock();
	b_ptr = rcu_dereference_rtnl(bearer_list[l_ptr->bearer_id]);
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

static u32 link_next_sent(struct tipc_link *l_ptr)
{
	if (l_ptr->next_out)
		return buf_seqno(l_ptr->next_out);
	return mod(l_ptr->next_out_no);
}

static u32 link_last_sent(struct tipc_link *l_ptr)
{
	return mod(link_next_sent(l_ptr) - 1);
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
static void link_timeout(struct tipc_link *l_ptr)
{
	tipc_node_lock(l_ptr->owner);

	/* update counters used in statistical profiling of send traffic */
	l_ptr->stats.accu_queue_sz += l_ptr->out_queue_size;
	l_ptr->stats.queue_sz_counts++;

	if (l_ptr->first_out) {
		struct tipc_msg *msg = buf_msg(l_ptr->first_out);
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

	if (l_ptr->next_out)
		tipc_link_push_queue(l_ptr);

	tipc_node_unlock(l_ptr->owner);
}

static void link_set_timer(struct tipc_link *l_ptr, u32 time)
{
	k_start_timer(&l_ptr->timer, time);
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
	struct tipc_link *l_ptr;
	struct tipc_msg *msg;
	char *if_name;
	char addr_string[16];
	u32 peer = n_ptr->addr;

	if (n_ptr->link_cnt >= 2) {
		tipc_addr_string_fill(addr_string, n_ptr->addr);
		pr_err("Attempt to establish third link to %s\n", addr_string);
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

	l_ptr->addr = peer;
	if_name = strchr(b_ptr->name, ':') + 1;
	sprintf(l_ptr->name, "%u.%u.%u:%s-%u.%u.%u:unknown",
		tipc_zone(tipc_own_addr), tipc_cluster(tipc_own_addr),
		tipc_node(tipc_own_addr),
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
	tipc_msg_init(msg, LINK_PROTOCOL, RESET_MSG, INT_H_SIZE, l_ptr->addr);
	msg_set_size(msg, sizeof(l_ptr->proto_msg));
	msg_set_session(msg, (tipc_random & 0xffff));
	msg_set_bearer_id(msg, b_ptr->identity);
	strcpy((char *)msg_data(msg), if_name);

	l_ptr->priority = b_ptr->priority;
	tipc_link_set_queue_limits(l_ptr, b_ptr->window);

	l_ptr->net_plane = b_ptr->net_plane;
	link_init_max_pkt(l_ptr);

	l_ptr->next_out_no = 1;
	INIT_LIST_HEAD(&l_ptr->waiting_ports);

	link_reset_statistics(l_ptr);

	tipc_node_attach_link(n_ptr, l_ptr);

	k_init_timer(&l_ptr->timer, (Handler)link_timeout,
		     (unsigned long)l_ptr);

	link_state_event(l_ptr, STARTING_EVT);

	return l_ptr;
}

void tipc_link_delete_list(unsigned int bearer_id, bool shutting_down)
{
	struct tipc_link *l_ptr;
	struct tipc_node *n_ptr;

	rcu_read_lock();
	list_for_each_entry_rcu(n_ptr, &tipc_node_list, list) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->links[bearer_id];
		if (l_ptr) {
			tipc_link_reset(l_ptr);
			if (shutting_down || !tipc_node_is_up(n_ptr)) {
				tipc_node_detach_link(l_ptr->owner, l_ptr);
				tipc_link_reset_fragments(l_ptr);
				tipc_node_unlock(n_ptr);

				/* Nobody else can access this link now: */
				del_timer_sync(&l_ptr->timer);
				kfree(l_ptr);
			} else {
				/* Detach/delete when failover is finished: */
				l_ptr->flags |= LINK_STOPPED;
				tipc_node_unlock(n_ptr);
				del_timer_sync(&l_ptr->timer);
			}
			continue;
		}
		tipc_node_unlock(n_ptr);
	}
	rcu_read_unlock();
}

/**
 * link_schedule_port - schedule port for deferred sending
 * @l_ptr: pointer to link
 * @origport: reference to sending port
 * @sz: amount of data to be sent
 *
 * Schedules port for renewed sending of messages after link congestion
 * has abated.
 */
static int link_schedule_port(struct tipc_link *l_ptr, u32 origport, u32 sz)
{
	struct tipc_port *p_ptr;
	struct tipc_sock *tsk;

	spin_lock_bh(&tipc_port_list_lock);
	p_ptr = tipc_port_lock(origport);
	if (p_ptr) {
		if (!list_empty(&p_ptr->wait_list))
			goto exit;
		tsk = tipc_port_to_sock(p_ptr);
		tsk->link_cong = 1;
		p_ptr->waiting_pkts = 1 + ((sz - 1) / l_ptr->max_pkt);
		list_add_tail(&p_ptr->wait_list, &l_ptr->waiting_ports);
		l_ptr->stats.link_congs++;
exit:
		tipc_port_unlock(p_ptr);
	}
	spin_unlock_bh(&tipc_port_list_lock);
	return -ELINKCONG;
}

void tipc_link_wakeup_ports(struct tipc_link *l_ptr, int all)
{
	struct tipc_port *p_ptr;
	struct tipc_sock *tsk;
	struct tipc_port *temp_p_ptr;
	int win = l_ptr->queue_limit[0] - l_ptr->out_queue_size;

	if (all)
		win = 100000;
	if (win <= 0)
		return;
	if (!spin_trylock_bh(&tipc_port_list_lock))
		return;
	if (link_congested(l_ptr))
		goto exit;
	list_for_each_entry_safe(p_ptr, temp_p_ptr, &l_ptr->waiting_ports,
				 wait_list) {
		if (win <= 0)
			break;
		tsk = tipc_port_to_sock(p_ptr);
		list_del_init(&p_ptr->wait_list);
		spin_lock_bh(p_ptr->lock);
		tsk->link_cong = 0;
		tipc_sock_wakeup(tsk);
		win -= p_ptr->waiting_pkts;
		spin_unlock_bh(p_ptr->lock);
	}

exit:
	spin_unlock_bh(&tipc_port_list_lock);
}

/**
 * link_release_outqueue - purge link's outbound message queue
 * @l_ptr: pointer to link
 */
static void link_release_outqueue(struct tipc_link *l_ptr)
{
	kfree_skb_list(l_ptr->first_out);
	l_ptr->first_out = NULL;
	l_ptr->out_queue_size = 0;
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
	kfree_skb_list(l_ptr->oldest_deferred_in);
	kfree_skb_list(l_ptr->first_out);
	tipc_link_reset_fragments(l_ptr);
	kfree_skb(l_ptr->proto_msg_queue);
	l_ptr->proto_msg_queue = NULL;
}

void tipc_link_reset(struct tipc_link *l_ptr)
{
	u32 prev_state = l_ptr->state;
	u32 checkpoint = l_ptr->next_in_no;
	int was_active_link = tipc_link_is_active(l_ptr);

	msg_set_session(l_ptr->pmsg, ((msg_session(l_ptr->pmsg) + 1) & 0xffff));

	/* Link is down, accept any session */
	l_ptr->peer_session = INVALID_SESSION;

	/* Prepare for max packet size negotiation */
	link_init_max_pkt(l_ptr);

	l_ptr->state = RESET_UNKNOWN;

	if ((prev_state == RESET_UNKNOWN) || (prev_state == RESET_RESET))
		return;

	tipc_node_link_down(l_ptr->owner, l_ptr);
	tipc_bearer_remove_dest(l_ptr->bearer_id, l_ptr->addr);

	if (was_active_link && tipc_node_active_links(l_ptr->owner)) {
		l_ptr->reset_checkpoint = checkpoint;
		l_ptr->exp_msg_count = START_CHANGEOVER;
	}

	/* Clean up all queues: */
	link_release_outqueue(l_ptr);
	kfree_skb(l_ptr->proto_msg_queue);
	l_ptr->proto_msg_queue = NULL;
	kfree_skb_list(l_ptr->oldest_deferred_in);
	if (!list_empty(&l_ptr->waiting_ports))
		tipc_link_wakeup_ports(l_ptr, 1);

	l_ptr->retransm_queue_head = 0;
	l_ptr->retransm_queue_size = 0;
	l_ptr->last_out = NULL;
	l_ptr->first_out = NULL;
	l_ptr->next_out = NULL;
	l_ptr->unacked_window = 0;
	l_ptr->checkpoint = 1;
	l_ptr->next_out_no = 1;
	l_ptr->deferred_inqueue_sz = 0;
	l_ptr->oldest_deferred_in = NULL;
	l_ptr->newest_deferred_in = NULL;
	l_ptr->fsm_msg_cnt = 0;
	l_ptr->stale_count = 0;
	link_reset_statistics(l_ptr);
}

void tipc_link_reset_list(unsigned int bearer_id)
{
	struct tipc_link *l_ptr;
	struct tipc_node *n_ptr;

	rcu_read_lock();
	list_for_each_entry_rcu(n_ptr, &tipc_node_list, list) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->links[bearer_id];
		if (l_ptr)
			tipc_link_reset(l_ptr);
		tipc_node_unlock(n_ptr);
	}
	rcu_read_unlock();
}

static void link_activate(struct tipc_link *l_ptr)
{
	l_ptr->next_in_no = l_ptr->stats.recv_info = 1;
	tipc_node_link_up(l_ptr->owner, l_ptr);
	tipc_bearer_add_dest(l_ptr->bearer_id, l_ptr->addr);
}

/**
 * link_state_event - link finite state machine
 * @l_ptr: pointer to link
 * @event: state machine event to process
 */
static void link_state_event(struct tipc_link *l_ptr, unsigned int event)
{
	struct tipc_link *other;
	u32 cont_intv = l_ptr->continuity_interval;

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
			pr_info("%s<%s>, requested by peer\n", link_rst_msg,
				l_ptr->name);
			tipc_link_reset(l_ptr);
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_proto_xmit(l_ptr, ACTIVATE_MSG,
					     0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		default:
			pr_err("%s%u in WW state\n", link_unk_evt, event);
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
			pr_info("%s<%s>, requested by peer while probing\n",
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
				pr_warn("%s<%s>, peer not responding\n",
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
			/* fall through */
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

/*
 * link_bundle_buf(): Append contents of a buffer to
 * the tail of an existing one.
 */
static int link_bundle_buf(struct tipc_link *l_ptr, struct sk_buff *bundler,
			   struct sk_buff *buf)
{
	struct tipc_msg *bundler_msg = buf_msg(bundler);
	struct tipc_msg *msg = buf_msg(buf);
	u32 size = msg_size(msg);
	u32 bundle_size = msg_size(bundler_msg);
	u32 to_pos = align(bundle_size);
	u32 pad = to_pos - bundle_size;

	if (msg_user(bundler_msg) != MSG_BUNDLER)
		return 0;
	if (msg_type(bundler_msg) != OPEN_MSG)
		return 0;
	if (skb_tailroom(bundler) < (pad + size))
		return 0;
	if (l_ptr->max_pkt < (to_pos + size))
		return 0;

	skb_put(bundler, pad + size);
	skb_copy_to_linear_data_offset(bundler, to_pos, buf->data, size);
	msg_set_size(bundler_msg, to_pos + size);
	msg_set_msgcnt(bundler_msg, msg_msgcnt(bundler_msg) + 1);
	kfree_skb(buf);
	l_ptr->stats.sent_bundled++;
	return 1;
}

static void link_add_to_outqueue(struct tipc_link *l_ptr,
				 struct sk_buff *buf,
				 struct tipc_msg *msg)
{
	u32 ack = mod(l_ptr->next_in_no - 1);
	u32 seqno = mod(l_ptr->next_out_no++);

	msg_set_word(msg, 2, ((ack << 16) | seqno));
	msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
	buf->next = NULL;
	if (l_ptr->first_out) {
		l_ptr->last_out->next = buf;
		l_ptr->last_out = buf;
	} else
		l_ptr->first_out = l_ptr->last_out = buf;

	l_ptr->out_queue_size++;
	if (l_ptr->out_queue_size > l_ptr->stats.max_queue_sz)
		l_ptr->stats.max_queue_sz = l_ptr->out_queue_size;
}

static void link_add_chain_to_outqueue(struct tipc_link *l_ptr,
				       struct sk_buff *buf_chain,
				       u32 long_msgno)
{
	struct sk_buff *buf;
	struct tipc_msg *msg;

	if (!l_ptr->next_out)
		l_ptr->next_out = buf_chain;
	while (buf_chain) {
		buf = buf_chain;
		buf_chain = buf_chain->next;

		msg = buf_msg(buf);
		msg_set_long_msgno(msg, long_msgno);
		link_add_to_outqueue(l_ptr, buf, msg);
	}
}

/*
 * tipc_link_xmit() is the 'full path' for messages, called from
 * inside TIPC when the 'fast path' in tipc_send_xmit
 * has failed, and from link_send()
 */
int __tipc_link_xmit(struct tipc_link *l_ptr, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	u32 size = msg_size(msg);
	u32 dsz = msg_data_sz(msg);
	u32 queue_size = l_ptr->out_queue_size;
	u32 imp = tipc_msg_tot_importance(msg);
	u32 queue_limit = l_ptr->queue_limit[imp];
	u32 max_packet = l_ptr->max_pkt;

	/* Match msg importance against queue limits: */
	if (unlikely(queue_size >= queue_limit)) {
		if (imp <= TIPC_CRITICAL_IMPORTANCE) {
			link_schedule_port(l_ptr, msg_origport(msg), size);
			kfree_skb(buf);
			return -ELINKCONG;
		}
		kfree_skb(buf);
		if (imp > CONN_MANAGER) {
			pr_warn("%s<%s>, send queue full", link_rst_msg,
				l_ptr->name);
			tipc_link_reset(l_ptr);
		}
		return dsz;
	}

	/* Fragmentation needed ? */
	if (size > max_packet)
		return tipc_link_frag_xmit(l_ptr, buf);

	/* Packet can be queued or sent. */
	if (likely(!link_congested(l_ptr))) {
		link_add_to_outqueue(l_ptr, buf, msg);

		tipc_bearer_send(l_ptr->bearer_id, buf, &l_ptr->media_addr);
		l_ptr->unacked_window = 0;
		return dsz;
	}
	/* Congestion: can message be bundled ? */
	if ((msg_user(msg) != CHANGEOVER_PROTOCOL) &&
	    (msg_user(msg) != MSG_FRAGMENTER)) {

		/* Try adding message to an existing bundle */
		if (l_ptr->next_out &&
		    link_bundle_buf(l_ptr, l_ptr->last_out, buf))
			return dsz;

		/* Try creating a new bundle */
		if (size <= max_packet * 2 / 3) {
			struct sk_buff *bundler = tipc_buf_acquire(max_packet);
			struct tipc_msg bundler_hdr;

			if (bundler) {
				tipc_msg_init(&bundler_hdr, MSG_BUNDLER, OPEN_MSG,
					 INT_H_SIZE, l_ptr->addr);
				skb_copy_to_linear_data(bundler, &bundler_hdr,
							INT_H_SIZE);
				skb_trim(bundler, INT_H_SIZE);
				link_bundle_buf(l_ptr, bundler, buf);
				buf = bundler;
				msg = buf_msg(buf);
				l_ptr->stats.sent_bundles++;
			}
		}
	}
	if (!l_ptr->next_out)
		l_ptr->next_out = buf;
	link_add_to_outqueue(l_ptr, buf, msg);
	return dsz;
}

/*
 * tipc_link_xmit(): same as __tipc_link_xmit(), but the link to use
 * has not been selected yet, and the the owner node is not locked
 * Called by TIPC internal users, e.g. the name distributor
 */
int tipc_link_xmit(struct sk_buff *buf, u32 dest, u32 selector)
{
	struct tipc_link *l_ptr;
	struct tipc_node *n_ptr;
	int res = -ELINKCONG;

	n_ptr = tipc_node_find(dest);
	if (n_ptr) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector & 1];
		if (l_ptr)
			res = __tipc_link_xmit(l_ptr, buf);
		else
			kfree_skb(buf);
		tipc_node_unlock(n_ptr);
	} else {
		kfree_skb(buf);
	}
	return res;
}

/* tipc_link_cong: determine return value and how to treat the
 * sent buffer during link congestion.
 * - For plain, errorless user data messages we keep the buffer and
 *   return -ELINKONG.
 * - For all other messages we discard the buffer and return -EHOSTUNREACH
 * - For TIPC internal messages we also reset the link
 */
static int tipc_link_cong(struct tipc_link *link, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	uint psz = msg_size(msg);
	uint imp = tipc_msg_tot_importance(msg);
	u32 oport = msg_tot_origport(msg);

	if (likely(imp <= TIPC_CRITICAL_IMPORTANCE)) {
		if (!msg_errcode(msg) && !msg_reroute_cnt(msg)) {
			link_schedule_port(link, oport, psz);
			return -ELINKCONG;
		}
	} else {
		pr_warn("%s<%s>, send queue full", link_rst_msg, link->name);
		tipc_link_reset(link);
	}
	kfree_skb_list(buf);
	return -EHOSTUNREACH;
}

/**
 * __tipc_link_xmit2(): same as tipc_link_xmit2, but destlink is known & locked
 * @link: link to use
 * @buf: chain of buffers containing message
 * Consumes the buffer chain, except when returning -ELINKCONG
 * Returns 0 if success, otherwise errno: -ELINKCONG, -EMSGSIZE (plain socket
 * user data messages) or -EHOSTUNREACH (all other messages/senders)
 * Only the socket functions tipc_send_stream() and tipc_send_packet() need
 * to act on the return value, since they may need to do more send attempts.
 */
int __tipc_link_xmit2(struct tipc_link *link, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	uint psz = msg_size(msg);
	uint qsz = link->out_queue_size;
	uint sndlim = link->queue_limit[0];
	uint imp = tipc_msg_tot_importance(msg);
	uint mtu = link->max_pkt;
	uint ack = mod(link->next_in_no - 1);
	uint seqno = link->next_out_no;
	uint bc_last_in = link->owner->bclink.last_in;
	struct tipc_media_addr *addr = &link->media_addr;
	struct sk_buff *next = buf->next;

	/* Match queue limits against msg importance: */
	if (unlikely(qsz >= link->queue_limit[imp]))
		return tipc_link_cong(link, buf);

	/* Has valid packet limit been used ? */
	if (unlikely(psz > mtu)) {
		kfree_skb_list(buf);
		return -EMSGSIZE;
	}

	/* Prepare each packet for sending, and add to outqueue: */
	while (buf) {
		next = buf->next;
		msg = buf_msg(buf);
		msg_set_word(msg, 2, ((ack << 16) | mod(seqno)));
		msg_set_bcast_ack(msg, bc_last_in);

		if (!link->first_out) {
			link->first_out = buf;
		} else if (qsz < sndlim) {
			link->last_out->next = buf;
		} else if (tipc_msg_bundle(link->last_out, buf, mtu)) {
			link->stats.sent_bundled++;
			buf = next;
			next = buf->next;
			continue;
		} else if (tipc_msg_make_bundle(&buf, mtu, link->addr)) {
			link->stats.sent_bundled++;
			link->stats.sent_bundles++;
			link->last_out->next = buf;
			if (!link->next_out)
				link->next_out = buf;
		} else {
			link->last_out->next = buf;
			if (!link->next_out)
				link->next_out = buf;
		}

		/* Send packet if possible: */
		if (likely(++qsz <= sndlim)) {
			tipc_bearer_send(link->bearer_id, buf, addr);
			link->next_out = next;
			link->unacked_window = 0;
		}
		seqno++;
		link->last_out = buf;
		buf = next;
	}
	link->next_out_no = seqno;
	link->out_queue_size = qsz;
	return 0;
}

/**
 * tipc_link_xmit2() is the general link level function for message sending
 * @buf: chain of buffers containing message
 * @dsz: amount of user data to be sent
 * @dnode: address of destination node
 * @selector: a number used for deterministic link selection
 * Consumes the buffer chain, except when returning -ELINKCONG
 * Returns 0 if success, otherwise errno: -ELINKCONG,-EHOSTUNREACH,-EMSGSIZE
 */
int tipc_link_xmit2(struct sk_buff *buf, u32 dnode, u32 selector)
{
	struct tipc_link *link = NULL;
	struct tipc_node *node;
	int rc = -EHOSTUNREACH;

	node = tipc_node_find(dnode);
	if (node) {
		tipc_node_lock(node);
		link = node->active_links[selector & 1];
		if (link)
			rc = __tipc_link_xmit2(link, buf);
		tipc_node_unlock(node);
	}

	if (link)
		return rc;

	if (likely(in_own_node(dnode)))
		return tipc_sk_rcv(buf);

	kfree_skb_list(buf);
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
	struct sk_buff *buf;
	struct tipc_msg *msg;

	buf = tipc_buf_acquire(INT_H_SIZE);
	if (!buf)
		return;

	msg = buf_msg(buf);
	tipc_msg_init(msg, BCAST_PROTOCOL, STATE_MSG, INT_H_SIZE, link->addr);
	msg_set_last_bcast(msg, link->owner->bclink.acked);
	__tipc_link_xmit2(link, buf);
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
 * tipc_link_push_packet: Push one unsent packet to the media
 */
static u32 tipc_link_push_packet(struct tipc_link *l_ptr)
{
	struct sk_buff *buf = l_ptr->first_out;
	u32 r_q_size = l_ptr->retransm_queue_size;
	u32 r_q_head = l_ptr->retransm_queue_head;

	/* Step to position where retransmission failed, if any,    */
	/* consider that buffers may have been released in meantime */
	if (r_q_size && buf) {
		u32 last = lesser(mod(r_q_head + r_q_size),
				  link_last_sent(l_ptr));
		u32 first = buf_seqno(buf);

		while (buf && less(first, r_q_head)) {
			first = mod(first + 1);
			buf = buf->next;
		}
		l_ptr->retransm_queue_head = r_q_head = first;
		l_ptr->retransm_queue_size = r_q_size = mod(last - first);
	}

	/* Continue retransmission now, if there is anything: */
	if (r_q_size && buf) {
		msg_set_ack(buf_msg(buf), mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(buf_msg(buf), l_ptr->owner->bclink.last_in);
		tipc_bearer_send(l_ptr->bearer_id, buf, &l_ptr->media_addr);
		l_ptr->retransm_queue_head = mod(++r_q_head);
		l_ptr->retransm_queue_size = --r_q_size;
		l_ptr->stats.retransmitted++;
		return 0;
	}

	/* Send deferred protocol message, if any: */
	buf = l_ptr->proto_msg_queue;
	if (buf) {
		msg_set_ack(buf_msg(buf), mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(buf_msg(buf), l_ptr->owner->bclink.last_in);
		tipc_bearer_send(l_ptr->bearer_id, buf, &l_ptr->media_addr);
		l_ptr->unacked_window = 0;
		kfree_skb(buf);
		l_ptr->proto_msg_queue = NULL;
		return 0;
	}

	/* Send one deferred data message, if send window not full: */
	buf = l_ptr->next_out;
	if (buf) {
		struct tipc_msg *msg = buf_msg(buf);
		u32 next = msg_seqno(msg);
		u32 first = buf_seqno(l_ptr->first_out);

		if (mod(next - first) < l_ptr->queue_limit[0]) {
			msg_set_ack(msg, mod(l_ptr->next_in_no - 1));
			msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
			tipc_bearer_send(l_ptr->bearer_id, buf,
					 &l_ptr->media_addr);
			if (msg_user(msg) == MSG_BUNDLER)
				msg_set_type(msg, BUNDLE_CLOSED);
			l_ptr->next_out = buf->next;
			return 0;
		}
	}
	return 1;
}

/*
 * push_queue(): push out the unsent messages of a link where
 *               congestion has abated. Node is locked
 */
void tipc_link_push_queue(struct tipc_link *l_ptr)
{
	u32 res;

	do {
		res = tipc_link_push_packet(l_ptr);
	} while (!res);
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

		n_ptr = tipc_bclink_retransmit_to();
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

		tipc_bclink_set_flags(TIPC_BCLINK_RESET);
		l_ptr->stale_count = 0;
	}
}

void tipc_link_retransmit(struct tipc_link *l_ptr, struct sk_buff *buf,
			  u32 retransmits)
{
	struct tipc_msg *msg;

	if (!buf)
		return;

	msg = buf_msg(buf);

	/* Detect repeated retransmit failures */
	if (l_ptr->last_retransmitted == msg_seqno(msg)) {
		if (++l_ptr->stale_count > 100) {
			link_retransmit_failure(l_ptr, buf);
			return;
		}
	} else {
		l_ptr->last_retransmitted = msg_seqno(msg);
		l_ptr->stale_count = 1;
	}

	while (retransmits && (buf != l_ptr->next_out) && buf) {
		msg = buf_msg(buf);
		msg_set_ack(msg, mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
		tipc_bearer_send(l_ptr->bearer_id, buf, &l_ptr->media_addr);
		buf = buf->next;
		retransmits--;
		l_ptr->stats.retransmitted++;
	}

	l_ptr->retransm_queue_head = l_ptr->retransm_queue_size = 0;
}

/**
 * link_insert_deferred_queue - insert deferred messages back into receive chain
 */
static struct sk_buff *link_insert_deferred_queue(struct tipc_link *l_ptr,
						  struct sk_buff *buf)
{
	u32 seq_no;

	if (l_ptr->oldest_deferred_in == NULL)
		return buf;

	seq_no = buf_seqno(l_ptr->oldest_deferred_in);
	if (seq_no == mod(l_ptr->next_in_no)) {
		l_ptr->newest_deferred_in->next = buf;
		buf = l_ptr->oldest_deferred_in;
		l_ptr->oldest_deferred_in = NULL;
		l_ptr->deferred_inqueue_sz = 0;
	}
	return buf;
}

/**
 * link_recv_buf_validate - validate basic format of received message
 *
 * This routine ensures a TIPC message has an acceptable header, and at least
 * as much data as the header indicates it should.  The routine also ensures
 * that the entire message header is stored in the main fragment of the message
 * buffer, to simplify future access to message header fields.
 *
 * Note: Having extra info present in the message header or data areas is OK.
 * TIPC will ignore the excess, under the assumption that it is optional info
 * introduced by a later release of the protocol.
 */
static int link_recv_buf_validate(struct sk_buff *buf)
{
	static u32 min_data_hdr_size[8] = {
		SHORT_H_SIZE, MCAST_H_SIZE, NAMED_H_SIZE, BASIC_H_SIZE,
		MAX_H_SIZE, MAX_H_SIZE, MAX_H_SIZE, MAX_H_SIZE
		};

	struct tipc_msg *msg;
	u32 tipc_hdr[2];
	u32 size;
	u32 hdr_size;
	u32 min_hdr_size;

	/* If this packet comes from the defer queue, the skb has already
	 * been validated
	 */
	if (unlikely(TIPC_SKB_CB(buf)->deferred))
		return 1;

	if (unlikely(buf->len < MIN_H_SIZE))
		return 0;

	msg = skb_header_pointer(buf, 0, sizeof(tipc_hdr), tipc_hdr);
	if (msg == NULL)
		return 0;

	if (unlikely(msg_version(msg) != TIPC_VERSION))
		return 0;

	size = msg_size(msg);
	hdr_size = msg_hdr_sz(msg);
	min_hdr_size = msg_isdata(msg) ?
		min_data_hdr_size[msg_type(msg)] : INT_H_SIZE;

	if (unlikely((hdr_size < min_hdr_size) ||
		     (size < hdr_size) ||
		     (buf->len < size) ||
		     (size - hdr_size > TIPC_MAX_USER_MSG_SIZE)))
		return 0;

	return pskb_may_pull(buf, hdr_size);
}

/**
 * tipc_rcv - process TIPC packets/messages arriving from off-node
 * @head: pointer to message buffer chain
 * @b_ptr: pointer to bearer message arrived on
 *
 * Invoked with no locks held.  Bearer pointer must point to a valid bearer
 * structure (i.e. cannot be NULL), but bearer can be inactive.
 */
void tipc_rcv(struct sk_buff *head, struct tipc_bearer *b_ptr)
{
	while (head) {
		struct tipc_node *n_ptr;
		struct tipc_link *l_ptr;
		struct sk_buff *crs;
		struct sk_buff *buf = head;
		struct tipc_msg *msg;
		u32 seq_no;
		u32 ackd;
		u32 released = 0;

		head = head->next;
		buf->next = NULL;

		/* Ensure message is well-formed */
		if (unlikely(!link_recv_buf_validate(buf)))
			goto discard;

		/* Ensure message data is a single contiguous unit */
		if (unlikely(skb_linearize(buf)))
			goto discard;

		/* Handle arrival of a non-unicast link message */
		msg = buf_msg(buf);

		if (unlikely(msg_non_seq(msg))) {
			if (msg_user(msg) ==  LINK_CONFIG)
				tipc_disc_rcv(buf, b_ptr);
			else
				tipc_bclink_rcv(buf);
			continue;
		}

		/* Discard unicast link messages destined for another node */
		if (unlikely(!msg_short(msg) &&
			     (msg_destnode(msg) != tipc_own_addr)))
			goto discard;

		/* Locate neighboring node that sent message */
		n_ptr = tipc_node_find(msg_prevnode(msg));
		if (unlikely(!n_ptr))
			goto discard;
		tipc_node_lock(n_ptr);

		/* Locate unicast link endpoint that should handle message */
		l_ptr = n_ptr->links[b_ptr->identity];
		if (unlikely(!l_ptr))
			goto unlock_discard;

		/* Verify that communication with node is currently allowed */
		if ((n_ptr->action_flags & TIPC_WAIT_PEER_LINKS_DOWN) &&
		    msg_user(msg) == LINK_PROTOCOL &&
		    (msg_type(msg) == RESET_MSG ||
		    msg_type(msg) == ACTIVATE_MSG) &&
		    !msg_redundant_link(msg))
			n_ptr->action_flags &= ~TIPC_WAIT_PEER_LINKS_DOWN;

		if (tipc_node_blocked(n_ptr))
			goto unlock_discard;

		/* Validate message sequence number info */
		seq_no = msg_seqno(msg);
		ackd = msg_ack(msg);

		/* Release acked messages */
		if (n_ptr->bclink.recv_permitted)
			tipc_bclink_acknowledge(n_ptr, msg_bcast_ack(msg));

		crs = l_ptr->first_out;
		while ((crs != l_ptr->next_out) &&
		       less_eq(buf_seqno(crs), ackd)) {
			struct sk_buff *next = crs->next;
			kfree_skb(crs);
			crs = next;
			released++;
		}
		if (released) {
			l_ptr->first_out = crs;
			l_ptr->out_queue_size -= released;
		}

		/* Try sending any messages link endpoint has pending */
		if (unlikely(l_ptr->next_out))
			tipc_link_push_queue(l_ptr);

		if (unlikely(!list_empty(&l_ptr->waiting_ports)))
			tipc_link_wakeup_ports(l_ptr, 0);

		/* Process the incoming packet */
		if (unlikely(!link_working_working(l_ptr))) {
			if (msg_user(msg) == LINK_PROTOCOL) {
				tipc_link_proto_rcv(l_ptr, buf);
				head = link_insert_deferred_queue(l_ptr, head);
				tipc_node_unlock(n_ptr);
				continue;
			}

			/* Traffic message. Conditionally activate link */
			link_state_event(l_ptr, TRAFFIC_MSG_EVT);

			if (link_working_working(l_ptr)) {
				/* Re-insert buffer in front of queue */
				buf->next = head;
				head = buf;
				tipc_node_unlock(n_ptr);
				continue;
			}
			goto unlock_discard;
		}

		/* Link is now in state WORKING_WORKING */
		if (unlikely(seq_no != mod(l_ptr->next_in_no))) {
			link_handle_out_of_seq_msg(l_ptr, buf);
			head = link_insert_deferred_queue(l_ptr, head);
			tipc_node_unlock(n_ptr);
			continue;
		}
		l_ptr->next_in_no++;
		if (unlikely(l_ptr->oldest_deferred_in))
			head = link_insert_deferred_queue(l_ptr, head);

		if (unlikely(++l_ptr->unacked_window >= TIPC_MIN_LINK_WIN)) {
			l_ptr->stats.sent_acks++;
			tipc_link_proto_xmit(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
		}

		if (tipc_link_prepare_input(l_ptr, &buf)) {
			tipc_node_unlock(n_ptr);
			continue;
		}
		tipc_node_unlock(n_ptr);
		msg = buf_msg(buf);
		if (tipc_link_input(l_ptr, buf) != 0)
			goto discard;
		continue;
unlock_discard:
		tipc_node_unlock(n_ptr);
discard:
		kfree_skb(buf);
	}
}

/**
 * tipc_link_prepare_input - process TIPC link messages
 *
 * returns nonzero if the message was consumed
 *
 * Node lock must be held
 */
static int tipc_link_prepare_input(struct tipc_link *l, struct sk_buff **buf)
{
	struct tipc_node *n;
	struct tipc_msg *msg;
	int res = -EINVAL;

	n = l->owner;
	msg = buf_msg(*buf);
	switch (msg_user(msg)) {
	case CHANGEOVER_PROTOCOL:
		if (tipc_link_tunnel_rcv(n, buf))
			res = 0;
		break;
	case MSG_FRAGMENTER:
		l->stats.recv_fragments++;
		if (tipc_buf_append(&l->reasm_buf, buf)) {
			l->stats.recv_fragmented++;
			res = 0;
		} else if (!l->reasm_buf) {
			tipc_link_reset(l);
		}
		break;
	case MSG_BUNDLER:
		l->stats.recv_bundles++;
		l->stats.recv_bundled += msg_msgcnt(msg);
		res = 0;
		break;
	case NAME_DISTRIBUTOR:
		n->bclink.recv_permitted = true;
		res = 0;
		break;
	case BCAST_PROTOCOL:
		tipc_link_sync_rcv(n, *buf);
		break;
	default:
		res = 0;
	}
	return res;
}
/**
 * tipc_link_input - Deliver message too higher layers
 */
static int tipc_link_input(struct tipc_link *l, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	int res = 0;

	switch (msg_user(msg)) {
	case TIPC_LOW_IMPORTANCE:
	case TIPC_MEDIUM_IMPORTANCE:
	case TIPC_HIGH_IMPORTANCE:
	case TIPC_CRITICAL_IMPORTANCE:
	case CONN_MANAGER:
		tipc_sk_rcv(buf);
		break;
	case NAME_DISTRIBUTOR:
		tipc_named_rcv(buf);
		break;
	case MSG_BUNDLER:
		tipc_link_bundle_rcv(buf);
		break;
	default:
		res = -EINVAL;
	}
	return res;
}

/**
 * tipc_link_defer_pkt - Add out-of-sequence message to deferred reception queue
 *
 * Returns increase in queue length (i.e. 0 or 1)
 */
u32 tipc_link_defer_pkt(struct sk_buff **head, struct sk_buff **tail,
			struct sk_buff *buf)
{
	struct sk_buff *queue_buf;
	struct sk_buff **prev;
	u32 seq_no = buf_seqno(buf);

	buf->next = NULL;

	/* Empty queue ? */
	if (*head == NULL) {
		*head = *tail = buf;
		return 1;
	}

	/* Last ? */
	if (less(buf_seqno(*tail), seq_no)) {
		(*tail)->next = buf;
		*tail = buf;
		return 1;
	}

	/* Locate insertion point in queue, then insert; discard if duplicate */
	prev = head;
	queue_buf = *head;
	for (;;) {
		u32 curr_seqno = buf_seqno(queue_buf);

		if (seq_no == curr_seqno) {
			kfree_skb(buf);
			return 0;
		}

		if (less(seq_no, curr_seqno))
			break;

		prev = &queue_buf->next;
		queue_buf = queue_buf->next;
	}

	buf->next = queue_buf;
	*prev = buf;
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

	if (tipc_link_defer_pkt(&l_ptr->oldest_deferred_in,
				&l_ptr->newest_deferred_in, buf)) {
		l_ptr->deferred_inqueue_sz++;
		l_ptr->stats.deferred_recv++;
		TIPC_SKB_CB(buf)->deferred = true;
		if ((l_ptr->deferred_inqueue_sz % 16) == 1)
			tipc_link_proto_xmit(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
	} else
		l_ptr->stats.duplicates++;
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

	/* Discard any previous message that was deferred due to congestion */
	if (l_ptr->proto_msg_queue) {
		kfree_skb(l_ptr->proto_msg_queue);
		l_ptr->proto_msg_queue = NULL;
	}

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
	msg_set_last_bcast(msg, tipc_bclink_get_last_sent());

	if (msg_typ == STATE_MSG) {
		u32 next_sent = mod(l_ptr->next_out_no);

		if (!tipc_link_is_up(l_ptr))
			return;
		if (l_ptr->next_out)
			next_sent = buf_seqno(l_ptr->next_out);
		msg_set_next_sent(msg, next_sent);
		if (l_ptr->oldest_deferred_in) {
			u32 rec = buf_seqno(l_ptr->oldest_deferred_in);
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

	tipc_bearer_send(l_ptr->bearer_id, buf, &l_ptr->media_addr);
	l_ptr->unacked_window = 0;
	kfree_skb(buf);
}

/*
 * Receive protocol message :
 * Note that network plane id propagates through the network, and may
 * change at any time. The node with lowest address rules
 */
static void tipc_link_proto_rcv(struct tipc_link *l_ptr, struct sk_buff *buf)
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
		if (tipc_own_addr > msg_prevnode(msg))
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
			pr_warn("%s<%s>, priority change %u->%u\n",
				link_rst_msg, l_ptr->name, l_ptr->priority,
				msg_linkprio(msg));
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
			tipc_link_retransmit(l_ptr, l_ptr->first_out,
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
	struct sk_buff *buf;
	u32 length = msg_size(msg);

	tunnel = l_ptr->owner->active_links[selector & 1];
	if (!tipc_link_is_up(tunnel)) {
		pr_warn("%stunnel link no longer available\n", link_co_err);
		return;
	}
	msg_set_size(tunnel_hdr, length + INT_H_SIZE);
	buf = tipc_buf_acquire(length + INT_H_SIZE);
	if (!buf) {
		pr_warn("%sunable to send tunnel msg\n", link_co_err);
		return;
	}
	skb_copy_to_linear_data(buf, tunnel_hdr, INT_H_SIZE);
	skb_copy_to_linear_data_offset(buf, INT_H_SIZE, msg, length);
	__tipc_link_xmit2(tunnel, buf);
}


/* tipc_link_failover_send_queue(): A link has gone down, but a second
 * link is still active. We can do failover. Tunnel the failing link's
 * whole send queue via the remaining link. This way, we don't lose
 * any packets, and sequence order is preserved for subsequent traffic
 * sent over the remaining link. Owner node is locked.
 */
void tipc_link_failover_send_queue(struct tipc_link *l_ptr)
{
	u32 msgcount = l_ptr->out_queue_size;
	struct sk_buff *crs = l_ptr->first_out;
	struct tipc_link *tunnel = l_ptr->owner->active_links[0];
	struct tipc_msg tunnel_hdr;
	int split_bundles;

	if (!tunnel)
		return;

	tipc_msg_init(&tunnel_hdr, CHANGEOVER_PROTOCOL,
		 ORIGINAL_MSG, INT_H_SIZE, l_ptr->addr);
	msg_set_bearer_id(&tunnel_hdr, l_ptr->peer_bearer_id);
	msg_set_msgcnt(&tunnel_hdr, msgcount);

	if (!l_ptr->first_out) {
		struct sk_buff *buf;

		buf = tipc_buf_acquire(INT_H_SIZE);
		if (buf) {
			skb_copy_to_linear_data(buf, &tunnel_hdr, INT_H_SIZE);
			msg_set_size(&tunnel_hdr, INT_H_SIZE);
			__tipc_link_xmit2(tunnel, buf);
		} else {
			pr_warn("%sunable to send changeover msg\n",
				link_co_err);
		}
		return;
	}

	split_bundles = (l_ptr->owner->active_links[0] !=
			 l_ptr->owner->active_links[1]);

	while (crs) {
		struct tipc_msg *msg = buf_msg(crs);

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
		crs = crs->next;
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
void tipc_link_dup_queue_xmit(struct tipc_link *l_ptr,
			      struct tipc_link *tunnel)
{
	struct sk_buff *iter;
	struct tipc_msg tunnel_hdr;

	tipc_msg_init(&tunnel_hdr, CHANGEOVER_PROTOCOL,
		 DUPLICATE_MSG, INT_H_SIZE, l_ptr->addr);
	msg_set_msgcnt(&tunnel_hdr, l_ptr->out_queue_size);
	msg_set_bearer_id(&tunnel_hdr, l_ptr->peer_bearer_id);
	iter = l_ptr->first_out;
	while (iter) {
		struct sk_buff *outbuf;
		struct tipc_msg *msg = buf_msg(iter);
		u32 length = msg_size(msg);

		if (msg_user(msg) == MSG_BUNDLER)
			msg_set_type(msg, CLOSED_MSG);
		msg_set_ack(msg, mod(l_ptr->next_in_no - 1));	/* Update */
		msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
		msg_set_size(&tunnel_hdr, length + INT_H_SIZE);
		outbuf = tipc_buf_acquire(length + INT_H_SIZE);
		if (outbuf == NULL) {
			pr_warn("%sunable to send duplicate msg\n",
				link_co_err);
			return;
		}
		skb_copy_to_linear_data(outbuf, &tunnel_hdr, INT_H_SIZE);
		skb_copy_to_linear_data_offset(outbuf, INT_H_SIZE, iter->data,
					       length);
		__tipc_link_xmit2(tunnel, outbuf);
		if (!tipc_link_is_up(l_ptr))
			return;
		iter = iter->next;
	}
}

/**
 * buf_extract - extracts embedded TIPC message from another message
 * @skb: encapsulating message buffer
 * @from_pos: offset to extract from
 *
 * Returns a new message buffer containing an embedded message.  The
 * encapsulating message itself is left unchanged.
 */
static struct sk_buff *buf_extract(struct sk_buff *skb, u32 from_pos)
{
	struct tipc_msg *msg = (struct tipc_msg *)(skb->data + from_pos);
	u32 size = msg_size(msg);
	struct sk_buff *eb;

	eb = tipc_buf_acquire(size);
	if (eb)
		skb_copy_to_linear_data(eb, msg, size);
	return eb;
}



/* tipc_link_dup_rcv(): Receive a tunnelled DUPLICATE_MSG packet.
 * Owner node is locked.
 */
static void tipc_link_dup_rcv(struct tipc_link *l_ptr,
			      struct sk_buff *t_buf)
{
	struct sk_buff *buf;

	if (!tipc_link_is_up(l_ptr))
		return;

	buf = buf_extract(t_buf, INT_H_SIZE);
	if (buf == NULL) {
		pr_warn("%sfailed to extract inner dup pkt\n", link_co_err);
		return;
	}

	/* Add buffer to deferred queue, if applicable: */
	link_handle_out_of_seq_msg(l_ptr, buf);
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

	if (tipc_link_is_up(l_ptr))
		tipc_link_reset(l_ptr);

	/* First failover packet? */
	if (l_ptr->exp_msg_count == START_CHANGEOVER)
		l_ptr->exp_msg_count = msg_msgcnt(t_msg);

	/* Should there be an inner packet? */
	if (l_ptr->exp_msg_count) {
		l_ptr->exp_msg_count--;
		buf = buf_extract(t_buf, INT_H_SIZE);
		if (buf == NULL) {
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
	if ((l_ptr->exp_msg_count == 0) && (l_ptr->flags & LINK_STOPPED)) {
		tipc_node_detach_link(l_ptr->owner, l_ptr);
		kfree(l_ptr);
	}
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

/*
 *  Bundler functionality:
 */
void tipc_link_bundle_rcv(struct sk_buff *buf)
{
	u32 msgcount = msg_msgcnt(buf_msg(buf));
	u32 pos = INT_H_SIZE;
	struct sk_buff *obuf;
	struct tipc_msg *omsg;

	while (msgcount--) {
		obuf = buf_extract(buf, pos);
		if (obuf == NULL) {
			pr_warn("Link unable to unbundle message(s)\n");
			break;
		}
		omsg = buf_msg(obuf);
		pos += align(msg_size(omsg));
		if (msg_isdata(omsg) || (msg_user(omsg) == CONN_MANAGER)) {
			tipc_sk_rcv(obuf);
		} else if (msg_user(omsg) == NAME_DISTRIBUTOR) {
			tipc_named_rcv(obuf);
		} else {
			pr_warn("Illegal bundled msg: %u\n", msg_user(omsg));
			kfree_skb(obuf);
		}
	}
	kfree_skb(buf);
}

/*
 *  Fragmentation/defragmentation:
 */

/*
 * tipc_link_frag_xmit: Entry for buffers needing fragmentation.
 * The buffer is complete, inclusive total message length.
 * Returns user data length.
 */
static int tipc_link_frag_xmit(struct tipc_link *l_ptr, struct sk_buff *buf)
{
	struct sk_buff *buf_chain = NULL;
	struct sk_buff *buf_chain_tail = (struct sk_buff *)&buf_chain;
	struct tipc_msg *inmsg = buf_msg(buf);
	struct tipc_msg fragm_hdr;
	u32 insize = msg_size(inmsg);
	u32 dsz = msg_data_sz(inmsg);
	unchar *crs = buf->data;
	u32 rest = insize;
	u32 pack_sz = l_ptr->max_pkt;
	u32 fragm_sz = pack_sz - INT_H_SIZE;
	u32 fragm_no = 0;
	u32 destaddr;

	if (msg_short(inmsg))
		destaddr = l_ptr->addr;
	else
		destaddr = msg_destnode(inmsg);

	/* Prepare reusable fragment header: */
	tipc_msg_init(&fragm_hdr, MSG_FRAGMENTER, FIRST_FRAGMENT,
		 INT_H_SIZE, destaddr);

	/* Chop up message: */
	while (rest > 0) {
		struct sk_buff *fragm;

		if (rest <= fragm_sz) {
			fragm_sz = rest;
			msg_set_type(&fragm_hdr, LAST_FRAGMENT);
		}
		fragm = tipc_buf_acquire(fragm_sz + INT_H_SIZE);
		if (fragm == NULL) {
			kfree_skb(buf);
			kfree_skb_list(buf_chain);
			return -ENOMEM;
		}
		msg_set_size(&fragm_hdr, fragm_sz + INT_H_SIZE);
		fragm_no++;
		msg_set_fragm_no(&fragm_hdr, fragm_no);
		skb_copy_to_linear_data(fragm, &fragm_hdr, INT_H_SIZE);
		skb_copy_to_linear_data_offset(fragm, INT_H_SIZE, crs,
					       fragm_sz);
		buf_chain_tail->next = fragm;
		buf_chain_tail = fragm;

		rest -= fragm_sz;
		crs += fragm_sz;
		msg_set_type(&fragm_hdr, FRAGMENT);
	}
	kfree_skb(buf);

	/* Append chain of fragments to send queue & send them */
	l_ptr->long_msg_seq_no++;
	link_add_chain_to_outqueue(l_ptr, buf_chain, l_ptr->long_msg_seq_no);
	l_ptr->stats.sent_fragments += fragm_no;
	l_ptr->stats.sent_fragmented++;
	tipc_link_push_queue(l_ptr);

	return dsz;
}

static void link_set_supervision_props(struct tipc_link *l_ptr, u32 tolerance)
{
	if ((tolerance < TIPC_MIN_LINK_TOL) || (tolerance > TIPC_MAX_LINK_TOL))
		return;

	l_ptr->tolerance = tolerance;
	l_ptr->continuity_interval =
		((tolerance / 4) > 500) ? 500 : tolerance / 4;
	l_ptr->abort_limit = tolerance / (l_ptr->continuity_interval / 4);
}

void tipc_link_set_queue_limits(struct tipc_link *l_ptr, u32 window)
{
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
 * @name: pointer to link name string
 * @bearer_id: pointer to index in 'node->links' array where the link was found.
 *
 * Returns pointer to node owning the link, or 0 if no matching link is found.
 */
static struct tipc_node *tipc_link_find_owner(const char *link_name,
					      unsigned int *bearer_id)
{
	struct tipc_link *l_ptr;
	struct tipc_node *n_ptr;
	struct tipc_node *found_node = 0;
	int i;

	*bearer_id = 0;
	rcu_read_lock();
	list_for_each_entry_rcu(n_ptr, &tipc_node_list, list) {
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
 * link_value_is_valid -- validate proposed link tolerance/priority/window
 *
 * @cmd: value type (TIPC_CMD_SET_LINK_*)
 * @new_value: the new value
 *
 * Returns 1 if value is within range, 0 if not.
 */
static int link_value_is_valid(u16 cmd, u32 new_value)
{
	switch (cmd) {
	case TIPC_CMD_SET_LINK_TOL:
		return (new_value >= TIPC_MIN_LINK_TOL) &&
			(new_value <= TIPC_MAX_LINK_TOL);
	case TIPC_CMD_SET_LINK_PRI:
		return (new_value <= TIPC_MAX_LINK_PRI);
	case TIPC_CMD_SET_LINK_WINDOW:
		return (new_value >= TIPC_MIN_LINK_WIN) &&
			(new_value <= TIPC_MAX_LINK_WIN);
	}
	return 0;
}

/**
 * link_cmd_set_value - change priority/tolerance/window for link/bearer/media
 * @name: ptr to link, bearer, or media name
 * @new_value: new value of link, bearer, or media setting
 * @cmd: which link, bearer, or media attribute to set (TIPC_CMD_SET_LINK_*)
 *
 * Caller must hold RTNL lock to ensure link/bearer/media is not deleted.
 *
 * Returns 0 if value updated and negative value on error.
 */
static int link_cmd_set_value(const char *name, u32 new_value, u16 cmd)
{
	struct tipc_node *node;
	struct tipc_link *l_ptr;
	struct tipc_bearer *b_ptr;
	struct tipc_media *m_ptr;
	int bearer_id;
	int res = 0;

	node = tipc_link_find_owner(name, &bearer_id);
	if (node) {
		tipc_node_lock(node);
		l_ptr = node->links[bearer_id];

		if (l_ptr) {
			switch (cmd) {
			case TIPC_CMD_SET_LINK_TOL:
				link_set_supervision_props(l_ptr, new_value);
				tipc_link_proto_xmit(l_ptr, STATE_MSG, 0, 0,
						     new_value, 0, 0);
				break;
			case TIPC_CMD_SET_LINK_PRI:
				l_ptr->priority = new_value;
				tipc_link_proto_xmit(l_ptr, STATE_MSG, 0, 0,
						     0, new_value, 0);
				break;
			case TIPC_CMD_SET_LINK_WINDOW:
				tipc_link_set_queue_limits(l_ptr, new_value);
				break;
			default:
				res = -EINVAL;
				break;
			}
		}
		tipc_node_unlock(node);
		return res;
	}

	b_ptr = tipc_bearer_find(name);
	if (b_ptr) {
		switch (cmd) {
		case TIPC_CMD_SET_LINK_TOL:
			b_ptr->tolerance = new_value;
			break;
		case TIPC_CMD_SET_LINK_PRI:
			b_ptr->priority = new_value;
			break;
		case TIPC_CMD_SET_LINK_WINDOW:
			b_ptr->window = new_value;
			break;
		default:
			res = -EINVAL;
			break;
		}
		return res;
	}

	m_ptr = tipc_media_find(name);
	if (!m_ptr)
		return -ENODEV;
	switch (cmd) {
	case TIPC_CMD_SET_LINK_TOL:
		m_ptr->tolerance = new_value;
		break;
	case TIPC_CMD_SET_LINK_PRI:
		m_ptr->priority = new_value;
		break;
	case TIPC_CMD_SET_LINK_WINDOW:
		m_ptr->window = new_value;
		break;
	default:
		res = -EINVAL;
		break;
	}
	return res;
}

struct sk_buff *tipc_link_cmd_config(const void *req_tlv_area, int req_tlv_space,
				     u16 cmd)
{
	struct tipc_link_config *args;
	u32 new_value;
	int res;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_LINK_CONFIG))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	args = (struct tipc_link_config *)TLV_DATA(req_tlv_area);
	new_value = ntohl(args->value);

	if (!link_value_is_valid(cmd, new_value))
		return tipc_cfg_reply_error_string(
			"cannot change, value invalid");

	if (!strcmp(args->name, tipc_bclink_name)) {
		if ((cmd == TIPC_CMD_SET_LINK_WINDOW) &&
		    (tipc_bclink_set_queue_limits(new_value) == 0))
			return tipc_cfg_reply_none();
		return tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
						   " (cannot change setting on broadcast link)");
	}

	res = link_cmd_set_value(args->name, new_value, cmd);
	if (res)
		return tipc_cfg_reply_error_string("cannot change link setting");

	return tipc_cfg_reply_none();
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

struct sk_buff *tipc_link_cmd_reset_stats(const void *req_tlv_area, int req_tlv_space)
{
	char *link_name;
	struct tipc_link *l_ptr;
	struct tipc_node *node;
	unsigned int bearer_id;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_LINK_NAME))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	link_name = (char *)TLV_DATA(req_tlv_area);
	if (!strcmp(link_name, tipc_bclink_name)) {
		if (tipc_bclink_reset_stats())
			return tipc_cfg_reply_error_string("link not found");
		return tipc_cfg_reply_none();
	}
	node = tipc_link_find_owner(link_name, &bearer_id);
	if (!node)
		return tipc_cfg_reply_error_string("link not found");

	tipc_node_lock(node);
	l_ptr = node->links[bearer_id];
	if (!l_ptr) {
		tipc_node_unlock(node);
		return tipc_cfg_reply_error_string("link not found");
	}
	link_reset_statistics(l_ptr);
	tipc_node_unlock(node);
	return tipc_cfg_reply_none();
}

/**
 * percent - convert count to a percentage of total (rounding up or down)
 */
static u32 percent(u32 count, u32 total)
{
	return (count * 100 + (total / 2)) / total;
}

/**
 * tipc_link_stats - print link statistics
 * @name: link name
 * @buf: print buffer area
 * @buf_size: size of print buffer area
 *
 * Returns length of print buffer data string (or 0 if error)
 */
static int tipc_link_stats(const char *name, char *buf, const u32 buf_size)
{
	struct tipc_link *l;
	struct tipc_stats *s;
	struct tipc_node *node;
	char *status;
	u32 profile_total = 0;
	unsigned int bearer_id;
	int ret;

	if (!strcmp(name, tipc_bclink_name))
		return tipc_bclink_stats(buf, buf_size);

	node = tipc_link_find_owner(name, &bearer_id);
	if (!node)
		return 0;

	tipc_node_lock(node);

	l = node->links[bearer_id];
	if (!l) {
		tipc_node_unlock(node);
		return 0;
	}

	s = &l->stats;

	if (tipc_link_is_active(l))
		status = "ACTIVE";
	else if (tipc_link_is_up(l))
		status = "STANDBY";
	else
		status = "DEFUNCT";

	ret = tipc_snprintf(buf, buf_size, "Link <%s>\n"
			    "  %s  MTU:%u  Priority:%u  Tolerance:%u ms"
			    "  Window:%u packets\n",
			    l->name, status, l->max_pkt, l->priority,
			    l->tolerance, l->queue_limit[0]);

	ret += tipc_snprintf(buf + ret, buf_size - ret,
			     "  RX packets:%u fragments:%u/%u bundles:%u/%u\n",
			     l->next_in_no - s->recv_info, s->recv_fragments,
			     s->recv_fragmented, s->recv_bundles,
			     s->recv_bundled);

	ret += tipc_snprintf(buf + ret, buf_size - ret,
			     "  TX packets:%u fragments:%u/%u bundles:%u/%u\n",
			     l->next_out_no - s->sent_info, s->sent_fragments,
			     s->sent_fragmented, s->sent_bundles,
			     s->sent_bundled);

	profile_total = s->msg_length_counts;
	if (!profile_total)
		profile_total = 1;

	ret += tipc_snprintf(buf + ret, buf_size - ret,
			     "  TX profile sample:%u packets  average:%u octets\n"
			     "  0-64:%u%% -256:%u%% -1024:%u%% -4096:%u%% "
			     "-16384:%u%% -32768:%u%% -66000:%u%%\n",
			     s->msg_length_counts,
			     s->msg_lengths_total / profile_total,
			     percent(s->msg_length_profile[0], profile_total),
			     percent(s->msg_length_profile[1], profile_total),
			     percent(s->msg_length_profile[2], profile_total),
			     percent(s->msg_length_profile[3], profile_total),
			     percent(s->msg_length_profile[4], profile_total),
			     percent(s->msg_length_profile[5], profile_total),
			     percent(s->msg_length_profile[6], profile_total));

	ret += tipc_snprintf(buf + ret, buf_size - ret,
			     "  RX states:%u probes:%u naks:%u defs:%u"
			     " dups:%u\n", s->recv_states, s->recv_probes,
			     s->recv_nacks, s->deferred_recv, s->duplicates);

	ret += tipc_snprintf(buf + ret, buf_size - ret,
			     "  TX states:%u probes:%u naks:%u acks:%u"
			     " dups:%u\n", s->sent_states, s->sent_probes,
			     s->sent_nacks, s->sent_acks, s->retransmitted);

	ret += tipc_snprintf(buf + ret, buf_size - ret,
			     "  Congestion link:%u  Send queue"
			     " max:%u avg:%u\n", s->link_congs,
			     s->max_queue_sz, s->queue_sz_counts ?
			     (s->accu_queue_sz / s->queue_sz_counts) : 0);

	tipc_node_unlock(node);
	return ret;
}

struct sk_buff *tipc_link_cmd_show_stats(const void *req_tlv_area, int req_tlv_space)
{
	struct sk_buff *buf;
	struct tlv_desc *rep_tlv;
	int str_len;
	int pb_len;
	char *pb;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_LINK_NAME))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	buf = tipc_cfg_reply_alloc(TLV_SPACE(ULTRA_STRING_MAX_LEN));
	if (!buf)
		return NULL;

	rep_tlv = (struct tlv_desc *)buf->data;
	pb = TLV_DATA(rep_tlv);
	pb_len = ULTRA_STRING_MAX_LEN;
	str_len = tipc_link_stats((char *)TLV_DATA(req_tlv_area),
				  pb, pb_len);
	if (!str_len) {
		kfree_skb(buf);
		return tipc_cfg_reply_error_string("link not found");
	}
	str_len += 1;	/* for "\0" */
	skb_put(buf, TLV_SPACE(str_len));
	TLV_SET(rep_tlv, TIPC_TLV_ULTRA_STRING, NULL, str_len);

	return buf;
}

/**
 * tipc_link_get_max_pkt - get maximum packet size to use when sending to destination
 * @dest: network address of destination node
 * @selector: used to select from set of active links
 *
 * If no active link can be found, uses default maximum packet size.
 */
u32 tipc_link_get_max_pkt(u32 dest, u32 selector)
{
	struct tipc_node *n_ptr;
	struct tipc_link *l_ptr;
	u32 res = MAX_PKT_DEFAULT;

	if (dest == tipc_own_addr)
		return MAX_MSG_SIZE;

	n_ptr = tipc_node_find(dest);
	if (n_ptr) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector & 1];
		if (l_ptr)
			res = l_ptr->max_pkt;
		tipc_node_unlock(n_ptr);
	}
	return res;
}

static void link_print(struct tipc_link *l_ptr, const char *str)
{
	struct tipc_bearer *b_ptr;

	rcu_read_lock();
	b_ptr = rcu_dereference_rtnl(bearer_list[l_ptr->bearer_id]);
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

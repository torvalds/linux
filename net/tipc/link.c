/*
 * net/tipc/link.c: TIPC link code
 *
 * Copyright (c) 1996-2007, 2012, Ericsson AB
 * Copyright (c) 2004-2007, 2010-2011, Wind River Systems
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
#include "name_distr.h"
#include "discover.h"
#include "config.h"

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

/**
 * struct tipc_link_name - deconstructed link name
 * @addr_local: network address of node at this end
 * @if_local: name of interface at this end
 * @addr_peer: network address of node at far end
 * @if_peer: name of interface at far end
 */
struct tipc_link_name {
	u32 addr_local;
	char if_local[TIPC_MAX_IF_NAME];
	u32 addr_peer;
	char if_peer[TIPC_MAX_IF_NAME];
};

static void link_handle_out_of_seq_msg(struct tipc_link *l_ptr,
				       struct sk_buff *buf);
static void link_recv_proto_msg(struct tipc_link *l_ptr, struct sk_buff *buf);
static int  link_recv_changeover_msg(struct tipc_link **l_ptr,
				     struct sk_buff **buf);
static void link_set_supervision_props(struct tipc_link *l_ptr, u32 tolerance);
static int  link_send_sections_long(struct tipc_port *sender,
				    struct iovec const *msg_sect,
				    u32 num_sect, unsigned int total_len,
				    u32 destnode);
static void link_check_defragm_bufs(struct tipc_link *l_ptr);
static void link_state_event(struct tipc_link *l_ptr, u32 event);
static void link_reset_statistics(struct tipc_link *l_ptr);
static void link_print(struct tipc_link *l_ptr, const char *str);
static void link_start(struct tipc_link *l_ptr);
static int link_send_long_buf(struct tipc_link *l_ptr, struct sk_buff *buf);
static void tipc_link_send_sync(struct tipc_link *l);
static void tipc_link_recv_sync(struct tipc_node *n, struct sk_buff *buf);

/*
 *  Simple link routines
 */
static unsigned int align(unsigned int i)
{
	return (i + 3) & ~3u;
}

static void link_init_max_pkt(struct tipc_link *l_ptr)
{
	u32 max_pkt;

	max_pkt = (l_ptr->b_ptr->mtu & ~3);
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
 * link_name_validate - validate & (optionally) deconstruct tipc_link name
 * @name: ptr to link name string
 * @name_parts: ptr to area for link name components (or NULL if not needed)
 *
 * Returns 1 if link name is valid, otherwise 0.
 */
static int link_name_validate(const char *name,
				struct tipc_link_name *name_parts)
{
	char name_copy[TIPC_MAX_LINK_NAME];
	char *addr_local;
	char *if_local;
	char *addr_peer;
	char *if_peer;
	char dummy;
	u32 z_local, c_local, n_local;
	u32 z_peer, c_peer, n_peer;
	u32 if_local_len;
	u32 if_peer_len;

	/* copy link name & ensure length is OK */
	name_copy[TIPC_MAX_LINK_NAME - 1] = 0;
	/* need above in case non-Posix strncpy() doesn't pad with nulls */
	strncpy(name_copy, name, TIPC_MAX_LINK_NAME);
	if (name_copy[TIPC_MAX_LINK_NAME - 1] != 0)
		return 0;

	/* ensure all component parts of link name are present */
	addr_local = name_copy;
	if_local = strchr(addr_local, ':');
	if (if_local == NULL)
		return 0;
	*(if_local++) = 0;
	addr_peer = strchr(if_local, '-');
	if (addr_peer == NULL)
		return 0;
	*(addr_peer++) = 0;
	if_local_len = addr_peer - if_local;
	if_peer = strchr(addr_peer, ':');
	if (if_peer == NULL)
		return 0;
	*(if_peer++) = 0;
	if_peer_len = strlen(if_peer) + 1;

	/* validate component parts of link name */
	if ((sscanf(addr_local, "%u.%u.%u%c",
		    &z_local, &c_local, &n_local, &dummy) != 3) ||
	    (sscanf(addr_peer, "%u.%u.%u%c",
		    &z_peer, &c_peer, &n_peer, &dummy) != 3) ||
	    (z_local > 255) || (c_local > 4095) || (n_local > 4095) ||
	    (z_peer  > 255) || (c_peer  > 4095) || (n_peer  > 4095) ||
	    (if_local_len <= 1) || (if_local_len > TIPC_MAX_IF_NAME) ||
	    (if_peer_len  <= 1) || (if_peer_len  > TIPC_MAX_IF_NAME))
		return 0;

	/* return link name components, if necessary */
	if (name_parts) {
		name_parts->addr_local = tipc_addr(z_local, c_local, n_local);
		strcpy(name_parts->if_local, if_local);
		name_parts->addr_peer = tipc_addr(z_peer, c_peer, n_peer);
		strcpy(name_parts->if_peer, if_peer);
	}
	return 1;
}

/**
 * link_timeout - handle expiration of link timer
 * @l_ptr: pointer to link
 *
 * This routine must not grab "tipc_net_lock" to avoid a potential deadlock conflict
 * with tipc_link_delete().  (There is no risk that the node will be deleted by
 * another thread because tipc_link_delete() always cancels the link timer before
 * tipc_node_delete() is called.)
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
	link_check_defragm_bufs(l_ptr);

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
	l_ptr->b_ptr = b_ptr;
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

	link_init_max_pkt(l_ptr);

	l_ptr->next_out_no = 1;
	INIT_LIST_HEAD(&l_ptr->waiting_ports);

	link_reset_statistics(l_ptr);

	tipc_node_attach_link(n_ptr, l_ptr);

	k_init_timer(&l_ptr->timer, (Handler)link_timeout, (unsigned long)l_ptr);
	list_add_tail(&l_ptr->link_list, &b_ptr->links);
	tipc_k_signal((Handler)link_start, (unsigned long)l_ptr);

	return l_ptr;
}

/**
 * tipc_link_delete - delete a link
 * @l_ptr: pointer to link
 *
 * Note: 'tipc_net_lock' is write_locked, bearer is locked.
 * This routine must not grab the node lock until after link timer cancellation
 * to avoid a potential deadlock situation.
 */
void tipc_link_delete(struct tipc_link *l_ptr)
{
	if (!l_ptr) {
		pr_err("Attempt to delete non-existent link\n");
		return;
	}

	k_cancel_timer(&l_ptr->timer);

	tipc_node_lock(l_ptr->owner);
	tipc_link_reset(l_ptr);
	tipc_node_detach_link(l_ptr->owner, l_ptr);
	tipc_link_stop(l_ptr);
	list_del_init(&l_ptr->link_list);
	tipc_node_unlock(l_ptr->owner);
	k_term_timer(&l_ptr->timer);
	kfree(l_ptr);
}

static void link_start(struct tipc_link *l_ptr)
{
	tipc_node_lock(l_ptr->owner);
	link_state_event(l_ptr, STARTING_EVT);
	tipc_node_unlock(l_ptr->owner);
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

	spin_lock_bh(&tipc_port_list_lock);
	p_ptr = tipc_port_lock(origport);
	if (p_ptr) {
		if (!p_ptr->wakeup)
			goto exit;
		if (!list_empty(&p_ptr->wait_list))
			goto exit;
		p_ptr->congested = 1;
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
		list_del_init(&p_ptr->wait_list);
		spin_lock_bh(p_ptr->lock);
		p_ptr->congested = 0;
		p_ptr->wakeup(p_ptr);
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
	struct sk_buff *buf = l_ptr->first_out;
	struct sk_buff *next;

	while (buf) {
		next = buf->next;
		kfree_skb(buf);
		buf = next;
	}
	l_ptr->first_out = NULL;
	l_ptr->out_queue_size = 0;
}

/**
 * tipc_link_reset_fragments - purge link's inbound message fragments queue
 * @l_ptr: pointer to link
 */
void tipc_link_reset_fragments(struct tipc_link *l_ptr)
{
	struct sk_buff *buf = l_ptr->defragm_buf;
	struct sk_buff *next;

	while (buf) {
		next = buf->next;
		kfree_skb(buf);
		buf = next;
	}
	l_ptr->defragm_buf = NULL;
}

/**
 * tipc_link_stop - purge all inbound and outbound messages associated with link
 * @l_ptr: pointer to link
 */
void tipc_link_stop(struct tipc_link *l_ptr)
{
	struct sk_buff *buf;
	struct sk_buff *next;

	buf = l_ptr->oldest_deferred_in;
	while (buf) {
		next = buf->next;
		kfree_skb(buf);
		buf = next;
	}

	buf = l_ptr->first_out;
	while (buf) {
		next = buf->next;
		kfree_skb(buf);
		buf = next;
	}

	tipc_link_reset_fragments(l_ptr);

	kfree_skb(l_ptr->proto_msg_queue);
	l_ptr->proto_msg_queue = NULL;
}

void tipc_link_reset(struct tipc_link *l_ptr)
{
	struct sk_buff *buf;
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
	tipc_bearer_remove_dest(l_ptr->b_ptr, l_ptr->addr);

	if (was_active_link && tipc_node_active_links(l_ptr->owner) &&
	    l_ptr->owner->permit_changeover) {
		l_ptr->reset_checkpoint = checkpoint;
		l_ptr->exp_msg_count = START_CHANGEOVER;
	}

	/* Clean up all queues: */
	link_release_outqueue(l_ptr);
	kfree_skb(l_ptr->proto_msg_queue);
	l_ptr->proto_msg_queue = NULL;
	buf = l_ptr->oldest_deferred_in;
	while (buf) {
		struct sk_buff *next = buf->next;
		kfree_skb(buf);
		buf = next;
	}
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


static void link_activate(struct tipc_link *l_ptr)
{
	l_ptr->next_in_no = l_ptr->stats.recv_info = 1;
	tipc_node_link_up(l_ptr->owner, l_ptr);
	tipc_bearer_add_dest(l_ptr->b_ptr, l_ptr->addr);
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

	if (!l_ptr->started && (event != STARTING_EVT))
		return;		/* Not yet. */

	if (link_blocked(l_ptr)) {
		if (event == TIMEOUT_EVT)
			link_set_timer(l_ptr, cont_intv);
		return;	  /* Changeover going on */
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
					tipc_link_send_proto_msg(l_ptr, STATE_MSG,
								 0, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				} else if (l_ptr->max_pkt < l_ptr->max_pkt_target) {
					tipc_link_send_proto_msg(l_ptr, STATE_MSG,
								 1, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				}
				link_set_timer(l_ptr, cont_intv);
				break;
			}
			l_ptr->state = WORKING_UNKNOWN;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_send_proto_msg(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv / 4);
			break;
		case RESET_MSG:
			pr_info("%s<%s>, requested by peer\n", link_rst_msg,
				l_ptr->name);
			tipc_link_reset(l_ptr);
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_send_proto_msg(l_ptr, ACTIVATE_MSG, 0, 0, 0, 0, 0);
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
			tipc_link_send_proto_msg(l_ptr, ACTIVATE_MSG, 0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case TIMEOUT_EVT:
			if (l_ptr->next_in_no != l_ptr->checkpoint) {
				l_ptr->state = WORKING_WORKING;
				l_ptr->fsm_msg_cnt = 0;
				l_ptr->checkpoint = l_ptr->next_in_no;
				if (tipc_bclink_acks_missing(l_ptr->owner)) {
					tipc_link_send_proto_msg(l_ptr, STATE_MSG,
								 0, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				}
				link_set_timer(l_ptr, cont_intv);
			} else if (l_ptr->fsm_msg_cnt < l_ptr->abort_limit) {
				tipc_link_send_proto_msg(l_ptr, STATE_MSG,
							 1, 0, 0, 0, 0);
				l_ptr->fsm_msg_cnt++;
				link_set_timer(l_ptr, cont_intv / 4);
			} else {	/* Link has failed */
				pr_warn("%s<%s>, peer not responding\n",
					link_rst_msg, l_ptr->name);
				tipc_link_reset(l_ptr);
				l_ptr->state = RESET_UNKNOWN;
				l_ptr->fsm_msg_cnt = 0;
				tipc_link_send_proto_msg(l_ptr, RESET_MSG,
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
			tipc_link_send_proto_msg(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			if (l_ptr->owner->working_links == 1)
				tipc_link_send_sync(l_ptr);
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_send_proto_msg(l_ptr, ACTIVATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case STARTING_EVT:
			l_ptr->started = 1;
			/* fall through */
		case TIMEOUT_EVT:
			tipc_link_send_proto_msg(l_ptr, RESET_MSG, 0, 0, 0, 0, 0);
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
			tipc_link_send_proto_msg(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			if (l_ptr->owner->working_links == 1)
				tipc_link_send_sync(l_ptr);
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			break;
		case TIMEOUT_EVT:
			tipc_link_send_proto_msg(l_ptr, ACTIVATE_MSG, 0, 0, 0, 0, 0);
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
static int link_bundle_buf(struct tipc_link *l_ptr,
			   struct sk_buff *bundler,
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
 * tipc_link_send_buf() is the 'full path' for messages, called from
 * inside TIPC when the 'fast path' in tipc_send_buf
 * has failed, and from link_send()
 */
int tipc_link_send_buf(struct tipc_link *l_ptr, struct sk_buff *buf)
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
		return link_send_long_buf(l_ptr, buf);

	/* Packet can be queued or sent. */
	if (likely(!tipc_bearer_blocked(l_ptr->b_ptr) &&
		   !link_congested(l_ptr))) {
		link_add_to_outqueue(l_ptr, buf, msg);

		tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr);
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
 * tipc_link_send(): same as tipc_link_send_buf(), but the link to use has
 * not been selected yet, and the the owner node is not locked
 * Called by TIPC internal users, e.g. the name distributor
 */
int tipc_link_send(struct sk_buff *buf, u32 dest, u32 selector)
{
	struct tipc_link *l_ptr;
	struct tipc_node *n_ptr;
	int res = -ELINKCONG;

	read_lock_bh(&tipc_net_lock);
	n_ptr = tipc_node_find(dest);
	if (n_ptr) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector & 1];
		if (l_ptr)
			res = tipc_link_send_buf(l_ptr, buf);
		else
			kfree_skb(buf);
		tipc_node_unlock(n_ptr);
	} else {
		kfree_skb(buf);
	}
	read_unlock_bh(&tipc_net_lock);
	return res;
}

/*
 * tipc_link_send_sync - synchronize broadcast link endpoints.
 *
 * Give a newly added peer node the sequence number where it should
 * start receiving and acking broadcast packets.
 *
 * Called with node locked
 */
static void tipc_link_send_sync(struct tipc_link *l)
{
	struct sk_buff *buf;
	struct tipc_msg *msg;

	buf = tipc_buf_acquire(INT_H_SIZE);
	if (!buf)
		return;

	msg = buf_msg(buf);
	tipc_msg_init(msg, BCAST_PROTOCOL, STATE_MSG, INT_H_SIZE, l->addr);
	msg_set_last_bcast(msg, l->owner->bclink.acked);
	link_add_chain_to_outqueue(l, buf, 0);
	tipc_link_push_queue(l);
}

/*
 * tipc_link_recv_sync - synchronize broadcast link endpoints.
 * Receive the sequence number where we should start receiving and
 * acking broadcast packets from a newly added peer node, and open
 * up for reception of such packets.
 *
 * Called with node locked
 */
static void tipc_link_recv_sync(struct tipc_node *n, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);

	n->bclink.last_sent = n->bclink.last_in = msg_last_bcast(msg);
	n->bclink.recv_permitted = true;
	kfree_skb(buf);
}

/*
 * tipc_link_send_names - send name table entries to new neighbor
 *
 * Send routine for bulk delivery of name table messages when contact
 * with a new neighbor occurs. No link congestion checking is performed
 * because name table messages *must* be delivered. The messages must be
 * small enough not to require fragmentation.
 * Called without any locks held.
 */
void tipc_link_send_names(struct list_head *message_list, u32 dest)
{
	struct tipc_node *n_ptr;
	struct tipc_link *l_ptr;
	struct sk_buff *buf;
	struct sk_buff *temp_buf;

	if (list_empty(message_list))
		return;

	read_lock_bh(&tipc_net_lock);
	n_ptr = tipc_node_find(dest);
	if (n_ptr) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->active_links[0];
		if (l_ptr) {
			/* convert circular list to linear list */
			((struct sk_buff *)message_list->prev)->next = NULL;
			link_add_chain_to_outqueue(l_ptr,
				(struct sk_buff *)message_list->next, 0);
			tipc_link_push_queue(l_ptr);
			INIT_LIST_HEAD(message_list);
		}
		tipc_node_unlock(n_ptr);
	}
	read_unlock_bh(&tipc_net_lock);

	/* discard the messages if they couldn't be sent */
	list_for_each_safe(buf, temp_buf, ((struct sk_buff *)message_list)) {
		list_del((struct list_head *)buf);
		kfree_skb(buf);
	}
}

/*
 * link_send_buf_fast: Entry for data messages where the
 * destination link is known and the header is complete,
 * inclusive total message length. Very time critical.
 * Link is locked. Returns user data length.
 */
static int link_send_buf_fast(struct tipc_link *l_ptr, struct sk_buff *buf,
			      u32 *used_max_pkt)
{
	struct tipc_msg *msg = buf_msg(buf);
	int res = msg_data_sz(msg);

	if (likely(!link_congested(l_ptr))) {
		if (likely(msg_size(msg) <= l_ptr->max_pkt)) {
			if (likely(!tipc_bearer_blocked(l_ptr->b_ptr))) {
				link_add_to_outqueue(l_ptr, buf, msg);
				tipc_bearer_send(l_ptr->b_ptr, buf,
						 &l_ptr->media_addr);
				l_ptr->unacked_window = 0;
				return res;
			}
		} else
			*used_max_pkt = l_ptr->max_pkt;
	}
	return tipc_link_send_buf(l_ptr, buf);  /* All other cases */
}

/*
 * tipc_send_buf_fast: Entry for data messages where the
 * destination node is known and the header is complete,
 * inclusive total message length.
 * Returns user data length.
 */
int tipc_send_buf_fast(struct sk_buff *buf, u32 destnode)
{
	struct tipc_link *l_ptr;
	struct tipc_node *n_ptr;
	int res;
	u32 selector = msg_origport(buf_msg(buf)) & 1;
	u32 dummy;

	read_lock_bh(&tipc_net_lock);
	n_ptr = tipc_node_find(destnode);
	if (likely(n_ptr)) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector];
		if (likely(l_ptr)) {
			res = link_send_buf_fast(l_ptr, buf, &dummy);
			tipc_node_unlock(n_ptr);
			read_unlock_bh(&tipc_net_lock);
			return res;
		}
		tipc_node_unlock(n_ptr);
	}
	read_unlock_bh(&tipc_net_lock);
	res = msg_data_sz(buf_msg(buf));
	tipc_reject_msg(buf, TIPC_ERR_NO_NODE);
	return res;
}


/*
 * tipc_link_send_sections_fast: Entry for messages where the
 * destination processor is known and the header is complete,
 * except for total message length.
 * Returns user data length or errno.
 */
int tipc_link_send_sections_fast(struct tipc_port *sender,
				 struct iovec const *msg_sect,
				 const u32 num_sect,
				 unsigned int total_len,
				 u32 destaddr)
{
	struct tipc_msg *hdr = &sender->phdr;
	struct tipc_link *l_ptr;
	struct sk_buff *buf;
	struct tipc_node *node;
	int res;
	u32 selector = msg_origport(hdr) & 1;

again:
	/*
	 * Try building message using port's max_pkt hint.
	 * (Must not hold any locks while building message.)
	 */
	res = tipc_msg_build(hdr, msg_sect, num_sect, total_len,
			     sender->max_pkt, !sender->user_port, &buf);

	read_lock_bh(&tipc_net_lock);
	node = tipc_node_find(destaddr);
	if (likely(node)) {
		tipc_node_lock(node);
		l_ptr = node->active_links[selector];
		if (likely(l_ptr)) {
			if (likely(buf)) {
				res = link_send_buf_fast(l_ptr, buf,
							 &sender->max_pkt);
exit:
				tipc_node_unlock(node);
				read_unlock_bh(&tipc_net_lock);
				return res;
			}

			/* Exit if build request was invalid */
			if (unlikely(res < 0))
				goto exit;

			/* Exit if link (or bearer) is congested */
			if (link_congested(l_ptr) ||
			    tipc_bearer_blocked(l_ptr->b_ptr)) {
				res = link_schedule_port(l_ptr,
							 sender->ref, res);
				goto exit;
			}

			/*
			 * Message size exceeds max_pkt hint; update hint,
			 * then re-try fast path or fragment the message
			 */
			sender->max_pkt = l_ptr->max_pkt;
			tipc_node_unlock(node);
			read_unlock_bh(&tipc_net_lock);


			if ((msg_hdr_sz(hdr) + res) <= sender->max_pkt)
				goto again;

			return link_send_sections_long(sender, msg_sect,
						       num_sect, total_len,
						       destaddr);
		}
		tipc_node_unlock(node);
	}
	read_unlock_bh(&tipc_net_lock);

	/* Couldn't find a link to the destination node */
	if (buf)
		return tipc_reject_msg(buf, TIPC_ERR_NO_NODE);
	if (res >= 0)
		return tipc_port_reject_sections(sender, hdr, msg_sect, num_sect,
						 total_len, TIPC_ERR_NO_NODE);
	return res;
}

/*
 * link_send_sections_long(): Entry for long messages where the
 * destination node is known and the header is complete,
 * inclusive total message length.
 * Link and bearer congestion status have been checked to be ok,
 * and are ignored if they change.
 *
 * Note that fragments do not use the full link MTU so that they won't have
 * to undergo refragmentation if link changeover causes them to be sent
 * over another link with an additional tunnel header added as prefix.
 * (Refragmentation will still occur if the other link has a smaller MTU.)
 *
 * Returns user data length or errno.
 */
static int link_send_sections_long(struct tipc_port *sender,
				   struct iovec const *msg_sect,
				   u32 num_sect,
				   unsigned int total_len,
				   u32 destaddr)
{
	struct tipc_link *l_ptr;
	struct tipc_node *node;
	struct tipc_msg *hdr = &sender->phdr;
	u32 dsz = total_len;
	u32 max_pkt, fragm_sz, rest;
	struct tipc_msg fragm_hdr;
	struct sk_buff *buf, *buf_chain, *prev;
	u32 fragm_crs, fragm_rest, hsz, sect_rest;
	const unchar *sect_crs;
	int curr_sect;
	u32 fragm_no;

again:
	fragm_no = 1;
	max_pkt = sender->max_pkt - INT_H_SIZE;
		/* leave room for tunnel header in case of link changeover */
	fragm_sz = max_pkt - INT_H_SIZE;
		/* leave room for fragmentation header in each fragment */
	rest = dsz;
	fragm_crs = 0;
	fragm_rest = 0;
	sect_rest = 0;
	sect_crs = NULL;
	curr_sect = -1;

	/* Prepare reusable fragment header */
	tipc_msg_init(&fragm_hdr, MSG_FRAGMENTER, FIRST_FRAGMENT,
		 INT_H_SIZE, msg_destnode(hdr));
	msg_set_size(&fragm_hdr, max_pkt);
	msg_set_fragm_no(&fragm_hdr, 1);

	/* Prepare header of first fragment */
	buf_chain = buf = tipc_buf_acquire(max_pkt);
	if (!buf)
		return -ENOMEM;
	buf->next = NULL;
	skb_copy_to_linear_data(buf, &fragm_hdr, INT_H_SIZE);
	hsz = msg_hdr_sz(hdr);
	skb_copy_to_linear_data_offset(buf, INT_H_SIZE, hdr, hsz);

	/* Chop up message */
	fragm_crs = INT_H_SIZE + hsz;
	fragm_rest = fragm_sz - hsz;

	do {		/* For all sections */
		u32 sz;

		if (!sect_rest) {
			sect_rest = msg_sect[++curr_sect].iov_len;
			sect_crs = (const unchar *)msg_sect[curr_sect].iov_base;
		}

		if (sect_rest < fragm_rest)
			sz = sect_rest;
		else
			sz = fragm_rest;

		if (likely(!sender->user_port)) {
			if (copy_from_user(buf->data + fragm_crs, sect_crs, sz)) {
error:
				for (; buf_chain; buf_chain = buf) {
					buf = buf_chain->next;
					kfree_skb(buf_chain);
				}
				return -EFAULT;
			}
		} else
			skb_copy_to_linear_data_offset(buf, fragm_crs,
						       sect_crs, sz);
		sect_crs += sz;
		sect_rest -= sz;
		fragm_crs += sz;
		fragm_rest -= sz;
		rest -= sz;

		if (!fragm_rest && rest) {

			/* Initiate new fragment: */
			if (rest <= fragm_sz) {
				fragm_sz = rest;
				msg_set_type(&fragm_hdr, LAST_FRAGMENT);
			} else {
				msg_set_type(&fragm_hdr, FRAGMENT);
			}
			msg_set_size(&fragm_hdr, fragm_sz + INT_H_SIZE);
			msg_set_fragm_no(&fragm_hdr, ++fragm_no);
			prev = buf;
			buf = tipc_buf_acquire(fragm_sz + INT_H_SIZE);
			if (!buf)
				goto error;

			buf->next = NULL;
			prev->next = buf;
			skb_copy_to_linear_data(buf, &fragm_hdr, INT_H_SIZE);
			fragm_crs = INT_H_SIZE;
			fragm_rest = fragm_sz;
		}
	} while (rest > 0);

	/*
	 * Now we have a buffer chain. Select a link and check
	 * that packet size is still OK
	 */
	node = tipc_node_find(destaddr);
	if (likely(node)) {
		tipc_node_lock(node);
		l_ptr = node->active_links[sender->ref & 1];
		if (!l_ptr) {
			tipc_node_unlock(node);
			goto reject;
		}
		if (l_ptr->max_pkt < max_pkt) {
			sender->max_pkt = l_ptr->max_pkt;
			tipc_node_unlock(node);
			for (; buf_chain; buf_chain = buf) {
				buf = buf_chain->next;
				kfree_skb(buf_chain);
			}
			goto again;
		}
	} else {
reject:
		for (; buf_chain; buf_chain = buf) {
			buf = buf_chain->next;
			kfree_skb(buf_chain);
		}
		return tipc_port_reject_sections(sender, hdr, msg_sect, num_sect,
						 total_len, TIPC_ERR_NO_NODE);
	}

	/* Append chain of fragments to send queue & send them */
	l_ptr->long_msg_seq_no++;
	link_add_chain_to_outqueue(l_ptr, buf_chain, l_ptr->long_msg_seq_no);
	l_ptr->stats.sent_fragments += fragm_no;
	l_ptr->stats.sent_fragmented++;
	tipc_link_push_queue(l_ptr);
	tipc_node_unlock(node);
	return dsz;
}

/*
 * tipc_link_push_packet: Push one unsent packet to the media
 */
u32 tipc_link_push_packet(struct tipc_link *l_ptr)
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
		tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr);
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
		tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr);
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
			tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr);
			if (msg_user(msg) == MSG_BUNDLER)
				msg_set_type(msg, CLOSED_MSG);
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

	if (tipc_bearer_blocked(l_ptr->b_ptr))
		return;

	do {
		res = tipc_link_push_packet(l_ptr);
	} while (!res);
}

static void link_reset_all(unsigned long addr)
{
	struct tipc_node *n_ptr;
	char addr_string[16];
	u32 i;

	read_lock_bh(&tipc_net_lock);
	n_ptr = tipc_node_find((u32)addr);
	if (!n_ptr) {
		read_unlock_bh(&tipc_net_lock);
		return;	/* node no longer exists */
	}

	tipc_node_lock(n_ptr);

	pr_warn("Resetting all links to %s\n",
		tipc_addr_string_fill(addr_string, n_ptr->addr));

	for (i = 0; i < MAX_BEARERS; i++) {
		if (n_ptr->links[i]) {
			link_print(n_ptr->links[i], "Resetting link\n");
			tipc_link_reset(n_ptr->links[i]);
		}
	}

	tipc_node_unlock(n_ptr);
	read_unlock_bh(&tipc_net_lock);
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

		tipc_k_signal((Handler)link_reset_all, (unsigned long)n_ptr->addr);

		tipc_node_unlock(n_ptr);

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

	if (tipc_bearer_blocked(l_ptr->b_ptr)) {
		if (l_ptr->retransm_queue_size == 0) {
			l_ptr->retransm_queue_head = msg_seqno(msg);
			l_ptr->retransm_queue_size = retransmits;
		} else {
			pr_err("Unexpected retransmit on link %s (qsize=%d)\n",
			       l_ptr->name, l_ptr->retransm_queue_size);
		}
		return;
	} else {
		/* Detect repeated retransmit failures on unblocked bearer */
		if (l_ptr->last_retransmitted == msg_seqno(msg)) {
			if (++l_ptr->stale_count > 100) {
				link_retransmit_failure(l_ptr, buf);
				return;
			}
		} else {
			l_ptr->last_retransmitted = msg_seqno(msg);
			l_ptr->stale_count = 1;
		}
	}

	while (retransmits && (buf != l_ptr->next_out) && buf) {
		msg = buf_msg(buf);
		msg_set_ack(msg, mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
		tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr);
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
 * tipc_recv_msg - process TIPC messages arriving from off-node
 * @head: pointer to message buffer chain
 * @tb_ptr: pointer to bearer message arrived on
 *
 * Invoked with no locks held.  Bearer pointer must point to a valid bearer
 * structure (i.e. cannot be NULL), but bearer can be inactive.
 */
void tipc_recv_msg(struct sk_buff *head, struct tipc_bearer *b_ptr)
{
	read_lock_bh(&tipc_net_lock);
	while (head) {
		struct tipc_node *n_ptr;
		struct tipc_link *l_ptr;
		struct sk_buff *crs;
		struct sk_buff *buf = head;
		struct tipc_msg *msg;
		u32 seq_no;
		u32 ackd;
		u32 released = 0;
		int type;

		head = head->next;

		/* Ensure bearer is still enabled */
		if (unlikely(!b_ptr->active))
			goto cont;

		/* Ensure message is well-formed */
		if (unlikely(!link_recv_buf_validate(buf)))
			goto cont;

		/* Ensure message data is a single contiguous unit */
		if (unlikely(skb_linearize(buf)))
			goto cont;

		/* Handle arrival of a non-unicast link message */
		msg = buf_msg(buf);

		if (unlikely(msg_non_seq(msg))) {
			if (msg_user(msg) ==  LINK_CONFIG)
				tipc_disc_recv_msg(buf, b_ptr);
			else
				tipc_bclink_recv_pkt(buf);
			continue;
		}

		/* Discard unicast link messages destined for another node */
		if (unlikely(!msg_short(msg) &&
			     (msg_destnode(msg) != tipc_own_addr)))
			goto cont;

		/* Locate neighboring node that sent message */
		n_ptr = tipc_node_find(msg_prevnode(msg));
		if (unlikely(!n_ptr))
			goto cont;
		tipc_node_lock(n_ptr);

		/* Locate unicast link endpoint that should handle message */
		l_ptr = n_ptr->links[b_ptr->identity];
		if (unlikely(!l_ptr)) {
			tipc_node_unlock(n_ptr);
			goto cont;
		}

		/* Verify that communication with node is currently allowed */
		if ((n_ptr->block_setup & WAIT_PEER_DOWN) &&
			msg_user(msg) == LINK_PROTOCOL &&
			(msg_type(msg) == RESET_MSG ||
					msg_type(msg) == ACTIVATE_MSG) &&
			!msg_redundant_link(msg))
			n_ptr->block_setup &= ~WAIT_PEER_DOWN;

		if (n_ptr->block_setup) {
			tipc_node_unlock(n_ptr);
			goto cont;
		}

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
		if (unlikely(++l_ptr->unacked_window >= TIPC_MIN_LINK_WIN)) {
			l_ptr->stats.sent_acks++;
			tipc_link_send_proto_msg(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
		}

		/* Now (finally!) process the incoming message */
protocol_check:
		if (likely(link_working_working(l_ptr))) {
			if (likely(seq_no == mod(l_ptr->next_in_no))) {
				l_ptr->next_in_no++;
				if (unlikely(l_ptr->oldest_deferred_in))
					head = link_insert_deferred_queue(l_ptr,
									  head);
deliver:
				if (likely(msg_isdata(msg))) {
					tipc_node_unlock(n_ptr);
					tipc_port_recv_msg(buf);
					continue;
				}
				switch (msg_user(msg)) {
					int ret;
				case MSG_BUNDLER:
					l_ptr->stats.recv_bundles++;
					l_ptr->stats.recv_bundled +=
						msg_msgcnt(msg);
					tipc_node_unlock(n_ptr);
					tipc_link_recv_bundle(buf);
					continue;
				case NAME_DISTRIBUTOR:
					n_ptr->bclink.recv_permitted = true;
					tipc_node_unlock(n_ptr);
					tipc_named_recv(buf);
					continue;
				case BCAST_PROTOCOL:
					tipc_link_recv_sync(n_ptr, buf);
					tipc_node_unlock(n_ptr);
					continue;
				case CONN_MANAGER:
					tipc_node_unlock(n_ptr);
					tipc_port_recv_proto_msg(buf);
					continue;
				case MSG_FRAGMENTER:
					l_ptr->stats.recv_fragments++;
					ret = tipc_link_recv_fragment(
						&l_ptr->defragm_buf,
						&buf, &msg);
					if (ret == 1) {
						l_ptr->stats.recv_fragmented++;
						goto deliver;
					}
					if (ret == -1)
						l_ptr->next_in_no--;
					break;
				case CHANGEOVER_PROTOCOL:
					type = msg_type(msg);
					if (link_recv_changeover_msg(&l_ptr,
								     &buf)) {
						msg = buf_msg(buf);
						seq_no = msg_seqno(msg);
						if (type == ORIGINAL_MSG)
							goto deliver;
						goto protocol_check;
					}
					break;
				default:
					kfree_skb(buf);
					buf = NULL;
					break;
				}
				tipc_node_unlock(n_ptr);
				tipc_net_route_msg(buf);
				continue;
			}
			link_handle_out_of_seq_msg(l_ptr, buf);
			head = link_insert_deferred_queue(l_ptr, head);
			tipc_node_unlock(n_ptr);
			continue;
		}

		/* Link is not in state WORKING_WORKING */
		if (msg_user(msg) == LINK_PROTOCOL) {
			link_recv_proto_msg(l_ptr, buf);
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
		tipc_node_unlock(n_ptr);
cont:
		kfree_skb(buf);
	}
	read_unlock_bh(&tipc_net_lock);
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
		link_recv_proto_msg(l_ptr, buf);
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
		if ((l_ptr->deferred_inqueue_sz % 16) == 1)
			tipc_link_send_proto_msg(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
	} else
		l_ptr->stats.duplicates++;
}

/*
 * Send protocol message to the other endpoint.
 */
void tipc_link_send_proto_msg(struct tipc_link *l_ptr, u32 msg_typ,
				int probe_msg, u32 gap, u32 tolerance,
				u32 priority, u32 ack_mtu)
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

	if (link_blocked(l_ptr))
		return;

	/* Abort non-RESET send if communication with node is prohibited */
	if ((l_ptr->owner->block_setup) && (msg_typ != RESET_MSG))
		return;

	/* Create protocol message with "out-of-sequence" sequence number */
	msg_set_type(msg, msg_typ);
	msg_set_net_plane(msg, l_ptr->b_ptr->net_plane);
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

	/* Defer message if bearer is already blocked */
	if (tipc_bearer_blocked(l_ptr->b_ptr)) {
		l_ptr->proto_msg_queue = buf;
		return;
	}

	tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr);
	l_ptr->unacked_window = 0;
	kfree_skb(buf);
}

/*
 * Receive protocol message :
 * Note that network plane id propagates through the network, and may
 * change at any time. The node with lowest address rules
 */
static void link_recv_proto_msg(struct tipc_link *l_ptr, struct sk_buff *buf)
{
	u32 rec_gap = 0;
	u32 max_pkt_info;
	u32 max_pkt_ack;
	u32 msg_tol;
	struct tipc_msg *msg = buf_msg(buf);

	if (link_blocked(l_ptr))
		goto exit;

	/* record unnumbered packet arrival (force mismatch on next timeout) */
	l_ptr->checkpoint--;

	if (l_ptr->b_ptr->net_plane != msg_net_plane(msg))
		if (tipc_own_addr > msg_prevnode(msg))
			l_ptr->b_ptr->net_plane = msg_net_plane(msg);

	l_ptr->owner->permit_changeover = msg_redundant_link(msg);

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
			l_ptr->owner->block_setup = WAIT_NODE_DOWN;
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
			tipc_link_send_proto_msg(l_ptr, STATE_MSG,
						 0, rec_gap, 0, 0, max_pkt_ack);
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


/*
 * tipc_link_tunnel(): Send one message via a link belonging to
 * another bearer. Owner node is locked.
 */
static void tipc_link_tunnel(struct tipc_link *l_ptr,
			     struct tipc_msg *tunnel_hdr,
			     struct tipc_msg  *msg,
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
	tipc_link_send_buf(tunnel, buf);
}



/*
 * changeover(): Send whole message queue via the remaining link
 *               Owner node is locked.
 */
void tipc_link_changeover(struct tipc_link *l_ptr)
{
	u32 msgcount = l_ptr->out_queue_size;
	struct sk_buff *crs = l_ptr->first_out;
	struct tipc_link *tunnel = l_ptr->owner->active_links[0];
	struct tipc_msg tunnel_hdr;
	int split_bundles;

	if (!tunnel)
		return;

	if (!l_ptr->owner->permit_changeover) {
		pr_warn("%speer did not permit changeover\n", link_co_err);
		return;
	}

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
			tipc_link_send_buf(tunnel, buf);
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
				tipc_link_tunnel(l_ptr, &tunnel_hdr, m,
						 msg_link_selector(m));
				pos += align(msg_size(m));
				m = (struct tipc_msg *)pos;
			}
		} else {
			tipc_link_tunnel(l_ptr, &tunnel_hdr, msg,
					 msg_link_selector(msg));
		}
		crs = crs->next;
	}
}

void tipc_link_send_duplicate(struct tipc_link *l_ptr, struct tipc_link *tunnel)
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
		tipc_link_send_buf(tunnel, outbuf);
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

/*
 *  link_recv_changeover_msg(): Receive tunneled packet sent
 *  via other link. Node is locked. Return extracted buffer.
 */
static int link_recv_changeover_msg(struct tipc_link **l_ptr,
				    struct sk_buff **buf)
{
	struct sk_buff *tunnel_buf = *buf;
	struct tipc_link *dest_link;
	struct tipc_msg *msg;
	struct tipc_msg *tunnel_msg = buf_msg(tunnel_buf);
	u32 msg_typ = msg_type(tunnel_msg);
	u32 msg_count = msg_msgcnt(tunnel_msg);

	dest_link = (*l_ptr)->owner->links[msg_bearer_id(tunnel_msg)];
	if (!dest_link)
		goto exit;
	if (dest_link == *l_ptr) {
		pr_err("Unexpected changeover message on link <%s>\n",
		       (*l_ptr)->name);
		goto exit;
	}
	*l_ptr = dest_link;
	msg = msg_get_wrapped(tunnel_msg);

	if (msg_typ == DUPLICATE_MSG) {
		if (less(msg_seqno(msg), mod(dest_link->next_in_no)))
			goto exit;
		*buf = buf_extract(tunnel_buf, INT_H_SIZE);
		if (*buf == NULL) {
			pr_warn("%sduplicate msg dropped\n", link_co_err);
			goto exit;
		}
		kfree_skb(tunnel_buf);
		return 1;
	}

	/* First original message ?: */
	if (tipc_link_is_up(dest_link)) {
		pr_info("%s<%s>, changeover initiated by peer\n", link_rst_msg,
			dest_link->name);
		tipc_link_reset(dest_link);
		dest_link->exp_msg_count = msg_count;
		if (!msg_count)
			goto exit;
	} else if (dest_link->exp_msg_count == START_CHANGEOVER) {
		dest_link->exp_msg_count = msg_count;
		if (!msg_count)
			goto exit;
	}

	/* Receive original message */
	if (dest_link->exp_msg_count == 0) {
		pr_warn("%sgot too many tunnelled messages\n", link_co_err);
		goto exit;
	}
	dest_link->exp_msg_count--;
	if (less(msg_seqno(msg), dest_link->reset_checkpoint)) {
		goto exit;
	} else {
		*buf = buf_extract(tunnel_buf, INT_H_SIZE);
		if (*buf != NULL) {
			kfree_skb(tunnel_buf);
			return 1;
		} else {
			pr_warn("%soriginal msg dropped\n", link_co_err);
		}
	}
exit:
	*buf = NULL;
	kfree_skb(tunnel_buf);
	return 0;
}

/*
 *  Bundler functionality:
 */
void tipc_link_recv_bundle(struct sk_buff *buf)
{
	u32 msgcount = msg_msgcnt(buf_msg(buf));
	u32 pos = INT_H_SIZE;
	struct sk_buff *obuf;

	while (msgcount--) {
		obuf = buf_extract(buf, pos);
		if (obuf == NULL) {
			pr_warn("Link unable to unbundle message(s)\n");
			break;
		}
		pos += align(msg_size(buf_msg(obuf)));
		tipc_net_route_msg(obuf);
	}
	kfree_skb(buf);
}

/*
 *  Fragmentation/defragmentation:
 */

/*
 * link_send_long_buf: Entry for buffers needing fragmentation.
 * The buffer is complete, inclusive total message length.
 * Returns user data length.
 */
static int link_send_long_buf(struct tipc_link *l_ptr, struct sk_buff *buf)
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
			while (buf_chain) {
				buf = buf_chain;
				buf_chain = buf_chain->next;
				kfree_skb(buf);
			}
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

/*
 * A pending message being re-assembled must store certain values
 * to handle subsequent fragments correctly. The following functions
 * help storing these values in unused, available fields in the
 * pending message. This makes dynamic memory allocation unnecessary.
 */
static void set_long_msg_seqno(struct sk_buff *buf, u32 seqno)
{
	msg_set_seqno(buf_msg(buf), seqno);
}

static u32 get_fragm_size(struct sk_buff *buf)
{
	return msg_ack(buf_msg(buf));
}

static void set_fragm_size(struct sk_buff *buf, u32 sz)
{
	msg_set_ack(buf_msg(buf), sz);
}

static u32 get_expected_frags(struct sk_buff *buf)
{
	return msg_bcast_ack(buf_msg(buf));
}

static void set_expected_frags(struct sk_buff *buf, u32 exp)
{
	msg_set_bcast_ack(buf_msg(buf), exp);
}

static u32 get_timer_cnt(struct sk_buff *buf)
{
	return msg_reroute_cnt(buf_msg(buf));
}

static void incr_timer_cnt(struct sk_buff *buf)
{
	msg_incr_reroute_cnt(buf_msg(buf));
}

/*
 * tipc_link_recv_fragment(): Called with node lock on. Returns
 * the reassembled buffer if message is complete.
 */
int tipc_link_recv_fragment(struct sk_buff **pending, struct sk_buff **fb,
			    struct tipc_msg **m)
{
	struct sk_buff *prev = NULL;
	struct sk_buff *fbuf = *fb;
	struct tipc_msg *fragm = buf_msg(fbuf);
	struct sk_buff *pbuf = *pending;
	u32 long_msg_seq_no = msg_long_msgno(fragm);

	*fb = NULL;

	/* Is there an incomplete message waiting for this fragment? */
	while (pbuf && ((buf_seqno(pbuf) != long_msg_seq_no) ||
			(msg_orignode(fragm) != msg_orignode(buf_msg(pbuf))))) {
		prev = pbuf;
		pbuf = pbuf->next;
	}

	if (!pbuf && (msg_type(fragm) == FIRST_FRAGMENT)) {
		struct tipc_msg *imsg = (struct tipc_msg *)msg_data(fragm);
		u32 msg_sz = msg_size(imsg);
		u32 fragm_sz = msg_data_sz(fragm);
		u32 exp_fragm_cnt = msg_sz/fragm_sz + !!(msg_sz % fragm_sz);
		u32 max =  TIPC_MAX_USER_MSG_SIZE + NAMED_H_SIZE;
		if (msg_type(imsg) == TIPC_MCAST_MSG)
			max = TIPC_MAX_USER_MSG_SIZE + MCAST_H_SIZE;
		if (msg_size(imsg) > max) {
			kfree_skb(fbuf);
			return 0;
		}
		pbuf = tipc_buf_acquire(msg_size(imsg));
		if (pbuf != NULL) {
			pbuf->next = *pending;
			*pending = pbuf;
			skb_copy_to_linear_data(pbuf, imsg,
						msg_data_sz(fragm));
			/*  Prepare buffer for subsequent fragments. */
			set_long_msg_seqno(pbuf, long_msg_seq_no);
			set_fragm_size(pbuf, fragm_sz);
			set_expected_frags(pbuf, exp_fragm_cnt - 1);
		} else {
			pr_debug("Link unable to reassemble fragmented message\n");
			kfree_skb(fbuf);
			return -1;
		}
		kfree_skb(fbuf);
		return 0;
	} else if (pbuf && (msg_type(fragm) != FIRST_FRAGMENT)) {
		u32 dsz = msg_data_sz(fragm);
		u32 fsz = get_fragm_size(pbuf);
		u32 crs = ((msg_fragm_no(fragm) - 1) * fsz);
		u32 exp_frags = get_expected_frags(pbuf) - 1;
		skb_copy_to_linear_data_offset(pbuf, crs,
					       msg_data(fragm), dsz);
		kfree_skb(fbuf);

		/* Is message complete? */
		if (exp_frags == 0) {
			if (prev)
				prev->next = pbuf->next;
			else
				*pending = pbuf->next;
			msg_reset_reroute_cnt(buf_msg(pbuf));
			*fb = pbuf;
			*m = buf_msg(pbuf);
			return 1;
		}
		set_expected_frags(pbuf, exp_frags);
		return 0;
	}
	kfree_skb(fbuf);
	return 0;
}

/**
 * link_check_defragm_bufs - flush stale incoming message fragments
 * @l_ptr: pointer to link
 */
static void link_check_defragm_bufs(struct tipc_link *l_ptr)
{
	struct sk_buff *prev = NULL;
	struct sk_buff *next = NULL;
	struct sk_buff *buf = l_ptr->defragm_buf;

	if (!buf)
		return;
	if (!link_working_working(l_ptr))
		return;
	while (buf) {
		u32 cnt = get_timer_cnt(buf);

		next = buf->next;
		if (cnt < 4) {
			incr_timer_cnt(buf);
			prev = buf;
		} else {
			if (prev)
				prev->next = buf->next;
			else
				l_ptr->defragm_buf = buf->next;
			kfree_skb(buf);
		}
		buf = next;
	}
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

/**
 * link_find_link - locate link by name
 * @name: ptr to link name string
 * @node: ptr to area to be filled with ptr to associated node
 *
 * Caller must hold 'tipc_net_lock' to ensure node and bearer are not deleted;
 * this also prevents link deletion.
 *
 * Returns pointer to link (or 0 if invalid link name).
 */
static struct tipc_link *link_find_link(const char *name,
					struct tipc_node **node)
{
	struct tipc_link_name link_name_parts;
	struct tipc_bearer *b_ptr;
	struct tipc_link *l_ptr;

	if (!link_name_validate(name, &link_name_parts))
		return NULL;

	b_ptr = tipc_bearer_find_interface(link_name_parts.if_local);
	if (!b_ptr)
		return NULL;

	*node = tipc_node_find(link_name_parts.addr_peer);
	if (!*node)
		return NULL;

	l_ptr = (*node)->links[b_ptr->identity];
	if (!l_ptr || strcmp(l_ptr->name, name))
		return NULL;

	return l_ptr;
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
 * Caller must hold 'tipc_net_lock' to ensure link/bearer/media is not deleted.
 *
 * Returns 0 if value updated and negative value on error.
 */
static int link_cmd_set_value(const char *name, u32 new_value, u16 cmd)
{
	struct tipc_node *node;
	struct tipc_link *l_ptr;
	struct tipc_bearer *b_ptr;
	struct tipc_media *m_ptr;

	l_ptr = link_find_link(name, &node);
	if (l_ptr) {
		/*
		 * acquire node lock for tipc_link_send_proto_msg().
		 * see "TIPC locking policy" in net.c.
		 */
		tipc_node_lock(node);
		switch (cmd) {
		case TIPC_CMD_SET_LINK_TOL:
			link_set_supervision_props(l_ptr, new_value);
			tipc_link_send_proto_msg(l_ptr,
				STATE_MSG, 0, 0, new_value, 0, 0);
			break;
		case TIPC_CMD_SET_LINK_PRI:
			l_ptr->priority = new_value;
			tipc_link_send_proto_msg(l_ptr,
				STATE_MSG, 0, 0, 0, new_value, 0);
			break;
		case TIPC_CMD_SET_LINK_WINDOW:
			tipc_link_set_queue_limits(l_ptr, new_value);
			break;
		}
		tipc_node_unlock(node);
		return 0;
	}

	b_ptr = tipc_bearer_find(name);
	if (b_ptr) {
		switch (cmd) {
		case TIPC_CMD_SET_LINK_TOL:
			b_ptr->tolerance = new_value;
			return 0;
		case TIPC_CMD_SET_LINK_PRI:
			b_ptr->priority = new_value;
			return 0;
		case TIPC_CMD_SET_LINK_WINDOW:
			b_ptr->window = new_value;
			return 0;
		}
		return -EINVAL;
	}

	m_ptr = tipc_media_find(name);
	if (!m_ptr)
		return -ENODEV;
	switch (cmd) {
	case TIPC_CMD_SET_LINK_TOL:
		m_ptr->tolerance = new_value;
		return 0;
	case TIPC_CMD_SET_LINK_PRI:
		m_ptr->priority = new_value;
		return 0;
	case TIPC_CMD_SET_LINK_WINDOW:
		m_ptr->window = new_value;
		return 0;
	}
	return -EINVAL;
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

	read_lock_bh(&tipc_net_lock);
	res = link_cmd_set_value(args->name, new_value, cmd);
	read_unlock_bh(&tipc_net_lock);
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

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_LINK_NAME))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	link_name = (char *)TLV_DATA(req_tlv_area);
	if (!strcmp(link_name, tipc_bclink_name)) {
		if (tipc_bclink_reset_stats())
			return tipc_cfg_reply_error_string("link not found");
		return tipc_cfg_reply_none();
	}

	read_lock_bh(&tipc_net_lock);
	l_ptr = link_find_link(link_name, &node);
	if (!l_ptr) {
		read_unlock_bh(&tipc_net_lock);
		return tipc_cfg_reply_error_string("link not found");
	}

	tipc_node_lock(node);
	link_reset_statistics(l_ptr);
	tipc_node_unlock(node);
	read_unlock_bh(&tipc_net_lock);
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
	int ret;

	if (!strcmp(name, tipc_bclink_name))
		return tipc_bclink_stats(buf, buf_size);

	read_lock_bh(&tipc_net_lock);
	l = link_find_link(name, &node);
	if (!l) {
		read_unlock_bh(&tipc_net_lock);
		return 0;
	}
	tipc_node_lock(node);
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
	read_unlock_bh(&tipc_net_lock);
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

	read_lock_bh(&tipc_net_lock);
	n_ptr = tipc_node_find(dest);
	if (n_ptr) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector & 1];
		if (l_ptr)
			res = l_ptr->max_pkt;
		tipc_node_unlock(n_ptr);
	}
	read_unlock_bh(&tipc_net_lock);
	return res;
}

static void link_print(struct tipc_link *l_ptr, const char *str)
{
	pr_info("%s Link %x<%s>:", str, l_ptr->addr, l_ptr->b_ptr->name);

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

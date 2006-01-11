/*
 * net/tipc/link.c: TIPC link code
 * 
 * Copyright (c) 1996-2006, Ericsson AB
 * Copyright (c) 2004-2005, Wind River Systems
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
#include "dbg.h"
#include "link.h"
#include "net.h"
#include "node.h"
#include "port.h"
#include "addr.h"
#include "node_subscr.h"
#include "name_distr.h"
#include "bearer.h"
#include "name_table.h"
#include "discover.h"
#include "config.h"
#include "bcast.h"


/* 
 * Limit for deferred reception queue: 
 */

#define DEF_QUEUE_LIMIT 256u

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
 * struct link_name - deconstructed link name
 * @addr_local: network address of node at this end
 * @if_local: name of interface at this end
 * @addr_peer: network address of node at far end
 * @if_peer: name of interface at far end
 */

struct link_name {
	u32 addr_local;
	char if_local[TIPC_MAX_IF_NAME];
	u32 addr_peer;
	char if_peer[TIPC_MAX_IF_NAME];
};

#if 0

/* LINK EVENT CODE IS NOT SUPPORTED AT PRESENT */

/** 
 * struct link_event - link up/down event notification
 */

struct link_event {
	u32 addr;
	int up;
	void (*fcn)(u32, char *, int);
	char name[TIPC_MAX_LINK_NAME];
};

#endif

static void link_handle_out_of_seq_msg(struct link *l_ptr,
				       struct sk_buff *buf);
static void link_recv_proto_msg(struct link *l_ptr, struct sk_buff *buf);
static int  link_recv_changeover_msg(struct link **l_ptr, struct sk_buff **buf);
static void link_set_supervision_props(struct link *l_ptr, u32 tolerance);
static int  link_send_sections_long(struct port *sender,
				    struct iovec const *msg_sect,
				    u32 num_sect, u32 destnode);
static void link_check_defragm_bufs(struct link *l_ptr);
static void link_state_event(struct link *l_ptr, u32 event);
static void link_reset_statistics(struct link *l_ptr);
static void link_print(struct link *l_ptr, struct print_buf *buf, 
		       const char *str);

/*
 * Debugging code used by link routines only
 *
 * When debugging link problems on a system that has multiple links,
 * the standard TIPC debugging routines may not be useful since they
 * allow the output from multiple links to be intermixed.  For this reason
 * routines of the form "dbg_link_XXX()" have been created that will capture
 * debug info into a link's personal print buffer, which can then be dumped
 * into the TIPC system log (LOG) upon request.
 *
 * To enable per-link debugging, use LINK_LOG_BUF_SIZE to specify the size
 * of the print buffer used by each link.  If LINK_LOG_BUF_SIZE is set to 0,
 * the dbg_link_XXX() routines simply send their output to the standard 
 * debug print buffer (DBG_OUTPUT), if it has been defined; this can be useful
 * when there is only a single link in the system being debugged.
 *
 * Notes:
 * - When enabled, LINK_LOG_BUF_SIZE should be set to at least 1000 (bytes)
 * - "l_ptr" must be valid when using dbg_link_XXX() macros  
 */

#define LINK_LOG_BUF_SIZE 0

#define dbg_link(fmt, arg...)  do {if (LINK_LOG_BUF_SIZE) tipc_printf(&l_ptr->print_buf, fmt, ## arg); } while(0)
#define dbg_link_msg(msg, txt) do {if (LINK_LOG_BUF_SIZE) msg_print(&l_ptr->print_buf, msg, txt); } while(0)
#define dbg_link_state(txt) do {if (LINK_LOG_BUF_SIZE) link_print(l_ptr, &l_ptr->print_buf, txt); } while(0)
#define dbg_link_dump() do { \
	if (LINK_LOG_BUF_SIZE) { \
		tipc_printf(LOG, "\n\nDumping link <%s>:\n", l_ptr->name); \
		printbuf_move(LOG, &l_ptr->print_buf); \
	} \
} while (0)

static inline void dbg_print_link(struct link *l_ptr, const char *str)
{
	if (DBG_OUTPUT)
		link_print(l_ptr, DBG_OUTPUT, str);
}

static inline void dbg_print_buf_chain(struct sk_buff *root_buf)
{
	if (DBG_OUTPUT) {
		struct sk_buff *buf = root_buf;

		while (buf) {
			msg_dbg(buf_msg(buf), "In chain: ");
			buf = buf->next;
		}
	}
}

/*
 *  Simple inlined link routines
 */

static inline unsigned int align(unsigned int i)
{
	return (i + 3) & ~3u;
}

static inline int link_working_working(struct link *l_ptr)
{
	return (l_ptr->state == WORKING_WORKING);
}

static inline int link_working_unknown(struct link *l_ptr)
{
	return (l_ptr->state == WORKING_UNKNOWN);
}

static inline int link_reset_unknown(struct link *l_ptr)
{
	return (l_ptr->state == RESET_UNKNOWN);
}

static inline int link_reset_reset(struct link *l_ptr)
{
	return (l_ptr->state == RESET_RESET);
}

static inline int link_blocked(struct link *l_ptr)
{
	return (l_ptr->exp_msg_count || l_ptr->blocked);
}

static inline int link_congested(struct link *l_ptr)
{
	return (l_ptr->out_queue_size >= l_ptr->queue_limit[0]);
}

static inline u32 link_max_pkt(struct link *l_ptr)
{
	return l_ptr->max_pkt;
}

static inline void link_init_max_pkt(struct link *l_ptr)
{
	u32 max_pkt;
	
	max_pkt = (l_ptr->b_ptr->publ.mtu & ~3);
	if (max_pkt > MAX_MSG_SIZE)
		max_pkt = MAX_MSG_SIZE;

        l_ptr->max_pkt_target = max_pkt;
	if (l_ptr->max_pkt_target < MAX_PKT_DEFAULT)
		l_ptr->max_pkt = l_ptr->max_pkt_target;
	else 
		l_ptr->max_pkt = MAX_PKT_DEFAULT;

        l_ptr->max_pkt_probes = 0;
}

static inline u32 link_next_sent(struct link *l_ptr)
{
	if (l_ptr->next_out)
		return msg_seqno(buf_msg(l_ptr->next_out));
	return mod(l_ptr->next_out_no);
}

static inline u32 link_last_sent(struct link *l_ptr)
{
	return mod(link_next_sent(l_ptr) - 1);
}

/*
 *  Simple non-inlined link routines (i.e. referenced outside this file)
 */

int link_is_up(struct link *l_ptr)
{
	if (!l_ptr)
		return 0;
	return (link_working_working(l_ptr) || link_working_unknown(l_ptr));
}

int link_is_active(struct link *l_ptr)
{
	return ((l_ptr->owner->active_links[0] == l_ptr) ||
		(l_ptr->owner->active_links[1] == l_ptr));
}

/**
 * link_name_validate - validate & (optionally) deconstruct link name
 * @name - ptr to link name string
 * @name_parts - ptr to area for link name components (or NULL if not needed)
 * 
 * Returns 1 if link name is valid, otherwise 0.
 */

static int link_name_validate(const char *name, struct link_name *name_parts)
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
	if ((if_local = strchr(addr_local, ':')) == NULL)
		return 0;
	*(if_local++) = 0;
	if ((addr_peer = strchr(if_local, '-')) == NULL)
		return 0;
	*(addr_peer++) = 0;
	if_local_len = addr_peer - if_local;
	if ((if_peer = strchr(addr_peer, ':')) == NULL)
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
	    (if_peer_len  <= 1) || (if_peer_len  > TIPC_MAX_IF_NAME) || 
	    (strspn(if_local, tipc_alphabet) != (if_local_len - 1)) ||
	    (strspn(if_peer, tipc_alphabet) != (if_peer_len - 1)))
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
 * This routine must not grab "net_lock" to avoid a potential deadlock conflict
 * with link_delete().  (There is no risk that the node will be deleted by
 * another thread because link_delete() always cancels the link timer before
 * node_delete() is called.)
 */

static void link_timeout(struct link *l_ptr)
{
	node_lock(l_ptr->owner);

	/* update counters used in statistical profiling of send traffic */

	l_ptr->stats.accu_queue_sz += l_ptr->out_queue_size;
	l_ptr->stats.queue_sz_counts++;

	if (l_ptr->out_queue_size > l_ptr->stats.max_queue_sz)
		l_ptr->stats.max_queue_sz = l_ptr->out_queue_size;

	if (l_ptr->first_out) {
		struct tipc_msg *msg = buf_msg(l_ptr->first_out);
		u32 length = msg_size(msg);

		if ((msg_user(msg) == MSG_FRAGMENTER)
		    && (msg_type(msg) == FIRST_FRAGMENT)) {
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
		link_push_queue(l_ptr);

	node_unlock(l_ptr->owner);
}

static inline void link_set_timer(struct link *l_ptr, u32 time)
{
	k_start_timer(&l_ptr->timer, time);
}

/**
 * link_create - create a new link
 * @b_ptr: pointer to associated bearer
 * @peer: network address of node at other end of link
 * @media_addr: media address to use when sending messages over link
 * 
 * Returns pointer to link.
 */

struct link *link_create(struct bearer *b_ptr, const u32 peer,
			 const struct tipc_media_addr *media_addr)
{
	struct link *l_ptr;
	struct tipc_msg *msg;
	char *if_name;

	l_ptr = (struct link *)kmalloc(sizeof(*l_ptr), GFP_ATOMIC);
	if (!l_ptr) {
		warn("Memory squeeze; Failed to create link\n");
		return NULL;
	}
	memset(l_ptr, 0, sizeof(*l_ptr));

	l_ptr->addr = peer;
	if_name = strchr(b_ptr->publ.name, ':') + 1;
	sprintf(l_ptr->name, "%u.%u.%u:%s-%u.%u.%u:",
		tipc_zone(tipc_own_addr), tipc_cluster(tipc_own_addr),
		tipc_node(tipc_own_addr), 
		if_name,
		tipc_zone(peer), tipc_cluster(peer), tipc_node(peer));
		/* note: peer i/f is appended to link name by reset/activate */
	memcpy(&l_ptr->media_addr, media_addr, sizeof(*media_addr));
	k_init_timer(&l_ptr->timer, (Handler)link_timeout, (unsigned long)l_ptr);
	list_add_tail(&l_ptr->link_list, &b_ptr->links);
	l_ptr->checkpoint = 1;
	l_ptr->b_ptr = b_ptr;
	link_set_supervision_props(l_ptr, b_ptr->media->tolerance);
	l_ptr->state = RESET_UNKNOWN;

	l_ptr->pmsg = (struct tipc_msg *)&l_ptr->proto_msg;
	msg = l_ptr->pmsg;
	msg_init(msg, LINK_PROTOCOL, RESET_MSG, TIPC_OK, INT_H_SIZE, l_ptr->addr);
	msg_set_size(msg, sizeof(l_ptr->proto_msg));
	msg_set_session(msg, tipc_random);
	msg_set_bearer_id(msg, b_ptr->identity);
	strcpy((char *)msg_data(msg), if_name);

	l_ptr->priority = b_ptr->priority;
	link_set_queue_limits(l_ptr, b_ptr->media->window);

	link_init_max_pkt(l_ptr);

	l_ptr->next_out_no = 1;
	INIT_LIST_HEAD(&l_ptr->waiting_ports);

	link_reset_statistics(l_ptr);

	l_ptr->owner = node_attach_link(l_ptr);
	if (!l_ptr->owner) {
		kfree(l_ptr);
		return NULL;
	}

	if (LINK_LOG_BUF_SIZE) {
		char *pb = kmalloc(LINK_LOG_BUF_SIZE, GFP_ATOMIC);

		if (!pb) {
			kfree(l_ptr);
			warn("Memory squeeze; Failed to create link\n");
			return NULL;
		}
		printbuf_init(&l_ptr->print_buf, pb, LINK_LOG_BUF_SIZE);
	}

	k_signal((Handler)link_start, (unsigned long)l_ptr);

	dbg("link_create(): tolerance = %u,cont intv = %u, abort_limit = %u\n",
	    l_ptr->tolerance, l_ptr->continuity_interval, l_ptr->abort_limit);
	
	return l_ptr;
}

/** 
 * link_delete - delete a link
 * @l_ptr: pointer to link
 * 
 * Note: 'net_lock' is write_locked, bearer is locked.
 * This routine must not grab the node lock until after link timer cancellation
 * to avoid a potential deadlock situation.  
 */

void link_delete(struct link *l_ptr)
{
	if (!l_ptr) {
		err("Attempt to delete non-existent link\n");
		return;
	}

	dbg("link_delete()\n");

	k_cancel_timer(&l_ptr->timer);
	
	node_lock(l_ptr->owner);
	link_reset(l_ptr);
	node_detach_link(l_ptr->owner, l_ptr);
	link_stop(l_ptr);
	list_del_init(&l_ptr->link_list);
	if (LINK_LOG_BUF_SIZE)
		kfree(l_ptr->print_buf.buf);
	node_unlock(l_ptr->owner);
	k_term_timer(&l_ptr->timer);
	kfree(l_ptr);
}

void link_start(struct link *l_ptr)
{
	dbg("link_start %x\n", l_ptr);
	link_state_event(l_ptr, STARTING_EVT);
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

static int link_schedule_port(struct link *l_ptr, u32 origport, u32 sz)
{
	struct port *p_ptr;

	spin_lock_bh(&port_list_lock);
	p_ptr = port_lock(origport);
	if (p_ptr) {
		if (!p_ptr->wakeup)
			goto exit;
		if (!list_empty(&p_ptr->wait_list))
			goto exit;
		p_ptr->congested_link = l_ptr;
		p_ptr->publ.congested = 1;
		p_ptr->waiting_pkts = 1 + ((sz - 1) / link_max_pkt(l_ptr));
		list_add_tail(&p_ptr->wait_list, &l_ptr->waiting_ports);
		l_ptr->stats.link_congs++;
exit:
		port_unlock(p_ptr);
	}
	spin_unlock_bh(&port_list_lock);
	return -ELINKCONG;
}

void link_wakeup_ports(struct link *l_ptr, int all)
{
	struct port *p_ptr;
	struct port *temp_p_ptr;
	int win = l_ptr->queue_limit[0] - l_ptr->out_queue_size;

	if (all)
		win = 100000;
	if (win <= 0)
		return;
	if (!spin_trylock_bh(&port_list_lock))
		return;
	if (link_congested(l_ptr))
		goto exit;
	list_for_each_entry_safe(p_ptr, temp_p_ptr, &l_ptr->waiting_ports, 
				 wait_list) {
		if (win <= 0)
			break;
		list_del_init(&p_ptr->wait_list);
		p_ptr->congested_link = 0;
		assert(p_ptr->wakeup);
		spin_lock_bh(p_ptr->publ.lock);
		p_ptr->publ.congested = 0;
		p_ptr->wakeup(&p_ptr->publ);
		win -= p_ptr->waiting_pkts;
		spin_unlock_bh(p_ptr->publ.lock);
	}

exit:
	spin_unlock_bh(&port_list_lock);
}

/** 
 * link_release_outqueue - purge link's outbound message queue
 * @l_ptr: pointer to link
 */

static void link_release_outqueue(struct link *l_ptr)
{
	struct sk_buff *buf = l_ptr->first_out;
	struct sk_buff *next;

	while (buf) {
		next = buf->next;
		buf_discard(buf);
		buf = next;
	}
	l_ptr->first_out = NULL;
	l_ptr->out_queue_size = 0;
}

/**
 * link_reset_fragments - purge link's inbound message fragments queue
 * @l_ptr: pointer to link
 */

void link_reset_fragments(struct link *l_ptr)
{
	struct sk_buff *buf = l_ptr->defragm_buf;
	struct sk_buff *next;

	while (buf) {
		next = buf->next;
		buf_discard(buf);
		buf = next;
	}
	l_ptr->defragm_buf = NULL;
}

/** 
 * link_stop - purge all inbound and outbound messages associated with link
 * @l_ptr: pointer to link
 */

void link_stop(struct link *l_ptr)
{
	struct sk_buff *buf;
	struct sk_buff *next;

	buf = l_ptr->oldest_deferred_in;
	while (buf) {
		next = buf->next;
		buf_discard(buf);
		buf = next;
	}

	buf = l_ptr->first_out;
	while (buf) {
		next = buf->next;
		buf_discard(buf);
		buf = next;
	}

	link_reset_fragments(l_ptr);

	buf_discard(l_ptr->proto_msg_queue);
	l_ptr->proto_msg_queue = NULL;
}

#if 0

/* LINK EVENT CODE IS NOT SUPPORTED AT PRESENT */

static void link_recv_event(struct link_event *ev)
{
	ev->fcn(ev->addr, ev->name, ev->up);
	kfree(ev);
}

static void link_send_event(void (*fcn)(u32 a, char *n, int up),
			    struct link *l_ptr, int up)
{
	struct link_event *ev;
	
	ev = kmalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev) {
		warn("Link event allocation failure\n");
		return;
	}
	ev->addr = l_ptr->addr;
	ev->up = up;
	ev->fcn = fcn;
	memcpy(ev->name, l_ptr->name, TIPC_MAX_LINK_NAME);
	k_signal((Handler)link_recv_event, (unsigned long)ev);
}

#else

#define link_send_event(fcn, l_ptr, up) do { } while (0)

#endif

void link_reset(struct link *l_ptr)
{
	struct sk_buff *buf;
	u32 prev_state = l_ptr->state;
	u32 checkpoint = l_ptr->next_in_no;
	
	msg_set_session(l_ptr->pmsg, msg_session(l_ptr->pmsg) + 1);

        /* Link is down, accept any session: */
	l_ptr->peer_session = 0;

        /* Prepare for max packet size negotiation */
	link_init_max_pkt(l_ptr);
	
	l_ptr->state = RESET_UNKNOWN;
	dbg_link_state("Resetting Link\n");

	if ((prev_state == RESET_UNKNOWN) || (prev_state == RESET_RESET))
		return;

	node_link_down(l_ptr->owner, l_ptr);
	bearer_remove_dest(l_ptr->b_ptr, l_ptr->addr);
#if 0
	tipc_printf(CONS, "\nReset link <%s>\n", l_ptr->name);
	dbg_link_dump();
#endif
	if (node_has_active_links(l_ptr->owner) &&
	    l_ptr->owner->permit_changeover) {
		l_ptr->reset_checkpoint = checkpoint;
		l_ptr->exp_msg_count = START_CHANGEOVER;
	}

	/* Clean up all queues: */

	link_release_outqueue(l_ptr);
	buf_discard(l_ptr->proto_msg_queue);
	l_ptr->proto_msg_queue = NULL;
	buf = l_ptr->oldest_deferred_in;
	while (buf) {
		struct sk_buff *next = buf->next;
		buf_discard(buf);
		buf = next;
	}
	if (!list_empty(&l_ptr->waiting_ports))
		link_wakeup_ports(l_ptr, 1);

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

	link_send_event(cfg_link_event, l_ptr, 0);
	if (!in_own_cluster(l_ptr->addr))
		link_send_event(disc_link_event, l_ptr, 0);
}


static void link_activate(struct link *l_ptr)
{
	l_ptr->next_in_no = 1;
	node_link_up(l_ptr->owner, l_ptr);
	bearer_add_dest(l_ptr->b_ptr, l_ptr->addr);
	link_send_event(cfg_link_event, l_ptr, 1);
	if (!in_own_cluster(l_ptr->addr))
		link_send_event(disc_link_event, l_ptr, 1);
}

/**
 * link_state_event - link finite state machine
 * @l_ptr: pointer to link
 * @event: state machine event to process
 */

static void link_state_event(struct link *l_ptr, unsigned event)
{
	struct link *other; 
	u32 cont_intv = l_ptr->continuity_interval;

	if (!l_ptr->started && (event != STARTING_EVT))
		return;		/* Not yet. */

	if (link_blocked(l_ptr)) {
		if (event == TIMEOUT_EVT) {
			link_set_timer(l_ptr, cont_intv);
		}
		return;	  /* Changeover going on */
	}
	dbg_link("STATE_EV: <%s> ", l_ptr->name);

	switch (l_ptr->state) {
	case WORKING_WORKING:
		dbg_link("WW/");
		switch (event) {
		case TRAFFIC_MSG_EVT:
			dbg_link("TRF-");
			/* fall through */
		case ACTIVATE_MSG:
			dbg_link("ACT\n");
			break;
		case TIMEOUT_EVT:
			dbg_link("TIM ");
			if (l_ptr->next_in_no != l_ptr->checkpoint) {
				l_ptr->checkpoint = l_ptr->next_in_no;
				if (bclink_acks_missing(l_ptr->owner)) {
					link_send_proto_msg(l_ptr, STATE_MSG, 
							    0, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				} else if (l_ptr->max_pkt < l_ptr->max_pkt_target) {
					link_send_proto_msg(l_ptr, STATE_MSG, 
							    1, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				}
				link_set_timer(l_ptr, cont_intv);
				break;
			}
			dbg_link(" -> WU\n");
			l_ptr->state = WORKING_UNKNOWN;
			l_ptr->fsm_msg_cnt = 0;
			link_send_proto_msg(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv / 4);
			break;
		case RESET_MSG:
			dbg_link("RES -> RR\n");
			link_reset(l_ptr);
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			link_send_proto_msg(l_ptr, ACTIVATE_MSG, 0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		default:
			err("Unknown link event %u in WW state\n", event);
		}
		break;
	case WORKING_UNKNOWN:
		dbg_link("WU/");
		switch (event) {
		case TRAFFIC_MSG_EVT:
			dbg_link("TRF-");
		case ACTIVATE_MSG:
			dbg_link("ACT -> WW\n");
			l_ptr->state = WORKING_WORKING;
			l_ptr->fsm_msg_cnt = 0;
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			dbg_link("RES -> RR\n");
			link_reset(l_ptr);
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			link_send_proto_msg(l_ptr, ACTIVATE_MSG, 0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case TIMEOUT_EVT:
			dbg_link("TIM ");
			if (l_ptr->next_in_no != l_ptr->checkpoint) {
				dbg_link("-> WW \n");
				l_ptr->state = WORKING_WORKING;
				l_ptr->fsm_msg_cnt = 0;
				l_ptr->checkpoint = l_ptr->next_in_no;
				if (bclink_acks_missing(l_ptr->owner)) {
					link_send_proto_msg(l_ptr, STATE_MSG,
							    0, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				}
				link_set_timer(l_ptr, cont_intv);
			} else if (l_ptr->fsm_msg_cnt < l_ptr->abort_limit) {
				dbg_link("Probing %u/%u,timer = %u ms)\n",
					 l_ptr->fsm_msg_cnt, l_ptr->abort_limit,
					 cont_intv / 4);
				link_send_proto_msg(l_ptr, STATE_MSG, 
						    1, 0, 0, 0, 0);
				l_ptr->fsm_msg_cnt++;
				link_set_timer(l_ptr, cont_intv / 4);
			} else {	/* Link has failed */
				dbg_link("-> RU (%u probes unanswered)\n",
					 l_ptr->fsm_msg_cnt);
				link_reset(l_ptr);
				l_ptr->state = RESET_UNKNOWN;
				l_ptr->fsm_msg_cnt = 0;
				link_send_proto_msg(l_ptr, RESET_MSG,
						    0, 0, 0, 0, 0);
				l_ptr->fsm_msg_cnt++;
				link_set_timer(l_ptr, cont_intv);
			}
			break;
		default:
			err("Unknown link event %u in WU state\n", event);
		}
		break;
	case RESET_UNKNOWN:
		dbg_link("RU/");
		switch (event) {
		case TRAFFIC_MSG_EVT:
			dbg_link("TRF-\n");
			break;
		case ACTIVATE_MSG:
			other = l_ptr->owner->active_links[0];
			if (other && link_working_unknown(other)) {
				dbg_link("ACT\n");
				break;
			}
			dbg_link("ACT -> WW\n");
			l_ptr->state = WORKING_WORKING;
			l_ptr->fsm_msg_cnt = 0;
			link_activate(l_ptr);
			link_send_proto_msg(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			dbg_link("RES \n");
			dbg_link(" -> RR\n");
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			link_send_proto_msg(l_ptr, ACTIVATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case STARTING_EVT:
			dbg_link("START-");
			l_ptr->started = 1;
			/* fall through */
		case TIMEOUT_EVT:
			dbg_link("TIM \n");
			link_send_proto_msg(l_ptr, RESET_MSG, 0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		default:
			err("Unknown link event %u in RU state\n", event);
		}
		break;
	case RESET_RESET:
		dbg_link("RR/ ");
		switch (event) {
		case TRAFFIC_MSG_EVT:
			dbg_link("TRF-");
			/* fall through */
		case ACTIVATE_MSG:
			other = l_ptr->owner->active_links[0];
			if (other && link_working_unknown(other)) {
				dbg_link("ACT\n");
				break;
			}
			dbg_link("ACT -> WW\n");
			l_ptr->state = WORKING_WORKING;
			l_ptr->fsm_msg_cnt = 0;
			link_activate(l_ptr);
			link_send_proto_msg(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			dbg_link("RES\n");
			break;
		case TIMEOUT_EVT:
			dbg_link("TIM\n");
			link_send_proto_msg(l_ptr, ACTIVATE_MSG, 0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			dbg_link("fsm_msg_cnt %u\n", l_ptr->fsm_msg_cnt);
			break;
		default:
			err("Unknown link event %u in RR state\n", event);
		}
		break;
	default:
		err("Unknown link state %u/%u\n", l_ptr->state, event);
	}
}

/*
 * link_bundle_buf(): Append contents of a buffer to
 * the tail of an existing one. 
 */

static int link_bundle_buf(struct link *l_ptr,
			   struct sk_buff *bundler, 
			   struct sk_buff *buf)
{
	struct tipc_msg *bundler_msg = buf_msg(bundler);
	struct tipc_msg *msg = buf_msg(buf);
	u32 size = msg_size(msg);
	u32 to_pos = align(msg_size(bundler_msg));
	u32 rest = link_max_pkt(l_ptr) - to_pos;

	if (msg_user(bundler_msg) != MSG_BUNDLER)
		return 0;
	if (msg_type(bundler_msg) != OPEN_MSG)
		return 0;
	if (rest < align(size))
		return 0;

	skb_put(bundler, (to_pos - msg_size(bundler_msg)) + size);
	memcpy(bundler->data + to_pos, buf->data, size);
	msg_set_size(bundler_msg, to_pos + size);
	msg_set_msgcnt(bundler_msg, msg_msgcnt(bundler_msg) + 1);
	dbg("Packed msg # %u(%u octets) into pos %u in buf(#%u)\n",
	    msg_msgcnt(bundler_msg), size, to_pos, msg_seqno(bundler_msg));
	msg_dbg(msg, "PACKD:");
	buf_discard(buf);
	l_ptr->stats.sent_bundled++;
	return 1;
}

static inline void link_add_to_outqueue(struct link *l_ptr, 
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
}

/* 
 * link_send_buf() is the 'full path' for messages, called from 
 * inside TIPC when the 'fast path' in tipc_send_buf
 * has failed, and from link_send()
 */

int link_send_buf(struct link *l_ptr, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	u32 size = msg_size(msg);
	u32 dsz = msg_data_sz(msg);
	u32 queue_size = l_ptr->out_queue_size;
	u32 imp = msg_tot_importance(msg);
	u32 queue_limit = l_ptr->queue_limit[imp];
	u32 max_packet = link_max_pkt(l_ptr);

	msg_set_prevnode(msg, tipc_own_addr);	/* If routed message */

	/* Match msg importance against queue limits: */

	if (unlikely(queue_size >= queue_limit)) {
		if (imp <= TIPC_CRITICAL_IMPORTANCE) {
			return link_schedule_port(l_ptr, msg_origport(msg),
						  size);
		}
		msg_dbg(msg, "TIPC: Congestion, throwing away\n");
		buf_discard(buf);
		if (imp > CONN_MANAGER) {
			warn("Resetting <%s>, send queue full", l_ptr->name);
			link_reset(l_ptr);
		}
		return dsz;
	}

	/* Fragmentation needed ? */

	if (size > max_packet)
		return link_send_long_buf(l_ptr, buf);

	/* Packet can be queued or sent: */

	if (queue_size > l_ptr->stats.max_queue_sz)
		l_ptr->stats.max_queue_sz = queue_size;

	if (likely(!bearer_congested(l_ptr->b_ptr, l_ptr) && 
		   !link_congested(l_ptr))) {
		link_add_to_outqueue(l_ptr, buf, msg);

		if (likely(bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr))) {
			l_ptr->unacked_window = 0;
		} else {
			bearer_schedule(l_ptr->b_ptr, l_ptr);
			l_ptr->stats.bearer_congs++;
			l_ptr->next_out = buf;
		}
		return dsz;
	}
	/* Congestion: can message be bundled ?: */

	if ((msg_user(msg) != CHANGEOVER_PROTOCOL) &&
	    (msg_user(msg) != MSG_FRAGMENTER)) {

		/* Try adding message to an existing bundle */

		if (l_ptr->next_out && 
		    link_bundle_buf(l_ptr, l_ptr->last_out, buf)) {
			bearer_resolve_congestion(l_ptr->b_ptr, l_ptr);
			return dsz;
		}

		/* Try creating a new bundle */

		if (size <= max_packet * 2 / 3) {
			struct sk_buff *bundler = buf_acquire(max_packet);
			struct tipc_msg bundler_hdr;

			if (bundler) {
				msg_init(&bundler_hdr, MSG_BUNDLER, OPEN_MSG,
					 TIPC_OK, INT_H_SIZE, l_ptr->addr);
				memcpy(bundler->data, (unchar *)&bundler_hdr, 
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
	bearer_resolve_congestion(l_ptr->b_ptr, l_ptr);
	return dsz;
}

/* 
 * link_send(): same as link_send_buf(), but the link to use has 
 * not been selected yet, and the the owner node is not locked
 * Called by TIPC internal users, e.g. the name distributor
 */

int link_send(struct sk_buff *buf, u32 dest, u32 selector)
{
	struct link *l_ptr;
	struct node *n_ptr;
	int res = -ELINKCONG;

	read_lock_bh(&net_lock);
	n_ptr = node_select(dest, selector);
	if (n_ptr) {
		node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector & 1];
		dbg("link_send: found link %x for dest %x\n", l_ptr, dest);
		if (l_ptr) {
			res = link_send_buf(l_ptr, buf);
		}
		node_unlock(n_ptr);
	} else {
		dbg("Attempt to send msg to unknown node:\n");
		msg_dbg(buf_msg(buf),">>>");
		buf_discard(buf);
	}
	read_unlock_bh(&net_lock);
	return res;
}

/* 
 * link_send_buf_fast: Entry for data messages where the 
 * destination link is known and the header is complete,
 * inclusive total message length. Very time critical.
 * Link is locked. Returns user data length.
 */

static inline int link_send_buf_fast(struct link *l_ptr, struct sk_buff *buf,
				     u32 *used_max_pkt)
{
	struct tipc_msg *msg = buf_msg(buf);
	int res = msg_data_sz(msg);

	if (likely(!link_congested(l_ptr))) {
		if (likely(msg_size(msg) <= link_max_pkt(l_ptr))) {
			if (likely(list_empty(&l_ptr->b_ptr->cong_links))) {
				link_add_to_outqueue(l_ptr, buf, msg);
				if (likely(bearer_send(l_ptr->b_ptr, buf,
						       &l_ptr->media_addr))) {
					l_ptr->unacked_window = 0;
					msg_dbg(msg,"SENT_FAST:");
					return res;
				}
				dbg("failed sent fast...\n");
				bearer_schedule(l_ptr->b_ptr, l_ptr);
				l_ptr->stats.bearer_congs++;
				l_ptr->next_out = buf;
				return res;
			}
		}
		else
			*used_max_pkt = link_max_pkt(l_ptr);
	}
	return link_send_buf(l_ptr, buf);  /* All other cases */
}

/* 
 * tipc_send_buf_fast: Entry for data messages where the 
 * destination node is known and the header is complete,
 * inclusive total message length.
 * Returns user data length.
 */
int tipc_send_buf_fast(struct sk_buff *buf, u32 destnode)
{
	struct link *l_ptr;
	struct node *n_ptr;
	int res;
	u32 selector = msg_origport(buf_msg(buf)) & 1;
	u32 dummy;

	if (destnode == tipc_own_addr)
		return port_recv_msg(buf);

	read_lock_bh(&net_lock);
	n_ptr = node_select(destnode, selector);
	if (likely(n_ptr)) {
		node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector];
		dbg("send_fast: buf %x selected %x, destnode = %x\n",
		    buf, l_ptr, destnode);
		if (likely(l_ptr)) {
			res = link_send_buf_fast(l_ptr, buf, &dummy);
			node_unlock(n_ptr);
			read_unlock_bh(&net_lock);
			return res;
		}
		node_unlock(n_ptr);
	}
	read_unlock_bh(&net_lock);
	res = msg_data_sz(buf_msg(buf));
	tipc_reject_msg(buf, TIPC_ERR_NO_NODE);
	return res;
}


/* 
 * link_send_sections_fast: Entry for messages where the 
 * destination processor is known and the header is complete,
 * except for total message length. 
 * Returns user data length or errno.
 */
int link_send_sections_fast(struct port *sender, 
			    struct iovec const *msg_sect,
			    const u32 num_sect, 
			    u32 destaddr)
{
	struct tipc_msg *hdr = &sender->publ.phdr;
	struct link *l_ptr;
	struct sk_buff *buf;
	struct node *node;
	int res;
	u32 selector = msg_origport(hdr) & 1;

	assert(destaddr != tipc_own_addr);

again:
	/*
	 * Try building message using port's max_pkt hint.
	 * (Must not hold any locks while building message.)
	 */

	res = msg_build(hdr, msg_sect, num_sect, sender->max_pkt,
			!sender->user_port, &buf);

	read_lock_bh(&net_lock);
	node = node_select(destaddr, selector);
	if (likely(node)) {
		node_lock(node);
		l_ptr = node->active_links[selector];
		if (likely(l_ptr)) {
			if (likely(buf)) {
				res = link_send_buf_fast(l_ptr, buf,
							 &sender->max_pkt);
				if (unlikely(res < 0))
					buf_discard(buf);
exit:
				node_unlock(node);
				read_unlock_bh(&net_lock);
				return res;
			}

			/* Exit if build request was invalid */

			if (unlikely(res < 0))
				goto exit;

			/* Exit if link (or bearer) is congested */

			if (link_congested(l_ptr) || 
			    !list_empty(&l_ptr->b_ptr->cong_links)) {
				res = link_schedule_port(l_ptr,
							 sender->publ.ref, res);
				goto exit;
			}

			/* 
			 * Message size exceeds max_pkt hint; update hint,
			 * then re-try fast path or fragment the message
			 */

			sender->max_pkt = link_max_pkt(l_ptr);
			node_unlock(node);
			read_unlock_bh(&net_lock);


			if ((msg_hdr_sz(hdr) + res) <= sender->max_pkt)
				goto again;

			return link_send_sections_long(sender, msg_sect,
						       num_sect, destaddr);
		}
		node_unlock(node);
	}
	read_unlock_bh(&net_lock);

	/* Couldn't find a link to the destination node */

	if (buf)
		return tipc_reject_msg(buf, TIPC_ERR_NO_NODE);
	if (res >= 0)
		return port_reject_sections(sender, hdr, msg_sect, num_sect,
					    TIPC_ERR_NO_NODE);
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
static int link_send_sections_long(struct port *sender,
				   struct iovec const *msg_sect,
				   u32 num_sect,
				   u32 destaddr)
{
	struct link *l_ptr;
	struct node *node;
	struct tipc_msg *hdr = &sender->publ.phdr;
	u32 dsz = msg_data_sz(hdr);
	u32 max_pkt,fragm_sz,rest;
	struct tipc_msg fragm_hdr;
	struct sk_buff *buf,*buf_chain,*prev;
	u32 fragm_crs,fragm_rest,hsz,sect_rest;
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
	sect_crs = 0;
	curr_sect = -1;

	/* Prepare reusable fragment header: */

	msg_dbg(hdr, ">FRAGMENTING>");
	msg_init(&fragm_hdr, MSG_FRAGMENTER, FIRST_FRAGMENT,
		 TIPC_OK, INT_H_SIZE, msg_destnode(hdr));
	msg_set_link_selector(&fragm_hdr, sender->publ.ref);
	msg_set_size(&fragm_hdr, max_pkt);
	msg_set_fragm_no(&fragm_hdr, 1);

	/* Prepare header of first fragment: */

	buf_chain = buf = buf_acquire(max_pkt);
	if (!buf)
		return -ENOMEM;
	buf->next = NULL;
	memcpy(buf->data, (unchar *)&fragm_hdr, INT_H_SIZE);
	hsz = msg_hdr_sz(hdr);
	memcpy(buf->data + INT_H_SIZE, (unchar *)hdr, hsz);
	msg_dbg(buf_msg(buf), ">BUILD>");

	/* Chop up message: */

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
					buf_discard(buf_chain);
				}
				return -EFAULT;
			}
		} else
			memcpy(buf->data + fragm_crs, sect_crs, sz);

		sect_crs += sz;
		sect_rest -= sz;
		fragm_crs += sz;
		fragm_rest -= sz;
		rest -= sz;

		if (!fragm_rest && rest) {

			/* Initiate new fragment: */
			if (rest <= fragm_sz) {
				fragm_sz = rest;
				msg_set_type(&fragm_hdr,LAST_FRAGMENT);
			} else {
				msg_set_type(&fragm_hdr, FRAGMENT);
			}
			msg_set_size(&fragm_hdr, fragm_sz + INT_H_SIZE);
			msg_set_fragm_no(&fragm_hdr, ++fragm_no);
			prev = buf;
			buf = buf_acquire(fragm_sz + INT_H_SIZE);
			if (!buf)
				goto error;

			buf->next = NULL;                                
			prev->next = buf;
			memcpy(buf->data, (unchar *)&fragm_hdr, INT_H_SIZE);
			fragm_crs = INT_H_SIZE;
			fragm_rest = fragm_sz;
			msg_dbg(buf_msg(buf),"  >BUILD>");
		}
	}
	while (rest > 0);

	/* 
	 * Now we have a buffer chain. Select a link and check
	 * that packet size is still OK
	 */
	node = node_select(destaddr, sender->publ.ref & 1);
	if (likely(node)) {
		node_lock(node);
		l_ptr = node->active_links[sender->publ.ref & 1];
		if (!l_ptr) {
			node_unlock(node);
			goto reject;
		}
		if (link_max_pkt(l_ptr) < max_pkt) {
			sender->max_pkt = link_max_pkt(l_ptr);
			node_unlock(node);
			for (; buf_chain; buf_chain = buf) {
				buf = buf_chain->next;
				buf_discard(buf_chain);
			}
			goto again;
		}
	} else {
reject:
		for (; buf_chain; buf_chain = buf) {
			buf = buf_chain->next;
			buf_discard(buf_chain);
		}
		return port_reject_sections(sender, hdr, msg_sect, num_sect,
					    TIPC_ERR_NO_NODE);
	}

	/* Append whole chain to send queue: */

	buf = buf_chain;
	l_ptr->long_msg_seq_no = mod(l_ptr->long_msg_seq_no + 1);
	if (!l_ptr->next_out)
		l_ptr->next_out = buf_chain;
	l_ptr->stats.sent_fragmented++;
	while (buf) {
		struct sk_buff *next = buf->next;
		struct tipc_msg *msg = buf_msg(buf);

		l_ptr->stats.sent_fragments++;
		msg_set_long_msgno(msg, l_ptr->long_msg_seq_no);
		link_add_to_outqueue(l_ptr, buf, msg);
		msg_dbg(msg, ">ADD>");
		buf = next;
	}

	/* Send it, if possible: */

	link_push_queue(l_ptr);
	node_unlock(node);
	return dsz;
}

/* 
 * link_push_packet: Push one unsent packet to the media
 */
u32 link_push_packet(struct link *l_ptr)
{
	struct sk_buff *buf = l_ptr->first_out;
	u32 r_q_size = l_ptr->retransm_queue_size;
	u32 r_q_head = l_ptr->retransm_queue_head;

	/* Step to position where retransmission failed, if any,    */
	/* consider that buffers may have been released in meantime */

	if (r_q_size && buf) {
		u32 last = lesser(mod(r_q_head + r_q_size), 
				  link_last_sent(l_ptr));
		u32 first = msg_seqno(buf_msg(buf));

		while (buf && less(first, r_q_head)) {
			first = mod(first + 1);
			buf = buf->next;
		}
		l_ptr->retransm_queue_head = r_q_head = first;
		l_ptr->retransm_queue_size = r_q_size = mod(last - first);
	}

	/* Continue retransmission now, if there is anything: */

	if (r_q_size && buf && !skb_cloned(buf)) {
		msg_set_ack(buf_msg(buf), mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(buf_msg(buf), l_ptr->owner->bclink.last_in); 
		if (bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr)) {
			msg_dbg(buf_msg(buf), ">DEF-RETR>");
			l_ptr->retransm_queue_head = mod(++r_q_head);
			l_ptr->retransm_queue_size = --r_q_size;
			l_ptr->stats.retransmitted++;
			return TIPC_OK;
		} else {
			l_ptr->stats.bearer_congs++;
			msg_dbg(buf_msg(buf), "|>DEF-RETR>");
			return PUSH_FAILED;
		}
	}

	/* Send deferred protocol message, if any: */

	buf = l_ptr->proto_msg_queue;
	if (buf) {
		msg_set_ack(buf_msg(buf), mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(buf_msg(buf),l_ptr->owner->bclink.last_in); 
		if (bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr)) {
			msg_dbg(buf_msg(buf), ">DEF-PROT>");
			l_ptr->unacked_window = 0;
			buf_discard(buf);
			l_ptr->proto_msg_queue = 0;
			return TIPC_OK;
		} else {
			msg_dbg(buf_msg(buf), "|>DEF-PROT>");
			l_ptr->stats.bearer_congs++;
			return PUSH_FAILED;
		}
	}

	/* Send one deferred data message, if send window not full: */

	buf = l_ptr->next_out;
	if (buf) {
		struct tipc_msg *msg = buf_msg(buf);
		u32 next = msg_seqno(msg);
		u32 first = msg_seqno(buf_msg(l_ptr->first_out));

		if (mod(next - first) < l_ptr->queue_limit[0]) {
			msg_set_ack(msg, mod(l_ptr->next_in_no - 1));
			msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in); 
			if (bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr)) {
				if (msg_user(msg) == MSG_BUNDLER)
					msg_set_type(msg, CLOSED_MSG);
				msg_dbg(msg, ">PUSH-DATA>");
				l_ptr->next_out = buf->next;
				return TIPC_OK;
			} else {
				msg_dbg(msg, "|PUSH-DATA|");
				l_ptr->stats.bearer_congs++;
				return PUSH_FAILED;
			}
		}
	}
	return PUSH_FINISHED;
}

/*
 * push_queue(): push out the unsent messages of a link where
 *               congestion has abated. Node is locked
 */
void link_push_queue(struct link *l_ptr)
{
	u32 res;

	if (bearer_congested(l_ptr->b_ptr, l_ptr))
		return;

	do {
		res = link_push_packet(l_ptr);
	}
	while (res == TIPC_OK);
	if (res == PUSH_FAILED)
		bearer_schedule(l_ptr->b_ptr, l_ptr);
}

void link_retransmit(struct link *l_ptr, struct sk_buff *buf, 
		     u32 retransmits)
{
	struct tipc_msg *msg;

	dbg("Retransmitting %u in link %x\n", retransmits, l_ptr);

	if (bearer_congested(l_ptr->b_ptr, l_ptr) && buf && !skb_cloned(buf)) {
		msg_dbg(buf_msg(buf), ">NO_RETR->BCONG>");
		dbg_print_link(l_ptr, "   ");
		l_ptr->retransm_queue_head = msg_seqno(buf_msg(buf));
		l_ptr->retransm_queue_size = retransmits;
		return;
	}
	while (retransmits && (buf != l_ptr->next_out) && buf && !skb_cloned(buf)) {
		msg = buf_msg(buf);
		msg_set_ack(msg, mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in); 
		if (bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr)) {
                        /* Catch if retransmissions fail repeatedly: */
                        if (l_ptr->last_retransmitted == msg_seqno(msg)) {
                                if (++l_ptr->stale_count > 100) {
                                        msg_print(CONS, buf_msg(buf), ">RETR>");
                                        info("...Retransmitted %u times\n",
					     l_ptr->stale_count);
                                        link_print(l_ptr, CONS, "Resetting Link\n");;
                                        link_reset(l_ptr);
                                        break;
                                }
                        } else {
                                l_ptr->stale_count = 0;
                        }
                        l_ptr->last_retransmitted = msg_seqno(msg);

			msg_dbg(buf_msg(buf), ">RETR>");
			buf = buf->next;
			retransmits--;
			l_ptr->stats.retransmitted++;
		} else {
			bearer_schedule(l_ptr->b_ptr, l_ptr);
			l_ptr->stats.bearer_congs++;
			l_ptr->retransm_queue_head = msg_seqno(buf_msg(buf));
			l_ptr->retransm_queue_size = retransmits;
			return;
		}
	}
	l_ptr->retransm_queue_head = l_ptr->retransm_queue_size = 0;
}

/* 
 * link_recv_non_seq: Receive packets which are outside
 *                    the link sequence flow
 */

static void link_recv_non_seq(struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);

	if (msg_user(msg) ==  LINK_CONFIG)
		disc_recv_msg(buf);
	else
		bclink_recv_pkt(buf);
}

/** 
 * link_insert_deferred_queue - insert deferred messages back into receive chain
 */

static struct sk_buff *link_insert_deferred_queue(struct link *l_ptr, 
						  struct sk_buff *buf)
{
	u32 seq_no;

	if (l_ptr->oldest_deferred_in == NULL)
		return buf;

	seq_no = msg_seqno(buf_msg(l_ptr->oldest_deferred_in));
	if (seq_no == mod(l_ptr->next_in_no)) {
		l_ptr->newest_deferred_in->next = buf;
		buf = l_ptr->oldest_deferred_in;
		l_ptr->oldest_deferred_in = NULL;
		l_ptr->deferred_inqueue_sz = 0;
	}
	return buf;
}

void tipc_recv_msg(struct sk_buff *head, struct tipc_bearer *tb_ptr)
{
	read_lock_bh(&net_lock);
	while (head) {
		struct bearer *b_ptr;
		struct node *n_ptr;
		struct link *l_ptr;
		struct sk_buff *crs;
		struct sk_buff *buf = head;
		struct tipc_msg *msg = buf_msg(buf);
		u32 seq_no = msg_seqno(msg);
		u32 ackd = msg_ack(msg);
		u32 released = 0;
		int type;

		b_ptr = (struct bearer *)tb_ptr;
		TIPC_SKB_CB(buf)->handle = b_ptr;

		head = head->next;
		if (unlikely(msg_version(msg) != TIPC_VERSION))
			goto cont;
#if 0
		if (msg_user(msg) != LINK_PROTOCOL)
#endif
			msg_dbg(msg,"<REC<");

		if (unlikely(msg_non_seq(msg))) {
			link_recv_non_seq(buf);
			continue;
		}
		n_ptr = node_find(msg_prevnode(msg));
		if (unlikely(!n_ptr))
			goto cont;

		node_lock(n_ptr);
		l_ptr = n_ptr->links[b_ptr->identity];
		if (unlikely(!l_ptr)) {
			node_unlock(n_ptr);
			goto cont;
		}
		/* 
		 * Release acked messages 
		 */
		if (less(n_ptr->bclink.acked, msg_bcast_ack(msg))) {
			if (node_is_up(n_ptr) && n_ptr->bclink.supported)
				bclink_acknowledge(n_ptr, msg_bcast_ack(msg));
		}

		crs = l_ptr->first_out;
		while ((crs != l_ptr->next_out) && 
		       less_eq(msg_seqno(buf_msg(crs)), ackd)) {
			struct sk_buff *next = crs->next;

			buf_discard(crs);
			crs = next;
			released++;
		}
		if (released) {
			l_ptr->first_out = crs;
			l_ptr->out_queue_size -= released;
		}
		if (unlikely(l_ptr->next_out))
			link_push_queue(l_ptr);
		if (unlikely(!list_empty(&l_ptr->waiting_ports)))
			link_wakeup_ports(l_ptr, 0);
		if (unlikely(++l_ptr->unacked_window >= TIPC_MIN_LINK_WIN)) {
			l_ptr->stats.sent_acks++;
			link_send_proto_msg(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
		}

protocol_check:
		if (likely(link_working_working(l_ptr))) {
			if (likely(seq_no == mod(l_ptr->next_in_no))) {
				l_ptr->next_in_no++;
				if (unlikely(l_ptr->oldest_deferred_in))
					head = link_insert_deferred_queue(l_ptr,
									  head);
				if (likely(msg_is_dest(msg, tipc_own_addr))) {
deliver:
					if (likely(msg_isdata(msg))) {
						node_unlock(n_ptr);
						port_recv_msg(buf);
						continue;
					}
					switch (msg_user(msg)) {
					case MSG_BUNDLER:
						l_ptr->stats.recv_bundles++;
						l_ptr->stats.recv_bundled += 
							msg_msgcnt(msg);
						node_unlock(n_ptr);
						link_recv_bundle(buf);
						continue;
					case ROUTE_DISTRIBUTOR:
						node_unlock(n_ptr);
						cluster_recv_routing_table(buf);
						continue;
					case NAME_DISTRIBUTOR:
						node_unlock(n_ptr);
						named_recv(buf);
						continue;
					case CONN_MANAGER:
						node_unlock(n_ptr);
						port_recv_proto_msg(buf);
						continue;
					case MSG_FRAGMENTER:
						l_ptr->stats.recv_fragments++;
						if (link_recv_fragment(
							&l_ptr->defragm_buf, 
							&buf, &msg)) {
							l_ptr->stats.recv_fragmented++;
							goto deliver;
						}
						break;
					case CHANGEOVER_PROTOCOL:
						type = msg_type(msg);
						if (link_recv_changeover_msg(
							&l_ptr, &buf)) {
							msg = buf_msg(buf);
							seq_no = msg_seqno(msg);
							TIPC_SKB_CB(buf)->handle 
								= b_ptr;
							if (type == ORIGINAL_MSG)
								goto deliver;
							goto protocol_check;
						}
						break;
					}
				}
				node_unlock(n_ptr);
				net_route_msg(buf);
				continue;
			}
			link_handle_out_of_seq_msg(l_ptr, buf);
			head = link_insert_deferred_queue(l_ptr, head);
			node_unlock(n_ptr);
			continue;
		}

		if (msg_user(msg) == LINK_PROTOCOL) {
			link_recv_proto_msg(l_ptr, buf);
			head = link_insert_deferred_queue(l_ptr, head);
			node_unlock(n_ptr);
			continue;
		}
		msg_dbg(msg,"NSEQ<REC<");
		link_state_event(l_ptr, TRAFFIC_MSG_EVT);

		if (link_working_working(l_ptr)) {
			/* Re-insert in front of queue */
			msg_dbg(msg,"RECV-REINS:");
			buf->next = head;
			head = buf;
			node_unlock(n_ptr);
			continue;
		}
		node_unlock(n_ptr);
cont:
		buf_discard(buf);
	}
	read_unlock_bh(&net_lock);
}

/* 
 * link_defer_buf(): Sort a received out-of-sequence packet 
 *                   into the deferred reception queue.
 * Returns the increase of the queue length,i.e. 0 or 1
 */

u32 link_defer_pkt(struct sk_buff **head,
		   struct sk_buff **tail,
		   struct sk_buff *buf)
{
	struct sk_buff *prev = 0;
	struct sk_buff *crs = *head;
	u32 seq_no = msg_seqno(buf_msg(buf));

	buf->next = NULL;

	/* Empty queue ? */
	if (*head == NULL) {
		*head = *tail = buf;
		return 1;
	}

	/* Last ? */
	if (less(msg_seqno(buf_msg(*tail)), seq_no)) {
		(*tail)->next = buf;
		*tail = buf;
		return 1;
	}

	/* Scan through queue and sort it in */
	do {
		struct tipc_msg *msg = buf_msg(crs);

		if (less(seq_no, msg_seqno(msg))) {
			buf->next = crs;
			if (prev)
				prev->next = buf;
			else
				*head = buf;   
			return 1;
		}
		if (seq_no == msg_seqno(msg)) {
			break;
		}
		prev = crs;
		crs = crs->next;
	}
	while (crs);

	/* Message is a duplicate of an existing message */

	buf_discard(buf);
	return 0;
}

/** 
 * link_handle_out_of_seq_msg - handle arrival of out-of-sequence packet
 */

static void link_handle_out_of_seq_msg(struct link *l_ptr, 
				       struct sk_buff *buf)
{
	u32 seq_no = msg_seqno(buf_msg(buf));

	if (likely(msg_user(buf_msg(buf)) == LINK_PROTOCOL)) {
		link_recv_proto_msg(l_ptr, buf);
		return;
	}

	dbg("rx OOS msg: seq_no %u, expecting %u (%u)\n", 
	    seq_no, mod(l_ptr->next_in_no), l_ptr->next_in_no);

	/* Record OOS packet arrival (force mismatch on next timeout) */

	l_ptr->checkpoint--;

	/* 
	 * Discard packet if a duplicate; otherwise add it to deferred queue
	 * and notify peer of gap as per protocol specification
	 */

	if (less(seq_no, mod(l_ptr->next_in_no))) {
		l_ptr->stats.duplicates++;
		buf_discard(buf);
		return;
	}

	if (link_defer_pkt(&l_ptr->oldest_deferred_in,
			   &l_ptr->newest_deferred_in, buf)) {
		l_ptr->deferred_inqueue_sz++;
		l_ptr->stats.deferred_recv++;
		if ((l_ptr->deferred_inqueue_sz % 16) == 1)
			link_send_proto_msg(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
	} else
		l_ptr->stats.duplicates++;
}

/*
 * Send protocol message to the other endpoint.
 */
void link_send_proto_msg(struct link *l_ptr, u32 msg_typ, int probe_msg,
			 u32 gap, u32 tolerance, u32 priority, u32 ack_mtu)
{
	struct sk_buff *buf = 0;
	struct tipc_msg *msg = l_ptr->pmsg;
        u32 msg_size = sizeof(l_ptr->proto_msg);

	if (link_blocked(l_ptr))
		return;
	msg_set_type(msg, msg_typ);
	msg_set_net_plane(msg, l_ptr->b_ptr->net_plane);
	msg_set_bcast_ack(msg, mod(l_ptr->owner->bclink.last_in)); 
	msg_set_last_bcast(msg, bclink_get_last_sent());

	if (msg_typ == STATE_MSG) {
		u32 next_sent = mod(l_ptr->next_out_no);

		if (!link_is_up(l_ptr))
			return;
		if (l_ptr->next_out)
			next_sent = msg_seqno(buf_msg(l_ptr->next_out));
		msg_set_next_sent(msg, next_sent);
		if (l_ptr->oldest_deferred_in) {
			u32 rec = msg_seqno(buf_msg(l_ptr->oldest_deferred_in));
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
		msg_set_link_tolerance(msg, l_ptr->tolerance);
		msg_set_linkprio(msg, l_ptr->priority);
		msg_set_max_pkt(msg, l_ptr->max_pkt_target);
	}

	if (node_has_redundant_links(l_ptr->owner)) {
		msg_set_redundant_link(msg);
	} else {
		msg_clear_redundant_link(msg);
	}
	msg_set_linkprio(msg, l_ptr->priority);

	/* Ensure sequence number will not fit : */

	msg_set_seqno(msg, mod(l_ptr->next_out_no + (0xffff/2)));

	/* Congestion? */

	if (bearer_congested(l_ptr->b_ptr, l_ptr)) {
		if (!l_ptr->proto_msg_queue) {
			l_ptr->proto_msg_queue =
				buf_acquire(sizeof(l_ptr->proto_msg));
		}
		buf = l_ptr->proto_msg_queue;
		if (!buf)
			return;
		memcpy(buf->data, (unchar *)msg, sizeof(l_ptr->proto_msg));
		return;
	}
	msg_set_timestamp(msg, jiffies_to_msecs(jiffies));

	/* Message can be sent */

	msg_dbg(msg, ">>");

	buf = buf_acquire(msg_size);
	if (!buf)
		return;

	memcpy(buf->data, (unchar *)msg, sizeof(l_ptr->proto_msg));
        msg_set_size(buf_msg(buf), msg_size);

	if (bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr)) {
		l_ptr->unacked_window = 0;
		buf_discard(buf);
		return;
	}

	/* New congestion */
	bearer_schedule(l_ptr->b_ptr, l_ptr);
	l_ptr->proto_msg_queue = buf;
	l_ptr->stats.bearer_congs++;
}

/*
 * Receive protocol message :
 * Note that network plane id propagates through the network, and may 
 * change at any time. The node with lowest address rules    
 */

static void link_recv_proto_msg(struct link *l_ptr, struct sk_buff *buf)
{
	u32 rec_gap = 0;
	u32 max_pkt_info;
        u32 max_pkt_ack;
	u32 msg_tol;
	struct tipc_msg *msg = buf_msg(buf);

	dbg("AT(%u):", jiffies_to_msecs(jiffies));
	msg_dbg(msg, "<<");
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
		if (!link_working_unknown(l_ptr) && l_ptr->peer_session) {
			if (msg_session(msg) == l_ptr->peer_session) {
				dbg("Duplicate RESET: %u<->%u\n",
				    msg_session(msg), l_ptr->peer_session);                                     
				break; /* duplicate: ignore */
			}
		}
		/* fall thru' */
	case ACTIVATE_MSG:
		/* Update link settings according other endpoint's values */

		strcpy((strrchr(l_ptr->name, ':') + 1), (char *)msg_data(msg));

		if ((msg_tol = msg_link_tolerance(msg)) &&
		    (msg_tol > l_ptr->tolerance))
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
		l_ptr->owner->bclink.supported = (max_pkt_info != 0);

		link_state_event(l_ptr, msg_type(msg));

		l_ptr->peer_session = msg_session(msg);
		l_ptr->peer_bearer_id = msg_bearer_id(msg);

		/* Synchronize broadcast sequence numbers */
		if (!node_has_redundant_links(l_ptr->owner)) {
			l_ptr->owner->bclink.last_in = mod(msg_last_bcast(msg));
		}
		break;
	case STATE_MSG:

		if ((msg_tol = msg_link_tolerance(msg)))
			link_set_supervision_props(l_ptr, msg_tol);
		
		if (msg_linkprio(msg) && 
		    (msg_linkprio(msg) != l_ptr->priority)) {
			warn("Changing prio <%s>: %u->%u\n",
			     l_ptr->name, l_ptr->priority, msg_linkprio(msg));
			l_ptr->priority = msg_linkprio(msg);
			link_reset(l_ptr); /* Enforce change to take effect */
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
                        dbg("Link <%s> updated MTU %u -> %u\n",
                            l_ptr->name, l_ptr->max_pkt, max_pkt_ack);
                        l_ptr->max_pkt = max_pkt_ack;
                        l_ptr->max_pkt_probes = 0;
                }

		max_pkt_ack = 0;
                if (msg_probe(msg)) {
			l_ptr->stats.recv_probes++;
                        if (msg_size(msg) > sizeof(l_ptr->proto_msg)) {
                                max_pkt_ack = msg_size(msg);
                        }
                }

		/* Protocol message before retransmits, reduce loss risk */

		bclink_check_gap(l_ptr->owner, msg_last_bcast(msg));

		if (rec_gap || (msg_probe(msg))) {
			link_send_proto_msg(l_ptr, STATE_MSG,
					    0, rec_gap, 0, 0, max_pkt_ack);
		}
		if (msg_seq_gap(msg)) {
			msg_dbg(msg, "With Gap:");
			l_ptr->stats.recv_nacks++;
			link_retransmit(l_ptr, l_ptr->first_out,
					msg_seq_gap(msg));
		}
		break;
	default:
		msg_dbg(buf_msg(buf), "<DISCARDING UNKNOWN<");
	}
exit:
	buf_discard(buf);
}


/*
 * link_tunnel(): Send one message via a link belonging to 
 * another bearer. Owner node is locked.
 */
void link_tunnel(struct link *l_ptr, 
	    struct tipc_msg *tunnel_hdr, 
	    struct tipc_msg  *msg,
	    u32 selector)
{
	struct link *tunnel;
	struct sk_buff *buf;
	u32 length = msg_size(msg);

	tunnel = l_ptr->owner->active_links[selector & 1];
	if (!link_is_up(tunnel))
		return;
	msg_set_size(tunnel_hdr, length + INT_H_SIZE);
	buf = buf_acquire(length + INT_H_SIZE);
	if (!buf)
		return;
	memcpy(buf->data, (unchar *)tunnel_hdr, INT_H_SIZE);
	memcpy(buf->data + INT_H_SIZE, (unchar *)msg, length);
	dbg("%c->%c:", l_ptr->b_ptr->net_plane, tunnel->b_ptr->net_plane);
	msg_dbg(buf_msg(buf), ">SEND>");
	assert(tunnel);
	link_send_buf(tunnel, buf);
}



/*
 * changeover(): Send whole message queue via the remaining link
 *               Owner node is locked.
 */

void link_changeover(struct link *l_ptr)
{
	u32 msgcount = l_ptr->out_queue_size;
	struct sk_buff *crs = l_ptr->first_out;
	struct link *tunnel = l_ptr->owner->active_links[0];
	int split_bundles = node_has_redundant_links(l_ptr->owner);
	struct tipc_msg tunnel_hdr;

	if (!tunnel)
		return;

	if (!l_ptr->owner->permit_changeover)
		return;

	msg_init(&tunnel_hdr, CHANGEOVER_PROTOCOL,
		 ORIGINAL_MSG, TIPC_OK, INT_H_SIZE, l_ptr->addr);
	msg_set_bearer_id(&tunnel_hdr, l_ptr->peer_bearer_id);
	msg_set_msgcnt(&tunnel_hdr, msgcount);
	if (!l_ptr->first_out) {
		struct sk_buff *buf;

		assert(!msgcount);
		buf = buf_acquire(INT_H_SIZE);
		if (buf) {
			memcpy(buf->data, (unchar *)&tunnel_hdr, INT_H_SIZE);
			msg_set_size(&tunnel_hdr, INT_H_SIZE);
			dbg("%c->%c:", l_ptr->b_ptr->net_plane,
			    tunnel->b_ptr->net_plane);
			msg_dbg(&tunnel_hdr, "EMPTY>SEND>");
			link_send_buf(tunnel, buf);
		} else {
			warn("Memory squeeze; link changeover failed\n");
		}
		return;
	}
	while (crs) {
		struct tipc_msg *msg = buf_msg(crs);

		if ((msg_user(msg) == MSG_BUNDLER) && split_bundles) {
			u32 msgcount = msg_msgcnt(msg);
			struct tipc_msg *m = msg_get_wrapped(msg);
			unchar* pos = (unchar*)m;

			while (msgcount--) {
				msg_set_seqno(m,msg_seqno(msg));
				link_tunnel(l_ptr, &tunnel_hdr, m,
					    msg_link_selector(m));
				pos += align(msg_size(m));
				m = (struct tipc_msg *)pos;
			}
		} else {
			link_tunnel(l_ptr, &tunnel_hdr, msg,
				    msg_link_selector(msg));
		}
		crs = crs->next;
	}
}

void link_send_duplicate(struct link *l_ptr, struct link *tunnel)
{
	struct sk_buff *iter;
	struct tipc_msg tunnel_hdr;

	msg_init(&tunnel_hdr, CHANGEOVER_PROTOCOL,
		 DUPLICATE_MSG, TIPC_OK, INT_H_SIZE, l_ptr->addr);
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
		outbuf = buf_acquire(length + INT_H_SIZE);
		if (outbuf == NULL) {
			warn("Memory squeeze; buffer duplication failed\n");
			return;
		}
		memcpy(outbuf->data, (unchar *)&tunnel_hdr, INT_H_SIZE);
		memcpy(outbuf->data + INT_H_SIZE, iter->data, length);
		dbg("%c->%c:", l_ptr->b_ptr->net_plane,
		    tunnel->b_ptr->net_plane);
		msg_dbg(buf_msg(outbuf), ">SEND>");
		link_send_buf(tunnel, outbuf);
		if (!link_is_up(l_ptr))
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

	eb = buf_acquire(size);
	if (eb)
		memcpy(eb->data, (unchar *)msg, size);
	return eb;
}

/* 
 *  link_recv_changeover_msg(): Receive tunneled packet sent
 *  via other link. Node is locked. Return extracted buffer.
 */

static int link_recv_changeover_msg(struct link **l_ptr,
				    struct sk_buff **buf)
{
	struct sk_buff *tunnel_buf = *buf;
	struct link *dest_link;
	struct tipc_msg *msg;
	struct tipc_msg *tunnel_msg = buf_msg(tunnel_buf);
	u32 msg_typ = msg_type(tunnel_msg);
	u32 msg_count = msg_msgcnt(tunnel_msg);

	dest_link = (*l_ptr)->owner->links[msg_bearer_id(tunnel_msg)];
	assert(dest_link != *l_ptr);
	if (!dest_link) {
		msg_dbg(tunnel_msg, "NOLINK/<REC<");
		goto exit;
	}
	dbg("%c<-%c:", dest_link->b_ptr->net_plane,
	    (*l_ptr)->b_ptr->net_plane);
	*l_ptr = dest_link;
	msg = msg_get_wrapped(tunnel_msg);

	if (msg_typ == DUPLICATE_MSG) {
		if (less(msg_seqno(msg), mod(dest_link->next_in_no))) {
			msg_dbg(tunnel_msg, "DROP/<REC<");
			goto exit;
		}
		*buf = buf_extract(tunnel_buf,INT_H_SIZE);
		if (*buf == NULL) {
			warn("Memory squeeze; failed to extract msg\n");
			goto exit;
		}
		msg_dbg(tunnel_msg, "TNL<REC<");
		buf_discard(tunnel_buf);
		return 1;
	}

	/* First original message ?: */

	if (link_is_up(dest_link)) {
		msg_dbg(tunnel_msg, "UP/FIRST/<REC<");
		link_reset(dest_link);
		dest_link->exp_msg_count = msg_count;
		if (!msg_count)
			goto exit;
	} else if (dest_link->exp_msg_count == START_CHANGEOVER) {
		msg_dbg(tunnel_msg, "BLK/FIRST/<REC<");
		dest_link->exp_msg_count = msg_count;
		if (!msg_count)
			goto exit;
	}

	/* Receive original message */

	if (dest_link->exp_msg_count == 0) {
		msg_dbg(tunnel_msg, "OVERDUE/DROP/<REC<");
		dbg_print_link(dest_link, "LINK:");
		goto exit;
	}
	dest_link->exp_msg_count--;
	if (less(msg_seqno(msg), dest_link->reset_checkpoint)) {
		msg_dbg(tunnel_msg, "DROP/DUPL/<REC<");
		goto exit;
	} else {
		*buf = buf_extract(tunnel_buf, INT_H_SIZE);
		if (*buf != NULL) {
			msg_dbg(tunnel_msg, "TNL<REC<");
			buf_discard(tunnel_buf);
			return 1;
		} else {
			warn("Memory squeeze; dropped incoming msg\n");
		}
	}
exit:
	*buf = 0;
	buf_discard(tunnel_buf);
	return 0;
}

/*
 *  Bundler functionality:
 */
void link_recv_bundle(struct sk_buff *buf)
{
	u32 msgcount = msg_msgcnt(buf_msg(buf));
	u32 pos = INT_H_SIZE;
	struct sk_buff *obuf;

	msg_dbg(buf_msg(buf), "<BNDL<: ");
	while (msgcount--) {
		obuf = buf_extract(buf, pos);
		if (obuf == NULL) {
			char addr_string[16];

			warn("Buffer allocation failure;\n");
			warn("  incoming message(s) from %s lost\n",
			     addr_string_fill(addr_string, 
					      msg_orignode(buf_msg(buf))));
			return;
		};
		pos += align(msg_size(buf_msg(obuf)));
		msg_dbg(buf_msg(obuf), "     /");
		net_route_msg(obuf);
	}
	buf_discard(buf);
}

/*
 *  Fragmentation/defragmentation:
 */


/* 
 * link_send_long_buf: Entry for buffers needing fragmentation.
 * The buffer is complete, inclusive total message length. 
 * Returns user data length.
 */
int link_send_long_buf(struct link *l_ptr, struct sk_buff *buf)
{
	struct tipc_msg *inmsg = buf_msg(buf);
	struct tipc_msg fragm_hdr;
	u32 insize = msg_size(inmsg);
	u32 dsz = msg_data_sz(inmsg);
	unchar *crs = buf->data;
	u32 rest = insize;
	u32 pack_sz = link_max_pkt(l_ptr);
	u32 fragm_sz = pack_sz - INT_H_SIZE;
	u32 fragm_no = 1;
	u32 destaddr = msg_destnode(inmsg);

	if (msg_short(inmsg))
		destaddr = l_ptr->addr;

	if (msg_routed(inmsg))
		msg_set_prevnode(inmsg, tipc_own_addr);

	/* Prepare reusable fragment header: */

	msg_init(&fragm_hdr, MSG_FRAGMENTER, FIRST_FRAGMENT,
		 TIPC_OK, INT_H_SIZE, destaddr);
	msg_set_link_selector(&fragm_hdr, msg_link_selector(inmsg));
	msg_set_long_msgno(&fragm_hdr, mod(l_ptr->long_msg_seq_no++));
	msg_set_fragm_no(&fragm_hdr, fragm_no);
	l_ptr->stats.sent_fragmented++;

	/* Chop up message: */

	while (rest > 0) {
		struct sk_buff *fragm;

		if (rest <= fragm_sz) {
			fragm_sz = rest;
			msg_set_type(&fragm_hdr, LAST_FRAGMENT);
		}
		fragm = buf_acquire(fragm_sz + INT_H_SIZE);
		if (fragm == NULL) {
			warn("Memory squeeze; failed to fragment msg\n");
			dsz = -ENOMEM;
			goto exit;
		}
		msg_set_size(&fragm_hdr, fragm_sz + INT_H_SIZE);
		memcpy(fragm->data, (unchar *)&fragm_hdr, INT_H_SIZE);
		memcpy(fragm->data + INT_H_SIZE, crs, fragm_sz);

		/*  Send queued messages first, if any: */

		l_ptr->stats.sent_fragments++;
		link_send_buf(l_ptr, fragm);
		if (!link_is_up(l_ptr))
			return dsz;
		msg_set_fragm_no(&fragm_hdr, ++fragm_no);
		rest -= fragm_sz;
		crs += fragm_sz;
		msg_set_type(&fragm_hdr, FRAGMENT);
	}
exit:
	buf_discard(buf);
	return dsz;
}

/* 
 * A pending message being re-assembled must store certain values 
 * to handle subsequent fragments correctly. The following functions 
 * help storing these values in unused, available fields in the
 * pending message. This makes dynamic memory allocation unecessary.
 */

static inline u32 get_long_msg_seqno(struct sk_buff *buf)
{
	return msg_seqno(buf_msg(buf));
}

static inline void set_long_msg_seqno(struct sk_buff *buf, u32 seqno)
{
	msg_set_seqno(buf_msg(buf), seqno);
}

static inline u32 get_fragm_size(struct sk_buff *buf)
{
	return msg_ack(buf_msg(buf));
}

static inline void set_fragm_size(struct sk_buff *buf, u32 sz)
{
	msg_set_ack(buf_msg(buf), sz);
}

static inline u32 get_expected_frags(struct sk_buff *buf)
{
	return msg_bcast_ack(buf_msg(buf));
}

static inline void set_expected_frags(struct sk_buff *buf, u32 exp)
{
	msg_set_bcast_ack(buf_msg(buf), exp);
}

static inline u32 get_timer_cnt(struct sk_buff *buf)
{
	return msg_reroute_cnt(buf_msg(buf));
}

static inline void incr_timer_cnt(struct sk_buff *buf)
{
	msg_incr_reroute_cnt(buf_msg(buf));
}

/* 
 * link_recv_fragment(): Called with node lock on. Returns 
 * the reassembled buffer if message is complete.
 */
int link_recv_fragment(struct sk_buff **pending, struct sk_buff **fb, 
		       struct tipc_msg **m)
{
	struct sk_buff *prev = 0;
	struct sk_buff *fbuf = *fb;
	struct tipc_msg *fragm = buf_msg(fbuf);
	struct sk_buff *pbuf = *pending;
	u32 long_msg_seq_no = msg_long_msgno(fragm);

	*fb = 0;
	msg_dbg(fragm,"FRG<REC<");

	/* Is there an incomplete message waiting for this fragment? */

	while (pbuf && ((msg_seqno(buf_msg(pbuf)) != long_msg_seq_no)
			|| (msg_orignode(fragm) != msg_orignode(buf_msg(pbuf))))) {
		prev = pbuf;
		pbuf = pbuf->next;
	}

	if (!pbuf && (msg_type(fragm) == FIRST_FRAGMENT)) {
		struct tipc_msg *imsg = (struct tipc_msg *)msg_data(fragm);
		u32 msg_sz = msg_size(imsg);
		u32 fragm_sz = msg_data_sz(fragm);
		u32 exp_fragm_cnt = msg_sz/fragm_sz + !!(msg_sz % fragm_sz);
		u32 max =  TIPC_MAX_USER_MSG_SIZE + LONG_H_SIZE;
		if (msg_type(imsg) == TIPC_MCAST_MSG)
			max = TIPC_MAX_USER_MSG_SIZE + MCAST_H_SIZE;
		if (msg_size(imsg) > max) {
			msg_dbg(fragm,"<REC<Oversized: ");
			buf_discard(fbuf);
			return 0;
		}
		pbuf = buf_acquire(msg_size(imsg));
		if (pbuf != NULL) {
			pbuf->next = *pending;
			*pending = pbuf;
			memcpy(pbuf->data, (unchar *)imsg, msg_data_sz(fragm));

			/*  Prepare buffer for subsequent fragments. */

			set_long_msg_seqno(pbuf, long_msg_seq_no); 
			set_fragm_size(pbuf,fragm_sz); 
			set_expected_frags(pbuf,exp_fragm_cnt - 1); 
		} else {
			warn("Memory squeeze; got no defragmenting buffer\n");
		}
		buf_discard(fbuf);
		return 0;
	} else if (pbuf && (msg_type(fragm) != FIRST_FRAGMENT)) {
		u32 dsz = msg_data_sz(fragm);
		u32 fsz = get_fragm_size(pbuf);
		u32 crs = ((msg_fragm_no(fragm) - 1) * fsz);
		u32 exp_frags = get_expected_frags(pbuf) - 1;
		memcpy(pbuf->data + crs, msg_data(fragm), dsz);
		buf_discard(fbuf);

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
		set_expected_frags(pbuf,exp_frags);     
		return 0;
	}
	dbg(" Discarding orphan fragment %x\n",fbuf);
	msg_dbg(fragm,"ORPHAN:");
	dbg("Pending long buffers:\n");
	dbg_print_buf_chain(*pending);
	buf_discard(fbuf);
	return 0;
}

/**
 * link_check_defragm_bufs - flush stale incoming message fragments
 * @l_ptr: pointer to link
 */

static void link_check_defragm_bufs(struct link *l_ptr)
{
	struct sk_buff *prev = 0;
	struct sk_buff *next = 0;
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
			dbg(" Discarding incomplete long buffer\n");
			msg_dbg(buf_msg(buf), "LONG:");
			dbg_print_link(l_ptr, "curr:");
			dbg("Pending long buffers:\n");
			dbg_print_buf_chain(l_ptr->defragm_buf);
			if (prev)
				prev->next = buf->next;
			else
				l_ptr->defragm_buf = buf->next;
			buf_discard(buf);
		}
		buf = next;
	}
}



static void link_set_supervision_props(struct link *l_ptr, u32 tolerance)
{
	l_ptr->tolerance = tolerance;
	l_ptr->continuity_interval =
		((tolerance / 4) > 500) ? 500 : tolerance / 4;
	l_ptr->abort_limit = tolerance / (l_ptr->continuity_interval / 4);
}


void link_set_queue_limits(struct link *l_ptr, u32 window)
{
	/* Data messages from this node, inclusive FIRST_FRAGM */
	l_ptr->queue_limit[DATA_LOW] = window;
	l_ptr->queue_limit[DATA_MEDIUM] = (window / 3) * 4;
	l_ptr->queue_limit[DATA_HIGH] = (window / 3) * 5;
	l_ptr->queue_limit[DATA_CRITICAL] = (window / 3) * 6;
	/* Transiting data messages,inclusive FIRST_FRAGM */
	l_ptr->queue_limit[DATA_LOW + 4] = 300;
	l_ptr->queue_limit[DATA_MEDIUM + 4] = 600;
	l_ptr->queue_limit[DATA_HIGH + 4] = 900;
	l_ptr->queue_limit[DATA_CRITICAL + 4] = 1200;
	l_ptr->queue_limit[CONN_MANAGER] = 1200;
	l_ptr->queue_limit[ROUTE_DISTRIBUTOR] = 1200;
	l_ptr->queue_limit[CHANGEOVER_PROTOCOL] = 2500;
	l_ptr->queue_limit[NAME_DISTRIBUTOR] = 3000;
	/* FRAGMENT and LAST_FRAGMENT packets */
	l_ptr->queue_limit[MSG_FRAGMENTER] = 4000;
}

/**
 * link_find_link - locate link by name
 * @name - ptr to link name string
 * @node - ptr to area to be filled with ptr to associated node
 * 
 * Caller must hold 'net_lock' to ensure node and bearer are not deleted;
 * this also prevents link deletion.
 * 
 * Returns pointer to link (or 0 if invalid link name).
 */

static struct link *link_find_link(const char *name, struct node **node)
{
	struct link_name link_name_parts;
	struct bearer *b_ptr;
	struct link *l_ptr; 

	if (!link_name_validate(name, &link_name_parts))
		return 0;

	b_ptr = bearer_find_interface(link_name_parts.if_local);
	if (!b_ptr)
		return 0;

	*node = node_find(link_name_parts.addr_peer); 
	if (!*node)
		return 0;

	l_ptr = (*node)->links[b_ptr->identity];
	if (!l_ptr || strcmp(l_ptr->name, name))
		return 0;

	return l_ptr;
}

struct sk_buff *link_cmd_config(const void *req_tlv_area, int req_tlv_space, 
			        u16 cmd)
{
	struct tipc_link_config *args;
        u32 new_value;
	struct link *l_ptr;
	struct node *node;
        int res;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_LINK_CONFIG))
		return cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	args = (struct tipc_link_config *)TLV_DATA(req_tlv_area);
	new_value = ntohl(args->value);

	if (!strcmp(args->name, bc_link_name)) {
		if ((cmd == TIPC_CMD_SET_LINK_WINDOW) &&
		    (bclink_set_queue_limits(new_value) == 0))
			return cfg_reply_none();
	       	return cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
					      " (cannot change setting on broadcast link)");
	}

	read_lock_bh(&net_lock);
	l_ptr = link_find_link(args->name, &node); 
	if (!l_ptr) {
		read_unlock_bh(&net_lock);
	       	return cfg_reply_error_string("link not found");
	}

	node_lock(node);
	res = -EINVAL;
	switch (cmd) {
	case TIPC_CMD_SET_LINK_TOL: 
		if ((new_value >= TIPC_MIN_LINK_TOL) && 
		    (new_value <= TIPC_MAX_LINK_TOL)) {
			link_set_supervision_props(l_ptr, new_value);
			link_send_proto_msg(l_ptr, STATE_MSG, 
					    0, 0, new_value, 0, 0);
			res = TIPC_OK;
		}
		break;
	case TIPC_CMD_SET_LINK_PRI: 
		if (new_value < TIPC_NUM_LINK_PRI) {
			l_ptr->priority = new_value;
			link_send_proto_msg(l_ptr, STATE_MSG, 
					    0, 0, 0, new_value, 0);
			res = TIPC_OK;
		}
		break;
	case TIPC_CMD_SET_LINK_WINDOW: 
		if ((new_value >= TIPC_MIN_LINK_WIN) && 
		    (new_value <= TIPC_MAX_LINK_WIN)) {
			link_set_queue_limits(l_ptr, new_value);
			res = TIPC_OK;
		}
		break;
	}
	node_unlock(node);

	read_unlock_bh(&net_lock);
	if (res)
	       	return cfg_reply_error_string("cannot change link setting");

	return cfg_reply_none();
}

/**
 * link_reset_statistics - reset link statistics
 * @l_ptr: pointer to link
 */

static void link_reset_statistics(struct link *l_ptr)
{
	memset(&l_ptr->stats, 0, sizeof(l_ptr->stats));
	l_ptr->stats.sent_info = l_ptr->next_out_no;
	l_ptr->stats.recv_info = l_ptr->next_in_no;
}

struct sk_buff *link_cmd_reset_stats(const void *req_tlv_area, int req_tlv_space)
{
	char *link_name;
	struct link *l_ptr; 
	struct node *node;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_LINK_NAME))
		return cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	link_name = (char *)TLV_DATA(req_tlv_area);
	if (!strcmp(link_name, bc_link_name)) {
		if (bclink_reset_stats())
			return cfg_reply_error_string("link not found");
		return cfg_reply_none();
	}

	read_lock_bh(&net_lock);
	l_ptr = link_find_link(link_name, &node); 
	if (!l_ptr) {
		read_unlock_bh(&net_lock);
		return cfg_reply_error_string("link not found");
	}

	node_lock(node);
	link_reset_statistics(l_ptr);
	node_unlock(node);
	read_unlock_bh(&net_lock);
	return cfg_reply_none();
}

/**
 * percent - convert count to a percentage of total (rounding up or down)
 */

static u32 percent(u32 count, u32 total)
{
	return (count * 100 + (total / 2)) / total;
}

/**
 * link_stats - print link statistics
 * @name: link name
 * @buf: print buffer area
 * @buf_size: size of print buffer area
 * 
 * Returns length of print buffer data string (or 0 if error)
 */

static int link_stats(const char *name, char *buf, const u32 buf_size)
{
	struct print_buf pb;
	struct link *l_ptr; 
	struct node *node;
	char *status;
	u32 profile_total = 0;

	if (!strcmp(name, bc_link_name))
		return bclink_stats(buf, buf_size);

	printbuf_init(&pb, buf, buf_size);

	read_lock_bh(&net_lock);
	l_ptr = link_find_link(name, &node); 
	if (!l_ptr) {
		read_unlock_bh(&net_lock);
		return 0;
	}
	node_lock(node);

	if (link_is_active(l_ptr))
		status = "ACTIVE";
	else if (link_is_up(l_ptr))
		status = "STANDBY";
	else
		status = "DEFUNCT";
	tipc_printf(&pb, "Link <%s>\n"
		         "  %s  MTU:%u  Priority:%u  Tolerance:%u ms"
		         "  Window:%u packets\n", 
		    l_ptr->name, status, link_max_pkt(l_ptr), 
		    l_ptr->priority, l_ptr->tolerance, l_ptr->queue_limit[0]);
	tipc_printf(&pb, "  RX packets:%u fragments:%u/%u bundles:%u/%u\n", 
		    l_ptr->next_in_no - l_ptr->stats.recv_info,
		    l_ptr->stats.recv_fragments,
		    l_ptr->stats.recv_fragmented,
		    l_ptr->stats.recv_bundles,
		    l_ptr->stats.recv_bundled);
	tipc_printf(&pb, "  TX packets:%u fragments:%u/%u bundles:%u/%u\n", 
		    l_ptr->next_out_no - l_ptr->stats.sent_info,
		    l_ptr->stats.sent_fragments,
		    l_ptr->stats.sent_fragmented, 
		    l_ptr->stats.sent_bundles,
		    l_ptr->stats.sent_bundled);
	profile_total = l_ptr->stats.msg_length_counts;
	if (!profile_total)
		profile_total = 1;
	tipc_printf(&pb, "  TX profile sample:%u packets  average:%u octets\n"
		         "  0-64:%u%% -256:%u%% -1024:%u%% -4096:%u%% "
		         "-16354:%u%% -32768:%u%% -66000:%u%%\n",
		    l_ptr->stats.msg_length_counts,
		    l_ptr->stats.msg_lengths_total / profile_total,
		    percent(l_ptr->stats.msg_length_profile[0], profile_total),
		    percent(l_ptr->stats.msg_length_profile[1], profile_total),
		    percent(l_ptr->stats.msg_length_profile[2], profile_total),
		    percent(l_ptr->stats.msg_length_profile[3], profile_total),
		    percent(l_ptr->stats.msg_length_profile[4], profile_total),
		    percent(l_ptr->stats.msg_length_profile[5], profile_total),
		    percent(l_ptr->stats.msg_length_profile[6], profile_total));
	tipc_printf(&pb, "  RX states:%u probes:%u naks:%u defs:%u dups:%u\n", 
		    l_ptr->stats.recv_states,
		    l_ptr->stats.recv_probes,
		    l_ptr->stats.recv_nacks,
		    l_ptr->stats.deferred_recv, 
		    l_ptr->stats.duplicates);
	tipc_printf(&pb, "  TX states:%u probes:%u naks:%u acks:%u dups:%u\n", 
		    l_ptr->stats.sent_states, 
		    l_ptr->stats.sent_probes, 
		    l_ptr->stats.sent_nacks, 
		    l_ptr->stats.sent_acks, 
		    l_ptr->stats.retransmitted);
	tipc_printf(&pb, "  Congestion bearer:%u link:%u  Send queue max:%u avg:%u\n",
		    l_ptr->stats.bearer_congs,
		    l_ptr->stats.link_congs, 
		    l_ptr->stats.max_queue_sz,
		    l_ptr->stats.queue_sz_counts
		    ? (l_ptr->stats.accu_queue_sz / l_ptr->stats.queue_sz_counts)
		    : 0);

	node_unlock(node);
	read_unlock_bh(&net_lock);
	return printbuf_validate(&pb);
}

#define MAX_LINK_STATS_INFO 2000

struct sk_buff *link_cmd_show_stats(const void *req_tlv_area, int req_tlv_space)
{
	struct sk_buff *buf;
	struct tlv_desc *rep_tlv;
	int str_len;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_LINK_NAME))
		return cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	buf = cfg_reply_alloc(TLV_SPACE(MAX_LINK_STATS_INFO));
	if (!buf)
		return NULL;

	rep_tlv = (struct tlv_desc *)buf->data;

	str_len = link_stats((char *)TLV_DATA(req_tlv_area),
			     (char *)TLV_DATA(rep_tlv), MAX_LINK_STATS_INFO);
	if (!str_len) {
		buf_discard(buf);
	       	return cfg_reply_error_string("link not found");
	}

	skb_put(buf, TLV_SPACE(str_len));
	TLV_SET(rep_tlv, TIPC_TLV_ULTRA_STRING, NULL, str_len);

	return buf;
}

#if 0
int link_control(const char *name, u32 op, u32 val)
{
	int res = -EINVAL;
	struct link *l_ptr;
	u32 bearer_id;
	struct node * node;
	u32 a;

	a = link_name2addr(name, &bearer_id);
	read_lock_bh(&net_lock);
	node = node_find(a);
	if (node) {
		node_lock(node);
		l_ptr = node->links[bearer_id];
		if (l_ptr) {
			if (op == TIPC_REMOVE_LINK) {
				struct bearer *b_ptr = l_ptr->b_ptr;
				spin_lock_bh(&b_ptr->publ.lock);
				link_delete(l_ptr);
				spin_unlock_bh(&b_ptr->publ.lock);
			}
			if (op == TIPC_CMD_BLOCK_LINK) {
				link_reset(l_ptr);
				l_ptr->blocked = 1;
			}
			if (op == TIPC_CMD_UNBLOCK_LINK) {
				l_ptr->blocked = 0;
			}
			res = TIPC_OK;
		}
		node_unlock(node);
	}
	read_unlock_bh(&net_lock);
	return res;
}
#endif

/**
 * link_get_max_pkt - get maximum packet size to use when sending to destination
 * @dest: network address of destination node
 * @selector: used to select from set of active links
 * 
 * If no active link can be found, uses default maximum packet size.
 */

u32 link_get_max_pkt(u32 dest, u32 selector)
{
	struct node *n_ptr;
	struct link *l_ptr;
	u32 res = MAX_PKT_DEFAULT;
	
	if (dest == tipc_own_addr)
		return MAX_MSG_SIZE;

	read_lock_bh(&net_lock);        
	n_ptr = node_select(dest, selector);
	if (n_ptr) {
		node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector & 1];
		if (l_ptr)
			res = link_max_pkt(l_ptr);
		node_unlock(n_ptr);
	}
	read_unlock_bh(&net_lock);       
	return res;
}

#if 0
static void link_dump_rec_queue(struct link *l_ptr)
{
	struct sk_buff *crs;

	if (!l_ptr->oldest_deferred_in) {
		info("Reception queue empty\n");
		return;
	}
	info("Contents of Reception queue:\n");
	crs = l_ptr->oldest_deferred_in;
	while (crs) {
		if (crs->data == (void *)0x0000a3a3) {
			info("buffer %x invalid\n", crs);
			return;
		}
		msg_dbg(buf_msg(crs), "In rec queue: \n");
		crs = crs->next;
	}
}
#endif

static void link_dump_send_queue(struct link *l_ptr)
{
	if (l_ptr->next_out) {
		info("\nContents of unsent queue:\n");
		dbg_print_buf_chain(l_ptr->next_out);
	}
	info("\nContents of send queue:\n");
	if (l_ptr->first_out) {
		dbg_print_buf_chain(l_ptr->first_out);
	}
	info("Empty send queue\n");
}

static void link_print(struct link *l_ptr, struct print_buf *buf,
		       const char *str)
{
	tipc_printf(buf, str);
	if (link_reset_reset(l_ptr) || link_reset_unknown(l_ptr))
		return;
	tipc_printf(buf, "Link %x<%s>:",
		    l_ptr->addr, l_ptr->b_ptr->publ.name);
	tipc_printf(buf, ": NXO(%u):", mod(l_ptr->next_out_no));
	tipc_printf(buf, "NXI(%u):", mod(l_ptr->next_in_no));
	tipc_printf(buf, "SQUE");
	if (l_ptr->first_out) {
		tipc_printf(buf, "[%u..", msg_seqno(buf_msg(l_ptr->first_out)));
		if (l_ptr->next_out)
			tipc_printf(buf, "%u..",
				    msg_seqno(buf_msg(l_ptr->next_out)));
		tipc_printf(buf, "%u]",
			    msg_seqno(buf_msg
				      (l_ptr->last_out)), l_ptr->out_queue_size);
		if ((mod(msg_seqno(buf_msg(l_ptr->last_out)) - 
			 msg_seqno(buf_msg(l_ptr->first_out))) 
		     != (l_ptr->out_queue_size - 1))
		    || (l_ptr->last_out->next != 0)) {
			tipc_printf(buf, "\nSend queue inconsistency\n");
			tipc_printf(buf, "first_out= %x ", l_ptr->first_out);
			tipc_printf(buf, "next_out= %x ", l_ptr->next_out);
			tipc_printf(buf, "last_out= %x ", l_ptr->last_out);
			link_dump_send_queue(l_ptr);
		}
	} else
		tipc_printf(buf, "[]");
	tipc_printf(buf, "SQSIZ(%u)", l_ptr->out_queue_size);
	if (l_ptr->oldest_deferred_in) {
		u32 o = msg_seqno(buf_msg(l_ptr->oldest_deferred_in));
		u32 n = msg_seqno(buf_msg(l_ptr->newest_deferred_in));
		tipc_printf(buf, ":RQUE[%u..%u]", o, n);
		if (l_ptr->deferred_inqueue_sz != mod((n + 1) - o)) {
			tipc_printf(buf, ":RQSIZ(%u)",
				    l_ptr->deferred_inqueue_sz);
		}
	}
	if (link_working_unknown(l_ptr))
		tipc_printf(buf, ":WU");
	if (link_reset_reset(l_ptr))
		tipc_printf(buf, ":RR");
	if (link_reset_unknown(l_ptr))
		tipc_printf(buf, ":RU");
	if (link_working_working(l_ptr))
		tipc_printf(buf, ":WW");
	tipc_printf(buf, "\n");
}


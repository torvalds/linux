/*
 * net/tipc/port.h: Include file for TIPC port code
 *
 * Copyright (c) 1994-2007, Ericsson AB
 * Copyright (c) 2004-2007, Wind River Systems
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

#ifndef _TIPC_PORT_H
#define _TIPC_PORT_H

#include "core.h"
#include "ref.h"
#include "net.h"
#include "msg.h"
#include "dbg.h"
#include "node_subscr.h"

/**
 * struct user_port - TIPC user port (used with native API)
 * @user_ref: id of user who created user port
 * @usr_handle: user-specified field
 * @ref: object reference to associated TIPC port
 * <various callback routines>
 * @uport_list: adjacent user ports in list of ports held by user
 */

struct user_port {
	u32 user_ref;
	void *usr_handle;
	u32 ref;
	tipc_msg_err_event err_cb;
	tipc_named_msg_err_event named_err_cb;
	tipc_conn_shutdown_event conn_err_cb;
	tipc_msg_event msg_cb;
	tipc_named_msg_event named_msg_cb;
	tipc_conn_msg_event conn_msg_cb;
	tipc_continue_event continue_event_cb;
	struct list_head uport_list;
};

/**
 * struct port - TIPC port structure
 * @publ: TIPC port info available to privileged users
 * @port_list: adjacent ports in TIPC's global list of ports
 * @dispatcher: ptr to routine which handles received messages
 * @wakeup: ptr to routine to call when port is no longer congested
 * @user_port: ptr to user port associated with port (if any)
 * @wait_list: adjacent ports in list of ports waiting on link congestion
 * @congested_link: ptr to congested link port is waiting on
 * @waiting_pkts:
 * @sent:
 * @acked:
 * @publications: list of publications for port
 * @pub_count: total # of publications port has made during its lifetime
 * @probing_state:
 * @probing_interval:
 * @last_in_seqno:
 * @timer_ref:
 * @subscription: "node down" subscription used to terminate failed connections
 */

struct port {
	struct tipc_port publ;
	struct list_head port_list;
	u32 (*dispatcher)(struct tipc_port *, struct sk_buff *);
	void (*wakeup)(struct tipc_port *);
	struct user_port *user_port;
	struct list_head wait_list;
	struct link *congested_link;
	u32 waiting_pkts;
	u32 sent;
	u32 acked;
	struct list_head publications;
	u32 pub_count;
	u32 probing_state;
	u32 probing_interval;
	u32 last_in_seqno;
	struct timer_list timer;
	struct node_subscr subscription;
};

extern spinlock_t tipc_port_list_lock;
struct port_list;

int tipc_port_recv_sections(struct port *p_ptr, u32 num_sect,
			    struct iovec const *msg_sect);
int tipc_port_reject_sections(struct port *p_ptr, struct tipc_msg *hdr,
			      struct iovec const *msg_sect, u32 num_sect,
			      int err);
struct sk_buff *tipc_port_get_ports(void);
struct sk_buff *port_show_stats(const void *req_tlv_area, int req_tlv_space);
void tipc_port_recv_proto_msg(struct sk_buff *buf);
void tipc_port_recv_mcast(struct sk_buff *buf, struct port_list *dp);
void tipc_port_reinit(void);

/**
 * tipc_port_lock - lock port instance referred to and return its pointer
 */

static inline struct port *tipc_port_lock(u32 ref)
{
	return (struct port *)tipc_ref_lock(ref);
}

/**
 * tipc_port_unlock - unlock a port instance
 *
 * Can use pointer instead of tipc_ref_unlock() since port is already locked.
 */

static inline void tipc_port_unlock(struct port *p_ptr)
{
	spin_unlock_bh(p_ptr->publ.lock);
}

static inline struct port* tipc_port_deref(u32 ref)
{
	return (struct port *)tipc_ref_deref(ref);
}

static inline u32 tipc_peer_port(struct port *p_ptr)
{
	return msg_destport(&p_ptr->publ.phdr);
}

static inline u32 tipc_peer_node(struct port *p_ptr)
{
	return msg_destnode(&p_ptr->publ.phdr);
}

static inline int tipc_port_congested(struct port *p_ptr)
{
	return((p_ptr->sent - p_ptr->acked) >= (TIPC_FLOW_CONTROL_WIN * 2));
}

/**
 * tipc_port_recv_msg - receive message from lower layer and deliver to port user
 */

static inline int tipc_port_recv_msg(struct sk_buff *buf)
{
	struct port *p_ptr;
	struct tipc_msg *msg = buf_msg(buf);
	u32 destport = msg_destport(msg);
	u32 dsz = msg_data_sz(msg);
	u32 err;

	/* forward unresolved named message */
	if (unlikely(!destport)) {
		tipc_net_route_msg(buf);
		return dsz;
	}

	/* validate destination & pass to port, otherwise reject message */
	p_ptr = tipc_port_lock(destport);
	if (likely(p_ptr)) {
		if (likely(p_ptr->publ.connected)) {
			if ((unlikely(msg_origport(msg) != tipc_peer_port(p_ptr))) ||
			    (unlikely(msg_orignode(msg) != tipc_peer_node(p_ptr))) ||
			    (unlikely(!msg_connected(msg)))) {
				err = TIPC_ERR_NO_PORT;
				tipc_port_unlock(p_ptr);
				goto reject;
			}
		}
		err = p_ptr->dispatcher(&p_ptr->publ, buf);
		tipc_port_unlock(p_ptr);
		if (likely(!err))
			return dsz;
	} else {
		err = TIPC_ERR_NO_PORT;
	}
reject:
	dbg("port->rejecting, err = %x..\n",err);
	return tipc_reject_msg(buf, err);
}

#endif

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

#include "ref.h"
#include "net.h"
#include "msg.h"
#include "node_subscr.h"

#define TIPC_FLOW_CONTROL_WIN 512

typedef void (*tipc_msg_err_event) (void *usr_handle, u32 portref,
		struct sk_buff **buf, unsigned char const *data,
		unsigned int size, int reason,
		struct tipc_portid const *attmpt_destid);

typedef void (*tipc_named_msg_err_event) (void *usr_handle, u32 portref,
		struct sk_buff **buf, unsigned char const *data,
		unsigned int size, int reason,
		struct tipc_name_seq const *attmpt_dest);

typedef void (*tipc_conn_shutdown_event) (void *usr_handle, u32 portref,
		struct sk_buff **buf, unsigned char const *data,
		unsigned int size, int reason);

typedef void (*tipc_msg_event) (void *usr_handle, u32 portref,
		struct sk_buff **buf, unsigned char const *data,
		unsigned int size, unsigned int importance,
		struct tipc_portid const *origin);

typedef void (*tipc_named_msg_event) (void *usr_handle, u32 portref,
		struct sk_buff **buf, unsigned char const *data,
		unsigned int size, unsigned int importance,
		struct tipc_portid const *orig,
		struct tipc_name_seq const *dest);

typedef void (*tipc_conn_msg_event) (void *usr_handle, u32 portref,
		struct sk_buff **buf, unsigned char const *data,
		unsigned int size);

typedef void (*tipc_continue_event) (void *usr_handle, u32 portref);

/**
 * struct user_port - TIPC user port (used with native API)
 * @usr_handle: user-specified field
 * @ref: object reference to associated TIPC port
 * <various callback routines>
 */

struct user_port {
	void *usr_handle;
	u32 ref;
	tipc_msg_err_event err_cb;
	tipc_named_msg_err_event named_err_cb;
	tipc_conn_shutdown_event conn_err_cb;
	tipc_msg_event msg_cb;
	tipc_named_msg_event named_msg_cb;
	tipc_conn_msg_event conn_msg_cb;
	tipc_continue_event continue_event_cb;
};

/**
 * struct tipc_port - TIPC port info available to socket API
 * @usr_handle: pointer to additional user-defined information about port
 * @lock: pointer to spinlock for controlling access to port
 * @connected: non-zero if port is currently connected to a peer port
 * @conn_type: TIPC type used when connection was established
 * @conn_instance: TIPC instance used when connection was established
 * @conn_unacked: number of unacknowledged messages received from peer port
 * @published: non-zero if port has one or more associated names
 * @congested: non-zero if cannot send because of link or port congestion
 * @max_pkt: maximum packet size "hint" used when building messages sent by port
 * @ref: unique reference to port in TIPC object registry
 * @phdr: preformatted message header used when sending messages
 */
struct tipc_port {
	void *usr_handle;
	spinlock_t *lock;
	int connected;
	u32 conn_type;
	u32 conn_instance;
	u32 conn_unacked;
	int published;
	u32 congested;
	u32 max_pkt;
	u32 ref;
	struct tipc_msg phdr;
};

/**
 * struct port - TIPC port structure
 * @publ: TIPC port info available to privileged users
 * @port_list: adjacent ports in TIPC's global list of ports
 * @dispatcher: ptr to routine which handles received messages
 * @wakeup: ptr to routine to call when port is no longer congested
 * @user_port: ptr to user port associated with port (if any)
 * @wait_list: adjacent ports in list of ports waiting on link congestion
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
	u32 waiting_pkts;
	u32 sent;
	u32 acked;
	struct list_head publications;
	u32 pub_count;
	u32 probing_state;
	u32 probing_interval;
	u32 last_in_seqno;
	struct timer_list timer;
	struct tipc_node_subscr subscription;
};

extern spinlock_t tipc_port_list_lock;
struct port_list;

/*
 * TIPC port manipulation routines
 */
struct tipc_port *tipc_createport_raw(void *usr_handle,
		u32 (*dispatcher)(struct tipc_port *, struct sk_buff *),
		void (*wakeup)(struct tipc_port *), const u32 importance);

int tipc_reject_msg(struct sk_buff *buf, u32 err);

int tipc_send_buf_fast(struct sk_buff *buf, u32 destnode);

void tipc_acknowledge(u32 port_ref, u32 ack);

int tipc_createport(void *usr_handle,
		unsigned int importance, tipc_msg_err_event error_cb,
		tipc_named_msg_err_event named_error_cb,
		tipc_conn_shutdown_event conn_error_cb, tipc_msg_event msg_cb,
		tipc_named_msg_event named_msg_cb,
		tipc_conn_msg_event conn_msg_cb,
		tipc_continue_event continue_event_cb, u32 *portref);

int tipc_deleteport(u32 portref);

int tipc_portimportance(u32 portref, unsigned int *importance);
int tipc_set_portimportance(u32 portref, unsigned int importance);

int tipc_portunreliable(u32 portref, unsigned int *isunreliable);
int tipc_set_portunreliable(u32 portref, unsigned int isunreliable);

int tipc_portunreturnable(u32 portref, unsigned int *isunreturnable);
int tipc_set_portunreturnable(u32 portref, unsigned int isunreturnable);

int tipc_publish(u32 portref, unsigned int scope,
		struct tipc_name_seq const *name_seq);
int tipc_withdraw(u32 portref, unsigned int scope,
		struct tipc_name_seq const *name_seq);

int tipc_connect2port(u32 portref, struct tipc_portid const *port);

int tipc_disconnect(u32 portref);

int tipc_shutdown(u32 ref);


/*
 * The following routines require that the port be locked on entry
 */
int tipc_disconnect_port(struct tipc_port *tp_ptr);

/*
 * TIPC messaging routines
 */
int tipc_send(u32 portref, unsigned int num_sect, struct iovec const *msg_sect);

int tipc_send2name(u32 portref, struct tipc_name const *name, u32 domain,
		unsigned int num_sect, struct iovec const *msg_sect);

int tipc_send2port(u32 portref, struct tipc_portid const *dest,
		unsigned int num_sect, struct iovec const *msg_sect);

int tipc_send_buf2port(u32 portref, struct tipc_portid const *dest,
		struct sk_buff *buf, unsigned int dsz);

int tipc_multicast(u32 portref, struct tipc_name_seq const *seq,
		unsigned int section_count, struct iovec const *msg);

int tipc_port_reject_sections(struct port *p_ptr, struct tipc_msg *hdr,
			      struct iovec const *msg_sect, u32 num_sect,
			      int err);
struct sk_buff *tipc_port_get_ports(void);
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

static inline struct port *tipc_port_deref(u32 ref)
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
	return (p_ptr->sent - p_ptr->acked) >= (TIPC_FLOW_CONTROL_WIN * 2);
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
	return tipc_reject_msg(buf, err);
}

#endif

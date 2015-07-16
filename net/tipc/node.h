/*
 * net/tipc/node.h: Include file for TIPC node management routines
 *
 * Copyright (c) 2000-2006, 2014-2015, Ericsson AB
 * Copyright (c) 2005, 2010-2014, Wind River Systems
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

#ifndef _TIPC_NODE_H
#define _TIPC_NODE_H

#include "addr.h"
#include "net.h"
#include "bearer.h"
#include "msg.h"

/* Out-of-range value for node signature */
#define INVALID_NODE_SIG	0x10000

#define INVALID_BEARER_ID -1

/* Node FSM states and events:
 */
enum {
	SELF_DOWN_PEER_DOWN    = 0xdd,
	SELF_UP_PEER_UP        = 0xaa,
	SELF_DOWN_PEER_LEAVING = 0xd1,
	SELF_UP_PEER_COMING    = 0xac,
	SELF_COMING_PEER_UP    = 0xca,
	SELF_LEAVING_PEER_DOWN = 0x1d,
};

enum {
	SELF_ESTABL_CONTACT_EVT = 0xec,
	SELF_LOST_CONTACT_EVT   = 0x1c,
	PEER_ESTABL_CONTACT_EVT = 0xfec,
	PEER_LOST_CONTACT_EVT   = 0xf1c
};

/* Flags used to take different actions according to flag type
 * TIPC_WAIT_PEER_LINKS_DOWN: wait to see that peer's links are down
 * TIPC_WAIT_OWN_LINKS_DOWN: wait until peer node is declared down
 * TIPC_NOTIFY_NODE_DOWN: notify node is down
 * TIPC_NOTIFY_NODE_UP: notify node is up
 * TIPC_DISTRIBUTE_NAME: publish or withdraw link state name type
 */
enum {
	TIPC_MSG_EVT                    = 1,
	TIPC_NOTIFY_NODE_DOWN		= (1 << 3),
	TIPC_NOTIFY_NODE_UP		= (1 << 4),
	TIPC_WAKEUP_BCAST_USERS		= (1 << 5),
	TIPC_NOTIFY_LINK_UP		= (1 << 6),
	TIPC_NOTIFY_LINK_DOWN		= (1 << 7),
	TIPC_NAMED_MSG_EVT		= (1 << 8),
	TIPC_BCAST_MSG_EVT		= (1 << 9),
	TIPC_BCAST_RESET		= (1 << 10)
};

/**
 * struct tipc_node_bclink - TIPC node bclink structure
 * @acked: sequence # of last outbound b'cast message acknowledged by node
 * @last_in: sequence # of last in-sequence b'cast message received from node
 * @last_sent: sequence # of last b'cast message sent by node
 * @oos_state: state tracker for handling OOS b'cast messages
 * @deferred_queue: deferred queue saved OOS b'cast message received from node
 * @reasm_buf: broadcast reassembly queue head from node
 * @inputq_map: bitmap indicating which inqueues should be kicked
 * @recv_permitted: true if node is allowed to receive b'cast messages
 */
struct tipc_node_bclink {
	u32 acked;
	u32 last_in;
	u32 last_sent;
	u32 oos_state;
	u32 deferred_size;
	struct sk_buff_head deferdq;
	struct sk_buff *reasm_buf;
	struct sk_buff_head namedq;
	bool recv_permitted;
};

struct tipc_link_entry {
	struct tipc_link *link;
	u32 mtu;
	struct sk_buff_head inputq;
	struct tipc_media_addr maddr;
};

/**
 * struct tipc_node - TIPC node structure
 * @addr: network address of node
 * @ref: reference counter to node object
 * @lock: spinlock governing access to structure
 * @net: the applicable net namespace
 * @hash: links to adjacent nodes in unsorted hash chain
 * @inputq: pointer to input queue containing messages for msg event
 * @namedq: pointer to name table input queue with name table messages
 * @active_links: bearer ids of active links, used as index into links[] array
 * @links: array containing references to all links to node
 * @action_flags: bit mask of different types of node actions
 * @bclink: broadcast-related info
 * @list: links to adjacent nodes in sorted list of cluster's nodes
 * @working_links: number of working links to node (both active and standby)
 * @link_cnt: number of links to node
 * @capabilities: bitmap, indicating peer node's functional capabilities
 * @signature: node instance identifier
 * @link_id: local and remote bearer ids of changing link, if any
 * @publ_list: list of publications
 * @rcu: rcu struct for tipc_node
 */
struct tipc_node {
	u32 addr;
	struct kref kref;
	spinlock_t lock;
	struct net *net;
	struct hlist_node hash;
	struct sk_buff_head *inputq;
	struct sk_buff_head *namedq;
	int active_links[2];
	struct tipc_link_entry links[MAX_BEARERS];
	int action_flags;
	struct tipc_node_bclink bclink;
	struct list_head list;
	int state;
	int link_cnt;
	u16 working_links;
	u16 capabilities;
	u32 signature;
	u32 link_id;
	struct list_head publ_list;
	struct list_head conn_sks;
	unsigned long keepalive_intv;
	struct timer_list timer;
	struct rcu_head rcu;
};

struct tipc_node *tipc_node_find(struct net *net, u32 addr);
void tipc_node_put(struct tipc_node *node);
struct tipc_node *tipc_node_create(struct net *net, u32 addr);
void tipc_node_stop(struct net *net);
void tipc_node_check_dest(struct tipc_node *n, struct tipc_bearer *bearer,
			  bool *link_up, bool *addr_match,
			  struct tipc_media_addr *maddr);
bool tipc_node_update_dest(struct tipc_node *n, struct tipc_bearer *bearer,
			   struct tipc_media_addr *maddr);
void tipc_node_attach_link(struct tipc_node *n_ptr, struct tipc_link *l_ptr);
void tipc_node_detach_link(struct tipc_node *n_ptr, struct tipc_link *l_ptr);
void tipc_node_link_down(struct tipc_node *n_ptr, int bearer_id);
void tipc_node_link_up(struct tipc_node *n_ptr, int bearer_id);
bool tipc_node_is_up(struct tipc_node *n);
int tipc_node_get_linkname(struct net *net, u32 bearer_id, u32 node,
			   char *linkname, size_t len);
void tipc_node_unlock(struct tipc_node *node);
int tipc_node_xmit(struct net *net, struct sk_buff_head *list, u32 dnode,
		   int selector);
int tipc_node_xmit_skb(struct net *net, struct sk_buff *skb, u32 dest,
		       u32 selector);
int tipc_node_add_conn(struct net *net, u32 dnode, u32 port, u32 peer_port);
void tipc_node_remove_conn(struct net *net, u32 dnode, u32 port);
int tipc_nl_node_dump(struct sk_buff *skb, struct netlink_callback *cb);

static inline void tipc_node_lock(struct tipc_node *node)
{
	spin_lock_bh(&node->lock);
}

static inline struct tipc_link *node_active_link(struct tipc_node *n, int sel)
{
	int bearer_id = n->active_links[sel & 1];

	if (unlikely(bearer_id == INVALID_BEARER_ID))
		return NULL;

	return n->links[bearer_id].link;
}

static inline unsigned int tipc_node_get_mtu(struct net *net, u32 addr, u32 sel)
{
	struct tipc_node *n;
	int bearer_id;
	unsigned int mtu = MAX_MSG_SIZE;

	n = tipc_node_find(net, addr);
	if (unlikely(!n))
		return mtu;

	bearer_id = n->active_links[sel & 1];
	if (likely(bearer_id != INVALID_BEARER_ID))
		mtu = n->links[bearer_id].mtu;
	tipc_node_put(n);
	return mtu;
}

#endif

/*
 * net/tipc/node.c: TIPC node management routines
 *
 * Copyright (c) 2000-2006, 2012-2016, Ericsson AB
 * Copyright (c) 2005-2006, 2010-2014, Wind River Systems
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
#include "node.h"
#include "name_distr.h"
#include "socket.h"
#include "bcast.h"
#include "monitor.h"
#include "discover.h"
#include "netlink.h"

#define INVALID_NODE_SIG	0x10000
#define NODE_CLEANUP_AFTER	300000

/* Flags used to take different actions according to flag type
 * TIPC_NOTIFY_NODE_DOWN: notify node is down
 * TIPC_NOTIFY_NODE_UP: notify node is up
 * TIPC_DISTRIBUTE_NAME: publish or withdraw link state name type
 */
enum {
	TIPC_NOTIFY_NODE_DOWN		= (1 << 3),
	TIPC_NOTIFY_NODE_UP		= (1 << 4),
	TIPC_NOTIFY_LINK_UP		= (1 << 6),
	TIPC_NOTIFY_LINK_DOWN		= (1 << 7)
};

struct tipc_link_entry {
	struct tipc_link *link;
	spinlock_t lock; /* per link */
	u32 mtu;
	struct sk_buff_head inputq;
	struct tipc_media_addr maddr;
};

struct tipc_bclink_entry {
	struct tipc_link *link;
	struct sk_buff_head inputq1;
	struct sk_buff_head arrvq;
	struct sk_buff_head inputq2;
	struct sk_buff_head namedq;
};

/**
 * struct tipc_node - TIPC node structure
 * @addr: network address of node
 * @ref: reference counter to node object
 * @lock: rwlock governing access to structure
 * @net: the applicable net namespace
 * @hash: links to adjacent nodes in unsorted hash chain
 * @inputq: pointer to input queue containing messages for msg event
 * @namedq: pointer to name table input queue with name table messages
 * @active_links: bearer ids of active links, used as index into links[] array
 * @links: array containing references to all links to node
 * @action_flags: bit mask of different types of node actions
 * @state: connectivity state vs peer node
 * @sync_point: sequence number where synch/failover is finished
 * @list: links to adjacent nodes in sorted list of cluster's nodes
 * @working_links: number of working links to node (both active and standby)
 * @link_cnt: number of links to node
 * @capabilities: bitmap, indicating peer node's functional capabilities
 * @signature: node instance identifier
 * @link_id: local and remote bearer ids of changing link, if any
 * @publ_list: list of publications
 * @rcu: rcu struct for tipc_node
 * @delete_at: indicates the time for deleting a down node
 */
struct tipc_node {
	u32 addr;
	struct kref kref;
	rwlock_t lock;
	struct net *net;
	struct hlist_node hash;
	int active_links[2];
	struct tipc_link_entry links[MAX_BEARERS];
	struct tipc_bclink_entry bc_entry;
	int action_flags;
	struct list_head list;
	int state;
	bool failover_sent;
	u16 sync_point;
	int link_cnt;
	u16 working_links;
	u16 capabilities;
	u32 signature;
	u32 link_id;
	u8 peer_id[16];
	struct list_head publ_list;
	struct list_head conn_sks;
	unsigned long keepalive_intv;
	struct timer_list timer;
	struct rcu_head rcu;
	unsigned long delete_at;
};

/* Node FSM states and events:
 */
enum {
	SELF_DOWN_PEER_DOWN    = 0xdd,
	SELF_UP_PEER_UP        = 0xaa,
	SELF_DOWN_PEER_LEAVING = 0xd1,
	SELF_UP_PEER_COMING    = 0xac,
	SELF_COMING_PEER_UP    = 0xca,
	SELF_LEAVING_PEER_DOWN = 0x1d,
	NODE_FAILINGOVER       = 0xf0,
	NODE_SYNCHING          = 0xcc
};

enum {
	SELF_ESTABL_CONTACT_EVT = 0xece,
	SELF_LOST_CONTACT_EVT   = 0x1ce,
	PEER_ESTABL_CONTACT_EVT = 0x9ece,
	PEER_LOST_CONTACT_EVT   = 0x91ce,
	NODE_FAILOVER_BEGIN_EVT = 0xfbe,
	NODE_FAILOVER_END_EVT   = 0xfee,
	NODE_SYNCH_BEGIN_EVT    = 0xcbe,
	NODE_SYNCH_END_EVT      = 0xcee
};

static void __tipc_node_link_down(struct tipc_node *n, int *bearer_id,
				  struct sk_buff_head *xmitq,
				  struct tipc_media_addr **maddr);
static void tipc_node_link_down(struct tipc_node *n, int bearer_id,
				bool delete);
static void node_lost_contact(struct tipc_node *n, struct sk_buff_head *inputq);
static void tipc_node_delete(struct tipc_node *node);
static void tipc_node_timeout(struct timer_list *t);
static void tipc_node_fsm_evt(struct tipc_node *n, int evt);
static struct tipc_node *tipc_node_find(struct net *net, u32 addr);
static struct tipc_node *tipc_node_find_by_id(struct net *net, u8 *id);
static void tipc_node_put(struct tipc_node *node);
static bool node_is_up(struct tipc_node *n);
static void tipc_node_delete_from_list(struct tipc_node *node);

struct tipc_sock_conn {
	u32 port;
	u32 peer_port;
	u32 peer_node;
	struct list_head list;
};

static struct tipc_link *node_active_link(struct tipc_node *n, int sel)
{
	int bearer_id = n->active_links[sel & 1];

	if (unlikely(bearer_id == INVALID_BEARER_ID))
		return NULL;

	return n->links[bearer_id].link;
}

int tipc_node_get_mtu(struct net *net, u32 addr, u32 sel)
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

bool tipc_node_get_id(struct net *net, u32 addr, u8 *id)
{
	u8 *own_id = tipc_own_id(net);
	struct tipc_node *n;

	if (!own_id)
		return true;

	if (addr == tipc_own_addr(net)) {
		memcpy(id, own_id, TIPC_NODEID_LEN);
		return true;
	}
	n = tipc_node_find(net, addr);
	if (!n)
		return false;

	memcpy(id, &n->peer_id, TIPC_NODEID_LEN);
	tipc_node_put(n);
	return true;
}

u16 tipc_node_get_capabilities(struct net *net, u32 addr)
{
	struct tipc_node *n;
	u16 caps;

	n = tipc_node_find(net, addr);
	if (unlikely(!n))
		return TIPC_NODE_CAPABILITIES;
	caps = n->capabilities;
	tipc_node_put(n);
	return caps;
}

static void tipc_node_kref_release(struct kref *kref)
{
	struct tipc_node *n = container_of(kref, struct tipc_node, kref);

	kfree(n->bc_entry.link);
	kfree_rcu(n, rcu);
}

static void tipc_node_put(struct tipc_node *node)
{
	kref_put(&node->kref, tipc_node_kref_release);
}

static void tipc_node_get(struct tipc_node *node)
{
	kref_get(&node->kref);
}

/*
 * tipc_node_find - locate specified node object, if it exists
 */
static struct tipc_node *tipc_node_find(struct net *net, u32 addr)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_node *node;
	unsigned int thash = tipc_hashfn(addr);

	rcu_read_lock();
	hlist_for_each_entry_rcu(node, &tn->node_htable[thash], hash) {
		if (node->addr != addr)
			continue;
		if (!kref_get_unless_zero(&node->kref))
			node = NULL;
		break;
	}
	rcu_read_unlock();
	return node;
}

/* tipc_node_find_by_id - locate specified node object by its 128-bit id
 * Note: this function is called only when a discovery request failed
 * to find the node by its 32-bit id, and is not time critical
 */
static struct tipc_node *tipc_node_find_by_id(struct net *net, u8 *id)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_node *n;
	bool found = false;

	rcu_read_lock();
	list_for_each_entry_rcu(n, &tn->node_list, list) {
		read_lock_bh(&n->lock);
		if (!memcmp(id, n->peer_id, 16) &&
		    kref_get_unless_zero(&n->kref))
			found = true;
		read_unlock_bh(&n->lock);
		if (found)
			break;
	}
	rcu_read_unlock();
	return found ? n : NULL;
}

static void tipc_node_read_lock(struct tipc_node *n)
{
	read_lock_bh(&n->lock);
}

static void tipc_node_read_unlock(struct tipc_node *n)
{
	read_unlock_bh(&n->lock);
}

static void tipc_node_write_lock(struct tipc_node *n)
{
	write_lock_bh(&n->lock);
}

static void tipc_node_write_unlock_fast(struct tipc_node *n)
{
	write_unlock_bh(&n->lock);
}

static void tipc_node_write_unlock(struct tipc_node *n)
{
	struct net *net = n->net;
	u32 addr = 0;
	u32 flags = n->action_flags;
	u32 link_id = 0;
	u32 bearer_id;
	struct list_head *publ_list;

	if (likely(!flags)) {
		write_unlock_bh(&n->lock);
		return;
	}

	addr = n->addr;
	link_id = n->link_id;
	bearer_id = link_id & 0xffff;
	publ_list = &n->publ_list;

	n->action_flags &= ~(TIPC_NOTIFY_NODE_DOWN | TIPC_NOTIFY_NODE_UP |
			     TIPC_NOTIFY_LINK_DOWN | TIPC_NOTIFY_LINK_UP);

	write_unlock_bh(&n->lock);

	if (flags & TIPC_NOTIFY_NODE_DOWN)
		tipc_publ_notify(net, publ_list, addr);

	if (flags & TIPC_NOTIFY_NODE_UP)
		tipc_named_node_up(net, addr);

	if (flags & TIPC_NOTIFY_LINK_UP) {
		tipc_mon_peer_up(net, addr, bearer_id);
		tipc_nametbl_publish(net, TIPC_LINK_STATE, addr, addr,
				     TIPC_NODE_SCOPE, link_id, link_id);
	}
	if (flags & TIPC_NOTIFY_LINK_DOWN) {
		tipc_mon_peer_down(net, addr, bearer_id);
		tipc_nametbl_withdraw(net, TIPC_LINK_STATE, addr,
				      addr, link_id);
	}
}

static struct tipc_node *tipc_node_create(struct net *net, u32 addr,
					  u8 *peer_id, u16 capabilities)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_node *n, *temp_node;
	struct tipc_link *l;
	int bearer_id;
	int i;

	spin_lock_bh(&tn->node_list_lock);
	n = tipc_node_find(net, addr);
	if (n) {
		if (n->capabilities == capabilities)
			goto exit;
		/* Same node may come back with new capabilities */
		write_lock_bh(&n->lock);
		n->capabilities = capabilities;
		for (bearer_id = 0; bearer_id < MAX_BEARERS; bearer_id++) {
			l = n->links[bearer_id].link;
			if (l)
				tipc_link_update_caps(l, capabilities);
		}
		write_unlock_bh(&n->lock);
		goto exit;
	}
	n = kzalloc(sizeof(*n), GFP_ATOMIC);
	if (!n) {
		pr_warn("Node creation failed, no memory\n");
		goto exit;
	}
	n->addr = addr;
	memcpy(&n->peer_id, peer_id, 16);
	n->net = net;
	n->capabilities = capabilities;
	kref_init(&n->kref);
	rwlock_init(&n->lock);
	INIT_HLIST_NODE(&n->hash);
	INIT_LIST_HEAD(&n->list);
	INIT_LIST_HEAD(&n->publ_list);
	INIT_LIST_HEAD(&n->conn_sks);
	skb_queue_head_init(&n->bc_entry.namedq);
	skb_queue_head_init(&n->bc_entry.inputq1);
	__skb_queue_head_init(&n->bc_entry.arrvq);
	skb_queue_head_init(&n->bc_entry.inputq2);
	for (i = 0; i < MAX_BEARERS; i++)
		spin_lock_init(&n->links[i].lock);
	n->state = SELF_DOWN_PEER_LEAVING;
	n->delete_at = jiffies + msecs_to_jiffies(NODE_CLEANUP_AFTER);
	n->signature = INVALID_NODE_SIG;
	n->active_links[0] = INVALID_BEARER_ID;
	n->active_links[1] = INVALID_BEARER_ID;
	if (!tipc_link_bc_create(net, tipc_own_addr(net),
				 addr, U16_MAX,
				 tipc_link_window(tipc_bc_sndlink(net)),
				 n->capabilities,
				 &n->bc_entry.inputq1,
				 &n->bc_entry.namedq,
				 tipc_bc_sndlink(net),
				 &n->bc_entry.link)) {
		pr_warn("Broadcast rcv link creation failed, no memory\n");
		kfree(n);
		n = NULL;
		goto exit;
	}
	tipc_node_get(n);
	timer_setup(&n->timer, tipc_node_timeout, 0);
	n->keepalive_intv = U32_MAX;
	hlist_add_head_rcu(&n->hash, &tn->node_htable[tipc_hashfn(addr)]);
	list_for_each_entry_rcu(temp_node, &tn->node_list, list) {
		if (n->addr < temp_node->addr)
			break;
	}
	list_add_tail_rcu(&n->list, &temp_node->list);
exit:
	spin_unlock_bh(&tn->node_list_lock);
	return n;
}

static void tipc_node_calculate_timer(struct tipc_node *n, struct tipc_link *l)
{
	unsigned long tol = tipc_link_tolerance(l);
	unsigned long intv = ((tol / 4) > 500) ? 500 : tol / 4;

	/* Link with lowest tolerance determines timer interval */
	if (intv < n->keepalive_intv)
		n->keepalive_intv = intv;

	/* Ensure link's abort limit corresponds to current tolerance */
	tipc_link_set_abort_limit(l, tol / n->keepalive_intv);
}

static void tipc_node_delete_from_list(struct tipc_node *node)
{
	list_del_rcu(&node->list);
	hlist_del_rcu(&node->hash);
	tipc_node_put(node);
}

static void tipc_node_delete(struct tipc_node *node)
{
	tipc_node_delete_from_list(node);

	del_timer_sync(&node->timer);
	tipc_node_put(node);
}

void tipc_node_stop(struct net *net)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_node *node, *t_node;

	spin_lock_bh(&tn->node_list_lock);
	list_for_each_entry_safe(node, t_node, &tn->node_list, list)
		tipc_node_delete(node);
	spin_unlock_bh(&tn->node_list_lock);
}

void tipc_node_subscribe(struct net *net, struct list_head *subscr, u32 addr)
{
	struct tipc_node *n;

	if (in_own_node(net, addr))
		return;

	n = tipc_node_find(net, addr);
	if (!n) {
		pr_warn("Node subscribe rejected, unknown node 0x%x\n", addr);
		return;
	}
	tipc_node_write_lock(n);
	list_add_tail(subscr, &n->publ_list);
	tipc_node_write_unlock_fast(n);
	tipc_node_put(n);
}

void tipc_node_unsubscribe(struct net *net, struct list_head *subscr, u32 addr)
{
	struct tipc_node *n;

	if (in_own_node(net, addr))
		return;

	n = tipc_node_find(net, addr);
	if (!n) {
		pr_warn("Node unsubscribe rejected, unknown node 0x%x\n", addr);
		return;
	}
	tipc_node_write_lock(n);
	list_del_init(subscr);
	tipc_node_write_unlock_fast(n);
	tipc_node_put(n);
}

int tipc_node_add_conn(struct net *net, u32 dnode, u32 port, u32 peer_port)
{
	struct tipc_node *node;
	struct tipc_sock_conn *conn;
	int err = 0;

	if (in_own_node(net, dnode))
		return 0;

	node = tipc_node_find(net, dnode);
	if (!node) {
		pr_warn("Connecting sock to node 0x%x failed\n", dnode);
		return -EHOSTUNREACH;
	}
	conn = kmalloc(sizeof(*conn), GFP_ATOMIC);
	if (!conn) {
		err = -EHOSTUNREACH;
		goto exit;
	}
	conn->peer_node = dnode;
	conn->port = port;
	conn->peer_port = peer_port;

	tipc_node_write_lock(node);
	list_add_tail(&conn->list, &node->conn_sks);
	tipc_node_write_unlock(node);
exit:
	tipc_node_put(node);
	return err;
}

void tipc_node_remove_conn(struct net *net, u32 dnode, u32 port)
{
	struct tipc_node *node;
	struct tipc_sock_conn *conn, *safe;

	if (in_own_node(net, dnode))
		return;

	node = tipc_node_find(net, dnode);
	if (!node)
		return;

	tipc_node_write_lock(node);
	list_for_each_entry_safe(conn, safe, &node->conn_sks, list) {
		if (port != conn->port)
			continue;
		list_del(&conn->list);
		kfree(conn);
	}
	tipc_node_write_unlock(node);
	tipc_node_put(node);
}

static void  tipc_node_clear_links(struct tipc_node *node)
{
	int i;

	for (i = 0; i < MAX_BEARERS; i++) {
		struct tipc_link_entry *le = &node->links[i];

		if (le->link) {
			kfree(le->link);
			le->link = NULL;
			node->link_cnt--;
		}
	}
}

/* tipc_node_cleanup - delete nodes that does not
 * have active links for NODE_CLEANUP_AFTER time
 */
static int tipc_node_cleanup(struct tipc_node *peer)
{
	struct tipc_net *tn = tipc_net(peer->net);
	bool deleted = false;

	spin_lock_bh(&tn->node_list_lock);
	tipc_node_write_lock(peer);

	if (!node_is_up(peer) && time_after(jiffies, peer->delete_at)) {
		tipc_node_clear_links(peer);
		tipc_node_delete_from_list(peer);
		deleted = true;
	}
	tipc_node_write_unlock(peer);
	spin_unlock_bh(&tn->node_list_lock);
	return deleted;
}

/* tipc_node_timeout - handle expiration of node timer
 */
static void tipc_node_timeout(struct timer_list *t)
{
	struct tipc_node *n = from_timer(n, t, timer);
	struct tipc_link_entry *le;
	struct sk_buff_head xmitq;
	int remains = n->link_cnt;
	int bearer_id;
	int rc = 0;

	if (!node_is_up(n) && tipc_node_cleanup(n)) {
		/*Removing the reference of Timer*/
		tipc_node_put(n);
		return;
	}

	__skb_queue_head_init(&xmitq);

	for (bearer_id = 0; remains && (bearer_id < MAX_BEARERS); bearer_id++) {
		tipc_node_read_lock(n);
		le = &n->links[bearer_id];
		if (le->link) {
			spin_lock_bh(&le->lock);
			/* Link tolerance may change asynchronously: */
			tipc_node_calculate_timer(n, le->link);
			rc = tipc_link_timeout(le->link, &xmitq);
			spin_unlock_bh(&le->lock);
			remains--;
		}
		tipc_node_read_unlock(n);
		tipc_bearer_xmit(n->net, bearer_id, &xmitq, &le->maddr);
		if (rc & TIPC_LINK_DOWN_EVT)
			tipc_node_link_down(n, bearer_id, false);
	}
	mod_timer(&n->timer, jiffies + msecs_to_jiffies(n->keepalive_intv));
}

/**
 * __tipc_node_link_up - handle addition of link
 * Node lock must be held by caller
 * Link becomes active (alone or shared) or standby, depending on its priority.
 */
static void __tipc_node_link_up(struct tipc_node *n, int bearer_id,
				struct sk_buff_head *xmitq)
{
	int *slot0 = &n->active_links[0];
	int *slot1 = &n->active_links[1];
	struct tipc_link *ol = node_active_link(n, 0);
	struct tipc_link *nl = n->links[bearer_id].link;

	if (!nl || tipc_link_is_up(nl))
		return;

	tipc_link_fsm_evt(nl, LINK_ESTABLISH_EVT);
	if (!tipc_link_is_up(nl))
		return;

	n->working_links++;
	n->action_flags |= TIPC_NOTIFY_LINK_UP;
	n->link_id = tipc_link_id(nl);

	/* Leave room for tunnel header when returning 'mtu' to users: */
	n->links[bearer_id].mtu = tipc_link_mtu(nl) - INT_H_SIZE;

	tipc_bearer_add_dest(n->net, bearer_id, n->addr);
	tipc_bcast_inc_bearer_dst_cnt(n->net, bearer_id);

	pr_debug("Established link <%s> on network plane %c\n",
		 tipc_link_name(nl), tipc_link_plane(nl));

	/* Ensure that a STATE message goes first */
	tipc_link_build_state_msg(nl, xmitq);

	/* First link? => give it both slots */
	if (!ol) {
		*slot0 = bearer_id;
		*slot1 = bearer_id;
		tipc_node_fsm_evt(n, SELF_ESTABL_CONTACT_EVT);
		n->failover_sent = false;
		n->action_flags |= TIPC_NOTIFY_NODE_UP;
		tipc_link_set_active(nl, true);
		tipc_bcast_add_peer(n->net, nl, xmitq);
		return;
	}

	/* Second link => redistribute slots */
	if (tipc_link_prio(nl) > tipc_link_prio(ol)) {
		pr_debug("Old link <%s> becomes standby\n", tipc_link_name(ol));
		*slot0 = bearer_id;
		*slot1 = bearer_id;
		tipc_link_set_active(nl, true);
		tipc_link_set_active(ol, false);
	} else if (tipc_link_prio(nl) == tipc_link_prio(ol)) {
		tipc_link_set_active(nl, true);
		*slot1 = bearer_id;
	} else {
		pr_debug("New link <%s> is standby\n", tipc_link_name(nl));
	}

	/* Prepare synchronization with first link */
	tipc_link_tnl_prepare(ol, nl, SYNCH_MSG, xmitq);
}

/**
 * tipc_node_link_up - handle addition of link
 *
 * Link becomes active (alone or shared) or standby, depending on its priority.
 */
static void tipc_node_link_up(struct tipc_node *n, int bearer_id,
			      struct sk_buff_head *xmitq)
{
	struct tipc_media_addr *maddr;

	tipc_node_write_lock(n);
	__tipc_node_link_up(n, bearer_id, xmitq);
	maddr = &n->links[bearer_id].maddr;
	tipc_bearer_xmit(n->net, bearer_id, xmitq, maddr);
	tipc_node_write_unlock(n);
}

/**
 * __tipc_node_link_down - handle loss of link
 */
static void __tipc_node_link_down(struct tipc_node *n, int *bearer_id,
				  struct sk_buff_head *xmitq,
				  struct tipc_media_addr **maddr)
{
	struct tipc_link_entry *le = &n->links[*bearer_id];
	int *slot0 = &n->active_links[0];
	int *slot1 = &n->active_links[1];
	int i, highest = 0, prio;
	struct tipc_link *l, *_l, *tnl;

	l = n->links[*bearer_id].link;
	if (!l || tipc_link_is_reset(l))
		return;

	n->working_links--;
	n->action_flags |= TIPC_NOTIFY_LINK_DOWN;
	n->link_id = tipc_link_id(l);

	tipc_bearer_remove_dest(n->net, *bearer_id, n->addr);

	pr_debug("Lost link <%s> on network plane %c\n",
		 tipc_link_name(l), tipc_link_plane(l));

	/* Select new active link if any available */
	*slot0 = INVALID_BEARER_ID;
	*slot1 = INVALID_BEARER_ID;
	for (i = 0; i < MAX_BEARERS; i++) {
		_l = n->links[i].link;
		if (!_l || !tipc_link_is_up(_l))
			continue;
		if (_l == l)
			continue;
		prio = tipc_link_prio(_l);
		if (prio < highest)
			continue;
		if (prio > highest) {
			highest = prio;
			*slot0 = i;
			*slot1 = i;
			continue;
		}
		*slot1 = i;
	}

	if (!node_is_up(n)) {
		if (tipc_link_peer_is_down(l))
			tipc_node_fsm_evt(n, PEER_LOST_CONTACT_EVT);
		tipc_node_fsm_evt(n, SELF_LOST_CONTACT_EVT);
		tipc_link_fsm_evt(l, LINK_RESET_EVT);
		tipc_link_reset(l);
		tipc_link_build_reset_msg(l, xmitq);
		*maddr = &n->links[*bearer_id].maddr;
		node_lost_contact(n, &le->inputq);
		tipc_bcast_dec_bearer_dst_cnt(n->net, *bearer_id);
		return;
	}
	tipc_bcast_dec_bearer_dst_cnt(n->net, *bearer_id);

	/* There is still a working link => initiate failover */
	*bearer_id = n->active_links[0];
	tnl = n->links[*bearer_id].link;
	tipc_link_fsm_evt(tnl, LINK_SYNCH_END_EVT);
	tipc_node_fsm_evt(n, NODE_SYNCH_END_EVT);
	n->sync_point = tipc_link_rcv_nxt(tnl) + (U16_MAX / 2 - 1);
	tipc_link_tnl_prepare(l, tnl, FAILOVER_MSG, xmitq);
	tipc_link_reset(l);
	tipc_link_fsm_evt(l, LINK_RESET_EVT);
	tipc_link_fsm_evt(l, LINK_FAILOVER_BEGIN_EVT);
	tipc_node_fsm_evt(n, NODE_FAILOVER_BEGIN_EVT);
	*maddr = &n->links[*bearer_id].maddr;
}

static void tipc_node_link_down(struct tipc_node *n, int bearer_id, bool delete)
{
	struct tipc_link_entry *le = &n->links[bearer_id];
	struct tipc_link *l = le->link;
	struct tipc_media_addr *maddr;
	struct sk_buff_head xmitq;
	int old_bearer_id = bearer_id;

	if (!l)
		return;

	__skb_queue_head_init(&xmitq);

	tipc_node_write_lock(n);
	if (!tipc_link_is_establishing(l)) {
		__tipc_node_link_down(n, &bearer_id, &xmitq, &maddr);
		if (delete) {
			kfree(l);
			le->link = NULL;
			n->link_cnt--;
		}
	} else {
		/* Defuse pending tipc_node_link_up() */
		tipc_link_fsm_evt(l, LINK_RESET_EVT);
	}
	tipc_node_write_unlock(n);
	if (delete)
		tipc_mon_remove_peer(n->net, n->addr, old_bearer_id);
	tipc_bearer_xmit(n->net, bearer_id, &xmitq, maddr);
	tipc_sk_rcv(n->net, &le->inputq);
}

static bool node_is_up(struct tipc_node *n)
{
	return n->active_links[0] != INVALID_BEARER_ID;
}

bool tipc_node_is_up(struct net *net, u32 addr)
{
	struct tipc_node *n;
	bool retval = false;

	if (in_own_node(net, addr))
		return true;

	n = tipc_node_find(net, addr);
	if (!n)
		return false;
	retval = node_is_up(n);
	tipc_node_put(n);
	return retval;
}

static u32 tipc_node_suggest_addr(struct net *net, u32 addr)
{
	struct tipc_node *n;

	addr ^= tipc_net(net)->random;
	while ((n = tipc_node_find(net, addr))) {
		tipc_node_put(n);
		addr++;
	}
	return addr;
}

/* tipc_node_try_addr(): Check if addr can be used by peer, suggest other if not
 * Returns suggested address if any, otherwise 0
 */
u32 tipc_node_try_addr(struct net *net, u8 *id, u32 addr)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_node *n;

	/* Suggest new address if some other peer is using this one */
	n = tipc_node_find(net, addr);
	if (n) {
		if (!memcmp(n->peer_id, id, NODE_ID_LEN))
			addr = 0;
		tipc_node_put(n);
		if (!addr)
			return 0;
		return tipc_node_suggest_addr(net, addr);
	}

	/* Suggest previously used address if peer is known */
	n = tipc_node_find_by_id(net, id);
	if (n) {
		addr = n->addr;
		tipc_node_put(n);
		return addr;
	}

	/* Even this node may be in conflict */
	if (tn->trial_addr == addr)
		return tipc_node_suggest_addr(net, addr);

	return 0;
}

void tipc_node_check_dest(struct net *net, u32 addr,
			  u8 *peer_id, struct tipc_bearer *b,
			  u16 capabilities, u32 signature,
			  struct tipc_media_addr *maddr,
			  bool *respond, bool *dupl_addr)
{
	struct tipc_node *n;
	struct tipc_link *l;
	struct tipc_link_entry *le;
	bool addr_match = false;
	bool sign_match = false;
	bool link_up = false;
	bool accept_addr = false;
	bool reset = true;
	char *if_name;
	unsigned long intv;
	u16 session;

	*dupl_addr = false;
	*respond = false;

	n = tipc_node_create(net, addr, peer_id, capabilities);
	if (!n)
		return;

	tipc_node_write_lock(n);

	le = &n->links[b->identity];

	/* Prepare to validate requesting node's signature and media address */
	l = le->link;
	link_up = l && tipc_link_is_up(l);
	addr_match = l && !memcmp(&le->maddr, maddr, sizeof(*maddr));
	sign_match = (signature == n->signature);

	/* These three flags give us eight permutations: */

	if (sign_match && addr_match && link_up) {
		/* All is fine. Do nothing. */
		reset = false;
	} else if (sign_match && addr_match && !link_up) {
		/* Respond. The link will come up in due time */
		*respond = true;
	} else if (sign_match && !addr_match && link_up) {
		/* Peer has changed i/f address without rebooting.
		 * If so, the link will reset soon, and the next
		 * discovery will be accepted. So we can ignore it.
		 * It may also be an cloned or malicious peer having
		 * chosen the same node address and signature as an
		 * existing one.
		 * Ignore requests until the link goes down, if ever.
		 */
		*dupl_addr = true;
	} else if (sign_match && !addr_match && !link_up) {
		/* Peer link has changed i/f address without rebooting.
		 * It may also be a cloned or malicious peer; we can't
		 * distinguish between the two.
		 * The signature is correct, so we must accept.
		 */
		accept_addr = true;
		*respond = true;
	} else if (!sign_match && addr_match && link_up) {
		/* Peer node rebooted. Two possibilities:
		 *  - Delayed re-discovery; this link endpoint has already
		 *    reset and re-established contact with the peer, before
		 *    receiving a discovery message from that node.
		 *    (The peer happened to receive one from this node first).
		 *  - The peer came back so fast that our side has not
		 *    discovered it yet. Probing from this side will soon
		 *    reset the link, since there can be no working link
		 *    endpoint at the peer end, and the link will re-establish.
		 *  Accept the signature, since it comes from a known peer.
		 */
		n->signature = signature;
	} else if (!sign_match && addr_match && !link_up) {
		/*  The peer node has rebooted.
		 *  Accept signature, since it is a known peer.
		 */
		n->signature = signature;
		*respond = true;
	} else if (!sign_match && !addr_match && link_up) {
		/* Peer rebooted with new address, or a new/duplicate peer.
		 * Ignore until the link goes down, if ever.
		 */
		*dupl_addr = true;
	} else if (!sign_match && !addr_match && !link_up) {
		/* Peer rebooted with new address, or it is a new peer.
		 * Accept signature and address.
		 */
		n->signature = signature;
		accept_addr = true;
		*respond = true;
	}

	if (!accept_addr)
		goto exit;

	/* Now create new link if not already existing */
	if (!l) {
		if (n->link_cnt == 2)
			goto exit;

		if_name = strchr(b->name, ':') + 1;
		get_random_bytes(&session, sizeof(u16));
		if (!tipc_link_create(net, if_name, b->identity, b->tolerance,
				      b->net_plane, b->mtu, b->priority,
				      b->window, session,
				      tipc_own_addr(net), addr, peer_id,
				      n->capabilities,
				      tipc_bc_sndlink(n->net), n->bc_entry.link,
				      &le->inputq,
				      &n->bc_entry.namedq, &l)) {
			*respond = false;
			goto exit;
		}
		tipc_link_reset(l);
		tipc_link_fsm_evt(l, LINK_RESET_EVT);
		if (n->state == NODE_FAILINGOVER)
			tipc_link_fsm_evt(l, LINK_FAILOVER_BEGIN_EVT);
		le->link = l;
		n->link_cnt++;
		tipc_node_calculate_timer(n, l);
		if (n->link_cnt == 1) {
			intv = jiffies + msecs_to_jiffies(n->keepalive_intv);
			if (!mod_timer(&n->timer, intv))
				tipc_node_get(n);
		}
	}
	memcpy(&le->maddr, maddr, sizeof(*maddr));
exit:
	tipc_node_write_unlock(n);
	if (reset && l && !tipc_link_is_reset(l))
		tipc_node_link_down(n, b->identity, false);
	tipc_node_put(n);
}

void tipc_node_delete_links(struct net *net, int bearer_id)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_node *n;

	rcu_read_lock();
	list_for_each_entry_rcu(n, &tn->node_list, list) {
		tipc_node_link_down(n, bearer_id, true);
	}
	rcu_read_unlock();
}

static void tipc_node_reset_links(struct tipc_node *n)
{
	int i;

	pr_warn("Resetting all links to %x\n", n->addr);

	for (i = 0; i < MAX_BEARERS; i++) {
		tipc_node_link_down(n, i, false);
	}
}

/* tipc_node_fsm_evt - node finite state machine
 * Determines when contact is allowed with peer node
 */
static void tipc_node_fsm_evt(struct tipc_node *n, int evt)
{
	int state = n->state;

	switch (state) {
	case SELF_DOWN_PEER_DOWN:
		switch (evt) {
		case SELF_ESTABL_CONTACT_EVT:
			state = SELF_UP_PEER_COMING;
			break;
		case PEER_ESTABL_CONTACT_EVT:
			state = SELF_COMING_PEER_UP;
			break;
		case SELF_LOST_CONTACT_EVT:
		case PEER_LOST_CONTACT_EVT:
			break;
		case NODE_SYNCH_END_EVT:
		case NODE_SYNCH_BEGIN_EVT:
		case NODE_FAILOVER_BEGIN_EVT:
		case NODE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case SELF_UP_PEER_UP:
		switch (evt) {
		case SELF_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_LEAVING;
			break;
		case PEER_LOST_CONTACT_EVT:
			state = SELF_LEAVING_PEER_DOWN;
			break;
		case NODE_SYNCH_BEGIN_EVT:
			state = NODE_SYNCHING;
			break;
		case NODE_FAILOVER_BEGIN_EVT:
			state = NODE_FAILINGOVER;
			break;
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
		case NODE_SYNCH_END_EVT:
		case NODE_FAILOVER_END_EVT:
			break;
		default:
			goto illegal_evt;
		}
		break;
	case SELF_DOWN_PEER_LEAVING:
		switch (evt) {
		case PEER_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_DOWN;
			break;
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
		case SELF_LOST_CONTACT_EVT:
			break;
		case NODE_SYNCH_END_EVT:
		case NODE_SYNCH_BEGIN_EVT:
		case NODE_FAILOVER_BEGIN_EVT:
		case NODE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case SELF_UP_PEER_COMING:
		switch (evt) {
		case PEER_ESTABL_CONTACT_EVT:
			state = SELF_UP_PEER_UP;
			break;
		case SELF_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_DOWN;
			break;
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_LOST_CONTACT_EVT:
		case NODE_SYNCH_END_EVT:
		case NODE_FAILOVER_BEGIN_EVT:
			break;
		case NODE_SYNCH_BEGIN_EVT:
		case NODE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case SELF_COMING_PEER_UP:
		switch (evt) {
		case SELF_ESTABL_CONTACT_EVT:
			state = SELF_UP_PEER_UP;
			break;
		case PEER_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_DOWN;
			break;
		case SELF_LOST_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
			break;
		case NODE_SYNCH_END_EVT:
		case NODE_SYNCH_BEGIN_EVT:
		case NODE_FAILOVER_BEGIN_EVT:
		case NODE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case SELF_LEAVING_PEER_DOWN:
		switch (evt) {
		case SELF_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_DOWN;
			break;
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
		case PEER_LOST_CONTACT_EVT:
			break;
		case NODE_SYNCH_END_EVT:
		case NODE_SYNCH_BEGIN_EVT:
		case NODE_FAILOVER_BEGIN_EVT:
		case NODE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case NODE_FAILINGOVER:
		switch (evt) {
		case SELF_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_LEAVING;
			break;
		case PEER_LOST_CONTACT_EVT:
			state = SELF_LEAVING_PEER_DOWN;
			break;
		case NODE_FAILOVER_END_EVT:
			state = SELF_UP_PEER_UP;
			break;
		case NODE_FAILOVER_BEGIN_EVT:
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
			break;
		case NODE_SYNCH_BEGIN_EVT:
		case NODE_SYNCH_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case NODE_SYNCHING:
		switch (evt) {
		case SELF_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_LEAVING;
			break;
		case PEER_LOST_CONTACT_EVT:
			state = SELF_LEAVING_PEER_DOWN;
			break;
		case NODE_SYNCH_END_EVT:
			state = SELF_UP_PEER_UP;
			break;
		case NODE_FAILOVER_BEGIN_EVT:
			state = NODE_FAILINGOVER;
			break;
		case NODE_SYNCH_BEGIN_EVT:
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
			break;
		case NODE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	default:
		pr_err("Unknown node fsm state %x\n", state);
		break;
	}
	n->state = state;
	return;

illegal_evt:
	pr_err("Illegal node fsm evt %x in state %x\n", evt, state);
}

static void node_lost_contact(struct tipc_node *n,
			      struct sk_buff_head *inputq)
{
	struct tipc_sock_conn *conn, *safe;
	struct tipc_link *l;
	struct list_head *conns = &n->conn_sks;
	struct sk_buff *skb;
	uint i;

	pr_debug("Lost contact with %x\n", n->addr);
	n->delete_at = jiffies + msecs_to_jiffies(NODE_CLEANUP_AFTER);

	/* Clean up broadcast state */
	tipc_bcast_remove_peer(n->net, n->bc_entry.link);

	/* Abort any ongoing link failover */
	for (i = 0; i < MAX_BEARERS; i++) {
		l = n->links[i].link;
		if (l)
			tipc_link_fsm_evt(l, LINK_FAILOVER_END_EVT);
	}

	/* Notify publications from this node */
	n->action_flags |= TIPC_NOTIFY_NODE_DOWN;

	/* Notify sockets connected to node */
	list_for_each_entry_safe(conn, safe, conns, list) {
		skb = tipc_msg_create(TIPC_CRITICAL_IMPORTANCE, TIPC_CONN_MSG,
				      SHORT_H_SIZE, 0, tipc_own_addr(n->net),
				      conn->peer_node, conn->port,
				      conn->peer_port, TIPC_ERR_NO_NODE);
		if (likely(skb))
			skb_queue_tail(inputq, skb);
		list_del(&conn->list);
		kfree(conn);
	}
}

/**
 * tipc_node_get_linkname - get the name of a link
 *
 * @bearer_id: id of the bearer
 * @node: peer node address
 * @linkname: link name output buffer
 *
 * Returns 0 on success
 */
int tipc_node_get_linkname(struct net *net, u32 bearer_id, u32 addr,
			   char *linkname, size_t len)
{
	struct tipc_link *link;
	int err = -EINVAL;
	struct tipc_node *node = tipc_node_find(net, addr);

	if (!node)
		return err;

	if (bearer_id >= MAX_BEARERS)
		goto exit;

	tipc_node_read_lock(node);
	link = node->links[bearer_id].link;
	if (link) {
		strncpy(linkname, tipc_link_name(link), len);
		err = 0;
	}
	tipc_node_read_unlock(node);
exit:
	tipc_node_put(node);
	return err;
}

/* Caller should hold node lock for the passed node */
static int __tipc_nl_add_node(struct tipc_nl_msg *msg, struct tipc_node *node)
{
	void *hdr;
	struct nlattr *attrs;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  NLM_F_MULTI, TIPC_NL_NODE_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_NODE);
	if (!attrs)
		goto msg_full;

	if (nla_put_u32(msg->skb, TIPC_NLA_NODE_ADDR, node->addr))
		goto attr_msg_full;
	if (node_is_up(node))
		if (nla_put_flag(msg->skb, TIPC_NLA_NODE_UP))
			goto attr_msg_full;

	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

/**
 * tipc_node_xmit() is the general link level function for message sending
 * @net: the applicable net namespace
 * @list: chain of buffers containing message
 * @dnode: address of destination node
 * @selector: a number used for deterministic link selection
 * Consumes the buffer chain.
 * Returns 0 if success, otherwise: -ELINKCONG,-EHOSTUNREACH,-EMSGSIZE,-ENOBUF
 */
int tipc_node_xmit(struct net *net, struct sk_buff_head *list,
		   u32 dnode, int selector)
{
	struct tipc_link_entry *le = NULL;
	struct tipc_node *n;
	struct sk_buff_head xmitq;
	int bearer_id;
	int rc;

	if (in_own_node(net, dnode)) {
		tipc_sk_rcv(net, list);
		return 0;
	}

	n = tipc_node_find(net, dnode);
	if (unlikely(!n)) {
		skb_queue_purge(list);
		return -EHOSTUNREACH;
	}

	tipc_node_read_lock(n);
	bearer_id = n->active_links[selector & 1];
	if (unlikely(bearer_id == INVALID_BEARER_ID)) {
		tipc_node_read_unlock(n);
		tipc_node_put(n);
		skb_queue_purge(list);
		return -EHOSTUNREACH;
	}

	__skb_queue_head_init(&xmitq);
	le = &n->links[bearer_id];
	spin_lock_bh(&le->lock);
	rc = tipc_link_xmit(le->link, list, &xmitq);
	spin_unlock_bh(&le->lock);
	tipc_node_read_unlock(n);

	if (unlikely(rc == -ENOBUFS))
		tipc_node_link_down(n, bearer_id, false);
	else
		tipc_bearer_xmit(net, bearer_id, &xmitq, &le->maddr);

	tipc_node_put(n);

	return rc;
}

/* tipc_node_xmit_skb(): send single buffer to destination
 * Buffers sent via this functon are generally TIPC_SYSTEM_IMPORTANCE
 * messages, which will not be rejected
 * The only exception is datagram messages rerouted after secondary
 * lookup, which are rare and safe to dispose of anyway.
 */
int tipc_node_xmit_skb(struct net *net, struct sk_buff *skb, u32 dnode,
		       u32 selector)
{
	struct sk_buff_head head;

	skb_queue_head_init(&head);
	__skb_queue_tail(&head, skb);
	tipc_node_xmit(net, &head, dnode, selector);
	return 0;
}

/* tipc_node_distr_xmit(): send single buffer msgs to individual destinations
 * Note: this is only for SYSTEM_IMPORTANCE messages, which cannot be rejected
 */
int tipc_node_distr_xmit(struct net *net, struct sk_buff_head *xmitq)
{
	struct sk_buff *skb;
	u32 selector, dnode;

	while ((skb = __skb_dequeue(xmitq))) {
		selector = msg_origport(buf_msg(skb));
		dnode = msg_destnode(buf_msg(skb));
		tipc_node_xmit_skb(net, skb, dnode, selector);
	}
	return 0;
}

void tipc_node_broadcast(struct net *net, struct sk_buff *skb)
{
	struct sk_buff *txskb;
	struct tipc_node *n;
	u32 dst;

	rcu_read_lock();
	list_for_each_entry_rcu(n, tipc_nodes(net), list) {
		dst = n->addr;
		if (in_own_node(net, dst))
			continue;
		if (!node_is_up(n))
			continue;
		txskb = pskb_copy(skb, GFP_ATOMIC);
		if (!txskb)
			break;
		msg_set_destnode(buf_msg(txskb), dst);
		tipc_node_xmit_skb(net, txskb, dst, 0);
	}
	rcu_read_unlock();

	kfree_skb(skb);
}

static void tipc_node_mcast_rcv(struct tipc_node *n)
{
	struct tipc_bclink_entry *be = &n->bc_entry;

	/* 'arrvq' is under inputq2's lock protection */
	spin_lock_bh(&be->inputq2.lock);
	spin_lock_bh(&be->inputq1.lock);
	skb_queue_splice_tail_init(&be->inputq1, &be->arrvq);
	spin_unlock_bh(&be->inputq1.lock);
	spin_unlock_bh(&be->inputq2.lock);
	tipc_sk_mcast_rcv(n->net, &be->arrvq, &be->inputq2);
}

static void tipc_node_bc_sync_rcv(struct tipc_node *n, struct tipc_msg *hdr,
				  int bearer_id, struct sk_buff_head *xmitq)
{
	struct tipc_link *ucl;
	int rc;

	rc = tipc_bcast_sync_rcv(n->net, n->bc_entry.link, hdr);

	if (rc & TIPC_LINK_DOWN_EVT) {
		tipc_node_reset_links(n);
		return;
	}

	if (!(rc & TIPC_LINK_SND_STATE))
		return;

	/* If probe message, a STATE response will be sent anyway */
	if (msg_probe(hdr))
		return;

	/* Produce a STATE message carrying broadcast NACK */
	tipc_node_read_lock(n);
	ucl = n->links[bearer_id].link;
	if (ucl)
		tipc_link_build_state_msg(ucl, xmitq);
	tipc_node_read_unlock(n);
}

/**
 * tipc_node_bc_rcv - process TIPC broadcast packet arriving from off-node
 * @net: the applicable net namespace
 * @skb: TIPC packet
 * @bearer_id: id of bearer message arrived on
 *
 * Invoked with no locks held.
 */
static void tipc_node_bc_rcv(struct net *net, struct sk_buff *skb, int bearer_id)
{
	int rc;
	struct sk_buff_head xmitq;
	struct tipc_bclink_entry *be;
	struct tipc_link_entry *le;
	struct tipc_msg *hdr = buf_msg(skb);
	int usr = msg_user(hdr);
	u32 dnode = msg_destnode(hdr);
	struct tipc_node *n;

	__skb_queue_head_init(&xmitq);

	/* If NACK for other node, let rcv link for that node peek into it */
	if ((usr == BCAST_PROTOCOL) && (dnode != tipc_own_addr(net)))
		n = tipc_node_find(net, dnode);
	else
		n = tipc_node_find(net, msg_prevnode(hdr));
	if (!n) {
		kfree_skb(skb);
		return;
	}
	be = &n->bc_entry;
	le = &n->links[bearer_id];

	rc = tipc_bcast_rcv(net, be->link, skb);

	/* Broadcast ACKs are sent on a unicast link */
	if (rc & TIPC_LINK_SND_STATE) {
		tipc_node_read_lock(n);
		tipc_link_build_state_msg(le->link, &xmitq);
		tipc_node_read_unlock(n);
	}

	if (!skb_queue_empty(&xmitq))
		tipc_bearer_xmit(net, bearer_id, &xmitq, &le->maddr);

	if (!skb_queue_empty(&be->inputq1))
		tipc_node_mcast_rcv(n);

	/* If reassembly or retransmission failure => reset all links to peer */
	if (rc & TIPC_LINK_DOWN_EVT)
		tipc_node_reset_links(n);

	tipc_node_put(n);
}

/**
 * tipc_node_check_state - check and if necessary update node state
 * @skb: TIPC packet
 * @bearer_id: identity of bearer delivering the packet
 * Returns true if state and msg are ok, otherwise false
 */
static bool tipc_node_check_state(struct tipc_node *n, struct sk_buff *skb,
				  int bearer_id, struct sk_buff_head *xmitq)
{
	struct tipc_msg *hdr = buf_msg(skb);
	int usr = msg_user(hdr);
	int mtyp = msg_type(hdr);
	u16 oseqno = msg_seqno(hdr);
	u16 iseqno = msg_seqno(msg_get_wrapped(hdr));
	u16 exp_pkts = msg_msgcnt(hdr);
	u16 rcv_nxt, syncpt, dlv_nxt, inputq_len;
	int state = n->state;
	struct tipc_link *l, *tnl, *pl = NULL;
	struct tipc_media_addr *maddr;
	int pb_id;

	l = n->links[bearer_id].link;
	if (!l)
		return false;
	rcv_nxt = tipc_link_rcv_nxt(l);


	if (likely((state == SELF_UP_PEER_UP) && (usr != TUNNEL_PROTOCOL)))
		return true;

	/* Find parallel link, if any */
	for (pb_id = 0; pb_id < MAX_BEARERS; pb_id++) {
		if ((pb_id != bearer_id) && n->links[pb_id].link) {
			pl = n->links[pb_id].link;
			break;
		}
	}

	if (!tipc_link_validate_msg(l, hdr))
		return false;

	/* Check and update node accesibility if applicable */
	if (state == SELF_UP_PEER_COMING) {
		if (!tipc_link_is_up(l))
			return true;
		if (!msg_peer_link_is_up(hdr))
			return true;
		tipc_node_fsm_evt(n, PEER_ESTABL_CONTACT_EVT);
	}

	if (state == SELF_DOWN_PEER_LEAVING) {
		if (msg_peer_node_is_up(hdr))
			return false;
		tipc_node_fsm_evt(n, PEER_LOST_CONTACT_EVT);
		return true;
	}

	if (state == SELF_LEAVING_PEER_DOWN)
		return false;

	/* Ignore duplicate packets */
	if ((usr != LINK_PROTOCOL) && less(oseqno, rcv_nxt))
		return true;

	/* Initiate or update failover mode if applicable */
	if ((usr == TUNNEL_PROTOCOL) && (mtyp == FAILOVER_MSG)) {
		syncpt = oseqno + exp_pkts - 1;
		if (pl && tipc_link_is_up(pl)) {
			__tipc_node_link_down(n, &pb_id, xmitq, &maddr);
			tipc_skb_queue_splice_tail_init(tipc_link_inputq(pl),
							tipc_link_inputq(l));
		}
		/* If parallel link was already down, and this happened before
		 * the tunnel link came up, FAILOVER was never sent. Ensure that
		 * FAILOVER is sent to get peer out of NODE_FAILINGOVER state.
		 */
		if (n->state != NODE_FAILINGOVER && !n->failover_sent) {
			tipc_link_create_dummy_tnl_msg(l, xmitq);
			n->failover_sent = true;
		}
		/* If pkts arrive out of order, use lowest calculated syncpt */
		if (less(syncpt, n->sync_point))
			n->sync_point = syncpt;
	}

	/* Open parallel link when tunnel link reaches synch point */
	if ((n->state == NODE_FAILINGOVER) && tipc_link_is_up(l)) {
		if (!more(rcv_nxt, n->sync_point))
			return true;
		tipc_node_fsm_evt(n, NODE_FAILOVER_END_EVT);
		if (pl)
			tipc_link_fsm_evt(pl, LINK_FAILOVER_END_EVT);
		return true;
	}

	/* No synching needed if only one link */
	if (!pl || !tipc_link_is_up(pl))
		return true;

	/* Initiate synch mode if applicable */
	if ((usr == TUNNEL_PROTOCOL) && (mtyp == SYNCH_MSG) && (oseqno == 1)) {
		syncpt = iseqno + exp_pkts - 1;
		if (!tipc_link_is_up(l))
			__tipc_node_link_up(n, bearer_id, xmitq);
		if (n->state == SELF_UP_PEER_UP) {
			n->sync_point = syncpt;
			tipc_link_fsm_evt(l, LINK_SYNCH_BEGIN_EVT);
			tipc_node_fsm_evt(n, NODE_SYNCH_BEGIN_EVT);
		}
	}

	/* Open tunnel link when parallel link reaches synch point */
	if (n->state == NODE_SYNCHING) {
		if (tipc_link_is_synching(l)) {
			tnl = l;
		} else {
			tnl = pl;
			pl = l;
		}
		inputq_len = skb_queue_len(tipc_link_inputq(pl));
		dlv_nxt = tipc_link_rcv_nxt(pl) - inputq_len;
		if (more(dlv_nxt, n->sync_point)) {
			tipc_link_fsm_evt(tnl, LINK_SYNCH_END_EVT);
			tipc_node_fsm_evt(n, NODE_SYNCH_END_EVT);
			return true;
		}
		if (l == pl)
			return true;
		if ((usr == TUNNEL_PROTOCOL) && (mtyp == SYNCH_MSG))
			return true;
		if (usr == LINK_PROTOCOL)
			return true;
		return false;
	}
	return true;
}

/**
 * tipc_rcv - process TIPC packets/messages arriving from off-node
 * @net: the applicable net namespace
 * @skb: TIPC packet
 * @bearer: pointer to bearer message arrived on
 *
 * Invoked with no locks held. Bearer pointer must point to a valid bearer
 * structure (i.e. cannot be NULL), but bearer can be inactive.
 */
void tipc_rcv(struct net *net, struct sk_buff *skb, struct tipc_bearer *b)
{
	struct sk_buff_head xmitq;
	struct tipc_node *n;
	struct tipc_msg *hdr;
	int bearer_id = b->identity;
	struct tipc_link_entry *le;
	u32 self = tipc_own_addr(net);
	int usr, rc = 0;
	u16 bc_ack;

	__skb_queue_head_init(&xmitq);

	/* Ensure message is well-formed before touching the header */
	if (unlikely(!tipc_msg_validate(&skb)))
		goto discard;
	hdr = buf_msg(skb);
	usr = msg_user(hdr);
	bc_ack = msg_bcast_ack(hdr);

	/* Handle arrival of discovery or broadcast packet */
	if (unlikely(msg_non_seq(hdr))) {
		if (unlikely(usr == LINK_CONFIG))
			return tipc_disc_rcv(net, skb, b);
		else
			return tipc_node_bc_rcv(net, skb, bearer_id);
	}

	/* Discard unicast link messages destined for another node */
	if (unlikely(!msg_short(hdr) && (msg_destnode(hdr) != self)))
		goto discard;

	/* Locate neighboring node that sent packet */
	n = tipc_node_find(net, msg_prevnode(hdr));
	if (unlikely(!n))
		goto discard;
	le = &n->links[bearer_id];

	/* Ensure broadcast reception is in synch with peer's send state */
	if (unlikely(usr == LINK_PROTOCOL))
		tipc_node_bc_sync_rcv(n, hdr, bearer_id, &xmitq);
	else if (unlikely(tipc_link_acked(n->bc_entry.link) != bc_ack))
		tipc_bcast_ack_rcv(net, n->bc_entry.link, hdr);

	/* Receive packet directly if conditions permit */
	tipc_node_read_lock(n);
	if (likely((n->state == SELF_UP_PEER_UP) && (usr != TUNNEL_PROTOCOL))) {
		spin_lock_bh(&le->lock);
		if (le->link) {
			rc = tipc_link_rcv(le->link, skb, &xmitq);
			skb = NULL;
		}
		spin_unlock_bh(&le->lock);
	}
	tipc_node_read_unlock(n);

	/* Check/update node state before receiving */
	if (unlikely(skb)) {
		if (unlikely(skb_linearize(skb)))
			goto discard;
		tipc_node_write_lock(n);
		if (tipc_node_check_state(n, skb, bearer_id, &xmitq)) {
			if (le->link) {
				rc = tipc_link_rcv(le->link, skb, &xmitq);
				skb = NULL;
			}
		}
		tipc_node_write_unlock(n);
	}

	if (unlikely(rc & TIPC_LINK_UP_EVT))
		tipc_node_link_up(n, bearer_id, &xmitq);

	if (unlikely(rc & TIPC_LINK_DOWN_EVT))
		tipc_node_link_down(n, bearer_id, false);

	if (unlikely(!skb_queue_empty(&n->bc_entry.namedq)))
		tipc_named_rcv(net, &n->bc_entry.namedq);

	if (unlikely(!skb_queue_empty(&n->bc_entry.inputq1)))
		tipc_node_mcast_rcv(n);

	if (!skb_queue_empty(&le->inputq))
		tipc_sk_rcv(net, &le->inputq);

	if (!skb_queue_empty(&xmitq))
		tipc_bearer_xmit(net, bearer_id, &xmitq, &le->maddr);

	tipc_node_put(n);
discard:
	kfree_skb(skb);
}

void tipc_node_apply_property(struct net *net, struct tipc_bearer *b,
			      int prop)
{
	struct tipc_net *tn = tipc_net(net);
	int bearer_id = b->identity;
	struct sk_buff_head xmitq;
	struct tipc_link_entry *e;
	struct tipc_node *n;

	__skb_queue_head_init(&xmitq);

	rcu_read_lock();

	list_for_each_entry_rcu(n, &tn->node_list, list) {
		tipc_node_write_lock(n);
		e = &n->links[bearer_id];
		if (e->link) {
			if (prop == TIPC_NLA_PROP_TOL)
				tipc_link_set_tolerance(e->link, b->tolerance,
							&xmitq);
			else if (prop == TIPC_NLA_PROP_MTU)
				tipc_link_set_mtu(e->link, b->mtu);
		}
		tipc_node_write_unlock(n);
		tipc_bearer_xmit(net, bearer_id, &xmitq, &e->maddr);
	}

	rcu_read_unlock();
}

int tipc_nl_peer_rm(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct nlattr *attrs[TIPC_NLA_NET_MAX + 1];
	struct tipc_node *peer;
	u32 addr;
	int err;

	/* We identify the peer by its net */
	if (!info->attrs[TIPC_NLA_NET])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_NET_MAX,
			       info->attrs[TIPC_NLA_NET], tipc_nl_net_policy,
			       info->extack);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_NET_ADDR])
		return -EINVAL;

	addr = nla_get_u32(attrs[TIPC_NLA_NET_ADDR]);

	if (in_own_node(net, addr))
		return -ENOTSUPP;

	spin_lock_bh(&tn->node_list_lock);
	peer = tipc_node_find(net, addr);
	if (!peer) {
		spin_unlock_bh(&tn->node_list_lock);
		return -ENXIO;
	}

	tipc_node_write_lock(peer);
	if (peer->state != SELF_DOWN_PEER_DOWN &&
	    peer->state != SELF_DOWN_PEER_LEAVING) {
		tipc_node_write_unlock(peer);
		err = -EBUSY;
		goto err_out;
	}

	tipc_node_clear_links(peer);
	tipc_node_write_unlock(peer);
	tipc_node_delete(peer);

	err = 0;
err_out:
	tipc_node_put(peer);
	spin_unlock_bh(&tn->node_list_lock);

	return err;
}

int tipc_nl_node_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int err;
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	int done = cb->args[0];
	int last_addr = cb->args[1];
	struct tipc_node *node;
	struct tipc_nl_msg msg;

	if (done)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rcu_read_lock();
	if (last_addr) {
		node = tipc_node_find(net, last_addr);
		if (!node) {
			rcu_read_unlock();
			/* We never set seq or call nl_dump_check_consistent()
			 * this means that setting prev_seq here will cause the
			 * consistence check to fail in the netlink callback
			 * handler. Resulting in the NLMSG_DONE message having
			 * the NLM_F_DUMP_INTR flag set if the node state
			 * changed while we released the lock.
			 */
			cb->prev_seq = 1;
			return -EPIPE;
		}
		tipc_node_put(node);
	}

	list_for_each_entry_rcu(node, &tn->node_list, list) {
		if (last_addr) {
			if (node->addr == last_addr)
				last_addr = 0;
			else
				continue;
		}

		tipc_node_read_lock(node);
		err = __tipc_nl_add_node(&msg, node);
		if (err) {
			last_addr = node->addr;
			tipc_node_read_unlock(node);
			goto out;
		}

		tipc_node_read_unlock(node);
	}
	done = 1;
out:
	cb->args[0] = done;
	cb->args[1] = last_addr;
	rcu_read_unlock();

	return skb->len;
}

/* tipc_node_find_by_name - locate owner node of link by link's name
 * @net: the applicable net namespace
 * @name: pointer to link name string
 * @bearer_id: pointer to index in 'node->links' array where the link was found.
 *
 * Returns pointer to node owning the link, or 0 if no matching link is found.
 */
static struct tipc_node *tipc_node_find_by_name(struct net *net,
						const char *link_name,
						unsigned int *bearer_id)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *l;
	struct tipc_node *n;
	struct tipc_node *found_node = NULL;
	int i;

	*bearer_id = 0;
	rcu_read_lock();
	list_for_each_entry_rcu(n, &tn->node_list, list) {
		tipc_node_read_lock(n);
		for (i = 0; i < MAX_BEARERS; i++) {
			l = n->links[i].link;
			if (l && !strcmp(tipc_link_name(l), link_name)) {
				*bearer_id = i;
				found_node = n;
				break;
			}
		}
		tipc_node_read_unlock(n);
		if (found_node)
			break;
	}
	rcu_read_unlock();

	return found_node;
}

int tipc_nl_node_set_link(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	int res = 0;
	int bearer_id;
	char *name;
	struct tipc_link *link;
	struct tipc_node *node;
	struct sk_buff_head xmitq;
	struct nlattr *attrs[TIPC_NLA_LINK_MAX + 1];
	struct net *net = sock_net(skb->sk);

	__skb_queue_head_init(&xmitq);

	if (!info->attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_LINK_MAX,
			       info->attrs[TIPC_NLA_LINK],
			       tipc_nl_link_policy, info->extack);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;

	name = nla_data(attrs[TIPC_NLA_LINK_NAME]);

	if (strcmp(name, tipc_bclink_name) == 0)
		return tipc_nl_bc_link_set(net, attrs);

	node = tipc_node_find_by_name(net, name, &bearer_id);
	if (!node)
		return -EINVAL;

	tipc_node_read_lock(node);

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
			tipc_link_set_tolerance(link, tol, &xmitq);
		}
		if (props[TIPC_NLA_PROP_PRIO]) {
			u32 prio;

			prio = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
			tipc_link_set_prio(link, prio, &xmitq);
		}
		if (props[TIPC_NLA_PROP_WIN]) {
			u32 win;

			win = nla_get_u32(props[TIPC_NLA_PROP_WIN]);
			tipc_link_set_queue_limits(link, win);
		}
	}

out:
	tipc_node_read_unlock(node);
	tipc_bearer_xmit(net, bearer_id, &xmitq, &node->links[bearer_id].maddr);
	return res;
}

int tipc_nl_node_get_link(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct nlattr *attrs[TIPC_NLA_LINK_MAX + 1];
	struct tipc_nl_msg msg;
	char *name;
	int err;

	msg.portid = info->snd_portid;
	msg.seq = info->snd_seq;

	if (!info->attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_LINK_MAX,
			       info->attrs[TIPC_NLA_LINK],
			       tipc_nl_link_policy, info->extack);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;

	name = nla_data(attrs[TIPC_NLA_LINK_NAME]);

	msg.skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg.skb)
		return -ENOMEM;

	if (strcmp(name, tipc_bclink_name) == 0) {
		err = tipc_nl_add_bc_link(net, &msg);
		if (err)
			goto err_free;
	} else {
		int bearer_id;
		struct tipc_node *node;
		struct tipc_link *link;

		node = tipc_node_find_by_name(net, name, &bearer_id);
		if (!node) {
			err = -EINVAL;
			goto err_free;
		}

		tipc_node_read_lock(node);
		link = node->links[bearer_id].link;
		if (!link) {
			tipc_node_read_unlock(node);
			err = -EINVAL;
			goto err_free;
		}

		err = __tipc_nl_add_link(net, &msg, link, 0);
		tipc_node_read_unlock(node);
		if (err)
			goto err_free;
	}

	return genlmsg_reply(msg.skb, info);

err_free:
	nlmsg_free(msg.skb);
	return err;
}

int tipc_nl_node_reset_link_stats(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *link_name;
	unsigned int bearer_id;
	struct tipc_link *link;
	struct tipc_node *node;
	struct nlattr *attrs[TIPC_NLA_LINK_MAX + 1];
	struct net *net = sock_net(skb->sk);
	struct tipc_link_entry *le;

	if (!info->attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_LINK_MAX,
			       info->attrs[TIPC_NLA_LINK],
			       tipc_nl_link_policy, info->extack);
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

	node = tipc_node_find_by_name(net, link_name, &bearer_id);
	if (!node)
		return -EINVAL;

	le = &node->links[bearer_id];
	tipc_node_read_lock(node);
	spin_lock_bh(&le->lock);
	link = node->links[bearer_id].link;
	if (!link) {
		spin_unlock_bh(&le->lock);
		tipc_node_read_unlock(node);
		return -EINVAL;
	}
	tipc_link_reset_stats(link);
	spin_unlock_bh(&le->lock);
	tipc_node_read_unlock(node);
	return 0;
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

int tipc_nl_node_dump_link(struct sk_buff *skb, struct netlink_callback *cb)
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
			tipc_node_read_lock(node);
			err = __tipc_nl_add_node_links(net, &msg, node,
						       &prev_link);
			tipc_node_read_unlock(node);
			if (err)
				goto out;

			prev_node = node->addr;
		}
	} else {
		err = tipc_nl_add_bc_link(net, &msg);
		if (err)
			goto out;

		list_for_each_entry_rcu(node, &tn->node_list, list) {
			tipc_node_read_lock(node);
			err = __tipc_nl_add_node_links(net, &msg, node,
						       &prev_link);
			tipc_node_read_unlock(node);
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

int tipc_nl_node_set_monitor(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[TIPC_NLA_MON_MAX + 1];
	struct net *net = sock_net(skb->sk);
	int err;

	if (!info->attrs[TIPC_NLA_MON])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_MON_MAX,
			       info->attrs[TIPC_NLA_MON],
			       tipc_nl_monitor_policy, info->extack);
	if (err)
		return err;

	if (attrs[TIPC_NLA_MON_ACTIVATION_THRESHOLD]) {
		u32 val;

		val = nla_get_u32(attrs[TIPC_NLA_MON_ACTIVATION_THRESHOLD]);
		err = tipc_nl_monitor_set_threshold(net, val);
		if (err)
			return err;
	}

	return 0;
}

static int __tipc_nl_add_monitor_prop(struct net *net, struct tipc_nl_msg *msg)
{
	struct nlattr *attrs;
	void *hdr;
	u32 val;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  0, TIPC_NL_MON_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_MON);
	if (!attrs)
		goto msg_full;

	val = tipc_nl_monitor_get_threshold(net);

	if (nla_put_u32(msg->skb, TIPC_NLA_MON_ACTIVATION_THRESHOLD, val))
		goto attr_msg_full;

	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

int tipc_nl_node_get_monitor(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = sock_net(skb->sk);
	struct tipc_nl_msg msg;
	int err;

	msg.skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg.skb)
		return -ENOMEM;
	msg.portid = info->snd_portid;
	msg.seq = info->snd_seq;

	err = __tipc_nl_add_monitor_prop(net, &msg);
	if (err) {
		nlmsg_free(msg.skb);
		return err;
	}

	return genlmsg_reply(msg.skb, info);
}

int tipc_nl_node_dump_monitor(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	u32 prev_bearer = cb->args[0];
	struct tipc_nl_msg msg;
	int bearer_id;
	int err;

	if (prev_bearer == MAX_BEARERS)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rtnl_lock();
	for (bearer_id = prev_bearer; bearer_id < MAX_BEARERS; bearer_id++) {
		err = __tipc_nl_add_monitor(net, &msg, bearer_id);
		if (err)
			break;
	}
	rtnl_unlock();
	cb->args[0] = bearer_id;

	return skb->len;
}

int tipc_nl_node_dump_monitor_peer(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	u32 prev_node = cb->args[1];
	u32 bearer_id = cb->args[2];
	int done = cb->args[0];
	struct tipc_nl_msg msg;
	int err;

	if (!prev_node) {
		struct nlattr **attrs;
		struct nlattr *mon[TIPC_NLA_MON_MAX + 1];

		err = tipc_nlmsg_parse(cb->nlh, &attrs);
		if (err)
			return err;

		if (!attrs[TIPC_NLA_MON])
			return -EINVAL;

		err = nla_parse_nested(mon, TIPC_NLA_MON_MAX,
				       attrs[TIPC_NLA_MON],
				       tipc_nl_monitor_policy, NULL);
		if (err)
			return err;

		if (!mon[TIPC_NLA_MON_REF])
			return -EINVAL;

		bearer_id = nla_get_u32(mon[TIPC_NLA_MON_REF]);

		if (bearer_id >= MAX_BEARERS)
			return -EINVAL;
	}

	if (done)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rtnl_lock();
	err = tipc_nl_add_monitor_peer(net, &msg, bearer_id, &prev_node);
	if (!err)
		done = 1;

	rtnl_unlock();
	cb->args[0] = done;
	cb->args[1] = prev_node;
	cb->args[2] = bearer_id;

	return skb->len;
}

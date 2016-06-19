/*
 * net/tipc/name_distr.c: TIPC name distribution code
 *
 * Copyright (c) 2000-2006, 2014, Ericsson AB
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

#include "core.h"
#include "link.h"
#include "name_distr.h"

int sysctl_tipc_named_timeout __read_mostly = 2000;

struct distr_queue_item {
	struct distr_item i;
	u32 dtype;
	u32 node;
	unsigned long expires;
	struct list_head next;
};

/**
 * publ_to_item - add publication info to a publication message
 */
static void publ_to_item(struct distr_item *i, struct publication *p)
{
	i->type = htonl(p->type);
	i->lower = htonl(p->lower);
	i->upper = htonl(p->upper);
	i->ref = htonl(p->ref);
	i->key = htonl(p->key);
}

/**
 * named_prepare_buf - allocate & initialize a publication message
 */
static struct sk_buff *named_prepare_buf(struct net *net, u32 type, u32 size,
					 u32 dest)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct sk_buff *buf = tipc_buf_acquire(INT_H_SIZE + size);
	struct tipc_msg *msg;

	if (buf != NULL) {
		msg = buf_msg(buf);
		tipc_msg_init(tn->own_addr, msg, NAME_DISTRIBUTOR, type,
			      INT_H_SIZE, dest);
		msg_set_size(msg, INT_H_SIZE + size);
	}
	return buf;
}

/**
 * tipc_named_publish - tell other nodes about a new publication by this node
 */
struct sk_buff *tipc_named_publish(struct net *net, struct publication *publ)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct sk_buff *buf;
	struct distr_item *item;

	list_add_tail_rcu(&publ->local_list,
			  &tn->nametbl->publ_list[publ->scope]);

	if (publ->scope == TIPC_NODE_SCOPE)
		return NULL;

	buf = named_prepare_buf(net, PUBLICATION, ITEM_SIZE, 0);
	if (!buf) {
		pr_warn("Publication distribution failure\n");
		return NULL;
	}

	item = (struct distr_item *)msg_data(buf_msg(buf));
	publ_to_item(item, publ);
	return buf;
}

/**
 * tipc_named_withdraw - tell other nodes about a withdrawn publication by this node
 */
struct sk_buff *tipc_named_withdraw(struct net *net, struct publication *publ)
{
	struct sk_buff *buf;
	struct distr_item *item;

	list_del(&publ->local_list);

	if (publ->scope == TIPC_NODE_SCOPE)
		return NULL;

	buf = named_prepare_buf(net, WITHDRAWAL, ITEM_SIZE, 0);
	if (!buf) {
		pr_warn("Withdrawal distribution failure\n");
		return NULL;
	}

	item = (struct distr_item *)msg_data(buf_msg(buf));
	publ_to_item(item, publ);
	return buf;
}

/**
 * named_distribute - prepare name info for bulk distribution to another node
 * @list: list of messages (buffers) to be returned from this function
 * @dnode: node to be updated
 * @pls: linked list of publication items to be packed into buffer chain
 */
static void named_distribute(struct net *net, struct sk_buff_head *list,
			     u32 dnode, struct list_head *pls)
{
	struct publication *publ;
	struct sk_buff *skb = NULL;
	struct distr_item *item = NULL;
	uint msg_dsz = (tipc_node_get_mtu(net, dnode, 0) / ITEM_SIZE) *
			ITEM_SIZE;
	uint msg_rem = msg_dsz;

	list_for_each_entry(publ, pls, local_list) {
		/* Prepare next buffer: */
		if (!skb) {
			skb = named_prepare_buf(net, PUBLICATION, msg_rem,
						dnode);
			if (!skb) {
				pr_warn("Bulk publication failure\n");
				return;
			}
			item = (struct distr_item *)msg_data(buf_msg(skb));
		}

		/* Pack publication into message: */
		publ_to_item(item, publ);
		item++;
		msg_rem -= ITEM_SIZE;

		/* Append full buffer to list: */
		if (!msg_rem) {
			__skb_queue_tail(list, skb);
			skb = NULL;
			msg_rem = msg_dsz;
		}
	}
	if (skb) {
		msg_set_size(buf_msg(skb), INT_H_SIZE + (msg_dsz - msg_rem));
		skb_trim(skb, INT_H_SIZE + (msg_dsz - msg_rem));
		__skb_queue_tail(list, skb);
	}
}

/**
 * tipc_named_node_up - tell specified node about all publications by this node
 */
void tipc_named_node_up(struct net *net, u32 dnode)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct sk_buff_head head;

	__skb_queue_head_init(&head);

	rcu_read_lock();
	named_distribute(net, &head, dnode,
			 &tn->nametbl->publ_list[TIPC_CLUSTER_SCOPE]);
	named_distribute(net, &head, dnode,
			 &tn->nametbl->publ_list[TIPC_ZONE_SCOPE]);
	rcu_read_unlock();

	tipc_node_xmit(net, &head, dnode, 0);
}

/**
 * tipc_publ_purge - remove publication associated with a failed node
 *
 * Invoked for each publication issued by a newly failed node.
 * Removes publication structure from name table & deletes it.
 */
static void tipc_publ_purge(struct net *net, struct publication *publ, u32 addr)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct publication *p;

	spin_lock_bh(&tn->nametbl_lock);
	p = tipc_nametbl_remove_publ(net, publ->type, publ->lower,
				     publ->node, publ->ref, publ->key);
	if (p)
		tipc_node_unsubscribe(net, &p->nodesub_list, addr);
	spin_unlock_bh(&tn->nametbl_lock);

	if (p != publ) {
		pr_err("Unable to remove publication from failed node\n"
		       " (type=%u, lower=%u, node=0x%x, ref=%u, key=%u)\n",
		       publ->type, publ->lower, publ->node, publ->ref,
		       publ->key);
	}

	kfree_rcu(p, rcu);
}

/**
 * tipc_dist_queue_purge - remove deferred updates from a node that went down
 */
static void tipc_dist_queue_purge(struct net *net, u32 addr)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct distr_queue_item *e, *tmp;

	spin_lock_bh(&tn->nametbl_lock);
	list_for_each_entry_safe(e, tmp, &tn->dist_queue, next) {
		if (e->node != addr)
			continue;
		list_del(&e->next);
		kfree(e);
	}
	spin_unlock_bh(&tn->nametbl_lock);
}

void tipc_publ_notify(struct net *net, struct list_head *nsub_list, u32 addr)
{
	struct publication *publ, *tmp;

	list_for_each_entry_safe(publ, tmp, nsub_list, nodesub_list)
		tipc_publ_purge(net, publ, addr);
	tipc_dist_queue_purge(net, addr);
}

/**
 * tipc_update_nametbl - try to process a nametable update and notify
 *			 subscribers
 *
 * tipc_nametbl_lock must be held.
 * Returns the publication item if successful, otherwise NULL.
 */
static bool tipc_update_nametbl(struct net *net, struct distr_item *i,
				u32 node, u32 dtype)
{
	struct publication *publ = NULL;

	if (dtype == PUBLICATION) {
		publ = tipc_nametbl_insert_publ(net, ntohl(i->type),
						ntohl(i->lower),
						ntohl(i->upper),
						TIPC_CLUSTER_SCOPE, node,
						ntohl(i->ref), ntohl(i->key));
		if (publ) {
			tipc_node_subscribe(net, &publ->nodesub_list, node);
			return true;
		}
	} else if (dtype == WITHDRAWAL) {
		publ = tipc_nametbl_remove_publ(net, ntohl(i->type),
						ntohl(i->lower),
						node, ntohl(i->ref),
						ntohl(i->key));
		if (publ) {
			tipc_node_unsubscribe(net, &publ->nodesub_list, node);
			kfree_rcu(publ, rcu);
			return true;
		}
	} else {
		pr_warn("Unrecognized name table message received\n");
	}
	return false;
}

/**
 * tipc_named_add_backlog - add a failed name table update to the backlog
 *
 */
static void tipc_named_add_backlog(struct net *net, struct distr_item *i,
				   u32 type, u32 node)
{
	struct distr_queue_item *e;
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	unsigned long now = get_jiffies_64();

	e = kzalloc(sizeof(*e), GFP_ATOMIC);
	if (!e)
		return;
	e->dtype = type;
	e->node = node;
	e->expires = now + msecs_to_jiffies(sysctl_tipc_named_timeout);
	memcpy(e, i, sizeof(*i));
	list_add_tail(&e->next, &tn->dist_queue);
}

/**
 * tipc_named_process_backlog - try to process any pending name table updates
 * from the network.
 */
void tipc_named_process_backlog(struct net *net)
{
	struct distr_queue_item *e, *tmp;
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	char addr[16];
	unsigned long now = get_jiffies_64();

	list_for_each_entry_safe(e, tmp, &tn->dist_queue, next) {
		if (time_after(e->expires, now)) {
			if (!tipc_update_nametbl(net, &e->i, e->node, e->dtype))
				continue;
		} else {
			tipc_addr_string_fill(addr, e->node);
			pr_warn_ratelimited("Dropping name table update (%d) of {%u, %u, %u} from %s key=%u\n",
					    e->dtype, ntohl(e->i.type),
					    ntohl(e->i.lower),
					    ntohl(e->i.upper),
					    addr, ntohl(e->i.key));
		}
		list_del(&e->next);
		kfree(e);
	}
}

/**
 * tipc_named_rcv - process name table update messages sent by another node
 */
void tipc_named_rcv(struct net *net, struct sk_buff_head *inputq)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_msg *msg;
	struct distr_item *item;
	uint count;
	u32 node;
	struct sk_buff *skb;
	int mtype;

	spin_lock_bh(&tn->nametbl_lock);
	for (skb = skb_dequeue(inputq); skb; skb = skb_dequeue(inputq)) {
		skb_linearize(skb);
		msg = buf_msg(skb);
		mtype = msg_type(msg);
		item = (struct distr_item *)msg_data(msg);
		count = msg_data_sz(msg) / ITEM_SIZE;
		node = msg_orignode(msg);
		while (count--) {
			if (!tipc_update_nametbl(net, item, node, mtype))
				tipc_named_add_backlog(net, item, mtype, node);
			item++;
		}
		kfree_skb(skb);
		tipc_named_process_backlog(net);
	}
	spin_unlock_bh(&tn->nametbl_lock);
}

/**
 * tipc_named_reinit - re-initialize local publications
 *
 * This routine is called whenever TIPC networking is enabled.
 * All name table entries published by this node are updated to reflect
 * the node's new network address.
 */
void tipc_named_reinit(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct publication *publ;
	int scope;

	spin_lock_bh(&tn->nametbl_lock);

	for (scope = TIPC_ZONE_SCOPE; scope <= TIPC_NODE_SCOPE; scope++)
		list_for_each_entry_rcu(publ, &tn->nametbl->publ_list[scope],
					local_list)
			publ->node = tn->own_addr;

	spin_unlock_bh(&tn->nametbl_lock);
}

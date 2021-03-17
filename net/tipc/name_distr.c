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
	i->port = htonl(p->port);
	i->key = htonl(p->key);
}

/**
 * named_prepare_buf - allocate & initialize a publication message
 *
 * The buffer returned is of size INT_H_SIZE + payload size
 */
static struct sk_buff *named_prepare_buf(struct net *net, u32 type, u32 size,
					 u32 dest)
{
	struct sk_buff *buf = tipc_buf_acquire(INT_H_SIZE + size, GFP_ATOMIC);
	u32 self = tipc_own_addr(net);
	struct tipc_msg *msg;

	if (buf != NULL) {
		msg = buf_msg(buf);
		tipc_msg_init(self, msg, NAME_DISTRIBUTOR,
			      type, INT_H_SIZE, dest);
		msg_set_size(msg, INT_H_SIZE + size);
	}
	return buf;
}

/**
 * tipc_named_publish - tell other nodes about a new publication by this node
 */
struct sk_buff *tipc_named_publish(struct net *net, struct publication *publ)
{
	struct name_table *nt = tipc_name_table(net);
	struct distr_item *item;
	struct sk_buff *skb;

	if (publ->scope == TIPC_NODE_SCOPE) {
		list_add_tail_rcu(&publ->binding_node, &nt->node_scope);
		return NULL;
	}
	write_lock_bh(&nt->cluster_scope_lock);
	list_add_tail(&publ->binding_node, &nt->cluster_scope);
	write_unlock_bh(&nt->cluster_scope_lock);
	skb = named_prepare_buf(net, PUBLICATION, ITEM_SIZE, 0);
	if (!skb) {
		pr_warn("Publication distribution failure\n");
		return NULL;
	}
	msg_set_named_seqno(buf_msg(skb), nt->snd_nxt++);
	msg_set_non_legacy(buf_msg(skb));
	item = (struct distr_item *)msg_data(buf_msg(skb));
	publ_to_item(item, publ);
	return skb;
}

/**
 * tipc_named_withdraw - tell other nodes about a withdrawn publication by this node
 */
struct sk_buff *tipc_named_withdraw(struct net *net, struct publication *publ)
{
	struct name_table *nt = tipc_name_table(net);
	struct distr_item *item;
	struct sk_buff *skb;

	write_lock_bh(&nt->cluster_scope_lock);
	list_del(&publ->binding_node);
	write_unlock_bh(&nt->cluster_scope_lock);
	if (publ->scope == TIPC_NODE_SCOPE)
		return NULL;

	skb = named_prepare_buf(net, WITHDRAWAL, ITEM_SIZE, 0);
	if (!skb) {
		pr_warn("Withdrawal distribution failure\n");
		return NULL;
	}
	msg_set_named_seqno(buf_msg(skb), nt->snd_nxt++);
	msg_set_non_legacy(buf_msg(skb));
	item = (struct distr_item *)msg_data(buf_msg(skb));
	publ_to_item(item, publ);
	return skb;
}

/**
 * named_distribute - prepare name info for bulk distribution to another node
 * @list: list of messages (buffers) to be returned from this function
 * @dnode: node to be updated
 * @pls: linked list of publication items to be packed into buffer chain
 */
static void named_distribute(struct net *net, struct sk_buff_head *list,
			     u32 dnode, struct list_head *pls, u16 seqno)
{
	struct publication *publ;
	struct sk_buff *skb = NULL;
	struct distr_item *item = NULL;
	u32 msg_dsz = ((tipc_node_get_mtu(net, dnode, 0, false) - INT_H_SIZE) /
			ITEM_SIZE) * ITEM_SIZE;
	u32 msg_rem = msg_dsz;
	struct tipc_msg *hdr;

	list_for_each_entry(publ, pls, binding_node) {
		/* Prepare next buffer: */
		if (!skb) {
			skb = named_prepare_buf(net, PUBLICATION, msg_rem,
						dnode);
			if (!skb) {
				pr_warn("Bulk publication failure\n");
				return;
			}
			hdr = buf_msg(skb);
			msg_set_bc_ack_invalid(hdr, true);
			msg_set_bulk(hdr);
			msg_set_non_legacy(hdr);
			item = (struct distr_item *)msg_data(hdr);
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
		hdr = buf_msg(skb);
		msg_set_size(hdr, INT_H_SIZE + (msg_dsz - msg_rem));
		skb_trim(skb, INT_H_SIZE + (msg_dsz - msg_rem));
		__skb_queue_tail(list, skb);
	}
	hdr = buf_msg(skb_peek_tail(list));
	msg_set_last_bulk(hdr);
	msg_set_named_seqno(hdr, seqno);
}

/**
 * tipc_named_node_up - tell specified node about all publications by this node
 */
void tipc_named_node_up(struct net *net, u32 dnode, u16 capabilities)
{
	struct name_table *nt = tipc_name_table(net);
	struct tipc_net *tn = tipc_net(net);
	struct sk_buff_head head;
	u16 seqno;

	__skb_queue_head_init(&head);
	spin_lock_bh(&tn->nametbl_lock);
	if (!(capabilities & TIPC_NAMED_BCAST))
		nt->rc_dests++;
	seqno = nt->snd_nxt;
	spin_unlock_bh(&tn->nametbl_lock);

	read_lock_bh(&nt->cluster_scope_lock);
	named_distribute(net, &head, dnode, &nt->cluster_scope, seqno);
	tipc_node_xmit(net, &head, dnode, 0);
	read_unlock_bh(&nt->cluster_scope_lock);
}

/**
 * tipc_publ_purge - remove publication associated with a failed node
 *
 * Invoked for each publication issued by a newly failed node.
 * Removes publication structure from name table & deletes it.
 */
static void tipc_publ_purge(struct net *net, struct publication *publ, u32 addr)
{
	struct tipc_net *tn = tipc_net(net);
	struct publication *p;

	spin_lock_bh(&tn->nametbl_lock);
	p = tipc_nametbl_remove_publ(net, publ->type, publ->lower, publ->upper,
				     publ->node, publ->key);
	if (p)
		tipc_node_unsubscribe(net, &p->binding_node, addr);
	spin_unlock_bh(&tn->nametbl_lock);

	if (p != publ) {
		pr_err("Unable to remove publication from failed node\n"
		       " (type=%u, lower=%u, node=0x%x, port=%u, key=%u)\n",
		       publ->type, publ->lower, publ->node, publ->port,
		       publ->key);
	}

	if (p)
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

void tipc_publ_notify(struct net *net, struct list_head *nsub_list,
		      u32 addr, u16 capabilities)
{
	struct name_table *nt = tipc_name_table(net);
	struct tipc_net *tn = tipc_net(net);

	struct publication *publ, *tmp;

	list_for_each_entry_safe(publ, tmp, nsub_list, binding_node)
		tipc_publ_purge(net, publ, addr);
	tipc_dist_queue_purge(net, addr);
	spin_lock_bh(&tn->nametbl_lock);
	if (!(capabilities & TIPC_NAMED_BCAST))
		nt->rc_dests--;
	spin_unlock_bh(&tn->nametbl_lock);
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
	struct publication *p = NULL;
	u32 lower = ntohl(i->lower);
	u32 upper = ntohl(i->upper);
	u32 type = ntohl(i->type);
	u32 port = ntohl(i->port);
	u32 key = ntohl(i->key);

	if (dtype == PUBLICATION) {
		p = tipc_nametbl_insert_publ(net, type, lower, upper,
					     TIPC_CLUSTER_SCOPE, node,
					     port, key);
		if (p) {
			tipc_node_subscribe(net, &p->binding_node, node);
			return true;
		}
	} else if (dtype == WITHDRAWAL) {
		p = tipc_nametbl_remove_publ(net, type, lower,
					     upper, node, key);
		if (p) {
			tipc_node_unsubscribe(net, &p->binding_node, node);
			kfree_rcu(p, rcu);
			return true;
		}
		pr_warn_ratelimited("Failed to remove binding %u,%u from %x\n",
				    type, lower, node);
	} else {
		pr_warn("Unrecognized name table message received\n");
	}
	return false;
}

static struct sk_buff *tipc_named_dequeue(struct sk_buff_head *namedq,
					  u16 *rcv_nxt, bool *open)
{
	struct sk_buff *skb, *tmp;
	struct tipc_msg *hdr;
	u16 seqno;

	spin_lock_bh(&namedq->lock);
	skb_queue_walk_safe(namedq, skb, tmp) {
		if (unlikely(skb_linearize(skb))) {
			__skb_unlink(skb, namedq);
			kfree_skb(skb);
			continue;
		}
		hdr = buf_msg(skb);
		seqno = msg_named_seqno(hdr);
		if (msg_is_last_bulk(hdr)) {
			*rcv_nxt = seqno;
			*open = true;
		}

		if (msg_is_bulk(hdr) || msg_is_legacy(hdr)) {
			__skb_unlink(skb, namedq);
			spin_unlock_bh(&namedq->lock);
			return skb;
		}

		if (*open && (*rcv_nxt == seqno)) {
			(*rcv_nxt)++;
			__skb_unlink(skb, namedq);
			spin_unlock_bh(&namedq->lock);
			return skb;
		}

		if (less(seqno, *rcv_nxt)) {
			__skb_unlink(skb, namedq);
			kfree_skb(skb);
			continue;
		}
	}
	spin_unlock_bh(&namedq->lock);
	return NULL;
}

/**
 * tipc_named_rcv - process name table update messages sent by another node
 */
void tipc_named_rcv(struct net *net, struct sk_buff_head *namedq,
		    u16 *rcv_nxt, bool *open)
{
	struct tipc_net *tn = tipc_net(net);
	struct distr_item *item;
	struct tipc_msg *hdr;
	struct sk_buff *skb;
	u32 count, node;

	spin_lock_bh(&tn->nametbl_lock);
	while ((skb = tipc_named_dequeue(namedq, rcv_nxt, open))) {
		hdr = buf_msg(skb);
		node = msg_orignode(hdr);
		item = (struct distr_item *)msg_data(hdr);
		count = msg_data_sz(hdr) / ITEM_SIZE;
		while (count--) {
			tipc_update_nametbl(net, item, node, msg_type(hdr));
			item++;
		}
		kfree_skb(skb);
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
	struct name_table *nt = tipc_name_table(net);
	struct tipc_net *tn = tipc_net(net);
	struct publication *publ;
	u32 self = tipc_own_addr(net);

	spin_lock_bh(&tn->nametbl_lock);

	list_for_each_entry_rcu(publ, &nt->node_scope, binding_node)
		publ->node = self;
	list_for_each_entry_rcu(publ, &nt->cluster_scope, binding_node)
		publ->node = self;
	nt->rc_dests = 0;
	spin_unlock_bh(&tn->nametbl_lock);
}

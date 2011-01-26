/*
 * net/tipc/name_distr.c: TIPC name distribution code
 *
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
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

#define ITEM_SIZE sizeof(struct distr_item)

/**
 * struct distr_item - publication info distributed to other nodes
 * @type: name sequence type
 * @lower: name sequence lower bound
 * @upper: name sequence upper bound
 * @ref: publishing port reference
 * @key: publication key
 *
 * ===> All fields are stored in network byte order. <===
 *
 * First 3 fields identify (name or) name sequence being published.
 * Reference field uniquely identifies port that published name sequence.
 * Key field uniquely identifies publication, in the event a port has
 * multiple publications of the same name sequence.
 *
 * Note: There is no field that identifies the publishing node because it is
 * the same for all items contained within a publication message.
 */

struct distr_item {
	__be32 type;
	__be32 lower;
	__be32 upper;
	__be32 ref;
	__be32 key;
};

/**
 * List of externally visible publications by this node --
 * that is, all publications having scope > TIPC_NODE_SCOPE.
 */

static LIST_HEAD(publ_root);
static u32 publ_cnt;

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

static struct sk_buff *named_prepare_buf(u32 type, u32 size, u32 dest)
{
	struct sk_buff *buf = tipc_buf_acquire(LONG_H_SIZE + size);
	struct tipc_msg *msg;

	if (buf != NULL) {
		msg = buf_msg(buf);
		tipc_msg_init(msg, NAME_DISTRIBUTOR, type, LONG_H_SIZE, dest);
		msg_set_size(msg, LONG_H_SIZE + size);
	}
	return buf;
}

static void named_cluster_distribute(struct sk_buff *buf)
{
	struct sk_buff *buf_copy;
	struct tipc_node *n_ptr;
	u32 n_num;

	for (n_num = 1; n_num <= tipc_net.highest_node; n_num++) {
		n_ptr = tipc_net.nodes[n_num];
		if (n_ptr && tipc_node_has_active_links(n_ptr)) {
			buf_copy = skb_copy(buf, GFP_ATOMIC);
			if (!buf_copy)
				break;
			msg_set_destnode(buf_msg(buf_copy), n_ptr->addr);
			tipc_link_send(buf_copy, n_ptr->addr, n_ptr->addr);
		}
	}

	buf_discard(buf);
}

/**
 * tipc_named_publish - tell other nodes about a new publication by this node
 */

void tipc_named_publish(struct publication *publ)
{
	struct sk_buff *buf;
	struct distr_item *item;

	list_add_tail(&publ->local_list, &publ_root);
	publ_cnt++;

	buf = named_prepare_buf(PUBLICATION, ITEM_SIZE, 0);
	if (!buf) {
		warn("Publication distribution failure\n");
		return;
	}

	item = (struct distr_item *)msg_data(buf_msg(buf));
	publ_to_item(item, publ);
	named_cluster_distribute(buf);
}

/**
 * tipc_named_withdraw - tell other nodes about a withdrawn publication by this node
 */

void tipc_named_withdraw(struct publication *publ)
{
	struct sk_buff *buf;
	struct distr_item *item;

	list_del(&publ->local_list);
	publ_cnt--;

	buf = named_prepare_buf(WITHDRAWAL, ITEM_SIZE, 0);
	if (!buf) {
		warn("Withdrawl distribution failure\n");
		return;
	}

	item = (struct distr_item *)msg_data(buf_msg(buf));
	publ_to_item(item, publ);
	named_cluster_distribute(buf);
}

/**
 * tipc_named_node_up - tell specified node about all publications by this node
 */

void tipc_named_node_up(unsigned long node)
{
	struct publication *publ;
	struct distr_item *item = NULL;
	struct sk_buff *buf = NULL;
	u32 left = 0;
	u32 rest;
	u32 max_item_buf;

	read_lock_bh(&tipc_nametbl_lock);
	max_item_buf = TIPC_MAX_USER_MSG_SIZE / ITEM_SIZE;
	max_item_buf *= ITEM_SIZE;
	rest = publ_cnt * ITEM_SIZE;

	list_for_each_entry(publ, &publ_root, local_list) {
		if (!buf) {
			left = (rest <= max_item_buf) ? rest : max_item_buf;
			rest -= left;
			buf = named_prepare_buf(PUBLICATION, left, node);
			if (!buf) {
				warn("Bulk publication distribution failure\n");
				goto exit;
			}
			item = (struct distr_item *)msg_data(buf_msg(buf));
		}
		publ_to_item(item, publ);
		item++;
		left -= ITEM_SIZE;
		if (!left) {
			msg_set_link_selector(buf_msg(buf), node);
			tipc_link_send(buf, node, node);
			buf = NULL;
		}
	}
exit:
	read_unlock_bh(&tipc_nametbl_lock);
}

/**
 * node_is_down - remove publication associated with a failed node
 *
 * Invoked for each publication issued by a newly failed node.
 * Removes publication structure from name table & deletes it.
 * In rare cases the link may have come back up again when this
 * function is called, and we have two items representing the same
 * publication. Nudge this item's key to distinguish it from the other.
 * (Note: Publication's node subscription is already unsubscribed.)
 */

static void node_is_down(struct publication *publ)
{
	struct publication *p;

	write_lock_bh(&tipc_nametbl_lock);
	publ->key += 1222345;
	p = tipc_nametbl_remove_publ(publ->type, publ->lower,
				     publ->node, publ->ref, publ->key);
	write_unlock_bh(&tipc_nametbl_lock);

	if (p != publ) {
		err("Unable to remove publication from failed node\n"
		    "(type=%u, lower=%u, node=0x%x, ref=%u, key=%u)\n",
		    publ->type, publ->lower, publ->node, publ->ref, publ->key);
	}

	kfree(p);
}

/**
 * tipc_named_recv - process name table update message sent by another node
 */

void tipc_named_recv(struct sk_buff *buf)
{
	struct publication *publ;
	struct tipc_msg *msg = buf_msg(buf);
	struct distr_item *item = (struct distr_item *)msg_data(msg);
	u32 count = msg_data_sz(msg) / ITEM_SIZE;

	write_lock_bh(&tipc_nametbl_lock);
	while (count--) {
		if (msg_type(msg) == PUBLICATION) {
			publ = tipc_nametbl_insert_publ(ntohl(item->type),
							ntohl(item->lower),
							ntohl(item->upper),
							TIPC_CLUSTER_SCOPE,
							msg_orignode(msg),
							ntohl(item->ref),
							ntohl(item->key));
			if (publ) {
				tipc_nodesub_subscribe(&publ->subscr,
						       msg_orignode(msg),
						       publ,
						       (net_ev_handler)node_is_down);
			}
		} else if (msg_type(msg) == WITHDRAWAL) {
			publ = tipc_nametbl_remove_publ(ntohl(item->type),
							ntohl(item->lower),
							msg_orignode(msg),
							ntohl(item->ref),
							ntohl(item->key));

			if (publ) {
				tipc_nodesub_unsubscribe(&publ->subscr);
				kfree(publ);
			} else {
				err("Unable to remove publication by node 0x%x\n"
				    "(type=%u, lower=%u, ref=%u, key=%u)\n",
				    msg_orignode(msg),
				    ntohl(item->type), ntohl(item->lower),
				    ntohl(item->ref), ntohl(item->key));
			}
		} else {
			warn("Unrecognized name table message received\n");
		}
		item++;
	}
	write_unlock_bh(&tipc_nametbl_lock);
	buf_discard(buf);
}

/**
 * tipc_named_reinit - re-initialize local publication list
 *
 * This routine is called whenever TIPC networking is (re)enabled.
 * All existing publications by this node that have "cluster" or "zone" scope
 * are updated to reflect the node's current network address.
 * (If the node's address is unchanged, the update loop terminates immediately.)
 */

void tipc_named_reinit(void)
{
	struct publication *publ;

	write_lock_bh(&tipc_nametbl_lock);
	list_for_each_entry(publ, &publ_root, local_list) {
		if (publ->node == tipc_own_addr)
			break;
		publ->node = tipc_own_addr;
	}
	write_unlock_bh(&tipc_nametbl_lock);
}

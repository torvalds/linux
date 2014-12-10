/*
 * net/tipc/node.c: TIPC node management routines
 *
 * Copyright (c) 2000-2006, 2012-2014, Ericsson AB
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
#include "config.h"
#include "node.h"
#include "name_distr.h"
#include "socket.h"

#define NODE_HTABLE_SIZE 512

static void node_lost_contact(struct tipc_node *n_ptr);
static void node_established_contact(struct tipc_node *n_ptr);

static struct hlist_head node_htable[NODE_HTABLE_SIZE];
LIST_HEAD(tipc_node_list);
static u32 tipc_num_nodes;
static u32 tipc_num_links;
static DEFINE_SPINLOCK(node_list_lock);

struct tipc_sock_conn {
	u32 port;
	u32 peer_port;
	u32 peer_node;
	struct list_head list;
};

static const struct nla_policy tipc_nl_node_policy[TIPC_NLA_NODE_MAX + 1] = {
	[TIPC_NLA_NODE_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_NODE_ADDR]		= { .type = NLA_U32 },
	[TIPC_NLA_NODE_UP]		= { .type = NLA_FLAG }
};

/*
 * A trivial power-of-two bitmask technique is used for speed, since this
 * operation is done for every incoming TIPC packet. The number of hash table
 * entries has been chosen so that no hash chain exceeds 8 nodes and will
 * usually be much smaller (typically only a single node).
 */
static unsigned int tipc_hashfn(u32 addr)
{
	return addr & (NODE_HTABLE_SIZE - 1);
}

/*
 * tipc_node_find - locate specified node object, if it exists
 */
struct tipc_node *tipc_node_find(u32 addr)
{
	struct tipc_node *node;

	if (unlikely(!in_own_cluster_exact(addr)))
		return NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(node, &node_htable[tipc_hashfn(addr)], hash) {
		if (node->addr == addr) {
			rcu_read_unlock();
			return node;
		}
	}
	rcu_read_unlock();
	return NULL;
}

struct tipc_node *tipc_node_create(u32 addr)
{
	struct tipc_node *n_ptr, *temp_node;

	spin_lock_bh(&node_list_lock);

	n_ptr = kzalloc(sizeof(*n_ptr), GFP_ATOMIC);
	if (!n_ptr) {
		spin_unlock_bh(&node_list_lock);
		pr_warn("Node creation failed, no memory\n");
		return NULL;
	}

	n_ptr->addr = addr;
	spin_lock_init(&n_ptr->lock);
	INIT_HLIST_NODE(&n_ptr->hash);
	INIT_LIST_HEAD(&n_ptr->list);
	INIT_LIST_HEAD(&n_ptr->publ_list);
	INIT_LIST_HEAD(&n_ptr->conn_sks);
	skb_queue_head_init(&n_ptr->waiting_sks);
	__skb_queue_head_init(&n_ptr->bclink.deferred_queue);

	hlist_add_head_rcu(&n_ptr->hash, &node_htable[tipc_hashfn(addr)]);

	list_for_each_entry_rcu(temp_node, &tipc_node_list, list) {
		if (n_ptr->addr < temp_node->addr)
			break;
	}
	list_add_tail_rcu(&n_ptr->list, &temp_node->list);
	n_ptr->action_flags = TIPC_WAIT_PEER_LINKS_DOWN;
	n_ptr->signature = INVALID_NODE_SIG;

	tipc_num_nodes++;

	spin_unlock_bh(&node_list_lock);
	return n_ptr;
}

static void tipc_node_delete(struct tipc_node *n_ptr)
{
	list_del_rcu(&n_ptr->list);
	hlist_del_rcu(&n_ptr->hash);
	kfree_rcu(n_ptr, rcu);

	tipc_num_nodes--;
}

void tipc_node_stop(void)
{
	struct tipc_node *node, *t_node;

	spin_lock_bh(&node_list_lock);
	list_for_each_entry_safe(node, t_node, &tipc_node_list, list)
		tipc_node_delete(node);
	spin_unlock_bh(&node_list_lock);
}

int tipc_node_add_conn(u32 dnode, u32 port, u32 peer_port)
{
	struct tipc_node *node;
	struct tipc_sock_conn *conn;

	if (in_own_node(dnode))
		return 0;

	node = tipc_node_find(dnode);
	if (!node) {
		pr_warn("Connecting sock to node 0x%x failed\n", dnode);
		return -EHOSTUNREACH;
	}
	conn = kmalloc(sizeof(*conn), GFP_ATOMIC);
	if (!conn)
		return -EHOSTUNREACH;
	conn->peer_node = dnode;
	conn->port = port;
	conn->peer_port = peer_port;

	tipc_node_lock(node);
	list_add_tail(&conn->list, &node->conn_sks);
	tipc_node_unlock(node);
	return 0;
}

void tipc_node_remove_conn(u32 dnode, u32 port)
{
	struct tipc_node *node;
	struct tipc_sock_conn *conn, *safe;

	if (in_own_node(dnode))
		return;

	node = tipc_node_find(dnode);
	if (!node)
		return;

	tipc_node_lock(node);
	list_for_each_entry_safe(conn, safe, &node->conn_sks, list) {
		if (port != conn->port)
			continue;
		list_del(&conn->list);
		kfree(conn);
	}
	tipc_node_unlock(node);
}

void tipc_node_abort_sock_conns(struct list_head *conns)
{
	struct tipc_sock_conn *conn, *safe;
	struct sk_buff *buf;

	list_for_each_entry_safe(conn, safe, conns, list) {
		buf = tipc_msg_create(TIPC_CRITICAL_IMPORTANCE, TIPC_CONN_MSG,
				      SHORT_H_SIZE, 0, tipc_own_addr,
				      conn->peer_node, conn->port,
				      conn->peer_port, TIPC_ERR_NO_NODE);
		if (likely(buf))
			tipc_sk_rcv(buf);
		list_del(&conn->list);
		kfree(conn);
	}
}

/**
 * tipc_node_link_up - handle addition of link
 *
 * Link becomes active (alone or shared) or standby, depending on its priority.
 */
void tipc_node_link_up(struct tipc_node *n_ptr, struct tipc_link *l_ptr)
{
	struct tipc_link **active = &n_ptr->active_links[0];

	n_ptr->working_links++;
	n_ptr->action_flags |= TIPC_NOTIFY_LINK_UP;
	n_ptr->link_id = l_ptr->peer_bearer_id << 16 | l_ptr->bearer_id;

	pr_info("Established link <%s> on network plane %c\n",
		l_ptr->name, l_ptr->net_plane);

	if (!active[0]) {
		active[0] = active[1] = l_ptr;
		node_established_contact(n_ptr);
		goto exit;
	}
	if (l_ptr->priority < active[0]->priority) {
		pr_info("New link <%s> becomes standby\n", l_ptr->name);
		goto exit;
	}
	tipc_link_dup_queue_xmit(active[0], l_ptr);
	if (l_ptr->priority == active[0]->priority) {
		active[0] = l_ptr;
		goto exit;
	}
	pr_info("Old link <%s> becomes standby\n", active[0]->name);
	if (active[1] != active[0])
		pr_info("Old link <%s> becomes standby\n", active[1]->name);
	active[0] = active[1] = l_ptr;
exit:
	/* Leave room for changeover header when returning 'mtu' to users: */
	n_ptr->act_mtus[0] = active[0]->max_pkt - INT_H_SIZE;
	n_ptr->act_mtus[1] = active[1]->max_pkt - INT_H_SIZE;
}

/**
 * node_select_active_links - select active link
 */
static void node_select_active_links(struct tipc_node *n_ptr)
{
	struct tipc_link **active = &n_ptr->active_links[0];
	u32 i;
	u32 highest_prio = 0;

	active[0] = active[1] = NULL;

	for (i = 0; i < MAX_BEARERS; i++) {
		struct tipc_link *l_ptr = n_ptr->links[i];

		if (!l_ptr || !tipc_link_is_up(l_ptr) ||
		    (l_ptr->priority < highest_prio))
			continue;

		if (l_ptr->priority > highest_prio) {
			highest_prio = l_ptr->priority;
			active[0] = active[1] = l_ptr;
		} else {
			active[1] = l_ptr;
		}
	}
}

/**
 * tipc_node_link_down - handle loss of link
 */
void tipc_node_link_down(struct tipc_node *n_ptr, struct tipc_link *l_ptr)
{
	struct tipc_link **active;

	n_ptr->working_links--;
	n_ptr->action_flags |= TIPC_NOTIFY_LINK_DOWN;
	n_ptr->link_id = l_ptr->peer_bearer_id << 16 | l_ptr->bearer_id;

	if (!tipc_link_is_active(l_ptr)) {
		pr_info("Lost standby link <%s> on network plane %c\n",
			l_ptr->name, l_ptr->net_plane);
		return;
	}
	pr_info("Lost link <%s> on network plane %c\n",
		l_ptr->name, l_ptr->net_plane);

	active = &n_ptr->active_links[0];
	if (active[0] == l_ptr)
		active[0] = active[1];
	if (active[1] == l_ptr)
		active[1] = active[0];
	if (active[0] == l_ptr)
		node_select_active_links(n_ptr);
	if (tipc_node_is_up(n_ptr))
		tipc_link_failover_send_queue(l_ptr);
	else
		node_lost_contact(n_ptr);

	/* Leave room for changeover header when returning 'mtu' to users: */
	if (active[0]) {
		n_ptr->act_mtus[0] = active[0]->max_pkt - INT_H_SIZE;
		n_ptr->act_mtus[1] = active[1]->max_pkt - INT_H_SIZE;
		return;
	}

	/* Loopback link went down? No fragmentation needed from now on. */
	if (n_ptr->addr == tipc_own_addr) {
		n_ptr->act_mtus[0] = MAX_MSG_SIZE;
		n_ptr->act_mtus[1] = MAX_MSG_SIZE;
	}
}

int tipc_node_active_links(struct tipc_node *n_ptr)
{
	return n_ptr->active_links[0] != NULL;
}

int tipc_node_is_up(struct tipc_node *n_ptr)
{
	return tipc_node_active_links(n_ptr);
}

void tipc_node_attach_link(struct tipc_node *n_ptr, struct tipc_link *l_ptr)
{
	n_ptr->links[l_ptr->bearer_id] = l_ptr;
	spin_lock_bh(&node_list_lock);
	tipc_num_links++;
	spin_unlock_bh(&node_list_lock);
	n_ptr->link_cnt++;
}

void tipc_node_detach_link(struct tipc_node *n_ptr, struct tipc_link *l_ptr)
{
	int i;

	for (i = 0; i < MAX_BEARERS; i++) {
		if (l_ptr != n_ptr->links[i])
			continue;
		n_ptr->links[i] = NULL;
		spin_lock_bh(&node_list_lock);
		tipc_num_links--;
		spin_unlock_bh(&node_list_lock);
		n_ptr->link_cnt--;
	}
}

static void node_established_contact(struct tipc_node *n_ptr)
{
	n_ptr->action_flags |= TIPC_NOTIFY_NODE_UP;
	n_ptr->bclink.oos_state = 0;
	n_ptr->bclink.acked = tipc_bclink_get_last_sent();
	tipc_bclink_add_node(n_ptr->addr);
}

static void node_lost_contact(struct tipc_node *n_ptr)
{
	char addr_string[16];
	u32 i;

	pr_info("Lost contact with %s\n",
		tipc_addr_string_fill(addr_string, n_ptr->addr));

	/* Flush broadcast link info associated with lost node */
	if (n_ptr->bclink.recv_permitted) {
		__skb_queue_purge(&n_ptr->bclink.deferred_queue);

		if (n_ptr->bclink.reasm_buf) {
			kfree_skb(n_ptr->bclink.reasm_buf);
			n_ptr->bclink.reasm_buf = NULL;
		}

		tipc_bclink_remove_node(n_ptr->addr);
		tipc_bclink_acknowledge(n_ptr, INVALID_LINK_SEQ);

		n_ptr->bclink.recv_permitted = false;
	}

	/* Abort link changeover */
	for (i = 0; i < MAX_BEARERS; i++) {
		struct tipc_link *l_ptr = n_ptr->links[i];
		if (!l_ptr)
			continue;
		l_ptr->reset_checkpoint = l_ptr->next_in_no;
		l_ptr->exp_msg_count = 0;
		tipc_link_reset_fragments(l_ptr);
	}

	n_ptr->action_flags &= ~TIPC_WAIT_OWN_LINKS_DOWN;

	/* Notify subscribers and prevent re-contact with node until
	 * cleanup is done.
	 */
	n_ptr->action_flags |= TIPC_WAIT_PEER_LINKS_DOWN |
			       TIPC_NOTIFY_NODE_DOWN;
}

struct sk_buff *tipc_node_get_nodes(const void *req_tlv_area, int req_tlv_space)
{
	u32 domain;
	struct sk_buff *buf;
	struct tipc_node *n_ptr;
	struct tipc_node_info node_info;
	u32 payload_size;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_NET_ADDR))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	domain = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (!tipc_addr_domain_valid(domain))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (network address)");

	spin_lock_bh(&node_list_lock);
	if (!tipc_num_nodes) {
		spin_unlock_bh(&node_list_lock);
		return tipc_cfg_reply_none();
	}

	/* For now, get space for all other nodes */
	payload_size = TLV_SPACE(sizeof(node_info)) * tipc_num_nodes;
	if (payload_size > 32768u) {
		spin_unlock_bh(&node_list_lock);
		return tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
						   " (too many nodes)");
	}
	spin_unlock_bh(&node_list_lock);

	buf = tipc_cfg_reply_alloc(payload_size);
	if (!buf)
		return NULL;

	/* Add TLVs for all nodes in scope */
	rcu_read_lock();
	list_for_each_entry_rcu(n_ptr, &tipc_node_list, list) {
		if (!tipc_in_scope(domain, n_ptr->addr))
			continue;
		node_info.addr = htonl(n_ptr->addr);
		node_info.up = htonl(tipc_node_is_up(n_ptr));
		tipc_cfg_append_tlv(buf, TIPC_TLV_NODE_INFO,
				    &node_info, sizeof(node_info));
	}
	rcu_read_unlock();
	return buf;
}

struct sk_buff *tipc_node_get_links(const void *req_tlv_area, int req_tlv_space)
{
	u32 domain;
	struct sk_buff *buf;
	struct tipc_node *n_ptr;
	struct tipc_link_info link_info;
	u32 payload_size;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_NET_ADDR))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	domain = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (!tipc_addr_domain_valid(domain))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (network address)");

	if (!tipc_own_addr)
		return tipc_cfg_reply_none();

	spin_lock_bh(&node_list_lock);
	/* Get space for all unicast links + broadcast link */
	payload_size = TLV_SPACE((sizeof(link_info)) * (tipc_num_links + 1));
	if (payload_size > 32768u) {
		spin_unlock_bh(&node_list_lock);
		return tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
						   " (too many links)");
	}
	spin_unlock_bh(&node_list_lock);

	buf = tipc_cfg_reply_alloc(payload_size);
	if (!buf)
		return NULL;

	/* Add TLV for broadcast link */
	link_info.dest = htonl(tipc_cluster_mask(tipc_own_addr));
	link_info.up = htonl(1);
	strlcpy(link_info.str, tipc_bclink_name, TIPC_MAX_LINK_NAME);
	tipc_cfg_append_tlv(buf, TIPC_TLV_LINK_INFO, &link_info, sizeof(link_info));

	/* Add TLVs for any other links in scope */
	rcu_read_lock();
	list_for_each_entry_rcu(n_ptr, &tipc_node_list, list) {
		u32 i;

		if (!tipc_in_scope(domain, n_ptr->addr))
			continue;
		tipc_node_lock(n_ptr);
		for (i = 0; i < MAX_BEARERS; i++) {
			if (!n_ptr->links[i])
				continue;
			link_info.dest = htonl(n_ptr->addr);
			link_info.up = htonl(tipc_link_is_up(n_ptr->links[i]));
			strcpy(link_info.str, n_ptr->links[i]->name);
			tipc_cfg_append_tlv(buf, TIPC_TLV_LINK_INFO,
					    &link_info, sizeof(link_info));
		}
		tipc_node_unlock(n_ptr);
	}
	rcu_read_unlock();
	return buf;
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
int tipc_node_get_linkname(u32 bearer_id, u32 addr, char *linkname, size_t len)
{
	struct tipc_link *link;
	struct tipc_node *node = tipc_node_find(addr);

	if ((bearer_id >= MAX_BEARERS) || !node)
		return -EINVAL;
	tipc_node_lock(node);
	link = node->links[bearer_id];
	if (link) {
		strncpy(linkname, link->name, len);
		tipc_node_unlock(node);
		return 0;
	}
	tipc_node_unlock(node);
	return -EINVAL;
}

void tipc_node_unlock(struct tipc_node *node)
{
	LIST_HEAD(nsub_list);
	LIST_HEAD(conn_sks);
	struct sk_buff_head waiting_sks;
	u32 addr = 0;
	int flags = node->action_flags;
	u32 link_id = 0;

	if (likely(!flags)) {
		spin_unlock_bh(&node->lock);
		return;
	}

	addr = node->addr;
	link_id = node->link_id;
	__skb_queue_head_init(&waiting_sks);

	if (flags & TIPC_WAKEUP_USERS)
		skb_queue_splice_init(&node->waiting_sks, &waiting_sks);

	if (flags & TIPC_NOTIFY_NODE_DOWN) {
		list_replace_init(&node->publ_list, &nsub_list);
		list_replace_init(&node->conn_sks, &conn_sks);
	}
	node->action_flags &= ~(TIPC_WAKEUP_USERS | TIPC_NOTIFY_NODE_DOWN |
				TIPC_NOTIFY_NODE_UP | TIPC_NOTIFY_LINK_UP |
				TIPC_NOTIFY_LINK_DOWN |
				TIPC_WAKEUP_BCAST_USERS);

	spin_unlock_bh(&node->lock);

	while (!skb_queue_empty(&waiting_sks))
		tipc_sk_rcv(__skb_dequeue(&waiting_sks));

	if (!list_empty(&conn_sks))
		tipc_node_abort_sock_conns(&conn_sks);

	if (!list_empty(&nsub_list))
		tipc_publ_notify(&nsub_list, addr);

	if (flags & TIPC_WAKEUP_BCAST_USERS)
		tipc_bclink_wakeup_users();

	if (flags & TIPC_NOTIFY_NODE_UP)
		tipc_named_node_up(addr);

	if (flags & TIPC_NOTIFY_LINK_UP)
		tipc_nametbl_publish(TIPC_LINK_STATE, addr, addr,
				     TIPC_NODE_SCOPE, link_id, addr);

	if (flags & TIPC_NOTIFY_LINK_DOWN)
		tipc_nametbl_withdraw(TIPC_LINK_STATE, addr,
				      link_id, addr);
}

/* Caller should hold node lock for the passed node */
static int __tipc_nl_add_node(struct tipc_nl_msg *msg, struct tipc_node *node)
{
	void *hdr;
	struct nlattr *attrs;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_v2_family,
			  NLM_F_MULTI, TIPC_NL_NODE_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_NODE);
	if (!attrs)
		goto msg_full;

	if (nla_put_u32(msg->skb, TIPC_NLA_NODE_ADDR, node->addr))
		goto attr_msg_full;
	if (tipc_node_is_up(node))
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

int tipc_nl_node_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int err;
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

	if (last_addr && !tipc_node_find(last_addr)) {
		rcu_read_unlock();
		/* We never set seq or call nl_dump_check_consistent() this
		 * means that setting prev_seq here will cause the consistence
		 * check to fail in the netlink callback handler. Resulting in
		 * the NLMSG_DONE message having the NLM_F_DUMP_INTR flag set if
		 * the node state changed while we released the lock.
		 */
		cb->prev_seq = 1;
		return -EPIPE;
	}

	list_for_each_entry_rcu(node, &tipc_node_list, list) {
		if (last_addr) {
			if (node->addr == last_addr)
				last_addr = 0;
			else
				continue;
		}

		tipc_node_lock(node);
		err = __tipc_nl_add_node(&msg, node);
		if (err) {
			last_addr = node->addr;
			tipc_node_unlock(node);
			goto out;
		}

		tipc_node_unlock(node);
	}
	done = 1;
out:
	cb->args[0] = done;
	cb->args[1] = last_addr;
	rcu_read_unlock();

	return skb->len;
}

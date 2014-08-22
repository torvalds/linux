/*
 * net/tipc/port.c: TIPC port code
 *
 * Copyright (c) 1992-2007, 2014, Ericsson AB
 * Copyright (c) 2004-2008, 2010-2013, Wind River Systems
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
#include "port.h"
#include "name_table.h"
#include "socket.h"

/* Connection management: */
#define PROBING_INTERVAL 3600000	/* [ms] => 1 h */

#define MAX_REJECT_SIZE 1024

DEFINE_SPINLOCK(tipc_port_list_lock);

static LIST_HEAD(ports);

/**
 * tipc_port_peer_msg - verify message was sent by connected port's peer
 *
 * Handles cases where the node's network address has changed from
 * the default of <0.0.0> to its configured setting.
 */
int tipc_port_peer_msg(struct tipc_port *p_ptr, struct tipc_msg *msg)
{
	u32 peernode;
	u32 orignode;

	if (msg_origport(msg) != tipc_port_peerport(p_ptr))
		return 0;

	orignode = msg_orignode(msg);
	peernode = tipc_port_peernode(p_ptr);
	return (orignode == peernode) ||
		(!orignode && (peernode == tipc_own_addr)) ||
		(!peernode && (orignode == tipc_own_addr));
}

/* tipc_port_init - intiate TIPC port and lock it
 *
 * Returns obtained reference if initialization is successful, zero otherwise
 */
u32 tipc_port_init(struct tipc_port *p_ptr,
		   const unsigned int importance)
{
	struct tipc_msg *msg;
	u32 ref;

	ref = tipc_ref_acquire(p_ptr, &p_ptr->lock);
	if (!ref) {
		pr_warn("Port registration failed, ref. table exhausted\n");
		return 0;
	}

	p_ptr->max_pkt = MAX_PKT_DEFAULT;
	p_ptr->ref = ref;
	INIT_LIST_HEAD(&p_ptr->subscription.nodesub_list);
	INIT_LIST_HEAD(&p_ptr->publications);
	INIT_LIST_HEAD(&p_ptr->port_list);

	/*
	 * Must hold port list lock while initializing message header template
	 * to ensure a change to node's own network address doesn't result
	 * in template containing out-dated network address information
	 */
	spin_lock_bh(&tipc_port_list_lock);
	msg = &p_ptr->phdr;
	tipc_msg_init(msg, importance, TIPC_NAMED_MSG, NAMED_H_SIZE, 0);
	msg_set_origport(msg, ref);
	list_add_tail(&p_ptr->port_list, &ports);
	spin_unlock_bh(&tipc_port_list_lock);
	return ref;
}

void tipc_port_destroy(struct tipc_port *p_ptr)
{
	struct sk_buff *buf = NULL;
	struct tipc_msg *msg = NULL;
	u32 peer_node;

	tipc_withdraw(p_ptr, 0, NULL);

	spin_lock_bh(p_ptr->lock);
	tipc_ref_discard(p_ptr->ref);
	spin_unlock_bh(p_ptr->lock);

	k_cancel_timer(&p_ptr->timer);
	if (p_ptr->connected) {
		peer_node = tipc_port_peernode(p_ptr);
		buf = tipc_msg_create(TIPC_CRITICAL_IMPORTANCE, TIPC_CONN_MSG,
				      SHORT_H_SIZE, 0, peer_node,
				      tipc_own_addr, tipc_port_peerport(p_ptr),
				      p_ptr->ref, TIPC_ERR_NO_PORT);
		if (buf) {
			msg = buf_msg(buf);
			tipc_link_xmit(buf, peer_node, msg_link_selector(msg));
		}
		tipc_node_remove_conn(peer_node, p_ptr->ref);
	}
	spin_lock_bh(&tipc_port_list_lock);
	list_del(&p_ptr->port_list);
	spin_unlock_bh(&tipc_port_list_lock);
	k_term_timer(&p_ptr->timer);
}

static int port_print(struct tipc_port *p_ptr, char *buf, int len, int full_id)
{
	struct publication *publ;
	int ret;

	if (full_id)
		ret = tipc_snprintf(buf, len, "<%u.%u.%u:%u>:",
				    tipc_zone(tipc_own_addr),
				    tipc_cluster(tipc_own_addr),
				    tipc_node(tipc_own_addr), p_ptr->ref);
	else
		ret = tipc_snprintf(buf, len, "%-10u:", p_ptr->ref);

	if (p_ptr->connected) {
		u32 dport = tipc_port_peerport(p_ptr);
		u32 destnode = tipc_port_peernode(p_ptr);

		ret += tipc_snprintf(buf + ret, len - ret,
				     " connected to <%u.%u.%u:%u>",
				     tipc_zone(destnode),
				     tipc_cluster(destnode),
				     tipc_node(destnode), dport);
		if (p_ptr->conn_type != 0)
			ret += tipc_snprintf(buf + ret, len - ret,
					     " via {%u,%u}", p_ptr->conn_type,
					     p_ptr->conn_instance);
	} else if (p_ptr->published) {
		ret += tipc_snprintf(buf + ret, len - ret, " bound to");
		list_for_each_entry(publ, &p_ptr->publications, pport_list) {
			if (publ->lower == publ->upper)
				ret += tipc_snprintf(buf + ret, len - ret,
						     " {%u,%u}", publ->type,
						     publ->lower);
			else
				ret += tipc_snprintf(buf + ret, len - ret,
						     " {%u,%u,%u}", publ->type,
						     publ->lower, publ->upper);
		}
	}
	ret += tipc_snprintf(buf + ret, len - ret, "\n");
	return ret;
}

struct sk_buff *tipc_port_get_ports(void)
{
	struct sk_buff *buf;
	struct tlv_desc *rep_tlv;
	char *pb;
	int pb_len;
	struct tipc_port *p_ptr;
	int str_len = 0;

	buf = tipc_cfg_reply_alloc(TLV_SPACE(ULTRA_STRING_MAX_LEN));
	if (!buf)
		return NULL;
	rep_tlv = (struct tlv_desc *)buf->data;
	pb = TLV_DATA(rep_tlv);
	pb_len = ULTRA_STRING_MAX_LEN;

	spin_lock_bh(&tipc_port_list_lock);
	list_for_each_entry(p_ptr, &ports, port_list) {
		spin_lock_bh(p_ptr->lock);
		str_len += port_print(p_ptr, pb, pb_len, 0);
		spin_unlock_bh(p_ptr->lock);
	}
	spin_unlock_bh(&tipc_port_list_lock);
	str_len += 1;	/* for "\0" */
	skb_put(buf, TLV_SPACE(str_len));
	TLV_SET(rep_tlv, TIPC_TLV_ULTRA_STRING, NULL, str_len);

	return buf;
}

void tipc_port_reinit(void)
{
	struct tipc_port *p_ptr;
	struct tipc_msg *msg;

	spin_lock_bh(&tipc_port_list_lock);
	list_for_each_entry(p_ptr, &ports, port_list) {
		msg = &p_ptr->phdr;
		msg_set_prevnode(msg, tipc_own_addr);
		msg_set_orignode(msg, tipc_own_addr);
	}
	spin_unlock_bh(&tipc_port_list_lock);
}

void tipc_acknowledge(u32 ref, u32 ack)
{
	struct tipc_port *p_ptr;
	struct sk_buff *buf = NULL;
	struct tipc_msg *msg;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return;
	if (p_ptr->connected)
		buf = tipc_msg_create(CONN_MANAGER, CONN_ACK, INT_H_SIZE,
				      0, tipc_port_peernode(p_ptr),
				      tipc_own_addr, tipc_port_peerport(p_ptr),
				      p_ptr->ref, TIPC_OK);
	tipc_port_unlock(p_ptr);
	if (!buf)
		return;
	msg = buf_msg(buf);
	msg_set_msgcnt(msg, ack);
	tipc_link_xmit(buf, msg_destnode(msg),	msg_link_selector(msg));
}

int tipc_publish(struct tipc_port *p_ptr, unsigned int scope,
		 struct tipc_name_seq const *seq)
{
	struct publication *publ;
	u32 key;

	if (p_ptr->connected)
		return -EINVAL;
	key = p_ptr->ref + p_ptr->pub_count + 1;
	if (key == p_ptr->ref)
		return -EADDRINUSE;

	publ = tipc_nametbl_publish(seq->type, seq->lower, seq->upper,
				    scope, p_ptr->ref, key);
	if (publ) {
		list_add(&publ->pport_list, &p_ptr->publications);
		p_ptr->pub_count++;
		p_ptr->published = 1;
		return 0;
	}
	return -EINVAL;
}

int tipc_withdraw(struct tipc_port *p_ptr, unsigned int scope,
		  struct tipc_name_seq const *seq)
{
	struct publication *publ;
	struct publication *tpubl;
	int res = -EINVAL;

	if (!seq) {
		list_for_each_entry_safe(publ, tpubl,
					 &p_ptr->publications, pport_list) {
			tipc_nametbl_withdraw(publ->type, publ->lower,
					      publ->ref, publ->key);
		}
		res = 0;
	} else {
		list_for_each_entry_safe(publ, tpubl,
					 &p_ptr->publications, pport_list) {
			if (publ->scope != scope)
				continue;
			if (publ->type != seq->type)
				continue;
			if (publ->lower != seq->lower)
				continue;
			if (publ->upper != seq->upper)
				break;
			tipc_nametbl_withdraw(publ->type, publ->lower,
					      publ->ref, publ->key);
			res = 0;
			break;
		}
	}
	if (list_empty(&p_ptr->publications))
		p_ptr->published = 0;
	return res;
}

int tipc_port_connect(u32 ref, struct tipc_portid const *peer)
{
	struct tipc_port *p_ptr;
	int res;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;
	res = __tipc_port_connect(ref, p_ptr, peer);
	tipc_port_unlock(p_ptr);
	return res;
}

/*
 * __tipc_port_connect - connect to a remote peer
 *
 * Port must be locked.
 */
int __tipc_port_connect(u32 ref, struct tipc_port *p_ptr,
			struct tipc_portid const *peer)
{
	struct tipc_msg *msg;
	int res = -EINVAL;

	if (p_ptr->published || p_ptr->connected)
		goto exit;
	if (!peer->ref)
		goto exit;

	msg = &p_ptr->phdr;
	msg_set_destnode(msg, peer->node);
	msg_set_destport(msg, peer->ref);
	msg_set_type(msg, TIPC_CONN_MSG);
	msg_set_lookup_scope(msg, 0);
	msg_set_hdr_sz(msg, SHORT_H_SIZE);

	p_ptr->probing_interval = PROBING_INTERVAL;
	p_ptr->probing_state = TIPC_CONN_OK;
	p_ptr->connected = 1;
	k_start_timer(&p_ptr->timer, p_ptr->probing_interval);
	res = tipc_node_add_conn(tipc_port_peernode(p_ptr), p_ptr->ref,
				 tipc_port_peerport(p_ptr));
exit:
	p_ptr->max_pkt = tipc_node_get_mtu(peer->node, ref);
	return res;
}

/*
 * __tipc_disconnect - disconnect port from peer
 *
 * Port must be locked.
 */
int __tipc_port_disconnect(struct tipc_port *tp_ptr)
{
	if (tp_ptr->connected) {
		tp_ptr->connected = 0;
		/* let timer expire on it's own to avoid deadlock! */
		tipc_node_remove_conn(tipc_port_peernode(tp_ptr), tp_ptr->ref);
		return 0;
	}

	return -ENOTCONN;
}

/*
 * tipc_port_disconnect(): Disconnect port form peer.
 *                    This is a node local operation.
 */
int tipc_port_disconnect(u32 ref)
{
	struct tipc_port *p_ptr;
	int res;

	p_ptr = tipc_port_lock(ref);
	if (!p_ptr)
		return -EINVAL;
	res = __tipc_port_disconnect(p_ptr);
	tipc_port_unlock(p_ptr);
	return res;
}

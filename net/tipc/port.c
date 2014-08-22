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

#define MAX_REJECT_SIZE 1024

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
	list_for_each_entry(p_ptr, &tipc_socks, port_list) {
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
	list_for_each_entry(p_ptr, &tipc_socks, port_list) {
		msg = &p_ptr->phdr;
		msg_set_prevnode(msg, tipc_own_addr);
		msg_set_orignode(msg, tipc_own_addr);
	}
	spin_unlock_bh(&tipc_port_list_lock);
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

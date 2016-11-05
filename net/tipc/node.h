/*
 * net/tipc/node.h: Include file for TIPC node management routines
 *
 * Copyright (c) 2000-2006, 2014-2016, Ericsson AB
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

/* Optional capabilities supported by this code version
 */
enum {
	TIPC_BCAST_SYNCH      = (1 << 1),
	TIPC_BCAST_STATE_NACK = (1 << 2),
	TIPC_BLOCK_FLOWCTL    = (1 << 3)
};

#define TIPC_NODE_CAPABILITIES (TIPC_BCAST_SYNCH | \
				TIPC_BCAST_STATE_NACK | \
				TIPC_BLOCK_FLOWCTL)
#define INVALID_BEARER_ID -1

void tipc_node_stop(struct net *net);
void tipc_node_check_dest(struct net *net, u32 onode,
			  struct tipc_bearer *bearer,
			  u16 capabilities, u32 signature,
			  struct tipc_media_addr *maddr,
			  bool *respond, bool *dupl_addr);
void tipc_node_delete_links(struct net *net, int bearer_id);
int tipc_node_get_linkname(struct net *net, u32 bearer_id, u32 node,
			   char *linkname, size_t len);
int tipc_node_xmit(struct net *net, struct sk_buff_head *list, u32 dnode,
		   int selector);
int tipc_node_xmit_skb(struct net *net, struct sk_buff *skb, u32 dest,
		       u32 selector);
void tipc_node_subscribe(struct net *net, struct list_head *subscr, u32 addr);
void tipc_node_unsubscribe(struct net *net, struct list_head *subscr, u32 addr);
void tipc_node_broadcast(struct net *net, struct sk_buff *skb);
int tipc_node_add_conn(struct net *net, u32 dnode, u32 port, u32 peer_port);
void tipc_node_remove_conn(struct net *net, u32 dnode, u32 port);
int tipc_node_get_mtu(struct net *net, u32 addr, u32 sel);
u16 tipc_node_get_capabilities(struct net *net, u32 addr);
int tipc_nl_node_dump(struct sk_buff *skb, struct netlink_callback *cb);
int tipc_nl_node_dump_link(struct sk_buff *skb, struct netlink_callback *cb);
int tipc_nl_node_reset_link_stats(struct sk_buff *skb, struct genl_info *info);
int tipc_nl_node_get_link(struct sk_buff *skb, struct genl_info *info);
int tipc_nl_node_set_link(struct sk_buff *skb, struct genl_info *info);
int tipc_nl_peer_rm(struct sk_buff *skb, struct genl_info *info);

int tipc_nl_node_set_monitor(struct sk_buff *skb, struct genl_info *info);
int tipc_nl_node_get_monitor(struct sk_buff *skb, struct genl_info *info);
int tipc_nl_node_dump_monitor(struct sk_buff *skb, struct netlink_callback *cb);
int tipc_nl_node_dump_monitor_peer(struct sk_buff *skb,
				   struct netlink_callback *cb);
#endif

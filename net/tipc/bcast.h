/*
 * net/tipc/bcast.h: Include file for TIPC broadcast code
 *
 * Copyright (c) 2003-2006, 2014-2015, Ericsson AB
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

#ifndef _TIPC_BCAST_H
#define _TIPC_BCAST_H

#include "core.h"

struct tipc_node;
struct tipc_msg;
struct tipc_nl_msg;
struct tipc_node_map;

int tipc_bcast_init(struct net *net);
void tipc_bcast_reinit(struct net *net);
void tipc_bcast_stop(struct net *net);
void tipc_bclink_add_node(struct net *net, u32 addr);
void tipc_bclink_remove_node(struct net *net, u32 addr);
struct tipc_node *tipc_bclink_retransmit_to(struct net *tn);
void tipc_bclink_acknowledge(struct tipc_node *n_ptr, u32 acked);
void tipc_bclink_rcv(struct net *net, struct sk_buff *buf);
u32  tipc_bclink_get_last_sent(struct net *net);
u32  tipc_bclink_acks_missing(struct tipc_node *n_ptr);
void tipc_bclink_update_link_state(struct tipc_node *node,
				   u32 last_sent);
void tipc_bcbearer_sort(struct net *net, struct tipc_node_map *nm_ptr,
			u32 node, bool action);
int  tipc_bclink_reset_stats(struct net *net);
int  tipc_bclink_set_queue_limits(struct net *net, u32 limit);
uint  tipc_bcast_get_mtu(void);
int tipc_bcast_xmit(struct net *net, struct sk_buff_head *list);
void tipc_bclink_wakeup_users(struct net *net);
int tipc_nl_add_bc_link(struct net *net, struct tipc_nl_msg *msg);
int tipc_nl_bc_link_set(struct net *net, struct nlattr *attrs[]);
void tipc_bclink_input(struct net *net);
void tipc_bclink_sync_state(struct tipc_node *n, struct tipc_msg *msg);

static inline void tipc_bcast_lock(struct net *net)
{
	spin_lock_bh(&tipc_net(net)->bclock);
}

static inline void tipc_bcast_unlock(struct net *net)
{
	spin_unlock_bh(&tipc_net(net)->bclock);
}

#endif

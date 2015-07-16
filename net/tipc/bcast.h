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

#include <linux/tipc_config.h>
#include "link.h"
#include "node.h"

/**
 * struct tipc_bcbearer_pair - a pair of bearers used by broadcast link
 * @primary: pointer to primary bearer
 * @secondary: pointer to secondary bearer
 *
 * Bearers must have same priority and same set of reachable destinations
 * to be paired.
 */

struct tipc_bcbearer_pair {
	struct tipc_bearer *primary;
	struct tipc_bearer *secondary;
};

#define	BCBEARER		MAX_BEARERS

/**
 * struct tipc_bcbearer - bearer used by broadcast link
 * @bearer: (non-standard) broadcast bearer structure
 * @media: (non-standard) broadcast media structure
 * @bpairs: array of bearer pairs
 * @bpairs_temp: temporary array of bearer pairs used by tipc_bcbearer_sort()
 * @remains: temporary node map used by tipc_bcbearer_send()
 * @remains_new: temporary node map used tipc_bcbearer_send()
 *
 * Note: The fields labelled "temporary" are incorporated into the bearer
 * to avoid consuming potentially limited stack space through the use of
 * large local variables within multicast routines.  Concurrent access is
 * prevented through use of the spinlock "bclink_lock".
 */
struct tipc_bcbearer {
	struct tipc_bearer bearer;
	struct tipc_media media;
	struct tipc_bcbearer_pair bpairs[MAX_BEARERS];
	struct tipc_bcbearer_pair bpairs_temp[TIPC_MAX_LINK_PRI + 1];
	struct tipc_node_map remains;
	struct tipc_node_map remains_new;
};

/**
 * struct tipc_bclink - link used for broadcast messages
 * @lock: spinlock governing access to structure
 * @link: (non-standard) broadcast link structure
 * @node: (non-standard) node structure representing b'cast link's peer node
 * @bcast_nodes: map of broadcast-capable nodes
 * @retransmit_to: node that most recently requested a retransmit
 *
 * Handles sequence numbering, fragmentation, bundling, etc.
 */
struct tipc_bclink {
	spinlock_t lock;
	struct tipc_link link;
	struct tipc_node node;
	struct sk_buff_head arrvq;
	struct sk_buff_head inputq;
	struct tipc_node_map bcast_nodes;
	struct tipc_node *retransmit_to;
};

struct tipc_node;
extern const char tipc_bclink_name[];

/**
 * tipc_nmap_equal - test for equality of node maps
 */
static inline int tipc_nmap_equal(struct tipc_node_map *nm_a,
				  struct tipc_node_map *nm_b)
{
	return !memcmp(nm_a, nm_b, sizeof(*nm_a));
}

int tipc_bclink_init(struct net *net);
void tipc_bclink_stop(struct net *net);
void tipc_bclink_add_node(struct net *net, u32 addr);
void tipc_bclink_remove_node(struct net *net, u32 addr);
struct tipc_node *tipc_bclink_retransmit_to(struct net *tn);
void tipc_bclink_acknowledge(struct tipc_node *n_ptr, u32 acked);
void tipc_bclink_rcv(struct net *net, struct sk_buff *buf);
u32  tipc_bclink_get_last_sent(struct net *net);
u32  tipc_bclink_acks_missing(struct tipc_node *n_ptr);
void tipc_bclink_update_link_state(struct tipc_node *node,
				   u32 last_sent);
int  tipc_bclink_reset_stats(struct net *net);
int  tipc_bclink_set_queue_limits(struct net *net, u32 limit);
void tipc_bcbearer_sort(struct net *net, struct tipc_node_map *nm_ptr,
			u32 node, bool action);
uint  tipc_bclink_get_mtu(void);
int tipc_bclink_xmit(struct net *net, struct sk_buff_head *list);
void tipc_bclink_wakeup_users(struct net *net);
int tipc_nl_add_bc_link(struct net *net, struct tipc_nl_msg *msg);
int tipc_nl_bc_link_set(struct net *net, struct nlattr *attrs[]);
void tipc_bclink_input(struct net *net);

#endif

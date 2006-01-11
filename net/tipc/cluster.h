/*
 * net/tipc/cluster.h: Include file for TIPC cluster management routines
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

#ifndef _TIPC_CLUSTER_H
#define _TIPC_CLUSTER_H

#include "addr.h"
#include "zone.h"

#define LOWEST_SLAVE  2048u

/**
 * struct cluster - TIPC cluster structure
 * @addr: network address of cluster
 * @owner: pointer to zone that cluster belongs to
 * @nodes: array of pointers to all nodes within cluster
 * @highest_node: id of highest numbered node within cluster
 * @highest_slave: (used for secondary node support)
 */
 
struct cluster {
	u32 addr;
	struct _zone *owner;
	struct node **nodes;
	u32 highest_node;
	u32 highest_slave;
};


extern struct node **local_nodes;
extern u32 highest_allowed_slave;
extern struct node_map cluster_bcast_nodes;

void cluster_remove_as_router(struct cluster *c_ptr, u32 router);
void cluster_send_ext_routes(struct cluster *c_ptr, u32 dest);
struct node *cluster_select_node(struct cluster *c_ptr, u32 selector);
u32 cluster_select_router(struct cluster *c_ptr, u32 ref);
void cluster_recv_routing_table(struct sk_buff *buf);
struct cluster *cluster_create(u32 addr);
void cluster_delete(struct cluster *c_ptr);
void cluster_attach_node(struct cluster *c_ptr, struct node *n_ptr);
void cluster_send_slave_routes(struct cluster *c_ptr, u32 dest);
void cluster_broadcast(struct sk_buff *buf);
int cluster_init(void);
u32 cluster_next_node(struct cluster *c_ptr, u32 addr);
void cluster_bcast_new_route(struct cluster *c_ptr, u32 dest, u32 lo, u32 hi);
void cluster_send_local_routes(struct cluster *c_ptr, u32 dest);
void cluster_bcast_lost_route(struct cluster *c_ptr, u32 dest, u32 lo, u32 hi);

static inline struct cluster *cluster_find(u32 addr)
{
	struct _zone *z_ptr = zone_find(addr);

	if (z_ptr)
		return z_ptr->clusters[1];
	return 0;
}

#endif

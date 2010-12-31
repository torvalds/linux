/*
 * net/tipc/cluster.c: TIPC cluster management routines
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
#include "cluster.h"
#include "link.h"

struct tipc_node **tipc_local_nodes = NULL;
struct tipc_node_map tipc_cltr_bcast_nodes = {0,{0,}};

struct cluster *tipc_cltr_create(u32 addr)
{
	struct cluster *c_ptr;
	int max_nodes;

	c_ptr = kzalloc(sizeof(*c_ptr), GFP_ATOMIC);
	if (c_ptr == NULL) {
		warn("Cluster creation failure, no memory\n");
		return NULL;
	}

	c_ptr->addr = tipc_addr(tipc_zone(addr), tipc_cluster(addr), 0);
	max_nodes = tipc_max_nodes + 1;

	c_ptr->nodes = kcalloc(max_nodes + 1, sizeof(void*), GFP_ATOMIC);
	if (c_ptr->nodes == NULL) {
		warn("Cluster creation failure, no memory for node area\n");
		kfree(c_ptr);
		return NULL;
	}

	tipc_local_nodes = c_ptr->nodes;
	c_ptr->highest_node = 0;

	tipc_net.clusters[1] = c_ptr;
	return c_ptr;
}

void tipc_cltr_delete(struct cluster *c_ptr)
{
	u32 n_num;

	if (!c_ptr)
		return;
	for (n_num = 1; n_num <= c_ptr->highest_node; n_num++) {
		tipc_node_delete(c_ptr->nodes[n_num]);
	}
	kfree(c_ptr->nodes);
	kfree(c_ptr);
}


void tipc_cltr_attach_node(struct cluster *c_ptr, struct tipc_node *n_ptr)
{
	u32 n_num = tipc_node(n_ptr->addr);
	u32 max_n_num = tipc_max_nodes;

	assert(n_num > 0);
	assert(n_num <= max_n_num);
	assert(c_ptr->nodes[n_num] == NULL);
	c_ptr->nodes[n_num] = n_ptr;
	if (n_num > c_ptr->highest_node)
		c_ptr->highest_node = n_num;
}

/**
 * tipc_cltr_broadcast - broadcast message to all nodes within cluster
 */

void tipc_cltr_broadcast(struct sk_buff *buf)
{
	struct sk_buff *buf_copy;
	struct cluster *c_ptr;
	struct tipc_node *n_ptr;
	u32 n_num;

	if (tipc_mode == TIPC_NET_MODE) {
		c_ptr = tipc_cltr_find(tipc_own_addr);

		/* Send to nodes */
		for (n_num = 1; n_num <= c_ptr->highest_node; n_num++) {
			n_ptr = c_ptr->nodes[n_num];
			if (n_ptr && tipc_node_has_active_links(n_ptr)) {
				buf_copy = skb_copy(buf, GFP_ATOMIC);
				if (buf_copy == NULL)
					goto exit;
				msg_set_destnode(buf_msg(buf_copy),
						 n_ptr->addr);
				tipc_link_send(buf_copy, n_ptr->addr,
					       n_ptr->addr);
			}
		}
	}
exit:
	buf_discard(buf);
}

int tipc_cltr_init(void)
{
	return tipc_cltr_create(tipc_own_addr) ? 0 : -ENOMEM;
}


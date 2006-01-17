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
#include "addr.h"
#include "node_subscr.h"
#include "link.h"
#include "node.h"
#include "net.h"
#include "msg.h"
#include "bearer.h"

void tipc_cltr_multicast(struct cluster *c_ptr, struct sk_buff *buf, 
			 u32 lower, u32 upper);
struct sk_buff *tipc_cltr_prepare_routing_msg(u32 data_size, u32 dest);

struct node **tipc_local_nodes = 0;
struct node_map tipc_cltr_bcast_nodes = {0,{0,}};
u32 tipc_highest_allowed_slave = 0;

struct cluster *tipc_cltr_create(u32 addr)
{
	struct _zone *z_ptr;
	struct cluster *c_ptr;
	int max_nodes; 
	int alloc;

	c_ptr = (struct cluster *)kmalloc(sizeof(*c_ptr), GFP_ATOMIC);
	if (c_ptr == NULL)
		return 0;
	memset(c_ptr, 0, sizeof(*c_ptr));

	c_ptr->addr = tipc_addr(tipc_zone(addr), tipc_cluster(addr), 0);
	if (in_own_cluster(addr))
		max_nodes = LOWEST_SLAVE + tipc_max_slaves;
	else
		max_nodes = tipc_max_nodes + 1;
	alloc = sizeof(void *) * (max_nodes + 1);
	c_ptr->nodes = (struct node **)kmalloc(alloc, GFP_ATOMIC);
	if (c_ptr->nodes == NULL) {
		kfree(c_ptr);
		return 0;
	}
	memset(c_ptr->nodes, 0, alloc);  
	if (in_own_cluster(addr))
		tipc_local_nodes = c_ptr->nodes;
	c_ptr->highest_slave = LOWEST_SLAVE - 1;
	c_ptr->highest_node = 0;
	
	z_ptr = tipc_zone_find(tipc_zone(addr));
	if (z_ptr == NULL) {
		z_ptr = tipc_zone_create(addr);
	}
	if (z_ptr != NULL) {
		tipc_zone_attach_cluster(z_ptr, c_ptr);
		c_ptr->owner = z_ptr;
	}
	else {
		kfree(c_ptr);
		c_ptr = 0;
	}

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
	for (n_num = LOWEST_SLAVE; n_num <= c_ptr->highest_slave; n_num++) {
		tipc_node_delete(c_ptr->nodes[n_num]);
	}
	kfree(c_ptr->nodes);
	kfree(c_ptr);
}

u32 tipc_cltr_next_node(struct cluster *c_ptr, u32 addr)
{
	struct node *n_ptr;
	u32 n_num = tipc_node(addr) + 1;

	if (!c_ptr)
		return addr;
	for (; n_num <= c_ptr->highest_node; n_num++) {
		n_ptr = c_ptr->nodes[n_num];
		if (n_ptr && tipc_node_has_active_links(n_ptr))
			return n_ptr->addr;
	}
	for (n_num = 1; n_num < tipc_node(addr); n_num++) {
		n_ptr = c_ptr->nodes[n_num];
		if (n_ptr && tipc_node_has_active_links(n_ptr))
			return n_ptr->addr;
	}
	return 0;
}

void tipc_cltr_attach_node(struct cluster *c_ptr, struct node *n_ptr)
{
	u32 n_num = tipc_node(n_ptr->addr);
	u32 max_n_num = tipc_max_nodes;

	if (in_own_cluster(n_ptr->addr))
		max_n_num = tipc_highest_allowed_slave;
	assert(n_num > 0);
	assert(n_num <= max_n_num);
	assert(c_ptr->nodes[n_num] == 0);
	c_ptr->nodes[n_num] = n_ptr;
	if (n_num > c_ptr->highest_node)
		c_ptr->highest_node = n_num;
}

/**
 * tipc_cltr_select_router - select router to a cluster
 * 
 * Uses deterministic and fair algorithm.
 */

u32 tipc_cltr_select_router(struct cluster *c_ptr, u32 ref)
{
	u32 n_num;
	u32 ulim = c_ptr->highest_node;
	u32 mask;
	u32 tstart;

	assert(!in_own_cluster(c_ptr->addr));
	if (!ulim)
		return 0;

	/* Start entry must be random */
	mask = tipc_max_nodes;
	while (mask > ulim)
		mask >>= 1;
	tstart = ref & mask;
	n_num = tstart;

	/* Lookup upwards with wrap-around */
	do {
		if (tipc_node_is_up(c_ptr->nodes[n_num]))
			break;
	} while (++n_num <= ulim);
	if (n_num > ulim) {
		n_num = 1;
		do {
			if (tipc_node_is_up(c_ptr->nodes[n_num]))
				break;
		} while (++n_num < tstart);
		if (n_num == tstart)
			return 0;
	}
	assert(n_num <= ulim);
	return tipc_node_select_router(c_ptr->nodes[n_num], ref);
}

/**
 * tipc_cltr_select_node - select destination node within a remote cluster
 * 
 * Uses deterministic and fair algorithm.
 */

struct node *tipc_cltr_select_node(struct cluster *c_ptr, u32 selector)
{
	u32 n_num;
	u32 mask = tipc_max_nodes;
	u32 start_entry;

	assert(!in_own_cluster(c_ptr->addr));
	if (!c_ptr->highest_node)
		return 0;

	/* Start entry must be random */
	while (mask > c_ptr->highest_node) {
		mask >>= 1;
	}
	start_entry = (selector & mask) ? selector & mask : 1u;
	assert(start_entry <= c_ptr->highest_node);

	/* Lookup upwards with wrap-around */
	for (n_num = start_entry; n_num <= c_ptr->highest_node; n_num++) {
		if (tipc_node_has_active_links(c_ptr->nodes[n_num]))
			return c_ptr->nodes[n_num];
	}
	for (n_num = 1; n_num < start_entry; n_num++) {
		if (tipc_node_has_active_links(c_ptr->nodes[n_num]))
			return c_ptr->nodes[n_num];
	}
	return 0;
}

/*
 *    Routing table management: See description in node.c
 */

struct sk_buff *tipc_cltr_prepare_routing_msg(u32 data_size, u32 dest)
{
	u32 size = INT_H_SIZE + data_size;
	struct sk_buff *buf = buf_acquire(size);
	struct tipc_msg *msg;

	if (buf) {
		msg = buf_msg(buf);
		memset((char *)msg, 0, size);
		msg_init(msg, ROUTE_DISTRIBUTOR, 0, TIPC_OK, INT_H_SIZE, dest);
	}
	return buf;
}

void tipc_cltr_bcast_new_route(struct cluster *c_ptr, u32 dest,
			     u32 lower, u32 upper)
{
	struct sk_buff *buf = tipc_cltr_prepare_routing_msg(0, c_ptr->addr);
	struct tipc_msg *msg;

	if (buf) {
		msg = buf_msg(buf);
		msg_set_remote_node(msg, dest);
		msg_set_type(msg, ROUTE_ADDITION);
		tipc_cltr_multicast(c_ptr, buf, lower, upper);
	} else {
		warn("Memory squeeze: broadcast of new route failed\n");
	}
}

void tipc_cltr_bcast_lost_route(struct cluster *c_ptr, u32 dest,
				u32 lower, u32 upper)
{
	struct sk_buff *buf = tipc_cltr_prepare_routing_msg(0, c_ptr->addr);
	struct tipc_msg *msg;

	if (buf) {
		msg = buf_msg(buf);
		msg_set_remote_node(msg, dest);
		msg_set_type(msg, ROUTE_REMOVAL);
		tipc_cltr_multicast(c_ptr, buf, lower, upper);
	} else {
		warn("Memory squeeze: broadcast of lost route failed\n");
	}
}

void tipc_cltr_send_slave_routes(struct cluster *c_ptr, u32 dest)
{
	struct sk_buff *buf;
	struct tipc_msg *msg;
	u32 highest = c_ptr->highest_slave;
	u32 n_num;
	int send = 0;

	assert(!is_slave(dest));
	assert(in_own_cluster(dest));
	assert(in_own_cluster(c_ptr->addr));
	if (highest <= LOWEST_SLAVE)
		return;
	buf = tipc_cltr_prepare_routing_msg(highest - LOWEST_SLAVE + 1,
					    c_ptr->addr);
	if (buf) {
		msg = buf_msg(buf);
		msg_set_remote_node(msg, c_ptr->addr);
		msg_set_type(msg, SLAVE_ROUTING_TABLE);
		for (n_num = LOWEST_SLAVE; n_num <= highest; n_num++) {
			if (c_ptr->nodes[n_num] && 
			    tipc_node_has_active_links(c_ptr->nodes[n_num])) {
				send = 1;
				msg_set_dataoctet(msg, n_num);
			}
		}
		if (send)
			tipc_link_send(buf, dest, dest);
		else
			buf_discard(buf);
	} else {
		warn("Memory squeeze: broadcast of lost route failed\n");
	}
}

void tipc_cltr_send_ext_routes(struct cluster *c_ptr, u32 dest)
{
	struct sk_buff *buf;
	struct tipc_msg *msg;
	u32 highest = c_ptr->highest_node;
	u32 n_num;
	int send = 0;

	if (in_own_cluster(c_ptr->addr))
		return;
	assert(!is_slave(dest));
	assert(in_own_cluster(dest));
	highest = c_ptr->highest_node;
	buf = tipc_cltr_prepare_routing_msg(highest + 1, c_ptr->addr);
	if (buf) {
		msg = buf_msg(buf);
		msg_set_remote_node(msg, c_ptr->addr);
		msg_set_type(msg, EXT_ROUTING_TABLE);
		for (n_num = 1; n_num <= highest; n_num++) {
			if (c_ptr->nodes[n_num] && 
			    tipc_node_has_active_links(c_ptr->nodes[n_num])) {
				send = 1;
				msg_set_dataoctet(msg, n_num);
			}
		}
		if (send)
			tipc_link_send(buf, dest, dest);
		else
			buf_discard(buf);
	} else {
		warn("Memory squeeze: broadcast of external route failed\n");
	}
}

void tipc_cltr_send_local_routes(struct cluster *c_ptr, u32 dest)
{
	struct sk_buff *buf;
	struct tipc_msg *msg;
	u32 highest = c_ptr->highest_node;
	u32 n_num;
	int send = 0;

	assert(is_slave(dest));
	assert(in_own_cluster(c_ptr->addr));
	buf = tipc_cltr_prepare_routing_msg(highest, c_ptr->addr);
	if (buf) {
		msg = buf_msg(buf);
		msg_set_remote_node(msg, c_ptr->addr);
		msg_set_type(msg, LOCAL_ROUTING_TABLE);
		for (n_num = 1; n_num <= highest; n_num++) {
			if (c_ptr->nodes[n_num] && 
			    tipc_node_has_active_links(c_ptr->nodes[n_num])) {
				send = 1;
				msg_set_dataoctet(msg, n_num);
			}
		}
		if (send)
			tipc_link_send(buf, dest, dest);
		else
			buf_discard(buf);
	} else {
		warn("Memory squeeze: broadcast of local route failed\n");
	}
}

void tipc_cltr_recv_routing_table(struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	struct cluster *c_ptr;
	struct node *n_ptr;
	unchar *node_table;
	u32 table_size;
	u32 router;
	u32 rem_node = msg_remote_node(msg);
	u32 z_num;
	u32 c_num;
	u32 n_num;

	c_ptr = tipc_cltr_find(rem_node);
	if (!c_ptr) {
		c_ptr = tipc_cltr_create(rem_node);
		if (!c_ptr) {
			buf_discard(buf);
			return;
		}
	}

	node_table = buf->data + msg_hdr_sz(msg);
	table_size = msg_size(msg) - msg_hdr_sz(msg);
	router = msg_prevnode(msg);
	z_num = tipc_zone(rem_node);
	c_num = tipc_cluster(rem_node);

	switch (msg_type(msg)) {
	case LOCAL_ROUTING_TABLE:
		assert(is_slave(tipc_own_addr));
	case EXT_ROUTING_TABLE:
		for (n_num = 1; n_num < table_size; n_num++) {
			if (node_table[n_num]) {
				u32 addr = tipc_addr(z_num, c_num, n_num);
				n_ptr = c_ptr->nodes[n_num];
				if (!n_ptr) {
					n_ptr = tipc_node_create(addr);
				}
				if (n_ptr)
					tipc_node_add_router(n_ptr, router);
			}
		}
		break;
	case SLAVE_ROUTING_TABLE:
		assert(!is_slave(tipc_own_addr));
		assert(in_own_cluster(c_ptr->addr));
		for (n_num = 1; n_num < table_size; n_num++) {
			if (node_table[n_num]) {
				u32 slave_num = n_num + LOWEST_SLAVE;
				u32 addr = tipc_addr(z_num, c_num, slave_num);
				n_ptr = c_ptr->nodes[slave_num];
				if (!n_ptr) {
					n_ptr = tipc_node_create(addr);
				}
				if (n_ptr)
					tipc_node_add_router(n_ptr, router);
			}
		}
		break;
	case ROUTE_ADDITION:
		if (!is_slave(tipc_own_addr)) {
			assert(!in_own_cluster(c_ptr->addr)
			       || is_slave(rem_node));
		} else {
			assert(in_own_cluster(c_ptr->addr)
			       && !is_slave(rem_node));
		}
		n_ptr = c_ptr->nodes[tipc_node(rem_node)];
		if (!n_ptr)
			n_ptr = tipc_node_create(rem_node);
		if (n_ptr)
			tipc_node_add_router(n_ptr, router);
		break;
	case ROUTE_REMOVAL:
		if (!is_slave(tipc_own_addr)) {
			assert(!in_own_cluster(c_ptr->addr)
			       || is_slave(rem_node));
		} else {
			assert(in_own_cluster(c_ptr->addr)
			       && !is_slave(rem_node));
		}
		n_ptr = c_ptr->nodes[tipc_node(rem_node)];
		if (n_ptr)
			tipc_node_remove_router(n_ptr, router);
		break;
	default:
		assert(!"Illegal routing manager message received\n");
	}
	buf_discard(buf);
}

void tipc_cltr_remove_as_router(struct cluster *c_ptr, u32 router)
{
	u32 start_entry;
	u32 tstop;
	u32 n_num;

	if (is_slave(router))
		return;	/* Slave nodes can not be routers */

	if (in_own_cluster(c_ptr->addr)) {
		start_entry = LOWEST_SLAVE;
		tstop = c_ptr->highest_slave;
	} else {
		start_entry = 1;
		tstop = c_ptr->highest_node;
	}

	for (n_num = start_entry; n_num <= tstop; n_num++) {
		if (c_ptr->nodes[n_num]) {
			tipc_node_remove_router(c_ptr->nodes[n_num], router);
		}
	}
}

/**
 * tipc_cltr_multicast - multicast message to local nodes 
 */

void tipc_cltr_multicast(struct cluster *c_ptr, struct sk_buff *buf, 
			 u32 lower, u32 upper)
{
	struct sk_buff *buf_copy;
	struct node *n_ptr;
	u32 n_num;
	u32 tstop;

	assert(lower <= upper);
	assert(((lower >= 1) && (lower <= tipc_max_nodes)) ||
	       ((lower >= LOWEST_SLAVE) && (lower <= tipc_highest_allowed_slave)));
	assert(((upper >= 1) && (upper <= tipc_max_nodes)) ||
	       ((upper >= LOWEST_SLAVE) && (upper <= tipc_highest_allowed_slave)));
	assert(in_own_cluster(c_ptr->addr));

	tstop = is_slave(upper) ? c_ptr->highest_slave : c_ptr->highest_node;
	if (tstop > upper)
		tstop = upper;
	for (n_num = lower; n_num <= tstop; n_num++) {
		n_ptr = c_ptr->nodes[n_num];
		if (n_ptr && tipc_node_has_active_links(n_ptr)) {
			buf_copy = skb_copy(buf, GFP_ATOMIC);
			if (buf_copy == NULL)
				break;
			msg_set_destnode(buf_msg(buf_copy), n_ptr->addr);
			tipc_link_send(buf_copy, n_ptr->addr, n_ptr->addr);
		}
	}
	buf_discard(buf);
}

/**
 * tipc_cltr_broadcast - broadcast message to all nodes within cluster
 */

void tipc_cltr_broadcast(struct sk_buff *buf)
{
	struct sk_buff *buf_copy;
	struct cluster *c_ptr;
	struct node *n_ptr;
	u32 n_num;
	u32 tstart;
	u32 tstop;
	u32 node_type;

	if (tipc_mode == TIPC_NET_MODE) {
		c_ptr = tipc_cltr_find(tipc_own_addr);
		assert(in_own_cluster(c_ptr->addr));	/* For now */

		/* Send to standard nodes, then repeat loop sending to slaves */
		tstart = 1;
		tstop = c_ptr->highest_node;
		for (node_type = 1; node_type <= 2; node_type++) {
			for (n_num = tstart; n_num <= tstop; n_num++) {
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
			tstart = LOWEST_SLAVE;
			tstop = c_ptr->highest_slave;
		}
	}
exit:
	buf_discard(buf);
}

int tipc_cltr_init(void)
{
	tipc_highest_allowed_slave = LOWEST_SLAVE + tipc_max_slaves;
	return tipc_cltr_create(tipc_own_addr) ? TIPC_OK : -ENOMEM;
}


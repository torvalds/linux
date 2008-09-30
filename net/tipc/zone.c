/*
 * net/tipc/zone.c: TIPC zone management routines
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
#include "zone.h"
#include "net.h"
#include "addr.h"
#include "node_subscr.h"
#include "cluster.h"
#include "node.h"

struct _zone *tipc_zone_create(u32 addr)
{
	struct _zone *z_ptr;
	u32 z_num;

	if (!tipc_addr_domain_valid(addr)) {
		err("Zone creation failed, invalid domain 0x%x\n", addr);
		return NULL;
	}

	z_ptr = kzalloc(sizeof(*z_ptr), GFP_ATOMIC);
	if (!z_ptr) {
		warn("Zone creation failed, insufficient memory\n");
		return NULL;
	}

	z_num = tipc_zone(addr);
	z_ptr->addr = tipc_addr(z_num, 0, 0);
	tipc_net.zones[z_num] = z_ptr;
	return z_ptr;
}

void tipc_zone_delete(struct _zone *z_ptr)
{
	u32 c_num;

	if (!z_ptr)
		return;
	for (c_num = 1; c_num <= tipc_max_clusters; c_num++) {
		tipc_cltr_delete(z_ptr->clusters[c_num]);
	}
	kfree(z_ptr);
}

void tipc_zone_attach_cluster(struct _zone *z_ptr, struct cluster *c_ptr)
{
	u32 c_num = tipc_cluster(c_ptr->addr);

	assert(c_ptr->addr);
	assert(c_num <= tipc_max_clusters);
	assert(z_ptr->clusters[c_num] == NULL);
	z_ptr->clusters[c_num] = c_ptr;
}

void tipc_zone_remove_as_router(struct _zone *z_ptr, u32 router)
{
	u32 c_num;

	for (c_num = 1; c_num <= tipc_max_clusters; c_num++) {
		if (z_ptr->clusters[c_num]) {
			tipc_cltr_remove_as_router(z_ptr->clusters[c_num],
						   router);
		}
	}
}

void tipc_zone_send_external_routes(struct _zone *z_ptr, u32 dest)
{
	u32 c_num;

	for (c_num = 1; c_num <= tipc_max_clusters; c_num++) {
		if (z_ptr->clusters[c_num]) {
			if (in_own_cluster(z_ptr->addr))
				continue;
			tipc_cltr_send_ext_routes(z_ptr->clusters[c_num], dest);
		}
	}
}

struct tipc_node *tipc_zone_select_remote_node(struct _zone *z_ptr, u32 addr, u32 ref)
{
	struct cluster *c_ptr;
	struct tipc_node *n_ptr;
	u32 c_num;

	if (!z_ptr)
		return NULL;
	c_ptr = z_ptr->clusters[tipc_cluster(addr)];
	if (!c_ptr)
		return NULL;
	n_ptr = tipc_cltr_select_node(c_ptr, ref);
	if (n_ptr)
		return n_ptr;

	/* Links to any other clusters within this zone ? */
	for (c_num = 1; c_num <= tipc_max_clusters; c_num++) {
		c_ptr = z_ptr->clusters[c_num];
		if (!c_ptr)
			return NULL;
		n_ptr = tipc_cltr_select_node(c_ptr, ref);
		if (n_ptr)
			return n_ptr;
	}
	return NULL;
}

u32 tipc_zone_select_router(struct _zone *z_ptr, u32 addr, u32 ref)
{
	struct cluster *c_ptr;
	u32 c_num;
	u32 router;

	if (!z_ptr)
		return 0;
	c_ptr = z_ptr->clusters[tipc_cluster(addr)];
	router = c_ptr ? tipc_cltr_select_router(c_ptr, ref) : 0;
	if (router)
		return router;

	/* Links to any other clusters within the zone? */
	for (c_num = 1; c_num <= tipc_max_clusters; c_num++) {
		c_ptr = z_ptr->clusters[c_num];
		router = c_ptr ? tipc_cltr_select_router(c_ptr, ref) : 0;
		if (router)
			return router;
	}
	return 0;
}


u32 tipc_zone_next_node(u32 addr)
{
	struct cluster *c_ptr = tipc_cltr_find(addr);

	if (c_ptr)
		return tipc_cltr_next_node(c_ptr, addr);
	return 0;
}


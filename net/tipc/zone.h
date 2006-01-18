/*
 * net/tipc/zone.h: Include file for TIPC zone management routines
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

#ifndef _TIPC_ZONE_H
#define _TIPC_ZONE_H

#include "node_subscr.h"
#include "net.h"


/**
 * struct _zone - TIPC zone structure
 * @addr: network address of zone
 * @clusters: array of pointers to all clusters within zone
 * @links: (used for inter-zone communication)
 */
 
struct _zone {
	u32 addr;
	struct cluster *clusters[2]; /* currently limited to just 1 cluster */
	u32 links;
};

struct node *zone_select_remote_node(struct _zone *z_ptr, u32 addr, u32 ref);
u32 zone_select_router(struct _zone *z_ptr, u32 addr, u32 ref);
void zone_remove_as_router(struct _zone *z_ptr, u32 router);
void zone_send_external_routes(struct _zone *z_ptr, u32 dest);
struct _zone *zone_create(u32 addr);
void zone_delete(struct _zone *z_ptr);
void zone_attach_cluster(struct _zone *z_ptr, struct cluster *c_ptr);
u32 zone_next_node(u32 addr);

static inline struct _zone *zone_find(u32 addr)
{
	return net.zones[tipc_zone(addr)];
}

#endif

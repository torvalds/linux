/*
 * net/tipc/node_subscr.c: TIPC "node down" subscription handling
 * 
 * Copyright (c) 1995-2006, Ericsson AB
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
#include "dbg.h"
#include "node_subscr.h"
#include "node.h"
#include "addr.h"

/**
 * tipc_nodesub_subscribe - create "node down" subscription for specified node
 */

void tipc_nodesub_subscribe(struct node_subscr *node_sub, u32 addr, 
		       void *usr_handle, net_ev_handler handle_down)
{
	node_sub->node = NULL;
	if (addr == tipc_own_addr)
		return;
	if (!tipc_addr_node_valid(addr)) {
		warn("node_subscr with illegal %x\n", addr);
		return;
	}

	node_sub->handle_node_down = handle_down;
	node_sub->usr_handle = usr_handle;
	node_sub->node = tipc_node_find(addr);
	assert(node_sub->node);
	tipc_node_lock(node_sub->node);
	list_add_tail(&node_sub->nodesub_list, &node_sub->node->nsub);
	tipc_node_unlock(node_sub->node);
}

/**
 * tipc_nodesub_unsubscribe - cancel "node down" subscription (if any)
 */

void tipc_nodesub_unsubscribe(struct node_subscr *node_sub)
{
	if (!node_sub->node)
		return;

	tipc_node_lock(node_sub->node);
	list_del_init(&node_sub->nodesub_list);
	tipc_node_unlock(node_sub->node);
}

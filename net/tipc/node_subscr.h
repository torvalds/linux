/*
 * net/tipc/node_subscr.h: Include file for TIPC "node down" subscription handling
 *
 * Copyright (c) 1995-2006, Ericsson AB
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

#ifndef _TIPC_NODE_SUBSCR_H
#define _TIPC_NODE_SUBSCR_H

#include "addr.h"

typedef void (*net_ev_handler) (void *usr_handle);

/**
 * struct tipc_node_subscr - "node down" subscription entry
 * @node: ptr to node structure of interest (or NULL, if none)
 * @handle_node_down: routine to invoke when node fails
 * @usr_handle: argument to pass to routine when node fails
 * @nodesub_list: adjacent entries in list of subscriptions for the node
 */
struct tipc_node_subscr {
	struct tipc_node *node;
	net_ev_handler handle_node_down;
	void *usr_handle;
	struct list_head nodesub_list;
};

void tipc_nodesub_subscribe(struct tipc_node_subscr *node_sub, u32 addr,
			    void *usr_handle, net_ev_handler handle_down);
void tipc_nodesub_unsubscribe(struct tipc_node_subscr *node_sub);
void tipc_nodesub_notify(struct tipc_node *node);

#endif

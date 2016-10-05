/*
 * net/tipc/monitor.h
 *
 * Copyright (c) 2015, Ericsson AB
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

#ifndef _TIPC_MONITOR_H
#define _TIPC_MONITOR_H

#include "netlink.h"

/* struct tipc_mon_state: link instance's cache of monitor list and domain state
 * @list_gen: current generation of this node's monitor list
 * @gen: current generation of this node's local domain
 * @peer_gen: most recent domain generation received from peer
 * @acked_gen: most recent generation of self's domain acked by peer
 * @monitoring: this peer endpoint should continuously monitored
 * @probing: peer endpoint should be temporarily probed for potential loss
 * @synched: domain record's generation has been synched with peer after reset
 */
struct tipc_mon_state {
	u16 list_gen;
	u16 peer_gen;
	u16 acked_gen;
	bool monitoring :1;
	bool probing    :1;
	bool reset      :1;
	bool synched    :1;
};

int tipc_mon_create(struct net *net, int bearer_id);
void tipc_mon_delete(struct net *net, int bearer_id);

void tipc_mon_peer_up(struct net *net, u32 addr, int bearer_id);
void tipc_mon_peer_down(struct net *net, u32 addr, int bearer_id);
void tipc_mon_prep(struct net *net, void *data, int *dlen,
		   struct tipc_mon_state *state, int bearer_id);
void tipc_mon_rcv(struct net *net, void *data, u16 dlen, u32 addr,
		  struct tipc_mon_state *state, int bearer_id);
void tipc_mon_get_state(struct net *net, u32 addr,
			struct tipc_mon_state *state,
			int bearer_id);
void tipc_mon_remove_peer(struct net *net, u32 addr, int bearer_id);

int tipc_nl_monitor_set_threshold(struct net *net, u32 cluster_size);
int tipc_nl_monitor_get_threshold(struct net *net);
int __tipc_nl_add_monitor(struct net *net, struct tipc_nl_msg *msg,
			  u32 bearer_id);
int tipc_nl_add_monitor_peer(struct net *net, struct tipc_nl_msg *msg,
			     u32 bearer_id, u32 *prev_node);

extern const int tipc_max_domain_size;
#endif

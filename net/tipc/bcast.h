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
struct tipc_nlist;
struct tipc_nitem;
extern const char tipc_bclink_name[];
extern unsigned long sysctl_tipc_bc_retruni;

#define TIPC_METHOD_EXPIRE msecs_to_jiffies(5000)

#define BCLINK_MODE_BCAST  0x1
#define BCLINK_MODE_RCAST  0x2
#define BCLINK_MODE_SEL    0x4

struct tipc_nlist {
	struct list_head list;
	u32 self;
	u16 remote;
	bool local;
};

void tipc_nlist_init(struct tipc_nlist *nl, u32 self);
void tipc_nlist_purge(struct tipc_nlist *nl);
void tipc_nlist_add(struct tipc_nlist *nl, u32 node);
void tipc_nlist_del(struct tipc_nlist *nl, u32 node);

/* Cookie to be used between socket and broadcast layer
 * @rcast: replicast (instead of broadcast) was used at previous xmit
 * @mandatory: broadcast/replicast indication was set by user
 * @deferredq: defer queue to make message in order
 * @expires: re-evaluate non-mandatory transmit method if we are past this
 */
struct tipc_mc_method {
	bool rcast;
	bool mandatory;
	struct sk_buff_head deferredq;
	unsigned long expires;
};

int tipc_bcast_init(struct net *net);
void tipc_bcast_stop(struct net *net);
void tipc_bcast_add_peer(struct net *net, struct tipc_link *l,
			 struct sk_buff_head *xmitq);
void tipc_bcast_remove_peer(struct net *net, struct tipc_link *rcv_bcl);
void tipc_bcast_inc_bearer_dst_cnt(struct net *net, int bearer_id);
void tipc_bcast_dec_bearer_dst_cnt(struct net *net, int bearer_id);
int  tipc_bcast_get_mtu(struct net *net);
void tipc_bcast_toggle_rcast(struct net *net, bool supp);
int tipc_mcast_xmit(struct net *net, struct sk_buff_head *pkts,
		    struct tipc_mc_method *method, struct tipc_nlist *dests,
		    u16 *cong_link_cnt);
int tipc_bcast_rcv(struct net *net, struct tipc_link *l, struct sk_buff *skb);
void tipc_bcast_ack_rcv(struct net *net, struct tipc_link *l,
			struct tipc_msg *hdr);
int tipc_bcast_sync_rcv(struct net *net, struct tipc_link *l,
			struct tipc_msg *hdr,
			struct sk_buff_head *retrq);
int tipc_nl_add_bc_link(struct net *net, struct tipc_nl_msg *msg,
			struct tipc_link *bcl);
int tipc_nl_bc_link_set(struct net *net, struct nlattr *attrs[]);
int tipc_bclink_reset_stats(struct net *net, struct tipc_link *l);

u32 tipc_bcast_get_broadcast_mode(struct net *net);
u32 tipc_bcast_get_broadcast_ratio(struct net *net);

void tipc_mcast_filter_msg(struct net *net, struct sk_buff_head *defq,
			   struct sk_buff_head *inputq);

static inline void tipc_bcast_lock(struct net *net)
{
	spin_lock_bh(&tipc_net(net)->bclock);
}

static inline void tipc_bcast_unlock(struct net *net)
{
	spin_unlock_bh(&tipc_net(net)->bclock);
}

static inline struct tipc_link *tipc_bc_sndlink(struct net *net)
{
	return tipc_net(net)->bcl;
}

#endif

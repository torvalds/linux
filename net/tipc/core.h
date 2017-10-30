/*
 * net/tipc/core.h: Include file for TIPC global declarations
 *
 * Copyright (c) 2005-2006, 2013 Ericsson AB
 * Copyright (c) 2005-2007, 2010-2013, Wind River Systems
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

#ifndef _TIPC_CORE_H
#define _TIPC_CORE_H

#include <linux/tipc.h>
#include <linux/tipc_config.h>
#include <linux/tipc_netlink.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/atomic.h>
#include <asm/hardirq.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <net/netns/generic.h>
#include <linux/rhashtable.h>

struct tipc_node;
struct tipc_bearer;
struct tipc_bc_base;
struct tipc_link;
struct tipc_name_table;
struct tipc_server;
struct tipc_monitor;

#define TIPC_MOD_VER "2.0.0"

#define NODE_HTABLE_SIZE       512
#define MAX_BEARERS	         3
#define TIPC_DEF_MON_THRESHOLD  32

extern unsigned int tipc_net_id __read_mostly;
extern int sysctl_tipc_rmem[3] __read_mostly;
extern int sysctl_tipc_named_timeout __read_mostly;

struct tipc_net {
	u32 own_addr;
	int net_id;
	int random;

	/* Node table and node list */
	spinlock_t node_list_lock;
	struct hlist_head node_htable[NODE_HTABLE_SIZE];
	struct list_head node_list;
	u32 num_nodes;
	u32 num_links;

	/* Neighbor monitoring list */
	struct tipc_monitor *monitors[MAX_BEARERS];
	int mon_threshold;

	/* Bearer list */
	struct tipc_bearer __rcu *bearer_list[MAX_BEARERS + 1];

	/* Broadcast link */
	spinlock_t bclock;
	struct tipc_bc_base *bcbase;
	struct tipc_link *bcl;

	/* Socket hash table */
	struct rhashtable sk_rht;

	/* Name table */
	spinlock_t nametbl_lock;
	struct name_table *nametbl;

	/* Name dist queue */
	struct list_head dist_queue;

	/* Topology subscription server */
	struct tipc_server *topsrv;
	atomic_t subscription_count;
};

static inline struct tipc_net *tipc_net(struct net *net)
{
	return net_generic(net, tipc_net_id);
}

static inline int tipc_netid(struct net *net)
{
	return tipc_net(net)->net_id;
}

static inline struct list_head *tipc_nodes(struct net *net)
{
	return &tipc_net(net)->node_list;
}

static inline unsigned int tipc_hashfn(u32 addr)
{
	return addr & (NODE_HTABLE_SIZE - 1);
}

static inline u16 mod(u16 x)
{
	return x & 0xffffu;
}

static inline int less_eq(u16 left, u16 right)
{
	return mod(right - left) < 32768u;
}

static inline int more(u16 left, u16 right)
{
	return !less_eq(left, right);
}

static inline int less(u16 left, u16 right)
{
	return less_eq(left, right) && (mod(right) != mod(left));
}

static inline int in_range(u16 val, u16 min, u16 max)
{
	return !less(val, min) && !more(val, max);
}

#ifdef CONFIG_SYSCTL
int tipc_register_sysctl(void);
void tipc_unregister_sysctl(void);
#else
#define tipc_register_sysctl() 0
#define tipc_unregister_sysctl()
#endif
#endif

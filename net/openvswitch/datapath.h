/*
 * Copyright (c) 2007-2011 Nicira Networks.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifndef DATAPATH_H
#define DATAPATH_H 1

#include <asm/page.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/u64_stats_sync.h>

#include "flow.h"

struct vport;

#define DP_MAX_PORTS 1024
#define SAMPLE_ACTION_DEPTH 3

/**
 * struct dp_stats_percpu - per-cpu packet processing statistics for a given
 * datapath.
 * @n_hit: Number of received packets for which a matching flow was found in
 * the flow table.
 * @n_miss: Number of received packets that had no matching flow in the flow
 * table.  The sum of @n_hit and @n_miss is the number of packets that have
 * been received by the datapath.
 * @n_lost: Number of received packets that had no matching flow in the flow
 * table that could not be sent to userspace (normally due to an overflow in
 * one of the datapath's queues).
 */
struct dp_stats_percpu {
	u64 n_hit;
	u64 n_missed;
	u64 n_lost;
	struct u64_stats_sync sync;
};

/**
 * struct datapath - datapath for flow-based packet switching
 * @rcu: RCU callback head for deferred destruction.
 * @list_node: Element in global 'dps' list.
 * @n_flows: Number of flows currently in flow table.
 * @table: Current flow table.  Protected by genl_lock and RCU.
 * @ports: Map from port number to &struct vport.  %OVSP_LOCAL port
 * always exists, other ports may be %NULL.  Protected by RTNL and RCU.
 * @port_list: List of all ports in @ports in arbitrary order.  RTNL required
 * to iterate or modify.
 * @stats_percpu: Per-CPU datapath statistics.
 *
 * Context: See the comment on locking at the top of datapath.c for additional
 * locking information.
 */
struct datapath {
	struct rcu_head rcu;
	struct list_head list_node;

	/* Flow table. */
	struct flow_table __rcu *table;

	/* Switch ports. */
	struct vport __rcu *ports[DP_MAX_PORTS];
	struct list_head port_list;

	/* Stats. */
	struct dp_stats_percpu __percpu *stats_percpu;
};

/**
 * struct ovs_skb_cb - OVS data in skb CB
 * @flow: The flow associated with this packet.  May be %NULL if no flow.
 */
struct ovs_skb_cb {
	struct sw_flow		*flow;
};
#define OVS_CB(skb) ((struct ovs_skb_cb *)(skb)->cb)

/**
 * struct dp_upcall - metadata to include with a packet to send to userspace
 * @cmd: One of %OVS_PACKET_CMD_*.
 * @key: Becomes %OVS_PACKET_ATTR_KEY.  Must be nonnull.
 * @userdata: If nonnull, its u64 value is extracted and passed to userspace as
 * %OVS_PACKET_ATTR_USERDATA.
 * @pid: Netlink PID to which packet should be sent.  If @pid is 0 then no
 * packet is sent and the packet is accounted in the datapath's @n_lost
 * counter.
 */
struct dp_upcall_info {
	u8 cmd;
	const struct sw_flow_key *key;
	const struct nlattr *userdata;
	u32 pid;
};

extern struct notifier_block ovs_dp_device_notifier;
extern struct genl_multicast_group ovs_dp_vport_multicast_group;

void ovs_dp_process_received_packet(struct vport *, struct sk_buff *);
void ovs_dp_detach_port(struct vport *);
int ovs_dp_upcall(struct datapath *, struct sk_buff *,
		  const struct dp_upcall_info *);

const char *ovs_dp_name(const struct datapath *dp);
struct sk_buff *ovs_vport_cmd_build_info(struct vport *, u32 pid, u32 seq,
					 u8 cmd);

int ovs_execute_actions(struct datapath *dp, struct sk_buff *skb);
#endif /* datapath.h */

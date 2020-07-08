/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2007-2014 Nicira, Inc.
 */

#ifndef DATAPATH_H
#define DATAPATH_H 1

#include <asm/page.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/u64_stats_sync.h>
#include <net/ip_tunnels.h>

#include "conntrack.h"
#include "flow.h"
#include "flow_table.h"
#include "meter.h"
#include "vport-internal_dev.h"

#define DP_MAX_PORTS           USHRT_MAX
#define DP_VPORT_HASH_BUCKETS  1024

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
 * @n_mask_hit: Number of masks looked up for flow match.
 *   @n_mask_hit / (@n_hit + @n_missed)  will be the average masks looked
 *   up per packet.
 */
struct dp_stats_percpu {
	u64 n_hit;
	u64 n_missed;
	u64 n_lost;
	u64 n_mask_hit;
	struct u64_stats_sync syncp;
};

/**
 * struct datapath - datapath for flow-based packet switching
 * @rcu: RCU callback head for deferred destruction.
 * @list_node: Element in global 'dps' list.
 * @table: flow table.
 * @ports: Hash table for ports.  %OVSP_LOCAL port always exists.  Protected by
 * ovs_mutex and RCU.
 * @stats_percpu: Per-CPU datapath statistics.
 * @net: Reference to net namespace.
 * @max_headroom: the maximum headroom of all vports in this datapath; it will
 * be used by all the internal vports in this dp.
 *
 * Context: See the comment on locking at the top of datapath.c for additional
 * locking information.
 */
struct datapath {
	struct rcu_head rcu;
	struct list_head list_node;

	/* Flow table. */
	struct flow_table table;

	/* Switch ports. */
	struct hlist_head *ports;

	/* Stats. */
	struct dp_stats_percpu __percpu *stats_percpu;

	/* Network namespace ref. */
	possible_net_t net;

	u32 user_features;

	u32 max_headroom;

	/* Switch meters. */
	struct dp_meter_table meter_tbl;
};

/**
 * struct ovs_skb_cb - OVS data in skb CB
 * @input_vport: The original vport packet came in on. This value is cached
 * when a packet is received by OVS.
 * @mru: The maximum received fragement size; 0 if the packet is not
 * fragmented.
 * @acts_origlen: The netlink size of the flow actions applied to this skb.
 * @cutlen: The number of bytes from the packet end to be removed.
 */
struct ovs_skb_cb {
	struct vport		*input_vport;
	u16			mru;
	u16			acts_origlen;
	u32			cutlen;
};
#define OVS_CB(skb) ((struct ovs_skb_cb *)(skb)->cb)

/**
 * struct dp_upcall - metadata to include with a packet to send to userspace
 * @cmd: One of %OVS_PACKET_CMD_*.
 * @userdata: If nonnull, its variable-length value is passed to userspace as
 * %OVS_PACKET_ATTR_USERDATA.
 * @portid: Netlink portid to which packet should be sent.  If @portid is 0
 * then no packet is sent and the packet is accounted in the datapath's @n_lost
 * counter.
 * @egress_tun_info: If nonnull, becomes %OVS_PACKET_ATTR_EGRESS_TUN_KEY.
 * @mru: If not zero, Maximum received IP fragment size.
 */
struct dp_upcall_info {
	struct ip_tunnel_info *egress_tun_info;
	const struct nlattr *userdata;
	const struct nlattr *actions;
	int actions_len;
	u32 portid;
	u8 cmd;
	u16 mru;
};

/**
 * struct ovs_net - Per net-namespace data for ovs.
 * @dps: List of datapaths to enable dumping them all out.
 * Protected by genl_mutex.
 */
struct ovs_net {
	struct list_head dps;
	struct work_struct dp_notify_work;
#if	IS_ENABLED(CONFIG_NETFILTER_CONNCOUNT)
	struct ovs_ct_limit_info *ct_limit_info;
#endif

	/* Module reference for configuring conntrack. */
	bool xt_label;
};

/**
 * enum ovs_pkt_hash_types - hash info to include with a packet
 * to send to userspace.
 * @OVS_PACKET_HASH_SW_BIT: indicates hash was computed in software stack.
 * @OVS_PACKET_HASH_L4_BIT: indicates hash is a canonical 4-tuple hash
 * over transport ports.
 */
enum ovs_pkt_hash_types {
	OVS_PACKET_HASH_SW_BIT = (1ULL << 32),
	OVS_PACKET_HASH_L4_BIT = (1ULL << 33),
};

extern unsigned int ovs_net_id;
void ovs_lock(void);
void ovs_unlock(void);

#ifdef CONFIG_LOCKDEP
int lockdep_ovsl_is_held(void);
#else
#define lockdep_ovsl_is_held()	1
#endif

#define ASSERT_OVSL()		WARN_ON(!lockdep_ovsl_is_held())
#define ovsl_dereference(p)					\
	rcu_dereference_protected(p, lockdep_ovsl_is_held())
#define rcu_dereference_ovsl(p)					\
	rcu_dereference_check(p, lockdep_ovsl_is_held())

static inline struct net *ovs_dp_get_net(const struct datapath *dp)
{
	return read_pnet(&dp->net);
}

static inline void ovs_dp_set_net(struct datapath *dp, struct net *net)
{
	write_pnet(&dp->net, net);
}

struct vport *ovs_lookup_vport(const struct datapath *dp, u16 port_no);

static inline struct vport *ovs_vport_rcu(const struct datapath *dp, int port_no)
{
	WARN_ON_ONCE(!rcu_read_lock_held());
	return ovs_lookup_vport(dp, port_no);
}

static inline struct vport *ovs_vport_ovsl_rcu(const struct datapath *dp, int port_no)
{
	WARN_ON_ONCE(!rcu_read_lock_held() && !lockdep_ovsl_is_held());
	return ovs_lookup_vport(dp, port_no);
}

static inline struct vport *ovs_vport_ovsl(const struct datapath *dp, int port_no)
{
	ASSERT_OVSL();
	return ovs_lookup_vport(dp, port_no);
}

/* Must be called with rcu_read_lock. */
static inline struct datapath *get_dp_rcu(struct net *net, int dp_ifindex)
{
	struct net_device *dev = dev_get_by_index_rcu(net, dp_ifindex);

	if (dev) {
		struct vport *vport = ovs_internal_dev_get_vport(dev);

		if (vport)
			return vport->dp;
	}

	return NULL;
}

/* The caller must hold either ovs_mutex or rcu_read_lock to keep the
 * returned dp pointer valid.
 */
static inline struct datapath *get_dp(struct net *net, int dp_ifindex)
{
	struct datapath *dp;

	WARN_ON_ONCE(!rcu_read_lock_held() && !lockdep_ovsl_is_held());
	rcu_read_lock();
	dp = get_dp_rcu(net, dp_ifindex);
	rcu_read_unlock();

	return dp;
}

extern struct notifier_block ovs_dp_device_notifier;
extern struct genl_family dp_vport_genl_family;

DECLARE_STATIC_KEY_FALSE(tc_recirc_sharing_support);

void ovs_dp_process_packet(struct sk_buff *skb, struct sw_flow_key *key);
void ovs_dp_detach_port(struct vport *);
int ovs_dp_upcall(struct datapath *, struct sk_buff *,
		  const struct sw_flow_key *, const struct dp_upcall_info *,
		  uint32_t cutlen);

const char *ovs_dp_name(const struct datapath *dp);
struct sk_buff *ovs_vport_cmd_build_info(struct vport *vport, struct net *net,
					 u32 portid, u32 seq, u8 cmd);

int ovs_execute_actions(struct datapath *dp, struct sk_buff *skb,
			const struct sw_flow_actions *, struct sw_flow_key *);

void ovs_dp_notify_wq(struct work_struct *work);

int action_fifos_init(void);
void action_fifos_exit(void);

/* 'KEY' must not have any bits set outside of the 'MASK' */
#define OVS_MASKED(OLD, KEY, MASK) ((KEY) | ((OLD) & ~(MASK)))
#define OVS_SET_MASKED(OLD, KEY, MASK) ((OLD) = OVS_MASKED(OLD, KEY, MASK))

#define OVS_NLERR(logging_allowed, fmt, ...)			\
do {								\
	if (logging_allowed && net_ratelimit())			\
		pr_info("netlink: " fmt "\n", ##__VA_ARGS__);	\
} while (0)
#endif /* datapath.h */

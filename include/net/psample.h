/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_PSAMPLE_H
#define __NET_PSAMPLE_H

#include <uapi/linux/psample.h>
#include <linux/list.h>

struct psample_group {
	struct list_head list;
	struct net *net;
	u32 group_num;
	u32 refcount;
	u32 seq;
	struct rcu_head rcu;
};

struct psample_metadata {
	u32 trunc_size;
	int in_ifindex;
	int out_ifindex;
	u16 out_tc;
	u64 out_tc_occ;	/* bytes */
	u64 latency;	/* nanoseconds */
	u8 out_tc_valid:1,
	   out_tc_occ_valid:1,
	   latency_valid:1,
	   unused:5;
};

struct psample_group *psample_group_get(struct net *net, u32 group_num);
void psample_group_take(struct psample_group *group);
void psample_group_put(struct psample_group *group);

struct sk_buff;

#if IS_ENABLED(CONFIG_PSAMPLE)

void psample_sample_packet(struct psample_group *group, struct sk_buff *skb,
			   u32 sample_rate, const struct psample_metadata *md);

#else

static inline void psample_sample_packet(struct psample_group *group,
					 struct sk_buff *skb, u32 sample_rate,
					 const struct psample_metadata *md)
{
}

#endif

#endif /* __NET_PSAMPLE_H */

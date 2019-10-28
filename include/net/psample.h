/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_PSAMPLE_H
#define __NET_PSAMPLE_H

#include <uapi/linux/psample.h>
#include <linux/module.h>
#include <linux/list.h>

struct psample_group {
	struct list_head list;
	struct net *net;
	u32 group_num;
	u32 refcount;
	u32 seq;
	struct rcu_head rcu;
};

struct psample_group *psample_group_get(struct net *net, u32 group_num);
void psample_group_put(struct psample_group *group);

#if IS_ENABLED(CONFIG_PSAMPLE)

void psample_sample_packet(struct psample_group *group, struct sk_buff *skb,
			   u32 trunc_size, int in_ifindex, int out_ifindex,
			   u32 sample_rate);

#else

static inline void psample_sample_packet(struct psample_group *group,
					 struct sk_buff *skb, u32 trunc_size,
					 int in_ifindex, int out_ifindex,
					 u32 sample_rate)
{
}

#endif

#endif /* __NET_PSAMPLE_H */

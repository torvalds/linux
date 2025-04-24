// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/cache.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <net/hotdata.h>
#include <net/proto_memory.h>

struct net_hotdata net_hotdata __cacheline_aligned = {
	.offload_base = LIST_HEAD_INIT(net_hotdata.offload_base),
	.gro_normal_batch = 8,

	.netdev_budget = 300,
	/* Must be at least 2 jiffes to guarantee 1 jiffy timeout */
	.netdev_budget_usecs = 2 * USEC_PER_SEC / HZ,

	.tstamp_prequeue = 1,
	.max_backlog = 1000,
	.dev_tx_weight = 64,
	.dev_rx_weight = 64,
	.sysctl_max_skb_frags = MAX_SKB_FRAGS,
	.sysctl_skb_defer_max = 64,
	.sysctl_mem_pcpu_rsv = SK_MEMORY_PCPU_RESERVE
};
EXPORT_SYMBOL(net_hotdata);

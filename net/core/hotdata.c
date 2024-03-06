// SPDX-License-Identifier: GPL-2.0-or-later
#include <net/hotdata.h>
#include <linux/cache.h>
#include <linux/jiffies.h>
#include <linux/list.h>


struct net_hotdata net_hotdata __cacheline_aligned = {
	.offload_base = LIST_HEAD_INIT(net_hotdata.offload_base),
	.gro_normal_batch = 8,

	.netdev_budget = 300,
	/* Must be at least 2 jiffes to guarantee 1 jiffy timeout */
	.netdev_budget_usecs = 2 * USEC_PER_SEC / HZ,
};

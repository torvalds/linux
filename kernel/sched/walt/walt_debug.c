// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>

#include <trace/hooks/sched.h>

#include "walt.h"
#include "walt_debug.h"

static void dump_throttled_rt_tasks(void *unused, int cpu, u64 clock,
		ktime_t rt_period, u64 rt_runtime, s64 rt_period_timer_expires)
{
	printk_deferred("sched: RT throttling activated for cpu %d\n", cpu);
	printk_deferred("rt_period_timer: expires=%lld now=%llu runtime=%llu period=%llu\n",
			rt_period_timer_expires, ktime_get_ns(), rt_runtime, rt_period);
	printk_deferred("potential CPU hogs:\n");
#ifdef CONFIG_SCHED_INFO
	if (sched_info_on())
		printk_deferred("current %s (%d) is running for %llu nsec\n",
				current->comm, current->pid,
				clock - current->sched_info.last_arrival);
#endif
	BUG();
}

static void android_rvh_schedule_bug(void *unused, void *unused2)
{
	BUG();
}

static int __init walt_debug_init(void)
{
	int ret;

	ret = preemptirq_long_init();
	if (!ret)
		return ret;

	register_trace_android_vh_dump_throttled_rt_tasks(dump_throttled_rt_tasks, NULL);
	register_trace_android_rvh_schedule_bug(android_rvh_schedule_bug, NULL);

	return 0;
}
module_init(walt_debug_init);

MODULE_DESCRIPTION("QTI WALT Debug Module");
MODULE_LICENSE("GPL v2");

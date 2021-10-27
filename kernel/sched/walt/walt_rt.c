// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#include <trace/hooks/sched.h>

#include "walt.h"
#include "trace.h"

static void rt_energy_aware_wake_cpu(void *unused, struct task_struct *task,
				struct cpumask *lowest_mask, int ret, int *best_cpu)
{
	int cpu;
	unsigned long util, best_cpu_util = ULONG_MAX;
	unsigned long best_cpu_util_cum = ULONG_MAX;
	unsigned long util_cum;
	unsigned long tutil = task_util(task);
	unsigned int best_idle_exit_latency = UINT_MAX;
	unsigned int cpu_idle_exit_latency = UINT_MAX;
	bool boost_on_big = rt_boost_on_big();
	int cluster;
	int order_index = (boost_on_big && num_sched_clusters > 1) ? 1 : 0;

	if (static_branch_unlikely(&walt_disabled))
		return;
	if (!ret)
		return; /* No targets found */

	rcu_read_lock();
	for (cluster = 0; cluster < num_sched_clusters; cluster++) {
		for_each_cpu_and(cpu, lowest_mask, &cpu_array[order_index][cluster]) {
			trace_sched_cpu_util(cpu);

			if (!cpu_active(cpu))
				continue;

			if (sched_cpu_high_irqload(cpu))
				continue;

			if (__cpu_overutilized(cpu, tutil))
				continue;

			util = cpu_util(cpu);

			/* Find the least loaded CPU */
			if (util > best_cpu_util)
				continue;

			/*
			 * If the previous CPU has same load, keep it as
			 * best_cpu.
			 */
			if (best_cpu_util == util && *best_cpu == task_cpu(task))
				continue;

			/*
			 * If candidate CPU is the previous CPU, select it.
			 * Otherwise, if its load is same with best_cpu and in
			 * a shallower C-state, select it.  If all above
			 * conditions are same, select the least cumulative
			 * window demand CPU.
			 */
			cpu_idle_exit_latency = walt_get_idle_exit_latency(cpu_rq(cpu));

			util_cum = cpu_util_cum(cpu, 0);
			if (cpu != task_cpu(task) && best_cpu_util == util) {
				if (best_idle_exit_latency < cpu_idle_exit_latency)
					continue;

				if (best_idle_exit_latency == cpu_idle_exit_latency &&
						best_cpu_util_cum < util_cum)
					continue;
			}

			best_idle_exit_latency = cpu_idle_exit_latency;
			best_cpu_util_cum = util_cum;
			best_cpu_util = util;
			*best_cpu = cpu;
		}

		if (*best_cpu != -1)
			break;
	}

	rcu_read_unlock();
}

void walt_rt_init(void)
{
	register_trace_android_rvh_find_lowest_rq(rt_energy_aware_wake_cpu, NULL);
}

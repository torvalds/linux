// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>

#include "walt.h"

#ifdef CONFIG_HOTPLUG_CPU

cpumask_t pending_active_mask = CPU_MASK_NONE;
int sched_pause_count(const cpumask_t *mask, bool include_offline)
{
	cpumask_t count_mask = CPU_MASK_NONE;
	cpumask_t pause_mask = CPU_MASK_NONE;

	if (cpumask_any(&pending_active_mask) >= nr_cpu_ids) {
		/* initialize pending_active_state */
		cpumask_copy(&pending_active_mask, cpu_active_mask);
	}

	if (include_offline) {

		/* get all offline or paused cpus */
		cpumask_complement(&pause_mask, &pending_active_mask);
		cpumask_complement(&count_mask, cpu_online_mask);
		cpumask_or(&count_mask, &count_mask, &pause_mask);

		/* get all offline or paused cpus in this cluster */
		cpumask_and(&count_mask, &count_mask, mask);
	} else {
		cpumask_andnot(&count_mask, mask, &pending_active_mask);
	}

	return cpumask_weight(&count_mask);
}

void sched_pause_pending(int cpu)
{
	cpumask_clear_cpu(cpu, &pending_active_mask);
}

void sched_unpause_pending(int cpu)
{
	cpumask_set_cpu(cpu, &pending_active_mask);
}

#endif /* CONFIG_HOTPLUG_CPU */

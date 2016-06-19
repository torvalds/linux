/*
 * Scheduler code and data structures related to cpufreq.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "sched.h"

DEFINE_PER_CPU(struct update_util_data *, cpufreq_update_util_data);

/**
 * cpufreq_set_update_util_data - Populate the CPU's update_util_data pointer.
 * @cpu: The CPU to set the pointer for.
 * @data: New pointer value.
 *
 * Set and publish the update_util_data pointer for the given CPU.  That pointer
 * points to a struct update_util_data object containing a callback function
 * to call from cpufreq_update_util().  That function will be called from an RCU
 * read-side critical section, so it must not sleep.
 *
 * Callers must use RCU-sched callbacks to free any memory that might be
 * accessed via the old update_util_data pointer or invoke synchronize_sched()
 * right after this function to avoid use-after-free.
 */
void cpufreq_set_update_util_data(int cpu, struct update_util_data *data)
{
	if (WARN_ON(data && !data->func))
		return;

	rcu_assign_pointer(per_cpu(cpufreq_update_util_data, cpu), data);
}
EXPORT_SYMBOL_GPL(cpufreq_set_update_util_data);

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Symbols stubs needed for GKI compliance
 */

#include "sched.h"

int sched_isolate_cpu(int cpu)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(sched_isolate_cpu);

int sched_unisolate_cpu_unlocked(int cpu)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(sched_unisolate_cpu_unlocked);

int sched_unisolate_cpu(int cpu)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(sched_unisolate_cpu);

int set_task_boost(int boost, u64 period)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(set_task_boost);

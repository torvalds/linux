// SPDX-License-Identifier: GPL-2.0
/* Manage affinity to optimize IPIs inside the kernel perf API. */
#define _GNU_SOURCE 1
#include <sched.h>
#include <stdlib.h>
#include <linux/bitmap.h>
#include <linux/zalloc.h>
#include "perf.h"
#include "cpumap.h"
#include "affinity.h"

static int get_cpu_set_size(void)
{
	int sz = cpu__max_cpu().cpu + 8 - 1;
	/*
	 * sched_getaffinity doesn't like masks smaller than the kernel.
	 * Hopefully that's big enough.
	 */
	if (sz < 4096)
		sz = 4096;
	return sz / 8;
}

int affinity__setup(struct affinity *a)
{
	int cpu_set_size = get_cpu_set_size();

	a->orig_cpus = bitmap_zalloc(cpu_set_size * 8);
	if (!a->orig_cpus)
		return -1;
	sched_getaffinity(0, cpu_set_size, (cpu_set_t *)a->orig_cpus);
	a->sched_cpus = bitmap_zalloc(cpu_set_size * 8);
	if (!a->sched_cpus) {
		zfree(&a->orig_cpus);
		return -1;
	}
	bitmap_zero((unsigned long *)a->sched_cpus, cpu_set_size);
	a->changed = false;
	return 0;
}

/*
 * perf_event_open does an IPI internally to the target CPU.
 * It is more efficient to change perf's affinity to the target
 * CPU and then set up all events on that CPU, so we amortize
 * CPU communication.
 */
void affinity__set(struct affinity *a, int cpu)
{
	int cpu_set_size = get_cpu_set_size();

	/*
	 * Return:
	 * - if cpu is -1
	 * - restrict out of bound access to sched_cpus
	 */
	if (cpu == -1 || ((cpu >= (cpu_set_size * 8))))
		return;

	a->changed = true;
	set_bit(cpu, a->sched_cpus);
	/*
	 * We ignore errors because affinity is just an optimization.
	 * This could happen for example with isolated CPUs or cpusets.
	 * In this case the IPIs inside the kernel's perf API still work.
	 */
	sched_setaffinity(0, cpu_set_size, (cpu_set_t *)a->sched_cpus);
	clear_bit(cpu, a->sched_cpus);
}

static void __affinity__cleanup(struct affinity *a)
{
	int cpu_set_size = get_cpu_set_size();

	if (a->changed)
		sched_setaffinity(0, cpu_set_size, (cpu_set_t *)a->orig_cpus);
	zfree(&a->sched_cpus);
	zfree(&a->orig_cpus);
}

void affinity__cleanup(struct affinity *a)
{
	if (a != NULL)
		__affinity__cleanup(a);
}

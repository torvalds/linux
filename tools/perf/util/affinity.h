// SPDX-License-Identifier: GPL-2.0
#ifndef PERF_AFFINITY_H
#define PERF_AFFINITY_H 1

#include <stdbool.h>

struct affinity {
	unsigned long *orig_cpus;
	unsigned long *sched_cpus;
	bool changed;
};

void affinity__cleanup(struct affinity *a);
void affinity__set(struct affinity *a, int cpu);
int affinity__setup(struct affinity *a);

#endif // PERF_AFFINITY_H

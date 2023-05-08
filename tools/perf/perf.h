/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_PERF_H
#define _PERF_PERF_H

#ifndef MAX_NR_CPUS
#define MAX_NR_CPUS			2048
#endif

enum perf_affinity {
	PERF_AFFINITY_SYS = 0,
	PERF_AFFINITY_NODE,
	PERF_AFFINITY_CPU,
	PERF_AFFINITY_MAX
};

#endif

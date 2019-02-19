/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CPUTOPO_H
#define __PERF_CPUTOPO_H

#include <linux/types.h>

struct cpu_topology {
	u32	  core_sib;
	u32	  thread_sib;
	char	**core_siblings;
	char	**thread_siblings;
};

struct cpu_topology *cpu_topology__new(void);
void cpu_topology__delete(struct cpu_topology *tp);

#endif /* __PERF_CPUTOPO_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CPUPOWER_CPUPOWER_H__
#define __CPUPOWER_CPUPOWER_H__

#define CPULIST_BUFFER 5

struct cpupower_topology {
	/* Amount of CPU cores, packages and threads per core in the system */
	unsigned int cores;
	unsigned int pkgs;
	unsigned int threads; /* per core */

	/* Array gets mallocated with cores entries, holding per core info */
	struct cpuid_core_info *core_info;
};

struct cpuid_core_info {
	int pkg;
	int core;
	int cpu;
	char core_cpu_list[CPULIST_BUFFER];

	/* flags */
	unsigned int is_online:1;
};

#ifdef __cplusplus
extern "C" {
#endif

int get_cpu_topology(struct cpupower_topology *cpu_top);
void cpu_topology_release(struct cpupower_topology cpu_top);
int cpupower_is_cpu_online(unsigned int cpu);

#ifdef __cplusplus
}
#endif

#endif

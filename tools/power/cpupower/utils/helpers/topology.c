/*
 *  (C) 2010,2011       Thomas Renninger <trenn@suse.de>, Novell Inc.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 * ToDo: Needs to be done more properly for AMD/Intel specifics
 */

/* Helper struct for qsort, must be in sync with cpupower_topology.cpu_info */
/* Be careful: Need to pass unsigned to the sort, so that offlined cores are
   in the end, but double check for -1 for offlined cpus at other places */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <helpers/helpers.h>
#include <helpers/sysfs.h>

/* returns -1 on failure, 0 on success */
static int sysfs_topology_read_file(unsigned int cpu, const char *fname, int *result)
{
	char linebuf[MAX_LINE_LEN];
	char *endp;
	char path[SYSFS_PATH_MAX];

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/topology/%s",
			 cpu, fname);
	if (sysfs_read_file(path, linebuf, MAX_LINE_LEN) == 0)
		return -1;
	*result = strtol(linebuf, &endp, 0);
	if (endp == linebuf || errno == ERANGE)
		return -1;
	return 0;
}

static int __compare(const void *t1, const void *t2)
{
	struct cpuid_core_info *top1 = (struct cpuid_core_info *)t1;
	struct cpuid_core_info *top2 = (struct cpuid_core_info *)t2;
	if (top1->pkg < top2->pkg)
		return -1;
	else if (top1->pkg > top2->pkg)
		return 1;
	else if (top1->core < top2->core)
		return -1;
	else if (top1->core > top2->core)
		return 1;
	else if (top1->cpu < top2->cpu)
		return -1;
	else if (top1->cpu > top2->cpu)
		return 1;
	else
		return 0;
}

/*
 * Returns amount of cpus, negative on error, cpu_top must be
 * passed to cpu_topology_release to free resources
 *
 * Array is sorted after ->pkg, ->core, then ->cpu
 */
int get_cpu_topology(struct cpupower_topology *cpu_top)
{
	int cpu, last_pkg, cpus = sysconf(_SC_NPROCESSORS_CONF);

	cpu_top->core_info = malloc(sizeof(struct cpuid_core_info) * cpus);
	if (cpu_top->core_info == NULL)
		return -ENOMEM;
	cpu_top->pkgs = cpu_top->cores = 0;
	for (cpu = 0; cpu < cpus; cpu++) {
		cpu_top->core_info[cpu].cpu = cpu;
		cpu_top->core_info[cpu].is_online = sysfs_is_cpu_online(cpu);
		if(sysfs_topology_read_file(
			cpu,
			"physical_package_id",
			&(cpu_top->core_info[cpu].pkg)) < 0) {
			cpu_top->core_info[cpu].pkg = -1;
			cpu_top->core_info[cpu].core = -1;
			continue;
		}
		if(sysfs_topology_read_file(
			cpu,
			"core_id",
			&(cpu_top->core_info[cpu].core)) < 0) {
			cpu_top->core_info[cpu].pkg = -1;
			cpu_top->core_info[cpu].core = -1;
			continue;
		}
	}

	qsort(cpu_top->core_info, cpus, sizeof(struct cpuid_core_info),
	      __compare);

	/* Count the number of distinct pkgs values. This works
	   because the primary sort of the core_info struct was just
	   done by pkg value. */
	last_pkg = cpu_top->core_info[0].pkg;
	for(cpu = 1; cpu < cpus; cpu++) {
		if (cpu_top->core_info[cpu].pkg != last_pkg &&
				cpu_top->core_info[cpu].pkg != -1) {

			last_pkg = cpu_top->core_info[cpu].pkg;
			cpu_top->pkgs++;
		}
	}
	if (!(cpu_top->core_info[0].pkg == -1))
		cpu_top->pkgs++;

	/* Intel's cores count is not consecutively numbered, there may
	 * be a core_id of 3, but none of 2. Assume there always is 0
	 * Get amount of cores by counting duplicates in a package
	for (cpu = 0; cpu_top->core_info[cpu].pkg = 0 && cpu < cpus; cpu++) {
		if (cpu_top->core_info[cpu].core == 0)
	cpu_top->cores++;
	*/
	return cpus;
}

void cpu_topology_release(struct cpupower_topology cpu_top)
{
	free(cpu_top.core_info);
}

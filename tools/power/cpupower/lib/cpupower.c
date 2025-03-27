// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2004-2009  Dominik Brodowski <linux@dominikbrodowski.de>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "cpupower.h"
#include "cpupower_intern.h"

int is_valid_path(const char *path)
{
	if (access(path, F_OK) == -1)
		return 0;
	return 1;
}

unsigned int cpupower_read_sysfs(const char *path, char *buf, size_t buflen)
{
	ssize_t numread;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return 0;

	numread = read(fd, buf, buflen - 1);
	if (numread < 1) {
		close(fd);
		return 0;
	}

	buf[numread] = '\0';
	close(fd);

	return (unsigned int) numread;
}

unsigned int cpupower_write_sysfs(const char *path, char *buf, size_t buflen)
{
	ssize_t numwritten;
	int fd;

	fd = open(path, O_WRONLY);
	if (fd == -1)
		return 0;

	numwritten = write(fd, buf, buflen - 1);
	if (numwritten < 1) {
		perror(path);
		close(fd);
		return -1;
	}

	close(fd);

	return (unsigned int) numwritten;
}

/*
 * Detect whether a CPU is online
 *
 * Returns:
 *     1 -> if CPU is online
 *     0 -> if CPU is offline
 *     negative errno values in error case
 */
int cpupower_is_cpu_online(unsigned int cpu)
{
	char path[SYSFS_PATH_MAX];
	int fd;
	ssize_t numread;
	unsigned long long value;
	char linebuf[MAX_LINE_LEN];
	char *endp;
	struct stat statbuf;

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u", cpu);

	if (stat(path, &statbuf) != 0)
		return 0;

	/*
	 * kernel without CONFIG_HOTPLUG_CPU
	 * -> cpuX directory exists, but not cpuX/online file
	 */
	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/online", cpu);
	if (stat(path, &statbuf) != 0)
		return 1;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -errno;

	numread = read(fd, linebuf, MAX_LINE_LEN - 1);
	if (numread < 1) {
		close(fd);
		return -EIO;
	}
	linebuf[numread] = '\0';
	close(fd);

	value = strtoull(linebuf, &endp, 0);
	if (value > 1)
		return -EINVAL;

	return value;
}

/* returns -1 on failure, 0 on success */
static int sysfs_topology_read_file(unsigned int cpu, const char *fname, int *result)
{
	char linebuf[MAX_LINE_LEN];
	char *endp;
	char path[SYSFS_PATH_MAX];

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/topology/%s",
			 cpu, fname);
	if (cpupower_read_sysfs(path, linebuf, MAX_LINE_LEN) == 0)
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

static int __compare_core_cpu_list(const void *t1, const void *t2)
{
	struct cpuid_core_info *top1 = (struct cpuid_core_info *)t1;
	struct cpuid_core_info *top2 = (struct cpuid_core_info *)t2;

	return strcmp(top1->core_cpu_list, top2->core_cpu_list);
}

/*
 * Returns amount of cpus, negative on error, cpu_top must be
 * passed to cpu_topology_release to free resources
 *
 * Array is sorted after ->cpu_smt_list ->pkg, ->core
 */
int get_cpu_topology(struct cpupower_topology *cpu_top)
{
	int cpu, last_pkg, cpus = sysconf(_SC_NPROCESSORS_CONF);
	char path[SYSFS_PATH_MAX];
	char *last_cpu_list;

	cpu_top->core_info = malloc(sizeof(struct cpuid_core_info) * cpus);
	if (cpu_top->core_info == NULL)
		return -ENOMEM;
	cpu_top->pkgs = cpu_top->cores = 0;
	for (cpu = 0; cpu < cpus; cpu++) {
		cpu_top->core_info[cpu].cpu = cpu;
		cpu_top->core_info[cpu].is_online = cpupower_is_cpu_online(cpu);
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
		if (cpu_top->core_info[cpu].core == -1) {
			strncpy(cpu_top->core_info[cpu].core_cpu_list, "-1", CPULIST_BUFFER);
			continue;
		}
		snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/topology/%s",
			 cpu, "core_cpus_list");
		if (cpupower_read_sysfs(
			path,
			cpu_top->core_info[cpu].core_cpu_list,
			CPULIST_BUFFER) < 1) {
			printf("Warning CPU%u has a 0 size core_cpus_list string", cpu);
		}
	}

	/* Count the number of distinct cpu lists to get the physical core
	 * count.
	 */
	qsort(cpu_top->core_info, cpus, sizeof(struct cpuid_core_info),
	      __compare_core_cpu_list);

	last_cpu_list = cpu_top->core_info[0].core_cpu_list;
	cpu_top->cores = 1;
	for (cpu = 1; cpu < cpus; cpu++) {
		if (strcmp(cpu_top->core_info[cpu].core_cpu_list, last_cpu_list) != 0 &&
		    cpu_top->core_info[cpu].pkg != -1) {
			last_cpu_list = cpu_top->core_info[cpu].core_cpu_list;
			cpu_top->cores++;
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

	return cpus;
}

void cpu_topology_release(struct cpupower_topology cpu_top)
{
	free(cpu_top.core_info);
}

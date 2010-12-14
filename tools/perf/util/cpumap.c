#include "util.h"
#include "../perf.h"
#include "cpumap.h"
#include <assert.h>
#include <stdio.h>

int cpumap[MAX_NR_CPUS];

static int default_cpu_map(void)
{
	int nr_cpus, i;

	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	assert(nr_cpus <= MAX_NR_CPUS);
	assert((int)nr_cpus >= 0);

	for (i = 0; i < nr_cpus; ++i)
		cpumap[i] = i;

	return nr_cpus;
}

static int read_all_cpu_map(void)
{
	FILE *onlnf;
	int nr_cpus = 0;
	int n, cpu, prev;
	char sep;

	onlnf = fopen("/sys/devices/system/cpu/online", "r");
	if (!onlnf)
		return default_cpu_map();

	sep = 0;
	prev = -1;
	for (;;) {
		n = fscanf(onlnf, "%u%c", &cpu, &sep);
		if (n <= 0)
			break;
		if (prev >= 0) {
			assert(nr_cpus + cpu - prev - 1 < MAX_NR_CPUS);
			while (++prev < cpu)
				cpumap[nr_cpus++] = prev;
		}
		assert (nr_cpus < MAX_NR_CPUS);
		cpumap[nr_cpus++] = cpu;
		if (n == 2 && sep == '-')
			prev = cpu;
		else
			prev = -1;
		if (n == 1 || sep == '\n')
			break;
	}
	fclose(onlnf);
	if (nr_cpus > 0)
		return nr_cpus;

	return default_cpu_map();
}

int read_cpu_map(const char *cpu_list)
{
	unsigned long start_cpu, end_cpu = 0;
	char *p = NULL;
	int i, nr_cpus = 0;

	if (!cpu_list)
		return read_all_cpu_map();

	if (!isdigit(*cpu_list))
		goto invalid;

	while (isdigit(*cpu_list)) {
		p = NULL;
		start_cpu = strtoul(cpu_list, &p, 0);
		if (start_cpu >= INT_MAX
		    || (*p != '\0' && *p != ',' && *p != '-'))
			goto invalid;

		if (*p == '-') {
			cpu_list = ++p;
			p = NULL;
			end_cpu = strtoul(cpu_list, &p, 0);

			if (end_cpu >= INT_MAX || (*p != '\0' && *p != ','))
				goto invalid;

			if (end_cpu < start_cpu)
				goto invalid;
		} else {
			end_cpu = start_cpu;
		}

		for (; start_cpu <= end_cpu; start_cpu++) {
			/* check for duplicates */
			for (i = 0; i < nr_cpus; i++)
				if (cpumap[i] == (int)start_cpu)
					goto invalid;

			assert(nr_cpus < MAX_NR_CPUS);
			cpumap[nr_cpus++] = (int)start_cpu;
		}
		if (*p)
			++p;

		cpu_list = p;
	}
	if (nr_cpus > 0)
		return nr_cpus;

	return default_cpu_map();
invalid:
	return -1;
}

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

int read_cpu_map(void)
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

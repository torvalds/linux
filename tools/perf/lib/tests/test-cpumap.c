// SPDX-License-Identifier: GPL-2.0
#include <perf/cpumap.h>
#include <internal/tests.h>

int main(int argc, char **argv)
{
	struct perf_cpu_map *cpus;

	__T_START;

	cpus = perf_cpu_map__dummy_new();
	if (!cpus)
		return -1;

	perf_cpu_map__get(cpus);
	perf_cpu_map__put(cpus);
	perf_cpu_map__put(cpus);

	__T_OK;
	return 0;
}

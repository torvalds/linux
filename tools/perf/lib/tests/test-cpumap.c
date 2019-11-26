// SPDX-License-Identifier: GPL-2.0
#include <stdarg.h>
#include <stdio.h>
#include <perf/cpumap.h>
#include <internal/tests.h>

static int libperf_print(enum libperf_print_level level,
			 const char *fmt, va_list ap)
{
	return vfprintf(stderr, fmt, ap);
}

int main(int argc, char **argv)
{
	struct perf_cpu_map *cpus;

	__T_START;

	libperf_init(libperf_print);

	cpus = perf_cpu_map__dummy_new();
	if (!cpus)
		return -1;

	perf_cpu_map__get(cpus);
	perf_cpu_map__put(cpus);
	perf_cpu_map__put(cpus);

	__T_END;
	return 0;
}

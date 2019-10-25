// SPDX-License-Identifier: GPL-2.0
#include <perf/threadmap.h>
#include <internal/tests.h>

int main(int argc, char **argv)
{
	struct perf_thread_map *threads;

	__T_START;

	threads = perf_thread_map__new_dummy();
	if (!threads)
		return -1;

	perf_thread_map__get(threads);
	perf_thread_map__put(threads);
	perf_thread_map__put(threads);

	__T_OK;
	return 0;
}

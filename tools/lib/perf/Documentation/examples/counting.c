#include <linux/perf_event.h>
#include <perf/evlist.h>
#include <perf/evsel.h>
#include <perf/cpumap.h>
#include <perf/threadmap.h>
#include <perf/mmap.h>
#include <perf/core.h>
#include <perf/event.h>
#include <stdio.h>
#include <unistd.h>

static int libperf_print(enum libperf_print_level level,
                         const char *fmt, va_list ap)
{
	return vfprintf(stderr, fmt, ap);
}

int main(int argc, char **argv)
{
	int count = 100000, err = 0;
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	struct perf_thread_map *threads;
	struct perf_counts_values counts;

	struct perf_event_attr attr1 = {
		.type        = PERF_TYPE_SOFTWARE,
		.config      = PERF_COUNT_SW_CPU_CLOCK,
		.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING,
		.disabled    = 1,
	};
	struct perf_event_attr attr2 = {
		.type        = PERF_TYPE_SOFTWARE,
		.config      = PERF_COUNT_SW_TASK_CLOCK,
		.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING,
		.disabled    = 1,
	};

	libperf_init(libperf_print);
	threads = perf_thread_map__new_dummy();
	if (!threads) {
		fprintf(stderr, "failed to create threads\n");
		return -1;
	}
	perf_thread_map__set_pid(threads, 0, 0);
	evlist = perf_evlist__new();
	if (!evlist) {
		fprintf(stderr, "failed to create evlist\n");
		goto out_threads;
	}
	evsel = perf_evsel__new(&attr1);
	if (!evsel) {
		fprintf(stderr, "failed to create evsel1\n");
		goto out_evlist;
	}
	perf_evlist__add(evlist, evsel);
	evsel = perf_evsel__new(&attr2);
	if (!evsel) {
		fprintf(stderr, "failed to create evsel2\n");
		goto out_evlist;
	}
	perf_evlist__add(evlist, evsel);
	perf_evlist__set_maps(evlist, NULL, threads);
	err = perf_evlist__open(evlist);
	if (err) {
		fprintf(stderr, "failed to open evsel\n");
		goto out_evlist;
	}
	perf_evlist__enable(evlist);
	while (count--);
	perf_evlist__disable(evlist);
	perf_evlist__for_each_evsel(evlist, evsel) {
		perf_evsel__read(evsel, 0, 0, &counts);
		fprintf(stdout, "count %llu, enabled %llu, run %llu\n",
				counts.val, counts.ena, counts.run);
	}
	perf_evlist__close(evlist);
out_evlist:
	perf_evlist__delete(evlist);
out_threads:
	perf_thread_map__put(threads);
	return err;
}

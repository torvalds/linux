#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>

#include "tests.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/cpumap.h"
#include "util/thread_map.h"

#define NR_LOOPS  1000000

/*
 * This test will open software clock events (cpu-clock, task-clock)
 * then check their frequency -> period conversion has no artifact of
 * setting period to 1 forcefully.
 */
static int __test__sw_clock_freq(enum perf_sw_ids clock_id)
{
	int i, err = -1;
	volatile int tmp = 0;
	u64 total_periods = 0;
	int nr_samples = 0;
	union perf_event *event;
	struct perf_evsel *evsel;
	struct perf_evlist *evlist;
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.config = clock_id,
		.sample_type = PERF_SAMPLE_PERIOD,
		.exclude_kernel = 1,
		.disabled = 1,
		.freq = 1,
	};

	attr.sample_freq = 10000;

	evlist = perf_evlist__new();
	if (evlist == NULL) {
		pr_debug("perf_evlist__new\n");
		return -1;
	}

	evsel = perf_evsel__new(&attr, 0);
	if (evsel == NULL) {
		pr_debug("perf_evsel__new\n");
		goto out_free_evlist;
	}
	perf_evlist__add(evlist, evsel);

	evlist->cpus = cpu_map__dummy_new();
	evlist->threads = thread_map__new_by_tid(getpid());
	if (!evlist->cpus || !evlist->threads) {
		err = -ENOMEM;
		pr_debug("Not enough memory to create thread/cpu maps\n");
		goto out_delete_maps;
	}

	perf_evlist__open(evlist);

	err = perf_evlist__mmap(evlist, 128, true);
	if (err < 0) {
		pr_debug("failed to mmap event: %d (%s)\n", errno,
			 strerror(errno));
		goto out_close_evlist;
	}

	perf_evlist__enable(evlist);

	/* collect samples */
	for (i = 0; i < NR_LOOPS; i++)
		tmp++;

	perf_evlist__disable(evlist);

	while ((event = perf_evlist__mmap_read(evlist, 0)) != NULL) {
		struct perf_sample sample;

		if (event->header.type != PERF_RECORD_SAMPLE)
			goto next_event;

		err = perf_evlist__parse_sample(evlist, event, &sample);
		if (err < 0) {
			pr_debug("Error during parse sample\n");
			goto out_unmap_evlist;
		}

		total_periods += sample.period;
		nr_samples++;
next_event:
		perf_evlist__mmap_consume(evlist, 0);
	}

	if ((u64) nr_samples == total_periods) {
		pr_debug("All (%d) samples have period value of 1!\n",
			 nr_samples);
		err = -1;
	}

out_unmap_evlist:
	perf_evlist__munmap(evlist);
out_close_evlist:
	perf_evlist__close(evlist);
out_delete_maps:
	perf_evlist__delete_maps(evlist);
out_free_evlist:
	perf_evlist__delete(evlist);
	return err;
}

int test__sw_clock_freq(void)
{
	int ret;

	ret = __test__sw_clock_freq(PERF_COUNT_SW_CPU_CLOCK);
	if (!ret)
		ret = __test__sw_clock_freq(PERF_COUNT_SW_TASK_CLOCK);

	return ret;
}

// SPDX-License-Identifier: GPL-2.0
/*
 * Test backward bit in event attribute, read ring buffer from end to
 * beginning
 */

#include <evlist.h>
#include <sys/prctl.h>
#include "record.h"
#include "tests.h"
#include "debug.h"
#include "parse-events.h"
#include "util/mmap.h"
#include <errno.h>
#include <linux/string.h>
#include <perf/mmap.h>

#define NR_ITERS 111

static void testcase(void)
{
	int i;

	for (i = 0; i < NR_ITERS; i++) {
		char proc_name[15];

		snprintf(proc_name, sizeof(proc_name), "p:%d\n", i);
		prctl(PR_SET_NAME, proc_name);
	}
}

static int count_samples(struct evlist *evlist, int *sample_count,
			 int *comm_count)
{
	int i;

	for (i = 0; i < evlist->core.nr_mmaps; i++) {
		struct mmap *map = &evlist->overwrite_mmap[i];
		union perf_event *event;

		perf_mmap__read_init(&map->core);
		while ((event = perf_mmap__read_event(&map->core)) != NULL) {
			const u32 type = event->header.type;

			switch (type) {
			case PERF_RECORD_SAMPLE:
				(*sample_count)++;
				break;
			case PERF_RECORD_COMM:
				(*comm_count)++;
				break;
			default:
				pr_err("Unexpected record of type %d\n", type);
				return TEST_FAIL;
			}
		}
		perf_mmap__read_done(&map->core);
	}
	return TEST_OK;
}

static int do_test(struct evlist *evlist, int mmap_pages,
		   int *sample_count, int *comm_count)
{
	int err;
	char sbuf[STRERR_BUFSIZE];

	err = evlist__mmap(evlist, mmap_pages);
	if (err < 0) {
		pr_debug("evlist__mmap: %s\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		return TEST_FAIL;
	}

	evlist__enable(evlist);
	testcase();
	evlist__disable(evlist);

	err = count_samples(evlist, sample_count, comm_count);
	evlist__munmap(evlist);
	return err;
}


static int test__backward_ring_buffer(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int ret = TEST_SKIP, err, sample_count = 0, comm_count = 0;
	char pid[16], sbuf[STRERR_BUFSIZE];
	struct evlist *evlist;
	struct evsel *evsel __maybe_unused;
	struct parse_events_error parse_error;
	struct record_opts opts = {
		.target = {
			.uid = UINT_MAX,
			.uses_mmap = true,
		},
		.freq	      = 0,
		.mmap_pages   = 256,
		.default_interval = 1,
	};

	snprintf(pid, sizeof(pid), "%d", getpid());
	pid[sizeof(pid) - 1] = '\0';
	opts.target.tid = opts.target.pid = pid;

	evlist = evlist__new();
	if (!evlist) {
		pr_debug("Not enough memory to create evlist\n");
		return TEST_FAIL;
	}

	err = evlist__create_maps(evlist, &opts.target);
	if (err < 0) {
		pr_debug("Not enough memory to create thread/cpu maps\n");
		goto out_delete_evlist;
	}

	parse_events_error__init(&parse_error);
	/*
	 * Set backward bit, ring buffer should be writing from end. Record
	 * it in aux evlist
	 */
	err = parse_events(evlist, "syscalls:sys_enter_prctl/overwrite/", &parse_error);
	parse_events_error__exit(&parse_error);
	if (err) {
		pr_debug("Failed to parse tracepoint event, try use root\n");
		ret = TEST_SKIP;
		goto out_delete_evlist;
	}

	evlist__config(evlist, &opts, NULL);

	err = evlist__open(evlist);
	if (err < 0) {
		pr_debug("perf_evlist__open: %s\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	ret = TEST_FAIL;
	err = do_test(evlist, opts.mmap_pages, &sample_count,
		      &comm_count);
	if (err != TEST_OK)
		goto out_delete_evlist;

	if ((sample_count != NR_ITERS) || (comm_count != NR_ITERS)) {
		pr_err("Unexpected counter: sample_count=%d, comm_count=%d\n",
		       sample_count, comm_count);
		goto out_delete_evlist;
	}

	evlist__close(evlist);

	err = evlist__open(evlist);
	if (err < 0) {
		pr_debug("perf_evlist__open: %s\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	err = do_test(evlist, 1, &sample_count, &comm_count);
	if (err != TEST_OK)
		goto out_delete_evlist;

	ret = TEST_OK;
out_delete_evlist:
	evlist__delete(evlist);
	return ret;
}

DEFINE_SUITE("Read backward ring buffer", backward_ring_buffer);

// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "bench.h"
#include "../util/debug.h"
#include "../util/stat.h"
#include "../util/evlist.h"
#include "../util/evsel.h"
#include "../util/strbuf.h"
#include "../util/record.h"
#include "../util/parse-events.h"
#include "internal/threadmap.h"
#include "internal/cpumap.h"
#include <linux/perf_event.h>
#include <linux/kernel.h>
#include <linux/time64.h>
#include <linux/string.h>
#include <subcmd/parse-options.h>

#define MMAP_FLUSH_DEFAULT 1

static int iterations = 100;
static int nr_events = 1;
static const char *event_string = "dummy";

static inline u64 timeval2usec(struct timeval *tv)
{
	return tv->tv_sec * USEC_PER_SEC + tv->tv_usec;
}

static struct record_opts opts = {
	.sample_time	     = true,
	.mmap_pages	     = UINT_MAX,
	.user_freq	     = UINT_MAX,
	.user_interval	     = ULLONG_MAX,
	.freq		     = 4000,
	.target		     = {
		.uses_mmap   = true,
		.default_per_cpu = true,
	},
	.mmap_flush          = MMAP_FLUSH_DEFAULT,
	.nr_threads_synthesize = 1,
	.ctl_fd              = -1,
	.ctl_fd_ack          = -1,
};

static const struct option options[] = {
	OPT_STRING('e', "event", &event_string, "event", "event selector. use 'perf list' to list available events"),
	OPT_INTEGER('n', "nr-events", &nr_events,
		     "number of dummy events to create (default 1). If used with -e, it clones those events n times (1 = no change)"),
	OPT_INTEGER('i', "iterations", &iterations, "Number of iterations used to compute average (default=100)"),
	OPT_BOOLEAN('a', "all-cpus", &opts.target.system_wide, "system-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &opts.target.cpu_list, "cpu", "list of cpus where to open events"),
	OPT_STRING('p', "pid", &opts.target.pid, "pid", "record events on existing process id"),
	OPT_STRING('t', "tid", &opts.target.tid, "tid", "record events on existing thread id"),
	OPT_STRING('u', "uid", &opts.target.uid_str, "user", "user to profile"),
	OPT_BOOLEAN(0, "per-thread", &opts.target.per_thread, "use per-thread mmaps"),
	OPT_END()
};

static const char *const bench_usage[] = {
	"perf bench internals evlist-open-close <options>",
	NULL
};

static int evlist__count_evsel_fds(struct evlist *evlist)
{
	struct evsel *evsel;
	int cnt = 0;

	evlist__for_each_entry(evlist, evsel)
		cnt += evsel->core.threads->nr * perf_cpu_map__nr(evsel->core.cpus);

	return cnt;
}

static struct evlist *bench__create_evlist(char *evstr)
{
	struct parse_events_error err;
	struct evlist *evlist = evlist__new();
	int ret;

	if (!evlist) {
		pr_err("Not enough memory to create evlist\n");
		return NULL;
	}

	parse_events_error__init(&err);
	ret = parse_events(evlist, evstr, &err);
	if (ret) {
		parse_events_error__print(&err, evstr);
		parse_events_error__exit(&err);
		pr_err("Run 'perf list' for a list of valid events\n");
		ret = 1;
		goto out_delete_evlist;
	}
	parse_events_error__exit(&err);
	ret = evlist__create_maps(evlist, &opts.target);
	if (ret < 0) {
		pr_err("Not enough memory to create thread/cpu maps\n");
		goto out_delete_evlist;
	}

	evlist__config(evlist, &opts, NULL);

	return evlist;

out_delete_evlist:
	evlist__delete(evlist);
	return NULL;
}

static int bench__do_evlist_open_close(struct evlist *evlist)
{
	char sbuf[STRERR_BUFSIZE];
	int err = evlist__open(evlist);

	if (err < 0) {
		pr_err("evlist__open: %s\n", str_error_r(errno, sbuf, sizeof(sbuf)));
		return err;
	}

	err = evlist__mmap(evlist, opts.mmap_pages);
	if (err < 0) {
		pr_err("evlist__mmap: %s\n", str_error_r(errno, sbuf, sizeof(sbuf)));
		return err;
	}

	evlist__enable(evlist);
	evlist__disable(evlist);
	evlist__munmap(evlist);
	evlist__close(evlist);

	return 0;
}

static int bench_evlist_open_close__run(char *evstr)
{
	// used to print statistics only
	struct evlist *evlist = bench__create_evlist(evstr);
	double time_average, time_stddev;
	struct timeval start, end, diff;
	struct stats time_stats;
	u64 runtime_us;
	int i, err;

	if (!evlist)
		return -ENOMEM;

	init_stats(&time_stats);

	printf("  Number of cpus:\t%d\n", perf_cpu_map__nr(evlist->core.user_requested_cpus));
	printf("  Number of threads:\t%d\n", evlist->core.threads->nr);
	printf("  Number of events:\t%d (%d fds)\n",
		evlist->core.nr_entries, evlist__count_evsel_fds(evlist));
	printf("  Number of iterations:\t%d\n", iterations);

	evlist__delete(evlist);

	for (i = 0; i < iterations; i++) {
		pr_debug("Started iteration %d\n", i);
		evlist = bench__create_evlist(evstr);
		if (!evlist)
			return -ENOMEM;

		gettimeofday(&start, NULL);
		err = bench__do_evlist_open_close(evlist);
		if (err) {
			evlist__delete(evlist);
			return err;
		}

		gettimeofday(&end, NULL);
		timersub(&end, &start, &diff);
		runtime_us = timeval2usec(&diff);
		update_stats(&time_stats, runtime_us);

		evlist__delete(evlist);
		pr_debug("Iteration %d took:\t%" PRIu64 "us\n", i, runtime_us);
	}

	time_average = avg_stats(&time_stats);
	time_stddev = stddev_stats(&time_stats);
	printf("  Average open-close took: %.3f usec (+- %.3f usec)\n", time_average, time_stddev);

	return 0;
}

static char *bench__repeat_event_string(const char *evstr, int n)
{
	char sbuf[STRERR_BUFSIZE];
	struct strbuf buf;
	int i, str_size = strlen(evstr),
	    final_size = str_size * n + n,
	    err = strbuf_init(&buf, final_size);

	if (err) {
		pr_err("strbuf_init: %s\n", str_error_r(err, sbuf, sizeof(sbuf)));
		goto out_error;
	}

	for (i = 0; i < n; i++) {
		err = strbuf_add(&buf, evstr, str_size);
		if (err) {
			pr_err("strbuf_add: %s\n", str_error_r(err, sbuf, sizeof(sbuf)));
			goto out_error;
		}

		err = strbuf_addch(&buf, i == n-1 ? '\0' : ',');
		if (err) {
			pr_err("strbuf_addch: %s\n", str_error_r(err, sbuf, sizeof(sbuf)));
			goto out_error;
		}
	}

	return strbuf_detach(&buf, NULL);

out_error:
	strbuf_release(&buf);
	return NULL;
}


int bench_evlist_open_close(int argc, const char **argv)
{
	char *evstr, errbuf[BUFSIZ];
	int err;

	argc = parse_options(argc, argv, options, bench_usage, 0);
	if (argc) {
		usage_with_options(bench_usage, options);
		exit(EXIT_FAILURE);
	}

	err = target__validate(&opts.target);
	if (err) {
		target__strerror(&opts.target, err, errbuf, sizeof(errbuf));
		pr_err("%s\n", errbuf);
		goto out;
	}

	err = target__parse_uid(&opts.target);
	if (err) {
		target__strerror(&opts.target, err, errbuf, sizeof(errbuf));
		pr_err("%s", errbuf);
		goto out;
	}

	/* Enable ignoring missing threads when -u/-p option is defined. */
	opts.ignore_missing_thread = opts.target.uid != UINT_MAX || opts.target.pid;

	evstr = bench__repeat_event_string(event_string, nr_events);
	if (!evstr) {
		err = -ENOMEM;
		goto out;
	}

	err = bench_evlist_open_close__run(evstr);

	free(evstr);
out:
	return err;
}

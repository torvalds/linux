// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <stddef.h>
#include <ftw.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <linux/kernel.h>
#include <linux/time64.h>
#include <linux/list.h>
#include <linux/err.h>
#include <internal/lib.h>
#include <subcmd/parse-options.h>

#include "bench.h"
#include "util/data.h"
#include "util/stat.h"
#include "util/debug.h"
#include "util/event.h"
#include "util/symbol.h"
#include "util/session.h"
#include "util/build-id.h"
#include "util/synthetic-events.h"

#define MMAP_DEV_MAJOR  8
#define DSO_MMAP_RATIO  4

static unsigned int iterations = 100;
static unsigned int nr_mmaps   = 100;
static unsigned int nr_samples = 100;  /* samples per mmap */

static u64 bench_sample_type;
static u16 bench_id_hdr_size;

struct bench_data {
	int			pid;
	int			input_pipe[2];
	int			output_pipe[2];
	pthread_t		th;
};

struct bench_dso {
	struct list_head	list;
	char			*name;
	int			ino;
};

static int nr_dsos;
static struct bench_dso *dsos;

extern int cmd_inject(int argc, const char *argv[]);

static const struct option options[] = {
	OPT_UINTEGER('i', "iterations", &iterations,
		     "Number of iterations used to compute average (default: 100)"),
	OPT_UINTEGER('m', "nr-mmaps", &nr_mmaps,
		     "Number of mmap events for each iteration (default: 100)"),
	OPT_UINTEGER('n', "nr-samples", &nr_samples,
		     "Number of sample events per mmap event (default: 100)"),
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show iteration count, DSO name, etc)"),
	OPT_END()
};

static const char *const bench_usage[] = {
	"perf bench internals inject-build-id <options>",
	NULL
};

/*
 * Helper for collect_dso that adds the given file as a dso to dso_list
 * if it contains a build-id.  Stops after collecting 4 times more than
 * we need (for MMAP2 events).
 */
static int add_dso(const char *fpath, const struct stat *sb __maybe_unused,
		   int typeflag, struct FTW *ftwbuf __maybe_unused)
{
	struct bench_dso *dso = &dsos[nr_dsos];
	struct build_id bid;

	if (typeflag == FTW_D || typeflag == FTW_SL)
		return 0;

	if (filename__read_build_id(fpath, &bid) < 0)
		return 0;

	dso->name = realpath(fpath, NULL);
	if (dso->name == NULL)
		return -1;

	dso->ino = nr_dsos++;
	pr_debug2("  Adding DSO: %s\n", fpath);

	/* stop if we collected enough DSOs */
	if ((unsigned int)nr_dsos == DSO_MMAP_RATIO * nr_mmaps)
		return 1;

	return 0;
}

static void collect_dso(void)
{
	dsos = calloc(nr_mmaps * DSO_MMAP_RATIO, sizeof(*dsos));
	if (dsos == NULL) {
		printf("  Memory allocation failed\n");
		exit(1);
	}

	if (nftw("/usr/lib/", add_dso, 10, FTW_PHYS) < 0)
		return;

	pr_debug("  Collected %d DSOs\n", nr_dsos);
}

static void release_dso(void)
{
	int i;

	for (i = 0; i < nr_dsos; i++) {
		struct bench_dso *dso = &dsos[i];

		free(dso->name);
	}
	free(dsos);
}

/* Fake address used by mmap and sample events */
static u64 dso_map_addr(struct bench_dso *dso)
{
	return 0x400000ULL + dso->ino * 8192ULL;
}

static ssize_t synthesize_attr(struct bench_data *data)
{
	union perf_event event;

	memset(&event, 0, sizeof(event.attr) + sizeof(u64));

	event.header.type = PERF_RECORD_HEADER_ATTR;
	event.header.size = sizeof(event.attr) + sizeof(u64);

	event.attr.attr.type = PERF_TYPE_SOFTWARE;
	event.attr.attr.config = PERF_COUNT_SW_TASK_CLOCK;
	event.attr.attr.exclude_kernel = 1;
	event.attr.attr.sample_id_all = 1;
	event.attr.attr.sample_type = bench_sample_type;

	return writen(data->input_pipe[1], &event, event.header.size);
}

static ssize_t synthesize_fork(struct bench_data *data)
{
	union perf_event event;

	memset(&event, 0, sizeof(event.fork) + bench_id_hdr_size);

	event.header.type = PERF_RECORD_FORK;
	event.header.misc = PERF_RECORD_MISC_FORK_EXEC;
	event.header.size = sizeof(event.fork) + bench_id_hdr_size;

	event.fork.ppid = 1;
	event.fork.ptid = 1;
	event.fork.pid = data->pid;
	event.fork.tid = data->pid;

	return writen(data->input_pipe[1], &event, event.header.size);
}

static ssize_t synthesize_mmap(struct bench_data *data, struct bench_dso *dso, u64 timestamp)
{
	union perf_event event;
	size_t len = offsetof(struct perf_record_mmap2, filename);
	u64 *id_hdr_ptr = (void *)&event;
	int ts_idx;

	len += roundup(strlen(dso->name) + 1, 8) + bench_id_hdr_size;

	memset(&event, 0, min(len, sizeof(event.mmap2)));

	event.header.type = PERF_RECORD_MMAP2;
	event.header.misc = PERF_RECORD_MISC_USER;
	event.header.size = len;

	event.mmap2.pid = data->pid;
	event.mmap2.tid = data->pid;
	event.mmap2.maj = MMAP_DEV_MAJOR;
	event.mmap2.ino = dso->ino;

	strcpy(event.mmap2.filename, dso->name);

	event.mmap2.start = dso_map_addr(dso);
	event.mmap2.len = 4096;
	event.mmap2.prot = PROT_EXEC;

	if (len > sizeof(event.mmap2)) {
		/* write mmap2 event first */
		if (writen(data->input_pipe[1], &event, len - bench_id_hdr_size) < 0)
			return -1;
		/* zero-fill sample id header */
		memset(id_hdr_ptr, 0, bench_id_hdr_size);
		/* put timestamp in the right position */
		ts_idx = (bench_id_hdr_size / sizeof(u64)) - 2;
		id_hdr_ptr[ts_idx] = timestamp;
		if (writen(data->input_pipe[1], id_hdr_ptr, bench_id_hdr_size) < 0)
			return -1;

		return len;
	}

	ts_idx = (len / sizeof(u64)) - 2;
	id_hdr_ptr[ts_idx] = timestamp;
	return writen(data->input_pipe[1], &event, len);
}

static ssize_t synthesize_sample(struct bench_data *data, struct bench_dso *dso, u64 timestamp)
{
	union perf_event event;
	struct perf_sample sample = {
		.tid = data->pid,
		.pid = data->pid,
		.ip = dso_map_addr(dso),
		.time = timestamp,
	};

	event.header.type = PERF_RECORD_SAMPLE;
	event.header.misc = PERF_RECORD_MISC_USER;
	event.header.size = perf_event__sample_event_size(&sample, bench_sample_type, 0);

	perf_event__synthesize_sample(&event, bench_sample_type, 0, &sample);

	return writen(data->input_pipe[1], &event, event.header.size);
}

static ssize_t synthesize_flush(struct bench_data *data)
{
	struct perf_event_header header = {
		.size = sizeof(header),
		.type = PERF_RECORD_FINISHED_ROUND,
	};

	return writen(data->input_pipe[1], &header, header.size);
}

static void *data_reader(void *arg)
{
	struct bench_data *data = arg;
	char buf[8192];
	int flag;
	int n;

	flag = fcntl(data->output_pipe[0], F_GETFL);
	fcntl(data->output_pipe[0], F_SETFL, flag | O_NONBLOCK);

	/* read out data from child */
	while (true) {
		n = read(data->output_pipe[0], buf, sizeof(buf));
		if (n > 0)
			continue;
		if (n == 0)
			break;

		if (errno != EINTR && errno != EAGAIN)
			break;

		usleep(100);
	}

	close(data->output_pipe[0]);
	return NULL;
}

static int setup_injection(struct bench_data *data, bool build_id_all)
{
	int ready_pipe[2];
	int dev_null_fd;
	char buf;

	if (pipe(ready_pipe) < 0)
		return -1;

	if (pipe(data->input_pipe) < 0)
		return -1;

	if (pipe(data->output_pipe) < 0)
		return -1;

	data->pid = fork();
	if (data->pid < 0)
		return -1;

	if (data->pid == 0) {
		const char **inject_argv;
		int inject_argc = 2;

		close(data->input_pipe[1]);
		close(data->output_pipe[0]);
		close(ready_pipe[0]);

		dup2(data->input_pipe[0], STDIN_FILENO);
		close(data->input_pipe[0]);
		dup2(data->output_pipe[1], STDOUT_FILENO);
		close(data->output_pipe[1]);

		dev_null_fd = open("/dev/null", O_WRONLY);
		if (dev_null_fd < 0)
			exit(1);

		dup2(dev_null_fd, STDERR_FILENO);

		if (build_id_all)
			inject_argc++;

		inject_argv = calloc(inject_argc + 1, sizeof(*inject_argv));
		if (inject_argv == NULL)
			exit(1);

		inject_argv[0] = strdup("inject");
		inject_argv[1] = strdup("-b");
		if (build_id_all)
			inject_argv[2] = strdup("--buildid-all");

		/* signal that we're ready to go */
		close(ready_pipe[1]);

		cmd_inject(inject_argc, inject_argv);

		exit(0);
	}

	pthread_create(&data->th, NULL, data_reader, data);

	close(ready_pipe[1]);
	close(data->input_pipe[0]);
	close(data->output_pipe[1]);

	/* wait for child ready */
	if (read(ready_pipe[0], &buf, 1) < 0)
		return -1;
	close(ready_pipe[0]);

	return 0;
}

static int inject_build_id(struct bench_data *data, u64 *max_rss)
{
	int status;
	unsigned int i, k;
	struct rusage rusage;

	/* this makes the child to run */
	if (perf_header__write_pipe(data->input_pipe[1]) < 0)
		return -1;

	if (synthesize_attr(data) < 0)
		return -1;

	if (synthesize_fork(data) < 0)
		return -1;

	for (i = 0; i < nr_mmaps; i++) {
		int idx = rand() % (nr_dsos - 1);
		struct bench_dso *dso = &dsos[idx];
		u64 timestamp = rand() % 1000000;

		pr_debug2("   [%d] injecting: %s\n", i+1, dso->name);
		if (synthesize_mmap(data, dso, timestamp) < 0)
			return -1;

		for (k = 0; k < nr_samples; k++) {
			if (synthesize_sample(data, dso, timestamp + k * 1000) < 0)
				return -1;
		}

		if ((i + 1) % 10 == 0) {
			if (synthesize_flush(data) < 0)
				return -1;
		}
	}

	/* this makes the child to finish */
	close(data->input_pipe[1]);

	wait4(data->pid, &status, 0, &rusage);
	*max_rss = rusage.ru_maxrss;

	pr_debug("   Child %d exited with %d\n", data->pid, status);

	return 0;
}

static void do_inject_loop(struct bench_data *data, bool build_id_all)
{
	unsigned int i;
	struct stats time_stats, mem_stats;
	double time_average, time_stddev;
	double mem_average, mem_stddev;

	init_stats(&time_stats);
	init_stats(&mem_stats);

	pr_debug("  Build-id%s injection benchmark\n", build_id_all ? "-all" : "");

	for (i = 0; i < iterations; i++) {
		struct timeval start, end, diff;
		u64 runtime_us, max_rss;

		pr_debug("  Iteration #%d\n", i+1);

		if (setup_injection(data, build_id_all) < 0) {
			printf("  Build-id injection setup failed\n");
			break;
		}

		gettimeofday(&start, NULL);
		if (inject_build_id(data, &max_rss) < 0) {
			printf("  Build-id injection failed\n");
			break;
		}

		gettimeofday(&end, NULL);
		timersub(&end, &start, &diff);
		runtime_us = diff.tv_sec * USEC_PER_SEC + diff.tv_usec;
		update_stats(&time_stats, runtime_us);
		update_stats(&mem_stats, max_rss);

		pthread_join(data->th, NULL);
	}

	time_average = avg_stats(&time_stats) / USEC_PER_MSEC;
	time_stddev = stddev_stats(&time_stats) / USEC_PER_MSEC;
	printf("  Average build-id%s injection took: %.3f msec (+- %.3f msec)\n",
	       build_id_all ? "-all" : "", time_average, time_stddev);

	/* each iteration, it processes MMAP2 + BUILD_ID + nr_samples * SAMPLE */
	time_average = avg_stats(&time_stats) / (nr_mmaps * (nr_samples + 2));
	time_stddev = stddev_stats(&time_stats) / (nr_mmaps * (nr_samples + 2));
	printf("  Average time per event: %.3f usec (+- %.3f usec)\n",
		time_average, time_stddev);

	mem_average = avg_stats(&mem_stats);
	mem_stddev = stddev_stats(&mem_stats);
	printf("  Average memory usage: %.0f KB (+- %.0f KB)\n",
		mem_average, mem_stddev);
}

static int do_inject_loops(struct bench_data *data)
{

	srand(time(NULL));
	symbol__init(NULL);

	bench_sample_type  = PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_IP;
	bench_sample_type |= PERF_SAMPLE_TID | PERF_SAMPLE_TIME;
	bench_id_hdr_size  = 32;

	collect_dso();
	if (nr_dsos == 0) {
		printf("  Cannot collect DSOs for injection\n");
		return -1;
	}

	do_inject_loop(data, false);
	do_inject_loop(data, true);

	release_dso();
	return 0;
}

int bench_inject_build_id(int argc, const char **argv)
{
	struct bench_data data;

	argc = parse_options(argc, argv, options, bench_usage, 0);
	if (argc) {
		usage_with_options(bench_usage, options);
		exit(EXIT_FAILURE);
	}

	return do_inject_loops(&data);
}


/*
 *
 * sched-pipe.c
 *
 * pipe: Benchmark for pipe()
 *
 * Based on pipe-test-1m.c by Ingo Molnar <mingo@redhat.com>
 *  http://people.redhat.com/mingo/cfs-scheduler/tools/pipe-test-1m.c
 * Ported to perf by Hitoshi Mitake <mitake@dcl.info.waseda.ac.jp>
 *
 */

#include "../perf.h"
#include "../util/util.h"
#include "../util/parse-options.h"
#include "../builtin.h"
#include "bench.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <linux/unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>

#define LOOPS_DEFAULT 1000000
static int loops = LOOPS_DEFAULT;

static const struct option options[] = {
	OPT_INTEGER('l', "loop", &loops,
		    "Specify number of loops"),
	OPT_END()
};

static const char * const bench_sched_pipe_usage[] = {
	"perf bench sched pipe <options>",
	NULL
};

int bench_sched_pipe(int argc, const char **argv,
		     const char *prefix __used)
{
	int pipe_1[2], pipe_2[2];
	int m = 0, i;
	struct timeval start, stop, diff;
	unsigned long long result_usec = 0;

	/*
	 * why does "ret" exist?
	 * discarding returned value of read(), write()
	 * causes error in building environment for perf
	 */
	int ret, wait_stat;
	pid_t pid, retpid;

	argc = parse_options(argc, argv, options,
			     bench_sched_pipe_usage, 0);

	assert(!pipe(pipe_1));
	assert(!pipe(pipe_2));

	pid = fork();
	assert(pid >= 0);

	gettimeofday(&start, NULL);

	if (!pid) {
		for (i = 0; i < loops; i++) {
			ret = read(pipe_1[0], &m, sizeof(int));
			ret = write(pipe_2[1], &m, sizeof(int));
		}
	} else {
		for (i = 0; i < loops; i++) {
			ret = write(pipe_1[1], &m, sizeof(int));
			ret = read(pipe_2[0], &m, sizeof(int));
		}
	}

	gettimeofday(&stop, NULL);
	timersub(&stop, &start, &diff);

	if (pid) {
		retpid = waitpid(pid, &wait_stat, 0);
		assert((retpid == pid) && WIFEXITED(wait_stat));
	} else {
		exit(0);
	}

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		printf("# Extecuted %d pipe operations between two tasks\n\n",
			loops);

		result_usec = diff.tv_sec * 1000000;
		result_usec += diff.tv_usec;

		printf(" %14s: %lu.%03lu [sec]\n\n", "Total time",
		       diff.tv_sec,
		       (unsigned long) (diff.tv_usec/1000));

		printf(" %14lf usecs/op\n",
		       (double)result_usec / (double)loops);
		printf(" %14d ops/sec\n",
		       (int)((double)loops /
			     ((double)result_usec / (double)1000000)));
		break;

	case BENCH_FORMAT_SIMPLE:
		printf("%lu.%03lu\n",
		       diff.tv_sec,
		       (unsigned long) (diff.tv_usec / 1000));
		break;

	default:
		/* reaching here is something disaster */
		fprintf(stderr, "Unknown format:%d\n", bench_format);
		exit(1);
		break;
	}

	return 0;
}

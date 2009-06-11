/*
 * builtin-stat.c
 *
 * Builtin stat command: Give a precise performance counters summary
 * overview about any workload, CPU or specific PID.
 *
 * Sample output:

   $ perf stat ~/hackbench 10
   Time: 0.104

    Performance counter stats for '/home/mingo/hackbench':

       1255.538611  task clock ticks     #      10.143 CPU utilization factor
             54011  context switches     #       0.043 M/sec
               385  CPU migrations       #       0.000 M/sec
             17755  pagefaults           #       0.014 M/sec
        3808323185  CPU cycles           #    3033.219 M/sec
        1575111190  instructions         #    1254.530 M/sec
          17367895  cache references     #      13.833 M/sec
           7674421  cache misses         #       6.112 M/sec

    Wall-clock time elapsed:   123.786620 msecs

 *
 * Copyright (C) 2008, Red Hat Inc, Ingo Molnar <mingo@redhat.com>
 *
 * Improvements and fixes by:
 *
 *   Arjan van de Ven <arjan@linux.intel.com>
 *   Yanmin Zhang <yanmin.zhang@intel.com>
 *   Wu Fengguang <fengguang.wu@intel.com>
 *   Mike Galbraith <efault@gmx.de>
 *   Paul Mackerras <paulus@samba.org>
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include "perf.h"
#include "builtin.h"
#include "util/util.h"
#include "util/parse-options.h"
#include "util/parse-events.h"

#include <sys/prctl.h>

static struct perf_counter_attr default_attrs[MAX_COUNTERS] = {

  { .type = PERF_TYPE_SOFTWARE, .config = PERF_COUNT_SW_TASK_CLOCK	},
  { .type = PERF_TYPE_SOFTWARE, .config = PERF_COUNT_SW_CONTEXT_SWITCHES},
  { .type = PERF_TYPE_SOFTWARE, .config = PERF_COUNT_SW_CPU_MIGRATIONS	},
  { .type = PERF_TYPE_SOFTWARE, .config = PERF_COUNT_SW_PAGE_FAULTS	},

  { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_CPU_CYCLES	},
  { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_INSTRUCTIONS	},
  { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_CACHE_REFERENCES},
  { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_CACHE_MISSES	},

};

static int			system_wide			=  0;
static int			inherit				=  1;
static int			verbose				=  0;

static int			fd[MAX_NR_CPUS][MAX_COUNTERS];

static int			target_pid			= -1;
static int			nr_cpus				=  0;
static unsigned int		page_size;

static int			scale				=  1;

static const unsigned int default_count[] = {
	1000000,
	1000000,
	  10000,
	  10000,
	1000000,
	  10000,
};

static __u64			event_res[MAX_COUNTERS][3];
static __u64			event_scaled[MAX_COUNTERS];

static __u64			runtime_nsecs;
static __u64			walltime_nsecs;
static __u64			runtime_cycles;

static void create_perf_stat_counter(int counter)
{
	struct perf_counter_attr *attr = attrs + counter;

	if (scale)
		attr->read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
				    PERF_FORMAT_TOTAL_TIME_RUNNING;

	if (system_wide) {
		int cpu;
		for (cpu = 0; cpu < nr_cpus; cpu ++) {
			fd[cpu][counter] = sys_perf_counter_open(attr, -1, cpu, -1, 0);
			if (fd[cpu][counter] < 0 && verbose) {
				printf("Error: counter %d, sys_perf_counter_open() syscall returned with %d (%s)\n", counter, fd[cpu][counter], strerror(errno));
			}
		}
	} else {
		attr->inherit	= inherit;
		attr->disabled	= 1;

		fd[0][counter] = sys_perf_counter_open(attr, 0, -1, -1, 0);
		if (fd[0][counter] < 0 && verbose) {
			printf("Error: counter %d, sys_perf_counter_open() syscall returned with %d (%s)\n", counter, fd[0][counter], strerror(errno));
		}
	}
}

/*
 * Does the counter have nsecs as a unit?
 */
static inline int nsec_counter(int counter)
{
	if (attrs[counter].type != PERF_TYPE_SOFTWARE)
		return 0;

	if (attrs[counter].config == PERF_COUNT_SW_CPU_CLOCK)
		return 1;

	if (attrs[counter].config == PERF_COUNT_SW_TASK_CLOCK)
		return 1;

	return 0;
}

/*
 * Read out the results of a single counter:
 */
static void read_counter(int counter)
{
	__u64 *count, single_count[3];
	ssize_t res;
	int cpu, nv;
	int scaled;

	count = event_res[counter];

	count[0] = count[1] = count[2] = 0;

	nv = scale ? 3 : 1;
	for (cpu = 0; cpu < nr_cpus; cpu ++) {
		if (fd[cpu][counter] < 0)
			continue;

		res = read(fd[cpu][counter], single_count, nv * sizeof(__u64));
		assert(res == nv * sizeof(__u64));

		count[0] += single_count[0];
		if (scale) {
			count[1] += single_count[1];
			count[2] += single_count[2];
		}
	}

	scaled = 0;
	if (scale) {
		if (count[2] == 0) {
			event_scaled[counter] = -1;
			count[0] = 0;
			return;
		}

		if (count[2] < count[1]) {
			event_scaled[counter] = 1;
			count[0] = (unsigned long long)
				((double)count[0] * count[1] / count[2] + 0.5);
		}
	}
	/*
	 * Save the full runtime - to allow normalization during printout:
	 */
	if (attrs[counter].type == PERF_TYPE_SOFTWARE &&
		attrs[counter].config == PERF_COUNT_SW_TASK_CLOCK)
		runtime_nsecs = count[0];
	if (attrs[counter].type == PERF_TYPE_HARDWARE &&
		attrs[counter].config == PERF_COUNT_HW_CPU_CYCLES)
		runtime_cycles = count[0];
}

/*
 * Print out the results of a single counter:
 */
static void print_counter(int counter)
{
	__u64 *count;
	int scaled;

	count = event_res[counter];
	scaled = event_scaled[counter];

	if (scaled == -1) {
		fprintf(stderr, " %14s  %-20s\n",
			"<not counted>", event_name(counter));
		return;
	}

	if (nsec_counter(counter)) {
		double msecs = (double)count[0] / 1000000;

		fprintf(stderr, " %14.6f  %-20s",
			msecs, event_name(counter));
		if (attrs[counter].type == PERF_TYPE_SOFTWARE &&
			attrs[counter].config == PERF_COUNT_SW_TASK_CLOCK) {

			if (walltime_nsecs)
				fprintf(stderr, " # %11.3f CPU utilization factor",
					(double)count[0] / (double)walltime_nsecs);
		}
	} else {
		fprintf(stderr, " %14Ld  %-20s",
			count[0], event_name(counter));
		if (runtime_nsecs)
			fprintf(stderr, " # %11.3f M/sec",
				(double)count[0]/runtime_nsecs*1000.0);
		if (runtime_cycles &&
			attrs[counter].type == PERF_TYPE_HARDWARE &&
				attrs[counter].config == PERF_COUNT_HW_INSTRUCTIONS) {

			fprintf(stderr, " # %1.3f per cycle",
				(double)count[0] / (double)runtime_cycles);
		}
	}
	if (scaled)
		fprintf(stderr, "  (scaled from %.2f%%)",
			(double) count[2] / count[1] * 100);
	fprintf(stderr, "\n");
}

static int do_perf_stat(int argc, const char **argv)
{
	unsigned long long t0, t1;
	int counter;
	int status;
	int pid;
	int i;

	if (!system_wide)
		nr_cpus = 1;

	for (counter = 0; counter < nr_counters; counter++)
		create_perf_stat_counter(counter);

	/*
	 * Enable counters and exec the command:
	 */
	t0 = rdclock();
	prctl(PR_TASK_PERF_COUNTERS_ENABLE);

	if ((pid = fork()) < 0)
		perror("failed to fork");

	if (!pid) {
		if (execvp(argv[0], (char **)argv)) {
			perror(argv[0]);
			exit(-1);
		}
	}

	while (wait(&status) >= 0)
		;

	prctl(PR_TASK_PERF_COUNTERS_DISABLE);
	t1 = rdclock();

	walltime_nsecs = t1 - t0;

	fflush(stdout);

	fprintf(stderr, "\n");
	fprintf(stderr, " Performance counter stats for \'%s", argv[0]);

	for (i = 1; i < argc; i++)
		fprintf(stderr, " %s", argv[i]);

	fprintf(stderr, "\':\n");
	fprintf(stderr, "\n");

	for (counter = 0; counter < nr_counters; counter++)
		read_counter(counter);

	for (counter = 0; counter < nr_counters; counter++)
		print_counter(counter);


	fprintf(stderr, "\n");
	fprintf(stderr, " Wall-clock time elapsed: %12.6f msecs\n",
			(double)(t1-t0)/1e6);
	fprintf(stderr, "\n");

	return 0;
}

static volatile int signr = -1;

static void skip_signal(int signo)
{
	signr = signo;
}

static void sig_atexit(void)
{
	if (signr == -1)
		return;

	signal(signr, SIG_DFL);
	kill(getpid(), signr);
}

static const char * const stat_usage[] = {
	"perf stat [<options>] <command>",
	NULL
};

static const struct option options[] = {
	OPT_CALLBACK('e', "event", NULL, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events),
	OPT_BOOLEAN('i', "inherit", &inherit,
		    "child tasks inherit counters"),
	OPT_INTEGER('p', "pid", &target_pid,
		    "stat events on existing pid"),
	OPT_BOOLEAN('a', "all-cpus", &system_wide,
			    "system-wide collection from all CPUs"),
	OPT_BOOLEAN('S', "scale", &scale,
			    "scale/normalize counters"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_END()
};

int cmd_stat(int argc, const char **argv, const char *prefix)
{
	page_size = sysconf(_SC_PAGE_SIZE);

	memcpy(attrs, default_attrs, sizeof(attrs));

	argc = parse_options(argc, argv, options, stat_usage, 0);
	if (!argc)
		usage_with_options(stat_usage, options);

	if (!nr_counters)
		nr_counters = 8;

	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	assert(nr_cpus <= MAX_NR_CPUS);
	assert(nr_cpus >= 0);

	/*
	 * We dont want to block the signals - that would cause
	 * child tasks to inherit that and Ctrl-C would not work.
	 * What we want is for Ctrl-C to work in the exec()-ed
	 * task, but being ignored by perf stat itself:
	 */
	atexit(sig_atexit);
	signal(SIGINT,  skip_signal);
	signal(SIGALRM, skip_signal);
	signal(SIGABRT, skip_signal);

	return do_perf_stat(argc, argv);
}

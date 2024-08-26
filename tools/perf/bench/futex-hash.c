// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013  Davidlohr Bueso <davidlohr@hp.com>
 *
 * futex-hash: Stress the hell out of the Linux kernel futex uaddr hashing.
 *
 * This program is particularly useful for measuring the kernel's futex hash
 * table/function implementation. In order for it to make sense, use with as
 * many threads and futexes as possible.
 */

/* For the CLR_() macros */
#include <string.h>
#include <pthread.h>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <perf/cpumap.h>

#include "../util/mutex.h"
#include "../util/stat.h"
#include <subcmd/parse-options.h>
#include "bench.h"
#include "futex.h"

#include <err.h>

static bool done = false;
static int futex_flag = 0;

struct timeval bench__start, bench__end, bench__runtime;
static struct mutex thread_lock;
static unsigned int threads_starting;
static struct stats throughput_stats;
static struct cond thread_parent, thread_worker;

struct worker {
	int tid;
	u_int32_t *futex;
	pthread_t thread;
	unsigned long ops;
};

static struct bench_futex_parameters params = {
	.nfutexes = 1024,
	.runtime  = 10,
};

static const struct option options[] = {
	OPT_UINTEGER('t', "threads", &params.nthreads, "Specify amount of threads"),
	OPT_UINTEGER('r', "runtime", &params.runtime, "Specify runtime (in seconds)"),
	OPT_UINTEGER('f', "futexes", &params.nfutexes, "Specify amount of futexes per threads"),
	OPT_BOOLEAN( 's', "silent",  &params.silent, "Silent mode: do not display data/details"),
	OPT_BOOLEAN( 'S', "shared",  &params.fshared, "Use shared futexes instead of private ones"),
	OPT_BOOLEAN( 'm', "mlockall", &params.mlockall, "Lock all current and future memory"),
	OPT_END()
};

static const char * const bench_futex_hash_usage[] = {
	"perf bench futex hash <options>",
	NULL
};

static void *workerfn(void *arg)
{
	int ret;
	struct worker *w = (struct worker *) arg;
	unsigned int i;
	unsigned long ops = w->ops; /* avoid cacheline bouncing */

	mutex_lock(&thread_lock);
	threads_starting--;
	if (!threads_starting)
		cond_signal(&thread_parent);
	cond_wait(&thread_worker, &thread_lock);
	mutex_unlock(&thread_lock);

	do {
		for (i = 0; i < params.nfutexes; i++, ops++) {
			/*
			 * We want the futex calls to fail in order to stress
			 * the hashing of uaddr and not measure other steps,
			 * such as internal waitqueue handling, thus enlarging
			 * the critical region protected by hb->lock.
			 */
			ret = futex_wait(&w->futex[i], 1234, NULL, futex_flag);
			if (!params.silent &&
			    (!ret || errno != EAGAIN || errno != EWOULDBLOCK))
				warn("Non-expected futex return call");
		}
	}  while (!done);

	w->ops = ops;
	return NULL;
}

static void toggle_done(int sig __maybe_unused,
			siginfo_t *info __maybe_unused,
			void *uc __maybe_unused)
{
	/* inform all threads that we're done for the day */
	done = true;
	gettimeofday(&bench__end, NULL);
	timersub(&bench__end, &bench__start, &bench__runtime);
}

static void print_summary(void)
{
	unsigned long avg = avg_stats(&throughput_stats);
	double stddev = stddev_stats(&throughput_stats);

	printf("%sAveraged %ld operations/sec (+- %.2f%%), total secs = %d\n",
	       !params.silent ? "\n" : "", avg, rel_stddev_stats(stddev, avg),
	       (int)bench__runtime.tv_sec);
}

int bench_futex_hash(int argc, const char **argv)
{
	int ret = 0;
	cpu_set_t *cpuset;
	struct sigaction act;
	unsigned int i;
	pthread_attr_t thread_attr;
	struct worker *worker = NULL;
	struct perf_cpu_map *cpu;
	int nrcpus;
	size_t size;

	argc = parse_options(argc, argv, options, bench_futex_hash_usage, 0);
	if (argc) {
		usage_with_options(bench_futex_hash_usage, options);
		exit(EXIT_FAILURE);
	}

	cpu = perf_cpu_map__new_online_cpus();
	if (!cpu)
		goto errmem;

	memset(&act, 0, sizeof(act));
	sigfillset(&act.sa_mask);
	act.sa_sigaction = toggle_done;
	sigaction(SIGINT, &act, NULL);

	if (params.mlockall) {
		if (mlockall(MCL_CURRENT | MCL_FUTURE))
			err(EXIT_FAILURE, "mlockall");
	}

	if (!params.nthreads) /* default to the number of CPUs */
		params.nthreads = perf_cpu_map__nr(cpu);

	worker = calloc(params.nthreads, sizeof(*worker));
	if (!worker)
		goto errmem;

	if (!params.fshared)
		futex_flag = FUTEX_PRIVATE_FLAG;

	printf("Run summary [PID %d]: %d threads, each operating on %d [%s] futexes for %d secs.\n\n",
	       getpid(), params.nthreads, params.nfutexes, params.fshared ? "shared":"private", params.runtime);

	init_stats(&throughput_stats);
	mutex_init(&thread_lock);
	cond_init(&thread_parent);
	cond_init(&thread_worker);

	threads_starting = params.nthreads;
	pthread_attr_init(&thread_attr);
	gettimeofday(&bench__start, NULL);

	nrcpus = cpu__max_cpu().cpu;
	cpuset = CPU_ALLOC(nrcpus);
	BUG_ON(!cpuset);
	size = CPU_ALLOC_SIZE(nrcpus);

	for (i = 0; i < params.nthreads; i++) {
		worker[i].tid = i;
		worker[i].futex = calloc(params.nfutexes, sizeof(*worker[i].futex));
		if (!worker[i].futex)
			goto errmem;

		CPU_ZERO_S(size, cpuset);

		CPU_SET_S(perf_cpu_map__cpu(cpu, i % perf_cpu_map__nr(cpu)).cpu, size, cpuset);
		ret = pthread_attr_setaffinity_np(&thread_attr, size, cpuset);
		if (ret) {
			CPU_FREE(cpuset);
			err(EXIT_FAILURE, "pthread_attr_setaffinity_np");
		}
		ret = pthread_create(&worker[i].thread, &thread_attr, workerfn,
				     (void *)(struct worker *) &worker[i]);
		if (ret) {
			CPU_FREE(cpuset);
			err(EXIT_FAILURE, "pthread_create");
		}

	}
	CPU_FREE(cpuset);
	pthread_attr_destroy(&thread_attr);

	mutex_lock(&thread_lock);
	while (threads_starting)
		cond_wait(&thread_parent, &thread_lock);
	cond_broadcast(&thread_worker);
	mutex_unlock(&thread_lock);

	sleep(params.runtime);
	toggle_done(0, NULL, NULL);

	for (i = 0; i < params.nthreads; i++) {
		ret = pthread_join(worker[i].thread, NULL);
		if (ret)
			err(EXIT_FAILURE, "pthread_join");
	}

	/* cleanup & report results */
	cond_destroy(&thread_parent);
	cond_destroy(&thread_worker);
	mutex_destroy(&thread_lock);

	for (i = 0; i < params.nthreads; i++) {
		unsigned long t = bench__runtime.tv_sec > 0 ?
			worker[i].ops / bench__runtime.tv_sec : 0;
		update_stats(&throughput_stats, t);
		if (!params.silent) {
			if (params.nfutexes == 1)
				printf("[thread %2d] futex: %p [ %ld ops/sec ]\n",
				       worker[i].tid, &worker[i].futex[0], t);
			else
				printf("[thread %2d] futexes: %p ... %p [ %ld ops/sec ]\n",
				       worker[i].tid, &worker[i].futex[0],
				       &worker[i].futex[params.nfutexes-1], t);
		}

		zfree(&worker[i].futex);
	}

	print_summary();

	free(worker);
	free(cpu);
	return ret;
errmem:
	err(EXIT_FAILURE, "calloc");
}

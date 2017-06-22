/*
 * Copyright (C) 2015 Davidlohr Bueso.
 */

/* For the CLR_() macros */
#include <pthread.h>

#include <signal.h>
#include "../util/stat.h"
#include <subcmd/parse-options.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <errno.h>
#include "bench.h"
#include "futex.h"

#include <err.h>
#include <stdlib.h>
#include <sys/time.h>

struct worker {
	int tid;
	u_int32_t *futex;
	pthread_t thread;
	unsigned long ops;
};

static u_int32_t global_futex = 0;
static struct worker *worker;
static unsigned int nsecs = 10;
static bool silent = false, multi = false;
static bool done = false, fshared = false;
static unsigned int ncpus, nthreads = 0;
static int futex_flag = 0;
struct timeval start, end, runtime;
static pthread_mutex_t thread_lock;
static unsigned int threads_starting;
static struct stats throughput_stats;
static pthread_cond_t thread_parent, thread_worker;

static const struct option options[] = {
	OPT_UINTEGER('t', "threads",  &nthreads, "Specify amount of threads"),
	OPT_UINTEGER('r', "runtime", &nsecs,     "Specify runtime (in seconds)"),
	OPT_BOOLEAN( 'M', "multi",   &multi,     "Use multiple futexes"),
	OPT_BOOLEAN( 's', "silent",  &silent,    "Silent mode: do not display data/details"),
	OPT_BOOLEAN( 'S', "shared",  &fshared,   "Use shared futexes instead of private ones"),
	OPT_END()
};

static const char * const bench_futex_lock_pi_usage[] = {
	"perf bench futex lock-pi <options>",
	NULL
};

static void print_summary(void)
{
	unsigned long avg = avg_stats(&throughput_stats);
	double stddev = stddev_stats(&throughput_stats);

	printf("%sAveraged %ld operations/sec (+- %.2f%%), total secs = %d\n",
	       !silent ? "\n" : "", avg, rel_stddev_stats(stddev, avg),
	       (int) runtime.tv_sec);
}

static void toggle_done(int sig __maybe_unused,
			siginfo_t *info __maybe_unused,
			void *uc __maybe_unused)
{
	/* inform all threads that we're done for the day */
	done = true;
	gettimeofday(&end, NULL);
	timersub(&end, &start, &runtime);
}

static void *workerfn(void *arg)
{
	struct worker *w = (struct worker *) arg;
	unsigned long ops = w->ops;

	pthread_mutex_lock(&thread_lock);
	threads_starting--;
	if (!threads_starting)
		pthread_cond_signal(&thread_parent);
	pthread_cond_wait(&thread_worker, &thread_lock);
	pthread_mutex_unlock(&thread_lock);

	do {
		int ret;
	again:
		ret = futex_lock_pi(w->futex, NULL, futex_flag);

		if (ret) { /* handle lock acquisition */
			if (!silent)
				warn("thread %d: Could not lock pi-lock for %p (%d)",
				     w->tid, w->futex, ret);
			if (done)
				break;

			goto again;
		}

		usleep(1);
		ret = futex_unlock_pi(w->futex, futex_flag);
		if (ret && !silent)
			warn("thread %d: Could not unlock pi-lock for %p (%d)",
			     w->tid, w->futex, ret);
		ops++; /* account for thread's share of work */
	}  while (!done);

	w->ops = ops;
	return NULL;
}

static void create_threads(struct worker *w, pthread_attr_t thread_attr)
{
	cpu_set_t cpu;
	unsigned int i;

	threads_starting = nthreads;

	for (i = 0; i < nthreads; i++) {
		worker[i].tid = i;

		if (multi) {
			worker[i].futex = calloc(1, sizeof(u_int32_t));
			if (!worker[i].futex)
				err(EXIT_FAILURE, "calloc");
		} else
			worker[i].futex = &global_futex;

		CPU_ZERO(&cpu);
		CPU_SET(i % ncpus, &cpu);

		if (pthread_attr_setaffinity_np(&thread_attr, sizeof(cpu_set_t), &cpu))
			err(EXIT_FAILURE, "pthread_attr_setaffinity_np");

		if (pthread_create(&w[i].thread, &thread_attr, workerfn, &worker[i]))
			err(EXIT_FAILURE, "pthread_create");
	}
}

int bench_futex_lock_pi(int argc, const char **argv,
			const char *prefix __maybe_unused)
{
	int ret = 0;
	unsigned int i;
	struct sigaction act;
	pthread_attr_t thread_attr;

	argc = parse_options(argc, argv, options, bench_futex_lock_pi_usage, 0);
	if (argc)
		goto err;

	ncpus = sysconf(_SC_NPROCESSORS_ONLN);

	sigfillset(&act.sa_mask);
	act.sa_sigaction = toggle_done;
	sigaction(SIGINT, &act, NULL);

	if (!nthreads)
		nthreads = ncpus;

	worker = calloc(nthreads, sizeof(*worker));
	if (!worker)
		err(EXIT_FAILURE, "calloc");

	if (!fshared)
		futex_flag = FUTEX_PRIVATE_FLAG;

	printf("Run summary [PID %d]: %d threads doing pi lock/unlock pairing for %d secs.\n\n",
	       getpid(), nthreads, nsecs);

	init_stats(&throughput_stats);
	pthread_mutex_init(&thread_lock, NULL);
	pthread_cond_init(&thread_parent, NULL);
	pthread_cond_init(&thread_worker, NULL);

	threads_starting = nthreads;
	pthread_attr_init(&thread_attr);
	gettimeofday(&start, NULL);

	create_threads(worker, thread_attr);
	pthread_attr_destroy(&thread_attr);

	pthread_mutex_lock(&thread_lock);
	while (threads_starting)
		pthread_cond_wait(&thread_parent, &thread_lock);
	pthread_cond_broadcast(&thread_worker);
	pthread_mutex_unlock(&thread_lock);

	sleep(nsecs);
	toggle_done(0, NULL, NULL);

	for (i = 0; i < nthreads; i++) {
		ret = pthread_join(worker[i].thread, NULL);
		if (ret)
			err(EXIT_FAILURE, "pthread_join");
	}

	/* cleanup & report results */
	pthread_cond_destroy(&thread_parent);
	pthread_cond_destroy(&thread_worker);
	pthread_mutex_destroy(&thread_lock);

	for (i = 0; i < nthreads; i++) {
		unsigned long t = worker[i].ops/runtime.tv_sec;

		update_stats(&throughput_stats, t);
		if (!silent)
			printf("[thread %3d] futex: %p [ %ld ops/sec ]\n",
			       worker[i].tid, worker[i].futex, t);

		if (multi)
			free(worker[i].futex);
	}

	print_summary();

	free(worker);
	return ret;
err:
	usage_with_options(bench_futex_lock_pi_usage, options);
	exit(EXIT_FAILURE);
}

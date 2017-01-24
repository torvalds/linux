/*
 * Copyright (C) 2013  Davidlohr Bueso <davidlohr@hp.com>
 *
 * futex-requeue: Block a bunch of threads on futex1 and requeue them
 *                on futex2, N at a time.
 *
 * This program is particularly useful to measure the latency of nthread
 * requeues without waking up any tasks -- thus mimicking a regular futex_wait.
 */

/* For the CLR_() macros */
#include <pthread.h>

#include <signal.h>
#include "../util/stat.h"
#include <subcmd/parse-options.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/time64.h>
#include <errno.h>
#include "bench.h"
#include "futex.h"

#include <err.h>
#include <stdlib.h>
#include <sys/time.h>

static u_int32_t futex1 = 0, futex2 = 0;

/*
 * How many tasks to requeue at a time.
 * Default to 1 in order to make the kernel work more.
 */
static unsigned int nrequeue = 1;

static pthread_t *worker;
static bool done = false, silent = false, fshared = false;
static pthread_mutex_t thread_lock;
static pthread_cond_t thread_parent, thread_worker;
static struct stats requeuetime_stats, requeued_stats;
static unsigned int ncpus, threads_starting, nthreads = 0;
static int futex_flag = 0;

static const struct option options[] = {
	OPT_UINTEGER('t', "threads",  &nthreads, "Specify amount of threads"),
	OPT_UINTEGER('q', "nrequeue", &nrequeue, "Specify amount of threads to requeue at once"),
	OPT_BOOLEAN( 's', "silent",   &silent,   "Silent mode: do not display data/details"),
	OPT_BOOLEAN( 'S', "shared",   &fshared,  "Use shared futexes instead of private ones"),
	OPT_END()
};

static const char * const bench_futex_requeue_usage[] = {
	"perf bench futex requeue <options>",
	NULL
};

static void print_summary(void)
{
	double requeuetime_avg = avg_stats(&requeuetime_stats);
	double requeuetime_stddev = stddev_stats(&requeuetime_stats);
	unsigned int requeued_avg = avg_stats(&requeued_stats);

	printf("Requeued %d of %d threads in %.4f ms (+-%.2f%%)\n",
	       requeued_avg,
	       nthreads,
	       requeuetime_avg / USEC_PER_MSEC,
	       rel_stddev_stats(requeuetime_stddev, requeuetime_avg));
}

static void *workerfn(void *arg __maybe_unused)
{
	pthread_mutex_lock(&thread_lock);
	threads_starting--;
	if (!threads_starting)
		pthread_cond_signal(&thread_parent);
	pthread_cond_wait(&thread_worker, &thread_lock);
	pthread_mutex_unlock(&thread_lock);

	futex_wait(&futex1, 0, NULL, futex_flag);
	return NULL;
}

static void block_threads(pthread_t *w,
			  pthread_attr_t thread_attr)
{
	cpu_set_t cpu;
	unsigned int i;

	threads_starting = nthreads;

	/* create and block all threads */
	for (i = 0; i < nthreads; i++) {
		CPU_ZERO(&cpu);
		CPU_SET(i % ncpus, &cpu);

		if (pthread_attr_setaffinity_np(&thread_attr, sizeof(cpu_set_t), &cpu))
			err(EXIT_FAILURE, "pthread_attr_setaffinity_np");

		if (pthread_create(&w[i], &thread_attr, workerfn, NULL))
			err(EXIT_FAILURE, "pthread_create");
	}
}

static void toggle_done(int sig __maybe_unused,
			siginfo_t *info __maybe_unused,
			void *uc __maybe_unused)
{
	done = true;
}

int bench_futex_requeue(int argc, const char **argv,
			const char *prefix __maybe_unused)
{
	int ret = 0;
	unsigned int i, j;
	struct sigaction act;
	pthread_attr_t thread_attr;

	argc = parse_options(argc, argv, options, bench_futex_requeue_usage, 0);
	if (argc)
		goto err;

	ncpus = sysconf(_SC_NPROCESSORS_ONLN);

	sigfillset(&act.sa_mask);
	act.sa_sigaction = toggle_done;
	sigaction(SIGINT, &act, NULL);

	if (!nthreads)
		nthreads = ncpus;
	else
		nthreads = futexbench_sanitize_numeric(nthreads);

	worker = calloc(nthreads, sizeof(*worker));
	if (!worker)
		err(EXIT_FAILURE, "calloc");

	if (!fshared)
		futex_flag = FUTEX_PRIVATE_FLAG;

	if (nrequeue > nthreads)
		nrequeue = nthreads;

	printf("Run summary [PID %d]: Requeuing %d threads (from [%s] %p to %p), "
	       "%d at a time.\n\n",  getpid(), nthreads,
	       fshared ? "shared":"private", &futex1, &futex2, nrequeue);

	init_stats(&requeued_stats);
	init_stats(&requeuetime_stats);
	pthread_attr_init(&thread_attr);
	pthread_mutex_init(&thread_lock, NULL);
	pthread_cond_init(&thread_parent, NULL);
	pthread_cond_init(&thread_worker, NULL);

	for (j = 0; j < bench_repeat && !done; j++) {
		unsigned int nrequeued = 0;
		struct timeval start, end, runtime;

		/* create, launch & block all threads */
		block_threads(worker, thread_attr);

		/* make sure all threads are already blocked */
		pthread_mutex_lock(&thread_lock);
		while (threads_starting)
			pthread_cond_wait(&thread_parent, &thread_lock);
		pthread_cond_broadcast(&thread_worker);
		pthread_mutex_unlock(&thread_lock);

		usleep(100000);

		/* Ok, all threads are patiently blocked, start requeueing */
		gettimeofday(&start, NULL);
		while (nrequeued < nthreads) {
			/*
			 * Do not wakeup any tasks blocked on futex1, allowing
			 * us to really measure futex_wait functionality.
			 */
			nrequeued += futex_cmp_requeue(&futex1, 0, &futex2, 0,
						       nrequeue, futex_flag);
		}

		gettimeofday(&end, NULL);
		timersub(&end, &start, &runtime);

		update_stats(&requeued_stats, nrequeued);
		update_stats(&requeuetime_stats, runtime.tv_usec);

		if (!silent) {
			printf("[Run %d]: Requeued %d of %d threads in %.4f ms\n",
			       j + 1, nrequeued, nthreads, runtime.tv_usec / (double)USEC_PER_MSEC);
		}

		/* everybody should be blocked on futex2, wake'em up */
		nrequeued = futex_wake(&futex2, nrequeued, futex_flag);
		if (nthreads != nrequeued)
			warnx("couldn't wakeup all tasks (%d/%d)", nrequeued, nthreads);

		for (i = 0; i < nthreads; i++) {
			ret = pthread_join(worker[i], NULL);
			if (ret)
				err(EXIT_FAILURE, "pthread_join");
		}
	}

	/* cleanup & report results */
	pthread_cond_destroy(&thread_parent);
	pthread_cond_destroy(&thread_worker);
	pthread_mutex_destroy(&thread_lock);
	pthread_attr_destroy(&thread_attr);

	print_summary();

	free(worker);
	return ret;
err:
	usage_with_options(bench_futex_requeue_usage, options);
	exit(EXIT_FAILURE);
}

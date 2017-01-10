/*
 * Copyright (C) 2015 Davidlohr Bueso.
 *
 * Block a bunch of threads and let parallel waker threads wakeup an
 * equal amount of them. The program output reflects the avg latency
 * for each individual thread to service its share of work. Ultimately
 * it can be used to measure futex_wake() changes.
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

struct thread_data {
	pthread_t worker;
	unsigned int nwoken;
	struct timeval runtime;
};

static unsigned int nwakes = 1;

/* all threads will block on the same futex -- hash bucket chaos ;) */
static u_int32_t futex = 0;

static pthread_t *blocked_worker;
static bool done = false, silent = false, fshared = false;
static unsigned int nblocked_threads = 0, nwaking_threads = 0;
static pthread_mutex_t thread_lock;
static pthread_cond_t thread_parent, thread_worker;
static struct stats waketime_stats, wakeup_stats;
static unsigned int ncpus, threads_starting;
static int futex_flag = 0;

static const struct option options[] = {
	OPT_UINTEGER('t', "threads", &nblocked_threads, "Specify amount of threads"),
	OPT_UINTEGER('w', "nwakers", &nwaking_threads, "Specify amount of waking threads"),
	OPT_BOOLEAN( 's', "silent",  &silent,   "Silent mode: do not display data/details"),
	OPT_BOOLEAN( 'S', "shared",  &fshared,  "Use shared futexes instead of private ones"),
	OPT_END()
};

static const char * const bench_futex_wake_parallel_usage[] = {
	"perf bench futex wake-parallel <options>",
	NULL
};

static void *waking_workerfn(void *arg)
{
	struct thread_data *waker = (struct thread_data *) arg;
	struct timeval start, end;

	gettimeofday(&start, NULL);

	waker->nwoken = futex_wake(&futex, nwakes, futex_flag);
	if (waker->nwoken != nwakes)
		warnx("couldn't wakeup all tasks (%d/%d)",
		      waker->nwoken, nwakes);

	gettimeofday(&end, NULL);
	timersub(&end, &start, &waker->runtime);

	pthread_exit(NULL);
	return NULL;
}

static void wakeup_threads(struct thread_data *td, pthread_attr_t thread_attr)
{
	unsigned int i;

	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);

	/* create and block all threads */
	for (i = 0; i < nwaking_threads; i++) {
		/*
		 * Thread creation order will impact per-thread latency
		 * as it will affect the order to acquire the hb spinlock.
		 * For now let the scheduler decide.
		 */
		if (pthread_create(&td[i].worker, &thread_attr,
				   waking_workerfn, (void *)&td[i]))
			err(EXIT_FAILURE, "pthread_create");
	}

	for (i = 0; i < nwaking_threads; i++)
		if (pthread_join(td[i].worker, NULL))
			err(EXIT_FAILURE, "pthread_join");
}

static void *blocked_workerfn(void *arg __maybe_unused)
{
	pthread_mutex_lock(&thread_lock);
	threads_starting--;
	if (!threads_starting)
		pthread_cond_signal(&thread_parent);
	pthread_cond_wait(&thread_worker, &thread_lock);
	pthread_mutex_unlock(&thread_lock);

	while (1) { /* handle spurious wakeups */
		if (futex_wait(&futex, 0, NULL, futex_flag) != EINTR)
			break;
	}

	pthread_exit(NULL);
	return NULL;
}

static void block_threads(pthread_t *w, pthread_attr_t thread_attr)
{
	cpu_set_t cpu;
	unsigned int i;

	threads_starting = nblocked_threads;

	/* create and block all threads */
	for (i = 0; i < nblocked_threads; i++) {
		CPU_ZERO(&cpu);
		CPU_SET(i % ncpus, &cpu);

		if (pthread_attr_setaffinity_np(&thread_attr, sizeof(cpu_set_t), &cpu))
			err(EXIT_FAILURE, "pthread_attr_setaffinity_np");

		if (pthread_create(&w[i], &thread_attr, blocked_workerfn, NULL))
			err(EXIT_FAILURE, "pthread_create");
	}
}

static void print_run(struct thread_data *waking_worker, unsigned int run_num)
{
	unsigned int i, wakeup_avg;
	double waketime_avg, waketime_stddev;
	struct stats __waketime_stats, __wakeup_stats;

	init_stats(&__wakeup_stats);
	init_stats(&__waketime_stats);

	for (i = 0; i < nwaking_threads; i++) {
		update_stats(&__waketime_stats, waking_worker[i].runtime.tv_usec);
		update_stats(&__wakeup_stats, waking_worker[i].nwoken);
	}

	waketime_avg = avg_stats(&__waketime_stats);
	waketime_stddev = stddev_stats(&__waketime_stats);
	wakeup_avg = avg_stats(&__wakeup_stats);

	printf("[Run %d]: Avg per-thread latency (waking %d/%d threads) "
	       "in %.4f ms (+-%.2f%%)\n", run_num + 1, wakeup_avg,
	       nblocked_threads, waketime_avg / USEC_PER_MSEC,
	       rel_stddev_stats(waketime_stddev, waketime_avg));
}

static void print_summary(void)
{
	unsigned int wakeup_avg;
	double waketime_avg, waketime_stddev;

	waketime_avg = avg_stats(&waketime_stats);
	waketime_stddev = stddev_stats(&waketime_stats);
	wakeup_avg = avg_stats(&wakeup_stats);

	printf("Avg per-thread latency (waking %d/%d threads) in %.4f ms (+-%.2f%%)\n",
	       wakeup_avg,
	       nblocked_threads,
	       waketime_avg / USEC_PER_MSEC,
	       rel_stddev_stats(waketime_stddev, waketime_avg));
}


static void do_run_stats(struct thread_data *waking_worker)
{
	unsigned int i;

	for (i = 0; i < nwaking_threads; i++) {
		update_stats(&waketime_stats, waking_worker[i].runtime.tv_usec);
		update_stats(&wakeup_stats, waking_worker[i].nwoken);
	}

}

static void toggle_done(int sig __maybe_unused,
			siginfo_t *info __maybe_unused,
			void *uc __maybe_unused)
{
	done = true;
}

int bench_futex_wake_parallel(int argc, const char **argv,
			      const char *prefix __maybe_unused)
{
	int ret = 0;
	unsigned int i, j;
	struct sigaction act;
	pthread_attr_t thread_attr;
	struct thread_data *waking_worker;

	argc = parse_options(argc, argv, options,
			     bench_futex_wake_parallel_usage, 0);
	if (argc) {
		usage_with_options(bench_futex_wake_parallel_usage, options);
		exit(EXIT_FAILURE);
	}

	sigfillset(&act.sa_mask);
	act.sa_sigaction = toggle_done;
	sigaction(SIGINT, &act, NULL);

	ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	nwaking_threads = futexbench_sanitize_numeric(nwaking_threads);

	if (!nblocked_threads)
		nblocked_threads = ncpus;
	else
		nblocked_threads = futexbench_sanitize_numeric(nblocked_threads);

	/* some sanity checks */
	if (nwaking_threads > nblocked_threads || !nwaking_threads)
		nwaking_threads = nblocked_threads;

	if (nblocked_threads % nwaking_threads)
		errx(EXIT_FAILURE, "Must be perfectly divisible");
	/*
	 * Each thread will wakeup nwakes tasks in
	 * a single futex_wait call.
	 */
	nwakes = nblocked_threads/nwaking_threads;

	blocked_worker = calloc(nblocked_threads, sizeof(*blocked_worker));
	if (!blocked_worker)
		err(EXIT_FAILURE, "calloc");

	if (!fshared)
		futex_flag = FUTEX_PRIVATE_FLAG;

	printf("Run summary [PID %d]: blocking on %d threads (at [%s] "
	       "futex %p), %d threads waking up %d at a time.\n\n",
	       getpid(), nblocked_threads, fshared ? "shared":"private",
	       &futex, nwaking_threads, nwakes);

	init_stats(&wakeup_stats);
	init_stats(&waketime_stats);

	pthread_attr_init(&thread_attr);
	pthread_mutex_init(&thread_lock, NULL);
	pthread_cond_init(&thread_parent, NULL);
	pthread_cond_init(&thread_worker, NULL);

	for (j = 0; j < bench_repeat && !done; j++) {
		waking_worker = calloc(nwaking_threads, sizeof(*waking_worker));
		if (!waking_worker)
			err(EXIT_FAILURE, "calloc");

		/* create, launch & block all threads */
		block_threads(blocked_worker, thread_attr);

		/* make sure all threads are already blocked */
		pthread_mutex_lock(&thread_lock);
		while (threads_starting)
			pthread_cond_wait(&thread_parent, &thread_lock);
		pthread_cond_broadcast(&thread_worker);
		pthread_mutex_unlock(&thread_lock);

		usleep(100000);

		/* Ok, all threads are patiently blocked, start waking folks up */
		wakeup_threads(waking_worker, thread_attr);

		for (i = 0; i < nblocked_threads; i++) {
			ret = pthread_join(blocked_worker[i], NULL);
			if (ret)
				err(EXIT_FAILURE, "pthread_join");
		}

		do_run_stats(waking_worker);
		if (!silent)
			print_run(waking_worker, j);

		free(waking_worker);
	}

	/* cleanup & report results */
	pthread_cond_destroy(&thread_parent);
	pthread_cond_destroy(&thread_worker);
	pthread_mutex_destroy(&thread_lock);
	pthread_attr_destroy(&thread_attr);

	print_summary();

	free(blocked_worker);
	return ret;
}

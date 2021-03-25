// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Davidlohr Bueso.
 *
 * Benchmark the various operations allowed for epoll_ctl(2).
 * The idea is to concurrently stress a single epoll instance
 */
#ifdef HAVE_EVENTFD_SUPPORT
/* For the CLR_() macros */
#include <string.h>
#include <pthread.h>

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <perf/cpumap.h>

#include "../util/stat.h"
#include <subcmd/parse-options.h>
#include "bench.h"

#include <err.h>

#define printinfo(fmt, arg...) \
	do { if (__verbose) printf(fmt, ## arg); } while (0)

static unsigned int nthreads = 0;
static unsigned int nsecs    = 8;
static bool done, __verbose, randomize;

/*
 * epoll related shared variables.
 */

/* Maximum number of nesting allowed inside epoll sets */
#define EPOLL_MAXNESTS 4

enum {
	OP_EPOLL_ADD,
	OP_EPOLL_MOD,
	OP_EPOLL_DEL,
	EPOLL_NR_OPS,
};

static int epollfd;
static int *epollfdp;
static bool noaffinity;
static unsigned int nested = 0;

/* amount of fds to monitor, per thread */
static unsigned int nfds = 64;

static pthread_mutex_t thread_lock;
static unsigned int threads_starting;
static struct stats all_stats[EPOLL_NR_OPS];
static pthread_cond_t thread_parent, thread_worker;

struct worker {
	int tid;
	pthread_t thread;
	unsigned long ops[EPOLL_NR_OPS];
	int *fdmap;
};

static const struct option options[] = {
	OPT_UINTEGER('t', "threads", &nthreads, "Specify amount of threads"),
	OPT_UINTEGER('r', "runtime", &nsecs,    "Specify runtime (in seconds)"),
	OPT_UINTEGER('f', "nfds", &nfds, "Specify amount of file descriptors to monitor for each thread"),
	OPT_BOOLEAN( 'n', "noaffinity",  &noaffinity,   "Disables CPU affinity"),
	OPT_UINTEGER( 'N', "nested",  &nested,   "Nesting level epoll hierarchy (default is 0, no nesting)"),
	OPT_BOOLEAN( 'R', "randomize", &randomize,   "Perform random operations on random fds"),
	OPT_BOOLEAN( 'v', "verbose",  &__verbose,   "Verbose mode"),
	OPT_END()
};

static const char * const bench_epoll_ctl_usage[] = {
	"perf bench epoll ctl <options>",
	NULL
};

static void toggle_done(int sig __maybe_unused,
			siginfo_t *info __maybe_unused,
			void *uc __maybe_unused)
{
	/* inform all threads that we're done for the day */
	done = true;
	gettimeofday(&bench__end, NULL);
	timersub(&bench__end, &bench__start, &bench__runtime);
}

static void nest_epollfd(void)
{
	unsigned int i;
	struct epoll_event ev;

	if (nested > EPOLL_MAXNESTS)
		nested = EPOLL_MAXNESTS;
	printinfo("Nesting level(s): %d\n", nested);

	epollfdp = calloc(nested, sizeof(int));
	if (!epollfd)
		err(EXIT_FAILURE, "calloc");

	for (i = 0; i < nested; i++) {
		epollfdp[i] = epoll_create(1);
		if (epollfd < 0)
			err(EXIT_FAILURE, "epoll_create");
	}

	ev.events = EPOLLHUP; /* anything */
	ev.data.u64 = i; /* any number */

	for (i = nested - 1; i; i--) {
		if (epoll_ctl(epollfdp[i - 1], EPOLL_CTL_ADD,
			      epollfdp[i], &ev) < 0)
			err(EXIT_FAILURE, "epoll_ctl");
	}

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, *epollfdp, &ev) < 0)
		err(EXIT_FAILURE, "epoll_ctl");
}

static inline void do_epoll_op(struct worker *w, int op, int fd)
{
	int error;
	struct epoll_event ev;

	ev.events = EPOLLIN;
	ev.data.u64 = fd;

	switch (op) {
	case OP_EPOLL_ADD:
		error = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
		break;
	case OP_EPOLL_MOD:
		ev.events = EPOLLOUT;
		error = epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
		break;
	case OP_EPOLL_DEL:
		error = epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
		break;
	default:
		error = 1;
		break;
	}

	if (!error)
		w->ops[op]++;
}

static inline void do_random_epoll_op(struct worker *w)
{
	unsigned long rnd1 = random(), rnd2 = random();
	int op, fd;

	fd = w->fdmap[rnd1 % nfds];
	op = rnd2 % EPOLL_NR_OPS;

	do_epoll_op(w, op, fd);
}

static void *workerfn(void *arg)
{
	unsigned int i;
	struct worker *w = (struct worker *) arg;
	struct timespec ts = { .tv_sec = 0,
			       .tv_nsec = 250 };

	pthread_mutex_lock(&thread_lock);
	threads_starting--;
	if (!threads_starting)
		pthread_cond_signal(&thread_parent);
	pthread_cond_wait(&thread_worker, &thread_lock);
	pthread_mutex_unlock(&thread_lock);

	/* Let 'em loose */
	do {
		/* random */
		if (randomize) {
			do_random_epoll_op(w);
		} else {
			for (i = 0; i < nfds; i++) {
				do_epoll_op(w, OP_EPOLL_ADD, w->fdmap[i]);
				do_epoll_op(w, OP_EPOLL_MOD, w->fdmap[i]);
				do_epoll_op(w, OP_EPOLL_DEL, w->fdmap[i]);
			}
		}

		nanosleep(&ts, NULL);
	}  while (!done);

	return NULL;
}

static void init_fdmaps(struct worker *w, int pct)
{
	unsigned int i;
	int inc;
	struct epoll_event ev;

	if (!pct)
		return;

	inc = 100/pct;
	for (i = 0; i < nfds; i+=inc) {
		ev.data.fd = w->fdmap[i];
		ev.events = EPOLLIN;

		if (epoll_ctl(epollfd, EPOLL_CTL_ADD, w->fdmap[i], &ev) < 0)
			err(EXIT_FAILURE, "epoll_ct");
	}
}

static int do_threads(struct worker *worker, struct perf_cpu_map *cpu)
{
	pthread_attr_t thread_attr, *attrp = NULL;
	cpu_set_t cpuset;
	unsigned int i, j;
	int ret = 0;

	if (!noaffinity)
		pthread_attr_init(&thread_attr);

	for (i = 0; i < nthreads; i++) {
		struct worker *w = &worker[i];

		w->tid = i;
		w->fdmap = calloc(nfds, sizeof(int));
		if (!w->fdmap)
			return 1;

		for (j = 0; j < nfds; j++) {
			w->fdmap[j] = eventfd(0, EFD_NONBLOCK);
			if (w->fdmap[j] < 0)
				err(EXIT_FAILURE, "eventfd");
		}

		/*
		 * Lets add 50% of the fdmap to the epoll instance, and
		 * do it before any threads are started; otherwise there is
		 * an initial bias of the call failing  (mod and del ops).
		 */
		if (randomize)
			init_fdmaps(w, 50);

		if (!noaffinity) {
			CPU_ZERO(&cpuset);
			CPU_SET(cpu->map[i % cpu->nr], &cpuset);

			ret = pthread_attr_setaffinity_np(&thread_attr, sizeof(cpu_set_t), &cpuset);
			if (ret)
				err(EXIT_FAILURE, "pthread_attr_setaffinity_np");

			attrp = &thread_attr;
		}

		ret = pthread_create(&w->thread, attrp, workerfn,
				     (void *)(struct worker *) w);
		if (ret)
			err(EXIT_FAILURE, "pthread_create");
	}

	if (!noaffinity)
		pthread_attr_destroy(&thread_attr);

	return ret;
}

static void print_summary(void)
{
	int i;
	unsigned long avg[EPOLL_NR_OPS];
	double stddev[EPOLL_NR_OPS];

	for (i = 0; i < EPOLL_NR_OPS; i++) {
		avg[i] = avg_stats(&all_stats[i]);
		stddev[i] = stddev_stats(&all_stats[i]);
	}

	printf("\nAveraged %ld ADD operations (+- %.2f%%)\n",
	       avg[OP_EPOLL_ADD], rel_stddev_stats(stddev[OP_EPOLL_ADD],
						   avg[OP_EPOLL_ADD]));
	printf("Averaged %ld MOD operations (+- %.2f%%)\n",
	       avg[OP_EPOLL_MOD], rel_stddev_stats(stddev[OP_EPOLL_MOD],
						   avg[OP_EPOLL_MOD]));
	printf("Averaged %ld DEL operations (+- %.2f%%)\n",
	       avg[OP_EPOLL_DEL], rel_stddev_stats(stddev[OP_EPOLL_DEL],
						   avg[OP_EPOLL_DEL]));
}

int bench_epoll_ctl(int argc, const char **argv)
{
	int j, ret = 0;
	struct sigaction act;
	struct worker *worker = NULL;
	struct perf_cpu_map *cpu;
	struct rlimit rl, prevrl;
	unsigned int i;

	argc = parse_options(argc, argv, options, bench_epoll_ctl_usage, 0);
	if (argc) {
		usage_with_options(bench_epoll_ctl_usage, options);
		exit(EXIT_FAILURE);
	}

	memset(&act, 0, sizeof(act));
	sigfillset(&act.sa_mask);
	act.sa_sigaction = toggle_done;
	sigaction(SIGINT, &act, NULL);

	cpu = perf_cpu_map__new(NULL);
	if (!cpu)
		goto errmem;

	/* a single, main epoll instance */
	epollfd = epoll_create(1);
	if (epollfd < 0)
		err(EXIT_FAILURE, "epoll_create");

	/*
	 * Deal with nested epolls, if any.
	 */
	if (nested)
		nest_epollfd();

	/* default to the number of CPUs */
	if (!nthreads)
		nthreads = cpu->nr;

	worker = calloc(nthreads, sizeof(*worker));
	if (!worker)
		goto errmem;

	if (getrlimit(RLIMIT_NOFILE, &prevrl))
	    err(EXIT_FAILURE, "getrlimit");
	rl.rlim_cur = rl.rlim_max = nfds * nthreads * 2 + 50;
	printinfo("Setting RLIMIT_NOFILE rlimit from %" PRIu64 " to: %" PRIu64 "\n",
		  (uint64_t)prevrl.rlim_max, (uint64_t)rl.rlim_max);
	if (setrlimit(RLIMIT_NOFILE, &rl) < 0)
		err(EXIT_FAILURE, "setrlimit");

	printf("Run summary [PID %d]: %d threads doing epoll_ctl ops "
	       "%d file-descriptors for %d secs.\n\n",
	       getpid(), nthreads, nfds, nsecs);

	for (i = 0; i < EPOLL_NR_OPS; i++)
		init_stats(&all_stats[i]);

	pthread_mutex_init(&thread_lock, NULL);
	pthread_cond_init(&thread_parent, NULL);
	pthread_cond_init(&thread_worker, NULL);

	threads_starting = nthreads;

	gettimeofday(&bench__start, NULL);

	do_threads(worker, cpu);

	pthread_mutex_lock(&thread_lock);
	while (threads_starting)
		pthread_cond_wait(&thread_parent, &thread_lock);
	pthread_cond_broadcast(&thread_worker);
	pthread_mutex_unlock(&thread_lock);

	sleep(nsecs);
	toggle_done(0, NULL, NULL);
	printinfo("main thread: toggling done\n");

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
		unsigned long t[EPOLL_NR_OPS];

		for (j = 0; j < EPOLL_NR_OPS; j++) {
			t[j] = worker[i].ops[j];
			update_stats(&all_stats[j], t[j]);
		}

		if (nfds == 1)
			printf("[thread %2d] fdmap: %p [ add: %04ld; mod: %04ld; del: %04lds ops ]\n",
			       worker[i].tid, &worker[i].fdmap[0],
			       t[OP_EPOLL_ADD], t[OP_EPOLL_MOD], t[OP_EPOLL_DEL]);
		else
			printf("[thread %2d] fdmap: %p ... %p [ add: %04ld ops; mod: %04ld ops; del: %04ld ops ]\n",
			       worker[i].tid, &worker[i].fdmap[0],
			       &worker[i].fdmap[nfds-1],
			       t[OP_EPOLL_ADD], t[OP_EPOLL_MOD], t[OP_EPOLL_DEL]);
	}

	print_summary();

	close(epollfd);
	return ret;
errmem:
	err(EXIT_FAILURE, "calloc");
}
#endif // HAVE_EVENTFD_SUPPORT

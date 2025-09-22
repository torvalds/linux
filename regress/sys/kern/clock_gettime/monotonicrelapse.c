/* $OpenBSD: monotonicrelapse.c,v 1.3 2021/12/13 16:56:49 deraadt Exp $ */
/*
 * Scott Cheloha <scottcheloha@gmail.com>, 2019.  Public Domain.
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <err.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

void report_relapse(int, struct timespec *, struct timespec *);
void *thread_func(void *);
void thread_spawn(pthread_t **, int, void *(*)(void *));

/*
 * Is the active timecounter monotonic?
 *
 * Spawn the given number of threads and measure the monotonic clock
 * across nanosleep(2) calls.
 *
 * Threads have a tendency to wake up on new CPUs.  If the active
 * timecounter is not synchronized across CPUs this will be detected
 * relatively quickly and the test will fail.
 */

int
main(int argc, char *argv[])
{
	const char *errstr;
	pthread_t *thread;
	int error, i, nthreads;

	if (argc != 2) {
		fprintf(stderr, "usage: %s nthreads\n", getprogname());
		return 1;
	}
	nthreads = strtonum(argv[1], 1, INT_MAX, &errstr);
	if (errstr != NULL)
		errx(1, "nthreads is %s: %s", errstr, argv[1]);

	thread = calloc(nthreads, sizeof(*thread));
	if (thread == NULL)
		err(1, NULL);

	for (i = 0; i < nthreads; i++) {
		error = pthread_create(&thread[i], NULL, thread_func,
		    (void *)((uintptr_t)i + 1));
		if (error)
			errc(1, error, "pthread_create");
	}

	sleep(10);

	return 0;
}

void
report_relapse(int num, struct timespec *before, struct timespec *after)
{
	static pthread_mutex_t report_mutex = PTHREAD_MUTEX_INITIALIZER;
	struct timespec relapsed;
	int error;

	error = pthread_mutex_lock(&report_mutex);
	if (error)
		errc(1, error, "T%d: pthread_mutex_lock", num);

	timespecsub(before, after, &relapsed);
	errx(1, "T%d: monotonic clock relapsed %.9f seconds: %.9f -> %.9f",
	    num, relapsed.tv_sec + relapsed.tv_nsec / 1000000000.0,
	    before->tv_sec + before->tv_nsec / 1000000000.0,
	    after->tv_sec + after->tv_nsec / 1000000000.0);
}

void *
thread_func(void *arg)
{
	struct timespec after, before, timeout;
	int num;

	timeout.tv_sec = 0;
	timeout.tv_nsec = 1;
	num = (int)arg;

	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &before);
		if (nanosleep(&timeout, NULL) == -1)
			err(1, "T%d: nanosleep", num);
		clock_gettime(CLOCK_MONOTONIC, &after);
		if (timespeccmp(&after, &before, <))
			report_relapse(num, &before, &after);
	}

	return NULL;
}

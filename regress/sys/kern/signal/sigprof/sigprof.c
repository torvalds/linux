/*	$OpenBSD: sigprof.c,v 1.2 2021/07/06 13:19:57 bluhm Exp $ */
/*
 * Copyright (c) 2013 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Test that profiling signals are delivered to the thread whose execution
 * consumed the CPU time and resulted in the profiling timer expiring.
 * Inspired by a problem report test case from Russ Cox <rsc@golang.org>.
 */

#include <err.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#define NTHREADS	4
#define NSIGNALS	100
#define NSIGTOTAL	(NTHREADS * NSIGNALS)
#define NMINSIG		((NSIGNALS * 50) / 100)

void handler(int);
void *spinloop(void *);

pthread_t threads[NTHREADS + 1];
int sigcount[NTHREADS + 1];
volatile int sigtotal;
volatile int done;

void
handler(int sig)
{
	pthread_t self;
	int i;

	/*
	 * pthread_self() is not required to be async-signal-safe, however
	 * the OpenBSD implementation currently is.
	 */
	self = pthread_self();

	for (i = 0; i <= NTHREADS; i++)
		if (threads[i] == self)
			sigcount[i]++;

	if (++sigtotal >= NSIGTOTAL)
		done = 1;
}

void *
spinloop(void *arg)
{
	while (!done)
		;

	pthread_exit(NULL);
}

int
main(int argc, char **argv)
{
	struct sigaction sa;
	struct itimerval it;
	int i;

	bzero(&sa, sizeof(sa));
	sa.sa_handler = handler;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGPROF, &sa, 0);

	threads[0] = pthread_self();
	for (i = 1; i <= NTHREADS; i++)
		if (pthread_create(&threads[i], NULL, spinloop, NULL) != 0)
			err(1, "pthread_create");

	bzero(&it, sizeof(it));
	it.it_interval.tv_usec = 10000;
	it.it_value = it.it_interval;
	setitimer(ITIMER_PROF, &it, NULL);

	for (i = 1; i <= NTHREADS; i++)
		if (pthread_join(threads[i], NULL) != 0)
			err(1, "pthread_join");

	bzero(&it, sizeof(it));
	setitimer(ITIMER_PROF, &it, NULL);

	fprintf(stderr, "total profiling signals: %d\n", sigtotal);
	fprintf(stderr, "minimum signals per thread: %d\n", NMINSIG);
	fprintf(stderr, "main thread - %d\n", sigcount[0]);
	for (i = 1; i <= NTHREADS; i++)
		fprintf(stderr, "thread %d - %d\n", i, sigcount[i]);

	if (sigtotal < NSIGTOTAL)
		errx(1, "insufficient profiling signals (%d < %d)",
			sigtotal, NSIGTOTAL);

	/*
	 * The main thread is effectively sleeping and should have received
	 * almost no profiling signals. Allow a small tolerance.
	 */
	if (sigcount[0] > ((NSIGNALS * 10) / 100))
		errx(1, "main thread received too many signals (%d)",
			sigcount[0]);

	/*
	 * Ensure that the kernel delivered the profiling signals to the
	 * thread that consumed the CPU time. In an ideal world each thread
	 * would have received equal CPU time and an equal number of signals.
	 * In the less than ideal world we'll just settle for a percentage.
	 */
	for (i = 1; i <= NTHREADS; i++)
		if (sigcount[i] < NMINSIG)
			errx(1, "thread %d received only %d signals (%d < %d)",
				i, sigcount[i], sigcount[i], NMINSIG);

	return 0;
}

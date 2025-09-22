/*	$OpenBSD: sigpthread.c,v 1.2 2021/07/06 11:50:34 bluhm Exp $	*/
/*
 * Copyright (c) 2019 Alexander Bluhm <bluhm@openbsd.org>
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

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void __dead usage(void);
void handler(int);
void *runner(void *);

void __dead
usage(void)
{
	fprintf(stderr, "sigpthread [-bSsU] [-k kill] -t threads [-u unblock] "
	    "[-w waiter]\n"
	    "    -b             block signal to make it pending\n"
	    "    -k kill        thread to kill, else process\n"
	    "    -S             sleep in each thread before suspend\n"
	    "    -s             sleep in main before kill\n"
	    "    -t threads     number of threads to run\n"
	    "    -U             sleep in thread before unblock\n"
	    "    -u unblock     thread to unblock, else unblock all\n"
	    "    -w waiter      use sigwait in thread\n"
	);
	exit(1);
}

int blocksignal = 0;
int threadmax, threadunblock = -1, threadwaiter = -1;
int sleepthread, sleepmain, sleepunblock;
sigset_t set, oset;
pthread_t *threads;
volatile sig_atomic_t *signaled;

int
main(int argc, char *argv[])
{
	struct sigaction act;
	int ch, ret, tnum, threadkill = -1;
	long arg;
	void *val;
	const char *errstr;

	while ((ch = getopt(argc, argv, "bk:Sst:Uu:w:")) != -1) {
		switch (ch) {
		case 'b':
			blocksignal = 1;
			break;
		case 'k':
			threadkill = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "thread to kill is %s: %s",
				    errstr, optarg);
			break;
		case 'S':
			sleepthread = 1;
			break;
		case 's':
			sleepmain = 1;
			break;
		case 't':
			threadmax = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "number of threads is %s: %s",
				    errstr, optarg);
			break;
		case 'U':
			sleepunblock = 1;
			break;
		case 'u':
			threadunblock = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "thread to unblock is %s: %s",
				    errstr, optarg);
			break;
		case 'w':
			threadwaiter = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "thread to wait is %s: %s",
				    errstr, optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0)
		errx(1, "more arguments than expected");
	if (threadmax == 0)
		errx(1, "number of threads required");
	if (threadkill >= threadmax)
		errx(1, "thread to kill greater than number of threads");
	if (threadunblock >= threadmax)
		errx(1, "thread to unblock greater than number of threads");
	if (threadwaiter >= threadmax)
		errx(1, "thread to wait greater than number of threads");
	if (!blocksignal && threadunblock >= 0)
		errx(1, "do not unblock thread without blocked signals");
	if (!blocksignal && threadwaiter >= 0)
		errx(1, "do not wait in thread without blocked signals");
	if (threadunblock >= 0 && threadwaiter >= 0)
		errx(1, "do not unblock and wait together");
	if (sleepunblock && threadwaiter >= 0)
		errx(1, "do not sleep before unblock and wait together");

	/* Make sure that we do not hang forever. */
	alarm(10);

	if (sigemptyset(&set) == -1)
		err(1, "sigemptyset");
	if (sigaddset(&set, SIGUSR1) == -1)
		err(1, "sigaddset");
	/* Either deliver SIGUSR2 immediately, or mark it pending. */
	if (blocksignal) {
		if (sigaddset(&set, SIGUSR2) == -1)
			err(1, "sigaddset");
	}
	/* Block both SIGUSR1 and SIGUSR2 with set. */
	if (sigprocmask(SIG_BLOCK, &set, &oset) == -1)
		err(1, "sigprocmask");

	/* Prepare to wait for SIGUSR1, but block SIGUSR2 with oset. */
	if (sigaddset(&oset, SIGUSR2) == -1)
		err(1, "sigaddset");
	/* Unblock or wait for SIGUSR2 */
	if (sigemptyset(&set) == -1)
		err(1, "sigemptyset");
	if (sigaddset(&set, SIGUSR2) == -1)
		err(1, "sigaddset");

	memset(&act, 0, sizeof(act));
	act.sa_handler = handler;
	if (sigaction(SIGUSR1, &act, NULL) == -1)
		err(1, "sigaction SIGUSR1");
	if (sigaction(SIGUSR2, &act, NULL) == -1)
		err(1, "sigaction SIGUSR2");

	signaled = calloc(threadmax, sizeof(*signaled));
	if (signaled == NULL)
		err(1, "calloc signaled");
	threads = calloc(threadmax, sizeof(*threads));
	if (threads == NULL)
		err(1, "calloc threads");

	for (tnum = 1; tnum < threadmax; tnum++) {
		arg = tnum;
		errno = pthread_create(&threads[tnum], NULL, runner,
		    (void *)arg);
		if (errno)
			err(1, "pthread_create %d", tnum);
	}
	/* Handle the main thread like thread 0. */
	threads[0] = pthread_self();

	/* Test what happens if thread is running when killed. */
	if (sleepmain)
		sleep(1);

	/* All threads are still alive. */
	if (threadkill < 0) {
		if (kill(getpid(), SIGUSR2) == -1)
			err(1, "kill SIGUSR2");
	} else {
		errno = pthread_kill(threads[threadkill], SIGUSR2);
		if (errno)
			err(1, "pthread_kill %d SIGUSR2", tnum);
	}

	/* Sending SIGUSR1 means threads can continue and finish. */
	for (tnum = 0; tnum < threadmax; tnum++) {
		errno = pthread_kill(threads[tnum], SIGUSR1);
		if (errno)
			err(1, "pthread_kill %d SIGUSR1", tnum);
	}

	val = runner(0);
	ret = (int)val;

	for (tnum = 1; tnum < threadmax; tnum++) {
		errno = pthread_join(threads[tnum], &val);
		if (errno)
			err(1, "pthread_join %d", tnum);
		ret = (int)val;
		if (ret)
			errx(1, "pthread %d returned %d", tnum, ret);
	}
	free(threads);

	for (tnum = 0; tnum < threadmax; tnum++) {
		int i;

		for (i = 0; i < signaled[tnum]; i++)
			printf("signal %d\n", tnum);
	}
	free((void *)signaled);

	return 0;
}

void
handler(int sig)
{
	pthread_t tid;
	int tnum;

	tid = pthread_self();
	for (tnum = 0; tnum < threadmax; tnum++) {
		if (tid == threads[tnum])
			break;
	}
	switch (sig) {
	case SIGUSR1:
		break;
	case SIGUSR2:
		signaled[tnum]++;
		break;
	default:
		errx(1, "unexpected signal %d thread %d", sig, tnum);
	}
}

void *
runner(void *arg)
{
	int tnum = (int)arg;

	/* Test what happens if thread is running when killed. */
	if (sleepthread)
		sleep(1);

	if (tnum == threadwaiter) {
		int sig;

		if (sigwait(&set, &sig) != 0)
			err(1, "sigwait thread %d", tnum);
		if (sig != SIGUSR2)
			errx(1, "unexpected signal %d thread %d", sig, tnum);
		signaled[tnum]++;
	}

	/*
	 * Wait for SIGUSER1, continue to block SIGUSER2.
	 * The thread is keeps running until it gets SIGUSER1.
	 */
	if (sigsuspend(&oset) != -1 || errno != EINTR)
		err(1, "sigsuspend thread %d", tnum);
	if ((threadunblock < 0 || tnum == threadunblock) && threadwaiter < 0) {
		/* Test what happens if other threads exit before unblock. */
		if (sleepunblock)
			sleep(1);

		/* Also unblock SIGUSER2, if this thread should get it. */
		if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) == -1)
			err(1, "pthread_sigmask thread %d", tnum);
	}

	return (void *)0;
}

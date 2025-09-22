/*	$OpenBSD: fork-exit.c,v 1.8 2024/08/07 18:25:39 claudio Exp $	*/

/*
 * Copyright (c) 2021 Alexander Bluhm <bluhm@openbsd.org>
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

#include <sys/mman.h>
#include <sys/select.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int execute = 0;
int daemonize = 0;
int heap = 0;
int procs = 1;
int stack = 0;
int threads = 0;
int timeout = 30;

int pagesize;

pthread_barrier_t thread_barrier;
char timeoutstr[sizeof("-2147483647")];

static void __dead
usage(void)
{
	fprintf(stderr, "fork-exit [-ed] [-p procs] [-t threads] [-T timeout]\n"
	    "    -e          child execs sleep(1), default call sleep(3)\n"
	    "    -d          daemonize, use if already process group leader\n"
	    "    -h heap     allocate number of kB of heap memory, default 0\n"
	    "    -p procs    number of processes to fork, default 1\n"
	    "    -s stack    allocate number of kB of stack memory, default 0\n"
	    "    -t threads  number of threads to create, default 0\n"
	    "    -T timeout  parent and children will exit, default 30 sec\n");
	exit(2);
}

static void
recurse_page(int depth)
{
	int p[4096 / sizeof(int)];

	if (depth == 0)
		return;
	p[1] = 0x9abcdef0;
	explicit_bzero(p, sizeof(int));
	recurse_page(depth - 1);
}

static void
alloc_stack(void)
{
	recurse_page((stack * 1024) / (4096 + 200));
}

static void
alloc_heap(void)
{
	int *p;
	int i;

	for (i = 0; i < heap / (pagesize / 1024); i++) {
		p = mmap(0, pagesize, PROT_WRITE|PROT_READ,
		    MAP_SHARED|MAP_ANON, -1, 0);
		if (p == MAP_FAILED)
			err(1, "mmap");
		p[1] = 0x12345678;
		explicit_bzero(p, sizeof(int));
	}
}

static void * __dead
run_thread(void *arg)
{
	int error;

	if (heap)
		alloc_heap();
	if (stack)
		alloc_stack();

	error = pthread_barrier_wait(&thread_barrier);
	if (error && error != PTHREAD_BARRIER_SERIAL_THREAD)
		errc(1, error, "pthread_barrier_wait");

	if (sleep(timeout) != 0)
		err(1, "sleep %d", timeout);

	/* should not happen */
	_exit(0);
}

static void
create_threads(void)
{
	pthread_attr_t tattr;
	pthread_t *thrs;
	int i, error;

	error = pthread_attr_init(&tattr);
	if (error)
		errc(1, error, "pthread_attr_init");
	if (stack) {
		/* thread start and function call overhead needs a bit more */
		error = pthread_attr_setstacksize(&tattr,
		    (stack + 2) * (1024ULL + 50));
		if (error)
			errc(1, error, "pthread_attr_setstacksize");
	}

	error = pthread_barrier_init(&thread_barrier, NULL, threads + 1);
	if (error)
		errc(1, error, "pthread_barrier_init");

	thrs = reallocarray(NULL, threads, sizeof(pthread_t));
	if (thrs == NULL)
		err(1, "thrs");

	for (i = 0; i < threads; i++) {
		error = pthread_create(&thrs[i], &tattr, run_thread, NULL);
		if (error)
			errc(1, error, "pthread_create");
	}

	error = pthread_barrier_wait(&thread_barrier);
	if (error && error != PTHREAD_BARRIER_SERIAL_THREAD)
		errc(1, error, "pthread_barrier_wait");

	/* return to close child's pipe and sleep */
}

static void __dead
exec_sleep(void)
{
	execl("/bin/sleep", "sleep", timeoutstr, NULL);
	err(1, "exec sleep");
}

static void __dead
run_child(int fd)
{
	/* close pipe to parent and sleep until killed */
	if (execute) {
		if (fcntl(fd, F_SETFD, FD_CLOEXEC))
			err(1, "fcntl FD_CLOEXEC");
		exec_sleep();
	} else {
		if (threads) {
			create_threads();
		} else {
			if (heap)
				alloc_heap();
			if (stack)
				alloc_stack();
		}
		if (close(fd) == -1)
			err(1, "close child");
		if (sleep(timeout) != 0)
			err(1, "sleep %d", timeout);
	}

	/* should not happen */
	_exit(0);
}

static void
sigexit(int sig)
{
	int i, status;
	pid_t pid;

	/* all children must terminate in time */
	alarm(timeout);

	for (i = 0; i < procs; i++) {
		pid = wait(&status);
		if (pid == -1)
			err(1, "wait");
		if (!WIFSIGNALED(status))
			errx(1, "child %d not killed", pid);
		if(WTERMSIG(status) != SIGTERM)
			errx(1, "child %d signal %d", pid, WTERMSIG(status));
	}
	exit(0);
}

int
main(int argc, char *argv[])
{
	const char *errstr;
	int ch, i, fdmax, fdlen, *rfds, waiting;
	fd_set *fdset;
	pid_t pgrp;
	struct timeval tv;

	pagesize = sysconf(_SC_PAGESIZE);

	while ((ch = getopt(argc, argv, "edh:p:s:T:t:")) != -1) {
	switch (ch) {
		case 'e':
			execute = 1;
			break;
		case 'd':
			daemonize = 1;
			break;
		case 'h':
			heap = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "number of heap allocations is %s: %s",
				    errstr, optarg);
			break;
		case 'p':
			procs = strtonum(optarg, 0, INT_MAX / pagesize,
			    &errstr);
			if (errstr != NULL)
				errx(1, "number of procs is %s: %s", errstr,
				    optarg);
			break;
		case 's':
			stack = strtonum(optarg, 0,
			    (INT_MAX / (1024 + 50)) - 2, &errstr);
			if (errstr != NULL)
				errx(1, "number of stack allocations is %s: %s",
				    errstr, optarg);
			break;
		case 't':
			threads = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "number of threads is %s: %s", errstr,
				    optarg);
			break;
		case 'T':
			timeout = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "timeout is %s: %s", errstr, optarg);
			break;
		default:
			usage();
		}
	}
	if (execute) {
		int ret;

		if (threads > 0)
			errx(1, "execute sleep cannot be used with threads");

		ret = snprintf(timeoutstr, sizeof(timeoutstr), "%d", timeout);
		if (ret < 0 || (size_t)ret >= sizeof(timeoutstr))
			err(1, "snprintf");
	}

	/* become process group leader */
	if (daemonize) {
		/* get rid of process leadership */
		switch (fork()) {
		case -1:
			err(1, "fork parent");
		case 0:
			break;
		default:
			/* parent leaves orphan behind to do the work */
			_exit(0);
		}
	}
	pgrp = setsid();
	if (pgrp == -1) {
		if (!daemonize)
			warnx("try -d to become process group leader");
		err(1, "setsid");
	}

	/* create pipes to keep in contact with children */
	rfds = reallocarray(NULL, procs, sizeof(int));
	if (rfds == NULL)
		err(1, "rfds");
	fdmax = 0;

	/* fork child processes and pass writing end of pipe */
	for (i = 0; i < procs; i++) {
		int pipefds[2], error;

		if (pipe(pipefds) == -1)
			err(1, "pipe");
		if (fdmax < pipefds[0])
			fdmax = pipefds[0];
		rfds[i] = pipefds[0];

		switch (fork()) {
		case -1:
			/* resource temporarily unavailable may happen */
			error = errno;
			/* reap children, but not parent */
			signal(SIGTERM, SIG_IGN);
			kill(-pgrp, SIGTERM);
			errc(1, error, "fork child");
		case 0:
			/* child closes reading end, read is for the parent */
			if (close(pipefds[0]) == -1)
				err(1, "close read");
			run_child(pipefds[1]);
			/* cannot happen */
			_exit(0);
		default:
			/* parent closes writing end, write is for the child */
			if (close(pipefds[1]) == -1)
				err(1, "close write");
			break;
		}
	}

	/* create select mask with all reading ends of child pipes */
	fdlen = howmany(fdmax + 1, NFDBITS);
	fdset = calloc(fdlen, sizeof(fd_mask));
	if (fdset == NULL)
		err(1, "fdset");
	waiting = 0;
	for (i = 0; i < procs; i++) {
		FD_SET(rfds[i], fdset);
		waiting = 1;
	}

	/* wait until all child processes are waiting */
	while (waiting) {
		tv.tv_sec = timeout;
		tv.tv_usec = 0;
		errno = ETIMEDOUT;
		if (select(fdmax + 1, fdset, NULL, NULL, &tv) <= 0)
			err(1, "select");

		waiting = 0;
		/* remove fd of children that closed their end  */
		for (i = 0; i < procs; i++) {
			if (rfds[i] >= 0) {
				if (FD_ISSET(rfds[i], fdset)) {
					if (close(rfds[i]) == -1)
						err(1, "close parent");
					FD_CLR(rfds[i], fdset);
					rfds[i] = -1;
				} else {
					FD_SET(rfds[i], fdset);
					waiting = 1;
				}
			}
		}
	}

	/* kill all children simultaneously, parent exits in signal handler */
	if (signal(SIGTERM, sigexit) == SIG_ERR)
		err(1, "signal SIGTERM");
	if (kill(-pgrp, SIGTERM) == -1)
		err(1, "kill %d", -pgrp);

	errx(1, "alive");
}

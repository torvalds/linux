/*-
 * Copyright (c) 2005-2006 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

static int	threaded;		/* 1 for threaded, 0 for forked. */
static int	numthreads;		/* Number of threads/procs. */
static int	numseconds;		/* Length of test. */

/*
 * Simple, multi-threaded HTTP benchmark.  Fetches a single URL using the
 * specified parameters, and after a period of execution, reports on how it
 * worked out.
 */
#define	MAXTHREADS	128
#define	DEFAULTTHREADS	32
#define	DEFAULTSECONDS	20
#define	BUFFER	(48*1024)
#define	QUIET	1

struct http_worker_description {
	pthread_t	hwd_thread;
	pid_t		hwd_pid;
	uintmax_t	hwd_count;
	uintmax_t	hwd_errorcount;
	int		hwd_start_signal_barrier;
};

static struct state {
	struct sockaddr_in		 sin;
	char				*path;
	struct http_worker_description	 hwd[MAXTHREADS];
	int				 run_done;
	pthread_barrier_t		 start_barrier;
} *statep;

int curthread;

/*
 * Borrowed from sys/param.h>
 */
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))	/* to any y */

/*
 * Given a partially processed URL, fetch it from the specified host.
 */
static int
http_fetch(struct sockaddr_in *sin, char *path, int quiet)
{
	u_char buffer[BUFFER];
	ssize_t len;
	size_t sofar;
	int sock;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		if (!quiet)
			warn("socket(PF_INET, SOCK_STREAM)");
		return (-1);
	}

	/* XXX: Mark non-blocking. */

	if (connect(sock, (struct sockaddr *)sin, sizeof(*sin)) < 0) {
		if (!quiet)
			warn("connect");
		close(sock);
		return (-1);
	}

	/* XXX: select for connection. */

	/* Send a request. */
	snprintf(buffer, BUFFER, "GET %s HTTP/1.0\n\n", path);
	sofar = 0;
	while (sofar < strlen(buffer)) {
		len = send(sock, buffer, strlen(buffer), 0);
		if (len < 0) {
			if (!quiet)
				warn("send");
			close(sock);
			return (-1);
		}
		if (len == 0) {
			if (!quiet)
				warnx("send: len == 0");
		}
		sofar += len;
	}

	/* Read until done.  Not very smart. */
	while (1) {
		len = recv(sock, buffer, BUFFER, 0);
		if (len < 0) {
			if (!quiet)
				warn("recv");
			close(sock);
			return (-1);
		}
		if (len == 0)
			break;
	}

	close(sock);
	return (0);
}

static void
killall(void)
{
	int i;

	for (i = 0; i < numthreads; i++) {
		if (statep->hwd[i].hwd_pid != 0)
			kill(statep->hwd[i].hwd_pid, SIGTERM);
	}
}

static void
signal_handler(int signum __unused)
{

	statep->hwd[curthread].hwd_start_signal_barrier = 1;
}

static void
signal_barrier_wait(void)
{

	/* Wait for EINTR. */
	if (signal(SIGHUP, signal_handler) == SIG_ERR)
		err(-1, "signal");
	while (1) {
		sleep(100);
		if (statep->hwd[curthread].hwd_start_signal_barrier)
			break;
	}
}

static void
signal_barrier_wakeup(void)
{
	int i;

	for (i = 0; i < numthreads; i++) {
		if (statep->hwd[i].hwd_pid != 0)
			kill(statep->hwd[i].hwd_pid, SIGHUP);
	}
}

static void *
http_worker(void *arg)
{
	struct http_worker_description *hwdp;
	int ret;

	if (threaded) {
		ret = pthread_barrier_wait(&statep->start_barrier);
		if (ret != 0 && ret != PTHREAD_BARRIER_SERIAL_THREAD)
			err(-1, "pthread_barrier_wait");
	} else {
		signal_barrier_wait();
	}

	hwdp = arg;
	while (!statep->run_done) {
		if (http_fetch(&statep->sin, statep->path, QUIET) < 0) {
			hwdp->hwd_errorcount++;
			continue;
		}
		/* Don't count transfers that didn't finish in time. */
		if (!statep->run_done)
			hwdp->hwd_count++;
	}

	if (threaded)
		return (NULL);
	else
		exit(0);
}

static void
usage(void)
{

	fprintf(stderr,
	    "http [-n numthreads] [-s seconds] [-t] ip port path\n");
	exit(EX_USAGE);
}

static void
main_sighup(int signum __unused)
{

	killall();
}

int
main(int argc, char *argv[])
{
	int ch, error, i;
	struct state *pagebuffer;
	uintmax_t total;
	size_t len;
	pid_t pid;

	numthreads = DEFAULTTHREADS;
	numseconds = DEFAULTSECONDS;
	while ((ch = getopt(argc, argv, "n:s:t")) != -1) {
		switch (ch) {
		case 'n':
			numthreads = atoi(optarg);
			break;

		case 's':
			numseconds = atoi(optarg);
			break;

		case 't':
			threaded = 1;
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 3)
		usage();

	if (numthreads > MAXTHREADS)
		errx(-1, "%d exceeds max threads %d", numthreads,
		    MAXTHREADS);

	len = roundup(sizeof(struct state), getpagesize());
	pagebuffer = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
	if (pagebuffer == MAP_FAILED)
		err(-1, "mmap");
	if (minherit(pagebuffer, len, INHERIT_SHARE) < 0)
		err(-1, "minherit");
	statep = pagebuffer;

	bzero(&statep->sin, sizeof(statep->sin));
	statep->sin.sin_len = sizeof(statep->sin);
	statep->sin.sin_family = AF_INET;
	statep->sin.sin_addr.s_addr = inet_addr(argv[0]);
	statep->sin.sin_port = htons(atoi(argv[1]));
	statep->path = argv[2];

	/*
	 * Do one test retrieve so we can report the error from it, if any.
	 */
	if (http_fetch(&statep->sin, statep->path, 0) < 0)
		exit(-1);

	if (threaded) {
		if (pthread_barrier_init(&statep->start_barrier, NULL,
		    numthreads) != 0)
			err(-1, "pthread_barrier_init");
	}

	for (i = 0; i < numthreads; i++) {
		statep->hwd[i].hwd_count = 0;
		if (threaded) {
			if (pthread_create(&statep->hwd[i].hwd_thread, NULL,
			    http_worker, &statep->hwd[i]) != 0)
				err(-1, "pthread_create");
		} else {
			curthread = i;
			pid = fork();
			if (pid < 0) {
				error = errno;
				killall();
				errno = error;
				err(-1, "fork");
			}
			if (pid == 0) {
				http_worker(&statep->hwd[i]);
				printf("Doh\n");
				exit(0);
			}
			statep->hwd[i].hwd_pid = pid;
		}
	}
	if (!threaded) {
		signal(SIGHUP, main_sighup);
		sleep(2);
		signal_barrier_wakeup();
	}
	sleep(numseconds);
	statep->run_done = 1;
	if (!threaded)
		sleep(2);
	for (i = 0; i < numthreads; i++) {
		if (threaded) {
			if (pthread_join(statep->hwd[i].hwd_thread, NULL)
			    != 0)
				err(-1, "pthread_join");
		} else {
			pid = waitpid(statep->hwd[i].hwd_pid, NULL, 0);
			if (pid == statep->hwd[i].hwd_pid)
				statep->hwd[i].hwd_pid = 0;
		}
	}
	if (!threaded)
		killall();
	total = 0;
	for (i = 0; i < numthreads; i++)
		total += statep->hwd[i].hwd_count;
	printf("%ju transfers/second\n", total / numseconds);
	total = 0;
	for (i = 0; i < numthreads; i++)
		total += statep->hwd[i].hwd_errorcount;
	printf("%ju errors/second\n", total / numseconds);
	return (0);
}

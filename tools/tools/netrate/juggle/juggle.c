/*-
 * Copyright (c) 2005 Robert N. M. Watson
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
#include <sys/socket.h>
#include <sys/stdint.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * juggle is a simple IPC/context switch performance test, which works on
 * pairs of file descriptors of various types.  In various runs, it considers
 * the cost of bouncing a message synchronously across the descriptor pair,
 * either in the same thread, two different threads, or two different
 * processes.  Timing measurements for each series of I/O's are reported, but
 * the first measurement in each series discarded as "warmup" on the IPC
 * primitive.  Variations on the test permit for pipelining, or the insertion
 * of more than one packet into the stream at a time, intended to permit
 * greater parallelism, hopefully allowing performance numbers to reflect
 * use of available parallelism, and/or intelligence in context switching to
 * avoid premature switching when multiple messages are queued.
 */

/*
 * The UDP test uses UDP over the loopback interface.  Two arbitrary but
 * fixed port numbers.
 */
#define	UDP_PORT1	2020
#define	UDP_PORT2	2021

/*
 * Size of each message.  Must be smaller than the socket buffer or pipe
 * buffer maximum size, as we want to send it atomically without blocking.
 * If pipelining is in use, must be able to fit PIPELINE_MAX of these
 * messages into the send queue.
 */
#define	MESSAGELEN	128

/*
 * Number of message cycles -- into fd1, out of fd2, into fd2, and out of
 * fd1.  By counting in cycles, we allow the master thread or process to
 * perform timing without explicitly synchronizing with the secondary thread
 * or process.
 */
#define	NUMCYCLES	1024

/*
 * Number of times to run each test.
 */
#define	LOOPS		10

/*
 * Number of in-flight messages per cycle.  I adjusting this value, be
 * careful not to exceed the socket/etc buffer depth, or messages may be lost
 * or result in blocking.
 */
#define	PIPELINE_MAX	4

static int
udp_create(int *fd1p, int *fd2p)
{
	struct sockaddr_in sin1, sin2;
	int sock1, sock2;

	sock1 = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock1 == -1)
		return (-1);

	sock2 = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock2 == -1) {
		close(sock1);
		return (-1);
	}

	bzero(&sin1, sizeof(sin1));
	sin1.sin_len = sizeof(sin1);
	sin1.sin_family = AF_INET;
	sin1.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin1.sin_port = htons(UDP_PORT1);

	bzero(&sin2, sizeof(sin2));
	sin2.sin_len = sizeof(sin2);
	sin2.sin_family = AF_INET;
	sin2.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin2.sin_port = htons(UDP_PORT2);

	if (bind(sock1, (struct sockaddr *) &sin1, sizeof(sin1)) < 0) {
		close(sock1);
		close(sock2);
		return (-1);
	}

	if (bind(sock2, (struct sockaddr *) &sin2, sizeof(sin2)) < 0) {
		close(sock1);
		close(sock2);
		return (-1);
	}

	if (connect(sock1, (struct sockaddr *) &sin2, sizeof(sin2)) < 0) {
		close(sock1);
		close(sock2);
		return (-1);
	}

	if (connect(sock2, (struct sockaddr *) &sin1, sizeof(sin1)) < 0) {
		close(sock1);
		close(sock2);
		return (-1);
	}

	*fd1p = sock1;
	*fd2p = sock2;

	return (0);
}

static int
pipe_create(int *fd1p, int *fd2p)
{
	int fds[2];

	if (pipe(fds) < 0)
		return (-1);

	*fd1p = fds[0];
	*fd2p = fds[1];

	return (0);
}

static int
socketpairdgram_create(int *fd1p, int *fd2p)
{
	int fds[2];

	if (socketpair(PF_LOCAL, SOCK_DGRAM, 0, fds) < 0)
		return (-1);

	*fd1p = fds[0];
	*fd2p = fds[1];

	return (0);
}

static int
socketpairstream_create(int *fd1p, int *fd2p)
{
	int fds[2];

	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, fds) < 0)
		return (-1);

	*fd1p = fds[0];
	*fd2p = fds[1];

	return (0);
}

static int
message_send(int s)
{
	u_char buffer[MESSAGELEN];
	ssize_t len;

	bzero(buffer, sizeof(buffer));

	len = write(s, buffer, sizeof(buffer));
	if (len == -1)
		return (-1);
	if (len != sizeof(buffer)) {
		errno = EMSGSIZE;
		return (-1);
	}
	return (0);
}

static int
message_recv(int s)
{
	u_char buffer[MESSAGELEN];
	ssize_t len;

	len = read(s, buffer, sizeof(buffer));
	if (len == -1)
		return (-1);
	if (len != sizeof(buffer)) {
		errno = EMSGSIZE;
		return (-1);
	}
	return (0);
}

/*
 * Juggle messages between two file descriptors in a single thread/process,
 * so simply a measure of IPC performance.
 */
static struct timespec
juggle(int fd1, int fd2, int pipeline)
{
	struct timespec tstart, tfinish;
	int i, j;

	if (clock_gettime(CLOCK_REALTIME, &tstart) < 0)
		err(-1, "juggle: clock_gettime");

	for (i = 0; i < NUMCYCLES; i++) {

		for (j = 0; j < pipeline; j++) {
			if (message_send(fd1) < 0)
				err(-1, "message_send fd1");
		}

		for (j = 0; j < pipeline; j++) {
			if (message_recv(fd2) < 0)
				err(-1, "message_recv fd2");

			if (message_send(fd2) < 0)
				err(-1, "message_send fd2");
		}

		for (j = 0; j < pipeline; j++) {
			if (message_recv(fd1) < 0)
				err(-1, "message_recv fd1");
		}
	}

	if (clock_gettime(CLOCK_REALTIME, &tfinish) < 0)
		err(-1, "juggle: clock_gettime");

	timespecsub(&tfinish, &tstart, &tfinish);

	return (tfinish);
}

/*
 * Juggle messages between two file descriptors in two threads, so measure
 * the cost of IPC and the cost of a thread context switch.
 *
 * In order to avoid measuring thread creation time, we make use of a
 * condition variable to decide when both threads are ready to begin
 * juggling.
 */
static int threaded_child_ready;
static pthread_mutex_t threaded_mtx;
static pthread_cond_t threaded_cond;
static int threaded_pipeline;

static void *
juggling_thread(void *arg)
{
	int fd2, i, j;

	fd2 = *(int *)arg;

	if (pthread_mutex_lock(&threaded_mtx) != 0)
		err(-1, "juggling_thread: pthread_mutex_lock");

	threaded_child_ready = 1;

	if (pthread_cond_signal(&threaded_cond) != 0)
		err(-1, "juggling_thread: pthread_cond_signal");

	if (pthread_mutex_unlock(&threaded_mtx) != 0)
		err(-1, "juggling_thread: pthread_mutex_unlock");

	for (i = 0; i < NUMCYCLES; i++) {
		for (j = 0; j < threaded_pipeline; j++) {
			if (message_recv(fd2) < 0)
				err(-1, "message_recv fd2");

			if (message_send(fd2) < 0)
				err(-1, "message_send fd2");
		}
	}

	return (NULL);
}

static struct timespec
thread_juggle(int fd1, int fd2, int pipeline)
{
	struct timespec tstart, tfinish;
	pthread_t thread;
	int i, j;

	threaded_pipeline = pipeline;

	if (pthread_mutex_init(&threaded_mtx, NULL) != 0)
		err(-1, "thread_juggle: pthread_mutex_init");

	if (pthread_create(&thread, NULL, juggling_thread, &fd2) != 0)
		err(-1, "thread_juggle: pthread_create");

	if (pthread_mutex_lock(&threaded_mtx) != 0)
		err(-1, "thread_juggle: pthread_mutex_lock");

	while (!threaded_child_ready) {
		if (pthread_cond_wait(&threaded_cond, &threaded_mtx) != 0)
			err(-1, "thread_juggle: pthread_cond_wait");
	}

	if (pthread_mutex_unlock(&threaded_mtx) != 0)
		err(-1, "thread_juggle: pthread_mutex_unlock");

	if (clock_gettime(CLOCK_REALTIME, &tstart) < 0)
		err(-1, "thread_juggle: clock_gettime");

	for (i = 0; i < NUMCYCLES; i++) {
		for (j = 0; j < pipeline; j++) {
			if (message_send(fd1) < 0)
				err(-1, "message_send fd1");
		}

		for (j = 0; j < pipeline; j++) {
			if (message_recv(fd1) < 0)
				err(-1, "message_recv fd1");
		}
	}

	if (clock_gettime(CLOCK_REALTIME, &tfinish) < 0)
		err(-1, "thread_juggle: clock_gettime");

	if (pthread_join(thread, NULL) != 0)
		err(-1, "thread_juggle: pthread_join");

	timespecsub(&tfinish, &tstart, &tfinish);

	return (tfinish);
}

/*
 * Juggle messages between two file descriptors in two processes, so measure
 * the cost of IPC and the cost of a process context switch.
 *
 * Since we can't use a mutex between the processes, we simply do an extra
 * write on the child to let the parent know that it's ready to start.
 */
static struct timespec
process_juggle(int fd1, int fd2, int pipeline)
{
	struct timespec tstart, tfinish;
	pid_t pid, ppid, wpid;
	int error, i, j;

	ppid = getpid();

	pid = fork();
	if (pid < 0)
		err(-1, "process_juggle: fork");

	if (pid == 0) {
		if (message_send(fd2) < 0) {
			error = errno;
			kill(ppid, SIGTERM);
			errno = error;
			err(-1, "process_juggle: child: message_send");
		}

		for (i = 0; i < NUMCYCLES; i++) {
			for (j = 0; j < pipeline; j++) {
				if (message_send(fd2) < 0)
					err(-1, "message_send fd2");

				if (message_recv(fd2) < 0)
					err(-1, "message_recv fd2");
			}
		}

		exit(0);
	} else {
		if (message_recv(fd1) < 0) {
			error = errno;
			kill(pid, SIGTERM);
			errno = error;
			err(-1, "process_juggle: parent: message_recv");
		}

		if (clock_gettime(CLOCK_REALTIME, &tstart) < 0)
			err(-1, "process_juggle: clock_gettime");

		for (i = 0; i < NUMCYCLES; i++) {
			for (j = 0; j < pipeline; j++) {
				if (message_send(fd1) < 0) {
					error = errno;
					kill(pid, SIGTERM);
					errno = error;
					err(-1, "message_send fd1");
				}
			}

			for (j = 0; j < pipeline; j++) {
				if (message_recv(fd1) < 0) {
					error = errno;
					kill(pid, SIGTERM);
					errno = error;
					err(-1, "message_recv fd1");
				}
			}
		}

		if (clock_gettime(CLOCK_REALTIME, &tfinish) < 0)
			err(-1, "process_juggle: clock_gettime");
	}

	wpid = waitpid(pid, NULL, 0);
	if (wpid < 0)
		err(-1, "process_juggle: waitpid");
	if (wpid != pid)
		errx(-1, "process_juggle: waitpid: pid != wpid");

	timespecsub(&tfinish, &tstart, &tfinish);

	return (tfinish);
}

/*
 * When we print out results for larger pipeline sizes, we scale back by the
 * depth of the pipeline.  This generally means dividing by the pipeline
 * depth.  Except when it means dividing by zero.
 */
static void
scale_timespec(struct timespec *ts, int p)
{

	if (p == 0)
		return;

	ts->tv_sec /= p;
	ts->tv_nsec /= p;
}

static const struct ipctype {
	int		(*it_create)(int *fd1p, int *fd2p);
	const char	*it_name;
} ipctypes[] = {
	{ pipe_create, "pipe" },
	{ udp_create, "udp" },
	{ socketpairdgram_create, "socketpairdgram" },
	{ socketpairstream_create, "socketpairstream" },
};
static const int ipctypes_len = (sizeof(ipctypes) / sizeof(struct ipctype));

int
main(int argc, char *argv[])
{
	struct timespec juggle_results[LOOPS], process_results[LOOPS];
	struct timespec thread_results[LOOPS];
	int fd1, fd2, i, j, p;
	struct utsname uts;

	printf("version, juggle.c %s\n", "$FreeBSD$");

	if (uname(&uts) < 0)
		err(-1, "utsname");
	printf("sysname, %s\n", uts.sysname);
	printf("nodename, %s\n", uts.nodename);
	printf("release, %s\n", uts.release);
	printf("version, %s\n", uts.version);
	printf("machine, %s\n", uts.machine);
	printf("\n");

	printf("MESSAGELEN, %d\n", MESSAGELEN);
	printf("NUMCYCLES, %d\n", NUMCYCLES);
	printf("LOOPS, %d\n", LOOPS);
	printf("PIPELINE_MAX, %d\n", PIPELINE_MAX);
	printf("\n\n");

	printf("ipctype, test, pipeline_depth");
	for (j = 0; j < LOOPS; j++)
		printf(", data%d", j);
	printf("\n");
	fflush(stdout);
	for (p = 0; p < PIPELINE_MAX + 1; p++) {
		for (i = 0; i < ipctypes_len; i++) {
			if (ipctypes[i].it_create(&fd1, &fd2) < 0)
				err(-1, "main: %s", ipctypes[i].it_name);

			/*
			 * For each test, do one uncounted warmup, then LOOPS
			 * runs of the actual test.
			 */
			juggle(fd1, fd2, p);
			for (j = 0; j < LOOPS; j++)
				juggle_results[j] = juggle(fd1, fd2, p);
			process_juggle(fd1, fd2, p);
			for (j = 0; j < LOOPS; j++)
				process_results[j] = process_juggle(fd1, fd2,
				    p);
			thread_juggle(fd1, fd2, p);
			for (j = 0; j < LOOPS; j++)
				thread_results[j] = thread_juggle(fd1, fd2,
				    p);
			for (j = 0; j < LOOPS; j++) {
				thread_results[j].tv_sec = 0;
				thread_results[j].tv_nsec = 0;
			}
			close(fd1);
			close(fd2);
		}
		/*
		 * When printing results for the round, normalize the results
		 * with respect to the pipeline depth.  We're doing p times
		 * as much work, and are we taking p times as long?
		 */
		for (i = 0; i < ipctypes_len; i++) {
			printf("%s, juggle, %d, ", ipctypes[i].it_name, p);
			for (j = 0; j < LOOPS; j++) {
				if (j != 0)
					printf(", ");
				scale_timespec(&juggle_results[j], p);
				printf("%jd.%09lu",
				    (intmax_t)juggle_results[j].tv_sec,
				    juggle_results[j].tv_nsec);
			}
			printf("\n");
			printf("%s, process_juggle, %d, ",
			    ipctypes[i].it_name, p);
			for (j = 0; j < LOOPS; j++) {
				if (j != 0)
					printf(", ");
				scale_timespec(&process_results[j], p);
				printf("%jd.%09lu",
                                    (intmax_t)process_results[j].tv_sec,
				    process_results[j].tv_nsec);
			}
			printf("\n");
			printf("%s, thread_juggle, %d, ",
			    ipctypes[i].it_name, p);
			for (j = 0; j < LOOPS; j++) {
				if (j != 0)
					printf(", ");
				scale_timespec(&thread_results[j], p);
				printf("%jd.%09lu",
				    (intmax_t)thread_results[j].tv_sec,
				    thread_results[j].tv_nsec);
			}
			printf("\n");
		}
		fflush(stdout);
	}
	return (0);
}

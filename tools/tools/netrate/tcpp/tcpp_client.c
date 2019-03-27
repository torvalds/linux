/*-
 * Copyright (c) 2008-2009 Robert N. M. Watson
 * Copyright (c) 2010 Juniper Networks, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract
 * to Juniper Networks, Inc.
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
#include <sys/event.h>
#include <sys/resource.h>
#include <sys/sched.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tcpp.h"

#define	min(x, y)	(x < y ? x : y)


/*
 * Gist of each client worker: build up to mflag connections at a time, and
 * pump data in to them somewhat fairly until tflag connections have been
 * completed.
 */
#define	CONNECTION_MAGIC	0x87a3f56e
struct connection {
	uint32_t	conn_magic;		/* Just magic. */
	int		conn_fd;
	struct tcpp_header	conn_header;	/* Header buffer. */
	u_int		conn_header_sent;	/* Header bytes sent. */
	u_int64_t	conn_data_sent;		/* Data bytes sent.*/
};

static u_char			 buffer[256 * 1024];	/* Buffer to send. */
static pid_t			*pid_list;
static int			 kq;
static int			 started;	/* Number started so far. */
static int			 finished;	/* Number finished so far. */
static int			 counter;	/* IP number offset. */
static uint64_t			 payload_len;

static struct connection *
tcpp_client_newconn(void)
{
	struct sockaddr_in sin;
	struct connection *conn;
	struct kevent kev;
	int fd, i;

	/*
	 * Spread load over available IPs, rotating through them as we go.  No
	 * attempt to localize IPs to particular workers.
	 */
	sin = localipbase;
	sin.sin_addr.s_addr = htonl(ntohl(localipbase.sin_addr.s_addr) +
	    (counter++ % Mflag));

	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		err(-1, "socket");

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
		err(-1, "fcntl");

	i = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &i, sizeof(i)) < 0)
		err(-1, "setsockopt");
	i = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &i, sizeof(i)) < 0)
		err(-1, "setsockopt");
#if 0
	i = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0)
		err(-1, "setsockopt");
#endif

	if (lflag) {
		if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
			err(-1, "bind");
	}

	if (connect(fd, (struct sockaddr *)&remoteip, sizeof(remoteip)) < 0 &&
	    errno != EINPROGRESS)
		err(-1, "connect");

	conn = malloc(sizeof(*conn));
	if (conn == NULL)
		return (NULL);
	bzero(conn, sizeof(*conn));
	conn->conn_magic = CONNECTION_MAGIC;
	conn->conn_fd = fd;
	conn->conn_header.th_magic = TCPP_MAGIC;
	conn->conn_header.th_len = payload_len;
	tcpp_header_encode(&conn->conn_header);

	EV_SET(&kev, fd, EVFILT_WRITE, EV_ADD, 0, 0, conn);
	if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0)
		err(-1, "newconn kevent");

	started++;
	return (conn);
}

static void
tcpp_client_closeconn(struct connection *conn)
{

	close(conn->conn_fd);
	bzero(conn, sizeof(*conn));
	free(conn);
	finished++;
}

static void
tcpp_client_handleconn(struct kevent *kev)
{
	struct connection *conn;
	struct iovec iov[2];
	ssize_t len, header_left;

	conn = kev->udata;
	if (conn->conn_magic != CONNECTION_MAGIC)
		errx(-1, "tcpp_client_handleconn: magic");

	if (conn->conn_header_sent < sizeof(conn->conn_header)) {
		header_left = sizeof(conn->conn_header) -
		    conn->conn_header_sent;
		iov[0].iov_base = ((u_char *)&conn->conn_header) +
		    conn->conn_header_sent;
		iov[0].iov_len = header_left;
		iov[1].iov_base = buffer;
		iov[1].iov_len = min(sizeof(buffer), payload_len);
		len = writev(conn->conn_fd, iov, 2);
		if (len < 0) {
			tcpp_client_closeconn(conn);
			err(-1, "tcpp_client_handleconn: header write");
		}
		if (len == 0) {
			tcpp_client_closeconn(conn);
			errx(-1, "tcpp_client_handleconn: header write "
			    "premature EOF");
		}
		if (len > header_left) {
			conn->conn_data_sent += (len - header_left);
			conn->conn_header_sent += header_left;
		} else
			conn->conn_header_sent += len;
	} else {
		len = write(conn->conn_fd, buffer, min(sizeof(buffer),
		    payload_len - conn->conn_data_sent));
		if (len < 0) {
			tcpp_client_closeconn(conn);
			err(-1, "tcpp_client_handleconn: data write");
		}
		if (len == 0) {
			tcpp_client_closeconn(conn);
			errx(-1, "tcpp_client_handleconn: data write: "
			    "premature EOF");
		}
		conn->conn_data_sent += len;
	}
	if (conn->conn_data_sent >= payload_len) {
		/*
		 * All is well.
		 */
		tcpp_client_closeconn(conn);
	}
}

static void
tcpp_client_worker(int workernum)
{
	struct kevent *kev_array;
	int i, numevents, kev_bytes;
#if defined(CPU_SETSIZE) && 0
	cpu_set_t mask;
	int ncpus;
	size_t len;

	if (Pflag) {
		len = sizeof(ncpus);
		if (sysctlbyname(SYSCTLNAME_CPUS, &ncpus, &len, NULL, 0) < 0)
			err(-1, "sysctlbyname: %s", SYSCTLNAME_CPUS);
		if (len != sizeof(ncpus))
			errx(-1, "sysctlbyname: %s: len %jd", SYSCTLNAME_CPUS,
			    (intmax_t)len);

		CPU_ZERO(&mask);
		CPU_SET(workernum % ncpus, &mask);
		if (sched_setaffinity(0, CPU_SETSIZE, &mask) < 0)
			err(-1, "sched_setaffinity");
	}
#endif
	setproctitle("tcpp_client %d", workernum);

	/*
	 * Add the worker number to the remote port.
	 */
	remoteip.sin_port = htons(rflag + workernum);

	kev_bytes = sizeof(*kev_array) * mflag;
	kev_array = malloc(kev_bytes);
	if (kev_array == NULL)
		err(-1, "malloc");
	bzero(kev_array, kev_bytes);

	kq = kqueue();
	if (kq < 0)
		err(-1, "kqueue");

	while (finished < tflag) {
		while ((started - finished < mflag) && (started < tflag))
			(void)tcpp_client_newconn();
		numevents = kevent(kq, NULL, 0, kev_array, mflag, NULL);
		if (numevents < 0)
			err(-1, "kevent");
		if (numevents > mflag)
			errx(-1, "kevent: %d", numevents);
		for (i = 0; i < numevents; i++)
			tcpp_client_handleconn(&kev_array[i]);
	}
	/* printf("Worker %d done - %d finished\n", workernum, finished); */
}

void
tcpp_client(void)
{
	struct timespec ts_start, ts_finish;
	long cp_time_start[CPUSTATES], cp_time_finish[CPUSTATES];
	long ticks;
	size_t size;
	pid_t pid;
	int i, failed, status;

	if (bflag < sizeof(struct tcpp_header))
		errx(-1, "Can't use -b less than %zu\n",
		   sizeof(struct tcpp_header));
	payload_len = bflag - sizeof(struct tcpp_header);

	pid_list = malloc(sizeof(*pid_list) * pflag);
	if (pid_list == NULL)
		err(-1, "malloc pid_list");
	bzero(pid_list, sizeof(*pid_list) * pflag);

	/*
	 * Start workers.
	 */
	size = sizeof(cp_time_start);
	if (sysctlbyname(SYSCTLNAME_CPTIME, &cp_time_start, &size, NULL, 0)
	    < 0)
		err(-1, "sysctlbyname: %s", SYSCTLNAME_CPTIME);
	if (clock_gettime(CLOCK_REALTIME, &ts_start) < 0)
		err(-1, "clock_gettime");
	for (i = 0; i < pflag; i++) {
		pid = fork();
		if (pid < 0) {
			warn("fork");
			for (i = 0; i < pflag; i++) {
				if (pid_list[i] != 0)
					(void)kill(pid_list[i], SIGKILL);
			}
			exit(-1);
		}
		if (pid == 0) {
			tcpp_client_worker(i);
			exit(0);
		}
		pid_list[i] = pid;
	}

	/*
	 * GC workers.
	 */
	failed = 0;
	for (i = 0; i < pflag; i++) {
		if (pid_list[i] != 0) {
			while (waitpid(pid_list[i], &status, 0) != pid_list[i]);
			if (WEXITSTATUS(status) != 0)
				failed = 1;
		}
	}
	if (clock_gettime(CLOCK_REALTIME, &ts_finish) < 0)
		err(-1, "clock_gettime");
	size = sizeof(cp_time_finish);
	if (sysctlbyname(SYSCTLNAME_CPTIME, &cp_time_finish, &size, NULL, 0)
	    < 0)
		err(-1, "sysctlbyname: %s", SYSCTLNAME_CPTIME);
	timespecsub(&ts_finish, &ts_start, &ts_finish);

	if (failed)
		errx(-1, "Too many errors");

	if (hflag)
		printf("bytes,seconds,conn/s,Gb/s,user%%,nice%%,sys%%,"
		    "intr%%,idle%%\n");

	/*
	 * Configuration parameters.
	 */
	printf("%jd,", bflag * tflag * pflag);
	printf("%jd.%09jd,", (intmax_t)ts_finish.tv_sec,
	    (intmax_t)(ts_finish.tv_nsec));

	/*
	 * Effective transmit rates.
	 */
	printf("%f,", (double)(pflag * tflag)/
	    (ts_finish.tv_sec + ts_finish.tv_nsec * 1e-9));
	printf("%f,", (double)(bflag * tflag * pflag * 8) /
	    (ts_finish.tv_sec + ts_finish.tv_nsec * 1e-9) * 1e-9);

	/*
	 * CPU time (est).
	 */
	ticks = 0;
	for (i = 0; i < CPUSTATES; i++) {
		cp_time_finish[i] -= cp_time_start[i];
		ticks += cp_time_finish[i];
	}
	printf("%0.02f,", (float)(100 * cp_time_finish[CP_USER]) / ticks);
	printf("%0.02f,", (float)(100 * cp_time_finish[CP_NICE]) / ticks);
	printf("%0.02f,", (float)(100 * cp_time_finish[CP_SYS]) / ticks);
	printf("%0.02f,", (float)(100 * cp_time_finish[CP_INTR]) / ticks);
	printf("%0.02f", (float)(100 * cp_time_finish[CP_IDLE]) / ticks);
	printf("\n");
}

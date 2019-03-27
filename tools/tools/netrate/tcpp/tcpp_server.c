/*-
 * Copyright (c) 2008-2009 Robert N. M. Watson
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
#include <sys/endian.h>
#include <sys/event.h>
#include <sys/resource.h>
#include <sys/sched.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tcpp.h"

/*
 * Server side -- create a pool of processes, each listening on its own TCP
 * port number for new connections.  The first 8 bytes of each connection
 * will be a network byte order length, then there will be that number of
 * bytes of data.  We use non-blocking sockets with kqueue to to avoid the
 * overhead of threading or more than one process per processor, which makes
 * things a bit awkward when dealing with data we care about.  As such, we
 * read into a small character buffer which we then convert to a length once
 * we have all the data.
 */
#define	CONNECTION_MAGIC	0x6392af27
struct connection {
	uint32_t	conn_magic;		/* Just magic. */
	int		conn_fd;
	struct tcpp_header	conn_header;	/* Header buffer. */
	u_int		conn_header_len;	/* Bytes so far. */
	u_int64_t	conn_data_len;		/* How much to sink. */
	u_int64_t	conn_data_received;	/* How much so far. */
};

static pid_t			*pid_list;
static int			 kq;

static struct connection *
tcpp_server_newconn(int listen_fd)
{
	struct connection *conn;
	struct kevent kev;
	int fd;

	fd = accept(listen_fd, NULL, NULL);
	if (fd < 0) {
		warn("accept");
		return (NULL);
	}

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
		err(-1, "fcntl");

	conn = malloc(sizeof(*conn));
	if (conn == NULL)
		return (NULL);
	bzero(conn, sizeof(*conn));
	conn->conn_magic = CONNECTION_MAGIC;
	conn->conn_fd = fd;

	/*
	 * Register to read on the socket, and set our conn pointer as the
	 * udata so we can find it quickly in the future.
	 */
	EV_SET(&kev, fd, EVFILT_READ, EV_ADD, 0, 0, conn);
	if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0)
		err(-1, "kevent");

	return (conn);
}

static void
tcpp_server_closeconn(struct connection *conn)
{

	/*
	 * Kqueue cleans up after itself once we close the socket, and since
	 * we are processing only one kevent at a time, we don't need to
	 * worry about watching out for future kevents referring to it.
	 *
	 * ... right?
	 */
	close(conn->conn_fd);
	bzero(conn, sizeof(*conn));
	free(conn);
}

static u_char buffer[256*1024];	/* Buffer in which to sink data. */
static void
tcpp_server_handleconn(struct kevent *kev)
{
	struct connection *conn;
	ssize_t len;

	conn = kev->udata;
	if (conn->conn_magic != CONNECTION_MAGIC)
		errx(-1, "tcpp_server_handleconn: magic");

	if (conn->conn_header_len < sizeof(conn->conn_header)) {
		len = read(conn->conn_fd,
		    ((u_char *)&conn->conn_header) + conn->conn_header_len,
		    sizeof(conn->conn_header) - conn->conn_header_len);
		if (len < 0) {
			warn("tcpp_server_handleconn: header read");
			tcpp_server_closeconn(conn);
			return;
		}
		if (len == 0) {
			warnx("tcpp_server_handleconn: header premature eof");
			tcpp_server_closeconn(conn);
			return;
		}
		conn->conn_header_len += len;
		if (conn->conn_header_len == sizeof(conn->conn_header)) {
			tcpp_header_decode(&conn->conn_header);
			if (conn->conn_header.th_magic != TCPP_MAGIC) {
				warnx("tcpp_server_handleconn: bad magic");
				tcpp_server_closeconn(conn);
				return;
			}
		}
	} else {
		/*
		 * Drain up to a buffer from the connection, so that we pay
		 * attention to other connections too.
		 */
		len = read(conn->conn_fd, buffer, sizeof(buffer));
		if (len < 0) {
			warn("tcpp_server_handleconn: data bad read");
			tcpp_server_closeconn(conn);
			return;
		}
		if (len == 0 && conn->conn_data_received <
		    conn->conn_header.th_len) {
			warnx("tcpp_server_handleconn: data premature eof");
			tcpp_server_closeconn(conn);
			return;
		}
		conn->conn_data_received += len;
		if (conn->conn_data_received > conn->conn_header.th_len) {
			warnx("tcpp_server_handleconn: too much data");
			tcpp_server_closeconn(conn);
			return;
		}
		if (conn->conn_data_received == conn->conn_header.th_len) {
			/*
			 * All is well.
			 */
			tcpp_server_closeconn(conn);
			return;
		}
	}
}

static void
tcpp_server_worker(int workernum)
{
	int i, listen_sock, numevents;
	struct kevent kev, *kev_array;
	int kev_bytes;
#if defined(CPU_SETSIZE) && 0
	cpu_set_t mask;
	int ncpus;
	ssize_t len;

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
	setproctitle("tcpp_server %d", workernum);

	/* Allow an extra kevent for the listen socket. */
	kev_bytes = sizeof(*kev_array) * (mflag + 1);
	kev_array = malloc(kev_bytes);
	if (kev_array == NULL)
		err(-1, "malloc");
	bzero(kev_array, kev_bytes);

	/* XXXRW: Want to set and pin the CPU here. */

	/*
	 * Add the worker number to the local port.
	 */
	localipbase.sin_port = htons(rflag + workernum);

	listen_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (listen_sock < 0)
		err(-1, "socket");
	i = 1;
	if (setsockopt(listen_sock, SOL_SOCKET, SO_NOSIGPIPE, &i, sizeof(i))
	    < 0)
		err(-1, "setsockopt");
	i = 1;
	if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEPORT, &i, sizeof(i))
	    < 0)
		err(-1, "setsockopt");
	i = 1;
	if (setsockopt(listen_sock, IPPROTO_TCP, TCP_NODELAY, &i, sizeof(i))
	    < 0)
		err(-1, "setsockopt");
	if (bind(listen_sock, (struct sockaddr *)&localipbase,
	    sizeof(localipbase)) < 0)
		err(-1, "bind");
	if (listen(listen_sock, 16384))
		err(-1, "listen");
	if (fcntl(listen_sock, F_SETFL, O_NONBLOCK) < 0)
		err(-1, "fcntl");

	kq = kqueue();
	if (kq < 0)
		err(-1, "kqueue");

	EV_SET(&kev, listen_sock, EVFILT_READ, EV_ADD, 0, 0, NULL);
	if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0)
		err(-1, "kevent");

	while ((numevents = kevent(kq, NULL, 0, kev_array, mflag + 1, NULL))
	    > 0) {
		for (i = 0; i < numevents; i++) {
			if (kev_array[i].ident == (u_int)listen_sock)
				(void)tcpp_server_newconn(listen_sock);
			else
				tcpp_server_handleconn(&kev_array[i]);
		}
	}
	printf("Worker %d done\n", workernum);
}

void
tcpp_server(void)
{
#if 0
	long cp_time_last[CPUSTATES], cp_time_now[CPUSTATES], ticks;
	size_t size;
#endif
	pid_t pid;
	int i;

	pid_list = malloc(sizeof(*pid_list) * pflag);
	if (pid_list == NULL)
		err(-1, "malloc pid_list");
	bzero(pid_list, sizeof(*pid_list) * pflag);

	/*
	 * Start workers.
	 */
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
			tcpp_server_worker(i);
			exit(0);
		}
		pid_list[i] = pid;
	}

#if 0
		size = sizeof(cp_time_last);
		if (sysctlbyname(SYSCTLNAME_CPTIME, &cp_time_last, &size,
		    NULL, 0) < 0)
			err(-1, "sysctlbyname: %s", SYSCTLNAME_CPTIME);
		while (1) {
			sleep(10);
			size = sizeof(cp_time_last);
			if (sysctlbyname(SYSCTLNAME_CPTIME, &cp_time_now,
			    &size, NULL, 0) < 0)
				err(-1, "sysctlbyname: %s",
				    SYSCTLNAME_CPTIME);
			ticks = 0;
			for (i = 0; i < CPUSTATES; i++) {
				cp_time_last[i] = cp_time_now[i] -
				    cp_time_last[i];
				ticks += cp_time_last[i];
			}
			printf("user%% %lu nice%% %lu sys%% %lu intr%% %lu "
			    "idle%% %lu\n",
			    (100 * cp_time_last[CP_USER]) / ticks,
			    (100 * cp_time_last[CP_NICE]) / ticks,
			    (100 * cp_time_last[CP_SYS]) / ticks,
			    (100 * cp_time_last[CP_INTR]) / ticks,
			    (100 * cp_time_last[CP_IDLE]) / ticks);
			bcopy(cp_time_now, cp_time_last, sizeof(cp_time_last));
		}
#endif

	/*
	 * GC workers.
	 */
	for (i = 0; i < pflag; i++) {
		if (pid_list[i] != 0) {
			while (waitpid(pid_list[i], NULL, 0) != pid_list[i]);
		}
	}
}

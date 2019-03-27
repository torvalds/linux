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
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

static int	threaded;		/* 1 for threaded, 0 for forked. */

/*
 * Simple, multi-threaded/multi-process HTTP server.  Very dumb.
 *
 * If a path is specified as an argument, only that file is served.  If no
 * path is specified, httpd will create one file to send per server thread.
 */
#define	THREADS		128
#define	BUFFER		1024
#define	FILESIZE	1024

#define	HTTP_OK		"HTTP/1.1 200 OK\n"
#define	HTTP_SERVER1	"Server rwatson_httpd/1.0 ("
#define	HTTP_SERVER2	")\n"
#define	HTTP_CONNECTION	"Connection: close\n"
#define	HTTP_CONTENT	"Content-Type: text/html\n\n"

/*
 * In order to support both multi-threaded and multi-process operation but
 * use a single shared memory statistics model, we create a page-aligned
 * statistics buffer.  For threaded operation, it's just shared memory due to
 * threading; for multi-process operation, we mark it as INHERIT_SHARE, so we
 * must put it in page-aligned memory that isn't shared with other memory, or
 * risk accidental sharing of other statep.
 */
static struct state {
	struct httpd_thread_statep {
		pthread_t	hts_thread;	/* Multi-thread. */
		pid_t		hts_pid;	/* Multi-process. */
		int		hts_fd;
	} hts[THREADS];

	const char	*path;
	int		 data_file;
	int		 listen_sock;
	struct utsname	 utsname;
} *statep;

/*
 * Borrowed from sys/param.h.
 */
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))	/* to any y */

/*
 * Given an open client socket, process its request.  No notion of timeout.
 */
static int
http_serve(int sock, int fd)
{
	struct iovec header_iovec[6];
	struct sf_hdtr sf_hdtr;
	char buffer[BUFFER];
	ssize_t len;
	int i, ncount;

	/* Read until \n\n.  Not very smart. */
	ncount = 0;
	while (1) {
		len = recv(sock, buffer, BUFFER, 0);
		if (len < 0) {
			warn("recv");
			return (-1);
		}
		if (len == 0)
			return (-1);
		for (i = 0; i < len; i++) {
			switch (buffer[i]) {
			case '\n':
				ncount++;
				break;

			case '\r':
				break;

			default:
				ncount = 0;
			}
		}
		if (ncount == 2)
			break;
	}

	bzero(&sf_hdtr, sizeof(sf_hdtr));
	bzero(&header_iovec, sizeof(header_iovec));
	header_iovec[0].iov_base = HTTP_OK;
	header_iovec[0].iov_len = strlen(HTTP_OK);
	header_iovec[1].iov_base = HTTP_SERVER1;
	header_iovec[1].iov_len = strlen(HTTP_SERVER1);
	header_iovec[2].iov_base = statep->utsname.sysname;
	header_iovec[2].iov_len = strlen(statep->utsname.sysname);
	header_iovec[3].iov_base = HTTP_SERVER2;
	header_iovec[3].iov_len = strlen(HTTP_SERVER2);
	header_iovec[4].iov_base = HTTP_CONNECTION;
	header_iovec[4].iov_len = strlen(HTTP_CONNECTION);
	header_iovec[5].iov_base = HTTP_CONTENT;
	header_iovec[5].iov_len = strlen(HTTP_CONTENT);
	sf_hdtr.headers = header_iovec;
	sf_hdtr.hdr_cnt = 6;
	sf_hdtr.trailers = NULL;
	sf_hdtr.trl_cnt = 0;

	if (sendfile(fd, sock, 0, 0, &sf_hdtr, NULL, 0) < 0)
		warn("sendfile");

	return (0);
}

static void *
httpd_worker(void *arg)
{
	struct httpd_thread_statep *htsp;
	int sock;

	htsp = arg;

	while (1) {
		sock = accept(statep->listen_sock, NULL, NULL);
		if (sock < 0)
			continue;
		(void)http_serve(sock, htsp->hts_fd);
		close(sock);
	}
}

static void
killall(void)
{
	int i;

	for (i = 0; i < THREADS; i++) {
		if (statep->hts[i].hts_pid != 0)
			(void)kill(statep->hts[i].hts_pid, SIGTERM);
	}
}

static void
usage(void)
{

	fprintf(stderr, "httpd [-t] port [path]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	u_char filebuffer[FILESIZE];
	char temppath[PATH_MAX];
	struct sockaddr_in sin;
	int ch, error, i;
	char *pagebuffer;
	ssize_t len;
	pid_t pid;


	while ((ch = getopt(argc, argv, "t")) != -1) {
		switch (ch) {
		case 't':
			threaded = 1;
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1 && argc != 2)
		usage();

	len = roundup(sizeof(struct state), getpagesize());
	pagebuffer = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
	if (pagebuffer == MAP_FAILED)
		err(-1, "mmap");
	if (minherit(pagebuffer, len, INHERIT_SHARE) < 0)
		err(-1, "minherit");
	statep = (struct state *)pagebuffer;

	if (uname(&statep->utsname) < 0)
		err(-1, "utsname");

	statep->listen_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (statep->listen_sock < 0)
		err(-1, "socket(PF_INET, SOCK_STREAM)");

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(atoi(argv[0]));

	/*
	 * If a path is specified, use it.  Otherwise, create temporary files
	 * with some data for each thread.
	 */
	statep->path = argv[1];
	if (statep->path != NULL) {
		statep->data_file = open(statep->path, O_RDONLY);
		if (statep->data_file < 0)
			err(-1, "open: %s", statep->path);
		for (i = 0; i < THREADS; i++)
			statep->hts[i].hts_fd = statep->data_file;
	} else {
		memset(filebuffer, 'A', FILESIZE - 1);
		filebuffer[FILESIZE - 1] = '\n';
		for (i = 0; i < THREADS; i++) {
			snprintf(temppath, PATH_MAX, "/tmp/httpd.XXXXXXXXXXX");
			statep->hts[i].hts_fd = mkstemp(temppath);
			if (statep->hts[i].hts_fd < 0)
				err(-1, "mkstemp");
			(void)unlink(temppath);
			len = write(statep->hts[i].hts_fd, filebuffer,
			    FILESIZE);
			if (len < 0)
				err(-1, "write");
			if (len < FILESIZE)
				errx(-1, "write: short");
		}
	}

	if (bind(statep->listen_sock, (struct sockaddr *)&sin,
	    sizeof(sin)) < 0)
		err(-1, "bind");

	if (listen(statep->listen_sock, -1) < 0)
		err(-1, "listen");

	for (i = 0; i < THREADS; i++) {
		if (threaded) {
			if (pthread_create(&statep->hts[i].hts_thread, NULL,
			    httpd_worker, &statep->hts[i]) != 0)
				err(-1, "pthread_create");
		} else {
			pid = fork();
			if (pid < 0) {
				error = errno;
				killall();
				errno = error;
				err(-1, "fork");
			}
			if (pid == 0)
				httpd_worker(&statep->hts[i]);
			statep->hts[i].hts_pid = pid;
		}
	}

	for (i = 0; i < THREADS; i++) {
		if (threaded) {
			if (pthread_join(statep->hts[i].hts_thread, NULL)
			    != 0)
				err(-1, "pthread_join");
		} else {
			pid = waitpid(statep->hts[i].hts_pid, NULL, 0);
			if (pid == statep->hts[i].hts_pid)
				statep->hts[i].hts_pid = 0;
		}
	}
	if (!threaded)
		killall();
	return (0);
}

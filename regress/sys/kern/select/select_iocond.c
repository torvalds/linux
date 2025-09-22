/*	$OpenBSD: select_iocond.c,v 1.3 2021/12/27 16:38:06 visa Exp $	*/

/*
 * Copyright (c) 2021 Visa Hankala
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
 * Test select(2) with various I/O conditions.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__OpenBSD__)
/* for pty */
#include <termios.h>
#include <util.h>
#endif

#if !defined(__linux__)
#define HAVE_SOCKADDR_LEN 1
#endif

#define MIN(a, b) ((a) <= (b) ? (a) : (b))

#define TEST_FIFO_NAME "iocond_fifo"

enum filetype {
	FTYPE_NONE,
	FTYPE_FIFO,
	FTYPE_PIPE,
	FTYPE_PTY,
	FTYPE_SOCKET_TCP,
	FTYPE_SOCKET_UDP,
	FTYPE_SOCKET_UNIX,
};

static struct {
	const char		*name;
	enum filetype		 type;
} filetypes[] = {
	{ "fifo",		FTYPE_FIFO },
	{ "pipe",		FTYPE_PIPE },
#if defined(__OpenBSD__)
	{ "pty",		FTYPE_PTY },
#endif
	{ "socket-tcp",		FTYPE_SOCKET_TCP },
	{ "socket-udp",		FTYPE_SOCKET_UDP },
	{ "socket-unix",	FTYPE_SOCKET_UNIX },
};

static enum filetype filetype = FTYPE_NONE;

static void cleanup(void);
static void proc_barrier(int);
static void proc_child(int, int);
static void proc_parent(int, int);

int
main(int argc, char *argv[])
{
	const char *ftname;
	int bfd[2], fds[2];
	int child_fd = -1;
	int parent_fd = -1;
	int sock = -1;
	unsigned int i;
	pid_t pid;

	/* Enforce test timeout. */
	alarm(10);

	if (argc != 2) {
		fprintf(stderr, "usage: %s filetype\n", argv[0]);
		return 1;
	}
	ftname = argv[1];

	for (i = 0; i < sizeof(filetypes) / sizeof(filetypes[0]); i++) {
		if (strcmp(ftname, filetypes[i].name) == 0) {
			filetype = filetypes[i].type;
			break;
		}
	}
	if (filetype == FTYPE_NONE)
		errx(1, "unknown filetype");

	/* Open barrier sockets. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, bfd) == -1)
		err(1, "socketpair");

	atexit(cleanup);

	switch (filetype) {
	case FTYPE_FIFO:
		(void)unlink(TEST_FIFO_NAME);
		if (mkfifo(TEST_FIFO_NAME, 0644) == -1)
			err(1, "mkfifo");
		break;
	case FTYPE_PIPE:
		if (pipe(fds) == -1)
			err(1, "pipe");
		parent_fd = fds[0];
		child_fd = fds[1];
		break;
#if defined(__OpenBSD__)
	case FTYPE_PTY:
		if (openpty(&parent_fd, &child_fd, NULL, NULL, NULL) == -1)
			err(1, "openpty");
		break;
#endif
	case FTYPE_SOCKET_TCP: {
		struct sockaddr_in inaddr;

		sock = socket(AF_INET, SOCK_STREAM, 0);

		memset(&inaddr, 0, sizeof(inaddr));
#ifdef HAVE_SOCKADDR_LEN
		inaddr.sin_len = sizeof(inaddr);
#endif
		inaddr.sin_family = AF_INET;
		inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (bind(sock, (struct sockaddr *)&inaddr,
		    sizeof(inaddr)) == -1)
			err(1, "bind");
		if (listen(sock, 1) == -1)
			err(1, "listen");
		break;
	    }
	case FTYPE_SOCKET_UDP: {
		struct sockaddr_in inaddr;

		sock = socket(AF_INET, SOCK_DGRAM, 0);

		memset(&inaddr, 0, sizeof(inaddr));
#ifdef HAVE_SOCKADDR_LEN
		inaddr.sin_len = sizeof(inaddr);
#endif
		inaddr.sin_family = AF_INET;
		inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (bind(sock, (struct sockaddr *)&inaddr,
		    sizeof(inaddr)) == -1)
			err(1, "bind");
		break;
	    }
	case FTYPE_SOCKET_UNIX:
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1)
			err(1, "socketpair");
		parent_fd = fds[0];
		child_fd = fds[1];
		break;
	default:
		errx(1, "unhandled filetype");
	}

	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork");
	case 0:
		switch (filetype) {
		case FTYPE_FIFO:
			child_fd = open(TEST_FIFO_NAME, O_WRONLY);
			if (child_fd == -1)
				err(1, "child: open");
			break;
		case FTYPE_SOCKET_TCP: {
			struct sockaddr_in inaddr;
			socklen_t inaddrlen;
			int on = 1;

			/* Get the bound address. */
			inaddrlen = sizeof(inaddr);
			if (getsockname(sock, (struct sockaddr *)&inaddr,
			    &inaddrlen) == -1)
				err(1, "child: getsockname");

			child_fd = socket(AF_INET, SOCK_STREAM, 0);
			if (child_fd == -1)
				err(1, "child: socket");
			if (connect(child_fd, (struct sockaddr *)&inaddr,
			    sizeof(inaddr)) == -1)
				err(1, "child: connect");
			if (setsockopt(child_fd, IPPROTO_TCP, TCP_NODELAY,
			    &on, sizeof(on)) == -1)
				err(1, "child: setsockopt(TCP_NODELAY)");
			break;
		    }
		case FTYPE_SOCKET_UDP: {
			struct sockaddr_in inaddr;
			socklen_t inaddrlen;

			/* Get the bound address. */
			inaddrlen = sizeof(inaddr);
			if (getsockname(sock, (struct sockaddr *)&inaddr,
			    &inaddrlen) == -1)
				err(1, "child: getsockname");

			child_fd = socket(AF_INET, SOCK_DGRAM, 0);
			if (child_fd == -1)
				err(1, "child: socket");
			if (connect(child_fd, (struct sockaddr *)&inaddr,
			    sizeof(inaddr)) == -1)
				err(1, "child: connect");
			break;
		    }
		default:
			break;
		}
		if (parent_fd != -1) {
			close(parent_fd);
			parent_fd = -1;
		}
		if (sock != -1) {
			close(sock);
			sock = -1;
		}
		proc_child(child_fd, bfd[1]);
		_exit(0);
	default:
		switch (filetype) {
		case FTYPE_FIFO:
			parent_fd = open(TEST_FIFO_NAME, O_RDONLY);
			if (parent_fd == -1)
				err(1, "parent: open");
			break;
		case FTYPE_SOCKET_TCP: {
			int on = 1;

			parent_fd = accept(sock, NULL, NULL);
			if (parent_fd == -1)
				err(1, "parent: accept");
			if (setsockopt(parent_fd, IPPROTO_TCP, TCP_NODELAY,
			    &on, sizeof(on)) == -1)
				err(1, "parent: setsockopt(TCP_NODELAY)");
			break;
		    }
		case FTYPE_SOCKET_UDP:
			parent_fd = sock;
			sock = -1;
			break;
		default:
			break;
		}
		if (child_fd != -1) {
			close(child_fd);
			child_fd = -1;
		}
		if (sock != -1) {
			close(sock);
			sock = -1;
		}
		proc_parent(parent_fd, bfd[0]);
		break;
	}

	if (waitpid(pid, NULL, 0) == -1)
		err(1, "waitpid");

	return 0;
}

static void
cleanup(void)
{
	if (filetype == FTYPE_FIFO)
		(void)unlink(TEST_FIFO_NAME);
}

static void
proc_barrier(int fd)
{
	int ret;
	char b = 0;

	ret = write(fd, &b, 1);
	assert(ret == 1);
	ret = read(fd, &b, 1);
	assert(ret == 1);
}

static void
fdset_init(fd_set *rfd, fd_set *wfd, fd_set *efd, int fd)
{
	FD_ZERO(rfd);
	FD_ZERO(wfd);
	FD_ZERO(efd);
	FD_SET(fd, rfd);
	FD_SET(fd, wfd);
	FD_SET(fd, efd);
}

static void
proc_child(int fd, int bfd)
{
	char buf[1024];
	fd_set efd, rfd, wfd;
	struct timeval tv = { 0, 1 };
	struct timeval zerotv = { 0, 0 };
	size_t nbytes;
	int ret;
	char b = 0;

	proc_barrier(bfd);

	fdset_init(&rfd, &wfd, &efd, fd);
	ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
	assert(ret == 1);
	assert(FD_ISSET(fd, &rfd) == 0);
	assert(FD_ISSET(fd, &wfd) != 0);
	assert(FD_ISSET(fd, &efd) == 0);

	proc_barrier(bfd);

	ret = write(fd, &b, 1);
	assert(ret == 1);

	proc_barrier(bfd);

	fdset_init(&rfd, &wfd, &efd, fd);
	ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
	assert(ret == 1);
	assert(FD_ISSET(fd, &rfd) == 0);
	assert(FD_ISSET(fd, &wfd) != 0);
	assert(FD_ISSET(fd, &efd) == 0);

	proc_barrier(bfd);

	/* parent: read */

	proc_barrier(bfd);

	if (filetype != FTYPE_SOCKET_UDP) {
		/* write until full */
		memset(buf, 0, sizeof(buf));
		nbytes = 0;
		for (;;) {
			FD_ZERO(&wfd);
			FD_SET(fd, &wfd);
			ret = select(fd + 1, NULL, &wfd, NULL, &zerotv);
			if (ret == 0)
				break;
			assert(ret == 1);
			assert(FD_ISSET(fd, &wfd) != 0);
			ret = write(fd, buf, sizeof(buf));
			assert(ret > 0);
			nbytes += ret;
		}
		ret = write(bfd, &nbytes, sizeof(nbytes));
		assert(ret == sizeof(nbytes));

		proc_barrier(bfd);

		/* parent: read until empty */
	}

	proc_barrier(bfd);

	/* Test out-of-band data. */
	switch (filetype) {
#if defined(__OpenBSD__)
	case FTYPE_PTY: {
		/* parent: enable user ioctl command mode */

		proc_barrier(bfd);

		ret = write(fd, &b, 1);
		assert(ret == 1);

		if (ioctl(fd, UIOCCMD(42), NULL) == -1)
			err(1, "child: ioctl(UIOCCMD)");

		ret = write(fd, &b, 1);
		assert(ret == 1);

		proc_barrier(bfd);

		/* parent: read, and disable user ioctl command mode */

		proc_barrier(bfd);
		break;
	    }
#endif /* __OpenBSD__ */

	case FTYPE_SOCKET_TCP:
		ret = send(fd, &b, 1, 0);
		assert(ret == 1);

		ret = send(fd, &b, 1, MSG_OOB);
		assert(ret == 1);

		ret = send(fd, &b, 1, 0);
		assert(ret == 1);

		proc_barrier(bfd);

		/* parent: read */

		proc_barrier(bfd);
		break;

	default:
		break;
	}

	/* Test socket shutdown. */
	switch (filetype) {
	case FTYPE_SOCKET_TCP:
	case FTYPE_SOCKET_UNIX:
		ret = write(fd, &b, 1);
		assert(ret == 1);

		ret = shutdown(fd, SHUT_WR);
		assert(ret == 0);

		proc_barrier(bfd);

		/* parent: read and shutdown */

		proc_barrier(bfd);

		/* Let inet sockets take their time. */
		if (filetype == FTYPE_SOCKET_TCP)
			usleep(10000);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
		assert(ret == 2);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) == 0);
		break;

	case FTYPE_FIFO:
	case FTYPE_PIPE:
	case FTYPE_PTY:
	case FTYPE_SOCKET_UDP:
	default:
		break;
	}

	proc_barrier(bfd);

	close(fd);

	proc_barrier(bfd);

	fdset_init(&rfd, &wfd, &efd, fd);
	ret = select(fd + 1, &rfd, &wfd, &efd, NULL);
	assert(ret == -1);
	assert(errno == EBADF);
}

static void
proc_parent(int fd, int bfd)
{
	char buf[1024];
	fd_set efd, rfd, wfd;
	struct timeval tv = { 0, 1 };
	struct timeval zerotv = { 0, 0 };
	size_t nbytes;
	int ret, retries;
	char b = 0;

	proc_barrier(bfd);

	fdset_init(&rfd, &wfd, &efd, fd);
	if (filetype == FTYPE_FIFO || filetype == FTYPE_PIPE)
		FD_CLR(fd, &wfd);
	ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
	switch (filetype) {
	case FTYPE_FIFO:
	case FTYPE_PIPE:
		assert(ret == 0);
		assert(FD_ISSET(fd, &rfd) == 0);
		assert(FD_ISSET(fd, &wfd) == 0);
		assert(FD_ISSET(fd, &efd) == 0);
		break;
	default:
		assert(ret == 1);
		assert(FD_ISSET(fd, &rfd) == 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) == 0);
		break;
	}

	proc_barrier(bfd);

	/* child: write */

	proc_barrier(bfd);

	/* Let inet sockets take their time. */
	if (filetype == FTYPE_SOCKET_TCP ||
	    filetype == FTYPE_SOCKET_UDP)
		usleep(10000);

	fdset_init(&rfd, &wfd, &efd, fd);
	if (filetype == FTYPE_FIFO || filetype == FTYPE_PIPE)
		FD_CLR(fd, &wfd);
	ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
	switch (filetype) {
	case FTYPE_FIFO:
	case FTYPE_PIPE:
		assert(ret == 1);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) == 0);
		assert(FD_ISSET(fd, &efd) == 0);
		break;
	case FTYPE_PTY:
	case FTYPE_SOCKET_TCP:
	case FTYPE_SOCKET_UDP:
	case FTYPE_SOCKET_UNIX:
		assert(ret == 2);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) == 0);
		break;
	default:
		assert(0);
	}

	proc_barrier(bfd);

	ret = read(fd, &b, 1);
	assert(ret == 1);

	fdset_init(&rfd, &wfd, &efd, fd);
	ret = select(fd + 1, &rfd, NULL, NULL, &tv);
	assert(ret == 0);

	proc_barrier(bfd);

	if (filetype != FTYPE_SOCKET_UDP) {
		/* child: write until full */
		nbytes = 0;
		ret = read(bfd, &nbytes, sizeof(nbytes));
		assert(ret == sizeof(nbytes));

		proc_barrier(bfd);

		/* read until empty */
		retries = 5;
		while (retries > 0) {
			FD_ZERO(&rfd);
			FD_SET(fd, &rfd);
			ret = select(fd + 1, &rfd, NULL, NULL, &zerotv);
			if (ret == 0) {
				retries--;
				/* Let inet sockets take their time. */
				if (nbytes > 0 && retries > 0)
					usleep(10000);
				continue;
			}
			assert(ret == 1);
			assert(FD_ISSET(fd, &rfd) != 0);
			assert(nbytes > 0);
			ret = read(fd, buf, MIN(sizeof(buf), nbytes));
			assert(ret > 0);
			nbytes -= ret;
		}
		assert(nbytes == 0);
	}

	proc_barrier(bfd);

	/* Test out-of-band data. */
	switch (filetype) {
#if defined(__OpenBSD__)
	case FTYPE_PTY: {
		int off = 0;
		int on = 1;

		if (ioctl(fd, TIOCUCNTL, &on) == -1)
			err(1, "parent: ioctl(TIOCUCNTL, 1)");

		proc_barrier(bfd);

		/* child: write */

		proc_barrier(bfd);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
		assert(ret == 3);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) != 0);

		/* Read out-of-band data. */
		ret = read(fd, buf, sizeof(buf));
		assert(ret == 1);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
		assert(ret == 2);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) == 0);

		/* Read normal data. */
		ret = read(fd, buf, sizeof(buf));
		assert(ret == 3);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
		assert(ret == 1);
		assert(FD_ISSET(fd, &rfd) == 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) == 0);

		if (ioctl(fd, TIOCUCNTL, &off) == -1)
			err(1, "parent: ioctl(TIOCUCNTL, 0)");

		proc_barrier(bfd);
		break;
	    }
#endif /* __OpenBSD__ */

	case FTYPE_SOCKET_TCP: {
		int atmark;
		int on = 1;

		/* child: write */

		if (setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &on,
		    sizeof(on)) == -1)
			err(1, "parent: setsockopt(SO_OOBINLINE)");

		proc_barrier(bfd);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
		assert(ret == 3);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) != 0);

		/* Read normal data. */
		atmark = 0;
		if (ioctl(fd, SIOCATMARK, &atmark) == -1)
			err(1, "parent: ioctl(SIOCATMARK)");
		assert(atmark == 0);
		ret = recv(fd, buf, sizeof(buf), 0);
		assert(ret == 1);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
		assert(ret == 3);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) != 0);

		/* Read out-of-band data. */
		atmark = 0;
		if (ioctl(fd, SIOCATMARK, &atmark) == -1)
			err(1, "parent: ioctl(SIOCATMARK)");
		assert(atmark != 0);
		ret = recv(fd, &b, 1, 0);
		assert(ret == 1);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
		assert(ret == 2);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) == 0);

		/* Read normal data. */
		atmark = 0;
		if (ioctl(fd, SIOCATMARK, &atmark) == -1)
			err(1, "parent: ioctl(SIOCATMARK)");
		assert(atmark == 0);
		ret = recv(fd, buf, sizeof(buf), 0);
		assert(ret == 1);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
		assert(ret == 1);
		assert(FD_ISSET(fd, &rfd) == 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) == 0);

		proc_barrier(bfd);
		break;
	    }

	default:
		break;
	}

	/* Test socket shutdown. */
	switch (filetype) {
	case FTYPE_SOCKET_TCP:
	case FTYPE_SOCKET_UNIX:
		/* child: write and shutdown */

		proc_barrier(bfd);

		/* Let inet sockets take their time. */
		if (filetype == FTYPE_SOCKET_TCP)
			usleep(10000);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, NULL, NULL, &tv);
		assert(ret == 1);
		assert(FD_ISSET(fd, &rfd) != 0);

		ret = read(fd, &b, 1);
		assert(ret == 1);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, NULL, NULL, &tv);
		assert(ret == 1);
		assert(FD_ISSET(fd, &rfd) != 0);

		ret = read(fd, &b, 1);
		assert(ret == 0);

		ret = shutdown(fd, SHUT_WR);
		assert(ret == 0);

		proc_barrier(bfd);

		fdset_init(&rfd, &wfd, &efd, fd);
		ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
		assert(ret == 2);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) == 0);
		break;

	case FTYPE_FIFO:
	case FTYPE_PIPE:
	case FTYPE_PTY:
	case FTYPE_SOCKET_UDP:
	default:
		break;
	}

	proc_barrier(bfd);

	/* child: close */

	proc_barrier(bfd);

	fdset_init(&rfd, &wfd, &efd, fd);
	if (filetype == FTYPE_FIFO || filetype == FTYPE_PIPE)
		FD_CLR(fd, &wfd);
	ret = select(fd + 1, &rfd, &wfd, &efd, &tv);
	switch (filetype) {
	case FTYPE_FIFO:
	case FTYPE_PIPE:
		assert(ret == 1);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) == 0);
		assert(FD_ISSET(fd, &efd) == 0);
		break;
	case FTYPE_PTY:
	case FTYPE_SOCKET_TCP:
	case FTYPE_SOCKET_UNIX:
		assert(ret == 2);
		assert(FD_ISSET(fd, &rfd) != 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) == 0);
		break;
	case FTYPE_SOCKET_UDP:
		assert(ret == 1);
		assert(FD_ISSET(fd, &rfd) == 0);
		assert(FD_ISSET(fd, &wfd) != 0);
		assert(FD_ISSET(fd, &efd) == 0);
		break;
	default:
		assert(0);
	}

	close(fd);
}

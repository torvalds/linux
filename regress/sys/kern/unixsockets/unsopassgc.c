/* $OpenBSD: unsopassgc.c,v 1.4 2021/12/29 00:04:35 mvs Exp $ */

/*
 * Copyright (c) 2021 Vitaliy Makkoveev <mvs@openbsd.org>
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
 * Try to beak unix(4) sockets garbage collector and make it to clean
 * `so_rcv' buffer of alive socket. Successful breakage should produce
 * kernel panic.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t therr_mtx = PTHREAD_MUTEX_INITIALIZER;

static void
therr(int eval, const char *fmt, ...)
{
	va_list ap;

	pthread_mutex_lock(&therr_mtx);

	va_start(ap, fmt);
	verr(eval, fmt, ap);
	va_end(ap);
}

static void
therrx(int eval, const char *fmt, ...)
{
	va_list ap;

	pthread_mutex_lock(&therr_mtx);

	va_start(ap, fmt);
	verrx(eval, fmt, ap);
	va_end(ap);
}

static void
therrc(int eval, int code, const char *fmt, ...)
{
	va_list ap;

	pthread_mutex_lock(&therr_mtx);

	va_start(ap, fmt);
	verrc(eval, code, fmt, ap);
	va_end(ap);
}

#define PASSFD_NUM (4)

union msg_control {
	struct cmsghdr cmsgh;
	char control[CMSG_SPACE(sizeof(int) * PASSFD_NUM)];
};

static struct thr_pass_arg {
	int s[2];
	int passfd;
} *thr_pass_args;

static struct thr_gc_arg {
	int passfd;
} *thr_gc_arg;

static void *
thr_send(void *arg)
{
	union msg_control msg_control;
	int iov_buf;
	struct iovec iov;
	struct msghdr msgh;
	struct cmsghdr *cmsgh;
	int *s = ((struct thr_pass_arg *)arg)->s;
	int passfd = ((struct thr_pass_arg *)arg)->passfd;

	while (1) {
		iov_buf = 0;
		iov.iov_base = &iov_buf;
		iov.iov_len = sizeof(iov_buf);
		msgh.msg_control = msg_control.control;
		msgh.msg_controllen = sizeof(msg_control.control);
		msgh.msg_iov = &iov;
		msgh.msg_iovlen = 1;
		msgh.msg_name = NULL;
		msgh.msg_namelen = 0;
		cmsgh = CMSG_FIRSTHDR(&msgh);
		cmsgh->cmsg_len = CMSG_LEN(sizeof(int) * PASSFD_NUM);
		cmsgh->cmsg_level = SOL_SOCKET;
		cmsgh->cmsg_type = SCM_RIGHTS;
		*((int *)CMSG_DATA(cmsgh) + 0) = s[0];
		*((int *)CMSG_DATA(cmsgh) + 1) = s[1];
		*((int *)CMSG_DATA(cmsgh) + 2) = passfd;
		*((int *)CMSG_DATA(cmsgh) + 3) = passfd;

		if (sendmsg(s[0], &msgh, 0) < 0) {
			switch (errno) {
			case EMFILE:
			case ENOBUFS:
				break;
			default:
				therr(1, "sendmsg");
			}
		}
	}

	return NULL;
}

static void *
thr_recv(void *arg)
{
	union msg_control msg_control;
	int iov_buf;
	struct iovec iov;
	struct msghdr msgh;
	struct cmsghdr *cmsgh;
	int i, fd;
	int *s = ((struct thr_pass_arg *)arg)->s;

	while (1) {
		msg_control.cmsgh.cmsg_level = SOL_SOCKET;
		msg_control.cmsgh.cmsg_type = SCM_RIGHTS;
		msg_control.cmsgh.cmsg_len =
		    CMSG_LEN(sizeof(int) * PASSFD_NUM);

		iov.iov_base = &iov_buf;
		iov.iov_len = sizeof(iov_buf);

		msgh.msg_control = msg_control.control;
		msgh.msg_controllen = sizeof(msg_control.control);
		msgh.msg_iov = &iov;
		msgh.msg_iovlen = 1;
		msgh.msg_name = NULL;
		msgh.msg_namelen = 0;

		if(recvmsg(s[1], &msgh, 0) < 0)
			therr(1, "recvmsg");

		if(!(cmsgh = CMSG_FIRSTHDR(&msgh)))
			therrx(1, "bad cmsg header");
		if(cmsgh->cmsg_level != SOL_SOCKET)
			therrx(1, "bad cmsg level");
		if(cmsgh->cmsg_type != SCM_RIGHTS)
			therrx(1, "bad cmsg type");
		if(cmsgh->cmsg_len != CMSG_LEN(sizeof(fd) * PASSFD_NUM))
			therrx(1, "bad cmsg length");

		for (i = 0; i < PASSFD_NUM; ++i) {
			fd = *((int *)CMSG_DATA(cmsgh) + i);
			close(fd);
		}
	}

	return NULL;
}

static void *
thr_dispose(void *arg)
{
	uint8_t buf[sizeof(union msg_control)];
	int *s = ((struct thr_pass_arg *)arg)->s;

	while (1) {
		if (read(s[1], buf, sizeof(buf)) < 0)
			therr(1, "read");
	}

	return NULL;
}

static void *
thr_gc(void *arg)
{
	union msg_control msg_control;
	int iov_buf;
	struct iovec iov;
	struct msghdr msgh;
	struct cmsghdr *cmsgh;
	int s[2], passfd = ((struct thr_gc_arg *)arg)->passfd;

	while (1) {
		if (socketpair(AF_UNIX, SOCK_DGRAM, 0, s) < 0)
			therr(1, "socketpair");

		iov_buf = 0;
		iov.iov_base = &iov_buf;
		iov.iov_len = sizeof(iov_buf);
		msgh.msg_control = msg_control.control;
		msgh.msg_controllen = sizeof(msg_control.control);
		msgh.msg_iov = &iov;
		msgh.msg_iovlen = 1;
		msgh.msg_name = NULL;
		msgh.msg_namelen = 0;
		cmsgh = CMSG_FIRSTHDR(&msgh);
		cmsgh->cmsg_len = CMSG_LEN(sizeof(int) * PASSFD_NUM);
		cmsgh->cmsg_level = SOL_SOCKET;
		cmsgh->cmsg_type = SCM_RIGHTS;
		*((int *)CMSG_DATA(cmsgh) + 0) = s[0];
		*((int *)CMSG_DATA(cmsgh) + 1) = s[1];
		*((int *)CMSG_DATA(cmsgh) + 2) = passfd;
		*((int *)CMSG_DATA(cmsgh) + 3) = passfd;

		if (sendmsg(s[0], &msgh, 0) < 0) {
			switch (errno) {
			case EMFILE:
			case ENOBUFS:
				break;
			default:
				therr(1, "sendmsg");
			}
		}

		close(s[0]);
		close(s[1]);
	}

	return NULL;
}

int
main(int argc, char *argv[])
{
	struct timespec testtime = {
		.tv_sec = 60,
		.tv_nsec = 0,
	};

	int mib[2], ncpu;
	size_t len;

	pthread_t thr;
	int i, error;

	if (argc == 2 && !strcmp(argv[1], "--infinite"))
		testtime.tv_sec = (10 * 365 * 86400);

	mib[0] = CTL_HW;
	mib[1] = HW_NCPUONLINE;
	len = sizeof(ncpu);

	if (sysctl(mib, 2, &ncpu, &len, NULL, 0) < 0)
		err(1, "sysctl");
	if (ncpu <= 0)
		errx(1, "Wrong number of CPUs online: %d", ncpu);

	if (!(thr_pass_args = calloc(ncpu, sizeof(*thr_pass_args))))
		err(1, "malloc");
	if (!(thr_gc_arg = malloc(sizeof(*thr_gc_arg))))
		err(1, "malloc");

	for (i = 0; i < ncpu; ++i) {
		if (socketpair(AF_UNIX, SOCK_DGRAM, 0, thr_pass_args[i].s) < 0)
			err(1, "socketpair");
		thr_pass_args[i].passfd = thr_pass_args[i].s[0];
	}

	thr_gc_arg->passfd = thr_pass_args[0].s[0];

	for (i = 0; i < ncpu; ++i) {
		error = pthread_create(&thr, NULL,
		    thr_send, &thr_pass_args[i]);
		if (error)
			therrc(1, error, "pthread_create");
		error = pthread_create(&thr, NULL,
		    thr_recv, &thr_pass_args[i]);
		if (error)
			therrc(1, error, "pthread_create");
		error = pthread_create(&thr, NULL,
		    thr_dispose, &thr_pass_args[i]);
		if (error)
			therrc(1, error, "pthread_create");
	}

	if ((error = pthread_create(&thr, NULL, thr_gc, thr_gc_arg)))
		therrc(1, error, "pthread_create");

	nanosleep(&testtime, NULL);

	return 0;
}

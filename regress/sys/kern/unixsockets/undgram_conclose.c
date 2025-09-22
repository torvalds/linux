/* $OpenBSD: undgram_conclose.c,v 1.1 2021/12/10 00:33:25 mvs Exp $ */

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
 * Try to kill the datagram socket connected to the dying socket while
 * it cleaning it's list of connected sockets. Incorrect handling of
 * this case could produce kernel crash.
 */

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <stdarg.h>
#include <stdio.h>
#include <err.h>
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
therrc(int eval, int code, const char *fmt, ...)
{
	va_list ap;

	pthread_mutex_lock(&therr_mtx);

	va_start(ap, fmt);
	verrc(eval, code, fmt, ap);
	va_end(ap);
}

static void *
thr_close(void *arg)
{
	struct sockaddr_un *sun = arg;
	int s;

	while (1) {
		unlink(sun->sun_path);

		if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
			therr(1, "socket");
		if (bind(s, (struct sockaddr *)sun, sizeof(*sun)) < 0)
			therr(1, "bind");
		close(s);
	}

	return NULL;
}

static void *
thr_conn(void *arg)
{
	struct sockaddr_un *sun = arg;
	int s;

	while (1) {
		if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
			therr(1, "socket");
		connect(s, (struct sockaddr *)sun, sizeof(*sun));
		close(s);
	}

	return NULL;
}

static struct sockaddr_un sun;

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
	int error, i;

	umask(0077);

	if (argc == 2 && !strcmp(argv[1], "--infinite"))
		testtime.tv_sec = (10 * 365 * 86400);

	mib[0] = CTL_HW;
	mib[1] = HW_NCPUONLINE;
	len = sizeof(ncpu);

	if (sysctl(mib, 2, &ncpu, &len, NULL, 0) < 0)
		err(1, "sysctl");
	if (ncpu <= 0)
		errx(1, "Wrong number of CPUs online: %d", ncpu);

	memset(&sun, 0, sizeof(sun));
	sun.sun_len = sizeof(sun);
	sun.sun_family = AF_UNIX;
	snprintf(sun.sun_path, sizeof(sun.sun_path) - 1,
	    "undgram_conclose.socket");

	if ((error = pthread_create(&thr, NULL, thr_close, &sun)))
		therrc(1, error, "pthread_create");

	for (i = 0; i < (ncpu * 4); ++i) {
		if ((error = pthread_create(&thr, NULL, thr_conn, &sun)))
			therrc(1, error, "pthread_create");
	}

	nanosleep(&testtime, NULL);

	return 0;
}

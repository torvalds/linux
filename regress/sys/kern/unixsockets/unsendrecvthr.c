/* $OpenBSD: unsendrecvthr.c,v 1.2 2023/07/09 09:33:30 bluhm Exp $ */

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
 * Create the pair of SOCK_SEQPACKET sockets and perform #count_of_cpus
 * simultaneous writes on each of them. In half of transmissions the
 * sockets will be re-locked in kernel space. Be sure no data corruption
 * or loss.
 */

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

struct data {
	int id;
	unsigned int cnt;
};

struct thr_tx_arg {
	int s;
	int id;
};

struct rx_data {
	unsigned int cnt;
};

struct thr_rx_arg {
	int s;
	int rx_data_num;
	struct rx_data *rx_data;
};

static void *
thr_tx(void *arg)
{
	struct data data;
	int s = ((struct thr_tx_arg *)arg)->s;

	data.id = ((struct thr_tx_arg *)arg)->id;
	data.cnt = 1;

	while (1) {
		ssize_t ret;

		if ((ret = send(s, &data, sizeof(data), 0)) < 0)
			therr(1, "send");
		if (ret != sizeof(data))
			therrx(1, "send: wrong data size");

		data.cnt++;
	}

	return NULL;
}

static void *
thr_rx(void *arg)
{
	int s = ((struct thr_rx_arg *)arg)->s;
	int rx_data_num = ((struct thr_rx_arg *)arg)->rx_data_num;
	struct rx_data *rx_data = ((struct thr_rx_arg *)arg)->rx_data;

	while (1) {
		struct data data;
		ssize_t ret;

		if ((ret = recv(s, &data, sizeof(data), 0)) < 0)
			therr(1, "recv");
		if (ret != sizeof(data))
			therrx(1, "recv: wrong data size");

		if (data.id >= rx_data_num)
			therrx(1, "recv: wrong id");

		if (data.cnt != (unsigned int)(rx_data[data.id].cnt + 1)) {
			therrx(1, "recv: data loss %d -> %d",
			    rx_data[data.id].cnt, data.cnt);
		}
		rx_data[data.id].cnt = data.cnt;
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

	struct rx_data *rx_data[2];
	struct thr_rx_arg rx_args[2];
	struct thr_tx_arg *tx_args[2];

	int s[2], i, j;

	if (argc == 2 && !strcmp(argv[1], "--infinite"))
		testtime.tv_sec = (10 * 365 * 86400);

	mib[0] = CTL_HW;
	mib[1] = HW_NCPUONLINE;
	len = sizeof(ncpu);

	if (sysctl(mib, 2, &ncpu, &len, NULL, 0) < 0)
		err(1, "sysctl");
	if (ncpu <= 0)
		errx(1, "Wrong number of CPUs online: %d", ncpu);

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, s) < 0)
		err(1, "socketpair");

	for (i = 0; i < 2; ++i) {
		if (!(rx_data[i] = calloc(ncpu, sizeof(struct rx_data))))
			err(1, "calloc");

		for (j = 0; j < ncpu; ++j)
			rx_data[i][j].cnt = 0;
	}

	for (i = 0; i < 2; ++i) {
		rx_args[i].s = s[i];
		rx_args[i].rx_data_num = ncpu;
		rx_args[i].rx_data = rx_data[i];
	}

	for (i = 0; i < 2; ++i) {
		if (!(tx_args[i] = calloc(ncpu, sizeof(struct thr_tx_arg))))
			err(1, "calloc");

		for (j = 0; j < ncpu; ++j) {
			tx_args[i][j].s = s[i];
			tx_args[i][j].id = j;
		}
	}

	for (i = 0; i < 2; ++i) {
		pthread_t thr;
		int error;

		error = pthread_create(&thr, NULL, thr_rx, &rx_args[i]);
		if (error)
			therrc(1, error, "pthread_create");
	}

	for (i = 0; i < 2; ++i) {
		pthread_t thr;
		int error;

		for (j = 0; j < ncpu; ++j) {
			error = pthread_create(&thr, NULL,
			    thr_tx, &tx_args[i][j]);
			if (error)
				therrc(1, error, "pthread_create");
		}
	}

	nanosleep(&testtime, NULL);

	return 0;
}

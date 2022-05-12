// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

/* Test listening on the same port 443 with multiple VIPS.
 * Each VIP:443 will have multiple sk listening on by using
 * SO_REUSEPORT.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>

#define IP6_LADDR_START "2401:dead::1"
#define IP6_LPORT 443
#define NSEC_PER_SEC 1000000000L
#define NSEC_PER_USEC 1000L

static unsigned int nr_socks_per_vip;
static unsigned int nr_vips;

static int *bind_reuseport_sock6(void)
{
	int *lfds, *cur_fd, err, optvalue = 1;
	struct sockaddr_in6 sa6 = {};
	unsigned int i, j;

	sa6.sin6_family = AF_INET6;
	sa6.sin6_port = htons(IP6_LPORT);
	err = inet_pton(AF_INET6, IP6_LADDR_START, &sa6.sin6_addr);
	if (err != 1)
		error(1, err, "inet_pton(%s)", IP6_LADDR_START);

	lfds = malloc(nr_vips * nr_socks_per_vip * sizeof(lfds[0]));
	if (!lfds)
		error(1, errno, "cannot alloc array of lfds");

	cur_fd = lfds;
	for (i = 0; i < nr_vips; i++) {
		for (j = 0; j < nr_socks_per_vip; j++) {
			*cur_fd = socket(AF_INET6, SOCK_STREAM, 0);
			if (*cur_fd == -1)
				error(1, errno,
				      "lfds[%u,%u] = socket(AF_INET6)", i, j);

			err = setsockopt(*cur_fd, SOL_SOCKET, SO_REUSEPORT,
					 &optvalue, sizeof(optvalue));
			if (err)
				error(1, errno,
				      "setsockopt(lfds[%u,%u], SO_REUSEPORT)",
				      i, j);

			err = bind(*cur_fd, (struct sockaddr *)&sa6,
				   sizeof(sa6));
			if (err)
				error(1, errno, "bind(lfds[%u,%u])", i, j);
			cur_fd++;
		}
		sa6.sin6_addr.s6_addr32[3]++;
	}

	return lfds;
}

int main(int argc, const char *argv[])
{
	struct timespec start_ts, end_ts;
	unsigned long start_ns, end_ns;
	unsigned int nr_lsocks;
	int *lfds, i, err;

	if (argc != 3 || atoi(argv[1]) <= 0 || atoi(argv[2]) <= 0)
		error(1, 0, "Usage: %s <nr_vips> <nr_socks_per_vip>\n",
		      argv[0]);

	nr_vips = atoi(argv[1]);
	nr_socks_per_vip = atoi(argv[2]);
	nr_lsocks = nr_vips * nr_socks_per_vip;
	lfds = bind_reuseport_sock6();

	clock_gettime(CLOCK_MONOTONIC, &start_ts);
	for (i = 0; i < nr_lsocks; i++) {
		err = listen(lfds[i], 0);
		if (err)
			error(1, errno, "listen(lfds[%d])", i);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_ts);

	start_ns = start_ts.tv_sec * NSEC_PER_SEC + start_ts.tv_nsec;
	end_ns = end_ts.tv_sec * NSEC_PER_SEC + end_ts.tv_nsec;

	printf("listen %d socks took %lu.%lu\n", nr_lsocks,
	       (end_ns - start_ns) / NSEC_PER_SEC,
	       (end_ns - start_ns) / NSEC_PER_USEC);

	for (i = 0; i < nr_lsocks; i++)
		close(lfds[i]);

	free(lfds);
	return 0;
}

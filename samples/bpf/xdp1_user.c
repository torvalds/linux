/* Copyright (c) 2016 PLUMgrid
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/bpf.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bpf_load.h"
#include "bpf_util.h"
#include "libbpf.h"

static int ifindex;

static void int_exit(int sig)
{
	set_link_xdp_fd(ifindex, -1);
	exit(0);
}

/* simple per-protocol drop counter
 */
static void poll_stats(int interval)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	const unsigned int nr_keys = 256;
	__u64 values[nr_cpus], prev[nr_keys][nr_cpus];
	__u32 key;
	int i;

	memset(prev, 0, sizeof(prev));

	while (1) {
		sleep(interval);

		for (key = 0; key < nr_keys; key++) {
			__u64 sum = 0;

			assert(bpf_map_lookup_elem(map_fd[0], &key, values) == 0);
			for (i = 0; i < nr_cpus; i++)
				sum += (values[i] - prev[key][i]);
			if (sum)
				printf("proto %u: %10llu pkt/s\n",
				       key, sum / interval);
			memcpy(prev[key], values, sizeof(values));
		}
	}
}

int main(int ac, char **argv)
{
	char filename[256];

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (ac != 2) {
		printf("usage: %s IFINDEX\n", argv[0]);
		return 1;
	}

	ifindex = strtoul(argv[1], NULL, 0);

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	if (!prog_fd[0]) {
		printf("load_bpf_file: %s\n", strerror(errno));
		return 1;
	}

	signal(SIGINT, int_exit);

	if (set_link_xdp_fd(ifindex, prog_fd[0]) < 0) {
		printf("link set xdp fd failed\n");
		return 1;
	}

	poll_stats(2);

	return 0;
}

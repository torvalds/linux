/* Copyright (c) 2016 PLUMgrid
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/bpf.h>
#include <linux/if_link.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/resource.h>

#include "bpf_load.h"
#include "bpf_util.h"
#include "libbpf.h"

static int ifindex;
static __u32 xdp_flags;

static void int_exit(int sig)
{
	bpf_set_link_xdp_fd(ifindex, -1, xdp_flags);
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

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [OPTS] IFINDEX\n\n"
		"OPTS:\n"
		"    -S    use skb-mode\n"
		"    -N    enforce native mode\n",
		prog);
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	const char *optstr = "SN";
	char filename[256];
	int opt;

	while ((opt = getopt(argc, argv, optstr)) != -1) {
		switch (opt) {
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
			break;
		case 'N':
			xdp_flags |= XDP_FLAGS_DRV_MODE;
			break;
		default:
			usage(basename(argv[0]));
			return 1;
		}
	}

	if (optind == argc) {
		usage(basename(argv[0]));
		return 1;
	}

	if (setrlimit(RLIMIT_MEMLOCK, &r)) {
		perror("setrlimit(RLIMIT_MEMLOCK)");
		return 1;
	}

	ifindex = strtoul(argv[optind], NULL, 0);

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	if (!prog_fd[0]) {
		printf("load_bpf_file: %s\n", strerror(errno));
		return 1;
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	if (bpf_set_link_xdp_fd(ifindex, prog_fd[0], xdp_flags) < 0) {
		printf("link set xdp fd failed\n");
		return 1;
	}

	poll_stats(2);

	return 0;
}

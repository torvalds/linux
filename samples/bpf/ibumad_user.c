// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB

/**
 * ibumad BPF sample user side
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * Copyright(c) 2018 Ira Weiny, Intel Corporation
 */

#include <linux/bpf.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>

#include <sys/resource.h>
#include <getopt.h>
#include <net/if.h>

#include "bpf_load.h"
#include "bpf_util.h"
#include <bpf/libbpf.h>

static void dump_counts(int fd)
{
	__u32 key;
	__u64 value;

	for (key = 0; key < 256; key++) {
		if (bpf_map_lookup_elem(fd, &key, &value)) {
			printf("failed to read key %u\n", key);
			continue;
		}
		if (value)
			printf("0x%02x : %llu\n", key, value);
	}
}

static void dump_all_counts(void)
{
	printf("Read 'Class : count'\n");
	dump_counts(map_fd[0]);
	printf("Write 'Class : count'\n");
	dump_counts(map_fd[1]);
}

static void dump_exit(int sig)
{
	dump_all_counts();
	exit(0);
}

static const struct option long_options[] = {
	{"help",      no_argument,       NULL, 'h'},
	{"delay",     required_argument, NULL, 'd'},
};

static void usage(char *cmd)
{
	printf("eBPF test program to count packets from various IP addresses\n"
		"Usage: %s <options>\n"
		"       --help,   -h  this menu\n"
		"       --delay,  -d  <delay>  wait <delay> sec between prints [1 - 1000000]\n"
		, cmd
		);
}

int main(int argc, char **argv)
{
	unsigned long delay = 5;
	int longindex = 0;
	int opt;
	char bpf_file[256];

	/* Create the eBPF kernel code path name.
	 * This follows the pattern of all of the other bpf samples
	 */
	snprintf(bpf_file, sizeof(bpf_file), "%s_kern.o", argv[0]);

	/* Do one final dump when exiting */
	signal(SIGINT, dump_exit);
	signal(SIGTERM, dump_exit);

	while ((opt = getopt_long(argc, argv, "hd:rSw",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'd':
			delay = strtoul(optarg, NULL, 0);
			if (delay == ULONG_MAX || delay < 0 ||
			    delay > 1000000) {
				fprintf(stderr, "ERROR: invalid delay : %s\n",
					optarg);
				usage(argv[0]);
				return 1;
			}
			break;
		default:
		case 'h':
			usage(argv[0]);
			return 1;
		}
	}

	if (load_bpf_file(bpf_file)) {
		fprintf(stderr, "ERROR: failed to load eBPF from file : %s\n",
			bpf_file);
		return 1;
	}

	while (1) {
		sleep(delay);
		dump_all_counts();
	}

	return 0;
}

// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-18 David Ahern <dsahern@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#include <linux/bpf.h>
#include <linux/if_link.h>
#include <linux/limits.h>
#include <net/if.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>

#include "bpf_load.h"
#include "bpf_util.h"
#include <bpf/bpf.h>


static int do_attach(int idx, int fd, const char *name)
{
	int err;

	err = bpf_set_link_xdp_fd(idx, fd, 0);
	if (err < 0)
		printf("ERROR: failed to attach program to %s\n", name);

	return err;
}

static int do_detach(int idx, const char *name)
{
	int err;

	err = bpf_set_link_xdp_fd(idx, -1, 0);
	if (err < 0)
		printf("ERROR: failed to detach program from %s\n", name);

	return err;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [OPTS] interface-list\n"
		"\nOPTS:\n"
		"    -d    detach program\n"
		"    -D    direct table lookups (skip fib rules)\n",
		prog);
}

int main(int argc, char **argv)
{
	char filename[PATH_MAX];
	int opt, i, idx, err;
	int prog_id = 0;
	int attach = 1;
	int ret = 0;

	while ((opt = getopt(argc, argv, ":dD")) != -1) {
		switch (opt) {
		case 'd':
			attach = 0;
			break;
		case 'D':
			prog_id = 1;
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

	if (attach) {
		snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

		if (access(filename, O_RDONLY) < 0) {
			printf("error accessing file %s: %s\n",
				filename, strerror(errno));
			return 1;
		}

		if (load_bpf_file(filename)) {
			printf("%s", bpf_log_buf);
			return 1;
		}

		if (!prog_fd[prog_id]) {
			printf("load_bpf_file: %s\n", strerror(errno));
			return 1;
		}
	}
	if (attach) {
		for (i = 1; i < 64; ++i)
			bpf_map_update_elem(map_fd[0], &i, &i, 0);
	}

	for (i = optind; i < argc; ++i) {
		idx = if_nametoindex(argv[i]);
		if (!idx)
			idx = strtoul(argv[i], NULL, 0);

		if (!idx) {
			fprintf(stderr, "Invalid arg\n");
			return 1;
		}
		if (!attach) {
			err = do_detach(idx, argv[i]);
			if (err)
				ret = err;
		} else {
			err = do_attach(idx, prog_fd[prog_id], argv[i]);
			if (err)
				ret = err;
		}
	}

	return ret;
}
